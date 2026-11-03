#pragma once

#include <QTimer>
#include <QWidget>

class QFrame;
class QLabel;
class QTextEdit;
class QToolButton;

namespace mini {

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

    void setReplyPreview(const QString &clientMsgId, const QString &text);
    void clearReplyPreview();
    [[nodiscard]] bool hasReplyPreview() const { return !m_replyTo.isEmpty(); }
    [[nodiscard]] QString replyToClientMsgId() const { return m_replyTo; }

signals:
    void sendRequested(const QString &text);
    void typing();
    // Debounced; the view stores it as the per-chat draft.
    void draftChanged(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void submit();
    void updateSendState();
    void updateHeight();
    void scheduleDraftSave();

    QToolButton *m_attach = nullptr;
    QTextEdit *m_editor = nullptr;
    QToolButton *m_emoji = nullptr;
    QToolButton *m_send = nullptr;
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
