#pragma once

#include <QDialog>

class QPlainTextEdit;
class QPushButton;

namespace mini {

// §40.10: one caption for the whole album, preview of the first item.
class AlbumSendDialog : public QDialog
{
    Q_OBJECT

public:
    AlbumSendDialog(int count, const QPixmap &preview, QWidget *parent = nullptr);

    [[nodiscard]] QString caption() const;
    [[nodiscard]] QPlainTextEdit *captionEdit() const { return m_caption; }
    [[nodiscard]] QPushButton *sendButton() const { return m_send; }

private:
    QPlainTextEdit *m_caption = nullptr;
    QPushButton *m_send = nullptr;
};

} // namespace mini
