#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QLabel>

SettingsDialog::SettingsDialog(const QList<QCameraDevice> &cameras, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Input Settings");
    setStyleSheet("background-color: #222222; color: white;");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Select Video Device:"));
    
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setStyleSheet("background-color: #333333; color: white; padding: 5px;");
    for (const auto &cam : cameras) {
        m_cameraCombo->addItem(cam.description());
    }
    layout->addWidget(m_cameraCombo);
    
    QPushButton *applyBtn = new QPushButton("Apply", this);
    applyBtn->setStyleSheet("background-color: #444444; color: white; padding: 8px; border-radius: 4px;");
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(applyBtn);
}

int SettingsDialog::selectedIndex() const {
    return m_cameraCombo->currentIndex();
}
