#include "NativeCamera.h"
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QVideoFrame>
#include <QDebug>

struct NativeCameraPrivate {
    QCamera *camera = nullptr;
    QMediaCaptureSession *captureSession = nullptr;
    QVideoSink *sink = nullptr;
    bool active = false;
};

NativeCamera::NativeCamera(QObject *parent) : QObject(parent) {
    m_opaque = new NativeCameraPrivate();
}

NativeCamera::~NativeCamera() {
    stop();
    delete (NativeCameraPrivate *)m_opaque;
}

void NativeCamera::start(const QString &deviceName) {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    stop();

    p->camera = new QCamera(this);
    // Note: deviceName mapping to QCameraDevice needed for full implementation
    p->captureSession = new QMediaCaptureSession(this);
    p->sink = new QVideoSink(this);

    p->captureSession->setCamera(p->camera);
    p->captureSession->setVideoSink(p->sink);

    connect(p->sink, &QVideoSink::videoFrameChanged, [this](const QVideoFrame &frame) {
        if (frame.isValid()) {
            QImage img = frame.toImage().convertToFormat(QImage::Format_RGBA8888);
            emit frameAvailable(img);
        }
    });

    p->camera->start();
    p->active = true;
}

void NativeCamera::startFile(const QString &filePath, bool loop) {
    // Media player implementation for Windows
    qDebug() << "Media file playback on Windows not yet fully implemented via NativeCamera.";
}

void NativeCamera::pause() {}
void NativeCamera::resume() {}
void NativeCamera::seek(double positionPercent) {}
void NativeCamera::setVolume(int volume) {}
void NativeCamera::setMuted(bool muted) {}

void NativeCamera::stop() {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (p->camera) {
        p->camera->stop();
        delete p->camera;
        p->camera = nullptr;
    }
    if (p->captureSession) {
        delete p->captureSession;
        p->captureSession = nullptr;
    }
    if (p->sink) {
        delete p->sink;
        p->sink = nullptr;
    }
    p->active = false;
}

bool NativeCamera::isActive() const {
    return ((NativeCameraPrivate *)m_opaque)->active;
}
