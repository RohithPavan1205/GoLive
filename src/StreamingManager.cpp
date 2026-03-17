#include "StreamingManager.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>

StreamingManager::StreamingManager(QObject *parent) : QObject(parent) {
    avformat_network_init();
}

StreamingManager::~StreamingManager() {
    stopStreaming();
}

void StreamingManager::updateState(State newState) {
    if (m_state == newState) return;
    m_state = newState;
    emit stateChanged(newState);
}

static int interrupt_cb(void *ctx) {
    std::atomic<bool> *should_stop = static_cast<std::atomic<bool>*>(ctx);
    return (should_stop && should_stop->load()) ? 1 : 0;
}

void StreamingManager::logFFmpegError(int err, const QString &context) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, errbuf, AV_ERROR_MAX_STRING_SIZE);
    QString msg = QString("%1: %2 (%3)").arg(context).arg(errbuf).arg(err);
    qWarning() << "[StreamingManager]" << msg;
    emit errorOccurred(msg);
}

bool StreamingManager::startStreaming(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    if (isActive()) {
        qWarning() << "[StreamingManager] Already active, stop first.";
        return false;
    }

    // 1. Validation
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
    m_shouldStop = false;

    m_workerThread = QThread::create([this]() {
        if (initFFmpeg(m_initialUrls, m_width, m_height, m_fps, m_bitrate)) {
            updateState(State::Streaming);
            streamLoop();
        } else {
            cleanupFFmpeg();
            if (!m_shouldStop) updateState(State::Failed);
            else updateState(State::Idle);
        }
    });
    m_workerThread->setObjectName("StreamingWorker");
    m_workerThread->start();
    
    return true;
}

void StreamingManager::stopStreaming() {
    if (m_state == State::Idle) return;
    
    updateState(State::Stopping);
    m_shouldStop = true;
    
    if (m_workerThread) {
        if (!m_workerThread->wait(5000)) {
            qWarning() << "[StreamingManager] Worker thread timed out, terminating...";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    
    cleanupFFmpeg();
    updateState(State::Idle);
}

void StreamingManager::pushFrame(const QImage &image) {
    if (m_state != State::Streaming) return;
    
    QMutexLocker locker(&m_queueMutex);
    if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
        m_droppedFrames++;
        return;
    }
    m_frameQueue.push(image.copy());
}

void StreamingManager::pushAudio(const QByteArray &data) {
    if (m_state != State::Streaming) return;
    QMutexLocker locker(&m_queueMutex);
    if (m_audioQueue.size() >= 100) return; // Silent drop for audio
    m_audioQueue.push(data);
}

bool StreamingManager::initFFmpeg(const QList<QString> &urls, int width, int height, int fps, int bitrate) {
    int ret;

    // 1. Video Encoder Fallback Logic
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
    m_videoCodecCtx->time_base = {1, fps};
    m_videoCodecCtx->framerate = {fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12; // Standard for Apple hardware
    if (strcmp(videoCodec->name, "libx264") == 0) m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    
    m_videoCodecCtx->bit_rate = bitrate;
    m_videoCodecCtx->gop_size = fps * 2;
    m_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if ((ret = avcodec_open2(m_videoCodecCtx, videoCodec, nullptr)) < 0) {
        logFFmpegError(ret, "Failed to open video codec");
        return false;
    }

    // 2. Audio Encoder (Temporarily disabled to fix video-only stall)
    /*
    const AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    ...
    */

    // 3. Create Outputs
    for (const QString &url : urls) {
        if (m_shouldStop) break;
        OutputTarget target;
        target.url = url;
        if ((ret = avformat_alloc_output_context2(&target.formatCtx, nullptr, "flv", url.toUtf8().constData())) < 0) {
            logFFmpegError(ret, QString("Context allocation failed for %1").arg(url));
            continue;
        }

        // Add interrupt callback
        target.formatCtx->interrupt_callback.callback = interrupt_cb;
        target.formatCtx->interrupt_callback.opaque = &m_shouldStop;

        target.videoStream = avformat_new_stream(target.formatCtx, videoCodec);
        avcodec_parameters_from_context(target.videoStream->codecpar, m_videoCodecCtx);
        
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

    // 4. Converters
    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, m_videoCodecCtx->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
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

        bool handledSomething = false;

        // --- Video Processing ---
        if (!img.isNull()) {
            handledSomething = true;
            uint8_t *srcData[1] = { (uint8_t*)img.bits() };
            int srcLinesize[1] = { (int)img.bytesPerLine() };
            sws_scale(m_swsCtx, srcData, srcLinesize, 0, m_height, videoFrame->data, videoFrame->linesize);
            videoFrame->pts = m_frameCount++;
            m_totalFrames++;

            if (avcodec_send_frame(m_videoCodecCtx, videoFrame) >= 0) {
                while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
                    m_totalBytesSent += pkt->size;
                    for (auto &target : m_targets) {
                        if (!target.connected) continue;
                        AVPacket *outPkt = av_packet_clone(pkt);
                        av_packet_rescale_ts(outPkt, m_videoCodecCtx->time_base, target.videoStream->time_base);
                        outPkt->stream_index = target.videoStream->index;
                        
                        // Use av_write_frame (non-interleaved) for video-only to avoid stall
                        if (av_write_frame(target.formatCtx, outPkt) < 0) {
                            qWarning() << "[StreamingManager] Write failed for" << target.url;
                            target.connected = false;
                        }
                        av_packet_free(&outPkt);
                    }
                    av_packet_unref(pkt);
                }
            }
        }

        // --- Audio Processing (Disabled) ---
        /*
        if (!audioData.isEmpty()) {
            ...
        }
        */

        // --- Reconnection & Health Check ---
        static qint64 lastHealthCheck = 0;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastHealthCheck > 2000) {
            lastHealthCheck = now;
            bool anyDisconnected = false;
            for (auto &target : m_targets) {
                if (!target.connected) {
                    anyDisconnected = true;
                    if (target.retryCount < MAX_RECONNECT_RETRIES && now - target.lastRetryTime > RECONNECT_DELAY_MS) {
                        qDebug() << "[StreamingManager] Reconnecting to" << target.url;
                        target.lastRetryTime = now;
                        target.retryCount++;
                        updateState(State::Reconnecting);
                        // Simple retry: Close and reopen IO
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

        if (!handledSomething) QThread::msleep(5);
    }

    // Flush encoders
    avcodec_send_frame(m_videoCodecCtx, nullptr);
    while (avcodec_receive_packet(m_videoCodecCtx, pkt) >= 0) {
        for (auto &target : m_targets) {
            if (target.connected) {
                AVPacket *outPkt = av_packet_clone(pkt);
                av_interleaved_write_frame(target.formatCtx, outPkt);
                av_packet_free(&outPkt);
            }
        }
        av_packet_unref(pkt);
    }

    for (auto &target : m_targets) {
        if (target.connected) av_write_trailer(target.formatCtx);
    }
    
    av_frame_free(&videoFrame);
    av_frame_free(&audioFrame);
    av_packet_free(&pkt);
}

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
