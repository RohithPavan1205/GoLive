#include "CameraManager.h"
#include <QVBoxLayout>
#include <QDebug>
#include <QVideoFrame>
#include <QPainter>
#include <QDateTime>

CameraManager::CameraManager(QObject *parent) : QObject(parent) {}

CameraManager::~CameraManager() {
    for (auto slot : m_slots.values()) {
        delete slot;
    }
}

QList<DeviceInfo> CameraManager::getAvailableCameras() {
    return NativeCamera::enumerateDevices();
}

QList<DeviceInfo> CameraManager::getAvailableAudioDevices() {
    return NativeCamera::enumerateAudioDevices();
}

void CameraManager::setupInput(int id, QFrame *container) {
    if (!container) return;
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

bool CameraManager::openCameraForInput(int id, const QString &deviceId, int width, int height, int fps) {
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
    m_currentEffectImage.load(pngPath);
    m_currentEffectOpening = opening;
    m_hasEffect = !m_currentEffectImage.isNull();
}

void CameraManager::clearEffect() {
    m_hasEffect = false;
    m_currentEffectImage = QImage();
}

void CameraManager::setOutputSettings(int width, int height, int fps) {
    m_outputWidth = width;
    m_outputHeight = height;
    m_outputFps = fps;
}

void CameraManager::onFrameAvailable(const QImage &image, int slotId) {
    if (m_slots.contains(slotId)) {
        QVideoFrame frame(image);
        m_slots[slotId]->videoSink->setVideoFrame(frame);
        
        if (slotId == m_previewSlotId && m_slots.contains(0)) {
            // FPS Control
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_outputFps > 0) {
                if (now - m_lastFrameTime < (1000 / m_outputFps)) return;
            }
            m_lastFrameTime = now;

            // Compose output: Use configured sizes
            QImage canvas(m_outputWidth, m_outputHeight, QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::black);
            
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            
            if (m_hasEffect) {
                // 1. Draw video in hole
                QRectF targetRect(
                    m_currentEffectOpening.x() * m_outputWidth,
                    m_currentEffectOpening.y() * m_outputHeight,
                    m_currentEffectOpening.width() * m_outputWidth,
                    m_currentEffectOpening.height() * m_outputHeight
                );
                painter.drawImage(targetRect, image);
                
                // 2. Draw Effect on top (scaled to canvas)
                painter.drawImage(canvas.rect(), m_currentEffectImage);
            } else {
                // No effect: Draw full screen
                painter.drawImage(canvas.rect(), image);
            }
            painter.end();
            
            m_slots[0]->videoSink->setVideoFrame(QVideoFrame(canvas));
        }
    }
}

bool CameraManager::isMuted(int id) const {
    return m_slots.contains(id) ? m_slots[id]->muted : false;
}
