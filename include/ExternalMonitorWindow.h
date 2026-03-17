#ifndef EXTERNALMONITORWINDOW_H
#define EXTERNALMONITORWINDOW_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <QWindow>

class ExternalMonitorWindow : public QWidget {
    Q_OBJECT
public:
    explicit ExternalMonitorWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("GoLive - External Monitor");
        setStyleSheet("background-color: black;");
    }

public slots:
    void updateFrame(const QImage &image) {
        m_frame = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        if (!m_frame.isNull()) {
            QImage scaled = m_frame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int x = (width() - scaled.width()) / 2;
            int y = (height() - scaled.height()) / 2;
            painter.drawImage(x, y, scaled);
        } else {
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 40, QFont::Bold));
            painter.drawText(rect(), Qt::AlignCenter, "GoLive Studio\nNo Live Source");
        }
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
        }
    }

private:
    QImage m_frame;
};

#endif // EXTERNALMONITORWINDOW_H
