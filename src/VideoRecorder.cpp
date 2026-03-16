#include "VideoRecorder.h"
#include <QDebug>

VideoRecorder::VideoRecorder(QObject *parent) : QObject(parent) {}

VideoRecorder::~VideoRecorder() {
    stopRecording();
}

bool VideoRecorder::startRecording(const QString &filePath, int width, int height, int fps) {
    if (m_isRecording) return false;

    m_width = width;
    m_height = height;

    m_ffmpegProcess = new QProcess(this);
    
    // FFmpeg command for recording from stdin
    // -f rawvideo: input is raw video frames
    // -pixel_format rgb24: each frame is raw RGB data
    // -video_size: dimensions
    // -i -: read from stdin
    // -c:v libx264: encode to H.264
    // -preset ultrafast: minimize CPU impact during recording
    // -pix_fmt yuv420p: standard pixel format for compatibility
    QStringList args;
    args << "-y" // Overwrite output
         << "-f" << "rawvideo"
         << "-pixel_format" << "rgb24"
         << "-video_size" << QString("%1x%2").arg(width).arg(height)
         << "-framerate" << QString::number(fps)
         << "-i" << "-" // Input from pipe
         << "-c:v" << "libx264"
         << "-preset" << "ultrafast"
         << "-pix_fmt" << "yuv420p"
         << filePath;

    m_ffmpegProcess->start("ffmpeg", args);
    
    if (!m_ffmpegProcess->waitForStarted()) {
        qDebug() << "Failed to start FFmpeg:" << m_ffmpegProcess->errorString();
        delete m_ffmpegProcess;
        m_ffmpegProcess = nullptr;
        return false;
    }

    m_isRecording = true;
    qDebug() << "Recording started:" << filePath;
    return true;
}

void VideoRecorder::stopRecording() {
    if (!m_isRecording || !m_ffmpegProcess) return;

    m_ffmpegProcess->closeWriteChannel(); // Signal EOF to ffmpeg
    if (!m_ffmpegProcess->waitForFinished(5000)) {
        m_ffmpegProcess->kill();
    }

    delete m_ffmpegProcess;
    m_ffmpegProcess = nullptr;
    m_isRecording = false;
    qDebug() << "Recording stopped.";
}

void VideoRecorder::writeFrame(const QImage &frame) {
    if (!m_isRecording || !m_ffmpegProcess) return;

    // Convert QImage to 24-bit RGB for FFmpeg
    QImage rgb = frame.convertToFormat(QImage::Format_RGB888).scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    
    m_ffmpegProcess->write((const char*)rgb.bits(), rgb.sizeInBytes());
}
