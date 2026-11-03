#pragma once

#include <QFrame>

class QToolButton;

namespace mini {

// 72 px navigation rail: chats, contacts, settings and profile.
class Rail : public QFrame
{
    Q_OBJECT

public:
    explicit Rail(QWidget *parent = nullptr);

    [[nodiscard]] QToolButton *chatsButton() const { return m_chats; }
    [[nodiscard]] QToolButton *contactsButton() const { return m_contacts; }
    [[nodiscard]] QToolButton *settingsButton() const { return m_settings; }
    [[nodiscard]] QToolButton *profileButton() const { return m_profile; }

signals:
    void chatsRequested();
    void contactsRequested();
    void settingsRequested();
    void profileRequested();

private:
    void refreshIcons();

    QToolButton *m_chats = nullptr;
    QToolButton *m_contacts = nullptr;
    QToolButton *m_settings = nullptr;
    QToolButton *m_profile = nullptr;
};

} // namespace mini
