#pragma once

#include "services/BotService.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace mini {

// One-time token screen (§33.13, §44.1). The token exists only in this
// widget; closing the dialog destroys it and there is no way to show it again.
class BotTokenDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BotTokenDialog(const services::BotToken &token, QWidget *parent = nullptr);

    [[nodiscard]] QLineEdit *tokenField() const { return m_token; }
    [[nodiscard]] QLabel *warningLabel() const { return m_warning; }
    [[nodiscard]] QPushButton *copyButton() const { return m_copy; }
    [[nodiscard]] QString token() const;

private:
    QLineEdit *m_token = nullptr;
    QLabel *m_warning = nullptr;
    QPushButton *m_copy = nullptr;
};

} // namespace mini
