#include "MainWindow.h"
#include "CameraSelectionDialog.h"
#include "MediaSelectionDialog.h"
#include <QMessageBox>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QUiLoader loader;
    QFile file(":/mainwindow.ui");
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open mainwindow.ui from resources");
        return;
    }

    m_uiRoot = loader.load(&file, nullptr);
    file.close();

    if (!m_uiRoot) {
        QMessageBox::critical(this, "Error", "Could not load mainwindow.ui");
        return;
    }

    QMainWindow *loadedMainWin = qobject_cast<QMainWindow*>(m_uiRoot);
    if (loadedMainWin) {
        QWidget *central = loadedMainWin->centralWidget();
        if (central) {
            central->setParent(this);
            setCentralWidget(central);
        }
        if (loadedMainWin->menuBar()) setMenuBar(loadedMainWin->menuBar());
        if (loadedMainWin->statusBar()) setStatusBar(loadedMainWin->statusBar());
        setWindowTitle(loadedMainWin->windowTitle());
        resize(loadedMainWin->size());
    } else {
        setCentralWidget(m_uiRoot);
    }

    m_cameraManager = new CameraManager(this);
    
    QFrame *previewFrame = this->findChild<QFrame*>("outputPreview");
    if (previewFrame) m_cameraManager->setupInput(0, previewFrame);
    
    QString bundlePath = QCoreApplication::applicationDirPath() + "/../Resources/effects";
    QString effectsPath = QDir(bundlePath).exists() ? bundlePath : QCoreApplication::applicationDirPath() + "/effects";
    
    m_effectsManager = new EffectsManager(effectsPath, this);
    
    QTreeWidget *tree = this->findChild<QTreeWidget*>("treeWidget_effects_cats");
    QStackedWidget *stack = this->findChild<QStackedWidget*>("stackedWidget_effects");
    if (tree && stack) {
        tree->setFixedWidth(130);
        m_effectsManager->setupUI(tree, stack, this);
    }

    fixControlsLayout();
    setupUi();
}

MainWindow::~MainWindow() {}

void MainWindow::fixControlsLayout() {
    // Correct name from UI
    QFrame *controlsPanel = this->findChild<QFrame*>("frame_controlPanel"); 
    if (controlsPanel && controlsPanel->layout()) {
        QVBoxLayout *vbox = qobject_cast<QVBoxLayout*>(controlsPanel->layout());
        if (vbox) {
            // Find if a stretch is already there
            bool hasSpacer = false;
            for (int i = 0; i < vbox->count(); ++i) {
                if (vbox->itemAt(i)->spacerItem()) {
                    hasSpacer = true;
                    break;
                }
            }
            if (!hasSpacer) vbox->addStretch(1);
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_effectsManager) {
        m_effectsManager->handleResize();
    }
}

void MainWindow::setupUi() {
    QStringList prefixes = {"input", "media"};
    for (const QString &prefix : prefixes) {
        for (int i = 1; i <= 3; ++i) {
            int slotId = (prefix == "input" ? i : i + 3);
            QString frameName = QString("%1VideoFrame%2").arg(prefix).arg(i);
            QFrame *frame = this->findChild<QFrame*>(frameName);
            if (frame) {
                m_cameraManager->setupInput(slotId, frame);
                frame->installEventFilter(this);
                frame->setProperty("slotId", slotId);
                frame->setCursor(Qt::PointingHandCursor);
            }
            QString settingsBtnName = QString("%1%2SettingsButton").arg(prefix).arg(i);
            QPushButton *settingsBtn = this->findChild<QPushButton*>(settingsBtnName);
            if (settingsBtn) {
                connect(settingsBtn, &QPushButton::clicked, [this, slotId, prefix]() { 
                    if (prefix == "input") onSettingsClicked(slotId);
                    else onMediaSettingsClicked(slotId);
                });
            }
            QString audioBtnName = QString("%1%2AudioButton").arg(prefix).arg(i);
            QPushButton *audioBtn = this->findChild<QPushButton*>(audioBtnName);
            if (audioBtn) {
                audioBtn->setCheckable(true);
                connect(audioBtn, &QPushButton::toggled, [this, slotId, audioBtn](bool checked) {
                    m_cameraManager->setMuted(slotId, checked);
                    audioBtn->setIcon(QIcon(checked ? ":/icons/Mute.png" : ":/icons/Volume.png"));
                });
            }
            if (prefix == "media") {
                QString playBtnName = QString("pushButton_%1").arg(18 + i);
                QPushButton *playBtn = this->findChild<QPushButton*>(playBtnName);
                if (playBtn) {
                    playBtn->setCheckable(true);
                    playBtn->setText("");
                    playBtn->setIcon(QIcon(":/icons/Pause.png"));
                    connect(playBtn, &QPushButton::clicked, [this, slotId]() {
                        m_cameraManager->togglePlayPause(slotId);
                    });
                }
                QString sliderName = (i == 1 ? "horizontalSlider" : QString("horizontalSlider_%1").arg(i));
                QSlider *slider = this->findChild<QSlider*>(sliderName);
                if (slider) {
                    slider->setRange(0, 1000);
                    connect(slider, &QSlider::sliderMoved, [this, slotId](int val) {
                        m_cameraManager->seek(slotId, val / 1000.0);
                    });
                    connect(m_cameraManager, &CameraManager::mediaPositionChanged, [this, slotId, slider](int id, double percent) {
                        if (id == slotId && !slider->isSliderDown()) {
                            slider->setValue(percent * 1000);
                        }
                    });
                }
            }
        }
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QFrame *frame = qobject_cast<QFrame*>(watched);
        if (frame && frame->property("slotId").isValid()) {
            int slotId = frame->property("slotId").toInt();
            m_cameraManager->setPreviewSlot(slotId);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSettingsClicked(int id) {
    CameraSelectionDialog dialog(m_cameraManager, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_cameraManager->openCameraForInput(id, 
            dialog.getSelectedCameraId(),
            dialog.getSelectedWidth(),
            dialog.getSelectedHeight(),
            dialog.getSelectedFps()
        );
    }
}

void MainWindow::onMediaSettingsClicked(int id) {
    MediaSelectionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        m_cameraManager->openFileForInput(id, dialog.getFilePath(), dialog.isLooping());
    }
}
