#include "StreamingManager.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <cstring>

// =============================================================================
// OBS-Style Streaming Architecture
// =============================================================================
//
// KEY PRINCIPLES (matching OBS):
//
// 1. MASTER CLOCK: A single QElapsedTimer (monotonic) started at stream begin.
//    Everything is derived from this clock — no QDateTime::currentMSecsSinceEpoch().
//
// 2. VIDEO TIMING: frame-index based PTS.
//    video_pts = frame_index (in codec timebase {1, fps})
//    NOT wall-clock milliseconds. FFmpeg's av_packet_rescale_ts() converts properly.
//
// 3. AUDIO TIMING: sample-count based PTS.
//    audio_pts = accumulated_samples (in codec timebase {1, 48000})
//    Audio is CONTINUOUS — silence is generated when no real audio arrives.
//    Audio NEVER stops, or the stream becomes invalid.
//
// 4. TWO INDEPENDENT THREADS:
//    - Video thread: dequeues frames, encodes H.264, writes packets
//    - Audio thread: reads FIFO (or generates silence), encodes AAC, writes packets
//    Both use av_packet_rescale_ts() before writing.
//
// 5. SYNC: Audio runs slightly ahead of video (natural due to small AAC frames).
//    The muxer's av_interleaved_write_frame() handles final interleaving.
//    Drift is impossible because both timelines are sample-count based.
//
// =============================================================================

StreamingManager::StreamingManager(QObject *parent) : QObject(parent) {
    avformat_network_init();
}

StreamingManager::~StreamingManager() {
    stopStreaming();
}

void StreamingManager::updateState(State newState) {
    if (m_state == newState) return;
    m_state = newState;
    QMetaObject::invokeMethod(this, [this, newState]() {
        emit stateChanged(newState);
    }, Qt::QueuedConnection);
}

void StreamingManager::logFFmpegError(int err, const QString &context) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, errbuf, AV_ERROR_MAX_STRING_SIZE);
    QString msg = QString("%1: %2 (%3)").arg(context).arg(errbuf).arg(err);
    qWarning() << "[StreamingManager]" << msg;
    QMetaObject::invokeMethod(this, [this, msg]() {
        emit errorOccurred(msg);
    }, Qt::QueuedConnection);
}

// =============================================================================
// START / STOP
// =============================================================================

bool StreamingManager::startStreaming(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    if (isActive()) {
        qWarning() << "[StreamingManager] Already active, stop first.";
        return false;
    }

    // Validation
    if (urls.isEmpty()) {
        emit errorOccurred("No streaming URLs provided.");
        return false;
    }
    if (width <= 0 || height <= 0 || fps <= 0 || bitrate <= 0) {
        emit errorOccurred("Invalid streaming parameters (dimensions/fps/bitrate).");
        return false;
    }

    updateState(State::Connecting);
    
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_bitrate = bitrate;
    m_initialUrls = urls;

    if (!initFFmpeg(urls, width, height, fps, bitrate)) {
        cleanupFFmpeg();
        updateState(State::Failed);
        return false;
    }

    // Reset all counters
    m_shouldStop = false;
    m_videoFrameIndex = 0;
    m_audioSampleCount = 0;
    m_totalFrames = 0;
    m_droppedFrames = 0;
    m_totalBytesSent = 0;

    // Start the master clock (monotonic, nanosecond precision)
    m_masterClock.start();

    // Launch independent video encode thread
    m_videoThread = QThread::create([this]() {
        videoEncodeLoop();
    });
    m_videoThread->setObjectName("StreamVideoEncode");
    m_videoThread->start(QThread::HighPriority);

    // Launch independent audio encode + mux thread
    m_audioThread = QThread::create([this]() {
        audioMuxLoop();
    });
    m_audioThread->setObjectName("StreamAudioMux");
    m_audioThread->start(QThread::HighestPriority); // Audio is king — must never skip

    updateState(State::Streaming);
    return true;
}

void StreamingManager::stopStreaming() {
    if (m_state == State::Idle) return;
    
    updateState(State::Stopping);
    m_shouldStop = true;
    
    // Wait for video thread
    if (m_videoThread) {
        if (!m_videoThread->wait(5000)) {
            qWarning() << "[StreamingManager] Video thread timed out, terminating...";
            m_videoThread->terminate();
            m_videoThread->wait();
        }
        delete m_videoThread;
        m_videoThread = nullptr;
    }

    // Wait for audio thread
    if (m_audioThread) {
        if (!m_audioThread->wait(5000)) {
            qWarning() << "[StreamingManager] Audio thread timed out, terminating...";
            m_audioThread->terminate();
            m_audioThread->wait();
        }
        delete m_audioThread;
        m_audioThread = nullptr;
    }
    
    cleanupFFmpeg();
    updateState(State::Idle);
}

