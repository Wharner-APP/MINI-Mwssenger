// Regression: CaptchaService keeps its callback after CaptchaDialog is gone.
// Closing the dialog while a challenge/verify request is in flight must not
// dereference the destroyed dialog.

#include "StubHttpServer.h"

#include "core/BuildInfo.h"
#include "net/TunnelResolver.h"
#include "services/CaptchaService.h"
#include "ui/CaptchaDialog.h"
#include "ui/Theme.h"

#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

using namespace mini;

namespace {

QByteArray jsonBody(const QByteArray &body)
{
    return body;
}

} // namespace

class TestCaptchaDialogLifetime : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void deletedDialogIgnoresPendingVerifyResult();
};

void TestCaptchaDialogLifetime::initTestCase()
{
    Resources::initialize();
    Theme::instance().apply(ThemeMode::Dark, Theme::Accent::Lime, 100,
                            Density::Comfortable);
}

void TestCaptchaDialogLifetime::deletedDialogIgnoresPendingVerifyResult()
{
    StubHttpServer server;
    QVERIFY(server.listen());
    server.setHandler([](const StubHttpServer::Request &request) {
        StubHttpServer::Response response;
        if (request.path.endsWith(QStringLiteral("/captcha/challenge"))) {
            response.body = jsonBody(
                R"({"challenge_id":"challenge-1","image_svg":)"
                R"("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'></svg>",)"
                R"("expires_in":120})");
            return response;
        }
        if (request.path.endsWith(QStringLiteral("/captcha/verify"))) {
            response.body = jsonBody(R"({"ticket":"ticket-1","expires_in":120})");
            // The caller is deleted before this arrives.
            response.delayMs = 400;
            return response;
        }
        response.status = 404;
        response.body = QByteArrayLiteral(R"({"error":{"code":"not_found"}})");
        return response;
    });

    const auto endpoint = net::TunnelResolver::parseEndpoint(server.baseUrl());
    QVERIFY(endpoint.isOk());
    services::CaptchaService service(endpoint.value());
    service.setAllowInsecureTransportForTesting(true);

    auto *dialog = new CaptchaDialog(&service);
    dialog->show();

    QLineEdit *answer = nullptr;
    QPushButton *check = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        answer = dialog->findChild<QLineEdit *>();
        const auto buttons = dialog->findChildren<QPushButton *>();
        for (QPushButton *button : buttons) {
            if (button->text() == QStringLiteral("Check")) {
                check = button;
            }
        }
        return answer != nullptr && check != nullptr && answer->isEnabled();
    }()), 5000);

    answer->setText(QStringLiteral("ABCDEF"));
    check->click();

    // The verify request is now in flight; destroy its caller immediately.
    delete dialog;
    dialog = nullptr;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }

    QVERIFY(server.requests().size() >= 2);
}

QTEST_MAIN(TestCaptchaDialogLifetime)
#include "test_captcha_dialog_lifetime.moc"
