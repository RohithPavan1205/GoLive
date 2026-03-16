#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUiLoader>
#include <QFile>
#include <QPushButton>
#include <QEvent>
#include <QSlider>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QResizeEvent>
#include "CameraManager.h"
#include "EffectsManager.h"
#include "RecordingSettingsDialog.h"
#include <QMessageBox>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSettingsClicked(int id);
    void onMediaSettingsClicked(int id);
    void onRecordingSettingsClicked();
    void onStartRecordingClicked();

private:
    QWidget *m_uiRoot;
    CameraManager *m_cameraManager;
    EffectsManager *m_effectsManager;
    
    void setupUi();
    void setupOutputControls();
    void fixControlsLayout();

    RecordingSettings m_recordSettings;
    bool m_isRecording = false;
};

#endif // MAINWINDOW_H
