#ifndef RECORDINGSETTINGSDIALOG_H
#define RECORDINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

struct RecordingSettings {
    QString outputDir;
    QString fileNamePrefix;
    QString format;
    int quality; // 0: low, 1: med, 2: high
    bool isConfigured = false;
};

class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(const RecordingSettings &currentSettings, QWidget *parent = nullptr);
    RecordingSettings getSettings() const;

private slots:
    void onBrowseClicked();
    void onSaveClicked();

private:
    void setupUI();
    
    QLineEdit *m_pathEdit;
    QLineEdit *m_nameEdit;
    QComboBox *m_formatCombo;
    QComboBox *m_qualityCombo;
    
    RecordingSettings m_settings;
};

#endif // RECORDINGSETTINGSDIALOG_H
