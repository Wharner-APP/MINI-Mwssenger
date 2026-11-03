#include "ui/PollCreateDialog.h"

#include "services/PollService.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mini {

PollCreateDialog::PollCreateDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("pollCreateDialog"));
    setWindowTitle(tr("Create poll"));
    setModal(true);

    m_question = new QLineEdit(this);
    m_question->setObjectName(QStringLiteral("pollQuestion"));
    m_question->setMaxLength(kPollQuestionMax);
    m_question->setPlaceholderText(tr("Ask a question"));

    m_options = new QPlainTextEdit(this);
    m_options->setObjectName(QStringLiteral("pollOptions"));
    m_options->setPlaceholderText(tr("One option per line (2–10)"));
    m_options->setTabChangesFocus(true);
    m_options->setFixedHeight(120);

    m_anonymous = new QCheckBox(tr("Anonymous vote"), this);
    m_anonymous->setObjectName(QStringLiteral("pollAnonymous"));
    m_anonymous->setChecked(true);
    m_multiple = new QCheckBox(tr("Allow multiple answers"), this);
    m_multiple->setObjectName(QStringLiteral("pollMultiple"));
    m_quiz = new QCheckBox(tr("Quiz (choose the correct option)"), this);
    m_quiz->setObjectName(QStringLiteral("pollQuiz"));
    m_allowChange = new QCheckBox(tr("Voters can change their answer"), this);
    m_allowChange->setObjectName(QStringLiteral("pollAllowChange"));

    m_correct = new QSpinBox(this);
    m_correct->setObjectName(QStringLiteral("pollCorrect"));
    m_correct->setRange(1, kPollMaxOptions);
    m_correct->setEnabled(false);
    connect(m_quiz, &QCheckBox::toggled, m_correct, &QSpinBox::setEnabled);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("pollError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #d9534f;"));

    auto *form = new QFormLayout();
    form->addRow(tr("Question"), m_question);
    form->addRow(tr("Options"), m_options);
    form->addRow(QString(), m_anonymous);
    form->addRow(QString(), m_multiple);
    form->addRow(QString(), m_quiz);
    form->addRow(tr("Correct option"), m_correct);
    form->addRow(QString(), m_allowChange);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &PollCreateDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PollCreateDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addWidget(buttons);
    resize(440, 380);
}

PollBody PollCreateDialog::poll() const
{
    PollBody body;
    body.question = m_question->text();
    const QStringList lines =
        m_options->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) {
            body.options.append(line.trimmed());
        }
    }
    body.anonymous = m_anonymous->isChecked();
    body.multiple = m_multiple->isChecked();
    body.quiz = m_quiz->isChecked();
    body.allowChange = m_allowChange->isChecked();
    body.correctOption = body.quiz ? m_correct->value() - 1 : -1;
    return body;
}

void PollCreateDialog::setPoll(const PollBody &poll)
{
    m_question->setText(poll.question);
    m_options->setPlainText(poll.options.join(QLatin1Char('\n')));
    m_anonymous->setChecked(poll.anonymous);
    m_multiple->setChecked(poll.multiple);
    m_quiz->setChecked(poll.quiz);
    m_allowChange->setChecked(poll.allowChange);
    m_correct->setValue(qMax(1, poll.correctOption + 1));
}

void PollCreateDialog::accept()
{
    auto valid = services::PollService::validateDraft(poll());
    if (valid.isError()) {
        m_error->setText(valid.error());
        return;
    }
    m_error->clear();
    QDialog::accept();
}

} // namespace mini
