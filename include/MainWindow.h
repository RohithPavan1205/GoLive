#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QEvent>
#include <QResizeEvent>
#include "CameraManager.h"
#include "EffectsManager.h"

namespace Ui { class MainWindow; }

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
    Ui::MainWindow *ui;
    CameraManager *m_cameraManager;
    EffectsManager *m_effectsManager;
    QString m_recordingPath;
    
    void setupUi();
    void setupOutputControls();
    void fixControlsLayout();
};

#endif // MAINWINDOW_H
