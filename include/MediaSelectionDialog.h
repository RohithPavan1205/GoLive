#ifndef MEDIASELECTIONDIALOG_H
#define MEDIASELECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

class MediaSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit MediaSelectionDialog(QWidget *parent = nullptr);
    
    QString getFilePath() const;
    bool isLooping() const;

private slots:
    void onBrowseClicked();

private:
    void setupUI();
    
    QLineEdit *m_fileLineEdit;
    QCheckBox *m_loopCheckBox;
};

#endif // MEDIASELECTIONDIALOG_H
