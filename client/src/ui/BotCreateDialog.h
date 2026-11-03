#pragma once

#include "services/BotService.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace mini {

// S25 BotCreate (§33.13): name 3..32, lowercase login with the automatic
// `_bot` suffix, description up to 255 and the group/inline switches.
class BotCreateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BotCreateDialog(QWidget *parent = nullptr);

    [[nodiscard]] services::BotDraft draft() const;
    void setDraft(const services::BotDraft &draft);
    void setError(const QString &text);

    [[nodiscard]] QLineEdit *nameEdit() const { return m_name; }
    [[nodiscard]] QLineEdit *loginEdit() const { return m_login; }
    [[nodiscard]] QPlainTextEdit *aboutEdit() const { return m_about; }
    [[nodiscard]] QCheckBox *groupsCheck() const { return m_groups; }
    [[nodiscard]] QCheckBox *inlineCheck() const { return m_inline; }
    [[nodiscard]] QLabel *errorLabel() const { return m_error; }
    [[nodiscard]] QPushButton *createButton() const { return m_create; }

    // The final login the server will see, e.g. "helper_bot".
    [[nodiscard]] QString normalizedLogin() const;

private:
    void submit();

    QLineEdit *m_name = nullptr;
    QLineEdit *m_login = nullptr;
    QPlainTextEdit *m_about = nullptr;
    QCheckBox *m_groups = nullptr;
    QCheckBox *m_inline = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_create = nullptr;
};

} // namespace mini
