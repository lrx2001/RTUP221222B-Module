#include "playbacksettingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>

PlaybackSettingsDialog::PlaybackSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("回放设置"));
    setMinimumWidth(480);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel(tr("数据库文件路径:")));
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(tr("选择数据库文件路径"));
    pathLayout->addWidget(m_pathEdit, 1);
    QPushButton *browseBtn = new QPushButton(tr("浏览..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &PlaybackSettingsDialog::onBrowseClicked);
    pathLayout->addWidget(browseBtn);
    mainLayout->addLayout(pathLayout);

    QHBoxLayout *intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel(tr("回放间隔时间:")));
    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(1, 10000);
    m_intervalSpin->setValue(500);
    m_intervalSpin->setSuffix(tr(" ms"));
    intervalLayout->addWidget(m_intervalSpin);
    intervalLayout->addStretch();
    mainLayout->addLayout(intervalLayout);

    // 新增：是否一次性加载全部数据，而不是按时间间隔播放
    m_loadAllCheck = new QCheckBox(tr("直接加载全部数据（不按时间播放）"), this);
    m_loadAllCheck->setChecked(true);
    mainLayout->addWidget(m_loadAllCheck);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(box);
}

QString PlaybackSettingsDialog::databaseFilePath() const
{
    return m_pathEdit ? m_pathEdit->text().trimmed() : QString();
}

int PlaybackSettingsDialog::playbackIntervalMs() const
{
    return m_intervalSpin ? m_intervalSpin->value() : 500;
}

bool PlaybackSettingsDialog::loadAllAtOnce() const
{
    return m_loadAllCheck && m_loadAllCheck->isChecked();
}

void PlaybackSettingsDialog::onBrowseClicked()
{
    QString path = QFileDialog::getOpenFileName(this, tr("选择数据库文件"),
                                                m_pathEdit->text(),
                                                tr("数据库文件 (*.db *.sqlite *.dat);;所有文件 (*)"));
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}
