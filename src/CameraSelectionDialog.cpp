#include "CameraSelectionDialog.h"
#include "CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QDebug>
#include <QSize>
#include <QVariant>

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
    setWindowTitle("Select Camera Input");
    setMinimumWidth(400);
    setMinimumHeight(500);
    
    // Main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Camera selection section
    QLabel *cameraLabel = new QLabel("Available Cameras:", this);
    mainLayout->addWidget(cameraLabel);
    
    m_cameraListWidget = new QListWidget(this);
    m_cameraListWidget->setMinimumHeight(150);
    connect(m_cameraListWidget, &QListWidget::itemClicked,
            this, &CameraSelectionDialog::onCameraSelected);
    mainLayout->addWidget(m_cameraListWidget);
    
    // Resolution section
    QLabel *resolutionLabel = new QLabel("Resolution:", this);
    mainLayout->addWidget(resolutionLabel);
    
    m_resolutionComboBox = new QComboBox(this);
    connect(m_resolutionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSelectionDialog::onResolutionChanged);
    mainLayout->addWidget(m_resolutionComboBox);
    
    // FPS section
    QLabel *fpsLabel = new QLabel("Frame Rate (FPS):", this);
    mainLayout->addWidget(fpsLabel);
    
    m_fpsComboBox = new QComboBox(this);
    mainLayout->addWidget(m_fpsComboBox);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    m_okButton = new QPushButton("OK", this);
    m_cancelButton = new QPushButton("Cancel", this);
    
    connect(m_okButton, &QPushButton::clicked, this, &CameraSelectionDialog::onOkClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    
    mainLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
}

void CameraSelectionDialog::populateCameraList() {
    m_cameraListWidget->clear();
    
    const auto cameras = m_cameraManager->getAvailableCameras();
    
    for (const auto &camera : cameras) {
        QListWidgetItem *item = new QListWidgetItem(camera.name);
        item->setData(Qt::UserRole, camera.id);
        m_cameraListWidget->addItem(item);
    }
    
    // Select first camera by default
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
    m_resolutionComboBox->addItem("320x240", QVariant::fromValue(QSize(320, 240)));
    m_resolutionComboBox->addItem("640x480", QVariant::fromValue(QSize(640, 480)));
    m_resolutionComboBox->addItem("800x600", QVariant::fromValue(QSize(800, 600)));
    m_resolutionComboBox->addItem("1024x768", QVariant::fromValue(QSize(1024, 768)));
    m_resolutionComboBox->addItem("1280x720", QVariant::fromValue(QSize(1280, 720)));
    m_resolutionComboBox->addItem("1920x1080", QVariant::fromValue(QSize(1920, 1080)));
    
    // Select 640x480 by default
    m_resolutionComboBox->setCurrentIndex(1);
    
    populateFrameRates();
}

void CameraSelectionDialog::populateFrameRates() {
    m_fpsComboBox->clear();
    m_fpsComboBox->addItem("15 FPS", 15);
    m_fpsComboBox->addItem("24 FPS", 24);
    m_fpsComboBox->addItem("30 FPS", 30);
    m_fpsComboBox->addItem("60 FPS", 60);
    
    // Select 30 FPS by default
    m_fpsComboBox->setCurrentIndex(2);
}

void CameraSelectionDialog::onResolutionChanged(int index) {
    QVariant data = m_resolutionComboBox->itemData(index);
    QSize resolution = qvariant_cast<QSize>(data);
    m_selectedWidth = resolution.width();
    m_selectedHeight = resolution.height();
}

void CameraSelectionDialog::onOkClicked() {
    m_selectedFps = m_fpsComboBox->currentData().toInt();
    accept();
}

QString CameraSelectionDialog::getSelectedCameraId() const {
    return m_selectedCameraId;
}

int CameraSelectionDialog::getSelectedWidth() const {
    return m_selectedWidth;
}

int CameraSelectionDialog::getSelectedHeight() const {
    return m_selectedHeight;
}

int CameraSelectionDialog::getSelectedFps() const {
    return m_selectedFps;
}
