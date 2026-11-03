#include "ui/Composer.h"

#include "services/StickerService.h"
#include "ui/EmojiPanel.h"
#include "ui/GifPanel.h"
#include "ui/StickerPanel.h"
#include "ui/Theme.h"
#include "ui/widgets/Icons.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolButton>
#include <QUrl>
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
    m_attach->setToolTip(tr("Attach a file or drop it here"));

    m_editor = new QTextEdit(this);
    m_editor->setObjectName(QStringLiteral("composerInput"));
    m_editor->setAcceptRichText(false);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setPlaceholderText(tr("Write a message…"));
    m_editor->setAccessibleName(tr("Message"));
    m_editor->setMinimumHeight(m_minEditorHeight);
    m_editor->setMaximumHeight(m_maxEditorHeight);
    // Let the composer, not the text editor, handle dropped files.
    m_editor->setAcceptDrops(false);
    m_editor->installEventFilter(this);

    m_emoji = makeToolButton(QStringLiteral("emojiButton"), this);
    m_emoji->setToolTip(tr("Emoji"));
    m_emoji->setAccessibleName(tr("Emoji"));
    m_emoji->setCheckable(true);

    m_sticker = makeToolButton(QStringLiteral("stickerButton"), this);
    m_sticker->setToolTip(tr("Stickers"));
    m_sticker->setAccessibleName(tr("Stickers"));
    m_sticker->setCheckable(true);

    m_gif = makeToolButton(QStringLiteral("gifButton"), this);
    m_gif->setToolTip(tr("GIF"));
    m_gif->setAccessibleName(tr("GIF"));
    m_gif->setCheckable(true);

    m_poll = makeToolButton(QStringLiteral("pollButton"), this);
    m_poll->setToolTip(tr("Create a poll"));
    m_poll->setAccessibleName(tr("Create a poll"));

    m_send = makeToolButton(QStringLiteral("sendButton"), this);
    m_send->setToolTip(tr("Send"));
    m_send->setAccessibleName(tr("Send"));
    m_send->setEnabled(false);

    m_picker = new QFrame(this);
    m_picker->setObjectName(QStringLiteral("composerPicker"));
    m_picker->setFrameShape(QFrame::NoFrame);
    m_picker->setFixedHeight(292);
    m_picker->setVisible(false);
    m_pages = new QStackedWidget(m_picker);
    m_emojiPanel = new EmojiPanel(m_pages);
    m_stickerPanel = new StickerPanel(m_pages);
    m_gifPanel = new GifPanel(m_pages);
    m_pages->addWidget(m_emojiPanel);
    m_pages->addWidget(m_stickerPanel);
    m_pages->addWidget(m_gifPanel);
    auto *pickerLayout = new QVBoxLayout(m_picker);
    pickerLayout->setContentsMargins(8, 4, 8, 0);
    pickerLayout->addWidget(m_pages);

    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(12, 8, 12, 8);
    inputRow->setSpacing(6);
    inputRow->addWidget(m_attach, 0, Qt::AlignBottom);
    inputRow->addWidget(m_editor, 1);
    inputRow->addWidget(m_emoji, 0, Qt::AlignBottom);
    inputRow->addWidget(m_sticker, 0, Qt::AlignBottom);
    inputRow->addWidget(m_gif, 0, Qt::AlignBottom);
    inputRow->addWidget(m_poll, 0, Qt::AlignBottom);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_replyBar);
    layout->addWidget(m_picker);
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
    connect(m_attach, &QToolButton::clicked, this, &Composer::attachRequested);
    connect(m_emoji, &QToolButton::clicked, this, [this] { showPicker(0); });
    connect(m_sticker, &QToolButton::clicked, this, [this] { showPicker(1); });
    connect(m_gif, &QToolButton::clicked, this, [this] { showPicker(2); });
    connect(m_poll, &QToolButton::clicked, this, &Composer::pollRequested);
    connect(m_emojiPanel, &EmojiPanel::emojiSelected, this, &Composer::insertEmoji);
    connect(m_emojiPanel, &EmojiPanel::recentChanged, this,
            &Composer::recentEmojiChanged);
    connect(m_stickerPanel, &StickerPanel::stickerSelected, this,
            [this](const QString &packId, const QString &stickerId) {
                emit stickerRequested(packId, stickerId);
                hidePicker();
            });
    connect(m_stickerPanel, &StickerPanel::installRequested, this,
            &Composer::stickerInstallRequested);
    connect(m_stickerPanel, &StickerPanel::removeRequested, this,
            &Composer::stickerRemoveRequested);
    connect(m_gifPanel, &GifPanel::gifSelected, this, [this](const QString &gifId) {
        emit gifRequested(gifId);
        hidePicker();
    });
    setAcceptDrops(true);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        const Tokens t = Theme::instance().tokens();
        m_attach->setIcon(Icons::icon(QStringLiteral("attach"), t.textSecondary, 20));
        m_emoji->setIcon(Icons::icon(QStringLiteral("emoji"), t.textSecondary, 20));
        m_sticker->setIcon(Icons::icon(QStringLiteral("sticker"), t.textSecondary, 20));
        m_gif->setIcon(Icons::icon(QStringLiteral("gif"), t.textSecondary, 20));
        m_poll->setIcon(Icons::icon(QStringLiteral("checks"), t.textSecondary, 20));
        m_send->setIcon(Icons::icon(QStringLiteral("send"), t.accent, 20));
        m_replyClose->setIcon(Icons::icon(QStringLiteral("close"), t.textTertiary, 14));
        updateHeight();
    });

    const Tokens t = Theme::instance().tokens();
    m_attach->setIcon(Icons::icon(QStringLiteral("attach"), t.textSecondary, 20));
    m_emoji->setIcon(Icons::icon(QStringLiteral("emoji"), t.textSecondary, 20));
    m_sticker->setIcon(Icons::icon(QStringLiteral("sticker"), t.textSecondary, 20));
    m_gif->setIcon(Icons::icon(QStringLiteral("gif"), t.textSecondary, 20));
    m_poll->setIcon(Icons::icon(QStringLiteral("checks"), t.textSecondary, 20));
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
    m_attach->setEnabled(enabled);
    m_emoji->setEnabled(enabled);
    m_sticker->setEnabled(enabled);
    m_gif->setEnabled(enabled);
    m_poll->setEnabled(enabled);
    if (!enabled) {
        clearReplyPreview();
        hidePicker();
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
        if (key->key() == Qt::Key_Escape && m_picker->isVisible()) {
            hidePicker();
            return true;
        }
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

namespace {

QStringList droppedFiles(const QMimeData *mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls()) {
        return paths;
    }
    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) {
            paths.append(path);
        }
    }
    return paths;
}

} // namespace

