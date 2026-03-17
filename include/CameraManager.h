#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QFrame>
#include <QMap>
#include <vector>
#include <memory>
#include <QVideoWidget>
#include <QVideoSink>
#include <QColor>
#include <QFont>
#include "NativeCamera.h"
#include "TextOverlaySettingsDialog.h"
#include <thread>
#include <atomic>
#include <mutex>

class CameraManager : public QObject {
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    QList<DeviceInfo> getAvailableCameras();
    QList<DeviceInfo> getAvailableAudioDevices();
    
    void setupInput(int id, QFrame *container);
    
    // Actions
    bool openCameraForInput(int id, const QString &deviceId, int width = 1280, int height = 720, int fps = 30);
    bool openFileForInput(int id, const QString &filePath, bool loop = false);
    
    void togglePlayPause(int id);
    void seek(int id, double percent);
    void setMuted(int id, bool muted);
    bool isMuted(int id) const;

    void setOutputSettings(int width, int height, int fps);

    void setPreviewSlot(int id);
    int getPreviewSlot() const { return m_previewSlotId; }

    // Effect Support
    void setEffect(const QString &pngPath, const QRectF &opening);
    void clearEffect();
    
    // Text Overlay Support
    void setTextOverlay(const TextOverlaySettingsDialog::Settings &settings);

    void transition();
    void swap();

signals:
    void mediaPositionChanged(int id, double percent);
    void mediaFinished(int id);
    void programFrameAvailable(const QImage &image);
    void programAudioAvailable(const QByteArray &data);

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
    int m_programSlotId = -1;

    // Effect State (Preview/Staging)
    QImage m_previewEffectImage;
    QRectF m_previewEffectOpening;
    bool m_previewHasEffect = false;
    float m_previewEffectAlpha = 0.0f;
    float m_previewEffectTargetAlpha = 0.0f;

    // Effect State (Program/Live)
    QImage m_programEffectImage;
    QRectF m_programEffectOpening;
    bool m_programHasEffect = false;
    float m_programEffectAlpha = 0.0f;
    float m_programEffectTargetAlpha = 0.0f;

    // Dissolve Transition State
    std::atomic<bool> m_isDissolving{false};
    qint64 m_dissolveStartTime = 0;
    qint64 m_dissolveDuration = 300; // ms
    QImage m_dissolveSourceFrame;

    std::atomic<bool> m_previewIsDissolving{false};
    qint64 m_previewDissolveStartTime = 0;
    QImage m_previewDissolveSourceFrame;

    // Text Overlay State
    TextOverlaySettingsDialog::Settings m_previewText;
    TextOverlaySettingsDialog::Settings m_programText;

    // Output Configuration
    int m_outputWidth = 1920;
    int m_outputHeight = 1080;
    int m_outputFps = 30;
    qint64 m_lastPreviewFrameTime = 0;
    qint64 m_lastProgramFrameTime = 0;
    double m_previewJitter = 0.0;
    double m_programJitter = 0.0;
    double m_maxPreviewJitter = 0.0;
    double m_maxProgramJitter = 0.0;

    // Fixed-interval Render Thread (The OBS Way)
    std::thread m_renderThread;
    std::atomic<bool> m_renderThreadRunning{false};
    std::mutex m_frameMutex;
    std::mutex m_slotMutex; // Separate mutex for slots
    QImage m_latestPreviewSourceFrame;
    QImage m_latestProgramSourceFrame;
    QImage m_previewCanvas;
    QImage m_programCanvas;
    void renderLoop();
};

#endif // CAMERAMANAGER_H
