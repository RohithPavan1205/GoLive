#include "CameraManager.h"
#include <QVBoxLayout>
#include <QDebug>
#include <QVideoFrame>
#include <QPainter>
#include <QDateTime>
#ifdef __APPLE__
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>
#endif

static double mach_time_to_ms(uint64_t mach_time) {
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    return (double)mach_time * (double)timebase.numer / (double)timebase.denom / 1000000.0;
}

CameraManager::CameraManager(QObject *parent) : QObject(parent) {
    m_previewCanvas = QImage(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    m_programCanvas = QImage(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    m_renderThreadRunning = true;
    m_renderThread = std::thread(&CameraManager::renderLoop, this);
}

CameraManager::~CameraManager() {
    m_renderThreadRunning = false;
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }
    
    std::lock_guard<std::mutex> lock(m_slotMutex);
    for (auto slot : m_slots.values()) {
        delete slot;
    }
    m_slots.clear();
}

QList<DeviceInfo> CameraManager::getAvailableCameras() {
    return NativeCamera::enumerateDevices();
}

QList<DeviceInfo> CameraManager::getAvailableAudioDevices() {
    return NativeCamera::enumerateAudioDevices();
}

void CameraManager::setupInput(int id, QFrame *container) {
    if (!container) return;
    std::lock_guard<std::mutex> lock(m_slotMutex);
    if (m_slots.contains(id)) delete m_slots[id];

    InputSlot *slot = new InputSlot();
    slot->container = container;
    container->setStyleSheet("background-color: black; border: 1px solid #333333;");
    
    slot->videoWidget = new QVideoWidget(container);
    slot->videoSink = slot->videoWidget->videoSink();
    
    if (container->layout()) {
        QLayoutItem *child;
        while ((child = container->layout()->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        delete container->layout();
    }

    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(slot->videoWidget);
    
    m_slots[id] = slot;
    slot->videoWidget->show();
}

void CameraManager::setPreviewSlot(int id) {
    if (m_previewSlotId == id) return;
    
    // Snap current Preview for dissolve
    if (!m_previewIsDissolving) {
        m_previewDissolveSourceFrame = m_previewCanvas;
        m_previewDissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_previewIsDissolving = true;
    }
    
    m_previewSlotId = id;
}

bool CameraManager::openCameraForInput(int id, const QString &deviceId, int width, int height, int fps) {
    // Snap for dissolve if this slot is active
    if (id == m_programSlotId && !m_isDissolving) {
        m_dissolveSourceFrame = m_programCanvas;
        m_dissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_isDissolving = true;
    } else if (id == m_previewSlotId && !m_previewIsDissolving) {
        m_previewDissolveSourceFrame = m_previewCanvas;
        m_previewDissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_previewIsDissolving = true;
    }

    if (!m_slots.contains(id)) return false;
    InputSlot *slot = m_slots[id];
    if (slot->nativeCamera) { slot->nativeCamera->stop(); delete slot->nativeCamera; }

    slot->nativeCamera = new NativeCamera(this);
    connect(slot->nativeCamera, &NativeCamera::frameAvailable, this, [this, id](const QImage &image) {
        this->onFrameAvailable(image, id);
    });

    bool success = slot->nativeCamera->start(deviceId, width, height, fps);
    if (success) {
        slot->currentSourceId = deviceId;
        slot->isPlaying = true;
        slot->nativeCamera->setMuted(slot->muted);
    }
    return success;
}

bool CameraManager::openFileForInput(int id, const QString &filePath, bool loop) {
    // Snap for dissolve if this slot is active
    if (id == m_programSlotId && !m_isDissolving) {
        m_dissolveSourceFrame = m_programCanvas;
        m_dissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_isDissolving = true;
    } else if (id == m_previewSlotId && !m_previewIsDissolving) {
        m_previewDissolveSourceFrame = m_previewCanvas;
        m_previewDissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_previewIsDissolving = true;
    }

    if (!m_slots.contains(id)) return false;
    InputSlot *slot = m_slots[id];
    if (slot->nativeCamera) { slot->nativeCamera->stop(); delete slot->nativeCamera; }

    slot->nativeCamera = new NativeCamera(this);
    connect(slot->nativeCamera, &NativeCamera::frameAvailable, this, [this, id](const QImage &image) {
        this->onFrameAvailable(image, id);
    });
    connect(slot->nativeCamera, &NativeCamera::positionChanged, this, [this, id](double percent) {
        emit mediaPositionChanged(id, percent);
    });
    connect(slot->nativeCamera, &NativeCamera::playbackFinished, this, [this, id]() {
        emit mediaFinished(id);
    });

    bool success = slot->nativeCamera->startFile(QUrl::fromLocalFile(filePath), loop);
    if (success) {
        slot->currentSourceId = filePath;
        slot->isPlaying = true;
        slot->nativeCamera->setMuted(slot->muted);
    }
    return success;
}

void CameraManager::togglePlayPause(int id) {
    if (!m_slots.contains(id)) return;
    InputSlot *slot = m_slots[id];
    if (!slot->nativeCamera) return;

    if (slot->isPlaying) {
        slot->nativeCamera->pause();
        slot->isPlaying = false;
    } else {
        slot->nativeCamera->resume();
        slot->isPlaying = true;
    }
}

void CameraManager::seek(int id, double percent) {
    if (m_slots.contains(id) && m_slots[id]->nativeCamera) {
        m_slots[id]->nativeCamera->seek(percent);
    }
}

void CameraManager::setMuted(int id, bool muted) {
    if (m_slots.contains(id)) {
        m_slots[id]->muted = muted;
        if (m_slots[id]->nativeCamera) {
            m_slots[id]->nativeCamera->setMuted(muted);
        }
    }
}

void CameraManager::setEffect(const QString &pngPath, const QRectF &opening) {
    m_previewEffectImage.load(pngPath);
    m_previewEffectOpening = opening;
    m_previewHasEffect = true;
    m_previewEffectTargetAlpha = 1.0f;
}

void CameraManager::clearEffect() {
    m_previewEffectTargetAlpha = 0.0f;
    // We don't set m_previewHasEffect=false here directly; renderLoop will handle it when alpha hits 0
}

void CameraManager::setTextOverlay(const TextOverlaySettingsDialog::Settings &settings) {
    m_previewText = settings;
}

void CameraManager::transition() {
    // Kick off dissolve
    if (!m_isDissolving) {
        m_dissolveSourceFrame = m_programCanvas; // Snap current Program
        m_dissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_isDissolving = true;
    }

    m_programSlotId = m_previewSlotId;
    m_programEffectImage = m_previewEffectImage;
    m_programEffectOpening = m_previewEffectOpening;
    m_programHasEffect = m_previewHasEffect;
    m_programEffectAlpha = m_previewEffectAlpha;
    m_programEffectTargetAlpha = m_previewEffectTargetAlpha;
    
    m_programText = m_previewText;
}

void CameraManager::swap() {
    // Kick off dissolve
    if (!m_isDissolving) {
        m_dissolveSourceFrame = m_programCanvas;
        m_dissolveStartTime = QDateTime::currentMSecsSinceEpoch();
        m_isDissolving = true;
    }

    // Swap Slots
    int tmpSlot = m_previewSlotId;
    m_previewSlotId = m_programSlotId;
    m_programSlotId = tmpSlot;

    // Swap Effect State
    QImage tmpImg = m_previewEffectImage;
    m_previewEffectImage = m_programEffectImage;
    m_programEffectImage = tmpImg;

    QRectF tmpRect = m_previewEffectOpening;
    m_previewEffectOpening = m_programEffectOpening;
    m_programEffectOpening = tmpRect;

    bool tmpHas = m_previewHasEffect;
    m_previewHasEffect = m_programHasEffect;
    m_programHasEffect = tmpHas;

    float tmpAlpha = m_previewEffectAlpha;
    m_previewEffectAlpha = m_programEffectAlpha;
    m_programEffectAlpha = tmpAlpha;

    float tmpTAlpha = m_previewEffectTargetAlpha;
    m_previewEffectTargetAlpha = m_programEffectTargetAlpha;
    m_programEffectTargetAlpha = tmpTAlpha;

    // Swap Text
    TextOverlaySettingsDialog::Settings tmpText = m_previewText;
    m_previewText = m_programText;
    m_programText = tmpText;
}

void CameraManager::setOutputSettings(int width, int height, int fps) {
    m_outputWidth = width;
    m_outputHeight = height;
    m_outputFps = fps;
}

void CameraManager::onFrameAvailable(const QImage &image, int slotId) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_slots.contains(slotId)) {
        // Just stash for the render thread
        if (slotId == m_previewSlotId) {
            m_latestPreviewSourceFrame = image;
        }
        if (slotId == m_programSlotId) {
            m_latestProgramSourceFrame = image;
        }
        
        // Native sink update for non-monitored inputs
        if (slotId != m_previewSlotId && slotId != m_programSlotId) {
            m_slots[slotId]->videoSink->setVideoFrame(QVideoFrame(image));
        }
    }
}

void CameraManager::renderLoop() {
#ifdef __APPLE__
    // macOS: Set to high priority (The OBS Way)
    thread_precedence_policy_data_t policy = {32}; // High priority
    thread_policy_set(mach_thread_self(), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&policy, THREAD_PRECEDENCE_POLICY_COUNT);
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    
    // Lock to partition (simulated affinity on macOS)
    thread_affinity_policy_data_t affinity_policy = {1}; 
    thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, (thread_policy_t)&affinity_policy, THREAD_AFFINITY_POLICY_COUNT);
#endif

    uint64_t next_frame_time_ns = 0;
    while (m_renderThreadRunning) {
        double now_ms = mach_time_to_ms(mach_absolute_time());
        uint64_t now_ns = (uint64_t)(now_ms * 1000000.0);
        
        if (next_frame_time_ns == 0) next_frame_time_ns = now_ns;
        
        // Busy Wait until it's time (Sub-ms precision)
        while (now_ns < next_frame_time_ns && m_renderThreadRunning) {
            std::this_thread::yield();
            now_ms = mach_time_to_ms(mach_absolute_time());
            now_ns = (uint64_t)(now_ms * 1000000.0);
        }
        
        if (!m_renderThreadRunning) break;

        // Render Tick
        QImage previewFrame, programFrame;
        {
            std::lock_guard<std::mutex> frame_lock(m_frameMutex);
            previewFrame = m_latestPreviewSourceFrame;
            programFrame = m_latestProgramSourceFrame;
        }

        std::lock_guard<std::mutex> slot_lock(m_slotMutex);
        int fps = (m_outputFps > 0) ? m_outputFps : 30;
        float dt = 1.0f / fps;

        // Animate Alphas
        auto updateAlpha = [&](float &current, float target) {
            if (current < target) {
                current = qMin(target, current + 5.0f * dt); // Fade over ~200ms
            } else if (current > target) {
                current = qMax(target, current - 5.0f * dt);
            }
        };
        updateAlpha(m_previewEffectAlpha, m_previewEffectTargetAlpha);
        updateAlpha(m_programEffectAlpha, m_programEffectTargetAlpha);

        // 1. Render Preview
        if (!previewFrame.isNull() && m_slots.contains(-1)) {
            m_previewCanvas.fill(Qt::black);
            QPainter painter(&m_previewCanvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(m_previewCanvas.rect(), previewFrame);

            if (m_previewEffectAlpha > 0.0f) {
                painter.setOpacity(m_previewEffectAlpha);
                if (m_previewHasEffect) {
                    QRectF targetRect(m_previewEffectOpening.x() * m_outputWidth, m_previewEffectOpening.y() * m_outputHeight, 
                                     m_previewEffectOpening.width() * m_outputWidth, m_previewEffectOpening.height() * m_outputHeight);
                    painter.drawImage(targetRect, previewFrame); // Redraw source in "opening" if needed or just overlay
                    painter.drawImage(m_previewCanvas.rect(), m_previewEffectImage);
                }
                painter.setOpacity(1.0f);
            }

            if (m_previewText.isConfigured && m_previewText.isVisible) {
                painter.setPen(m_previewText.color);
                painter.setFont(QFont(m_previewText.font, m_previewText.size));
                QRect textRect = painter.fontMetrics().boundingRect(m_previewText.text);
                painter.drawText((m_outputWidth - textRect.width()) / 2, m_outputHeight - 50, m_previewText.text);
            }
            painter.end();
            
            // Handle Preview Dissolve
            if (m_previewIsDissolving) {
                qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_previewDissolveStartTime;
                float progress = qMin(1.0f, (float)elapsed / m_dissolveDuration);
                
                QImage blendCanvas(m_outputWidth, m_outputHeight, QImage::Format_ARGB32_Premultiplied);
                QPainter blendPainter(&blendCanvas);
                blendPainter.setCompositionMode(QPainter::CompositionMode_Source);
                blendPainter.drawImage(0, 0, m_previewDissolveSourceFrame);
                blendPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                blendPainter.setOpacity(progress);
                blendPainter.drawImage(0, 0, m_previewCanvas);
                blendPainter.end();
                
                m_slots[-1]->videoSink->setVideoFrame(QVideoFrame(blendCanvas));
                if (progress >= 1.0f) m_previewIsDissolving = false;
            } else {
                m_slots[-1]->videoSink->setVideoFrame(QVideoFrame(m_previewCanvas));
            }
        }

        // 2. Render Program
        if (!programFrame.isNull() && m_slots.contains(0)) {
            m_programCanvas.fill(Qt::black);
            QPainter painter(&m_programCanvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(m_programCanvas.rect(), programFrame);

            if (m_programEffectAlpha > 0.0f) {
                painter.setOpacity(m_programEffectAlpha);
                if (m_programHasEffect) {
                    QRectF targetRect(m_programEffectOpening.x() * 1920, m_programEffectOpening.y() * 1080, 
                                     m_programEffectOpening.width() * 1920, m_programEffectOpening.height() * 1080);
                    painter.drawImage(targetRect, programFrame);
                    painter.drawImage(m_programCanvas.rect(), m_programEffectImage);
                }
                painter.setOpacity(1.0f);
            }

            if (m_programText.isConfigured && m_programText.isVisible) {
                painter.setPen(m_programText.color);
                painter.setFont(QFont(m_programText.font, m_programText.size));
                QRect textRect = painter.fontMetrics().boundingRect(m_programText.text);
                painter.drawText((1920 - textRect.width()) / 2, 1080 - 50, m_programText.text);
            }
            painter.end();

            // Handle Dissolve
            if (m_isDissolving) {
                qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_dissolveStartTime;
                float progress = qMin(1.0f, (float)elapsed / m_dissolveDuration);
                
                QImage blendCanvas(1920, 1080, QImage::Format_ARGB32_Premultiplied);
                QPainter blendPainter(&blendCanvas);
                blendPainter.setCompositionMode(QPainter::CompositionMode_Source);
                blendPainter.drawImage(0, 0, m_dissolveSourceFrame);
                blendPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                blendPainter.setOpacity(progress);
                blendPainter.drawImage(0, 0, m_programCanvas);
                blendPainter.end();
                
                m_slots[0]->videoSink->setVideoFrame(QVideoFrame(blendCanvas));
                emit programFrameAvailable(blendCanvas);

                if (progress >= 1.0f) {
                    m_isDissolving = false;
                }
            } else {
                m_slots[0]->videoSink->setVideoFrame(QVideoFrame(m_programCanvas));
                emit programFrameAvailable(m_programCanvas);
            }
        }

        // Log Stats every 60 rendered frames
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            qDebug() << "[RenderThread] Stats: Avg Jitter:" << QString::number(m_programJitter, 'f', 2) 
                     << "ms, Max:" << QString::number(m_maxProgramJitter, 'f', 2) << "ms";
            m_maxProgramJitter = 0.0;
        }

        next_frame_time_ns += (uint64_t)(1000000000ULL / fps);
    }
}

bool CameraManager::isMuted(int id) const {
    return m_slots.contains(id) ? m_slots[id]->muted : false;
}
