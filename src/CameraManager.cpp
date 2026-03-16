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
    m_previewEffectImage.load(pngPath);
    m_previewEffectOpening = opening;
    m_previewHasEffect = !m_previewEffectImage.isNull();
}

void CameraManager::clearEffect() {
    m_previewHasEffect = false;
    m_previewEffectImage = QImage();
}

void CameraManager::setTextOverlay(const TextOverlaySettingsDialog::Settings &settings) {
    m_previewText = settings;
}

void CameraManager::transition() {
    m_programSlotId = m_previewSlotId;
    m_programEffectImage = m_previewEffectImage;
    m_programEffectOpening = m_previewEffectOpening;
    m_programHasEffect = m_previewHasEffect;
    
    m_programText = m_previewText;
}

void CameraManager::swap() {
    // Swap Slots
    int tmpSlot = m_previewSlotId;
    m_previewSlotId = m_programSlotId;
    m_programSlotId = tmpSlot;

    // Swap Effects
    QImage tmpImg = m_previewEffectImage;
    m_previewEffectImage = m_programEffectImage;
    m_programEffectImage = tmpImg;

    QRectF tmpRect = m_previewEffectOpening;
    m_previewEffectOpening = m_programEffectOpening;
    m_programEffectOpening = tmpRect;

    bool tmpHas = m_previewHasEffect;
    m_previewHasEffect = m_programHasEffect;
    m_programHasEffect = tmpHas;

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
    if (m_slots.contains(slotId)) {
        QVideoFrame frame(image);
        m_slots[slotId]->videoSink->setVideoFrame(frame);
        
        // 1. Send to Preview Monitor (Left - Slot -1)
        if (slotId == m_previewSlotId && m_slots.contains(-1)) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            static qint64 lastPreviewTime = 0;
            if (m_outputFps <= 0 || (now - lastPreviewTime >= (1000 / m_outputFps))) {
                lastPreviewTime = now;
                
                QImage canvas(m_outputWidth, m_outputHeight, QImage::Format_ARGB32_Premultiplied);
                canvas.fill(Qt::black);
                QPainter painter(&canvas);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::TextAntialiasing);
                
                if (m_previewHasEffect) {
                    QRectF targetRect(m_previewEffectOpening.x() * m_outputWidth, m_previewEffectOpening.y() * m_outputHeight, 
                                     m_previewEffectOpening.width() * m_outputWidth, m_previewEffectOpening.height() * m_outputHeight);
                    painter.drawImage(targetRect, image);
                    painter.drawImage(canvas.rect(), m_previewEffectImage);
                } else {
                    painter.drawImage(canvas.rect(), image);
                }

                // Render Staged Text Overlay
                if (m_previewText.isConfigured && m_previewText.isVisible) {
                    painter.setPen(m_previewText.color);
                    QFont font(m_previewText.font, m_previewText.size);
                    painter.setFont(font);
                    // Center bottom position logic
                    QRect textRect = painter.fontMetrics().boundingRect(m_previewText.text);
                    int x = (m_outputWidth - textRect.width()) / 2;
                    int y = m_outputHeight - 50; 
                    painter.drawText(x, y, m_previewText.text);
                }

                painter.end();
                m_slots[-1]->videoSink->setVideoFrame(QVideoFrame(canvas));
            }
        }

        // 2. Send to Program Monitor (Right - Slot 0)
        if (slotId == m_programSlotId && m_slots.contains(0)) {
            QImage canvas(1920, 1080, QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::black);
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            
            if (m_programHasEffect) {
                QRectF targetRect(m_programEffectOpening.x() * 1920, m_programEffectOpening.y() * 1080, 
                                 m_programEffectOpening.width() * 1920, m_programEffectOpening.height() * 1080);
                painter.drawImage(targetRect, image);
                painter.drawImage(canvas.rect(), m_programEffectImage);
            } else {
                painter.drawImage(canvas.rect(), image);
            }

            // Render Live Text Overlay
            if (m_programText.isConfigured && m_programText.isVisible) {
                painter.setPen(m_programText.color);
                QFont font(m_programText.font, m_programText.size);
                painter.setFont(font);
                QRect textRect = painter.fontMetrics().boundingRect(m_programText.text);
                int x = (1920 - textRect.width()) / 2;
                int y = 1080 - 50; 
                painter.drawText(x, y, m_programText.text);
            }

            painter.end();
            m_slots[0]->videoSink->setVideoFrame(QVideoFrame(canvas));
        }
    }
}

bool CameraManager::isMuted(int id) const {
    return m_slots.contains(id) ? m_slots[id]->muted : false;
}
