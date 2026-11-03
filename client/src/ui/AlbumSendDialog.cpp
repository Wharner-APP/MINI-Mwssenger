#include "ui/AlbumSendDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

AlbumSendDialog::AlbumSendDialog(int count, const QPixmap &preview, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("albumSendDialog"));
    setWindowTitle(tr("Send album (%1)").arg(count));

    auto *image = new QLabel(this);
    image->setObjectName(QStringLiteral("albumSendPreview"));
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(240, 160);
    image->setMaximumHeight(280);
    if (!preview.isNull()) {
        image->setPixmap(preview.scaled(320, 280, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    } else {
        image->setText(tr("Preview unavailable"));
    }

    auto *summary = new QLabel(tr("%1 items in one message").arg(count), this);
    summary->setObjectName(QStringLiteral("albumSendSummary"));
    summary->setProperty("miniRole", QStringLiteral("subtitle"));

    m_caption = new QPlainTextEdit(this);
    m_caption->setObjectName(QStringLiteral("albumSendCaption"));
    m_caption->setPlaceholderText(tr("Caption (up to 1024 characters)"));
    m_caption->setMaximumHeight(90);
    connect(m_caption, &QPlainTextEdit::textChanged, this, [this] {
        const QString text = m_caption->toPlainText();
        if (text.size() > 1024) {
            QTextCursor cursor = m_caption->textCursor();
            cursor.movePosition(QTextCursor::End);
            m_caption->setPlainText(text.left(1024));
            cursor.movePosition(QTextCursor::End);
            m_caption->setTextCursor(cursor);
        }
    });

    auto *buttons = new QDialogButtonBox(this);
    m_send = buttons->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
    m_send->setObjectName(QStringLiteral("albumSendAccept"));
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("albumSendCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(image, 0, Qt::AlignHCenter);
    layout->addWidget(summary);
    layout->addWidget(m_caption);
    layout->addWidget(buttons);

    resize(420, 430);
}

QString AlbumSendDialog::caption() const
{
    return m_caption->toPlainText().trimmed();
}

} // namespace mini
