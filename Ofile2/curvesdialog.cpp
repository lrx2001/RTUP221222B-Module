#include "curvesdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDateTime>
#include <QColorDialog>
#include <QRegularExpression>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QRandomGenerator>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>

const QList<QColor> CurvesDialog::s_curveColors = {
    Qt::red, Qt::blue, Qt::darkGreen, Qt::magenta, Qt::darkCyan,
    Qt::darkYellow, Qt::darkMagenta, Qt::darkBlue
};

const QColor CurvesDialog::s_nameBackgroundColor(205, 210, 180);
const QColor CurvesDialog::s_nameHiddenBackgroundColor(169, 183, 98);

CurvesDialog::CurvesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("曲线显示"));
    // 允许最小化/最大化（全屏由 F11 控制）
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    setMinimumSize(1000, 550);
    resize(1100, 600);

    m_curvesWidget = new CurvesWidget(this);
    m_curvesWidget->setBackgroundColor(Qt::white);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_configBtn = new QPushButton(tr("曲线配置"), this);
    m_timeLabel = new QLabel(tr("时间: -"), this);
//    m_valueLabel = new QLabel(tr("当前值: -"), this);
    m_statusTimeLabel = new QLabel(tr("当前采样时刻: -"), this);
    m_statusDurationLabel = new QLabel(tr("已采样时长: 0 分钟"), this);

    m_slider->setMinimum(0);
    m_slider->setMaximum(0);
    // 键盘左右键一步 = 3 帧 ≈ 3 秒
    m_slider->setSingleStep(3);
    m_slider->setPageStep(30); // PageUp/PageDown 约 30 秒

    connect(m_slider, &QSlider::valueChanged, this, &CurvesDialog::onSliderValueChanged);
    connect(m_curvesWidget, &CurvesWidget::currentValueChanged, this, &CurvesDialog::onCurrentValueChanged);
    connect(m_configBtn, &QPushButton::clicked, this, &CurvesDialog::onConfigClicked);

    Layer layer0;
    layer0.name = QStringLiteral("Layer0");
    layer0.axisLabel = QStringLiteral("Bool/A V/Pluse");
    layer0.minScale = -25;
    layer0.maxScale = 125;
    m_curvesWidget->addLayer(layer0);
    Layer layer1;
    layer1.name = QStringLiteral("Layer1");
    layer1.axisLabel = QStringLiteral("500");
    layer1.minScale = -500;
    layer1.maxScale = 2500;
    m_curvesWidget->addLayer(layer1);
    Layer layer2;
    layer2.name = QStringLiteral("Layer2");
    layer2.axisLabel = QStringLiteral("°C");
    layer2.minScale = -50;
    layer2.maxScale = 250;
    m_curvesWidget->addLayer(layer2);

    m_leftPanel = new QWidget(this);
    m_leftPanel->setFixedWidth(220);
    m_leftPanel->setStyleSheet(QStringLiteral("background-color: #f5f5f5;"));
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->addWidget(new QLabel(tr("配置")));
    m_timeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    leftLayout->addWidget(m_timeLabel);
    QHBoxLayout *bgLayout = new QHBoxLayout;
    bgLayout->addWidget(new QLabel(tr("背景色:")));
    QPushButton *bgBtn = new QPushButton(tr("..."), m_leftPanel);
    connect(bgBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(Qt::white, this, tr("背景色"));
        if (c.isValid()) m_curvesWidget->setBackgroundColor(c);
    });
    bgLayout->addWidget(bgBtn);
    leftLayout->addLayout(bgLayout);
    // 通道标题 + 曲线配置按钮
    QHBoxLayout *channelHeaderLayout = new QHBoxLayout;
    QLabel *channelLabel = new QLabel(tr("通道"), m_leftPanel);
    channelHeaderLayout->addWidget(channelLabel);
    channelHeaderLayout->addStretch();
    channelHeaderLayout->addWidget(m_configBtn);
    leftLayout->addLayout(channelHeaderLayout);
    m_legendTable = new QTableWidget(m_leftPanel);
    m_legendTable->setColumnCount(2);
    m_legendTable->setHorizontalHeaderLabels(QStringList() << tr("数据名称") << tr("数值"));
    QHeaderView *header = m_legendTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setStretchLastSection(false);
    // 3:1 宽度比例：名称列占 3，数值列占 1（左侧面板固定宽 220）
    m_legendTable->setColumnWidth(0, 129);  // 约 3/4
    m_legendTable->setColumnWidth(1, 55);   // 约 1/4
    m_legendTable->verticalHeader()->setVisible(false);
    m_legendTable->setMinimumHeight(200);
    m_legendTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 使用“按住拖到另一行=互换”的自定义逻辑，避免 Qt 内置 DnD 的移动/覆盖行为
    m_legendTable->setDragEnabled(false);
    m_legendTable->setAcceptDrops(false);
    m_legendTable->setDropIndicatorShown(false);
    m_legendTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_legendTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_legendTable, &QTableWidget::customContextMenuRequested, this, &CurvesDialog::onLegendContextMenu);
    connect(m_legendTable, &QTableWidget::cellClicked, this, &CurvesDialog::onLegendCellDoubleClicked);
    // 拖拽期间暂停刷新，避免刷新改写单元格导致“弹回/丢失”
    m_legendTable->viewport()->installEventFilter(this);
    leftLayout->addWidget(m_legendTable, 1);
    m_legendTable->setRowCount(100);
    for (int i = 0; i < 100; ++i) {
        QTableWidgetItem *nameItem = new QTableWidgetItem(QStringLiteral("-"));
        nameItem->setData(Qt::UserRole, -1);
        nameItem->setBackground(s_nameBackgroundColor);
        // 名称列：只显示，不允许编辑
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_legendTable->setItem(i, 0, nameItem);
        QTableWidgetItem *valueItem = new QTableWidgetItem(QStringLiteral("-"));
        // 初始为空行：数值列背景为灰色，文字为黑色
        valueItem->setBackground(QColor(200, 200, 200));
        valueItem->setForeground(Qt::black);
        m_legendTable->setItem(i, 1, valueItem);
    }

    QHBoxLayout *topRow = new QHBoxLayout;
    topRow->addWidget(m_leftPanel);
    topRow->addWidget(m_curvesWidget, 1);

    QHBoxLayout *statusLayout = new QHBoxLayout;
    statusLayout->addWidget(m_statusTimeLabel);
    statusLayout->addSpacing(30);
    statusLayout->addWidget(m_statusDurationLabel);
    statusLayout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topRow);

    // 让进度条只在曲线框下方，从左侧曲线区域左对齐
    QHBoxLayout *sliderRow = new QHBoxLayout;
    sliderRow->addSpacing(m_leftPanel->width()); // 与左侧面板宽度对齐
    sliderRow->addWidget(m_slider, 1);
    mainLayout->addLayout(sliderRow);

    mainLayout->addLayout(statusLayout);
    setLayout(mainLayout);

    m_sampleTimer = new QTimer(this);
    connect(m_sampleTimer, &QTimer::timeout, this, &CurvesDialog::onSampleTimer);
    m_legendRefreshTimer = new QTimer(this);
    connect(m_legendRefreshTimer, &QTimer::timeout, this, &CurvesDialog::refreshLegendAndStatus);
}

