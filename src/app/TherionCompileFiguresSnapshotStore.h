#pragma once

#include "TherionCompileResult.h"

#include <QString>
#include <QStringList>

namespace TherionStudio
{
// Keeps a copy of the files behind the last good compile figures of each config, so a new window or the next app
// start can show them again (marked out of date when the sources changed since). The one-off database export lives
// in a temporary folder that goes away with the window, hence the copy. Holds no state beyond its root folder, so it
// can be used from the figures loader's worker thread.
class TherionCompileFiguresSnapshotStore final
{
public:
    // An empty root disables the store.
    explicit TherionCompileFiguresSnapshotStore(QString rootDirectory = QString());

    [[nodiscard]] bool isEnabled() const;

    // Copies the compile's .xtherion.dat and database exports; replaces the config's previous snapshot.
    bool save(const TherionCompileResult &result) const;
    // The saved compile of configPath with its file paths pointing into the snapshot, or an invalid result.
    [[nodiscard]] TherionCompileResult load(const QString &configPath) const;
    // The newest saved compile among configPaths, or an invalid result.
    [[nodiscard]] TherionCompileResult loadNewest(const QStringList &configPaths) const;

private:
    [[nodiscard]] QString snapshotDirectory(const QString &configPath) const;

    QString rootDirectory_;
};
}
