#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QString>
#include <QImage>
#include <QObject>
#include <QProcess>

class VideoRecorder : public QObject {
    Q_OBJECT

public:
    explicit VideoRecorder(QObject *parent = nullptr);
    ~VideoRecorder();

    bool startRecording(const QString &filePath, int width, int height, int fps);
    void stopRecording();
    void writeFrame(const QImage &frame);
    bool isRecording() const { return m_isRecording; }

private:
    QProcess *m_ffmpegProcess = nullptr;
    bool m_isRecording = false;
    int m_width;
    int m_height;
};

#endif // VIDEORECORDER_H
