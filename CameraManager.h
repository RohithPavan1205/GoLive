#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QMediaCaptureSession>
#include <QCamera>
#include <QVideoFrame>
#include <QVideoSink>
#include <memory>
#include <vector>

struct CameraInfo {
    QString deviceId;
    QString description;
    QString displayName;
};

class CameraManager : public QObject {
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    // Camera enumeration
    std::vector<CameraInfo> getAvailableCameras();
    CameraInfo getCameraInfo(const QString &deviceId);

    // Camera control
    bool openCamera(const QString &deviceId);
    void closeCamera();
    bool isConnected() const;
    
    // Get current camera info
    QString getCurrentCameraId() const;
    QCamera* getCurrentCamera() const;
    QMediaCaptureSession* getMediaSession() const;
    QVideoSink* getVideoSink() const;

    // Resolution/quality management
    bool setResolution(int width, int height);
    bool setFrameRate(int fps);
    QSize getCurrentResolution() const;
    int getCurrentFrameRate() const;

    // Quality preset (for preview vs output)
    enum QualityPreset {
        LowQuality,      // 320x240, 15fps
        MediumQuality,   // 640x480, 24fps
        HighQuality      // 1280x720, 30fps
    };
    void setQualityPreset(QualityPreset preset);

signals:
    void cameraOpened(const QString &deviceId);
    void cameraClosed();
    void frameAvailable(const QVideoFrame &frame);
    void resolutionChanged(int width, int height);
    void frameRateChanged(int fps);
    void errorOccurred(const QString &errorMessage);

private slots:
    void onVideoFrameAvailable(const QVideoFrame &frame);

private:
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_captureSession;
    std::unique_ptr<QVideoSink> m_videoSink;
    
    QString m_currentCameraId;
    QSize m_currentResolution;
    int m_currentFrameRate;
    
    void enumerateCameras();
    std::vector<CameraInfo> m_availableCameras;
};

#endif // CAMERAMANAGER_H
