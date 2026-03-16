#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QCameraDevice>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const QList<QCameraDevice> &cameras, QWidget *parent = nullptr);
    int selectedIndex() const;

private:
    QComboBox *m_cameraCombo;
};

#endif // SETTINGSDIALOG_H