bool CurvesDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_legendTable->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton) break;
            m_legendDragStartPos = me->pos();
            m_legendDragSourceRow = m_legendTable->indexAt(me->pos()).row();
            m_legendDragging = false;
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (!(me->buttons() & Qt::LeftButton)) break;
            if (m_legendDragSourceRow < 0) break;
            if (!m_legendDragging) {
                if ((me->pos() - m_legendDragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                    m_legendDragging = true;
                }
            }
            // 拖动中不做任何刷新/重排
            if (m_legendDragging) return true;
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton) break;
            if (!m_legendDragging) {
                // 不是拖拽，仅仅是点击：交给表格原逻辑（单击隐藏/显示、改颜色）
                m_legendDragSourceRow = -1;
                return QDialog::eventFilter(obj, event);
            }

            int sourceRow = m_legendDragSourceRow;
            int targetRow = m_legendTable->indexAt(me->pos()).row();
            m_legendDragging = false;
            m_legendDragSourceRow = -1;

            if (sourceRow >= 0 && sourceRow < m_legendTable->rowCount() &&
                targetRow >= 0 && targetRow < m_legendTable->rowCount() &&
                sourceRow != targetRow) {
                QTableWidgetItem *srcName = m_legendTable->item(sourceRow, 0);
                QTableWidgetItem *dstName = m_legendTable->item(targetRow, 0);
                if (srcName && dstName) {
                    int srcKey = srcName->data(Qt::UserRole).toInt();
                    int dstKey = dstName->data(Qt::UserRole).toInt();
                    srcName->setData(Qt::UserRole, dstKey);
                    dstName->setData(Qt::UserRole, srcKey);
                }

                // 持久化顺序：以当前表格自上而下的 key 列表为准
                QList<int> newOrder;
                for (int r = 0; r < m_legendTable->rowCount(); ++r) {
                    QTableWidgetItem *nameItem = m_legendTable->item(r, 0);
                    if (!nameItem) continue;
                    int key = nameItem->data(Qt::UserRole).toInt();
                    if (key >= 0 && m_curveKeyToDisplayName.contains(key) && !newOrder.contains(key))
                        newOrder.append(key);
                }
                m_selectedCurveKeyOrder = newOrder;
                refreshLegendAndStatus();
            }

            // 吃掉 release，避免拖拽后触发一次 click 导致误切换显示/隐藏
            return true;
        }
        default:
            break;
        }
    }
    return QDialog::eventFilter(obj, event);
}

