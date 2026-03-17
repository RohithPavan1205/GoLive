#include "StreamingManager.h"
#include <QDebug>
#include <QDateTime>

StreamingManager::StreamingManager(QObject *parent) : QObject(parent) {
    avformat_network_init();
}

StreamingManager::~StreamingManager() {
    stopStreaming();
}

bool StreamingManager::startStreaming(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    if (m_isStreaming) return false;

    if (!initFFmpeg(urls, width, height, fps, bitrate)) {
        cleanupFFmpeg();
        return false;
    }

    m_isStreaming = true;
    m_shouldStop = false;
    m_frameCount = 0;
    
    m_workerThread = QThread::create([this]() {
        streamLoop();
    });
    m_workerThread->start();
    
    emit statusChanged(true);
    return true;
}

void StreamingManager::stopStreaming() {
    if (!m_isStreaming) return;
    
    m_shouldStop = true;
    if (m_workerThread) {
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    cleanupFFmpeg();
    m_isStreaming = false;
    emit statusChanged(false);
}

void StreamingManager::pushFrame(const QImage &image) {
    if (!m_isStreaming) return;
    
    QMutexLocker locker(&m_queueMutex);
    if (m_frameQueue.size() < 30) {
        m_frameQueue.push(image.copy());
    }
}

void StreamingManager::pushAudio(const QByteArray &data) {
    if (!m_isStreaming) return;
    QMutexLocker locker(&m_queueMutex);
    if (m_audioQueue.size() < 100) {
        m_audioQueue.push(data);
    }
}

bool StreamingManager::initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    m_width = width;
    m_height = height;
    m_fps = fps;

    // 1. Video Encoder (Global for all targets)
    const AVCodec *videoCodec = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!videoCodec) videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    
    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    m_videoCodecCtx->width = width;
    m_videoCodecCtx->height = height;
    m_videoCodecCtx->time_base = {1, fps};
    m_videoCodecCtx->framerate = {fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12;
    m_videoCodecCtx->bit_rate = bitrate;
    m_videoCodecCtx->gop_size = fps * 2;
    m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) return false;

    // 2. Audio Encoder (Global for all targets)
    const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecCtx->bit_rate = 128000;
    m_audioCodecCtx->sample_rate = 48000;
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, 2);
    m_audioCodecCtx->time_base = {1, 48000};
    m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) return false;

    // 3. Create Outputs for each URL
    for (const QString &url : urls) {
        OutputTarget target;
        avformat_alloc_output_context2(&target.formatCtx, nullptr, "flv", url.toUtf8().constData());
        if (!target.formatCtx) continue;

        target.videoStream = avformat_new_stream(target.formatCtx, videoCodec);
        avcodec_parameters_from_context(target.videoStream->codecpar, m_videoCodecCtx);
        target.videoStream->time_base = m_videoCodecCtx->time_base;

        target.audioStream = avformat_new_stream(target.formatCtx, audioCodec);
        avcodec_parameters_from_context(target.audioStream->codecpar, m_audioCodecCtx);
        target.audioStream->time_base = m_audioCodecCtx->time_base;

        if (!(target.formatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&target.formatCtx->pb, url.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
                avformat_free_context(target.formatCtx);
                continue;
            }
        }

        if (avformat_write_header(target.formatCtx, nullptr) < 0) {
            avformat_free_context(target.formatCtx);
            continue;
        }
        m_targets.append(target);
    }

    if (m_targets.isEmpty()) return false;

    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_NV12, SWS_BICUBIC, nullptr, nullptr, nullptr);
    m_swrCtx = swr_alloc();
    AVChannelLayout in_ch_layout;
    av_channel_layout_default(&in_ch_layout, 2);
    swr_alloc_set_opts2(&m_swrCtx, &m_audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, 48000,
                        &in_ch_layout, AV_SAMPLE_FMT_S16, 44100, 0, nullptr);
    swr_init(m_swrCtx);

    return true;
}

void StreamingManager::streamLoop() {
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

    while (!m_shouldStop) {
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
                    for (auto &target : m_targets) {
                        AVPacket *outPkt = av_packet_clone(pkt);
                        av_packet_rescale_ts(outPkt, m_videoCodecCtx->time_base, target.videoStream->time_base);
                        outPkt->stream_index = target.videoStream->index;
                        av_interleaved_write_frame(target.formatCtx, outPkt);
                        av_packet_free(&outPkt);
                    }
                    av_packet_unref(pkt);
                }
            }
        }

        if (!audioData.isEmpty()) {
            const uint8_t *in_data[1] = { (const uint8_t*)audioData.constData() };
            int in_samples = audioData.size() / 4; 
            int out_samples = swr_convert(m_swrCtx, audioFrame->data, audioFrame->nb_samples, in_data, in_samples);
            
            if (out_samples > 0) {
                audioFrame->pts = m_audioFrameCount;
                m_audioFrameCount += out_samples;
                if (avcodec_send_frame(m_audioCodecCtx, audioFrame) >= 0) {
                    while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
                        for (auto &target : m_targets) {
                            AVPacket *outPkt = av_packet_clone(pkt);
                            av_packet_rescale_ts(outPkt, m_audioCodecCtx->time_base, target.audioStream->time_base);
                            outPkt->stream_index = target.audioStream->index;
                            av_interleaved_write_frame(target.formatCtx, outPkt);
                            av_packet_free(&outPkt);
                        }
                        av_packet_unref(pkt);
                    }
                }
            }
        }

        if (img.isNull() && audioData.isEmpty()) QThread::msleep(5);
    }

    for (auto &target : m_targets) av_write_trailer(target.formatCtx);
    av_frame_free(&videoFrame);
    av_frame_free(&audioFrame);
    av_packet_free(&pkt);
}

void StreamingManager::cleanupFFmpeg() {
    for (auto &target : m_targets) {
        if (!(target.formatCtx->oformat->flags & AVFMT_NOFILE)) avio_closep(&target.formatCtx->pb);
        avformat_free_context(target.formatCtx);
    }
    m_targets.clear();

    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); m_videoCodecCtx = nullptr; }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); m_audioCodecCtx = nullptr; }
}
