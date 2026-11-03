#include "ui/MediaSendDialog.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mini {

MediaSendDialog::MediaSendDialog(const services::MediaPreparation &prep,
                                 const QPixmap &preview, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("mediaSendDialog"));
    setWindowTitle(tr("Send attachment"));

    auto *image = new QLabel(this);
    image->setObjectName(QStringLiteral("mediaSendPreview"));
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(240, 160);
    image->setMaximumHeight(320);
    if (!preview.isNull()) {
        image->setPixmap(preview.scaled(360, 320, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    } else {
        image->setText(tr("Preview unavailable"));
    }

    auto *name = new QLabel(prep.name, this);
    name->setObjectName(QStringLiteral("mediaSendName"));
    name->setWordWrap(true);
    auto *meta = new QLabel(
        QStringLiteral("%1 · %2").arg(prep.mime).arg(prep.size / 1024.0, 0, 'f', 1)
            + tr(" KB"), this);
    meta->setProperty("miniRole", QStringLiteral("caption"));

    m_caption = new QPlainTextEdit(this);
    m_caption->setObjectName(QStringLiteral("mediaSendCaption"));
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

    m_asFile = new QCheckBox(tr("Send as a file (no recompression)"), this);
    m_asFile->setObjectName(QStringLiteral("mediaSendAsFile"));
    // §40.3 step 5: every photo offers "send as a file" / "send as a photo".
    m_asFile->setVisible(prep.kind == MediaKind::Photo);

    auto *buttons = new QDialogButtonBox(this);
    m_send = buttons->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
    m_send->setObjectName(QStringLiteral("mediaSendAccept"));
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("mediaSendCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(image, 0, Qt::AlignHCenter);
    layout->addWidget(name);
    layout->addWidget(meta);
    layout->addWidget(m_caption);
    layout->addWidget(m_asFile);
    layout->addWidget(buttons);

    resize(420, 480);
}

QString MediaSendDialog::caption() const
{
    return m_caption->toPlainText().trimmed();
}

bool MediaSendDialog::sendAsFile() const
{
    return m_asFile->isVisible() && m_asFile->isChecked();
}

} // namespace mini
