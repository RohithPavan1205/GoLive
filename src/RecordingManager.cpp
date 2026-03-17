#include "RecordingManager.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QThread>
#include <cstring>

// =============================================================================
// OBS-Style Recording Architecture
// =============================================================================
// Mirrors the StreamingManager architecture:
// - Master clock for timing
// - Frame-index video PTS
// - Sample-count audio PTS (continuous, silence when no input)
// - Two independent threads: video encode + audio encode
// - Thread-safe muxing
// =============================================================================

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
    m_videoFrameIndex = 0;
    m_audioSampleCount = 0;

    // Start master clock
    m_masterClock.start();

    // Launch video encode thread
    m_videoThread = QThread::create([this]() {
        videoEncodeLoop();
    });
    m_videoThread->setObjectName("RecordVideoEncode");
    m_videoThread->start(QThread::HighPriority);

    // Launch audio encode thread
    m_audioThread = QThread::create([this]() {
        audioEncodeLoop();
    });
    m_audioThread->setObjectName("RecordAudioEncode");
    m_audioThread->start(QThread::HighestPriority);

    emit statusChanged(true);
    return true;
}

void RecordingManager::stopRecording() {
    if (!m_isRecording) return;
    
    m_shouldStop = true;

    if (m_videoThread) {
        m_videoThread->wait();
        delete m_videoThread;
        m_videoThread = nullptr;
    }

    if (m_audioThread) {
        m_audioThread->wait();
        delete m_audioThread;
        m_audioThread = nullptr;
    }

    cleanupFFmpeg();
    m_isRecording = false;
    emit statusChanged(false);
}

void RecordingManager::pushFrame(const QImage &image) {
    if (!m_isRecording) return;
    if (image.isNull()) return;
    QMutexLocker locker(&m_videoQueueMutex);
    if (m_videoFrameQueue.size() < 60) {
        m_videoFrameQueue.push(image);
    }
}

void RecordingManager::pushAudio(const QByteArray &data) {
    if (!m_isRecording) return;
    if (data.isEmpty()) return;

    // Resample incoming S16/44100 → FLTP/48000 and write to FIFO
    int in_samples = data.size() / 4;
    const uint8_t *in_data[1] = { (const uint8_t*)data.constData() };

    int max_out = av_rescale_rnd(
        swr_get_delay(m_swrCtx, 44100) + in_samples,
        AUDIO_SAMPLE_RATE, 44100, AV_ROUND_UP);

    uint8_t **tmp_out = nullptr;
    av_samples_alloc_array_and_samples(&tmp_out, nullptr,
        AUDIO_CHANNELS, max_out, AV_SAMPLE_FMT_FLTP, 0);

    int out_samples = swr_convert(m_swrCtx, tmp_out, max_out, in_data, in_samples);

    if (out_samples > 0) {
        QMutexLocker locker(&m_audioFifoMutex);
        av_audio_fifo_write(m_audioFifo, (void **)tmp_out, out_samples);
    }

    if (tmp_out) {
        av_freep(&tmp_out[0]);
        av_freep(&tmp_out);
    }
}

