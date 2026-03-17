#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include "NativeCamera.h"
#include <QDebug>
#include <QImage>
#include <QByteArray>
#include <QMetaObject>

@interface CameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate>
@property (nonatomic, assign) NativeCamera *proxy;
@property (nonatomic, strong) AVAudioConverter *converter;
@property (nonatomic, strong) AVAudioFormat *targetFormat;
@property (nonatomic, strong) AVAudioFormat *sourceFormat;
@end

@implementation CameraDelegate
- (instancetype)init {
    self = [super init];
    self.targetFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16 sampleRate:44100 channels:2 interleaved:YES];
    return self;
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    if ([output isKindOfClass:[AVCaptureVideoDataOutput class]]) {
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!imageBuffer) return;
        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        int width = (int)CVPixelBufferGetWidth(imageBuffer);
        int height = (int)CVPixelBufferGetHeight(imageBuffer);
        unsigned char *baseAddress = (unsigned char *)CVPixelBufferGetBaseAddress(imageBuffer);
        QImage image(baseAddress, width, height, QImage::Format_ARGB32);
        QImage copied = image.copy();
        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        QMetaObject::invokeMethod(self.proxy, "frameAvailable", Qt::QueuedConnection, Q_ARG(QImage, copied));
    } else if ([output isKindOfClass:[AVCaptureAudioDataOutput class]]) {
        CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
        const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
        if (!asbd) return;
        
        AVAudioFormat *incomingFormat = [[AVAudioFormat alloc] initWithStreamDescription:asbd];
        if (!self.converter || ![self.sourceFormat isEqual:incomingFormat]) {
            self.sourceFormat = incomingFormat;
            self.converter = [[AVAudioConverter alloc] initFromFormat:self.sourceFormat toFormat:self.targetFormat];
        }
        
        AVAudioPCMBuffer *inBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self.sourceFormat frameCapacity:CMSampleBufferGetNumSamples(sampleBuffer)];
        inBuffer.frameLength = CMSampleBufferGetNumSamples(sampleBuffer);
        CMSampleBufferCopyPCMDataIntoAudioBufferList(sampleBuffer, 0, inBuffer.frameLength, inBuffer.mutableAudioBufferList);
        
        AVAudioPCMBuffer *outBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self.targetFormat frameCapacity:inBuffer.frameLength * (self.targetFormat.sampleRate / self.sourceFormat.sampleRate + 1)];
        NSError *error = nil;
        [self.converter convertToBuffer:outBuffer error:&error withInputFromBlock:^AVAudioBuffer * _Nullable(AVAudioPacketCount inNumberOfPackets, AVAudioConverterInputStatus * _Nonnull outStatus) {
            *outStatus = AVAudioConverterInputStatus_HaveData;
            return inBuffer; // Returning it directly works for a single block
        }];
        
        if (!error && outBuffer.frameLength > 0) {
            int bytes = outBuffer.frameLength * self.targetFormat.streamDescription->mBytesPerFrame;
            QByteArray audioData((const char*)outBuffer.int16ChannelData[0], bytes);
            QMetaObject::invokeMethod(self.proxy, "audioAvailable", Qt::QueuedConnection, Q_ARG(QByteArray, audioData));
        }
    }
}
@end

@interface PlayerDelegate : NSObject
@property (nonatomic, assign) NativeCamera *proxy;
@property (nonatomic, strong) AVPlayer *player;
@property (nonatomic, strong) AVPlayerItemVideoOutput *videoOutput;
@property (nonatomic, strong) NSTimer *timer;
@property (nonatomic, assign) bool loop;

@property (nonatomic, assign) AudioStreamBasicDescription tapFormat;
@property (nonatomic, strong) AVAudioConverter *converter;
@property (nonatomic, strong) AVAudioFormat *targetFormat;
@property (nonatomic, strong) AVAudioFormat *sourceFormat;

- (void)processAudio:(AudioBufferList *)bufferList frames:(CMItemCount)frames;
@end

