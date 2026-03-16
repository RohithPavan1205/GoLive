#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUiLoader>
#include <QFile>
#include <QMessageBox>
#include <QCamera>
#include <QMediaDevices>
#include <QVideoWidget>
#include <QMediaCaptureSession>
#include <QDialog>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include <QFrame>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openSettingsForInput(int inputId);
    void applyCameraToInput(int inputId, int cameraIndex);

private:
    QWidget *ui_root;
    QMediaCaptureSession *captureSession;
    QCamera *camera;
    QVideoWidget *videoWidget;
};

#endif // MAINWINDOW_H
