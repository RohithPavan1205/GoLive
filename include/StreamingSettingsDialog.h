#ifndef STREAMINGSETTINGSDIALOG_H
#define STREAMINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QString>

class StreamingSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        QString service = "YouTube";
        QString server;
        QString streamKey;
        bool isConfigured = false;
    };

    explicit StreamingSettingsDialog(const QString &title, Settings *settings, QWidget *parent = nullptr) 
        : QDialog(parent), m_settings(settings) {
        setWindowTitle(title);
        setMinimumWidth(400);

        QFormLayout *layout = new QFormLayout(this);

        m_serviceCombo = new QComboBox(this);
        m_serviceCombo->addItems({"YouTube", "Twitch", "Facebook Live", "Custom RTMP"});
        m_serviceCombo->setCurrentText(m_settings->service);
        layout->addRow("Service:", m_serviceCombo);

        m_serverEdit = new QLineEdit(this);
        m_serverEdit->setPlaceholderText("rtmp://a.rtmp.youtube.com/live2");
        m_serverEdit->setText(m_settings->server);
        layout->addRow("Server URL:", m_serverEdit);

        m_keyEdit = new QLineEdit(this);
        m_keyEdit->setEchoMode(QLineEdit::Password);
        m_keyEdit->setPlaceholderText("Enter your stream key here");
        m_keyEdit->setText(m_settings->streamKey);
        layout->addRow("Stream Key:", m_keyEdit);

        QPushButton *saveBtn = new QPushButton("Apply Stream Settings", this);
        saveBtn->setFixedHeight(40);
        saveBtn->setStyleSheet("background-color: #3498db; color: white; border-radius: 4px; font-weight: bold;");
        connect(saveBtn, &QPushButton::clicked, this, &StreamingSettingsDialog::onSave);
        layout->addRow(saveBtn);

        connect(m_serviceCombo, &QComboBox::currentIndexChanged, [this](int index) {
            if (m_serviceCombo->currentText() == "YouTube") m_serverEdit->setText("rtmp://a.rtmp.youtube.com/live2");
            else if (m_serviceCombo->currentText() == "Twitch") m_serverEdit->setText("rtmp://ingest-server.twitch.tv/app");
        });
    }

private slots:
    void onSave() {
        m_settings->service = m_serviceCombo->currentText();
        m_settings->server = m_serverEdit->text();
        m_settings->streamKey = m_keyEdit->text();
        m_settings->isConfigured = !m_serverEdit->text().isEmpty() && !m_keyEdit->text().isEmpty();
        accept();
    }

private:
    Settings *m_settings;
    QComboBox *m_serviceCombo;
    QLineEdit *m_serverEdit;
    QLineEdit *m_keyEdit;
};

#endif // STREAMINGSETTINGSDIALOG_H
