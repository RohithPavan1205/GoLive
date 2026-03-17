#ifndef STREAMINGMANAGER_H
#define STREAMINGMANAGER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QByteArray>
#include <QList>
#include <QElapsedTimer>
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
#include <libavutil/audio_fifo.h>
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
    // === OBS-Style Architecture ===
    // Two independent threads: one for video, one for audio+sync+mux
    void videoEncodeLoop();
    void audioMuxLoop();

    bool initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate);
    void cleanupFFmpeg();
    void updateState(State newState);
    void logFFmpegError(int err, const QString &context);

    // Write an encoded packet to all connected targets
    void writePacketToTargets(AVPacket *pkt, bool isVideo);

    // Reconnection health check
    void checkTargetHealth();

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

    // === Master Clock (OBS-Style) ===
    // A single monotonic timer started when streaming begins.
    // Both video and audio PTS are derived from this clock.
    QElapsedTimer m_masterClock;

    // === Video Timing ===
    // Video PTS is frame-index based: pts = frame_index
    // Time base: {1, fps} → FFmpeg converts to stream timebase via rescale_ts
    qint64 m_videoFrameIndex = 0;

    // === Audio Timing ===
    // Audio PTS is sample-count based: pts = accumulated_samples
    // Time base: {1, 48000} → FFmpeg converts to stream timebase
    // Audio runs CONTINUOUSLY — silence is generated when no real audio is available
    qint64 m_audioSampleCount = 0;

    // === Audio FIFO (48kHz float32 stereo, resampled) ===
    // All incoming audio from all devices is resampled to 48kHz/FLTP/stereo
    // and written into this FIFO. The audio thread reads from it continuously.
    AVAudioFifo *m_audioFifo = nullptr;
    QMutex m_audioFifoMutex;

    // Metrics
    std::atomic<qint64> m_totalFrames{0};
    std::atomic<qint64> m_droppedFrames{0};
    std::atomic<qint64> m_totalBytesSent{0};

    // Threads
    QThread *m_videoThread = nullptr;
    QThread *m_audioThread = nullptr;
    
    // Video frame queue (render thread → video encode thread)
    QMutex m_videoQueueMutex;
    std::queue<QImage> m_videoFrameQueue;

    // Muxer lock (both threads write to the same output targets)
    QMutex m_muxMutex;

    static const int MAX_VIDEO_QUEUE_SIZE = 30;
    static const int MAX_RECONNECT_RETRIES = 5;
    static const int RECONNECT_DELAY_MS = 2000;

    // Audio constants
    static const int AUDIO_SAMPLE_RATE = 48000;
    static const int AUDIO_CHANNELS = 2;
};

#endif // STREAMINGMANAGER_H
