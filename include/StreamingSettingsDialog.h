#ifndef STREAMINGSETTINGSDIALOG_H
#define STREAMINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QString>
#include <QList>

#include <QScreen>
#include <QGuiApplication>

class StreamingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        QString service = "YouTube";
        QString server;
        QString streamKey;
        QString monitorId;
        bool isExternalMonitor = false;
        bool isConfigured = false;
    };

    explicit StreamingSettingsDialog(const QString &title, Settings *settings, QWidget *parent = nullptr) 
        : QDialog(parent), m_settings(settings) {
        setWindowTitle(title);
        setMinimumWidth(400);

        QFormLayout *layout = new QFormLayout(this);

        m_serviceCombo = new QComboBox(this);
        m_serviceCombo->addItems({"YouTube", "Twitch", "Facebook Live", "External Monitor", "Custom RTMP"});
        m_serviceCombo->setCurrentText(m_settings->service);
        layout->addRow("Service:", m_serviceCombo);

        // RTMP Fields
        m_serverLabel = new QLabel("Server URL:", this);
        m_serverEdit = new QLineEdit(this);
        m_serverEdit->setPlaceholderText("rtmp://a.rtmp.youtube.com/live2");
        m_serverEdit->setText(m_settings->server);
        layout->addRow(m_serverLabel, m_serverEdit);

        m_keyLabel = new QLabel("Stream Key:", this);
        m_keyEdit = new QLineEdit(this);
        m_keyEdit->setEchoMode(QLineEdit::Password);
        m_keyEdit->setPlaceholderText("Enter your stream key here");
        m_keyEdit->setText(m_settings->streamKey);
        layout->addRow(m_keyLabel, m_keyEdit);

        // Monitor Selection Fields
        m_monitorLabel = new QLabel("Select Monitor:", this);
        m_monitorCombo = new QComboBox(this);
        QList<QScreen*> screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            m_monitorCombo->addItem(QString("Monitor %1: %2").arg(i+1).arg(screens[i]->name()), screens[i]->name());
        }
        if (!m_settings->monitorId.isEmpty()) {
            int idx = m_monitorCombo->findData(m_settings->monitorId);
            if (idx != -1) m_monitorCombo->setCurrentIndex(idx);
        }
        layout->addRow(m_monitorLabel, m_monitorCombo);

        QPushButton *saveBtn = new QPushButton("Apply Stream Settings", this);
        saveBtn->setFixedHeight(40);
        saveBtn->setStyleSheet("background-color: #3498db; color: white; border-radius: 4px; font-weight: bold;");
        connect(saveBtn, &QPushButton::clicked, this, &StreamingSettingsDialog::onSave);
        layout->addRow(saveBtn);

        connect(m_serviceCombo, &QComboBox::currentTextChanged, this, &StreamingSettingsDialog::updateVisibility);
        updateVisibility(m_serviceCombo->currentText());
    }

private slots:
    void updateVisibility(const QString &text) {
        bool isExternal = (text == "External Monitor");
        m_serverLabel->setVisible(!isExternal);
        m_serverEdit->setVisible(!isExternal);
        m_keyLabel->setVisible(!isExternal);
        m_keyEdit->setVisible(!isExternal);
        
        m_monitorLabel->setVisible(isExternal);
        m_monitorCombo->setVisible(isExternal);
    }

    void onSave() {
        m_settings->service = m_serviceCombo->currentText();
        m_settings->isExternalMonitor = (m_settings->service == "External Monitor");
        
        if (m_settings->isExternalMonitor) {
            m_settings->monitorId = m_monitorCombo->currentData().toString();
            m_settings->isConfigured = !m_settings->monitorId.isEmpty();
        } else {
            m_settings->server = m_serverEdit->text();
            m_settings->streamKey = m_keyEdit->text();
            m_settings->isConfigured = !m_serverEdit->text().isEmpty() && !m_keyEdit->text().isEmpty();
        }
        accept();
    }

private:
    Settings *m_settings;
    QComboBox *m_serviceCombo;
    QLabel *m_serverLabel;
    QLineEdit *m_serverEdit;
    QLabel *m_keyLabel;
    QLineEdit *m_keyEdit;
    QLabel *m_monitorLabel;
    QComboBox *m_monitorCombo;
};

#endif // STREAMINGSETTINGSDIALOG_H
