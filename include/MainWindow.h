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
#include "StreamingSettingsDialog.h"
#include "TextOverlaySettingsDialog.h"
#include "StreamingManager.h"
#include "RecordingManager.h"
#include "ExternalMonitorWindow.h"

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
    void onStreamingError(const QString &msg);
    void updateStreamingState();
    void onStreamingStateChanged(StreamingManager::State state);
    void onStreamingMetricsUpdated(const StreamingManager::Metrics &metrics);

private:
    QWidget *m_uiRoot;
    CameraManager *m_cameraManager;
    EffectsManager *m_effectsManager;
    StreamingManager *m_streamingManager;
    RecordingManager *m_recordingManager;
    
    // Config State
    RecordingSettingsDialog::Settings m_recordingSettings;
    StreamingSettingsDialog::Settings m_stream1Settings;
    StreamingSettingsDialog::Settings m_stream2Settings;
    TextOverlaySettingsDialog::Settings m_textSettings;
    ExternalMonitorWindow *m_externalMonitor1 = nullptr;
    ExternalMonitorWindow *m_externalMonitor2 = nullptr;

    void setupUi();
    void setupOutputControls();
    void fixControlsLayout();
};

#endif // MAINWINDOW_H
