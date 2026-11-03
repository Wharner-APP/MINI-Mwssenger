#include "app/Application.h"

#include "app/ClientRuntime.h"
#include "app/Config.h"
#include "app/Paths.h"
#include "app/SingleInstance.h"
#include "core/BuildInfo.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/TlsPin.h"
#include "net/TunnelResolver.h"
#include "services/AuthService.h"
#include "ui/AuthDialog.h"
#include "ui/MainWindow.h"
#include "ui/SplashScreen.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QTranslator>

namespace mini {

Application::Application(QObject *parent) : QObject(parent) {}

Application::~Application() = default;

int Application::run(int argc, char **argv)
{
    m_app = std::make_unique<QApplication>(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MINI Messenger"));
    QApplication::setApplicationDisplayName(QStringLiteral("MINI Messenger"));
    QApplication::setOrganizationName(QStringLiteral("MINI Messenger"));
    QApplication::setApplicationVersion(BuildInfo::appVersion());
    QApplication::setStyle(QStringLiteral("Fusion"));

    Resources::initialize();
    Paths::initialize();
    BuildInfo::initializeCrypto();
    Log::init(Paths::logsDir());
    Log::info(QStringLiteral("app.start"),
              {{QStringLiteral("version"), BuildInfo::appVersion()},
               {QStringLiteral("qt"), BuildInfo::qtVersion()},
               {QStringLiteral("data_root"), Paths::dataDir()}});

    m_app->setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    // Settings must be available before the first translated string is shown.
    m_settings = std::make_unique<Settings>();
    loadTranslations();
    applyThemeFromSettings();

    m_singleInstance = std::make_unique<SingleInstance>();
    if (!m_singleInstance->tryLock()) {
        Log::warn(QStringLiteral("app.second_instance"));
        return 0;
    }
    connect(m_singleInstance.get(), &SingleInstance::secondInstanceStarted, this, [this] {
        if (m_mainWindow) {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        } else if (m_auth) {
            m_auth->show();
            m_auth->raise();
            m_auth->activateWindow();
        }
    });

    connect(m_app.get(), &QCoreApplication::aboutToQuit, this, [this] {
        if (m_mainWindow) {
            m_mainWindow->saveGeometryToSettings();
        }
        if (m_settings) {
            m_settings->flush();
        }
        Log::info(QStringLiteral("app.quit"));
    });

    setupTunnel();
    showWelcome();
    // Every launch refreshes the tunnel address from the published README;
    // resolve() has already supplied the cached/offline endpoint above.
    QTimer::singleShot(0, this, [this] {
        if (m_resolver) {
            m_resolver->fetchFromReadme();
        }
    });
    const int code = m_app->exec();
    Log::shutdown();
    return code;
}

void Application::applyThemeFromSettings()
{
    const ThemeMode mode = Theme::modeFromId(m_settings->get(QStringLiteral("ui/theme"),
                                                             QStringLiteral("dark")));
    const Theme::Accent accent =
        Theme::accentFromId(m_settings->get(QStringLiteral("ui/accent"), QStringLiteral("lime")));
    const int scale = m_settings->getInt(QStringLiteral("ui/font_scale"), 100);
    const Density density = Theme::densityFromId(
        m_settings->get(QStringLiteral("ui/density"), QStringLiteral("comfortable")));
    Theme::instance().apply(mode, accent, scale, density);
}

void Application::loadTranslations()
{
    // "ui.language" is the documented key; "ui/language" is accepted as the
    // native QSettings spelling used by the other UI settings.
    const QString configured =
        m_settings
            ? m_settings->get(QStringLiteral("ui.language"),
                              m_settings->get(QStringLiteral("ui/language"),
                                              QStringLiteral("ru")))
            : QStringLiteral("ru");
    const QString language =
        configured.trimmed().startsWith(QLatin1String("en"), Qt::CaseInsensitive)
            ? QStringLiteral("en")
            : QStringLiteral("ru");

    m_translator = std::make_unique<QTranslator>();
    const QString embedded = QStringLiteral(":/i18n/mini_%1.qm").arg(language);
    if (m_translator->load(embedded)) {
        m_app->installTranslator(m_translator.get());
        Log::info(QStringLiteral("i18n.loaded"),
                  {{QStringLiteral("source"), QStringLiteral("embedded")},
                   {QStringLiteral("language"), language}});
        return;
    }

    // The Russian catalog is also embedded under the legacy "mini.qm" name
    // for compatibility with older test targets.
    if (language == QLatin1String("ru")
        && m_translator->load(QStringLiteral(":/i18n/mini.qm"))) {
        m_app->installTranslator(m_translator.get());
        Log::info(QStringLiteral("i18n.loaded"),
                  {{QStringLiteral("source"), QStringLiteral("embedded-legacy")},
                   {QStringLiteral("language"), language}});
        return;
    }

    // Development fallback when the resource was not generated.
    const QString path = QDir(Paths::translationsDir())
                             .filePath(QStringLiteral("mini_%1.qm").arg(language));
    if (QFile::exists(path) && m_translator->load(path)) {
        m_app->installTranslator(m_translator.get());
        Log::info(QStringLiteral("i18n.loaded"),
                  {{QStringLiteral("source"), path},
                   {QStringLiteral("language"), language}});
        return;
    }

    if (language == QLatin1String("ru")) {
        const QString legacy =
            QDir(Paths::translationsDir()).filePath(QStringLiteral("mini.qm"));
        if (QFile::exists(legacy) && m_translator->load(legacy)) {
            m_app->installTranslator(m_translator.get());
            Log::info(QStringLiteral("i18n.loaded"),
                      {{QStringLiteral("source"), legacy},
                       {QStringLiteral("language"), language}});
            return;
        }
    }

    Log::warn(QStringLiteral("i18n.translation_missing"),
              {{QStringLiteral("language"), language},
               {QStringLiteral("path"), path}});
}

void Application::setupTunnel()
{
    m_network = new QNetworkAccessManager(this);
    m_resolver = new net::TunnelResolver(
        QDir(Paths::configDir()).filePath(QStringLiteral("connect.ip")),
        QDir(Paths::dataDir()).filePath(QStringLiteral("connect_cache.txt")),
        net::TunnelResolver::defaultReadmeUrl(), m_network, this);

    const Result<net::Endpoint> offline = m_resolver->resolve();
    if (m_resolver->needsManualEntry() || offline.isError()) {
        promptForEndpoint();
    } else if (m_resolver->source() == net::TunnelResolver::Source::Cache) {
        // The cached address is the best known one; make config follow it so
        // AuthDialog and the shell read the same endpoint.
        m_resolver->saveEndpoint(offline.value());
    }

    connect(m_resolver, &net::TunnelResolver::resolved, this,
            [this](const net::Endpoint &endpoint, const QString &origin) {
                Log::info(QStringLiteral("tunnel.resolved"),
                          {{QStringLiteral("endpoint"), endpoint.toString()},
                           {QStringLiteral("origin"), origin}});
                if (m_auth) {
                    m_auth->applyEndpoint(endpoint);
                }
            });
    connect(m_resolver, &net::TunnelResolver::fingerprintResolved, this,
            [this](const QString &fingerprint) {
                const QString path = QDir(Paths::configDir())
                                          .filePath(QStringLiteral("server_fingerprint.txt"));
                QFile file(path);
                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    file.write(fingerprint.toUtf8() + '\n');
                }
                if (m_auth) {
                    m_auth->applyFingerprint(fingerprint);
                }
            });
    connect(m_resolver, &net::TunnelResolver::failed, this, [](const QString &reason) {
        Log::warn(QStringLiteral("tunnel.readme_failed"),
                  {{QStringLiteral("code"), reason}});
    });
}

void Application::promptForEndpoint()
{
    QString initial = Config().serverEndpoint();
    for (int attempt = 0; attempt < 3; ++attempt) {
        bool accepted = false;
        const QString text = QInputDialog::getText(
            nullptr, tr("Server address"),
            tr("Enter host:port (for example bore.pub:12345)"),
            QLineEdit::Normal, initial, &accepted);
        if (!accepted) {
            return;
        }
        const Result<net::Endpoint> parsed = net::TunnelResolver::parseEndpoint(text);
        if (parsed.isOk() && m_resolver && m_resolver->saveEndpoint(parsed.value())) {
            return;
        }
        QMessageBox::warning(nullptr, tr("Server address"),
                             tr("That address is not valid. Use host:port."));
        initial = text;
    }
}

void Application::showWelcome()
{
    m_auth = std::make_unique<AuthDialog>();
    connect(m_auth.get(), &AuthDialog::registerRequested, this, &Application::openMainWindow);
    connect(m_auth.get(), &AuthDialog::loginRequested, this, &Application::openMainWindow);

    auto *splash = new SplashScreen();
    splash->show();
    m_app->processEvents();

    QTimer::singleShot(500, this, [this, splash] {
        splash->finish(m_auth.get());
        splash->deleteLater();
        m_auth->show();
        m_auth->raise();
        m_auth->activateWindow();
    });
}

void Application::openMainWindow()
{
    startRuntime();
    if (!m_mainWindow) {
        m_mainWindow = std::make_unique<MainWindow>(m_settings.get());
        connect(m_mainWindow.get(), &MainWindow::signOutRequested, this,
                &Application::signOut);
        Log::info(QStringLiteral("app.shell_opened"));
    }
    if (m_runtime) {
        m_mainWindow->setChatService(m_runtime->chatService());
        m_mainWindow->setContactService(m_runtime->contactService());
        m_mainWindow->setGroupService(m_runtime->groupService());
        m_mainWindow->setChannelService(m_runtime->channelService());
        m_mainWindow->setMessageService(m_runtime->messageService());
        m_mainWindow->setPollService(m_runtime->pollService());
        m_mainWindow->setFolderService(m_runtime->folderService());
        m_mainWindow->setMediaService(m_runtime->mediaService());
        m_mainWindow->setStickerService(m_runtime->stickerService());
        m_mainWindow->setSearchService(m_runtime->searchService());
        m_mainWindow->setBotService(m_runtime->botService());
        m_mainWindow->setPrivacyService(m_runtime->privacyService());
        m_mainWindow->setExportService(m_runtime->exportService());
        m_mainWindow->setImportService(m_runtime->importService());
        m_mainWindow->setStorageWorker(m_runtime->storage());
        m_mainWindow->setWsClient(m_runtime->ws());
    }
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
    if (m_auth) {
        m_auth->hide();
    }
}

void Application::signOut()
{
    if (m_mainWindow) {
        m_mainWindow->setChatService(nullptr);
        m_mainWindow->setContactService(nullptr);
        m_mainWindow->setGroupService(nullptr);
        m_mainWindow->setChannelService(nullptr);
        m_mainWindow->setMessageService(nullptr);
        m_mainWindow->setPollService(nullptr);
        m_mainWindow->setFolderService(nullptr);
        m_mainWindow->setMediaService(nullptr);
        m_mainWindow->setStickerService(nullptr);
        m_mainWindow->setSearchService(nullptr);
        m_mainWindow->setBotService(nullptr);
        m_mainWindow->setPrivacyService(nullptr);
        m_mainWindow->setExportService(nullptr);
        m_mainWindow->setImportService(nullptr);
        m_mainWindow->setStorageWorker(nullptr);
        m_mainWindow->setWsClient(nullptr);
        m_mainWindow->hide();
    }
    if (m_runtime) {
        m_runtime->stop();
        m_runtime.reset();
    }
    if (m_auth && m_auth->authService()) {
        m_auth->authService()->logout([](Result<bool> result) {
            if (result.isError()) {
                Log::warn(QStringLiteral("app.logout_failed"),
                          {{QStringLiteral("code"), result.error()}});
            }
        });
        m_auth->showWelcomePage();
    }
}

void Application::startRuntime()
{
    if (m_runtime || !m_auth || !m_auth->authService()) {
        return;
    }
    const services::AuthSession session = m_auth->authService()->session();
    if (session.accessToken.isEmpty()) {
        Log::warn(QStringLiteral("runtime.no_session"));
        return;
    }

    const Config config;
    const auto parsed = net::TunnelResolver::parseEndpoint(config.serverEndpoint());
    if (parsed.isError()) {
        Log::error(QStringLiteral("runtime.endpoint_invalid"),
                   {{QStringLiteral("endpoint"), config.serverEndpoint()}});
        return;
    }

    m_runtime = std::make_unique<ClientRuntime>();
    auto *pin = new net::TlsPin(config.serverFingerprint(), m_runtime.get());

    ClientRuntime::Config runtime;
    runtime.endpoint = parsed.value();
    runtime.pin = pin;
    runtime.dataDir = Paths::dataDir();
    runtime.accessToken = session.accessToken;
    runtime.refreshToken = session.refreshToken;
    runtime.deviceId = session.deviceId;
    runtime.selfLogin = session.login;
    runtime.selfUserId = session.userId;
    runtime.kenc = m_auth->authService()->fileKeys().kenc;
    runtime.kmac = m_auth->authService()->fileKeys().kmac;

    const Result<bool> started = m_runtime->start(runtime);
    if (started.isError()) {
        Log::error(QStringLiteral("runtime.start_failed"),
                   {{QStringLiteral("code"), started.error()}});
        m_runtime.reset();
        return;
    }
    connect(m_runtime.get(), &ClientRuntime::authenticationRequired, this, [this] {
        if (m_auth) {
            m_auth->show();
            m_auth->raise();
        }
        if (m_mainWindow) {
            m_mainWindow->hide();
        }
        // Tear down now-dead services; the next successful login builds a
        // fresh ClientRuntime with the new tokens.
        QTimer::singleShot(0, this, [this] {
            if (m_mainWindow) {
                m_mainWindow->setChatService(nullptr);
                m_mainWindow->setContactService(nullptr);
                m_mainWindow->setGroupService(nullptr);
        m_mainWindow->setChannelService(nullptr);
                m_mainWindow->setMessageService(nullptr);
        m_mainWindow->setPollService(nullptr);
        m_mainWindow->setFolderService(nullptr);
                m_mainWindow->setMediaService(nullptr);
                m_mainWindow->setSearchService(nullptr);
                m_mainWindow->setStorageWorker(nullptr);
                m_mainWindow->setWsClient(nullptr);
            }
            if (m_runtime) {
                m_runtime->stop();
                m_runtime.reset();
            }
        });
    });
}

} // namespace mini
