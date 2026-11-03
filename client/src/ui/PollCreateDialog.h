#pragma once

#include "domain/Poll.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace mini {

// §38.3: question (≤255), 2–10 options (≤100), anonymous on by default,
// multiple answers, quiz with a correct option, changeable vote.
class PollCreateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PollCreateDialog(QWidget *parent = nullptr);

    [[nodiscard]] PollBody poll() const;
    void setPoll(const PollBody &poll);
    [[nodiscard]] QLabel *errorLabel() const { return m_error; }

private:
    void accept() override;

    QLineEdit *m_question = nullptr;
    QPlainTextEdit *m_options = nullptr;
    QCheckBox *m_anonymous = nullptr;
    QCheckBox *m_multiple = nullptr;
    QCheckBox *m_quiz = nullptr;
    QCheckBox *m_allowChange = nullptr;
    QSpinBox *m_correct = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace mini
