#pragma once

#include "services/BotService.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QListWidget;
class QPushButton;

namespace mini {

// S24 Bots (§33.12): the owner's bot list, a create button and the honest
// warning that bot chats are not end-to-end encrypted.
class BotsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BotsDialog(services::BotService *service, QWidget *parent = nullptr);

    void reload();

    [[nodiscard]] QListWidget *botList() const { return m_list; }
    [[nodiscard]] QPushButton *createButton() const { return m_create; }
    [[nodiscard]] QLabel *warningLabel() const { return m_warning; }
    [[nodiscard]] QLabel *statusLabel() const { return m_status; }
    [[nodiscard]] const QVector<services::BotInfo> &bots() const { return m_bots; }

signals:
    void chatRequested(const mini::services::BotInfo &bot);

private:
    void createBot();
    void openBot(const QString &botId);
    void showDetails(const QString &botId);
    [[nodiscard]] services::BotInfo botById(const QString &botId) const;

    services::BotService *m_service = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_create = nullptr;
    QLabel *m_warning = nullptr;
    QLabel *m_status = nullptr;
    QVector<services::BotInfo> m_bots;
};

} // namespace mini
