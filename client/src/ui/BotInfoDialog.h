#pragma once

#include "services/BotService.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace mini {

// S26 BotInfo: the bot's profile, its token prefix and the owner actions.
// The full token is never requested here; "Rotate" issues a new one and the
// parent shows it once.
class BotInfoDialog : public QDialog
{
    Q_OBJECT

public:
    BotInfoDialog(services::BotService *service, const services::BotInfo &bot,
                  QWidget *parent = nullptr);

    [[nodiscard]] QString botId() const { return m_bot.botId; }
    [[nodiscard]] QLabel *nameLabel() const { return m_name; }
    [[nodiscard]] QLabel *loginLabel() const { return m_login; }
    [[nodiscard]] QLabel *aboutLabel() const { return m_about; }
    [[nodiscard]] QLabel *prefixLabel() const { return m_prefix; }
    [[nodiscard]] QPushButton *rotateButton() const { return m_rotate; }
    [[nodiscard]] QPushButton *deleteButton() const { return m_delete; }

signals:
    void tokenIssued(const mini::services::BotToken &token);
    void botDeleted(const QString &botId);

private:
    void rotate();
    void remove();

    services::BotService *m_service = nullptr;
    services::BotInfo m_bot;
    QLabel *m_name = nullptr;
    QLabel *m_login = nullptr;
    QLabel *m_about = nullptr;
    QLabel *m_prefix = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_rotate = nullptr;
    QPushButton *m_delete = nullptr;
};

} // namespace mini
