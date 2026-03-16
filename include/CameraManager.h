#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QFrame>
#include <QMap>
#include <vector>
#include <memory>
#include <QVideoWidget>
#include <QVideoSink>
#include "NativeCamera.h"

class CameraManager : public QObject {
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    QList<DeviceInfo> getAvailableCameras();
    
    void setupInput(int id, QFrame *container);
    
    // Actions
    bool openCameraForInput(int id, const QString &deviceId, int width = 1280, int height = 720, int fps = 30);
    bool openFileForInput(int id, const QString &filePath, bool loop = false);
    
    void togglePlayPause(int id);
    void seek(int id, double percent);
    void setMuted(int id, bool muted);
    bool isMuted(int id) const;

    void setPreviewSlot(int id) { m_previewSlotId = id; }
    int getPreviewSlot() const { return m_previewSlotId; }

signals:
    void mediaPositionChanged(int id, double percent);
    void mediaFinished(int id);

private slots:
    void onFrameAvailable(const QImage &image, int slotId);

private:
    struct InputSlot {
        NativeCamera *nativeCamera = nullptr;
        QVideoWidget *videoWidget = nullptr;
        QVideoSink *videoSink = nullptr;
        QFrame *container = nullptr;
        QString currentSourceId;
        bool muted = false;
        bool isPlaying = false;
        
        ~InputSlot() {
            delete nativeCamera;
        }
    };

    QMap<int, InputSlot*> m_slots;
    int m_previewSlotId = -1;
};

#endif // CAMERAMANAGER_H
