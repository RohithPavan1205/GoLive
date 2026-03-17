#ifndef STREAMINGMANAGER_H
#define STREAMINGMANAGER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QByteArray>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class StreamingManager : public QObject {
    Q_OBJECT
public:
    explicit StreamingManager(QObject *parent = nullptr);
    ~StreamingManager();

    bool startStreaming(const QList<QString> &urls, int width, int height, int fps, int bitrate);
    void stopStreaming();
    bool isStreaming() const { return m_isStreaming; }

public slots:
    void pushFrame(const QImage &image);
    void pushAudio(const QByteArray &data);

signals:
    void errorOccurred(const QString &msg);
    void statusChanged(bool active);

private:
    void streamLoop();
    bool initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate);
    void cleanupFFmpeg();

    bool m_isStreaming = false;
    bool m_shouldStop = false;
    
    // FFmpeg Contexts
    struct OutputTarget {
        AVFormatContext *formatCtx = nullptr;
        AVStream *videoStream = nullptr;
        AVStream *audioStream = nullptr;
    };
    QList<OutputTarget> m_targets;

    AVCodecContext *m_videoCodecCtx = nullptr;
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    
    int m_width, m_height, m_fps;
    qint64 m_frameCount = 0;
    qint64 m_audioFrameCount = 0;
    
    QThread *m_workerThread = nullptr;
    QMutex m_queueMutex;
    std::queue<QImage> m_frameQueue;
    std::queue<QByteArray> m_audioQueue;
};

#endif // STREAMINGMANAGER_H
