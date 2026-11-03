#include "services/ImportService.h"

#include "services/BackupFormat.h"
#include "services/MediaCache.h"
#include "storage/Backup.h"
#include "storage/ChatsRepo.h"
#include "storage/Database.h"
#include "storage/MessagesRepo.h"
#include "storage/StorageWorker.h"

#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// Reads chats.json into the list. Both the documented object and a bare array
// are accepted so older archives keep importing.
bool parseChats(const QByteArray &bytes, QVector<Chat> *chats)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || chats == nullptr) {
        return false;
    }
    QJsonArray array;
    if (document.isArray()) {
        array = document.array();
    } else if (document.isObject()) {
        array = document.object().value(QStringLiteral("chats")).toArray();
    } else {
        return false;
    }
    chats->clear();
    for (const QJsonValue &value : array) {
        Chat chat;
        if (!value.isObject() || !backup::chatFromJson(value.toObject(), &chat)) {
            continue;
        }
        chats->push_back(chat);
    }
    return true;
}

} // namespace

ImportService::ImportService(storage::StorageWorker *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
}

void ImportService::setKMac(const QByteArray &kMac)
{
    m_kMac = kMac;
}

void ImportService::setMediaRoot(const QString &root)
{
    m_mediaRoot = root;
}

void ImportService::setBackupLocation(const QString &backupsDir, const QString &keysPath)
{
    m_backupDir = backupsDir;
    m_keysPath = keysPath;
}

void ImportService::cancel()
{
    m_cancelRequested.store(true);
    if (m_cancel) {
        m_cancel->store(true);
    }
}

