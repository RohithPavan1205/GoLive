#ifndef EFFECTSMANAGER_H
#define EFFECTSMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPixmap>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>

class ThumbnailLabel : public QLabel {
    Q_OBJECT
public:
    explicit ThumbnailLabel(const QString &filePath, QWidget *parent = nullptr);
    
signals:
    void thumbnailLoaded(QPixmap pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_filePath;
    QPixmap m_pixmap;
    bool m_loaded = false;
};

class EffectsManager : public QObject {
    Q_OBJECT
public:
    explicit EffectsManager(const QString &effectsPath, QObject *parent = nullptr);
    
    void setupUI(QTreeWidget *treeWidget, QStackedWidget *stackedWidget, QWidget *mainWindow);
    void handleResize(); 

private slots:
    void onCategorySelected(QTreeWidgetItem *item, int column);
    void delayedRebuild();

private:
    void scanDirectories();
    void populateCategory(const QString &categoryName);
    
    QString m_effectsPath;
    QMap<QString, QStringList> m_categoryFiles;
    QMap<QString, QWidget*> m_contentsWidgets;
    QStackedWidget *m_stackedWidget = nullptr;
    QString m_currentCategory;
    QTimer *m_resizeTimer;
    int m_lastWidth = 0;
};

#endif // EFFECTSMANAGER_H
