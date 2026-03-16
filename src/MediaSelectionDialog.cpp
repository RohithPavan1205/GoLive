#include "MediaSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>

MediaSelectionDialog::MediaSelectionDialog(QWidget *parent) : QDialog(parent) {
    setupUI();
}

void MediaSelectionDialog::setupUI() {
    setWindowTitle("Media Config");
    setMinimumWidth(400);
    setStyleSheet("background-color: #1a1a1a; color: #eeeeee;");
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    mainLayout->addWidget(new QLabel("Select Media File:"));
    
    QHBoxLayout *fileLayout = new QHBoxLayout;
    m_fileLineEdit = new QLineEdit(this);
    m_fileLineEdit->setStyleSheet("background-color: #333333; padding: 5px;");
    QPushButton *browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &MediaSelectionDialog::onBrowseClicked);
    fileLayout->addWidget(m_fileLineEdit);
    fileLayout->addWidget(browseBtn);
    mainLayout->addLayout(fileLayout);
    
    m_loopCheckBox = new QCheckBox("Loop Media", this);
    mainLayout->addWidget(m_loopCheckBox);
    
    mainLayout->addStretch();
    
    QHBoxLayout *btnLayout = new QHBoxLayout;
    QPushButton *okBtn = new QPushButton("Apply", this);
    QPushButton *cancelBtn = new QPushButton("Cancel", this);
    okBtn->setStyleSheet("background-color: #0078d4; color: white; padding: 10px;");
    cancelBtn->setStyleSheet("background-color: #444444; color: white; padding: 10px;");
    
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

void MediaSelectionDialog::onBrowseClicked() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Media", "", "Video Files (*.mp4 *.mov *.avi *.mkv)");
    if (!fileName.isEmpty()) {
        m_fileLineEdit->setText(fileName);
    }
}

QString MediaSelectionDialog::getFilePath() const { return m_fileLineEdit->text(); }
bool MediaSelectionDialog::isLooping() const { return m_loopCheckBox->isChecked(); }