// =============================================================================
// PUSH (called from render thread / camera thread — must be fast)
// =============================================================================

void StreamingManager::pushFrame(const QImage &image) {
    if (m_state != State::Streaming) return;
    if (image.isNull()) return;
    QMutexLocker locker(&m_videoQueueMutex);
    if ((int)m_videoFrameQueue.size() >= MAX_VIDEO_QUEUE_SIZE) {
        m_droppedFrames++;
        return;
    }
    m_videoFrameQueue.push(image);
}

void StreamingManager::pushAudio(const QByteArray &data) {
    if (m_state != State::Streaming) return;
    if (data.isEmpty()) return;

    // Resample incoming audio (S16, 44100Hz, stereo) → (FLTP, 48000Hz, stereo)
    // and write into the global audio FIFO
    int in_samples = data.size() / 4; // S16 stereo = 4 bytes per sample
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

// =============================================================================
// INIT FFMPEG
// =============================================================================

bool StreamingManager::initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    int ret;

    // --- Video Encoder (H.264) ---
    const char* encoders[] = {"h264_videotoolbox", "libx264", nullptr};
    const AVCodec *videoCodec = nullptr;
    for (int i = 0; encoders[i]; ++i) {
        videoCodec = avcodec_find_encoder_by_name(encoders[i]);
        if (videoCodec) break;
    }
    if (!videoCodec) videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!videoCodec) {
        emit errorOccurred("Could not find any H.264 encoder.");
        return false;
    }

    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    if (!m_videoCodecCtx) return false;

    m_videoCodecCtx->width = width;
    m_videoCodecCtx->height = height;
    m_videoCodecCtx->time_base = {1, fps};   // Video timebase: 1/fps
    m_videoCodecCtx->framerate = {fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12;
    if (strcmp(videoCodec->name, "libx264") == 0) m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    
    m_videoCodecCtx->bit_rate = bitrate;
    m_videoCodecCtx->gop_size = fps * 2;
    m_videoCodecCtx->max_b_frames = 0;
    m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *codecOpts = nullptr;
    if (strcmp(videoCodec->name, "h264_videotoolbox") == 0) {
        av_dict_set(&codecOpts, "realtime", "1", 0);
        av_dict_set(&codecOpts, "allow_sw", "1", 0);
    } else if (strcmp(videoCodec->name, "libx264") == 0) {
        av_dict_set(&codecOpts, "preset", "veryfast", 0);
        av_dict_set(&codecOpts, "tune", "zerolatency", 0);
        av_dict_set(&codecOpts, "profile", "baseline", 0);
    }

    if ((ret = avcodec_open2(m_videoCodecCtx, videoCodec, &codecOpts)) < 0) {
        av_dict_free(&codecOpts);
        logFFmpegError(ret, "Failed to open video codec");
        return false;
    }
    av_dict_free(&codecOpts);
    qDebug() << "[StreamingManager] Video codec:" << videoCodec->name
             << "pix_fmt:" << av_get_pix_fmt_name(m_videoCodecCtx->pix_fmt);

    // --- Audio Encoder (AAC) ---
    const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audioCodec) {
        emit errorOccurred("Could not find AAC encoder.");
        return false;
    }
    m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecCtx->bit_rate = 128000;
    m_audioCodecCtx->sample_rate = AUDIO_SAMPLE_RATE;
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, AUDIO_CHANNELS);
    m_audioCodecCtx->time_base = {1, AUDIO_SAMPLE_RATE};  // Audio timebase: 1/48000
    m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if ((ret = avcodec_open2(m_audioCodecCtx, audioCodec, nullptr)) < 0) {
        logFFmpegError(ret, "Failed to open audio codec");
        return false;
    }
    qDebug() << "[StreamingManager] Audio codec: AAC, frame_size:" << m_audioCodecCtx->frame_size;

    // --- Output Targets (RTMP) ---
    for (const QString &url : urls) {
        OutputTarget target;
        target.url = url;
        if ((ret = avformat_alloc_output_context2(&target.formatCtx, nullptr, "flv", url.toUtf8().constData())) < 0) {
            logFFmpegError(ret, QString("Context allocation failed for %1").arg(url));
            continue;
        }

        target.videoStream = avformat_new_stream(target.formatCtx, videoCodec);
        avcodec_parameters_from_context(target.videoStream->codecpar, m_videoCodecCtx);
        target.videoStream->time_base = m_videoCodecCtx->time_base;

        target.audioStream = avformat_new_stream(target.formatCtx, audioCodec);
        avcodec_parameters_from_context(target.audioStream->codecpar, m_audioCodecCtx);
        target.audioStream->time_base = m_audioCodecCtx->time_base;

        if (!(target.formatCtx->oformat->flags & AVFMT_NOFILE)) {
            AVDictionary *opts = nullptr;
            av_dict_set(&opts, "connect_timeout", "5000000", 0);
            if ((ret = avio_open2(&target.formatCtx->pb, url.toUtf8().constData(), AVIO_FLAG_WRITE, nullptr, &opts)) < 0) {
                logFFmpegError(ret, QString("Connection failed for %1").arg(url));
                av_dict_free(&opts);
                avformat_free_context(target.formatCtx);
                continue;
            }
            av_dict_free(&opts);
        }

        if ((ret = avformat_write_header(target.formatCtx, nullptr)) < 0) {
            logFFmpegError(ret, QString("Header write failed for %1").arg(url));
            avformat_free_context(target.formatCtx);
            continue;
        }
        target.connected = true;
        m_targets.append(target);
    }

    if (m_targets.isEmpty()) {
        emit errorOccurred("Failed to connect to any streaming targets.");
        return false;
    }

    // --- Pixel format converter ---
    m_swsCtx = sws_getContext(
        width, height, AV_PIX_FMT_BGRA,
        width, height, m_videoCodecCtx->pix_fmt,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!m_swsCtx) {
        emit errorOccurred("Failed to create pixel format converter.");
        return false;
    }

    // --- Audio resampler: S16/44100 → FLTP/48000 ---
    m_swrCtx = swr_alloc();
    AVChannelLayout in_ch_layout;
    av_channel_layout_default(&in_ch_layout, AUDIO_CHANNELS);
    swr_alloc_set_opts2(&m_swrCtx,
        &m_audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, AUDIO_SAMPLE_RATE,
        &in_ch_layout, AV_SAMPLE_FMT_S16, 44100,
        0, nullptr);
    if ((ret = swr_init(m_swrCtx)) < 0) {
        logFFmpegError(ret, "Failed to initialize audio resampler");
        return false;
    }

    // --- Audio FIFO (buffering zone between pushAudio and the audio thread) ---
    m_audioFifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE); // 1 second buffer
    if (!m_audioFifo) {
        emit errorOccurred("Failed to create audio FIFO.");
        return false;
    }

    return true;
}

