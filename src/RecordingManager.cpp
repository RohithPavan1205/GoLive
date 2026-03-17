#include "RecordingManager.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>

RecordingManager::RecordingManager(QObject *parent) : QObject(parent) {
    avformat_network_init();
}

RecordingManager::~RecordingManager() {
    stopRecording();
}

bool RecordingManager::startRecording(const QString &filePath, int width, int height, int fps, const QString &quality) {
    if (m_isRecording) return false;

    if (!initFFmpeg(filePath, width, height, fps, quality)) {
        cleanupFFmpeg();
        return false;
    }

    m_isRecording = true;
    m_shouldStop = false;
    m_frameCount = 0;
    
    m_workerThread = QThread::create([this]() {
        recordLoop();
    });
    m_workerThread->start();
    
    emit statusChanged(true);
    return true;
}

void RecordingManager::stopRecording() {
    if (!m_isRecording) return;
    
    m_shouldStop = true;
    if (m_workerThread) {
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    cleanupFFmpeg();
    m_isRecording = false;
    emit statusChanged(false);
}

void RecordingManager::pushFrame(const QImage &image) {
    if (!m_isRecording) return;
    
    QMutexLocker locker(&m_queueMutex);
    if (m_frameQueue.size() < 60) { // Larger buffer for recording to handle disk I/O spikes
        m_frameQueue.push(image.copy());
    }
}

void RecordingManager::pushAudio(const QByteArray &data) {
    if (!m_isRecording) return;
    QMutexLocker locker(&m_queueMutex);
    if (m_audioQueue.size() < 100) {
        m_audioQueue.push(data);
    }
}

bool RecordingManager::initFFmpeg(const QString &filePath, int width, int height, int fps, const QString &quality) {
    m_width = width;
    m_height = height;
    m_fps = fps;

    // 1. Output Context
    avformat_alloc_output_context2(&m_formatCtx, nullptr, nullptr, filePath.toUtf8().constData());
    if (!m_formatCtx) return false;

    // 2. Video Encoder (HEVC/H.264)
    // Use Hardware Acceleration on Mac
    const AVCodec *videoCodec = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!videoCodec) videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    
    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    m_videoCodecCtx->width = width;
    m_videoCodecCtx->height = height;
    m_videoCodecCtx->time_base = {1, fps};
    m_videoCodecCtx->framerate = {fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12; // Standard for H.264 hardware encoders
    
    // Quality to Bitrate Mapping (OBS-like)
    int64_t bitrate = 6000000;
    if (quality.contains("Low")) bitrate = 2500000;
    else if (quality.contains("Medium")) bitrate = 6000000;
    else if (quality.contains("High")) bitrate = 12000000;
    else if (quality.contains("Indistinguishable")) bitrate = 25000000;
    
    m_videoCodecCtx->bit_rate = bitrate;
    m_videoCodecCtx->gop_size = fps * 2;
    m_videoCodecCtx->max_b_frames = 2;
    m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) return false;

    // 2b. Audio Encoder (AAC)
    const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecCtx->bit_rate = 128000;
    m_audioCodecCtx->sample_rate = 48000;
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, 2);
    m_audioCodecCtx->time_base = {1, 48000};
    m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) return false;

    // 3. Create Streams
    m_videoStream = avformat_new_stream(m_formatCtx, videoCodec);
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecCtx);
    m_videoStream->time_base = m_videoCodecCtx->time_base;

    m_audioStream = avformat_new_stream(m_formatCtx, audioCodec);
    avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx);
    // Don't manually set stream timebase, let the muxer decide in write_header

    // 4. Open File
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_formatCtx->pb, filePath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) return false;
    }

    if (avformat_write_header(m_formatCtx, nullptr) < 0) return false;

    // 5. Converters
    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_NV12, SWS_BICUBIC, nullptr, nullptr, nullptr);
    
    m_swrCtx = swr_alloc();
    AVChannelLayout in_ch_layout;
    av_channel_layout_default(&in_ch_layout, 2);
    swr_alloc_set_opts2(&m_swrCtx, &m_audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, 48000,
                        &in_ch_layout, AV_SAMPLE_FMT_S16, 44100, 0, nullptr);
    swr_init(m_swrCtx);

    return true;
}

void RecordingManager::recordLoop() {
    AVFrame *videoFrame = av_frame_alloc();
    videoFrame->format = m_videoCodecCtx->pix_fmt;
    videoFrame->width = m_videoCodecCtx->width;
    videoFrame->height = m_videoCodecCtx->height;
    av_frame_get_buffer(videoFrame, 32);

    AVFrame *audioFrame = av_frame_alloc();
    audioFrame->format = m_audioCodecCtx->sample_fmt;
    audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
    av_channel_layout_copy(&audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    audioFrame->nb_samples = m_audioCodecCtx->frame_size;
    av_frame_get_buffer(audioFrame, 0);

    AVPacket *pkt = av_packet_alloc();

    while (!m_shouldStop || !m_frameQueue.empty() || !m_audioQueue.empty()) {
        QImage img;
        QByteArray audioData;
        {
            QMutexLocker locker(&m_queueMutex);
            if (!m_frameQueue.empty()) { img = m_frameQueue.front(); m_frameQueue.pop(); }
            if (!m_audioQueue.empty()) { audioData = m_audioQueue.front(); m_audioQueue.pop(); }
        }

        if (!img.isNull()) {
            uint8_t *srcData[1] = { (uint8_t*)img.bits() };
            int srcLinesize[1] = { (int)img.bytesPerLine() };
            sws_scale(m_swsCtx, srcData, srcLinesize, 0, m_height, videoFrame->data, videoFrame->linesize);
            
            videoFrame->pts = m_frameCount++;

            if (avcodec_send_frame(m_videoCodecCtx, videoFrame) >= 0) {
                while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
                    av_packet_rescale_ts(pkt, m_videoCodecCtx->time_base, m_videoStream->time_base);
                    pkt->stream_index = m_videoStream->index;
                    av_interleaved_write_frame(m_formatCtx, pkt);
                    av_packet_unref(pkt);
                }
            }
        }

        if (!audioData.isEmpty()) {
            const uint8_t *in_data[1] = { (const uint8_t*)audioData.constData() };
            int in_samples = audioData.size() / 4; // S16 Stereo
            int out_samples = swr_convert(m_swrCtx, audioFrame->data, audioFrame->nb_samples, in_data, in_samples);
            
            if (out_samples > 0) {
                audioFrame->pts = m_audioFrameCount;
                m_audioFrameCount += out_samples;
                if (avcodec_send_frame(m_audioCodecCtx, audioFrame) >= 0) {
                    while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
                        av_packet_rescale_ts(pkt, m_audioCodecCtx->time_base, m_audioStream->time_base);
                        pkt->stream_index = m_audioStream->index;
                        av_interleaved_write_frame(m_formatCtx, pkt);
                        av_packet_unref(pkt);
                    }
                }
            }
        }

        if (img.isNull() && audioData.isEmpty()) {
            QThread::msleep(5);
        }
    }

    av_write_trailer(m_formatCtx);
    av_frame_free(&videoFrame);
    av_frame_free(&audioFrame);
    av_packet_free(&pkt);
}

void RecordingManager::cleanupFFmpeg() {
    if (m_formatCtx) {
        if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) avio_closep(&m_formatCtx->pb);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
    }
    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); m_videoCodecCtx = nullptr; }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); m_audioCodecCtx = nullptr; }
}
