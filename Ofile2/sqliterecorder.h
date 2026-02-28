#ifndef SQLITERECORDER_H
#define SQLITERECORDER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include <QList>
#include <vector>
#include <utility>

struct CurveOption;

class SqliteRecorder : public QObject
{
    Q_OBJECT
public:
    explicit SqliteRecorder(QObject *parent = nullptr);
    ~SqliteRecorder() override;

    bool start(const QString &sqliteFilePath, const QList<CurveOption> &options);
    void stop();
    bool isActive() const;
    QString filePath() const { return m_filePath; }

    void recordSample(const QDateTime &ts, const std::vector<std::pair<int, double>> &sampleData);

private:
    bool ensureSchema();
    void upsertChannels(const QList<CurveOption> &options);

private:
    QString m_connectionName;
    QString m_filePath;
    QSqlDatabase m_db;
    QSqlQuery m_insertQuery;
    bool m_prepared = false;
    int m_samplesSinceCheckpoint = 0;
};

#endif // SQLITERECORDER_H

