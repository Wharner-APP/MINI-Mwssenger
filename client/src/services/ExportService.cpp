#include "services/ExportService.h"

#include "services/BackupFormat.h"
#include "services/MediaCache.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace mini::services {
namespace {

template <typename T>
void watch(QObject *owner, QFuture<T> future, std::function<void(T)> done)
{
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
                         T value = watcher->result();
                         watcher->deleteLater();
                         done(std::move(value));
                     });
    watcher->setFuture(future);
}

bool cancelled(const std::shared_ptr<std::atomic<bool>> &flag)
{
    return flag && flag->load();
}

} // namespace

ExportService::ExportService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void ExportService::setKMac(const QByteArray &kMac)
{
    m_kMac = kMac;
}

void ExportService::setUserId(const QString &userId)
{
    m_userId = userId;
}

void ExportService::setMediaRoot(const QString &root)
{
    m_mediaRoot = root;
}

void ExportService::cancel()
{
    m_cancelRequested.store(true);
    if (m_cancel) {
        m_cancel->store(true);
    }
}

void ExportService::exportBackup(const ExportOptions &options, Done done)
{
    if (!m_storage) {
        done(Result<ExportSummary>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    if (options.outputPath.isEmpty()) {
        done(Result<ExportSummary>::error(QStringLiteral("backup.path_missing")));
        return;
    }
    if (m_kMac.isEmpty()) {
        done(Result<ExportSummary>::error(QStringLiteral("backup.key_missing")));
        return;
    }

    m_cancel = std::make_shared<std::atomic<bool>>(m_cancelRequested.exchange(false));
    const auto cancelFlag = m_cancel;
    const QByteArray exportKey = backup::deriveExportKey(m_kMac);
    const QString userId = m_userId;
    const QString mediaRoot = m_mediaRoot;
    const ExportOptions opts = options;

    ExportSummary summary;
    summary.path = options.outputPath;

    auto future = m_storage->submit([this, opts, exportKey, userId, mediaRoot, cancelFlag,
                                     summary](storage::Database &db) mutable {
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ExportSummary>::ok(summary);
        }
        emit progressChanged(2, QStringLiteral("collecting"));

        // Chats selected by the caller, in the list order used by the UI.
        auto chats = storage::ChatsRepo(db).list();
        if (chats.isError()) {
            return Result<ExportSummary>::error(chats.error());
        }
        const QSet<QString> wanted(opts.chatIds.begin(), opts.chatIds.end());
        QVector<Chat> selected;
        for (const Chat &chat : chats.value()) {
            if (!opts.chatIds.isEmpty() && !wanted.contains(chat.chatId)) {
                continue;
            }
            if (!opts.includeSecretChats && chat.kind == QLatin1String("secret")) {
                continue;
            }
            selected.push_back(chat);
        }
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ExportSummary>::ok(summary);
        }
        emit progressChanged(5, QStringLiteral("collecting"));

        QVector<Message> messages;
        for (const Chat &chat : selected) {
            auto listed = storage::MessagesRepo(db).listAllByChat(chat.chatId);
            if (listed.isError()) {
                return Result<ExportSummary>::error(listed.error());
            }
            for (const Message &message : listed.value()) {
                if (message.state == MessageState::DeletedLocal) {
                    continue;
                }
                messages.push_back(message);
            }
        }
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ExportSummary>::ok(summary);
        }
        emit progressChanged(10, QStringLiteral("archiving"));

        QJsonArray chatArray;
        for (const Chat &chat : selected) {
            chatArray.append(backup::chatToJson(chat));
        }
        QJsonObject chatsRoot;
        chatsRoot.insert(QStringLiteral("version"), backup::kVersion);
        chatsRoot.insert(QStringLiteral("chats"), chatArray);
        const QByteArray chatsJson = QJsonDocument(chatsRoot).toJson(QJsonDocument::Compact);

        QByteArray messagesJsonl;
        MediaCache cache(mediaRoot);
        QVector<std::pair<QString, QByteArray>> mediaEntries;
        QSet<QString> mediaSeen;
        int mediaCount = 0;
        int written = 0;

        for (const Message &message : messages) {
            if (cancelled(cancelFlag)) {
                summary.cancelled = true;
                return Result<ExportSummary>::ok(summary);
            }

            QString mediaExtension;
            if (opts.includeMedia && !message.mediaId.isEmpty()) {
                for (const QString &mediaId : splitMediaIds(message.mediaId)) {
                    if (!backup::safeMediaId(mediaId)) {
                        continue;
                    }
                    const QString path = cache.existingPath(mediaId, false);
                    if (path.isEmpty()) {
                        continue;
                    }
                    QFile file(path);
                    if (!file.open(QIODevice::ReadOnly)) {
                        continue;
                    }
                    const QByteArray bytes = file.readAll();
                    file.close();
                    if (bytes.isEmpty()) {
                        continue;
                    }
                    const QString extension = backup::safeMediaExtension(
                        QFileInfo(path).suffix());
                    const QString entryName =
                        QStringLiteral("media/%1.%2").arg(mediaId, extension);
                    if (!mediaSeen.contains(entryName)) {
                        mediaSeen.insert(entryName);
                        mediaEntries.append({entryName, bytes});
                        ++mediaCount;
                    }
                    if (mediaExtension.isEmpty()) {
                        mediaExtension = extension;
                    }
                }
            }

            messagesJsonl.append(
                QJsonDocument(backup::messageToJson(message, mediaExtension))
                    .toJson(QJsonDocument::Compact));
            messagesJsonl.append('\n');
            ++written;

            if (messages.size() <= 200
                || written % qMax<qsizetype>(1, messages.size() / 100) == 0) {
                const int percent =
                    15 + (messages.isEmpty() ? 0 : (55 * written) / messages.size());
                emit progressChanged(percent, QStringLiteral("archiving"));
            }
        }

        summary.chatCount = selected.size();
        summary.messageCount = messages.size();

        QJsonObject manifest;
        manifest.insert(QStringLiteral("format"), QStringLiteral("minibackup"));
        manifest.insert(QStringLiteral("version"), backup::kVersion);
        manifest.insert(QStringLiteral("created_at_ms"),
                        QDateTime::currentMSecsSinceEpoch());
        manifest.insert(QStringLiteral("user_id"), userId);
        manifest.insert(QStringLiteral("chat_count"), summary.chatCount);
        manifest.insert(QStringLiteral("message_count"), summary.messageCount);
        manifest.insert(QStringLiteral("media_count"), mediaCount);
        QJsonObject manifestOptions;
        manifestOptions.insert(QStringLiteral("include_media"), opts.includeMedia);
        manifestOptions.insert(QStringLiteral("include_secret_chats"),
                               opts.includeSecretChats);
        manifest.insert(QStringLiteral("options"), manifestOptions);

        QVector<std::pair<QString, QByteArray>> entries;
        entries.append({QStringLiteral("manifest.json"),
                        QJsonDocument(manifest).toJson(QJsonDocument::Compact)});
        entries.append({QStringLiteral("chats.json"), chatsJson});
        entries.append({QStringLiteral("messages.jsonl"), messagesJsonl});
        entries.append(mediaEntries);

        emit progressChanged(75, QStringLiteral("packing"));
        auto zip = backup::buildZip(entries);
        if (zip.isError()) {
            return Result<ExportSummary>::error(zip.error());
        }
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ExportSummary>::ok(summary);
        }
        emit progressChanged(90, QStringLiteral("encrypting"));
        auto sealed = backup::sealArchive(zip.value(), exportKey);
        if (sealed.isError()) {
            return Result<ExportSummary>::error(sealed.error());
        }

        const QFileInfo target(opts.outputPath);
        if (!QDir().mkpath(target.absolutePath())) {
            return Result<ExportSummary>::error(QStringLiteral("backup.write_failed"));
        }
        QSaveFile file(opts.outputPath);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(sealed.value()) != sealed.value().size() || !file.commit()) {
            return Result<ExportSummary>::error(QStringLiteral("backup.write_failed"));
        }

        summary.mediaCount = mediaCount;
        summary.bytes = sealed.value().size();
        emit progressChanged(100, QStringLiteral("done"));
        return Result<ExportSummary>::ok(summary);
    });

    watch<Result<ExportSummary>>(
        this, future, [done = std::move(done)](Result<ExportSummary> result) {
            done(std::move(result));
        });
}

} // namespace mini::services
