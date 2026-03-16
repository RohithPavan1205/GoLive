#ifndef RECORDINGSETTINGSDIALOG_H
#define RECORDINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QStandardPaths>
#include <QString>
#include <QHBoxLayout>
#include <QColor> // Added as per instruction

class RecordingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        QString outputPath;
        QString container = "mp4";
        QString quality = "High";
        bool isConfigured = false;
    };

    explicit RecordingSettingsDialog(Settings *settings, QWidget *parent = nullptr) 
        : QDialog(parent), m_settings(settings) {
        setWindowTitle("Recording Settings");
        setMinimumWidth(400);

        QFormLayout *layout = new QFormLayout(this);

        m_pathEdit = new QLineEdit(this);
        m_pathEdit->setText(m_settings->outputPath.isEmpty() ? 
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/GoLive_Recordings" : 
            m_settings->outputPath);
        
        QPushButton *browseBtn = new QPushButton("Browse...", this);
        connect(browseBtn, &QPushButton::clicked, [this]() {
            QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory", m_pathEdit->text());
            if (!dir.isEmpty()) m_pathEdit->setText(dir);
        });

        QHBoxLayout *pathLayout = new QHBoxLayout();
        pathLayout->addWidget(m_pathEdit);
        pathLayout->addWidget(browseBtn);
        layout->addRow("Output Path:", pathLayout);

        m_containerCombo = new QComboBox(this);
        m_containerCombo->addItems({"mp4", "mkv", "mov", "flv"});
        m_containerCombo->setCurrentText(m_settings->container);
        layout->addRow("Format:", m_containerCombo);

        m_qualityCombo = new QComboBox(this);
        m_qualityCombo->addItems({"Low (Fast)", "Medium", "High (Recommended)", "Indistinguishable"});
        m_qualityCombo->setCurrentText(m_settings->quality);
        layout->addRow("Quality:", m_qualityCombo);

        QPushButton *saveBtn = new QPushButton("Save Configuration", this);
        saveBtn->setFixedHeight(40);
        saveBtn->setStyleSheet("background-color: #2ecc71; color: white; border-radius: 4px; font-weight: bold;");
        connect(saveBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onSave);
        layout->addRow(saveBtn);
    }

private slots:
    void onSave() {
        m_settings->outputPath = m_pathEdit->text();
        m_settings->container = m_containerCombo->currentText();
        m_settings->quality = m_qualityCombo->currentText();
        m_settings->isConfigured = !m_settings->outputPath.isEmpty();
        accept();
    }

private:
    Settings *m_settings;
    QLineEdit *m_pathEdit;
    QComboBox *m_containerCombo;
    QComboBox *m_qualityCombo;
};

#endif // RECORDINGSETTINGSDIALOG_H
