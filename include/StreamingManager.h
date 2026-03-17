#ifndef STREAMINGMANAGER_H
#define STREAMINGMANAGER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QByteArray>
#include <QList>
#include <queue>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/error.h>
}

class StreamingManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Connecting,
        Streaming,
        Reconnecting,
        Failed,
        Stopping
    };

    struct Metrics {
        qint64 totalFrames = 0;
        qint64 droppedFrames = 0;
        double currentBitrate = 0; // kbps
        qint64 totalBytesSent = 0;
        int activeTargets = 0;
    };

    explicit StreamingManager(QObject *parent = nullptr);
    ~StreamingManager();

    bool startStreaming(const QList<QString> &urls, int width, int height, int fps, int bitrate);
    void stopStreaming();
    
    State state() const { return m_state; }
    Metrics metrics() const;
    bool isActive() const { return m_state == State::Streaming || m_state == State::Connecting || m_state == State::Reconnecting; }

public slots:
    void pushFrame(const QImage &image);
    void pushAudio(const QByteArray &data);

signals:
    void errorOccurred(const QString &msg);
    void stateChanged(StreamingManager::State newState);
    void metricsUpdated(const StreamingManager::Metrics &metrics);

private:
    void streamLoop();
    bool initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate);
    void cleanupFFmpeg();
    void updateState(State newState);
    void logFFmpegError(int err, const QString &context);

    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_shouldStop{false};
    
    // FFmpeg Contexts
    struct OutputTarget {
        QString url;
        AVFormatContext *formatCtx = nullptr;
        AVStream *videoStream = nullptr;
        AVStream *audioStream = nullptr;
        bool connected = false;
        int retryCount = 0;
        qint64 lastRetryTime = 0;
    };
    QList<OutputTarget> m_targets;
    QList<QString> m_initialUrls;

    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    
    int m_width = 1920, m_height = 1080, m_fps = 30, m_bitrate = 4000000;
    qint64 m_frameCount = 0;
    qint64 m_audioFrameCount = 0;
    qint64 m_streamStartTime = 0;

    // Metrics
    std::atomic<qint64> m_totalFrames{0};
    std::atomic<qint64> m_droppedFrames{0};
    std::atomic<qint64> m_totalBytesSent{0};
    qint64 m_lastBitrateCheckTime = 0;
    qint64 m_bytesSinceLastCheck = 0;

    QThread *m_workerThread = nullptr;
    QMutex m_queueMutex;
    std::queue<QImage> m_frameQueue;
    std::queue<QByteArray> m_audioQueue;

    const int MAX_QUEUE_SIZE = 30;
    const int MAX_RECONNECT_RETRIES = 5;
    const int RECONNECT_DELAY_MS = 2000;
};

#endif // STREAMINGMANAGER_H