static void PlayerTapInit(MTAudioProcessingTapRef tap, void *clientInfo, void **tapStorageOut) {
    *tapStorageOut = clientInfo;
}
static void PlayerTapFinalize(MTAudioProcessingTapRef tap) {}
static void PlayerTapPrepare(MTAudioProcessingTapRef tap, CMItemCount maxFrames, const AudioStreamBasicDescription *processingFormat) {
    PlayerDelegate *delegate = (__bridge PlayerDelegate *)MTAudioProcessingTapGetStorage(tap);
    delegate.tapFormat = *processingFormat;
}
static void PlayerTapUnprepare(MTAudioProcessingTapRef tap) {}
static void PlayerTapProcess(MTAudioProcessingTapRef tap, CMItemCount numberFrames, MTAudioProcessingTapFlags flags, AudioBufferList *bufferListInOut, CMItemCount *numberFramesOut, MTAudioProcessingTapFlags *flagsOut) {
    OSStatus status = MTAudioProcessingTapGetSourceAudio(tap, numberFrames, bufferListInOut, flagsOut, NULL, numberFramesOut);
    if (status != noErr) return;
    PlayerDelegate *delegate = (__bridge PlayerDelegate *)MTAudioProcessingTapGetStorage(tap);
    [delegate processAudio:bufferListInOut frames:numberFrames];
}

@implementation PlayerDelegate
- (instancetype)init {
    self = [super init];
    self.targetFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16 sampleRate:44100 channels:2 interleaved:YES];
    return self;
}

- (void)processAudio:(AudioBufferList *)bufferList frames:(CMItemCount)frames {
    AVAudioFormat *incomingFormat = [[AVAudioFormat alloc] initWithStreamDescription:&_tapFormat];
    
    // CoreAudio tap callback might be on multiple threads
    @synchronized (self) {
        if (!self.converter || ![self.sourceFormat isEqual:incomingFormat]) {
            self.sourceFormat = incomingFormat;
            self.converter = [[AVAudioConverter alloc] initFromFormat:self.sourceFormat toFormat:self.targetFormat];
        }
        
        AVAudioPCMBuffer *inBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self.sourceFormat frameCapacity:frames];
        inBuffer.frameLength = frames;
        
        // Copy data handling interleaved vs non-interleaved
        for (UInt32 i = 0; i < bufferList->mNumberBuffers; i++) {
            if (i < inBuffer.mutableAudioBufferList->mNumberBuffers) {
                memcpy(inBuffer.mutableAudioBufferList->mBuffers[i].mData, bufferList->mBuffers[i].mData, bufferList->mBuffers[i].mDataByteSize);
            }
        }
        
        AVAudioPCMBuffer *outBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self.targetFormat frameCapacity:inBuffer.frameLength * (self.targetFormat.sampleRate / self.sourceFormat.sampleRate + 1)];
        NSError *error = nil;
        [self.converter convertToBuffer:outBuffer error:&error withInputFromBlock:^AVAudioBuffer * _Nullable(AVAudioPacketCount inNumberOfPackets, AVAudioConverterInputStatus * _Nonnull outStatus) {
            *outStatus = AVAudioConverterInputStatus_HaveData;
            return inBuffer;
        }];
        
        if (!error && outBuffer.frameLength > 0) {
            int bytes = outBuffer.frameLength * self.targetFormat.streamDescription->mBytesPerFrame;
            QByteArray audioData((const char*)outBuffer.int16ChannelData[0], bytes);
            QMetaObject::invokeMethod(self.proxy, "audioAvailable", Qt::QueuedConnection, Q_ARG(QByteArray, audioData));
        }
    }
}
- (void)setupWithURL:(NSURL *)url loop:(bool)loop {
    self.loop = loop;
    AVAsset *asset = [AVAsset assetWithURL:url];
    AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
    
    NSDictionary *pixBuffAttributes = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
    self.videoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:pixBuffAttributes];
    [item addOutput:self.videoOutput];
    
    // Setup Audio Tap
    dispatch_group_t group = dispatch_group_create();
    dispatch_group_enter(group);
    [asset loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *>* tracks, NSError *error) {
        if (tracks.count > 0) {
            AVAssetTrack *audioTrack = tracks[0];
            MTAudioProcessingTapCallbacks callbacks;
            callbacks.version = kMTAudioProcessingTapCallbacksVersion_0;
            callbacks.clientInfo = (__bridge void *)self;
            callbacks.init = PlayerTapInit;
            callbacks.finalize = PlayerTapFinalize;
            callbacks.prepare = PlayerTapPrepare;
            callbacks.unprepare = PlayerTapUnprepare;
            callbacks.process = PlayerTapProcess;
            
            MTAudioProcessingTapRef tap;
            if (MTAudioProcessingTapCreate(kCFAllocatorDefault, &callbacks, kMTAudioProcessingTapCreationFlag_PostEffects, &tap) == noErr) {
                AVMutableAudioMixInputParameters *inputParams = [AVMutableAudioMixInputParameters audioMixInputParametersWithTrack:audioTrack];
                inputParams.audioTapProcessor = tap;
                
                AVMutableAudioMix *audioMix = [AVMutableAudioMix audioMix];
                audioMix.inputParameters = @[inputParams];
                item.audioMix = audioMix;
                CFRelease(tap);
            }
        }
        dispatch_group_leave(group);
    }];
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    
    self.player = [AVPlayer playerWithPlayerItem:item];
    
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(playerItemDidReachEnd:) name:AVPlayerItemDidPlayToEndTimeNotification object:self.player.currentItem];
    
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 target:self selector:@selector(onTick) userInfo:nil repeats:YES];
    [self.player play];
}