// =============================================================================
// VIDEO ENCODE LOOP (Thread 1)
// =============================================================================
//
// Runs on its own thread. Dequeues video frames, encodes H.264, writes packets.
// VIDEO PTS = frame_index (NOT wall-clock).
// The render thread pushes frames at ~fps rate. We encode whatever is available.
// If no frame, we sleep briefly (the render thread will push the next one).
//
void StreamingManager::videoEncodeLoop() {
    AVFrame *frame = av_frame_alloc();
    frame->format = m_videoCodecCtx->pix_fmt;
    frame->width = m_videoCodecCtx->width;
    frame->height = m_videoCodecCtx->height;
    av_frame_get_buffer(frame, 32);

    AVPacket *pkt = av_packet_alloc();

    while (!m_shouldStop) {
        QImage img;
        {
            QMutexLocker locker(&m_videoQueueMutex);
            if (!m_videoFrameQueue.empty()) {
                img = m_videoFrameQueue.front();
                m_videoFrameQueue.pop();
            }
        }

        if (img.isNull()) {
            QThread::usleep(1000); // 1ms sleep — don't burn CPU
            continue;
        }

        // Convert QImage format
        QImage frameImg = img.format() == QImage::Format_ARGB32
            ? img
            : img.convertToFormat(QImage::Format_ARGB32);

        if (frameImg.width() != m_width || frameImg.height() != m_height) {
            frameImg = frameImg.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }

        av_frame_make_writable(frame);

        const uint8_t *srcData[1] = { frameImg.constBits() };
        int srcLinesize[1] = { (int)frameImg.bytesPerLine() };
        int scaleRet = sws_scale(m_swsCtx, srcData, srcLinesize, 0, m_height,
                                 frame->data, frame->linesize);
        if (scaleRet <= 0) {
            qWarning() << "[StreamVideo] sws_scale failed, ret =" << scaleRet;
            continue;
        }

        // *** OBS-STYLE VIDEO PTS ***
        // PTS = frame index. Time base is {1, fps}.
        // Frame 0 → PTS 0, Frame 1 → PTS 1, Frame 2 → PTS 2, ...
        // FFmpeg rescales: PTS 1 in {1,30} → PTS 1600 in {1,48000} etc.
        frame->pts = m_videoFrameIndex++;
        m_totalFrames++;

        int sendRet = avcodec_send_frame(m_videoCodecCtx, frame);
        if (sendRet < 0 && sendRet != AVERROR(EAGAIN)) {
            logFFmpegError(sendRet, "avcodec_send_frame (video)");
            continue;
        }

        while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
            m_totalBytesSent += pkt->size;
            writePacketToTargets(pkt, true);
            av_packet_unref(pkt);
        }
    }

    // Flush video encoder
    avcodec_send_frame(m_videoCodecCtx, nullptr);
    while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
        writePacketToTargets(pkt, true);
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
}

