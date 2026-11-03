#pragma once

#include <QStringList>
#include <QTimer>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QFrame;
class QLabel;
class QStackedWidget;
class QTextEdit;
class QToolButton;

namespace mini {

namespace services {
class StickerService;
}

class EmojiPanel;
class GifPanel;
class StickerPanel;

// Message input. Enter sends, Shift+Enter inserts a newline, the field grows
// from one to eight lines. Disabled until a chat is selected.
class Composer : public QWidget
{
    Q_OBJECT

public:
    explicit Composer(QWidget *parent = nullptr);

    [[nodiscard]] QTextEdit *editor() const { return m_editor; }
    [[nodiscard]] QToolButton *sendButton() const { return m_send; }
    [[nodiscard]] QFrame *replyBar() const { return m_replyBar; }
    [[nodiscard]] QLabel *replyLabel() const { return m_replyText; }
    [[nodiscard]] QToolButton *replyCloseButton() const { return m_replyClose; }
    [[nodiscard]] QString text() const;
    void setText(const QString &text);
    [[nodiscard]] bool canSend() const;
    void setComposerEnabled(bool enabled);
    [[nodiscard]] QToolButton *attachButton() const { return m_attach; }
    [[nodiscard]] QToolButton *emojiButton() const { return m_emoji; }
    [[nodiscard]] QToolButton *stickerButton() const { return m_sticker; }
    [[nodiscard]] QToolButton *gifButton() const { return m_gif; }
    [[nodiscard]] QFrame *picker() const { return m_picker; }
    [[nodiscard]] EmojiPanel *emojiPanel() const { return m_emojiPanel; }
    [[nodiscard]] StickerPanel *stickerPanel() const { return m_stickerPanel; }
    [[nodiscard]] GifPanel *gifPanel() const { return m_gifPanel; }
    void setStickerService(services::StickerService *service);
    [[nodiscard]] services::StickerService *stickerService() const;
    void setRecentEmoji(const QStringList &characters);
    void showPicker(int page);
    void hidePicker();
    void insertEmoji(const QString &character);

    void setReplyPreview(const QString &clientMsgId, const QString &text);
    void clearReplyPreview();
    [[nodiscard]] bool hasReplyPreview() const { return !m_replyTo.isEmpty(); }
    [[nodiscard]] QString replyToClientMsgId() const { return m_replyTo; }

signals:
    void sendRequested(const QString &text);
    void typing();
    // Debounced; the view stores it as the per-chat draft.
    void draftChanged(const QString &text);
    void attachRequested();
    void filesDropped(const QStringList &paths);
    void stickerRequested(const QString &packId, const QString &stickerId);
    void gifRequested(const QString &gifId);
    void stickerInstallRequested();
    void stickerRemoveRequested(const QString &packId);
    void recentEmojiChanged(const QStringList &characters);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void submit();
    void updateSendState();
    void updateHeight();
    void scheduleDraftSave();
    void updatePickerButtons();

    QToolButton *m_attach = nullptr;
    QTextEdit *m_editor = nullptr;
    QToolButton *m_emoji = nullptr;
    QToolButton *m_sticker = nullptr;
    QToolButton *m_gif = nullptr;
    QToolButton *m_send = nullptr;
    QFrame *m_picker = nullptr;
    QStackedWidget *m_pages = nullptr;
    EmojiPanel *m_emojiPanel = nullptr;
    StickerPanel *m_stickerPanel = nullptr;
    GifPanel *m_gifPanel = nullptr;
    int m_pickerPage = 0;
    QFrame *m_replyBar = nullptr;
    QLabel *m_replyText = nullptr;
    QToolButton *m_replyClose = nullptr;
    QString m_replyTo;
    QTimer m_draftTimer;
    bool m_applyingText = false;
    int m_minEditorHeight = 40;
    int m_maxEditorHeight = 160;
};

} // namespace mini
