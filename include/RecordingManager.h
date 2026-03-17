#ifndef RECORDINGMANAGER_H
#define RECORDINGMANAGER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QByteArray>
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
#include <libavutil/audio_fifo.h>
}

class RecordingManager : public QObject {
    Q_OBJECT
public:
    explicit RecordingManager(QObject *parent = nullptr);
    ~RecordingManager();

    bool startRecording(const QString &filePath, int width, int height, int fps, const QString &quality);
    void stopRecording();
    bool isRecording() const { return m_isRecording; }

public slots:
    void pushFrame(const QImage &image);
    void pushAudio(const QByteArray &data);

signals:
    void errorOccurred(const QString &msg);
    void statusChanged(bool active);

private:
    // OBS-Style: separate video encode and audio encode loops
    void videoEncodeLoop();
    void audioEncodeLoop();
    bool initFFmpeg(const QString &filePath, int width, int height, int fps, const QString &quality);
    void cleanupFFmpeg();

    std::atomic<bool> m_isRecording{false};
    std::atomic<bool> m_shouldStop{false};
    
    // FFmpeg Contexts
    AVFormatContext *m_formatCtx = nullptr;
    AVStream *m_videoStream = nullptr;
    AVStream *m_audioStream = nullptr;
    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    
    int m_width = 1920, m_height = 1080, m_fps = 30;

    // Master Clock (OBS-Style)
    QElapsedTimer m_masterClock;

    // Video: frame-index PTS
    qint64 m_videoFrameIndex = 0;

    // Audio: sample-count PTS (continuous, never stops)
    qint64 m_audioSampleCount = 0;

    // Audio FIFO
    AVAudioFifo *m_audioFifo = nullptr;
    QMutex m_audioFifoMutex;

    // Threads
    QThread *m_videoThread = nullptr;
    QThread *m_audioThread = nullptr;
    
    // Video frame queue
    QMutex m_videoQueueMutex;
    std::queue<QImage> m_videoFrameQueue;

    // Muxer lock
    QMutex m_muxMutex;

    static const int AUDIO_SAMPLE_RATE = 48000;
    static const int AUDIO_CHANNELS = 2;
};

#endif // RECORDINGMANAGER_H