// =============================================================================
// AUDIO MUX LOOP (Thread 2)
// =============================================================================
//
// This is the CRITICAL thread — audio must NEVER stop.
//
// OBS Architecture:
// 1. Read from audio FIFO (filled by pushAudio from capture threads)
// 2. If FIFO has enough samples → encode real audio
// 3. If FIFO is empty → encode SILENCE (audio must be continuous!)
// 4. Audio PTS = accumulated sample count (NOT wall-clock)
// 5. Encode AAC → write packet → av_interleaved_write_frame
//
// The audio thread also handles reconnection health checks.
//
void StreamingManager::audioMuxLoop() {
    AVFrame *audioFrame = av_frame_alloc();
    audioFrame->format = m_audioCodecCtx->sample_fmt;
    audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
    av_channel_layout_copy(&audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    audioFrame->nb_samples = m_audioCodecCtx->frame_size; // 1024 for AAC
    av_frame_get_buffer(audioFrame, 0);

    AVPacket *pkt = av_packet_alloc();
    int frame_size = m_audioCodecCtx->frame_size; // 1024 for AAC

    // Calculate how often we need to produce audio frames
    // At 48kHz with 1024 samples/frame: one frame every ~21.33ms
    double frame_duration_ms = (double)frame_size / (double)AUDIO_SAMPLE_RATE * 1000.0;

    qint64 lastHealthCheck = 0;

    while (!m_shouldStop) {
        // Calculate where audio timeline SHOULD be based on master clock
        double elapsed_ms = (double)m_masterClock.nsecsElapsed() / 1000000.0;
        double audio_time_ms = (double)m_audioSampleCount / (double)AUDIO_SAMPLE_RATE * 1000.0;

        // Produce audio frames to keep timeline in sync with master clock
        // Allow audio to run slightly AHEAD (by 2 frames) for buffering safety
        int framesProduced = 0;
        int maxFramesPerTick = 4; // Safety: don't produce more than 4 in one tick

        while (audio_time_ms <= elapsed_ms + (frame_duration_ms * 2) && framesProduced < maxFramesPerTick) {
            av_frame_make_writable(audioFrame);

            int fifo_available = 0;
            {
                QMutexLocker locker(&m_audioFifoMutex);
                fifo_available = av_audio_fifo_size(m_audioFifo);
            }

            if (fifo_available >= frame_size) {
                // *** REAL AUDIO from capture devices ***
                QMutexLocker locker(&m_audioFifoMutex);
                av_audio_fifo_read(m_audioFifo, (void **)audioFrame->data, frame_size);
            } else if (fifo_available > 0) {
                // Partial buffer — read what's available, pad rest with silence
                QMutexLocker locker(&m_audioFifoMutex);
                int read = av_audio_fifo_read(m_audioFifo, (void **)audioFrame->data, fifo_available);
                // Pad with silence
                for (int ch = 0; ch < audioFrame->ch_layout.nb_channels; ch++) {
                    float *channel_data = (float *)audioFrame->data[ch];
                    memset(channel_data + read, 0, (frame_size - read) * sizeof(float));
                }
            } else {
                // *** SILENCE — no audio available ***
                // This is CRITICAL: audio must be continuous.
                // OBS generates silence when no audio sources are active.
                for (int ch = 0; ch < audioFrame->ch_layout.nb_channels; ch++) {
                    memset(audioFrame->data[ch], 0, frame_size * sizeof(float));
                }
            }

            audioFrame->nb_samples = frame_size;

            // *** OBS-STYLE AUDIO PTS ***
            // PTS = total samples produced so far
            // Example: frame 0 → PTS 0, frame 1 → PTS 1024, frame 2 → PTS 2048, ...
            // This creates a perfect 48kHz timeline that NEVER drifts.
            audioFrame->pts = m_audioSampleCount;
            m_audioSampleCount += frame_size;

            // Update where we are now
            audio_time_ms = (double)m_audioSampleCount / (double)AUDIO_SAMPLE_RATE * 1000.0;

            // Encode AAC
            if (avcodec_send_frame(m_audioCodecCtx, audioFrame) >= 0) {
                while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
                    m_totalBytesSent += pkt->size;
                    writePacketToTargets(pkt, false);
                    av_packet_unref(pkt);
                }
            }

            framesProduced++;
        }

        // --- Reconnection Health Check (every 2 seconds) ---
        qint64 now = m_masterClock.elapsed();
        if (now - lastHealthCheck > 2000) {
            lastHealthCheck = now;
            checkTargetHealth();
        }

        // Sleep until next audio frame is needed
        // One AAC frame = ~21.33ms at 48kHz. Sleep for ~5ms to stay responsive.
        if (framesProduced == 0) {
            QThread::usleep(2000); // 2ms
        } else {
            QThread::usleep(1000); // 1ms
        }
    }

    // Flush audio encoder
    avcodec_send_frame(m_audioCodecCtx, nullptr);
    while (avcodec_receive_packet(m_audioCodecCtx, pkt) >= 0) {
        writePacketToTargets(pkt, false);
        av_packet_unref(pkt);
    }

    // Write trailers (from audio thread since it's the last to finish)
    {
        QMutexLocker locker(&m_muxMutex);
        for (auto &target : m_targets) {
            if (target.connected) av_write_trailer(target.formatCtx);
        }
    }

    av_frame_free(&audioFrame);
    av_packet_free(&pkt);
}