void ImportService::importBackup(const ImportOptions &options, Done done)
{
    if (!m_storage) {
        done(Result<ImportSummary>::error(QStringLiteral("storage_unavailable")));
        return;
    }
    if (options.inputPath.isEmpty()) {
        done(Result<ImportSummary>::error(QStringLiteral("backup.path_missing")));
        return;
    }
    if (m_kMac.isEmpty()) {
        done(Result<ImportSummary>::error(QStringLiteral("backup.key_missing")));
        return;
    }
    const QString backupDir =
        options.backupDir.isEmpty() ? m_backupDir : options.backupDir;
    if (backupDir.isEmpty()) {
        done(Result<ImportSummary>::error(
            QStringLiteral("backup.backup_dir_missing")));
        return;
    }

    m_cancel = std::make_shared<std::atomic<bool>>(m_cancelRequested.exchange(false));
    const auto cancelFlag = m_cancel;
    const QByteArray exportKey = backup::deriveExportKey(m_kMac);
    const QString mediaRoot = m_mediaRoot;
    const QString keysPath = m_keysPath;
    const ImportOptions opts = options;
    ImportSummary summary;

    auto future = m_storage->submit([this, opts, exportKey, mediaRoot, backupDir, keysPath,
                                     cancelFlag, summary](storage::Database &db) mutable {
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ImportSummary>::ok(summary);
        }
        emit progressChanged(2, QStringLiteral("reading"));

        QFile file(opts.inputPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return Result<ImportSummary>::error(QStringLiteral("backup.file_unreadable"));
        }
        const QByteArray sealed = file.readAll();
        file.close();
        if (sealed.isEmpty()) {
            return Result<ImportSummary>::error(QStringLiteral("backup.file_unreadable"));
        }

        auto archive = backup::openArchive(sealed, exportKey);
        if (archive.isError()) {
            return Result<ImportSummary>::error(archive.error());
        }
        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ImportSummary>::ok(summary);
        }
        emit progressChanged(10, QStringLiteral("reading"));

        auto entries = backup::readZip(archive.value());
        if (entries.isError()) {
            return Result<ImportSummary>::error(entries.error());
        }

        // Resolve every media/<id>.<ext> entry by id, so albums whose items
        // have different extensions still import. Names are validated here
        // instead of being trusted from the JSON line.
        QHash<QString, std::pair<QString, QByteArray>> mediaEntries;
        for (auto it = entries.value().constBegin(); it != entries.value().constEnd();
             ++it) {
            const QString &name = it.key();
            if (!name.startsWith(QLatin1String("media/"))) {
                continue;
            }
            const QString rest = name.mid(6);
            const int dot = rest.lastIndexOf(QLatin1Char('.'));
            if (dot <= 0) {
                continue;
            }
            const QString mediaId = rest.left(dot);
            if (!backup::safeMediaId(mediaId)) {
                continue;
            }
            mediaEntries.insert(
                mediaId, {backup::safeMediaExtension(rest.mid(dot + 1)), it.value()});
        }

        const QByteArray manifestBytes = entries.value().value(
            QStringLiteral("manifest.json"));
        if (manifestBytes.isEmpty()) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.manifest_invalid"));
        }
        QJsonParseError manifestError{};
        const QJsonDocument manifestDocument =
            QJsonDocument::fromJson(manifestBytes, &manifestError);
        if (manifestError.error != QJsonParseError::NoError
            || !manifestDocument.isObject()) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.manifest_invalid"));
        }
        const QJsonObject manifest = manifestDocument.object();
        if (manifest.value(QStringLiteral("format")).toString()
            != QLatin1String("minibackup")) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.manifest_invalid"));
        }
        if (manifest.value(QStringLiteral("version")).toInt() != backup::kVersion) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.unsupported_version"));
        }

        QVector<Chat> chats;
        if (!parseChats(entries.value().value(QStringLiteral("chats.json")), &chats)) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.manifest_invalid"));
        }

        QVector<Message> messages;
        const QByteArray lines = entries.value().value(QStringLiteral("messages.jsonl"));
        int start = 0;
        while (start < lines.size()) {
            int end = lines.indexOf('\n', start);
            if (end < 0) {
                end = lines.size();
            }
            QByteArray line = lines.mid(start, end - start);
            start = end + 1;
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.trimmed().isEmpty()) {
                continue;
            }
            QJsonParseError lineError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &lineError);
            Message message;
            if (lineError.error != QJsonParseError::NoError || !document.isObject()
                || !backup::messageFromJson(document.object(), &message, nullptr)) {
                return Result<ImportSummary>::error(
                    QStringLiteral("backup.messages_invalid"));
            }
            messages.push_back(message);
        }

        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ImportSummary>::ok(summary);
        }
        emit progressChanged(20, QStringLiteral("backup"));

        // §49.2: snapshot the live database before the first write.
        auto snapshot = storage::Backup::create(db, keysPath, backupDir, 3);
        if (snapshot.isError()) {
            return Result<ImportSummary>::error(
                QStringLiteral("backup.backup_failed"));
        }

        if (cancelled(cancelFlag)) {
            summary.cancelled = true;
            return Result<ImportSummary>::ok(summary);
        }
        emit progressChanged(30, QStringLiteral("merging"));

        QSet<QString> knownChats;
        auto existing = storage::ChatsRepo(db).list();
        if (existing.isError()) {
            return Result<ImportSummary>::error(existing.error());
        }
        for (const Chat &chat : existing.value()) {
            knownChats.insert(chat.chatId);
        }
        for (const Chat &chat : chats) {
            if (knownChats.contains(chat.chatId)) {
                continue;
            }
            auto stored = storage::ChatsRepo(db).upsert(chat);
            if (stored.isError()) {
                return Result<ImportSummary>::error(stored.error());
            }
            knownChats.insert(chat.chatId);
            ++summary.chatsAdded;
        }

        for (int i = 0; i < messages.size(); ++i) {
            if (cancelled(cancelFlag)) {
                summary.cancelled = true;
                return Result<ImportSummary>::ok(summary);
            }
            const Message &message = messages.at(i);
            if (!knownChats.contains(message.chatId)) {
                Chat chat;
                chat.chatId = message.chatId;
                chat.kind = QStringLiteral("direct");
                chat.title = message.chatId;
                auto stored = storage::ChatsRepo(db).upsert(chat);
                if (stored.isError()) {
                    return Result<ImportSummary>::error(stored.error());
                }
                knownChats.insert(chat.chatId);
                ++summary.chatsAdded;
            }

            auto inserted = storage::MessagesRepo(db).insertImported(message);
            if (inserted.isError()) {
                return Result<ImportSummary>::error(inserted.error());
            }
            if (inserted.value()) {
                ++summary.messagesAdded;
            } else {
                ++summary.messagesSkipped;
            }

            if (messages.size() <= 200
                || i % qMax<qsizetype>(1, messages.size() / 100) == 0) {
                const int percent =
                    30 + (messages.isEmpty() ? 0 : (50 * (i + 1)) / messages.size());
                emit progressChanged(percent, QStringLiteral("merging"));
            }
        }

        if (opts.restoreMedia && !mediaRoot.isEmpty()) {
            emit progressChanged(80, QStringLiteral("media"));
            MediaCache cache(mediaRoot);
            for (int i = 0; i < messages.size(); ++i) {
                if (cancelled(cancelFlag)) {
                    summary.cancelled = true;
                    return Result<ImportSummary>::ok(summary);
                }
                const Message &message = messages.at(i);
                if (message.mediaId.isEmpty()) {
                    continue;
                }
                for (const QString &mediaId : splitMediaIds(message.mediaId)) {
                    if (!backup::safeMediaId(mediaId)) {
                        continue;
                    }
                    const auto found = mediaEntries.constFind(mediaId);
                    if (found == mediaEntries.constEnd()) {
                        continue;
                    }
                    const QByteArray bytes = found->second;
                    if (bytes.isEmpty()) {
                        continue;
                    }
                    if (!cache.store(mediaId, bytes, found->first, false).isEmpty()) {
                        ++summary.mediaImported;
                    }
                }
                if (messages.size() <= 200
                    || i % qMax<qsizetype>(1, messages.size() / 100) == 0) {
                    const int percent =
                        80 + (messages.isEmpty() ? 0 : (15 * (i + 1)) / messages.size());
                    emit progressChanged(percent, QStringLiteral("media"));
                }
            }
        }

        emit progressChanged(100, QStringLiteral("done"));
        return Result<ImportSummary>::ok(summary);
    });

    watch<Result<ImportSummary>>(
        this, future, [done = std::move(done)](Result<ImportSummary> result) {
            done(std::move(result));
        });
}

} // namespace mini::services
