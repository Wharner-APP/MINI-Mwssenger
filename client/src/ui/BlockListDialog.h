#pragma once

#include "services/PrivacyService.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace mini {

// Blacklist editor (§11.12 #181): block by exact login, see who is blocked and
// unblock. Blocking is enforced by the server for messages and calls.
class BlockListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BlockListDialog(services::PrivacyService *service, QWidget *parent = nullptr);

    void reload();

    [[nodiscard]] QListWidget *blockList() const { return m_list; }
    [[nodiscard]] QLineEdit *loginEdit() const { return m_login; }
    [[nodiscard]] QPushButton *blockButton() const { return m_block; }
    [[nodiscard]] QPushButton *unblockButton() const { return m_unblock; }
    [[nodiscard]] QLabel *statusLabel() const { return m_status; }
    [[nodiscard]] const QVector<services::BlockedUser> &blocked() const { return m_blocked; }

private:
    void block();
    void unblockSelected();

    services::PrivacyService *m_service = nullptr;
    QListWidget *m_list = nullptr;
    QLineEdit *m_login = nullptr;
    QPushButton *m_block = nullptr;
    QPushButton *m_unblock = nullptr;
    QLabel *m_status = nullptr;
    QVector<services::BlockedUser> m_blocked;
};

} // namespace mini