double CurvesDialog::valueFromLineEdit(QLineEdit *le)
{
    if (!le) return 0.0;

    // 优先使用控件上保存的“曲线用原始数值”（例如状态 0/1/2...）
    QVariant raw = le->property("curveNumericValue");
    if (raw.isValid()) {
        bool ok = false;
        double v = raw.toDouble(&ok);
        if (ok) return v;
    }

    QString s = le->text().trimmed();
    if (s.isEmpty()) return 0.0;
    bool ok = false;
    double val = s.toDouble(&ok);
    if (ok) return val;
    QRegularExpression re(QStringLiteral("([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)"));
    QRegularExpressionMatch m = re.match(s);
    if (m.hasMatch()) {
        val = m.captured(1).toDouble(&ok);
        if (ok) return val;
    }
    return 0.0;
}

void CurvesDialog::setCurveOptions(const QList<CurveOption> &options)
{
    m_curveOptions = options;
    for (const CurveOption &opt : m_curveOptions)
        m_curvesWidget->ensureCurveInBuffer(opt.curveKey);
}

void CurvesDialog::recordCurrentValuesFromOptions()
{
    if (m_curveOptions.isEmpty()) return;
    std::vector<std::pair<int, double>> sampleData;
    for (const CurveOption &opt : m_curveOptions) {
        if (!opt.lineEdit) continue;
        double value = valueFromLineEdit(opt.lineEdit);
        sampleData.push_back({opt.curveKey, value});
    }
    if (sampleData.empty()) return;
    m_curvesWidget->addSample(sampleData, QDateTime::currentDateTime());
    int winSize = m_curvesWidget->visibleWindowSize();
    // 与 CurvesWidget 中的 kBlankTailSamples 保持一致，这里直接写 300
    const int kBlankTailSamples = 300;
    int totalSamples = m_curvesWidget->buffer().count + kBlankTailSamples;
    int maxStart = qMax(0, totalSamples - winSize);
    m_slider->setMaximum(qMax(0, maxStart));
    if (isVisible())
        refreshLegendAndStatus();
}

