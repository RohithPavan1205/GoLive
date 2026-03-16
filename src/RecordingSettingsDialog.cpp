#include "RecordingSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QStandardPaths>

RecordingSettingsDialog::RecordingSettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Recording Settings");
    setMinimumWidth(400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QLabel *label = new QLabel("Save Recording To:", this);
    mainLayout->addWidget(label);

    QHBoxLayout *pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    
    // Default path
    m_path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/GoLive_Recordings";
    m_pathEdit->setText(m_path);

    QPushButton *browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onBrowse);

    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(browseBtn);
    mainLayout->addLayout(pathLayout);

    mainLayout->addStretch();

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *cancelBtn = new QPushButton("Cancel", this);
    QPushButton *saveBtn = new QPushButton("Save", this);
    saveBtn->setDefault(true);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &RecordingSettingsDialog::onSave);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);
}

void RecordingSettingsDialog::setRecordingPath(const QString &path) {
    m_path = path;
    m_pathEdit->setText(path);
}

void RecordingSettingsDialog::onBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Recording Directory", m_path);
    if (!dir.isEmpty()) {
        m_path = dir;
        m_pathEdit->setText(m_path);
    }
}

void RecordingSettingsDialog::onSave() {
    accept();
}
