#ifndef RECORDINGSETTINGSDIALOG_H
#define RECORDINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(QWidget *parent = nullptr);
    
    QString getRecordingPath() const { return m_path; }
    void setRecordingPath(const QString &path);

private slots:
    void onBrowse();
    void onSave();

private:
    QLineEdit *m_pathEdit;
    QString m_path;
};

#endif // RECORDINGSETTINGSDIALOG_H