void CurvesDialog::addSample(const std::vector<std::pair<int, double>> &sampleData,
                             const QDateTime &sampleDate)
{
    m_curvesWidget->addSample(sampleData, sampleDate);
    int winSize = m_curvesWidget->visibleWindowSize();
    const int kBlankTailSamples = 300;
    int totalSamples = m_curvesWidget->buffer().count + kBlankTailSamples;
    int maxStart = qMax(0, totalSamples - winSize);
    m_slider->setMaximum(qMax(0, maxStart));
}

void CurvesDialog::onSliderValueChanged(int value)
{
    m_curvesWidget->setWindowStart(value);
    refreshLegendAndStatus();
}

void CurvesDialog::onCurrentValueChanged(int pos)
{
    const CurveDataBuffer &buf = m_curvesWidget->buffer();
    if (pos >= 0 && pos < buf.count) {
        int idx = pos % CurveDataBuffer::BufferSize;
        m_timeLabel->setText(buf.sampleTime[idx].toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
//        m_valueLabel->setText(tr(": %1").arg(pos));
    } else {
        m_timeLabel->setText(tr("-"));
        m_valueLabel->setText(tr("-"));
    }
    refreshLegendAndStatus();
}

void CurvesDialog::onConfigClicked()
{
    if (m_curveOptions.isEmpty()) {
        QMessageBox::information(this, tr("曲线配置"), tr("主窗口未提供可选的曲线数据项。"));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("曲线选择"));
    dlg.setMinimumSize(500, 400);
    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);

    QHBoxLayout *listsLayout = new QHBoxLayout();
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(new QLabel(tr("可选项:")));
    QListWidget *listAvailable = new QListWidget(&dlg);
    listAvailable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listAvailable->setMinimumWidth(180);
    leftLayout->addWidget(listAvailable);
    listsLayout->addLayout(leftLayout);

    // 以当前表格为准收集已选通道（支持拖到空行/任意顺序）
    QList<int> currentSelected;
    for (int r = 0; r < m_legendTable->rowCount(); ++r) {
        QTableWidgetItem *nameItem = m_legendTable->item(r, 0);
        if (!nameItem) continue;
        int key = nameItem->data(Qt::UserRole).toInt();
        if (key >= 0 && m_curveKeyToDisplayName.contains(key) && !currentSelected.contains(key))
            currentSelected.append(key);
    }
    m_selectedCurveKeyOrder = currentSelected;

    QSet<int> alreadySelected(m_selectedCurveKeyOrder.begin(), m_selectedCurveKeyOrder.end());
    for (const CurveOption &opt : m_curveOptions) {
        if (alreadySelected.contains(opt.curveKey)) continue;
        QListWidgetItem *item = new QListWidgetItem(opt.displayName);
        item->setData(Qt::UserRole, opt.curveKey);
        listAvailable->addItem(item);
    }

    QVBoxLayout *btnLayout = new QVBoxLayout();
    btnLayout->addStretch();
    QPushButton *btnAdd = new QPushButton(QStringLiteral(">>"), &dlg);
    btnAdd->setToolTip(tr("添加到已选项"));
    QPushButton *btnRemove = new QPushButton(QStringLiteral("<<"), &dlg);
    btnRemove->setToolTip(tr("从已选项移除"));
    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnRemove);
    btnLayout->addStretch();
    listsLayout->addLayout(btnLayout);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel(tr("已选项:")));
    QListWidget *listSelected = new QListWidget(&dlg);
    listSelected->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listSelected->setMinimumWidth(180);
    rightLayout->addWidget(listSelected);
    listsLayout->addLayout(rightLayout);

    for (int curveKey : m_selectedCurveKeyOrder) {
        QString name = m_curveKeyToDisplayName.value(curveKey, tr("通道%1").arg(curveKey));
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, curveKey);
        listSelected->addItem(item);
    }

    auto moveToSelected = [listAvailable, listSelected]() {
        QList<QListWidgetItem*> items = listAvailable->selectedItems();
        for (QListWidgetItem *it : items) {
            int key = it->data(Qt::UserRole).toInt();
            QString text = it->text();
            listAvailable->takeItem(listAvailable->row(it));
            QListWidgetItem *newItem = new QListWidgetItem(text);
            newItem->setData(Qt::UserRole, key);
            listSelected->addItem(newItem);
        }
    };
    auto moveToAvailable = [listAvailable, listSelected]() {
        QList<QListWidgetItem*> items = listSelected->selectedItems();
        for (QListWidgetItem *it : items) {
            int key = it->data(Qt::UserRole).toInt();
            QString text = it->text();
            listSelected->takeItem(listSelected->row(it));
            QListWidgetItem *newItem = new QListWidgetItem(text);
            newItem->setData(Qt::UserRole, key);
            listAvailable->addItem(newItem);
        }
    };
    connect(btnAdd, &QPushButton::clicked, moveToSelected);
    connect(btnRemove, &QPushButton::clicked, moveToAvailable);

    mainLayout->addLayout(listsLayout);
    mainLayout->addWidget(new QLabel(tr("可选项与已选项之间用 >> / << 添加或暂时移除，确定后生效。")));
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    mainLayout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QList<int> newOrder;
    for (int i = 0; i < listSelected->count(); ++i) {
        QListWidgetItem *item = listSelected->item(i);
        newOrder.append(item->data(Qt::UserRole).toInt());
    }

    QSet<int> newSet(newOrder.begin(), newOrder.end());
    for (int curveKey : m_selectedCurveKeyOrder) {
        if (!newSet.contains(curveKey)) {
            m_curvesWidget->removeCurveFromDisplay(curveKey);
            m_selectedLineEdits.remove(curveKey);
            m_curveKeyToDisplayName.remove(curveKey);
            m_curveKeyToUnit.remove(curveKey);
        }
    }
    m_selectedCurveKeyOrder.clear();
    for (int curveKey : newOrder) {
        CurveOption opt;
        for (const CurveOption &o : m_curveOptions) {
            if (o.curveKey == curveKey) { opt = o; break; }
        }
        if (!opt.lineEdit) continue;
        m_selectedLineEdits.insert(curveKey, opt.lineEdit);
        m_curveKeyToDisplayName.insert(curveKey, opt.displayName);
        m_curveKeyToUnit.insert(curveKey, opt.unit);
        m_selectedCurveKeyOrder.append(curveKey);
        // 为新调用到的曲线生成随机颜色
        QColor color = m_curvesWidget->getCurveColor(curveKey);
        if (!color.isValid()) {
            int r = QRandomGenerator::global()->bounded(60, 256);
            int g = QRandomGenerator::global()->bounded(60, 256);
            int b = QRandomGenerator::global()->bounded(60, 256);
            color = QColor(r, g, b);
        }
        int layerIndex = (m_selectedCurveKeyOrder.size() - 1) % 3;
        QString layerName = layerIndex == 0 ? QStringLiteral("Layer0") : (layerIndex == 1 ? QStringLiteral("Layer1") : QStringLiteral("Layer2"));
        m_curvesWidget->addCurve(curveKey, color, layerName);
        m_curvesWidget->setCurveDisplayName(curveKey, opt.displayName);
        m_curvesWidget->setCurveUnit(curveKey, opt.unit);
    }

    // 将已选通道写入表格：前 N 行放已选，后面为空行（之后可拖拽与空行互换）
    for (int i = 0; i < m_legendTable->rowCount(); ++i) {
        QTableWidgetItem *nameItem = m_legendTable->item(i, 0);
        if (!nameItem) continue;
        if (i < m_selectedCurveKeyOrder.size())
            nameItem->setData(Qt::UserRole, m_selectedCurveKeyOrder.at(i));
        else
            nameItem->setData(Qt::UserRole, -1);
    }
    refreshLegendAndStatus();
    if (isVisible() && !m_legendRefreshTimer->isActive()) {
        m_legendRefreshTimer->start(400);
    }
    m_curvesWidget->update();
}

void CurvesDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    const CurveDataBuffer &buf = m_curvesWidget->buffer();
    if (buf.count > 0) {
        int winSize = m_curvesWidget->visibleWindowSize();
        const int kBlankTailSamples = 300;
        int totalSamples = buf.count + kBlankTailSamples;
        int maxStart = qMax(0, totalSamples - winSize);
        int sliderMax = qMax(0, maxStart);
        m_slider->setMaximum(sliderMax);
        int sliderVal = sliderMax;
        m_curvesWidget->setWindowStart(sliderVal);
        m_slider->blockSignals(true);
        m_slider->setValue(sliderVal);
        m_slider->blockSignals(false);
        m_curvesWidget->setCurrentPos(buf.count - 1);
    }
    // 若有已选通道但表格尚未写入顺序，先按 m_selectedCurveKeyOrder 写入表格
    if (!m_selectedCurveKeyOrder.isEmpty()) {
        for (int i = 0; i < m_legendTable->rowCount(); ++i) {
            QTableWidgetItem *nameItem = m_legendTable->item(i, 0);
            if (!nameItem) continue;
            if (i < m_selectedCurveKeyOrder.size())
                nameItem->setData(Qt::UserRole, m_selectedCurveKeyOrder.at(i));
            else
                nameItem->setData(Qt::UserRole, -1);
        }
    }
    refreshLegendAndStatus();
    if (!m_legendRefreshTimer->isActive()) {
        m_legendRefreshTimer->start(400);
    }
}

