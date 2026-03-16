#ifndef CAMERASELECTIONDIALOG_H
#define CAMERASELECTIONDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <memory>

class CameraManager;

class CameraSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit CameraSelectionDialog(CameraManager *cameraManager, QWidget *parent = nullptr);
    
    QString getSelectedCameraId() const;
    int getSelectedWidth() const;
    int getSelectedHeight() const;
    int getSelectedFps() const;

private slots:
    void onCameraSelected(QListWidgetItem *item);
    void onResolutionChanged(int index);
    void populateCameraList();
    void onOkClicked();

private:
    void setupUI();
    void populateResolutions(const QString &cameraId);
    void populateFrameRates();
    
    CameraManager *m_cameraManager;
    
    // UI Components
    QListWidget *m_cameraListWidget;
    QComboBox *m_resolutionComboBox;
    QComboBox *m_fpsComboBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    
    QString m_selectedCameraId;
    int m_selectedWidth;
    int m_selectedHeight;
    int m_selectedFps;
};

#endif // CAMERASELECTIONDIALOG_H
