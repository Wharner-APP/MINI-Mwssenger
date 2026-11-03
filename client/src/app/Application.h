#pragma once

#include <QObject>

#include <memory>

class QApplication;
class QNetworkAccessManager;
class QTranslator;

namespace mini::net {
class TunnelResolver;
}

namespace mini {

class AuthDialog;
class ClientRuntime;
class MainWindow;
class Settings;
class SingleInstance;

// Owns the QApplication, startup order, the welcome screen and the shell.
class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    int run(int argc, char **argv);

private:
    void applyThemeFromSettings();
    void loadTranslations();
    void showWelcome();
    void openMainWindow();
    void startRuntime();
    void setupTunnel();
    void promptForEndpoint();
    void signOut();

    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<QTranslator> m_translator;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SingleInstance> m_singleInstance;
    std::unique_ptr<AuthDialog> m_auth;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<ClientRuntime> m_runtime;
    QNetworkAccessManager *m_network = nullptr;
    net::TunnelResolver *m_resolver = nullptr;
};

} // namespace mini
