#include "../include/CameraManager.h"
#include <QMediaDevices>
#include <QCameraFormat>
#include <QDebug>

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
    , m_currentFrameRate(30)
{
    enumerateCameras();
}

CameraManager::~CameraManager() {
    closeCamera();
}

void CameraManager::enumerateCameras() {
    m_availableCameras.clear();
    
    const auto cameras = QMediaDevices::videoInputs();
    
    for (const auto &cameraDevice : cameras) {
        CameraInfo info;
        info.deviceId = cameraDevice.id();
        info.description = cameraDevice.description();
        info.displayName = cameraDevice.description();
        
        // Get supported formats
        QCamera tempCamera(cameraDevice);
        const auto formats = tempCamera.cameraFormat();
        if (formats.isNull()) {
            qWarning() << "Could not get format for:" << info.displayName;
        }
        
        m_availableCameras.push_back(info);
        
        qDebug() << "Found camera:" << info.displayName << "ID:" << info.deviceId;
    }
}

std::vector<CameraInfo> CameraManager::getAvailableCameras() {
    return m_availableCameras;
}

CameraInfo CameraManager::getCameraInfo(const QString &deviceId) {
    for (const auto &info : m_availableCameras) {
        if (info.deviceId == deviceId) {
            return info;
        }
    }
    return CameraInfo();
}

bool CameraManager::openCamera(const QString &deviceId) {
    if (m_camera && m_currentCameraId == deviceId) {
        // Already open
        return true;
    }
    
    closeCamera();
    
    // Find the camera device
    const auto cameras = QMediaDevices::videoInputs();
    QCameraDevice selectedDevice;
    
    for (const auto &device : cameras) {
        if (device.id() == deviceId) {
            selectedDevice = device;
            break;
        }
    }
    
    if (selectedDevice.isNull()) {
        emit errorOccurred(QString("Camera device not found: %1").arg(deviceId));
        return false;
    }
    
    // Create new camera and session
    m_camera = std::make_unique<QCamera>(selectedDevice);
    m_captureSession = std::make_unique<QMediaCaptureSession>();
    m_videoSink = std::make_unique<QVideoSink>();
    
    // Connect frame available signal
    connect(m_videoSink.get(), &QVideoSink::videoFrameChanged,
            this, &CameraManager::onVideoFrameAvailable);
    
    // Setup capture session
    m_captureSession->setCamera(m_camera.get());
    m_captureSession->setVideoSink(m_videoSink.get());
    
    // Set initial resolution
    setResolution(640, 480);
    
    // Start camera
    m_camera->start();
    
    qDebug() << "Camera initialized and started for device:" << deviceId;
    
    m_currentCameraId = deviceId;
    
    qDebug() << "Camera opened successfully:" << deviceId;
    emit cameraOpened(deviceId);
    
    return true;
}

void CameraManager::closeCamera() {
    if (m_camera) {
        m_camera->stop();
        m_camera.reset();
    }
    
    if (m_captureSession) {
        m_captureSession.reset();
    }
    
    if (m_videoSink) {
        m_videoSink.reset();
    }
    
    m_currentCameraId.clear();
    
    emit cameraClosed();
}

bool CameraManager::isConnected() const {
    return m_camera != nullptr && m_camera->isActive();
}

QString CameraManager::getCurrentCameraId() const {
    return m_currentCameraId;
}

QCamera* CameraManager::getCurrentCamera() const {
    return m_camera.get();
}

QMediaCaptureSession* CameraManager::getMediaSession() const {
    return m_captureSession.get();
}

QVideoSink* CameraManager::getVideoSink() const {
    return m_videoSink.get();
}

bool CameraManager::setResolution(int width, int height) {
    if (!m_camera) {
        return false;
    }
    
    // Find the best matching format
    const auto formats = m_camera->cameraDevice().videoFormats();
    
    QCameraFormat bestFormat;
    int minDifference = INT_MAX;
    
    for (const auto &format : formats) {
        int difference = abs(format.resolution().width() - width) + 
                        abs(format.resolution().height() - height);
        
        if (difference < minDifference) {
            minDifference = difference;
            bestFormat = format;
        }
    }
    
    if (!bestFormat.isNull()) {
        m_camera->setCameraFormat(bestFormat);
        m_currentResolution = bestFormat.resolution();
        emit resolutionChanged(m_currentResolution.width(), m_currentResolution.height());
        return true;
    }
    
    return false;
}

bool CameraManager::setFrameRate(int fps) {
    if (!m_camera) {
        return false;
    }
    
    m_currentFrameRate = fps;
    emit frameRateChanged(fps);
    return true;
}

QSize CameraManager::getCurrentResolution() const {
    return m_currentResolution;
}

int CameraManager::getCurrentFrameRate() const {
    return m_currentFrameRate;
}

void CameraManager::setQualityPreset(QualityPreset preset) {
    switch (preset) {
        case LowQuality:
            setResolution(320, 240);
            setFrameRate(15);
            break;
        case MediumQuality:
            setResolution(640, 480);
            setFrameRate(24);
            break;
        case HighQuality:
            setResolution(1280, 720);
            setFrameRate(30);
            break;
    }
}

void CameraManager::onVideoFrameAvailable(const QVideoFrame &frame) {
    if (frame.width() <= 0 || frame.height() <= 0) {
        qDebug() << "Received invalid frame dimensions";
        return;
    }
    
    qDebug() << "Frame available:" << frame.width() << "x" << frame.height();
    emit frameAvailable(frame);
}
