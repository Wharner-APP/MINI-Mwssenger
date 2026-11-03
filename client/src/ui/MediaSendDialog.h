#pragma once

#include "services/MediaService.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace mini {

// §40.3 step 5 / §11.5 #78: preview before sending, caption up to 1024
// characters, and "send as file" for images that must not be recompressed.
class MediaSendDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MediaSendDialog(const services::MediaPreparation &prep,
                             const QPixmap &preview, QWidget *parent = nullptr);

    [[nodiscard]] QString caption() const;
    [[nodiscard]] bool sendAsFile() const;
    [[nodiscard]] QPushButton *sendButton() const { return m_send; }
    [[nodiscard]] QPlainTextEdit *captionEdit() const { return m_caption; }

private:
    QPlainTextEdit *m_caption = nullptr;
    QCheckBox *m_asFile = nullptr;
    QPushButton *m_send = nullptr;
};

} // namespace mini
