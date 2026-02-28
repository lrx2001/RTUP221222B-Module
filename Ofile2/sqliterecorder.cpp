#include "sqliterecorder.h"
#include "curvesdialog.h" // for CurveOption

#include <QDir>
#include <QSqlError>
#include <QVariant>

static QString makeConnectionName()
{
    return QStringLiteral("rec_%1").arg(QString::number(QDateTime::currentMSecsSinceEpoch()));
}

SqliteRecorder::SqliteRecorder(QObject *parent)
    : QObject(parent)
{
}

SqliteRecorder::~SqliteRecorder()
{
    stop();
}

bool SqliteRecorder::isActive() const
{
    return m_db.isValid() && m_db.isOpen();
}

// 启动录制：
// - 打开 / 创建 sqlite 数据库文件；
// - 配置 WAL + 同步策略；
// - 初始化表结构与通道信息；
// - 预编译插入 samples 的 SQL。
bool SqliteRecorder::start(const QString &sqliteFilePath, const QList<CurveOption> &options)
{
    stop();

    m_connectionName = makeConnectionName();
    m_filePath = sqliteFilePath;

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_filePath);
    if (!m_db.open()) {
        stop();
        return false;
    }

    // 基本性能/可靠性设置：使用 WAL 日志 + FULL 同步级别，增强异常断电/退出时的安全性。
    // 说明：
    // - journal_mode=WAL：写入先落在 .sqlite-wal 中，定期 checkpoint 合并到主库。
    // - synchronous=FULL：每次提交事务都会尽量把数据真正落盘，牺牲一点性能换可靠性。
    // - temp_store=MEMORY：临时表放内存，减少磁盘 IO。
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL;"));
        // 为了尽量保证异常退出后数据完整性，这里改为 FULL，同步更安全（略牺牲一点性能）
        q.exec(QStringLiteral("PRAGMA synchronous=FULL;"));
        q.exec(QStringLiteral("PRAGMA temp_store=MEMORY;"));
    }

    if (!ensureSchema()) {
        stop();
        return false;
    }

    upsertChannels(options);

    // 预编译插入语句
    m_insertQuery = QSqlQuery(m_db);
    m_prepared = m_insertQuery.prepare(QStringLiteral(
        "INSERT INTO samples(ts_ms, curve_key, value) VALUES(?, ?, ?);"));

    return true;
}

// 停止录制：
// - 关闭当前连接；
// - 在关闭前执行一次 FULL checkpoint，确保 .sqlite-wal 中的内容尽可能刷回主库。
void SqliteRecorder::stop()
{
    m_prepared = false;
    m_insertQuery = QSqlQuery();

    if (m_db.isValid()) {
        if (m_db.isOpen()) {
            // 在关闭前做一次 WAL checkpoint，把 -wal 内容刷回主库文件
            QSqlQuery q(m_db);
            q.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL);"));
            m_db.close();
        }
    }

    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    m_connectionName.clear();
    m_filePath.clear();
    m_db = QSqlDatabase();
}

// 确保数据库中存在所需的表结构：
// - meta    : 元数据（当前仅存 created_at）；
// - channels: 每个 curve_key 对应的中文名和单位，用于回放界面显示；
// - samples : 时间戳 + curve_key + 采样值 的明细表；
// 并为常用查询创建索引。
bool SqliteRecorder::ensureSchema()
{
    QSqlQuery q(m_db);

    const char *ddl[] = {
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");",

        "CREATE TABLE IF NOT EXISTS channels ("
        "  curve_key INTEGER PRIMARY KEY,"
        "  name TEXT,"
        "  unit TEXT"
        ");",

        "CREATE TABLE IF NOT EXISTS samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts_ms INTEGER NOT NULL,"
        "  curve_key INTEGER NOT NULL,"
        "  value REAL NOT NULL"
        ");",

        "CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts_ms);",
        "CREATE INDEX IF NOT EXISTS idx_samples_key_ts ON samples(curve_key, ts_ms);",
        nullptr
    };

    for (int i = 0; ddl[i]; ++i) {
        if (!q.exec(QString::fromUtf8(ddl[i]))) {
            return false;
        }
    }

    // 写入创建时间
    {
        QSqlQuery m(m_db);
        m.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES('created_at', ?);"));
        m.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        m.exec();
    }

    return true;
}

// 将当前曲线配置写入 channels 表：
// - 如果已存在相同 curve_key，则更新名称/单位；
// - 这样回放时即便 UI 改了名字，也能按录制时的名称展示。
void SqliteRecorder::upsertChannels(const QList<CurveOption> &options)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO channels(curve_key, name, unit) VALUES(?, ?, ?);"));
    for (const CurveOption &opt : options) {
        q.addBindValue(opt.curveKey);
        q.addBindValue(opt.displayName);
        q.addBindValue(opt.unit);
        q.exec();
        q.finish();
    }
}

// 写入一帧采样数据：
// - ts: 该帧统一的采样时间（毫秒）；
// - sampleData: 若干 (curve_key, value) 对；
// - 每帧使用一次事务，防止部分写入成功部分失败。
void SqliteRecorder::recordSample(const QDateTime &ts, const std::vector<std::pair<int, double>> &sampleData)
{
    if (!isActive() || !m_prepared) return;
    if (sampleData.empty()) return;

    const qint64 tsMs = ts.toMSecsSinceEpoch();

    if (!m_db.transaction())
        return;

    for (const auto &kv : sampleData) {
        m_insertQuery.addBindValue(tsMs);
        m_insertQuery.addBindValue(kv.first);
        m_insertQuery.addBindValue(kv.second);
        if (!m_insertQuery.exec()) {
            m_db.rollback();
            return;
        }
        m_insertQuery.finish();
    }

    m_db.commit();

    // 周期性执行 WAL checkpoint，避免长时间运行时大量数据只停留在 .sqlite-wal 中：
    // - 好处：即使程序异常退出，主库 .sqlite 中也已经包含绝大部分历史数据；
    // - 副作用：偶尔有轻微的额外 IO，通常可以接受。
    m_samplesSinceCheckpoint += 1;
    if (m_samplesSinceCheckpoint >= 600) { // 例如每 600 帧左右做一次（约 10 分钟，视采样周期而定）
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE);"));
        m_samplesSinceCheckpoint = 0;
    }
}

