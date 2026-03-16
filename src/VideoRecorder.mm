#import "VideoRecorder.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

@interface AVRecorderImpl : NSObject
@property (strong) AVAssetWriter *writer;
@property (strong) AVAssetWriterInput *videoInput;
@property (strong) AVAssetWriterInputPixelBufferAdaptor *adaptor;
@property (assign) CMTime startTime;
@property (assign) BOOL started;
@end

@implementation AVRecorderImpl
@end

VideoRecorder::VideoRecorder(QObject *parent) : QObject(parent), m_pimpl(nullptr) {
    m_pimpl = (__bridge_retained void*)[[AVRecorderImpl alloc] init];
}

VideoRecorder::~VideoRecorder() {
    stopRecording();
    AVRecorderImpl *impl = (__bridge_transfer AVRecorderImpl*)m_pimpl;
    impl = nil;
}

bool VideoRecorder::startRecording(const QString &filePath, int width, int height, int fps) {
    if (m_isRecording) return false;

    m_width = width;
    m_height = height;

    AVRecorderImpl *impl = (__bridge AVRecorderImpl*)m_pimpl;
    
    NSURL *url = [NSURL fileURLWithPath:filePath.toNSString()];
    
    // Remove existing file if any
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

    NSError *error = nil;
    impl.writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&error];
    if (error) return false;

    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height),
    };

    impl.videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
    impl.videoInput.expectsMediaDataInRealTime = YES;
    
    NSDictionary *pixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32RGBA),
        (id)kCVPixelBufferWidthKey: @(width),
        (id)kCVPixelBufferHeightKey: @(height),
    };

    impl.adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:impl.videoInput 
                                                                                   sourcePixelBufferAttributes:pixelBufferAttributes];

    if ([impl.writer canAddInput:impl.videoInput]) {
        [impl.writer addInput:impl.videoInput];
    } else {
        return false;
    }

    if ([impl.writer startWriting]) {
        m_isRecording = true;
        impl.started = NO;
        return true;
    }

    return false;
}

void VideoRecorder::stopRecording() {
    if (!m_isRecording) return;

    AVRecorderImpl *impl = (__bridge AVRecorderImpl*)m_pimpl;
    [impl.videoInput markAsFinished];
    [impl.writer finishWritingWithCompletionHandler:^{
        // Writing finished
    }];

    m_isRecording = false;
}

void VideoRecorder::writeFrame(const QImage &frame, qint64 timestampMs) {
    if (!m_isRecording) return;

    AVRecorderImpl *impl = (__bridge AVRecorderImpl*)m_pimpl;
    if (!impl.videoInput.isReadyForMoreMediaData) return;

    if (!impl.started) {
        [impl.writer startSessionAtSourceTime:CMTimeMake(timestampMs, 1000)];
        impl.startTime = CMTimeMake(timestampMs, 1000);
        impl.started = YES;
    }

    CVPixelBufferRef pixelBuffer = NULL;
    CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, impl.adaptor.pixelBufferPool, &pixelBuffer);
    
    if (status != kCVReturnSuccess || pixelBuffer == NULL) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    void *data = CVPixelBufferGetBaseAddress(pixelBuffer);
    
    // Convert QImage to RGBA (AVAssetWriter likes RGBA/BGRA)
    QImage swapped = frame.convertToFormat(QImage::Format_RGBA8888);
    memcpy(data, swapped.bits(), swapped.sizeInBytes());
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

    CMTime presentationTime = CMTimeMake(timestampMs, 1000);
    [impl.adaptor appendPixelBuffer:pixelBuffer withPresentationTime:presentationTime];
    
    CVPixelBufferRelease(pixelBuffer);
}
