#ifndef RECORDINGMANAGER_H
#define RECORDINGMANAGER_H

#include <QObject>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>
#include <QVideoFrame>
#include <QImage>
#include "RecordingSettingsDialog.h"

class RecordingManager : public QObject {
    Q_OBJECT

public:
    explicit RecordingManager(QObject *parent = nullptr);
    ~RecordingManager();

    bool startRecording(const RecordingSettings &settings);
    void stopRecording();
    bool isRecording() const;

public slots:
    void handleFrame(const QImage &image);

private:
    QMediaCaptureSession *m_captureSession;
    QMediaRecorder *m_recorder;
    QVideoFrameInput *m_videoInput;
    bool m_isRecording = false;
};

#endif // RECORDINGMANAGER_H
