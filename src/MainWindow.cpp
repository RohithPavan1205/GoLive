#include "MainWindow.h"
#include "CameraSelectionDialog.h"
#include "MediaSelectionDialog.h"
#include <QScreen>
#include <QWindow>
#include <QMessageBox>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QComboBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QHBoxLayout>

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
    QFrame *programFrame = this->findChild<QFrame*>("programPreview");
    if (previewFrame) m_cameraManager->setupInput(-1, previewFrame); // Slot -1: Preview (Staging)
    if (programFrame) m_cameraManager->setupInput(0, programFrame);  // Slot 0: Program (Live/Broadcast)
    
    QString bundlePath = QCoreApplication::applicationDirPath() + "/../Resources/effects";
    QString effectsPath = QDir(bundlePath).exists() ? bundlePath : QCoreApplication::applicationDirPath() + "/effects";
    
    m_effectsManager = new EffectsManager(effectsPath, this);
    
    QTreeWidget *tree = this->findChild<QTreeWidget*>("treeWidget_effects_cats");
    QStackedWidget *stack = this->findChild<QStackedWidget*>("stackedWidget_effects");
    if (tree && stack) {
        tree->setFixedWidth(130);
        m_effectsManager->setupUI(tree, stack, this);
        
        connect(m_effectsManager, &EffectsManager::effectApplied, m_cameraManager, &CameraManager::setEffect);
        connect(m_effectsManager, &EffectsManager::effectCleared, m_cameraManager, &CameraManager::clearEffect);
    }

    m_streamingManager = new StreamingManager(this);
    connect(m_streamingManager, &StreamingManager::errorOccurred, this, &MainWindow::onStreamingError);
    connect(m_cameraManager, &CameraManager::programFrameAvailable, m_streamingManager, &StreamingManager::pushFrame);

    m_recordingManager = new RecordingManager(this);
    connect(m_recordingManager, &RecordingManager::errorOccurred, this, &MainWindow::onStreamingError);
    connect(m_cameraManager, &CameraManager::programFrameAvailable, m_recordingManager, &RecordingManager::pushFrame);

    fixControlsLayout();
    setupUi();
    setupOutputControls();
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

void MainWindow::onStreamingError(const QString &msg) {
    QMessageBox::critical(this, "Streaming Error", msg);
}

void MainWindow::updateStreamingState() {
    QList<QString> urls;
    QToolButton *stream1Btn = this->findChild<QToolButton*>("stream1Btn1");
    QToolButton *stream2Btn = this->findChild<QToolButton*>("stream2Btn1");

    // Handle Stream 1
    if (stream1Btn && stream1Btn->isChecked()) {
        if (m_stream1Settings.isExternalMonitor) {
            if (!m_externalMonitor1) {
                m_externalMonitor1 = new ExternalMonitorWindow();
                connect(m_cameraManager, &CameraManager::programFrameAvailable, m_externalMonitor1, &ExternalMonitorWindow::updateFrame);
            }
            QList<QScreen*> screens = QGuiApplication::screens();
            QScreen *target = nullptr;
            for (QScreen *s : screens) if (s->name() == m_stream1Settings.monitorId) target = s;
            if (target) {
                m_externalMonitor1->show();
                m_externalMonitor1->raise();
                m_externalMonitor1->activateWindow();
                if (auto handle = m_externalMonitor1->windowHandle()) {
                    handle->setScreen(target);
                }
                m_externalMonitor1->setGeometry(target->geometry());
                m_externalMonitor1->showFullScreen();
            }
        } else if (m_stream1Settings.isConfigured) {
            urls.append(m_stream1Settings.server + "/" + m_stream1Settings.streamKey);
        } else {
            QMessageBox::warning(this, "Not Configured", "Stream 1 not configured.");
            stream1Btn->setChecked(false);
        }
    } else {
        if (m_externalMonitor1) {
            m_externalMonitor1->close();
            m_externalMonitor1->deleteLater();
            m_externalMonitor1 = nullptr;
        }
    }

    // Handle Stream 2
    if (stream2Btn && stream2Btn->isChecked()) {
        if (m_stream2Settings.isExternalMonitor) {
            if (!m_externalMonitor2) {
                m_externalMonitor2 = new ExternalMonitorWindow();
                connect(m_cameraManager, &CameraManager::programFrameAvailable, m_externalMonitor2, &ExternalMonitorWindow::updateFrame);
            }
            QList<QScreen*> screens = QGuiApplication::screens();
            QScreen *target = nullptr;
            for (QScreen *s : screens) if (s->name() == m_stream2Settings.monitorId) target = s;
            if (target) {
                m_externalMonitor2->show();
                m_externalMonitor2->raise();
                m_externalMonitor2->activateWindow();
                if (auto handle = m_externalMonitor2->windowHandle()) {
                    handle->setScreen(target);
                }
                m_externalMonitor2->setGeometry(target->geometry());
                m_externalMonitor2->showFullScreen();
            }
        } else if (m_stream2Settings.isConfigured) {
            urls.append(m_stream2Settings.server + "/" + m_stream2Settings.streamKey);
        } else {
            QMessageBox::warning(this, "Not Configured", "Stream 2 not configured.");
            stream2Btn->setChecked(false);
        }
    } else {
        if (m_externalMonitor2) {
            m_externalMonitor2->close();
            m_externalMonitor2->deleteLater();
            m_externalMonitor2 = nullptr;
        }
    }

    m_streamingManager->stopStreaming();
    if (!urls.isEmpty()) {
        m_streamingManager->startStreaming(urls, 1920, 1080, 30, 6000000);
    }
}

