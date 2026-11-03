#include "services/SignInService.h"

#include "app/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace mini::services {

namespace {

bool ensureWritableDirectory(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    QDir directory(path);
    if (!directory.exists() && !QDir().mkpath(path)) {
        return false;
    }
    return QFileInfo(path).isWritable();
}

Result<QString> writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return Result<QString>::error(QStringLiteral("signin.write"));
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return Result<QString>::error(QStringLiteral("signin.write"));
    }
    return Result<QString>::ok(path);
}

} // namespace

SignInService::SignInService(QString downloadsDirectory)
    : m_downloads(std::move(downloadsDirectory))
{
    if (m_downloads.isEmpty()) {
        m_downloads = defaultDownloadsDirectory();
    }
}

QString SignInService::defaultDownloadsDirectory()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    }
    return path;
}

Result<crypto::SignInMaterial> SignInService::create() const
{
    const crypto::SignInMaterial material = crypto::SignInKey::generate();
    if (material.parsed.normalized.isEmpty()) {
        return Result<crypto::SignInMaterial>::error(QStringLiteral("signin.generate"));
    }
    return Result<crypto::SignInMaterial>::ok(material);
}

Result<crypto::NormalizedSignIn> SignInService::read(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<crypto::NormalizedSignIn>::error(QStringLiteral("signin.missing"));
    }
    return crypto::SignInKey::normalize(file.readAll());
}

QString SignInService::uniquePath(const QString &directory, const QString &baseName)
{
    QDir dir(directory);
    const QString base = baseName.isEmpty() ? QStringLiteral("sign.in") : baseName;
    QString candidate = dir.filePath(base);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? base.left(dot) : base;
    const QString suffix = dot > 0 ? base.mid(dot) : QString();
    for (int index = 2; index < 10000; ++index) {
        candidate = dir.filePath(QStringLiteral("%1 (%2)%3").arg(stem).arg(index).arg(suffix));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return dir.filePath(QStringLiteral("%1 (%2)%3")
                            .arg(stem, QString::number(QDateTime::currentMSecsSinceEpoch()), suffix));
}

Result<QString> SignInService::save(const crypto::SignInMaterial &material,
                                    bool *usedFallback) const
{
    QString directory = m_downloads;
    bool fallback = false;
    if (!ensureWritableDirectory(directory)) {
        directory = Paths::dataDir();
        fallback = true;
        if (!ensureWritableDirectory(directory)) {
            return Result<QString>::error(QStringLiteral("signin.no_directory"));
        }
    }
    if (usedFallback) {
        *usedFallback = fallback;
    }
    return writeFile(uniquePath(directory), material.fileBytes);
}

Result<QString> SignInService::saveTo(const crypto::SignInMaterial &material,
                                      const QString &path) const
{
    if (path.isEmpty()) {
        return Result<QString>::error(QStringLiteral("signin.write"));
    }
    return writeFile(path, material.fileBytes);
}

Result<QString> SignInService::replaceFile(const QString &temporaryPath,
                                           const QString &path) const
{
    QFile temporary(temporaryPath);
    if (!temporary.open(QIODevice::ReadOnly)) {
        return Result<QString>::error(QStringLiteral("signin.write"));
    }
    const QByteArray bytes = temporary.readAll();
    temporary.close();
    const Result<QString> written = writeFile(path, bytes);
    if (written.isOk()) {
        QFile::remove(temporaryPath);
    }
    return written;
}

Result<QString> SignInService::writeBytes(const QString &path,
                                          const QByteArray &bytes) const
{
    if (path.isEmpty()) {
        return Result<QString>::error(QStringLiteral("signin.write"));
    }
    return writeFile(path, bytes);
}

} // namespace mini::services