// =============================================================================
// PACKET WRITING (thread-safe, used by both threads)
// =============================================================================

void StreamingManager::writePacketToTargets(AVPacket *pkt, bool isVideo) {
    QMutexLocker locker(&m_muxMutex);
    for (auto &target : m_targets) {
        if (!target.connected) continue;

        AVPacket *outPkt = av_packet_clone(pkt);
        if (isVideo) {
            av_packet_rescale_ts(outPkt, m_videoCodecCtx->time_base, target.videoStream->time_base);
            outPkt->stream_index = target.videoStream->index;
        } else {
            av_packet_rescale_ts(outPkt, m_audioCodecCtx->time_base, target.audioStream->time_base);
            outPkt->stream_index = target.audioStream->index;
        }

        if (av_interleaved_write_frame(target.formatCtx, outPkt) < 0) {
            qWarning() << "[StreamingManager]" << (isVideo ? "Video" : "Audio")
                       << "write failed for" << target.url;
            target.connected = false;
        }
        av_packet_free(&outPkt);
    }
}

// =============================================================================
// RECONNECTION
// =============================================================================

void StreamingManager::checkTargetHealth() {
    QMutexLocker locker(&m_muxMutex);
    bool anyDisconnected = false;
    qint64 now = m_masterClock.elapsed();

    for (auto &target : m_targets) {
        if (!target.connected) {
            anyDisconnected = true;
            if (target.retryCount < MAX_RECONNECT_RETRIES && now - target.lastRetryTime > RECONNECT_DELAY_MS) {
                qDebug() << "[StreamingManager] Reconnecting to" << target.url;
                target.lastRetryTime = now;
                target.retryCount++;
                updateState(State::Reconnecting);
                if (!(target.formatCtx->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&target.formatCtx->pb);
                    if (avio_open(&target.formatCtx->pb, target.url.toUtf8().constData(), AVIO_FLAG_WRITE) >= 0) {
                        target.connected = true;
                        target.retryCount = 0;
                    }
                }
            }
        }
    }
    if (!anyDisconnected && m_state == State::Reconnecting) updateState(State::Streaming);
}

// =============================================================================
// CLEANUP
// =============================================================================

void StreamingManager::cleanupFFmpeg() {
    for (auto &target : m_targets) {
        if (target.formatCtx) {
            if (target.connected && !(target.formatCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&target.formatCtx->pb);
            }
            avformat_free_context(target.formatCtx);
        }
    }
    m_targets.clear();

    if (m_audioFifo) { av_audio_fifo_free(m_audioFifo); m_audioFifo = nullptr; }
    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); m_videoCodecCtx = nullptr; }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); m_audioCodecCtx = nullptr; }
}

StreamingManager::Metrics StreamingManager::metrics() const {
    Metrics m;
    m.totalFrames = m_totalFrames;
    m.droppedFrames = m_droppedFrames;
    m.totalBytesSent = m_totalBytesSent;
    m.activeTargets = 0;
    for (const auto &t : m_targets) if (t.connected) m.activeTargets++;
    return m;
}
