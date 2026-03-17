#ifndef NATIVECAMERA_H
#define NATIVECAMERA_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QList>
#include <QUrl>

struct DeviceInfo {
    QString id;
    QString name;
};

class NativeCamera : public QObject {
    Q_OBJECT
public:
    explicit NativeCamera(QObject *parent = nullptr);
    ~NativeCamera();

    static QList<DeviceInfo> enumerateDevices();
    static QList<DeviceInfo> enumerateAudioDevices();
    
    // Camera Control
    bool start(const QString &deviceId, int width = 1280, int height = 720, int fps = 30);
    
    // Media Control
    bool startFile(const QUrl &fileUrl, bool loop = false);
    void pause();
    void resume();
    void seek(double positionPercent); // 0.0 to 1.0
    
    // Audio Control
    void setVolume(int volume); // 0 to 100
    void setMuted(bool muted);

    void stop();
    bool isActive() const;

signals:
    void frameAvailable(const QImage &image);
    void audioAvailable(const QByteArray &data);
    void positionChanged(double positionPercent, double currentTime, double totalTime);
    void playbackFinished();

private:
    void *m_opaque; 
};

#endif // NATIVECAMERA_H