- (void)playerItemDidReachEnd:(NSNotification *)notification {
    if (self.loop) {
        [self.player seekToTime:kCMTimeZero];
        [self.player play];
    } else {
        QMetaObject::invokeMethod(self.proxy, "playbackFinished", Qt::QueuedConnection);
    }
}

- (void)onTick {
    CMTime itemTime = [self.videoOutput itemTimeForHostTime:CACurrentMediaTime()];
    if ([self.videoOutput hasNewPixelBufferForItemTime:itemTime]) {
        CVPixelBufferRef pixelBuffer = [self.videoOutput copyPixelBufferForItemTime:itemTime itemTimeForDisplay:NULL];
        if (pixelBuffer) {
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            int width = (int)CVPixelBufferGetWidth(pixelBuffer);
            int height = (int)CVPixelBufferGetHeight(pixelBuffer);
            unsigned char *baseAddress = (unsigned char *)CVPixelBufferGetBaseAddress(pixelBuffer);
            QImage image(baseAddress, width, height, QImage::Format_ARGB32);
            QImage copied = image.copy();
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CVBufferRelease(pixelBuffer);
            QMetaObject::invokeMethod(self.proxy, "frameAvailable", Qt::QueuedConnection, Q_ARG(QImage, copied));
        }
    }
    
    // Position Update
    if (self.player.currentItem) {
        double current = CMTimeGetSeconds(self.player.currentTime);
        double total = CMTimeGetSeconds(self.player.currentItem.duration);
        if (total > 0) {
            QMetaObject::invokeMethod(self.proxy, "positionChanged", Qt::QueuedConnection, 
                Q_ARG(double, current / total), Q_ARG(double, current), Q_ARG(double, total));
        }
    }
}

- (void)stop {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.player pause];
    [self.timer invalidate];
    self.timer = nil;
    self.player = nil;
    self.videoOutput = nil;
}
@end

struct NativeCameraPrivate {
    AVCaptureSession *session;
    CameraDelegate *camDelegate;
    PlayerDelegate *playerDelegate;
    bool active;
    bool isCamera;
};

NativeCamera::NativeCamera(QObject *parent) : QObject(parent) {
    NativeCameraPrivate *p = new NativeCameraPrivate();
    p->session = nil;
    p->camDelegate = [[CameraDelegate alloc] init];
    p->camDelegate.proxy = this;
    p->playerDelegate = [[PlayerDelegate alloc] init];
    p->playerDelegate.proxy = this;
    p->active = false;
    p->isCamera = false;
    m_opaque = p;
}

