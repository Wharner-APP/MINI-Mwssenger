#include "ui/CaptchaDialog.h"

#include "services/CaptchaService.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSvgRenderer>
#include <QStackedWidget>
#include <QVBoxLayout>

#ifdef MINI_HAVE_WEBENGINE
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

namespace mini {

void CaptchaBridge::onToken(const QString &token)
{
    emit tokenReceived(token);
}

void CaptchaBridge::onError(const QString &reason)
{
    emit failed(reason);
}

CaptchaDialog::CaptchaDialog(services::CaptchaService *service, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
{
    setObjectName(QStringLiteral("captchaDialog"));
    setWindowTitle(tr("Confirm you are human"));
    setModal(true);
    setMinimumWidth(420);

    m_pages = new QStackedWidget(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    layout->addWidget(m_pages);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    m_fallback = new QPushButton(tr("Use built-in check"), this);
    m_fallback->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(m_fallback);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_fallback, &QPushButton::clicked, this, [this] { showBuiltin(); });

    buildExternalPage();
    buildBuiltinPage();

    if (m_service && m_service->provider() == services::CaptchaService::Provider::HCaptcha
        && m_externalPage) {
        m_pages->setCurrentWidget(m_externalPage);
        m_fallback->setVisible(true);
    } else {
        m_pages->setCurrentWidget(m_builtinPage);
        m_fallback->setVisible(false);
        requestBuiltin();
    }
}

CaptchaDialog::~CaptchaDialog() = default;

void CaptchaDialog::buildExternalPage()
{
    m_externalPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_externalPage);
    layout->setContentsMargins(0, 0, 0, 0);

#ifdef MINI_HAVE_WEBENGINE
    if (!m_service
        || m_service->provider() != services::CaptchaService::Provider::HCaptcha) {
        m_externalPage->deleteLater();
        m_externalPage = nullptr;
        return;
    }

    m_bridge = new CaptchaBridge(this);
    connect(m_bridge, &CaptchaBridge::tokenReceived, this, &CaptchaDialog::deliver);
    connect(m_bridge, &CaptchaBridge::failed, this, [this](const QString &reason) {
        showBuiltin(reason);
    });

    m_view = new QWebEngineView(m_externalPage);
    m_view->setMinimumSize(340, 220);
    m_view->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    auto *channel = new QWebChannel(m_view->page());
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_view->page()->setWebChannel(channel);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            showBuiltin(QStringLiteral("load"));
        }
    });
    m_view->setHtml(services::CaptchaService::hcaptchaPageHtml(m_service->siteKey()),
                    QUrl(QStringLiteral("https://mini.local/captcha")));
    layout->addWidget(m_view, 1);
#else
    Q_UNUSED(m_service)
    m_externalPage->deleteLater();
    m_externalPage = nullptr;
#endif
}

void CaptchaDialog::buildBuiltinPage()
{
    m_builtinPage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_builtinPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Enter the six characters from the picture"), m_builtinPage);
    title->setProperty("miniRole", QStringLiteral("subtitle"));
    title->setWordWrap(true);

    m_builtinImage = new QLabel(m_builtinPage);
    m_builtinImage->setMinimumSize(240, 80);
    m_builtinImage->setAlignment(Qt::AlignCenter);
    m_builtinImage->setProperty("miniRole", QStringLiteral("captchaImage"));

    auto *row = new QHBoxLayout();
    m_answer = new QLineEdit(m_builtinPage);
    m_answer->setMaxLength(6);
    m_answer->setPlaceholderText(tr("Code"));
    m_answer->setMinimumHeight(36);
    m_submit = new QPushButton(tr("Check"), m_builtinPage);
    m_submit->setProperty("miniRole", QStringLiteral("accent"));
    m_submit->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_answer, 1);
    row->addWidget(m_submit);
    connect(m_submit, &QPushButton::clicked, this, &CaptchaDialog::submitBuiltin);
    connect(m_answer, &QLineEdit::returnPressed, this, &CaptchaDialog::submitBuiltin);

    m_status = new QLabel(m_builtinPage);
    m_status->setProperty("miniRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(m_builtinImage);
    layout->addLayout(row);
    layout->addWidget(m_status);
    layout->addStretch(1);

    m_pages->addWidget(m_builtinPage);
    if (m_externalPage) {
        m_pages->addWidget(m_externalPage);
    }
}

void CaptchaDialog::showBuiltin(const QString &reason)
{
    if (!m_builtinPage) {
        return;
    }
    if (!reason.isEmpty()) {
        m_status->setText(tr("The external check is unavailable. Use the built-in one."));
    }
    m_pages->setCurrentWidget(m_builtinPage);
    m_fallback->setVisible(false);
    if (m_challengeId.isEmpty()) {
        requestBuiltin();
    }
}

void CaptchaDialog::requestBuiltin()
{
    if (!m_service) {
        m_status->setText(tr("Captcha service is not configured."));
        return;
    }
    m_submit->setEnabled(false);
    m_answer->setEnabled(false);
    m_status->setText(tr("Loading…"));
    // The dialog can be closed (and deleted) while the request is in flight.
    // The service keeps the callback alive, so it must not touch a dead
    // dialog: guard every access with the object's lifetime.
    QPointer<CaptchaDialog> self(this);
    m_service->requestBuiltin([self](Result<services::CaptchaChallenge> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            self->m_status->setText(
                tr("Could not load the check: %1").arg(result.error()));
            self->m_submit->setEnabled(true);
            self->m_answer->setEnabled(true);
            return;
        }
        self->m_challengeId = result.value().challengeId;
        QSvgRenderer renderer(result.value().svg.toUtf8());
        QImage image(360, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        self->m_builtinImage->setPixmap(QPixmap::fromImage(image));
        self->m_submit->setEnabled(true);
        self->m_answer->setEnabled(true);
        self->m_answer->setFocus();
        self->m_status->clear();
    });
}

void CaptchaDialog::submitBuiltin()
{
    if (m_challengeId.isEmpty() || m_answer->text().trimmed().isEmpty()) {
        return;
    }
    m_submit->setEnabled(false);
    m_answer->setEnabled(false);
    m_status->setText(tr("Checking…"));
    QPointer<CaptchaDialog> self(this);
    m_service->verifyBuiltin(m_challengeId, m_answer->text().trimmed(),
                             [self](Result<QString> result) {
        if (!self) {
            return;
        }
        if (result.isError()) {
            self->m_status->setText(tr("Wrong or expired code. Try again."));
            self->m_answer->clear();
            self->m_submit->setEnabled(true);
            self->m_answer->setEnabled(true);
            self->m_answer->setFocus();
            self->requestBuiltin();
            return;
        }
        self->deliver(result.value());
    });
}

void CaptchaDialog::deliver(const QString &ticket)
{
    if (ticket.isEmpty()) {
        return;
    }
    emit tokenReady(ticket);
    accept();
}

} // namespace mini
