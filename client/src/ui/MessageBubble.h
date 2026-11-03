#pragma once

#include "domain/Message.h"
#include "domain/Poll.h"
#include "domain/Reaction.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QSet>
#include <QStyledItemDelegate>
#include <QVariantList>
#include <QVector>

namespace mini {

namespace services {
class MediaService;
class StickerService;
}

// Virtualised history for ChatView: one row per message, painted by
// MessageBubbleDelegate. No widget is created per message.
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ClientMsgIdRole = Qt::UserRole + 1,
        ChatIdRole,
        BodyRole,
        TimeRole,
        OutgoingRole,
        StateRole,
        ReplyRole,
        ReplyTextRole,
        SenderRole,
        KindRole,
        MediaIdRole,
        ThumbRole,
        UploadPercentRole,
        MediaNameRole,
        MediaSizeRole,
        DurationRole,
        StickerPixmapRole,
        StickerPackRole,
        StickerIdRole,
        GifIdRole,
        GifFrameRole,
        AlbumIdsRole,
        AlbumThumbsRole,
        EditedRole,
        ForwardedRole,
        SearchMatchRole,
        SearchQueryRole,
        // §38.1
        ReactionsRole,
        // §38.3
        PollIdRole,
        PollQuestionRole,
        PollOptionsRole,
        PollCountsRole,
        PollMyChoicesRole,
        PollFlagsRole,
        PollCorrectRole,
        PollTotalRole,
        // §38.4 / §39
        SystemRole,
    };

    explicit MessageListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    void setMessages(const QVector<Message> &messages);
    void appendMessage(const Message &message);
    void updateMessage(const Message &message);
    void removeMessage(const QString &clientMsgId);

    [[nodiscard]] Message messageAt(int row) const;
    [[nodiscard]] int indexOf(const QString &clientMsgId) const;
    [[nodiscard]] int count() const { return m_messages.size(); }

    // Short quote shown above a message that answers another one. The view
    // fills it from the history it already holds.
    void setReplyPreview(const QString &clientMsgId, const QString &text);

    // Thumbnails and upload progress for media bubbles. The service is not
    // owned: it outlives the model by the lifetime of the window.
    void setMediaService(services::MediaService *media);
    [[nodiscard]] services::MediaService *mediaService() const { return m_media; }
    void setStickerService(services::StickerService *stickers);
    [[nodiscard]] services::StickerService *stickerService() const { return m_stickers; }
    void setUploadProgress(const QString &mediaId, int percent);
    void refreshMedia(const QString &mediaId);
    // §47.2: rows whose body matches the in-chat query, and the query itself
    // so the delegate can highlight the matching characters.
    void setSearchMatches(const QSet<QString> &clientMsgIds, const QString &query);
    void clearSearchMatches();
    [[nodiscard]] bool isSearchMatch(const QString &clientMsgId) const;
    // §37.1/47.2: the message a global search or a pin jumped to.
    void setHighlighted(const QString &clientMsgId);
    [[nodiscard]] QString highlighted() const { return m_highlighted; }

    // §38.1: chips under a bubble grouped by emoji with a count and a flag
    // for "mine". The model keeps them by message id.
    void setSelfLogin(const QString &login) { m_selfLogin = login; }
    void setReactions(const QString &chatId, const QVector<Reaction> &reactions);
    // §38.3: tally for a poll card.
    void setPollResult(const PollResult &result);
    void clearPolls();

private:
    void insertSorted(const Message &message);
    [[nodiscard]] static qint64 effectiveTs(const Message &message);
    void updateGifPlayback();
    void refreshGifFrames();

    QVector<Message> m_messages;
    QHash<QString, QString> m_replyPreviews;
    services::MediaService *m_media = nullptr;
    services::StickerService *m_stickers = nullptr;
    QHash<QString, int> m_uploadPercents;
    bool m_gifConnected = false;
    QSet<QString> m_searchMatches;
    QString m_searchQuery;
    QString m_highlighted;
    QHash<QString, QVariantList> m_reactions; // message id -> chip list
    QHash<QString, PollResult> m_polls;       // poll id -> tally
    QString m_selfLogin;
};

class MessageBubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageBubbleDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
};

// §38.1 / §38.3: hit testing shared by the delegate and ChatView so a click
// on a chip or a poll option does not depend on widget coordinates.
[[nodiscard]] bool reactionChipAt(const QStyleOptionViewItem &option,
                                  const QModelIndex &index, const QPoint &pos,
                                  QString *emoji);
[[nodiscard]] bool pollOptionAt(const QStyleOptionViewItem &option,
                                const QModelIndex &index, const QPoint &pos,
                                int *optionIndex);
[[nodiscard]] bool pollCloseAt(const QStyleOptionViewItem &option,
                               const QModelIndex &index, const QPoint &pos);

} // namespace mini
