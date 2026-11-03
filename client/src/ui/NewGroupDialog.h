#pragma once

#include "services/GroupService.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace mini {

// S17 NewGroup: title, description, participant logins and the two
// visibility switches from §42.1.
class NewGroupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewGroupDialog(QWidget *parent = nullptr);

    void setDraft(const services::GroupDraft &draft);
    [[nodiscard]] services::GroupDraft draft() const;
    void setError(const QString &text);

    [[nodiscard]] QLineEdit *titleEdit() const { return m_title; }
    [[nodiscard]] QPlainTextEdit *aboutEdit() const { return m_about; }
    [[nodiscard]] QPlainTextEdit *membersEdit() const { return m_members; }
    [[nodiscard]] QCheckBox *publicCheck() const { return m_public; }
    [[nodiscard]] QCheckBox *requestCheck() const { return m_request; }

private:
    void submit();

    QLineEdit *m_title = nullptr;
    QPlainTextEdit *m_about = nullptr;
    QPlainTextEdit *m_members = nullptr;
    QCheckBox *m_public = nullptr;
    QCheckBox *m_request = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace mini
