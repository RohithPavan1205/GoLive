#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

#include <QString>
#include <QImage>
#include <QObject>

class VideoRecorder : public QObject {
    Q_OBJECT

public:
    explicit VideoRecorder(QObject *parent = nullptr);
    ~VideoRecorder();

    bool startRecording(const QString &filePath, int width, int height, int fps);
    void stopRecording();
    void writeFrame(const QImage &frame, qint64 timestampMs);
    bool isRecording() const { return m_isRecording; }

private:
    void *m_pimpl; // Using opaque pointer for AVFoundation objects
    bool m_isRecording = false;
    int m_width;
    int m_height;
};

#endif // VIDEORECORDER_H
