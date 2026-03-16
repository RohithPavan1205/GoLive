#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include "NativeCamera.h"
#include <QDebug>
#include <QImage>

@interface CameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) NativeCamera *proxy;
@end

@implementation CameraDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
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
}
@end

@interface PlayerDelegate : NSObject
@property (nonatomic, assign) NativeCamera *proxy;
@property (nonatomic, strong) AVPlayer *player;
@property (nonatomic, strong) AVPlayerItemVideoOutput *videoOutput;
@property (nonatomic, strong) NSTimer *timer;
@property (nonatomic, assign) bool loop;
@end

@implementation PlayerDelegate
- (void)setupWithURL:(NSURL *)url loop:(bool)loop {
    self.loop = loop;
    self.player = [AVPlayer playerWithURL:url];
    NSDictionary *pixBuffAttributes = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
    self.videoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:pixBuffAttributes];
    [self.player.currentItem addOutput:self.videoOutput];
    
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
