#ifndef RECORDINGSETTINGSDIALOG_H
#define RECORDINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QString>

struct RecordingSettings {
    QString savePath;
    QString format;
    int videoBitrate; // in kbps
    int audioBitrate; // in kbps
    QString encoder;
    bool isValid() const {
        return !savePath.isEmpty() && !format.isEmpty();
    }
};

class RecordingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(QWidget *parent = nullptr);
    
    void setSettings(const RecordingSettings &settings);
    RecordingSettings getSettings() const;

private slots:
    void onBrowseClicked();
    void onSaveClicked();

private:
    void setupUi();

    QLineEdit *m_pathEdit;
    QComboBox *m_formatCombo;
    QSpinBox *m_vBitrateSpin;
    QSpinBox *m_aBitrateSpin;
    QComboBox *m_encoderCombo;
    
    RecordingSettings m_settings;
};

#endif // RECORDINGSETTINGSDIALOG_H