void CurvesDialog::hideEvent(QHideEvent *event)
{
    m_sampleTimer->stop();
    if (m_legendRefreshTimer) m_legendRefreshTimer->stop();
    QDialog::hideEvent(event);
}

void CurvesDialog::closeEvent(QCloseEvent *event)
{
    // 单例：关闭按钮只隐藏窗口，不销毁对象
    event->ignore();
    hide();
}

void CurvesDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F11) {
        // 全屏切换
        if (!m_isFullScreen) {
            m_isFullScreen = true;
            m_normalGeometry = geometry();
            showFullScreen();
        } else {
            m_isFullScreen = false;
            showNormal();
            if (m_normalGeometry.isValid())
                setGeometry(m_normalGeometry);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_isFullScreen) {
        m_isFullScreen = false;
        showNormal();
        if (m_normalGeometry.isValid())
            setGeometry(m_normalGeometry);
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void CurvesDialog::onSampleTimer()
{
    // 实时采样模式：
    // - 不走 SQLite 回放，而是直接从当前勾选的 m_selectedLineEdits 读取数值；
    // - 相当于“在线模式下的曲线”，每秒追加一帧。
    if (m_selectedLineEdits.isEmpty()) return;

    std::vector<std::pair<int, double>> sampleData;
    for (auto it = m_selectedLineEdits.begin(); it != m_selectedLineEdits.end(); ++it) {
        if (!it.value()) continue;
        double value = valueFromLineEdit(it.value());
        sampleData.push_back({it.key(), value});
    }
    if (sampleData.empty()) return;

    m_curvesWidget->addSample(sampleData, QDateTime::currentDateTime());
    int winSize = m_curvesWidget->visibleWindowSize();
    int maxStart = qMax(0, m_curvesWidget->buffer().count - winSize);
    m_slider->setMaximum(qMax(0, maxStart));
    refreshLegendAndStatus();
}

void CurvesDialog::refreshLegendAndStatus()
{
    // 图例与状态栏刷新逻辑：
    // - 图例表格左列：曲线名（来自 m_curveKeyToDisplayName）；
    // - 右列：当前光标位置对应的值（优先按状态映射显示文字，其次显示数值）；
    // - 状态栏：当前采样时刻 + 已采样时长。
    const CurveDataBuffer &buf = m_curvesWidget->buffer();
    int cursorPos = buf.currentPos;
    int progressPos = m_slider->value();

    // 以表格当前行内容为准刷新（支持与空行互换）
    for (int i = 0; i < m_legendTable->rowCount(); ++i) {
        QTableWidgetItem *nameItem = m_legendTable->item(i, 0);
        QTableWidgetItem *valueItem = m_legendTable->item(i, 1);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            // 名称列：只显示，不编辑
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_legendTable->setItem(i, 0, nameItem);
        }
        if (!valueItem) {
            valueItem = new QTableWidgetItem();
            m_legendTable->setItem(i, 1, valueItem);
        }

        int curveKey = nameItem->data(Qt::UserRole).toInt();  // 每一行绑定一个 curveKey，-1 表示空行
        if (curveKey >= 0 && m_curveKeyToDisplayName.contains(curveKey)) {
            QString name = m_curveKeyToDisplayName.value(curveKey, tr("通道%1").arg(curveKey));
            double val = 0.0;
            if (buf.count > 0 && cursorPos >= 0 && cursorPos < buf.count) {
                val = m_curvesWidget->getValueAt(curveKey, cursorPos);
            }

            // 数值列优先根据“当前光标位置的数值 + 状态映射”显示文字，
            // 例如：0 -> 停机，1 -> 制冷，2 -> 制热 ...
            // 状态映射的来源：
            // - 主窗口在更新 QLineEdit 时，把 ConversionRule::valueMap / textFor0/textFor1
            //   通过 property("curveValueMap") 挂在对应的 lineEdit 上；
            // - 这里通过 curveKey 找到 selectedLineEdits 中的 lineEdit，再按 val（取整）反查文字。
            QString valueText;
            QLineEdit *le = m_selectedLineEdits.value(curveKey, nullptr);
            if (le) {
                QVariant vmVar = le->property("curveValueMap");
                if (vmVar.isValid()) {
                    QVariantMap vm = vmVar.toMap();
                    int state = qRound(val);
                    QString key = QString::number(state);
                    if (vm.contains(key)) {
                        valueText = vm.value(key).toString();
                    }
                }
            }
            // 如果没有状态映射，则回退为数值显示
            if (valueText.isEmpty()) {
                valueText = QString::number(val, 'f', 1);
            }

            nameItem->setText(name);
            valueItem->setText(valueText);
            QColor curveColor = m_curvesWidget->getCurveColor(curveKey);
            if (!curveColor.isValid()) {
                curveColor = s_curveColors.at(i % s_curveColors.size());
            }
            valueItem->setBackground(curveColor);
            // 有曲线的数据：数值文字为白色
            valueItem->setForeground(Qt::white);
            bool visible = m_curvesWidget->isCurveVisible(curveKey);
            nameItem->setBackground(
                visible ? s_nameBackgroundColor : s_nameHiddenBackgroundColor);
        } else {
            nameItem->setText(QStringLiteral("-"));
            nameItem->setData(Qt::UserRole, -1);
            valueItem->setText(QStringLiteral("-"));
            // 未调用到曲线：背景灰色、文字黑色
            valueItem->setBackground(QColor(200, 200, 200));
            valueItem->setForeground(Qt::black);
            nameItem->setBackground(s_nameBackgroundColor);
        }
    }

    int statusIdx = progressPos % CurveDataBuffer::BufferSize;
    if (buf.count > 0 && progressPos >= 0 && progressPos < buf.count) {
        m_statusTimeLabel->setText(tr("当前采样时刻: %1").arg(buf.sampleTime[statusIdx].toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        int minutes = buf.count / 60;
        if (minutes < 1 && buf.count > 0) minutes = 1;
        m_statusDurationLabel->setText(tr("已采样时长: %1 分钟").arg(minutes));
    } else {
        m_statusTimeLabel->setText(tr("当前采样时刻: -"));
        m_statusDurationLabel->setText(tr("已采样时长: 0 分钟"));
    }
}

void CurvesDialog::onLegendCellDoubleClicked(int row, int column)
{
    if (row < 0 || row >= m_legendTable->rowCount()) return;

    QTableWidgetItem *nameItem = m_legendTable->item(row, 0);
    if (!nameItem) return;
    int curveKey = nameItem->data(Qt::UserRole).toInt();
    if (curveKey < 0) return;

    if (column == 0) {
        bool nowVisible = !m_curvesWidget->isCurveVisible(curveKey);
        m_curvesWidget->setCurveVisible(curveKey, nowVisible);
        refreshLegendAndStatus();
    } else if (column == 1) {
        QColor current = m_curvesWidget->getCurveColor(curveKey);
        QColor c = QColorDialog::getColor(current.isValid() ? current : Qt::red, this, tr("曲线颜色"));
        if (!c.isValid()) return;
        m_curvesWidget->setCurveColor(curveKey, c);
        refreshLegendAndStatus();
    }
}

void CurvesDialog::onLegendContextMenu(QPoint pos)
{
    QTableWidgetItem *item = m_legendTable->itemAt(pos);
    if (!item) return;
    int row = item->row();
    if (row < 0 || row >= m_legendTable->rowCount()) return;
    QTableWidgetItem *nameItem = m_legendTable->item(row, 0);
    if (!nameItem) return;
    int curveKey = nameItem->data(Qt::UserRole).toInt();
    if (curveKey < 0) return;

    QMenu menu(this);
    QAction *actRemove = menu.addAction(tr("移除曲线"));
    QAction *actColor = menu.addAction(tr("设置颜色"));
    QAction *chosen = menu.exec(m_legendTable->mapToGlobal(pos));
    if (chosen == actRemove)
        removeCurveAtLegendRow(row);
    else if (chosen == actColor)
        setCurveColorAtLegendRow(row);
}

void CurvesDialog::removeCurveAtLegendRow(int row)
{
    if (row < 0 || row >= m_legendTable->rowCount()) return;
    QTableWidgetItem *nameItem = m_legendTable->item(row, 0);
    if (!nameItem) return;
    int curveKey = nameItem->data(Qt::UserRole).toInt();
    if (curveKey < 0) return;

    m_curvesWidget->removeCurve(curveKey);
    m_selectedLineEdits.remove(curveKey);
    m_curveKeyToDisplayName.remove(curveKey);
    m_curveKeyToUnit.remove(curveKey);

    // 该行清空，允许与空行互换
    nameItem->setData(Qt::UserRole, -1);
    refreshLegendAndStatus();
    if (m_selectedLineEdits.isEmpty()) {
        m_sampleTimer->stop();
        m_legendRefreshTimer->stop();
    }
}

void CurvesDialog::setCurveColorAtLegendRow(int row)
{
    if (row < 0 || row >= m_legendTable->rowCount()) return;
    QTableWidgetItem *nameItem = m_legendTable->item(row, 0);
    if (!nameItem) return;
    int curveKey = nameItem->data(Qt::UserRole).toInt();
    if (curveKey < 0) return;
    QColor current = m_curvesWidget->getCurveColor(curveKey);
    QColor c = QColorDialog::getColor(current.isValid() ? current : Qt::red, this, tr("曲线颜色"));
    if (!c.isValid()) return;
    m_curvesWidget->setCurveColor(curveKey, c);
    refreshLegendAndStatus();
}