void Composer::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppedFiles(event->mimeData()).isEmpty() && m_editor->isEnabled()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void Composer::dragMoveEvent(QDragMoveEvent *event)
{
    if (!droppedFiles(event->mimeData()).isEmpty() && m_editor->isEnabled()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

void Composer::dropEvent(QDropEvent *event)
{
    const QStringList files = droppedFiles(event->mimeData());
    if (!files.isEmpty() && m_editor->isEnabled()) {
        event->acceptProposedAction();
        emit filesDropped(files);
        return;
    }
    QWidget::dropEvent(event);
}

void Composer::scheduleDraftSave()
{
    m_draftTimer.start();
}

void Composer::setStickerService(services::StickerService *service)
{
    m_stickerPanel->setService(service);
}

services::StickerService *Composer::stickerService() const
{
    return m_stickerPanel->service();
}

void Composer::setRecentEmoji(const QStringList &characters)
{
    m_emojiPanel->setRecent(characters);
}

void Composer::showPicker(int page)
{
    if (!m_editor->isEnabled()) {
        return;
    }
    if (m_picker->isVisible() && m_pickerPage == page) {
        hidePicker();
        return;
    }
    m_pickerPage = page;
    m_pages->setCurrentIndex(page);
    if (page == 1) {
        m_stickerPanel->reload();
    } else if (page == 2) {
        m_gifPanel->reload();
    }
    m_picker->setVisible(true);
    updatePickerButtons();
}

void Composer::hidePicker()
{
    m_picker->setVisible(false);
    updatePickerButtons();
}

void Composer::updatePickerButtons()
{
    const bool visible = m_picker->isVisible();
    m_emoji->setChecked(visible && m_pickerPage == 0);
    m_sticker->setChecked(visible && m_pickerPage == 1);
    m_gif->setChecked(visible && m_pickerPage == 2);
}

void Composer::insertEmoji(const QString &character)
{
    if (character.isEmpty() || !m_editor->isEnabled()) {
        return;
    }
    QTextCursor cursor = m_editor->textCursor();
    cursor.insertText(character);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus(Qt::OtherFocusReason);
    updateSendState();
}

} // namespace mini
