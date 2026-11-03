#pragma once

#include <QDialog>
#include <QObject>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
#ifdef MINI_HAVE_WEBENGINE
class QWebEngineView;
#endif

namespace mini::services {
class CaptchaService;
struct CaptchaChallenge;
} // namespace mini::services

namespace mini {

// Bridge exposed to the local hCaptcha page through QWebChannel.
class CaptchaBridge : public QObject
{
    Q_OBJECT

public:
    explicit CaptchaBridge(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void onToken(const QString &token);
    void onError(const QString &reason);

signals:
    void tokenReceived(const QString &token);
    void failed(const QString &reason);
};

// hCaptcha inside the app window (QWebEngineView + QWebChannel), with the
// built-in 6-symbol challenge as a fallback when the external widget cannot
// load. Emits tokenReady() with either the hCaptcha token or a builtin ticket.
class CaptchaDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CaptchaDialog(services::CaptchaService *service, QWidget *parent = nullptr);
    ~CaptchaDialog() override;

signals:
    void tokenReady(const QString &ticket);

private:
    void buildExternalPage();
    void buildBuiltinPage();
    void showBuiltin(const QString &reason = QString());
    void requestBuiltin();
    void submitBuiltin();
    void deliver(const QString &ticket);

    services::CaptchaService *m_service = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_externalPage = nullptr;
    QWidget *m_builtinPage = nullptr;
    QLabel *m_builtinImage = nullptr;
    QLineEdit *m_answer = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_submit = nullptr;
    QPushButton *m_fallback = nullptr;
    QString m_challengeId;
    CaptchaBridge *m_bridge = nullptr;
#ifdef MINI_HAVE_WEBENGINE
    QWebEngineView *m_view = nullptr;
#endif
};

} // namespace mini
