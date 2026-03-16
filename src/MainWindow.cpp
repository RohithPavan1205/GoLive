#include "MainWindow.h"
#include "CameraSelectionDialog.h"
#include "MediaSelectionDialog.h"
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
        connect(recordBtn, &QToolButton::clicked, this, [this]() {
            if (!m_recordingSettings.isConfigured) {
                QMessageBox::warning(this, "Not Configured", "Please configure recording settings first!");
                return;
            }
            // Implementation later
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
        connect(stream1Btn, &QToolButton::clicked, this, [this]() {
            if (!m_stream1Settings.isConfigured) {
                QMessageBox::warning(this, "Not Configured", "Please configure Stream 1 settings first!");
                return;
            }
        });
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
        connect(stream2Btn, &QToolButton::clicked, this, [this]() {
            if (!m_stream2Settings.isConfigured) {
                QMessageBox::warning(this, "Not Configured", "Please configure Stream 2 settings first!");
                return;
            }
        });
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
        });
    }
    if (textSetBtn) {
        textSetBtn->setIcon(QIcon(":/icons/Settings.png"));
        connect(textSetBtn, &QToolButton::clicked, this, [this]() {
            TextOverlaySettingsDialog dialog(&m_textSettings, this);
            dialog.exec();
        });
    }

    // Set icon sizes for visibility
    QList<QToolButton*> allBtns = {swapBtn, takeBtn, recordBtn, recordSetBtn, stream1Btn, stream1SetBtn, stream2Btn, stream2SetBtn, textBtn, textSetBtn};
    for (QToolButton* btn : allBtns) {
        if (btn) btn->setIconSize(QSize(24, 24));
    }
}
