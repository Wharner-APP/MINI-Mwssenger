#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace mini {

// Loading/Empty/Error states all use this block: an icon, a title, one line of
// explanation and an optional action.
class EmptyState : public QWidget
{
    Q_OBJECT

public:
    explicit EmptyState(QWidget *parent = nullptr);

    void setIconName(const QString &name);
    void setTitle(const QString &title);
    void setDescription(const QString &description);
    void setActionText(const QString &text);

    [[nodiscard]] QPushButton *actionButton() const { return m_action; }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString description() const;

signals:
    void actionTriggered();

private:
    void refreshIcon();

    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_description = nullptr;
    QPushButton *m_action = nullptr;
    QString m_iconName;
};

} // namespace mini
