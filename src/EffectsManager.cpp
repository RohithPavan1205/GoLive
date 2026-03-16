#include "EffectsManager.h"
#include <QDir>
#include <QDirIterator>
#include <QGridLayout>
#include <QPainter>
#include <QDebug>
#include <QScrollArea>

ThumbnailLabel::ThumbnailLabel(const QString &filePath, QWidget *parent)
    : QLabel(parent), m_filePath(filePath) 
{
    setFixedHeight(110); 
    setMinimumWidth(150);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet("background-color: #000000; border: 1px solid #333333; border-radius: 4px;");
    
    connect(this, &ThumbnailLabel::thumbnailLoaded, this, [this](QPixmap pix) {
        m_pixmap = pix;
        m_loaded = true;
        update();
    }, Qt::QueuedConnection);

    struct LoadTask : public QRunnable {
        QString path;
        QObject *receiver;
        void run() override {
            QImage img(path);
            if (!img.isNull()) {
                QPixmap pix = QPixmap::fromImage(img.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                QMetaObject::invokeMethod(receiver, "thumbnailLoaded", Qt::QueuedConnection, Q_ARG(QPixmap, pix));
            }
        }
    };
    LoadTask *task = new LoadTask();
    task->path = m_filePath;
    task->receiver = this;
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}

void ThumbnailLabel::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    if (m_loaded && !m_pixmap.isNull()) {
        QPixmap scaled = m_pixmap.scaled(size() - QSize(10, 24), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - 20 - scaled.height()) / 2 + 2;
        painter.drawPixmap(x, y, scaled);
    } else {
        painter.setPen(QColor(60, 60, 60));
        painter.drawText(rect(), Qt::AlignCenter, "...");
    }
    
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, height() - 18, width(), 18);
    
    QString name = QFileInfo(m_filePath).baseName();
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    painter.drawText(QRect(6, height() - 18, width() - 12, 18), Qt::AlignLeft | Qt::AlignVCenter | Qt::ElideRight, name);
}

EffectsManager::EffectsManager(const QString &effectsPath, QObject *parent)
    : QObject(parent), m_effectsPath(effectsPath), m_currentCategory("web01") 
{
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    connect(m_resizeTimer, &QTimer::timeout, this, &EffectsManager::delayedRebuild);
    scanDirectories();
}

void EffectsManager::scanDirectories() {
    QDir dir(m_effectsPath);
    QStringList topFolders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : topFolders) {
        QStringList allFiles;
        QDirIterator it(dir.absoluteFilePath(folder), QStringList() << "*.png" << "*.jpg" << "*.jpeg", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            allFiles.append(it.next());
        }
        m_categoryFiles[folder.toLower()] = allFiles;
    }
}

void EffectsManager::setupUI(QTreeWidget *treeWidget, QStackedWidget *stackedWidget, QWidget *mainWindow) {
    m_stackedWidget = stackedWidget;
    connect(treeWidget, &QTreeWidget::itemClicked, this, &EffectsManager::onCategorySelected);

    QStringList cats = {"web01", "web02", "web03", "god01", "muslim", "stage", "telugu"};
    for (const QString &cat : cats) {
        QString contentsName = "scrollAreaWidgetContents_" + cat;
        QWidget *contents = mainWindow->findChild<QWidget*>(contentsName);
        if (contents) {
            m_contentsWidgets[cat] = contents;
            // Dont populate all at once to avoid lag and potential crash
        }
    }
    populateCategory(m_currentCategory);
}

void EffectsManager::populateCategory(const QString &categoryName) {
    if (!m_contentsWidgets.contains(categoryName)) return;
    QWidget *contents = m_contentsWidgets[categoryName];

    // Clear 
    QList<QWidget*> children = contents->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) { child->hide(); child->deleteLater(); }
    if (contents->layout()) delete contents->layout();

    QGridLayout *layout = new QGridLayout(contents);
    layout->setContentsMargins(10, 10, 10, 10); 
    layout->setSpacing(10); 
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    const QStringList &files = m_categoryFiles[categoryName];
    
    int contentsWidth = 400;
    if (contents->parentWidget() && contents->parentWidget()->parentWidget()) {
        QScrollArea *sa = qobject_cast<QScrollArea*>(contents->parentWidget()->parentWidget());
        if (sa) contentsWidth = sa->viewport()->width();
    }
    
    int cols = qMax(1, (contentsWidth - 20) / 160); // 160 = minWidth + spacing
    
    int row = 0;
    int col = 0;
    for (const QString &path : files) {
        ThumbnailLabel *label = new ThumbnailLabel(path, contents);
        layout->addWidget(label, row, col);
        col++;
        if (col >= cols) { col = 0; row++; }
    }
    
    for (int i = 0; i < cols; ++i) layout->setColumnStretch(i, 1);
}

void EffectsManager::handleResize() {
    if (!m_contentsWidgets.contains(m_currentCategory)) return;
    QWidget *contents = m_contentsWidgets[m_currentCategory];
    int currentWidth = contents->width();
    if (qAbs(currentWidth - m_lastWidth) > 50) { // Only rebuild if width changed significantly
        m_lastWidth = currentWidth;
        m_resizeTimer->start(150); // Debounce
    }
}

void EffectsManager::delayedRebuild() {
    populateCategory(m_currentCategory);
}

void EffectsManager::onCategorySelected(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    QString text = item->text(0).split("(").first().trimmed().toLower();
    if (text == m_currentCategory) return;
    m_currentCategory = text;
    
    QMap<QString, int> catToIndex;
    catToIndex["web01"] = 0; catToIndex["web02"] = 1; catToIndex["web03"] = 2;
    catToIndex["god01"] = 3; catToIndex["muslim"] = 4; catToIndex["stage"] = 5;
    catToIndex["telugu"] = 6;
    
    if (catToIndex.contains(text) && m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(catToIndex[text]);
        populateCategory(text); 
    }
}