NativeCamera::~NativeCamera() {
    stop();
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    [p->camDelegate release];
    [p->playerDelegate release];
    delete p;
}

QList<DeviceInfo> NativeCamera::enumerateDevices() {
    QList<DeviceInfo> list;
    AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession 
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal]
        mediaType:AVMediaTypeVideo 
        position:AVCaptureDevicePositionUnspecified];
    for (AVCaptureDevice *device in discoverySession.devices) {
        DeviceInfo info;
        info.id = QString::fromNSString(device.uniqueID);
        info.name = QString::fromNSString(device.localizedName);
        list.append(info);
    }
    return list;
}

QList<DeviceInfo> NativeCamera::enumerateAudioDevices() {
    QList<DeviceInfo> list;
    AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession 
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInMicrophone, AVCaptureDeviceTypeExternal]
        mediaType:AVMediaTypeAudio 
        position:AVCaptureDevicePositionUnspecified];
    for (AVCaptureDevice *device in discoverySession.devices) {
        DeviceInfo info;
        info.id = QString::fromNSString(device.uniqueID);
        info.name = QString::fromNSString(device.localizedName);
        list.append(info);
    }
    return list;
}

bool NativeCamera::start(const QString &deviceId, int width, int height, int fps) {
    stop();
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    p->isCamera = true;
    AVCaptureDevice *device = [AVCaptureDevice deviceWithUniqueID:deviceId.toNSString()];
    if (!device) return false;
    
    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    if (error) return false;

    p->session = [[AVCaptureSession alloc] init];
    [p->session beginConfiguration];
    if ([p->session canAddInput:input]) [p->session addInput:input];
    
    AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
    [output setSampleBufferDelegate:p->camDelegate queue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)];
    output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
    if ([p->session canAddOutput:output]) [p->session addOutput:output];
    
    // Add audio input for camera
    AVCaptureDevice *audioDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    if (audioDevice) {
        AVCaptureDeviceInput *audioInput = [AVCaptureDeviceInput deviceInputWithDevice:audioDevice error:nil];
        if (audioInput && [p->session canAddInput:audioInput]) {
            [p->session addInput:audioInput];
            
            AVCaptureAudioDataOutput *audioOutput = [[AVCaptureAudioDataOutput alloc] init];
            [audioOutput setSampleBufferDelegate:p->camDelegate queue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)];
            if ([p->session canAddOutput:audioOutput]) {
                [p->session addOutput:audioOutput];
            }
        }
    }

    [p->session commitConfiguration];
    [p->session startRunning];
    p->active = true;
    return true;
}

bool NativeCamera::startFile(const QUrl &fileUrl, bool loop) {
    stop();
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    p->isCamera = false;
    NSURL *url = [NSURL fileURLWithPath:fileUrl.toLocalFile().toNSString()];
    [p->playerDelegate setupWithURL:url loop:loop];
    p->active = true;
    return true;
}

void NativeCamera::pause() {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (!p->isCamera) [p->playerDelegate.player pause];
}

void NativeCamera::resume() {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (!p->isCamera) [p->playerDelegate.player play];
}

void NativeCamera::seek(double positionPercent) {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (!p->isCamera && p->playerDelegate.player.currentItem) {
        double total = CMTimeGetSeconds(p->playerDelegate.player.currentItem.duration);
        CMTime target = CMTimeMakeWithSeconds(total * positionPercent, 1000);
        [p->playerDelegate.player seekToTime:target];
    }
}

void NativeCamera::setVolume(int volume) {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    float vol = volume / 100.0f;
    if (!p->isCamera) p->playerDelegate.player.volume = vol;
}

void NativeCamera::setMuted(bool muted) {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (!p->isCamera) p->playerDelegate.player.muted = muted;
}

void NativeCamera::stop() {
    NativeCameraPrivate *p = (NativeCameraPrivate *)m_opaque;
    if (p->session) {
        [p->session stopRunning];
        [p->session release];
        p->session = nil;
    }
    [p->playerDelegate stop];
    p->active = false;
}

bool NativeCamera::isActive() const {
    return ((NativeCameraPrivate *)m_opaque)->active;
}
