#pragma once

#include "domain/ChatFolder.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace mini {

// §48: title, include/exclude types, name substring, unread and mute rules.
class FolderEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FolderEditDialog(QWidget *parent = nullptr);

    [[nodiscard]] ChatFolder folder() const;
    void setFolder(const ChatFolder &folder);
    [[nodiscard]] QLabel *errorLabel() const { return m_error; }

private:
    void accept() override;

    QLineEdit *m_title = nullptr;
    QCheckBox *m_direct = nullptr;
    QCheckBox *m_group = nullptr;
    QCheckBox *m_channel = nullptr;
    QCheckBox *m_bot = nullptr;
    QCheckBox *m_excludeDirect = nullptr;
    QCheckBox *m_excludeGroup = nullptr;
    QCheckBox *m_excludeChannel = nullptr;
    QLineEdit *m_name = nullptr;
    QCheckBox *m_onlyUnread = nullptr;
    QCheckBox *m_onlyMuted = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace mini
