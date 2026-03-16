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

private:
    QWidget *m_uiRoot;
    CameraManager *m_cameraManager;
    EffectsManager *m_effectsManager;
    QString m_recordingPath;
    
    void setupUi();
    void setupOutputControls();
    void fixControlsLayout();
};

#endif // MAINWINDOW_H
