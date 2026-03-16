#ifndef TEXTOVERLAYSETTINGSDIALOG_H
#define TEXTOVERLAYSETTINGSDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QFontComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>

class TextOverlaySettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        QString text = "GoLive Studio";
        QString font = "Arial";
        int size = 48;
        QColor color = Qt::white;
        bool isVisible = true;
        bool isConfigured = true;
    };

    explicit TextOverlaySettingsDialog(Settings *settings, QWidget *parent = nullptr) 
        : QDialog(parent), m_settings(settings) {
        setWindowTitle("Text Overlay Settings");
        setMinimumWidth(400);

        QFormLayout *layout = new QFormLayout(this);

        m_textEdit = new QTextEdit(this);
        m_textEdit->setPlainText(m_settings->text);
        m_textEdit->setFixedHeight(60);
        layout->addRow("Overlay Text:", m_textEdit);

        m_fontCombo = new QFontComboBox(this);
        m_fontCombo->setCurrentFont(QFont(m_settings->font));
        layout->addRow("Font:", m_fontCombo);

        m_sizeSpin = new QSpinBox(this);
        m_sizeSpin->setRange(8, 200);
        m_sizeSpin->setValue(m_settings->size);
        layout->addRow("Font Size:", m_sizeSpin);

        m_colorBtn = new QPushButton("Pick Color", this);
        updateColorButton();
        connect(m_colorBtn, &QPushButton::clicked, [this]() {
            QColor c = QColorDialog::getColor(m_settings->color, this);
            if (c.isValid()) {
                m_settings->color = c;
                updateColorButton();
            }
        });
        layout->addRow("Color:", m_colorBtn);

        QPushButton *saveBtn = new QPushButton("Apply Overlay", this);
        saveBtn->setFixedHeight(40);
        saveBtn->setStyleSheet("background-color: #9b59b6; color: white; border-radius: 4px; font-weight: bold;");
        connect(saveBtn, &QPushButton::clicked, this, &TextOverlaySettingsDialog::onSave);
        layout->addRow(saveBtn);
    }

private:
    void updateColorButton() {
        m_colorBtn->setStyleSheet(QString("background-color: %1; color: %2;")
            .arg(m_settings->color.name())
            .arg(m_settings->color.lightness() > 128 ? "black" : "white"));
    }

private slots:
    void onSave() {
        m_settings->text = m_textEdit->toPlainText();
        m_settings->font = m_fontCombo->currentFont().family();
        m_settings->size = m_sizeSpin->value();
        m_settings->isConfigured = !m_settings->text.isEmpty();
        accept();
    }

private:
    Settings *m_settings;
    QTextEdit *m_textEdit;
    QFontComboBox *m_fontCombo;
    QSpinBox *m_sizeSpin;
    QPushButton *m_colorBtn;
};

#endif // TEXTOVERLAYSETTINGSDIALOG_H
