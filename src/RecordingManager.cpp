#include "RecordingManager.h"
#include <QDir>
#include <QDateTime>
#include <QVideoFrameFormat>
#include <QMediaFormat>

RecordingManager::RecordingManager(QObject *parent) : QObject(parent) {
    m_captureSession = new QMediaCaptureSession(this);
    m_recorder = new QMediaRecorder(this);
    m_captureSession->setRecorder(m_recorder);
    m_videoInput = nullptr;
}

RecordingManager::~RecordingManager() {
    if (m_isRecording) stopRecording();
}

bool RecordingManager::startRecording(const RecordingSettings &settings) {
    if (m_isRecording) return false;

    // Create unique filename
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("%1/%2_%3.%4")
        .arg(settings.outputDir)
        .arg(settings.fileNamePrefix)
        .arg(timestamp)
        .arg(settings.format);
        
    m_recorder->setOutputLocation(QUrl::fromLocalFile(fileName));

    // Configure Format & Quality
    QMediaFormat format;
    if (settings.format == "mp4") format.setFileFormat(QMediaFormat::MPEG4);
    else if (settings.format == "mkv") format.setFileFormat(QMediaFormat::Matroska);
    else if (settings.format == "mov") format.setFileFormat(QMediaFormat::QuickTime);
    
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(format);

    // Quality mapping
    switch (settings.quality) {
        case 0: m_recorder->setQuality(QMediaRecorder::HighQuality); break;
        case 1: m_recorder->setQuality(QMediaRecorder::NormalQuality); break;
        case 2: m_recorder->setQuality(QMediaRecorder::LowQuality); break;
    }

    // Prepare Video Input
    QSize frameSize(1920, 1080); // HD matches Live Monitor
    QVideoFrameFormat videoFormat(frameSize, QVideoFrameFormat::Format_ARGB8888);
    m_videoInput = new QVideoFrameInput(videoFormat, this);
    m_captureSession->setVideoInput(m_videoInput);

    m_recorder->record();
    m_isRecording = true;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void RecordingManager::stopRecording() {
    if (!m_isRecording) return;
    
    m_recorder->stop();
    m_captureSession->setVideoInput(nullptr);
    if (m_videoInput) {
        delete m_videoInput;
        m_videoInput = nullptr;
    }
    m_isRecording = false;
    m_startTime = 0; // Reset start time
}

bool RecordingManager::isRecording() const {
    return m_isRecording;
}

void RecordingManager::handleFrame(const QImage &image) {
    if (!m_isRecording || !m_videoInput) return;
    
    QImage frameImg = image.convertToFormat(QImage::Format_ARGB32);
    QVideoFrame frame(frameImg);
    
    // Set timestamp in microseconds
    qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_startTime;
    frame.setStartTime(elapsedMs * 1000); 
    
    if (!m_videoInput->sendVideoFrame(frame)) {
        // Queue full or error
    }
}
