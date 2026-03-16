#include "RecordingSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QStandardPaths>

RecordingSettingsDialog::RecordingSettingsDialog(const RecordingSettings &currentSettings, QWidget *parent)
    : QDialog(parent), m_settings(currentSettings) {
    setWindowTitle("Recording Settings");
    setupUI();
    
    if (m_settings.outputDir.isEmpty()) {
        m_settings.outputDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }
    m_pathEdit->setText(m_settings.outputDir);
    m_nameEdit->setText(m_settings.fileNamePrefix.isEmpty() ? "GoLive_Record" : m_settings.fileNamePrefix);
}

void RecordingSettingsDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Path
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel("Output Folder:"));
    m_pathEdit = new QLineEdit();
    m_pathEdit->setReadOnly(true);
    pathLayout->addWidget(m_pathEdit);
    QPushButton *browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onBrowseClicked);
    pathLayout->addWidget(browseBtn);
    mainLayout->addLayout(pathLayout);
    
    // Name
    QHBoxLayout *nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel("File Prefix:"));
    m_nameEdit = new QLineEdit();
    nameLayout->addWidget(m_nameEdit);
    mainLayout->addLayout(nameLayout);
    
    // Format
    QHBoxLayout *formatLayout = new QHBoxLayout();
    formatLayout->addWidget(new QLabel("Format:"));
    m_formatCombo = new QComboBox();
    m_formatCombo->addItems({"mp4", "mkv", "mov"});
    formatLayout->addWidget(m_formatCombo);
    mainLayout->addLayout(formatLayout);
    
    // Quality
    QHBoxLayout *qualityLayout = new QHBoxLayout();
    qualityLayout->addWidget(new QLabel("Quality:"));
    m_qualityCombo = new QComboBox();
    m_qualityCombo->addItems({"High", "Medium", "Low"});
    qualityLayout->addWidget(m_qualityCombo);
    mainLayout->addLayout(qualityLayout);
    
    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *saveBtn = new QPushButton("Save");
    QPushButton *cancelBtn = new QPushButton("Cancel");
    connect(saveBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onSaveClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void RecordingSettingsDialog::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory", m_pathEdit->text());
    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
    }
}

void RecordingSettingsDialog::onSaveClicked() {
    m_settings.outputDir = m_pathEdit->text();
    m_settings.fileNamePrefix = m_nameEdit->text();
    m_settings.format = m_formatCombo->currentText();
    m_settings.quality = m_qualityCombo->currentIndex();
    m_settings.isConfigured = true;
    accept();
}

RecordingSettings RecordingSettingsDialog::getSettings() const {
    return m_settings;
}
