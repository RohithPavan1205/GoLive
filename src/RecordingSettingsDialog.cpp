#include "RecordingSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QLabel>
#include <QStandardPaths>

RecordingSettingsDialog::RecordingSettingsDialog(QWidget *parent) : QDialog(parent) {
    setupUi();
    
    // Set default path to Movies folder
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/GoLive_Recordings";
    m_pathEdit->setText(defaultPath);
}

void RecordingSettingsDialog::setupUi() {
    setWindowTitle("Recording Settings");
    setMinimumWidth(500);
    setStyleSheet(
        "QDialog { background-color: #1e1e1e; color: white; }"
        "QGroupBox { border: 1px solid #333; border-radius: 8px; margin-top: 15px; padding-top: 10px; font-weight: bold; color: #aaa; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }"
        "QLineEdit, QComboBox, QSpinBox { background-color: #2b2b2b; color: white; border: 1px solid #444; border-radius: 4px; padding: 5px; }"
        "QPushButton { background-color: #444; color: white; border: none; border-radius: 4px; padding: 8px 15px; }"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton#saveBtn { background-color: #007acc; font-weight: bold; }"
        "QPushButton#saveBtn:hover { background-color: #008be5; }"
        "QLabel { color: #eee; }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // Output Group
    QGroupBox *outputGroup = new QGroupBox("Output Destination", this);
    QVBoxLayout *outputLayout = new QVBoxLayout(outputGroup);
    
    QHBoxLayout *pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("Select recording folder...");
    QPushButton *browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onBrowseClicked);
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(browseBtn);
    outputLayout->addLayout(pathLayout);
    mainLayout->addWidget(outputGroup);

    // Encoding Group
    QGroupBox *encodeGroup = new QGroupBox("Recording Quality", this);
    QFormLayout *formLayout = new QFormLayout(encodeGroup);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft);

    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItems({"mp4", "mkv", "mov", "flv"});
    formLayout->addRow("Container Format:", m_formatCombo);

    m_encoderCombo = new QComboBox(this);
    m_encoderCombo->addItems({"Software (x264)", "Hardware (VideoToolbox)"});
    formLayout->addRow("Video Encoder:", m_encoderCombo);

    m_vBitrateSpin = new QSpinBox(this);
    m_vBitrateSpin->setRange(1000, 50000);
    m_vBitrateSpin->setSuffix(" kbps");
    m_vBitrateSpin->setValue(6000);
    formLayout->addRow("Video Bitrate:", m_vBitrateSpin);

    m_aBitrateSpin = new QSpinBox(this);
    m_aBitrateSpin->setRange(64, 320);
    m_aBitrateSpin->setSuffix(" kbps");
    m_aBitrateSpin->setValue(160);
    formLayout->addRow("Audio Bitrate:", m_aBitrateSpin);

    mainLayout->addWidget(encodeGroup);

    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *cancelBtn = new QPushButton("Cancel", this);
    QPushButton *saveBtn = new QPushButton("Apply Settings", this);
    saveBtn->setObjectName("saveBtn");
    
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onSaveClicked);
    
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);
}

void RecordingSettingsDialog::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Recording Directory", m_pathEdit->text());
    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
    }
}

void RecordingSettingsDialog::onSaveClicked() {
    m_settings.savePath = m_pathEdit->text();
    m_settings.format = m_formatCombo->currentText();
    m_settings.videoBitrate = m_vBitrateSpin->value();
    m_settings.audioBitrate = m_aBitrateSpin->value();
    m_settings.encoder = m_encoderCombo->currentText();
    
    accept();
}

void RecordingSettingsDialog::setSettings(const RecordingSettings &settings) {
    m_settings = settings;
    m_pathEdit->setText(settings.savePath);
    m_formatCombo->setCurrentText(settings.format);
    m_vBitrateSpin->setValue(settings.videoBitrate);
    m_aBitrateSpin->setValue(settings.audioBitrate);
    m_encoderCombo->setCurrentText(settings.encoder);
}

RecordingSettings RecordingSettingsDialog::getSettings() const {
    return m_settings;
}
