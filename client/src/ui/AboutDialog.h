#pragma once

#include <QDialog>

class QLabel;

namespace mini {

// "About the app": version, third-party licenses and the G03 disclaimer.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    [[nodiscard]] QLabel *versionLabel() const { return m_version; }
    [[nodiscard]] QLabel *licensesLabel() const { return m_licenses; }
    [[nodiscard]] QLabel *disclaimerLabel() const { return m_disclaimer; }

private:
    QLabel *m_version = nullptr;
    QLabel *m_licenses = nullptr;
    QLabel *m_disclaimer = nullptr;
};

} // namespace mini
