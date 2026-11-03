#include "ui/Composer.h"

#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace mini {

namespace {

QToolButton *makeToolButton(const QString &objectName, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setFixedSize(36, 36);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

Composer::Composer(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("composerBar"));

    m_replyBar = new QFrame(this);
    m_replyBar->setObjectName(QStringLiteral("composerReplyBar"));
    m_replyBar->setFrameShape(QFrame::NoFrame);
    m_replyText = new QLabel(m_replyBar);
    m_replyText->setObjectName(QStringLiteral("composerReplyText"));
    m_replyText->setProperty("miniRole", QStringLiteral("caption"));
    m_replyText->setTextFormat(Qt::PlainText);
    m_replyClose = makeToolButton(QStringLiteral("composerReplyClose"), m_replyBar);
    m_replyClose->setFixedSize(24, 24);
    m_replyClose->setIconSize(QSize(14, 14));
    m_replyClose->setAccessibleName(tr("Cancel reply"));
    m_replyClose->setToolTip(tr("Cancel reply"));
    auto *replyLayout = new QHBoxLayout(m_replyBar);
    replyLayout->setContentsMargins(16, 4, 12, 0);
    replyLayout->setSpacing(8);
    replyLayout->addWidget(m_replyText, 1);
    replyLayout->addWidget(m_replyClose, 0);
    m_replyBar->setVisible(false);

    m_attach = makeToolButton(QStringLiteral("attachButton"), this);
    m_attach->setAccessibleName(tr("Attach a file"));
    m_attach->setEnabled(false);
    m_attach->setToolTip(tr("Attach a file — available in a later build"));

    m_editor = new QTextEdit(this);
    m_editor->setObjectName(QStringLiteral("composerInput"));
    m_editor->setAcceptRichText(false);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setPlaceholderText(tr("Write a message…"));
    m_editor->setAccessibleName(tr("Message"));
    m_editor->setMinimumHeight(m_minEditorHeight);
    m_editor->setMaximumHeight(m_maxEditorHeight);
    m_editor->installEventFilter(this);

    m_emoji = makeToolButton(QStringLiteral("emojiButton"), this);
    m_emoji->setToolTip(tr("Emoji — available in a later build"));
    m_emoji->setAccessibleName(tr("Emoji"));
    m_emoji->setEnabled(false);

    m_send = makeToolButton(QStringLiteral("sendButton"), this);
    m_send->setToolTip(tr("Send"));
    m_send->setAccessibleName(tr("Send"));
    m_send->setEnabled(false);

    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(12, 8, 12, 8);
    inputRow->setSpacing(8);
    inputRow->addWidget(m_attach, 0, Qt::AlignBottom);
    inputRow->addWidget(m_editor, 1);
    inputRow->addWidget(m_emoji, 0, Qt::AlignBottom);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_replyBar);
    layout->addLayout(inputRow);

    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(250);
    connect(&m_draftTimer, &QTimer::timeout, this,
            [this] { emit draftChanged(m_editor->toPlainText()); });
    connect(m_replyClose, &QToolButton::clicked, this, &Composer::clearReplyPreview);

    connect(m_editor, &QTextEdit::textChanged, this, [this] {
        updateSendState();
        updateHeight();
        if (!m_applyingText) {
            scheduleDraftSave();
        }
        emit typing();
    });
    connect(m_send, &QToolButton::clicked, this, &Composer::submit);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        const Tokens t = Theme::instance().tokens();
        m_attach->setIcon(Icons::icon(QStringLiteral("attach"), t.textSecondary, 20));
        m_emoji->setIcon(Icons::icon(QStringLiteral("emoji"), t.textSecondary, 20));
        m_send->setIcon(Icons::icon(QStringLiteral("send"), t.accent, 20));
        m_replyClose->setIcon(Icons::icon(QStringLiteral("close"), t.textTertiary, 14));
        updateHeight();
    });

    const Tokens t = Theme::instance().tokens();
    m_attach->setIcon(Icons::icon(QStringLiteral("attach"), t.textSecondary, 20));
    m_emoji->setIcon(Icons::icon(QStringLiteral("emoji"), t.textSecondary, 20));
    m_send->setIcon(Icons::icon(QStringLiteral("send"), t.accent, 20));
    m_replyClose->setIcon(Icons::icon(QStringLiteral("close"), t.textTertiary, 14));

    setComposerEnabled(false);
    updateHeight();
}

QString Composer::text() const
{
    return m_editor->toPlainText();
}

void Composer::setText(const QString &text)
{
    m_applyingText = true;
    m_editor->setPlainText(text);
    m_applyingText = false;
    updateSendState();
    updateHeight();
}

bool Composer::canSend() const
{
    const bool hasText = !m_editor->toPlainText().trimmed().isEmpty();
    return hasText && m_editor->isEnabled() && m_send->isEnabled();
}

void Composer::setComposerEnabled(bool enabled)
{
    m_editor->setEnabled(enabled);
    m_editor->setReadOnly(!enabled);
    m_attach->setEnabled(false);
    m_emoji->setEnabled(false);
    if (!enabled) {
        clearReplyPreview();
    }
    updateSendState();
}

void Composer::setReplyPreview(const QString &clientMsgId, const QString &text)
{
    m_replyTo = clientMsgId;
    const QString preview = text.isEmpty() ? clientMsgId : text;
    m_replyText->setText(tr("Replying to: %1").arg(preview));
    m_replyBar->setVisible(!clientMsgId.isEmpty());
}

void Composer::clearReplyPreview()
{
    m_replyTo.clear();
    m_replyText->clear();
    m_replyBar->setVisible(false);
}

bool Composer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !(key->modifiers() & Qt::ShiftModifier)) {
            submit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Composer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHeight();
}

void Composer::submit()
{
    if (!canSend()) {
        return;
    }
    const QString payload = m_editor->toPlainText().trimmed();
    if (payload.isEmpty()) {
        return;
    }
    m_applyingText = true;
    m_editor->clear();
    m_applyingText = false;
    updateSendState();
    updateHeight();
    emit draftChanged(QString());
    emit sendRequested(payload);
}

void Composer::updateSendState()
{
    const bool hasText = !m_editor->toPlainText().trimmed().isEmpty();
    m_send->setEnabled(hasText && m_editor->isEnabled());
}

void Composer::updateHeight()
{
    const int lineHeight = QFontMetrics(m_editor->font()).lineSpacing();
    const int minHeight = qMax(40, lineHeight + 20);
    const int maxHeight = qMax(minHeight, lineHeight * 8 + 20);
    m_minEditorHeight = minHeight;
    m_maxEditorHeight = maxHeight;

    const int width = m_editor->viewport()->width();
    if (width > 20) {
        m_editor->document()->setTextWidth(width);
    }
    const qreal documentHeight = m_editor->document()->size().height();
    const int desired = qBound(minHeight, qRound(documentHeight) + 20, maxHeight);
    m_editor->setFixedHeight(desired);
}

void Composer::scheduleDraftSave()
{
    m_draftTimer.start();
}

} // namespace mini
