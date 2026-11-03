#pragma once

#include "services/ChannelService.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace mini {

// S18 NewChannel: title, description and the public/private switch (§43.1).
class NewChannelDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewChannelDialog(QWidget *parent = nullptr);

    void setDraft(const services::ChannelDraft &draft);
    [[nodiscard]] services::ChannelDraft draft() const;
    void setError(const QString &text);

    [[nodiscard]] QLineEdit *titleEdit() const { return m_title; }
    [[nodiscard]] QPlainTextEdit *aboutEdit() const { return m_about; }
    [[nodiscard]] QCheckBox *publicCheck() const { return m_public; }

private:
    void submit();

    QLineEdit *m_title = nullptr;
    QPlainTextEdit *m_about = nullptr;
    QCheckBox *m_public = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace mini
