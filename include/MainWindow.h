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
#include "RecordingManager.h"
#include "RecordingSettingsDialog.h"

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
    void onRecordSettingsClicked();
    void onRecordClicked();

private:
    QWidget *m_uiRoot;
    CameraManager *m_cameraManager;
    EffectsManager *m_effectsManager;
    RecordingManager *m_recordingManager;
    RecordingSettings m_recordingSettings;
    
    void setupUi();
    void setupOutputControls();
    void fixControlsLayout();
};

#endif // MAINWINDOW_H
