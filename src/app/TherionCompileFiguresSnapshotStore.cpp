#include "TherionCompileFiguresSnapshotStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace TherionStudio
{
namespace
{
constexpr int kSnapshotVersion = 1;
constexpr qint64 kMaximumMetadataSize = 64 * 1024;
const QString kMetadataFileName = QStringLiteral("compile.json");
const QString kXtherionDataFileName = QStringLiteral("xtherion.dat");
const QString kPartialSuffix = QStringLiteral(".partial");

QString normalizedConfigPath(const QString &configPath)
{
    const QFileInfo info(configPath);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString databaseFileName(int index)
{
    return QStringLiteral("database-%1.sql").arg(index);
}

bool isSnapshotDatabaseFileName(const QString &fileName)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^database-\d+\.sql$)"));
    return pattern.match(fileName).hasMatch();
}

QDateTime parseTime(const QJsonValue &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}
}

TherionCompileFiguresSnapshotStore::TherionCompileFiguresSnapshotStore(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory))
{
}

bool TherionCompileFiguresSnapshotStore::isEnabled() const
{
    return !rootDirectory_.isEmpty();
}

QString TherionCompileFiguresSnapshotStore::snapshotDirectory(const QString &configPath) const
{
    const QByteArray hash =
        QCryptographicHash::hash(normalizedConfigPath(configPath).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(rootDirectory_).filePath(QString::fromLatin1(hash));
}

bool TherionCompileFiguresSnapshotStore::save(const TherionCompileResult &result) const
{
    if (!isEnabled() || !result.isValid() || !result.succeeded || result.configPath.isEmpty()
        || result.xtherionDataPath.isEmpty()) {
        return false;
    }

    const QString directory = snapshotDirectory(result.configPath);
    const QString partialDirectory = directory + kPartialSuffix;
    QDir(partialDirectory).removeRecursively();
    if (!QDir().mkpath(partialDirectory)) {
        return false;
    }
    const QDir partial(partialDirectory);
    bool copied = QFile::copy(result.xtherionDataPath, partial.filePath(kXtherionDataFileName));

    QJsonArray databaseFiles;
    for (const QString &databasePath : result.databasePaths) {
        const QString name = databaseFileName(static_cast<int>(databaseFiles.size()));
        if (QFile::copy(databasePath, partial.filePath(name))) {
            databaseFiles.append(name);
        }
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("version"), kSnapshotVersion);
    metadata.insert(QStringLiteral("configPath"), normalizedConfigPath(result.configPath));
    metadata.insert(QStringLiteral("workingDirectory"), result.workingDirectory);
    metadata.insert(QStringLiteral("startedAt"), result.startedAt.toString(Qt::ISODateWithMs));
    metadata.insert(QStringLiteral("finishedAt"), result.finishedAt.toString(Qt::ISODateWithMs));
    metadata.insert(QStringLiteral("databaseFiles"), databaseFiles);
    QFile metadataFile(partial.filePath(kMetadataFileName));
    const QByteArray metadataBytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    copied = copied && metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && metadataFile.write(metadataBytes) == metadataBytes.size();
    metadataFile.close();

    if (!copied) {
        QDir(partialDirectory).removeRecursively();
        return false;
    }
    QDir(directory).removeRecursively();
    if (!QDir().rename(partialDirectory, directory)) {
        QDir(partialDirectory).removeRecursively();
        return false;
    }
    return true;
}

TherionCompileResult TherionCompileFiguresSnapshotStore::load(const QString &configPath) const
{
    if (!isEnabled() || configPath.isEmpty()) {
        return TherionCompileResult();
    }

    const QDir directory(snapshotDirectory(configPath));
    QFile metadataFile(directory.filePath(kMetadataFileName));
    if (!metadataFile.open(QIODevice::ReadOnly) || metadataFile.size() > kMaximumMetadataSize) {
        return TherionCompileResult();
    }
    const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    if (metadata.value(QStringLiteral("version")).toInt() != kSnapshotVersion
        || metadata.value(QStringLiteral("configPath")).toString() != normalizedConfigPath(configPath)) {
        return TherionCompileResult();
    }

    TherionCompileResult result;
    result.configPath = configPath;
    result.workingDirectory = metadata.value(QStringLiteral("workingDirectory")).toString();
    result.startedAt = parseTime(metadata.value(QStringLiteral("startedAt")));
    result.finishedAt = parseTime(metadata.value(QStringLiteral("finishedAt")));
    result.succeeded = true;
    result.xtherionDataPath = directory.filePath(kXtherionDataFileName);
    if (!result.startedAt.isValid() || !result.finishedAt.isValid() || !QFileInfo::exists(result.xtherionDataPath)) {
        return TherionCompileResult();
    }
    // Only names this store wrote are accepted, so the snapshot cannot point outside its folder.
    for (const QJsonValue &value : metadata.value(QStringLiteral("databaseFiles")).toArray()) {
        const QString name = value.toString();
        if (isSnapshotDatabaseFileName(name) && QFileInfo::exists(directory.filePath(name))) {
            result.databasePaths.append(directory.filePath(name));
        }
    }
    return result;
}

TherionCompileResult TherionCompileFiguresSnapshotStore::loadNewest(const QStringList &configPaths) const
{
    TherionCompileResult newest;
    for (const QString &configPath : configPaths) {
        const TherionCompileResult result = load(configPath);
        if (result.isValid() && (!newest.isValid() || result.finishedAt > newest.finishedAt)) {
            newest = result;
        }
    }
    return newest;
}
}
