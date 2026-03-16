#include "CameraSelectionDialog.h"
#include "CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>

CameraSelectionDialog::CameraSelectionDialog(CameraManager *cameraManager, QWidget *parent)
    : QDialog(parent)
    , m_cameraManager(cameraManager)
    , m_selectedWidth(640)
    , m_selectedHeight(480)
    , m_selectedFps(30)
{
    setupUI();
    populateCameraList();
}

void CameraSelectionDialog::setupUI() {
    setWindowTitle("Select Camera Input (Native)");
    setMinimumSize(400, 500);
    setStyleSheet("background-color: #1a1a1a; color: #eeeeee;");
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    mainLayout->addWidget(new QLabel("AVAILABLE HARDWARE (OBS MODE):"));
    m_cameraListWidget = new QListWidget(this);
    m_cameraListWidget->setStyleSheet("background-color: #262626; border: 1px solid #333333; padding: 5px;");
    connect(m_cameraListWidget, &QListWidget::itemClicked, this, &CameraSelectionDialog::onCameraSelected);
    mainLayout->addWidget(m_cameraListWidget);
    
    mainLayout->addWidget(new QLabel("RESOLUTION PRESET:"));
    m_resolutionComboBox = new QComboBox(this);
    m_resolutionComboBox->setStyleSheet("background-color: #333333; padding: 5px;");
    connect(m_resolutionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CameraSelectionDialog::onResolutionChanged);
    mainLayout->addWidget(m_resolutionComboBox);
    
    mainLayout->addWidget(new QLabel("FRAMERATE:"));
    m_fpsComboBox = new QComboBox(this);
    m_fpsComboBox->setStyleSheet("background-color: #333333; padding: 5px;");
    m_fpsComboBox->addItem("30 FPS", 30);
    m_fpsComboBox->addItem("60 FPS", 60);
    m_fpsComboBox->setCurrentIndex(0);
    mainLayout->addWidget(m_fpsComboBox);
    
    mainLayout->addStretch();
    
    QHBoxLayout *btnLayout = new QHBoxLayout;
    QPushButton *okBtn = new QPushButton("INITIALIZE SOURCE", this);
    QPushButton *cancelBtn = new QPushButton("CANCEL", this);
    okBtn->setStyleSheet("background-color: #0078d4; color: white; padding: 10px; font-weight: bold;");
    cancelBtn->setStyleSheet("background-color: #444444; color: white; padding: 10px;");
    
    connect(okBtn, &QPushButton::clicked, this, &CameraSelectionDialog::onOkClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

void CameraSelectionDialog::populateCameraList() {
    m_cameraListWidget->clear();
    auto cameras = m_cameraManager->getAvailableCameras();
    for (const auto &cam : cameras) {
        QListWidgetItem *item = new QListWidgetItem(cam.name);
        item->setData(Qt::UserRole, cam.id);
        m_cameraListWidget->addItem(item);
    }
    if (m_cameraListWidget->count() > 0) {
        m_cameraListWidget->setCurrentRow(0);
        onCameraSelected(m_cameraListWidget->item(0));
    }
}

void CameraSelectionDialog::onCameraSelected(QListWidgetItem *item) {
    m_selectedCameraId = item->data(Qt::UserRole).toString();
    populateResolutions(m_selectedCameraId);
}

void CameraSelectionDialog::populateResolutions(const QString &cameraId) {
    m_resolutionComboBox->clear();
    m_resolutionComboBox->addItem("640x480 (SD)", QVariant::fromValue(QSize(640, 480)));
    m_resolutionComboBox->addItem("1280x720 (HD)", QVariant::fromValue(QSize(1280, 720)));
    m_resolutionComboBox->addItem("1920x1080 (FHD)", QVariant::fromValue(QSize(1920, 1080)));
    m_resolutionComboBox->setCurrentIndex(1);
}

void CameraSelectionDialog::onResolutionChanged(int index) {
    QSize res = qvariant_cast<QSize>(m_resolutionComboBox->itemData(index));
    m_selectedWidth = res.width();
    m_selectedHeight = res.height();
}

void CameraSelectionDialog::onOkClicked() {
    m_selectedFps = m_fpsComboBox->currentData().toInt();
    accept();
}

QString CameraSelectionDialog::getSelectedCameraId() const { return m_selectedCameraId; }
int CameraSelectionDialog::getSelectedWidth() const { return m_selectedWidth; }
int CameraSelectionDialog::getSelectedHeight() const { return m_selectedHeight; }
int CameraSelectionDialog::getSelectedFps() const { return m_selectedFps; }
