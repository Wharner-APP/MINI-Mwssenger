#include "services/CaptchaService.h"

#include <QtTest>

using mini::services::CaptchaService;

class TestCaptchaService : public QObject
{
    Q_OBJECT

private slots:
    void providerDependsOnSiteKey();
    void pageContainsSiteKeyAndBridge();
    void pageEscapesUnsafeCharacters();
    void refusesPlainHttpWithoutPin();
};

void TestCaptchaService::providerDependsOnSiteKey()
{
    CaptchaService builtin(mini::net::Endpoint(QStringLiteral("http"),
                                               QStringLiteral("127.0.0.1"), 8765));
    QCOMPARE(builtin.provider(), CaptchaService::Provider::Builtin);
    QVERIFY(builtin.required());

    CaptchaService external(mini::net::Endpoint(QStringLiteral("http"),
                                                QStringLiteral("127.0.0.1"), 8765),
                            QStringLiteral("site-key"));
    QCOMPARE(external.provider(), CaptchaService::Provider::HCaptcha);
    QVERIFY(external.required());
}

void TestCaptchaService::pageContainsSiteKeyAndBridge()
{
    const QString html = CaptchaService::hcaptchaPageHtml(QStringLiteral("abc-123"));
    QVERIFY(html.contains(QStringLiteral("https://js.hcaptcha.com/1/api.js")));
    QVERIFY(html.contains(QStringLiteral("qrc:///qtwebchannel/qwebchannel.js")));
    QVERIFY(html.contains(QStringLiteral("abc-123")));
    QVERIFY(html.contains(QStringLiteral("bridge.onToken")));
    QVERIFY(html.contains(QStringLiteral("sitekey")));
}

void TestCaptchaService::pageEscapesUnsafeCharacters()
{
    const QString html = CaptchaService::hcaptchaPageHtml(QStringLiteral("a'b<c"));
    QVERIFY(html.contains(QStringLiteral("a\\'b\\u003cc")));
    QVERIFY(!html.contains(QStringLiteral("a'b<c")));
}

void TestCaptchaService::refusesPlainHttpWithoutPin()
{
    CaptchaService service(mini::net::Endpoint(QStringLiteral("http"),
                                               QStringLiteral("127.0.0.1"), 8765));
    bool done = false;
    QString error;
    service.requestBuiltin([&](mini::Result<mini::services::CaptchaChallenge> result) {
        done = true;
        error = result.error();
    });
    QVERIFY(done);
    QCOMPARE(error, QStringLiteral("tls_required"));
}

QTEST_GUILESS_MAIN(TestCaptchaService)
#include "test_captcha_service.moc"