bool RecordingManager::initFFmpeg(const QString &filePath, int width, int height, int fps, const QString &quality) {
    m_width = width;
    m_height = height;
    m_fps = fps;

    // Output Context
    avformat_alloc_output_context2(&m_formatCtx, nullptr, nullptr, filePath.toUtf8().constData());
    if (!m_formatCtx) return false;

    // Video Encoder
    const AVCodec *videoCodec = avcodec_find_encoder_by_name("h264_videotoolbox");
    if (!videoCodec) videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    
    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    m_videoCodecCtx->width = width;
    m_videoCodecCtx->height = height;
    m_videoCodecCtx->time_base = {1, fps};
    m_videoCodecCtx->framerate = {fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12;
    if (strcmp(videoCodec->name, "libx264") == 0) m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    
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

    // Audio Encoder (AAC)
    const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecCtx->bit_rate = 128000;
    m_audioCodecCtx->sample_rate = AUDIO_SAMPLE_RATE;
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, AUDIO_CHANNELS);
    m_audioCodecCtx->time_base = {1, AUDIO_SAMPLE_RATE};
    m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) return false;

    // Streams
    m_videoStream = avformat_new_stream(m_formatCtx, videoCodec);
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecCtx);
    m_videoStream->time_base = m_videoCodecCtx->time_base;

    m_audioStream = avformat_new_stream(m_formatCtx, audioCodec);
    avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx);
    m_audioStream->time_base = m_audioCodecCtx->time_base;

    // Open File
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_formatCtx->pb, filePath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) return false;
    }

    if (avformat_write_header(m_formatCtx, nullptr) < 0) return false;

    // Converters
    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height,
        m_videoCodecCtx->pix_fmt, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    
    m_swrCtx = swr_alloc();
    AVChannelLayout in_ch_layout;
    av_channel_layout_default(&in_ch_layout, AUDIO_CHANNELS);
    swr_alloc_set_opts2(&m_swrCtx,
        &m_audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, AUDIO_SAMPLE_RATE,
        &in_ch_layout, AV_SAMPLE_FMT_S16, 44100, 0, nullptr);
    swr_init(m_swrCtx);

    // Audio FIFO
    m_audioFifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);
    if (!m_audioFifo) return false;

    return true;
}

// =============================================================================
// VIDEO ENCODE LOOP
// =============================================================================

void RecordingManager::videoEncodeLoop() {
    AVFrame *frame = av_frame_alloc();
    frame->format = m_videoCodecCtx->pix_fmt;
    frame->width = m_videoCodecCtx->width;
    frame->height = m_videoCodecCtx->height;
    av_frame_get_buffer(frame, 32);

    AVPacket *pkt = av_packet_alloc();

    while (!m_shouldStop || !m_videoFrameQueue.empty()) {
        QImage img;
        {
            QMutexLocker locker(&m_videoQueueMutex);
            if (!m_videoFrameQueue.empty()) {
                img = m_videoFrameQueue.front();
                m_videoFrameQueue.pop();
            }
        }

        if (img.isNull()) {
            QThread::usleep(1000);
            continue;
        }

        QImage frameImg = img.format() == QImage::Format_ARGB32
            ? img
            : img.convertToFormat(QImage::Format_ARGB32);
        if (frameImg.width() != m_width || frameImg.height() != m_height) {
            frameImg = frameImg.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }

        av_frame_make_writable(frame);

        const uint8_t *srcData[1] = { frameImg.constBits() };
        int srcLinesize[1] = { (int)frameImg.bytesPerLine() };
        sws_scale(m_swsCtx, srcData, srcLinesize, 0, m_height, frame->data, frame->linesize);
        
        // OBS-Style: frame-index PTS
        frame->pts = m_videoFrameIndex++;

        if (avcodec_send_frame(m_videoCodecCtx, frame) >= 0) {
            while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
                QMutexLocker locker(&m_muxMutex);
                av_packet_rescale_ts(pkt, m_videoCodecCtx->time_base, m_videoStream->time_base);
                pkt->stream_index = m_videoStream->index;
                av_interleaved_write_frame(m_formatCtx, pkt);
                av_packet_unref(pkt);
            }
        }
    }

    // Flush
    avcodec_send_frame(m_videoCodecCtx, nullptr);
    while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
        QMutexLocker locker(&m_muxMutex);
        av_packet_rescale_ts(pkt, m_videoCodecCtx->time_base, m_videoStream->time_base);
        pkt->stream_index = m_videoStream->index;
        av_interleaved_write_frame(m_formatCtx, pkt);
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

// =============================================================================
// AUDIO ENCODE LOOP (continuous, silence when no input)
// =============================================================================