void MainWindow::setupOutputControls() {
    QComboBox *sizeCombo = this->findChild<QComboBox*>("outputSizeComboBox");
    QComboBox *fpsCombo = this->findChild<QComboBox*>("fpsComboBox");
    QComboBox *audioCombo = this->findChild<QComboBox*>("audioOutputComboBox");

    if (sizeCombo) {
        sizeCombo->clear();
        sizeCombo->addItem("1080p (FHD)", QSize(1920, 1080));
        sizeCombo->addItem("720p (HD)", QSize(1280, 720));
        sizeCombo->addItem("576p (PAL)", QSize(1024, 576));
        sizeCombo->addItem("480p (SD)", QSize(854, 480));
        sizeCombo->addItem("360p", QSize(640, 360));
        sizeCombo->addItem("240p", QSize(426, 240));
        sizeCombo->addItem("4K (UHD)", QSize(3840, 2160));
        
        auto updateSettings = [this, sizeCombo, fpsCombo]() {
            QSize size = sizeCombo->currentData().toSize();
            int fps = 30;
            if (fpsCombo) {
                QString fpsText = fpsCombo->currentText();
                fpsText.remove(" FPS");
                fps = fpsText.toInt();
            }
            if (m_cameraManager) m_cameraManager->setOutputSettings(size.width(), size.height(), fps);
        };

        connect(sizeCombo, &QComboBox::currentIndexChanged, this, updateSettings);
    }

    if (fpsCombo) {
        fpsCombo->clear();
        QStringList fpsOptions = {"24 FPS", "25 FPS", "30 FPS", "50 FPS", "60 FPS"};
        fpsCombo->addItems(fpsOptions);
        fpsCombo->setCurrentText("30 FPS");
        
        auto updateSettings = [this, sizeCombo, fpsCombo]() {
            int width = 1920, height = 1080;
            if (sizeCombo) {
                QSize size = sizeCombo->currentData().toSize();
                width = size.width();
                height = size.height();
            }
            QString fpsText = fpsCombo->currentText();
            fpsText.remove(" FPS");
            int fps = fpsText.toInt();
            if (m_cameraManager) m_cameraManager->setOutputSettings(width, height, fps);
        };
        connect(fpsCombo, &QComboBox::currentIndexChanged, this, updateSettings);
    }

    if (audioCombo && m_cameraManager) {
        audioCombo->clear();
        audioCombo->addItem("Default Audio Device", "");
        QList<DeviceInfo> audios = m_cameraManager->getAvailableAudioDevices();
        for (const auto &device : audios) {
            audioCombo->addItem(device.name, device.id);
        }
    }

    // Set icons for control panel
    QToolButton *swapBtn = this->findChild<QToolButton*>("controlsBtn1");
    QToolButton *takeBtn = this->findChild<QToolButton*>("controlsBtn2");
    QToolButton *recordBtn = this->findChild<QToolButton*>("recordBtn1");
    QToolButton *recordSetBtn = this->findChild<QToolButton*>("recordBtn2");
    QToolButton *stream1Btn = this->findChild<QToolButton*>("stream1Btn1");
    QToolButton *stream1SetBtn = this->findChild<QToolButton*>("stream1Btn2");
    QToolButton *stream2Btn = this->findChild<QToolButton*>("stream2Btn1");
    QToolButton *stream2SetBtn = this->findChild<QToolButton*>("stream2Btn2");
    QToolButton *textBtn = this->findChild<QToolButton*>("textOverlayBtn1");
    QToolButton *textSetBtn = this->findChild<QToolButton*>("textOverlayBtn2");

    if (swapBtn) {
        swapBtn->setIcon(QIcon(":/icons/Swap.png"));
        connect(swapBtn, &QToolButton::clicked, m_cameraManager, &CameraManager::swap);
    }
    if (takeBtn) {
        takeBtn->setIcon(QIcon(":/icons/Take.png"));
        connect(takeBtn, &QToolButton::clicked, m_cameraManager, &CameraManager::transition);
    }
    if (recordBtn) {
        recordBtn->setIcon(QIcon(":/icons/Record.png"));
        recordBtn->setCheckable(true);
        connect(recordBtn, &QToolButton::clicked, this, [this, recordBtn]() {
            if (m_recordingManager->isRecording()) {
                m_recordingManager->stopRecording();
                recordBtn->setStyleSheet("");
                recordBtn->setIcon(QIcon(":/icons/Record.png"));
            } else {
                if (!m_recordingSettings.isConfigured) {
                    QMessageBox::warning(this, "Not Configured", "Please configure recording settings first!");
                    recordBtn->setChecked(false);
                    return;
                }
                
                QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
                QString fileName = QString("/GoLive_Record_%1.%2").arg(timestamp).arg(m_recordingSettings.container);
                QString fullPath = m_recordingSettings.outputPath + fileName;
                
                QDir().mkpath(m_recordingSettings.outputPath);

                if (m_recordingManager->startRecording(fullPath, 1920, 1080, 30, m_recordingSettings.quality)) {
                    recordBtn->setStyleSheet("background-color: #e74c3c; color: white; border-radius: 4px;");
                    recordBtn->setIcon(QIcon(":/icons/Record_Active.png")); // Fallback if icon exists or just color change
                } else {
                    recordBtn->setChecked(false);
                }
            }
        });
    }
    if (recordSetBtn) {
        recordSetBtn->setIcon(QIcon(":/icons/Settings.png"));
        connect(recordSetBtn, &QToolButton::clicked, this, [this]() {
            RecordingSettingsDialog dialog(&m_recordingSettings, this);
            dialog.exec();
        });
    }
    if (stream1Btn) {
        stream1Btn->setIcon(QIcon(":/icons/Stream.png"));
        stream1Btn->setCheckable(true);
        connect(stream1Btn, &QToolButton::clicked, this, &MainWindow::updateStreamingState);
    }
    if (stream1SetBtn) {
        stream1SetBtn->setIcon(QIcon(":/icons/Settings.png"));
        connect(stream1SetBtn, &QToolButton::clicked, this, [this]() {
            StreamingSettingsDialog dialog("Stream 1 Settings", &m_stream1Settings, this);
            dialog.exec();
        });
    }
    if (stream2Btn) {
        stream2Btn->setIcon(QIcon(":/icons/Stream.png"));
        stream2Btn->setCheckable(true);
        connect(stream2Btn, &QToolButton::clicked, this, &MainWindow::updateStreamingState);
    }
    if (stream2SetBtn) {
        stream2SetBtn->setIcon(QIcon(":/icons/Settings.png"));
        connect(stream2SetBtn, &QToolButton::clicked, this, [this]() {
            StreamingSettingsDialog dialog("Stream 2 Settings", &m_stream2Settings, this);
            dialog.exec();
        });
    }
    if (textBtn) {
        textBtn->setIcon(QIcon(":/icons/Text.png"));
        connect(textBtn, &QToolButton::clicked, this, [this]() {
            m_textSettings.isVisible = !m_textSettings.isVisible;
            m_cameraManager->setTextOverlay(m_textSettings);
        });
    }
    if (textSetBtn) {
        textSetBtn->setIcon(QIcon(":/icons/Settings.png"));
        connect(textSetBtn, &QToolButton::clicked, this, [this]() {
            TextOverlaySettingsDialog dialog(&m_textSettings, this);
            if (dialog.exec() == QDialog::Accepted) {
                m_cameraManager->setTextOverlay(m_textSettings);
            }
        });
    }

    // Set icon sizes for visibility
    QList<QToolButton*> allBtns = {swapBtn, takeBtn, recordBtn, recordSetBtn, stream1Btn, stream1SetBtn, stream2Btn, stream2SetBtn, textBtn, textSetBtn};
    for (QToolButton* btn : allBtns) {
        if (btn) btn->setIconSize(QSize(24, 24));
    }

    // Connect streaming state changes to update UI indicators
    if (stream1Btn || stream2Btn) {
        connect(m_streamingManager, &StreamingManager::stateChanged, this, [this, stream1Btn, stream2Btn](StreamingManager::State state) {
            QString buttonStyle = "";
            QString buttonIcon = ":/icons/Stream.png";
            
            if (state == StreamingManager::State::Streaming) {
                buttonStyle = "background-color: #27ae60; color: white; border-radius: 4px;";
                buttonIcon = ":/icons/Stream_Active.png";  // Will fallback to regular icon if file doesn't exist
            } else if (state == StreamingManager::State::Connecting) {
                buttonStyle = "background-color: #f39c12; color: white; border-radius: 4px;";
            } else if (state == StreamingManager::State::Failed) {
                buttonStyle = "background-color: #e74c3c; color: white; border-radius: 4px;";
            }
            
            // Update active stream button(s)
            if (stream1Btn && stream1Btn->isChecked()) {
                stream1Btn->setStyleSheet(buttonStyle);
                stream1Btn->setIcon(QIcon(buttonIcon));
            }
            if (stream2Btn && stream2Btn->isChecked()) {
                stream2Btn->setStyleSheet(buttonStyle);
                stream2Btn->setIcon(QIcon(buttonIcon));
            }
            
            // Reset to default when streaming stops
            if (state == StreamingManager::State::Idle || state == StreamingManager::State::Stopping) {
                if (stream1Btn) {
                    stream1Btn->setStyleSheet("");
                    stream1Btn->setIcon(QIcon(":/icons/Stream.png"));
                }
                if (stream2Btn) {
                    stream2Btn->setStyleSheet("");
                    stream2Btn->setIcon(QIcon(":/icons/Stream.png"));
                }
            }
        });
    }
}
