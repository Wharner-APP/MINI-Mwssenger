#pragma once

#include "domain/Chat.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QLineEdit;
class QListWidget;

namespace mini {

// §37.2: pick one or more destination chats for a forwarded message.
class ForwardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ForwardDialog(QWidget *parent = nullptr);

    void setChats(const QVector<Chat> &chats);
    [[nodiscard]] QStringList selectedChatIds() const;
    [[nodiscard]] QLineEdit *searchField() const { return m_search; }
    [[nodiscard]] QListWidget *chatList() const { return m_list; }

private:
    void rebuild();

    QVector<Chat> m_chats;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace mini
