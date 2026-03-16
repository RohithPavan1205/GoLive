#include "MainWindow.h"
#include <QIcon>
#include <QDebug>
#include <QVideoSink>
#include <QVideoFrame>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), camera(nullptr), videoWidget(nullptr)
{
    QUiLoader loader;
    QFile file("mainwindow.ui");
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open mainwindow.ui");
        return;
    }

    ui_root = loader.load(&file, this);
    file.close();

    if (!ui_root) {
        QMessageBox::critical(this, "Error", "Could not load mainwindow.ui");
        return;
    }

    setCentralWidget(ui_root);
    
    captureSession = new QMediaCaptureSession(this);
    
    // Setup for 6 inputs
    for (int i = 1; i <= 3; ++i) {
        QString btnName = QString("input%1SettingsButton").arg(i);
        QPushButton *btn = ui_root->findChild<QPushButton*>(btnName);
        if (btn) {
            connect(btn, &QPushButton::clicked, [this, i]() { openSettingsForInput(i); });
        }
    }
}

MainWindow::~MainWindow()
{
}

void MainWindow::openSettingsForInput(int inputId)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QString("Input-%1 Settings").arg(inputId));
    dialog.setStyleSheet("background-color: #222222; color: #ffffff;");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Select Camera:"));
    QComboBox *cameraCombo = new QComboBox(&dialog);
    cameraCombo->setStyleSheet("background-color: #333333; color: white; padding: 5px;");
    
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (const QCameraDevice &cameraDevice : cameras) {
        cameraCombo->addItem(cameraDevice.description());
    }
    layout->addWidget(cameraCombo);

    QPushButton *okBtn = new QPushButton("Apply", &dialog);
    okBtn->setStyleSheet("background-color: #444444; color: white; padding: 8px; border-radius: 4px;");
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(okBtn);

    if (dialog.exec() == QDialog::Accepted) {
        applyCameraToInput(inputId, cameraCombo->currentIndex());
    }
}

void MainWindow::applyCameraToInput(int inputId, int cameraIndex)
{
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameraIndex < 0 || cameraIndex >= cameras.size()) return;

    if (camera) {
        camera->stop();
        delete camera;
    }

    camera = new QCamera(cameras[cameraIndex], this);
    captureSession->setCamera(camera);
    
    QString frameName = QString("inputVideoFrame%1").arg(inputId);
    QFrame *monitorFrame = ui_root->findChild<QFrame*>(frameName);
    
    if (monitorFrame) {
        if (!videoWidget) {
            videoWidget = new QVideoWidget(monitorFrame);
            QVBoxLayout *layout = new QVBoxLayout(monitorFrame);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(videoWidget);
        }
        captureSession->setVideoOutput(videoWidget);
    }

    camera->start();
}