void RecordingManager::audioEncodeLoop() {
    AVFrame *audioFrame = av_frame_alloc();
    audioFrame->format = m_audioCodecCtx->sample_fmt;
    audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
    av_channel_layout_copy(&audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    audioFrame->nb_samples = m_audioCodecCtx->frame_size;
    av_frame_get_buffer(audioFrame, 0);

    AVPacket *pkt = av_packet_alloc();
    int frame_size = m_audioCodecCtx->frame_size;

    while (!m_shouldStop) {
        double elapsed_ms = (double)m_masterClock.nsecsElapsed() / 1000000.0;
        double audio_time_ms = (double)m_audioSampleCount / (double)AUDIO_SAMPLE_RATE * 1000.0;

        int framesProduced = 0;
        int maxFramesPerTick = 4;

        while (audio_time_ms <= elapsed_ms + ((double)frame_size / AUDIO_SAMPLE_RATE * 1000.0 * 2) && framesProduced < maxFramesPerTick) {
            av_frame_make_writable(audioFrame);

            int fifo_available = 0;
            {
                QMutexLocker locker(&m_audioFifoMutex);
                fifo_available = av_audio_fifo_size(m_audioFifo);
            }

            if (fifo_available >= frame_size) {
                QMutexLocker locker(&m_audioFifoMutex);
                av_audio_fifo_read(m_audioFifo, (void **)audioFrame->data, frame_size);
            } else if (fifo_available > 0) {
                QMutexLocker locker(&m_audioFifoMutex);
                int read = av_audio_fifo_read(m_audioFifo, (void **)audioFrame->data, fifo_available);
                for (int ch = 0; ch < audioFrame->ch_layout.nb_channels; ch++) {
                    float *channel_data = (float *)audioFrame->data[ch];
                    memset(channel_data + read, 0, (frame_size - read) * sizeof(float));
                }
            } else {
                // Generate silence — audio must never stop
                for (int ch = 0; ch < audioFrame->ch_layout.nb_channels; ch++) {
                    memset(audioFrame->data[ch], 0, frame_size * sizeof(float));
                }
            }

            audioFrame->nb_samples = frame_size;
            audioFrame->pts = m_audioSampleCount;
            m_audioSampleCount += frame_size;
            audio_time_ms = (double)m_audioSampleCount / (double)AUDIO_SAMPLE_RATE * 1000.0;

            if (avcodec_send_frame(m_audioCodecCtx, audioFrame) >= 0) {
                while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
                    QMutexLocker locker(&m_muxMutex);
                    av_packet_rescale_ts(pkt, m_audioCodecCtx->time_base, m_audioStream->time_base);
                    pkt->stream_index = m_audioStream->index;
                    av_interleaved_write_frame(m_formatCtx, pkt);
                    av_packet_unref(pkt);
                }
            }
            framesProduced++;
        }

        if (framesProduced == 0) {
            QThread::usleep(2000);
        } else {
            QThread::usleep(1000);
        }
    }

    // Flush remaining audio in FIFO
    {
        QMutexLocker locker(&m_audioFifoMutex);
        int remaining = av_audio_fifo_size(m_audioFifo);
        if (remaining > 0) {
            av_frame_make_writable(audioFrame);
            int read = av_audio_fifo_read(m_audioFifo, (void **)audioFrame->data, qMin(remaining, frame_size));
            if (read < frame_size) {
                for (int ch = 0; ch < audioFrame->ch_layout.nb_channels; ch++) {
                    float *cd = (float *)audioFrame->data[ch];
                    memset(cd + read, 0, (frame_size - read) * sizeof(float));
                }
            }
            audioFrame->nb_samples = frame_size;
            audioFrame->pts = m_audioSampleCount;
            m_audioSampleCount += frame_size;
            avcodec_send_frame(m_audioCodecCtx, audioFrame);
        }
    }

    // Flush encoder
    avcodec_send_frame(m_audioCodecCtx, nullptr);
    while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
        QMutexLocker locker(&m_muxMutex);
        av_packet_rescale_ts(pkt, m_audioCodecCtx->time_base, m_audioStream->time_base);
        pkt->stream_index = m_audioStream->index;
        av_interleaved_write_frame(m_formatCtx, pkt);
        av_packet_unref(pkt);
    }

    // Write trailer (audio thread finishes last)
    {
        QMutexLocker locker(&m_muxMutex);
        av_write_trailer(m_formatCtx);
    }

    av_frame_free(&audioFrame);
    av_packet_free(&pkt);
}

void RecordingManager::cleanupFFmpeg() {
    if (m_audioFifo) { av_audio_fifo_free(m_audioFifo); m_audioFifo = nullptr; }
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
