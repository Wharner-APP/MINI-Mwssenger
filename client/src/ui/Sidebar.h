#pragma once

#include <QWidget>

class QLineEdit;
class QToolButton;
class QAction;

namespace mini {

class ChatList;

// 320 px chat list column: title, new chat, search and the virtualised list.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    [[nodiscard]] ChatList *chatList() const { return m_chatList; }
    [[nodiscard]] QLineEdit *searchField() const { return m_search; }
    [[nodiscard]] QToolButton *newChatButton() const { return m_newChat; }

signals:
    void chatSelected(const QString &chatId);
    void newChatRequested();

private:
    void refreshIcons();

    ChatList *m_chatList = nullptr;
    QLineEdit *m_search = nullptr;
    QToolButton *m_newChat = nullptr;
    QAction *m_searchAction = nullptr;
};

} // namespace mini
