#ifndef PLAYBACKSETTINGSDIALOG_H
#define PLAYBACKSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>

class PlaybackSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlaybackSettingsDialog(QWidget *parent = nullptr);

    QString databaseFilePath() const;
    int playbackIntervalMs() const;
    bool loadAllAtOnce() const;

private slots:
    void onBrowseClicked();

private:
    QLineEdit *m_pathEdit = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QCheckBox *m_loadAllCheck = nullptr;
};

#endif // PLAYBACKSETTINGSDIALOG_H
