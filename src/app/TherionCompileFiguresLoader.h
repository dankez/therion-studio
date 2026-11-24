#pragma once

#include "TherionCompileFiguresSnapshotStore.h"
#include "TherionCompileResult.h"

#include "reports/TherionCompileDatabaseFigures.h"

#include "../core/TherionXtherionData.h"

#include <QHash>
#include <QObject>

template <typename T>
class QFutureWatcher;

namespace TherionStudio
{
// Figures of the last good compile, from the .xtherion.dat and SQL database export it wrote.
struct TherionCompileFigures
{
    // The compile the figures come from; invalid before the first good compile.
    TherionCompileResult compile;
    QHash<QString, TherionXtherionSurvey> surveysByFullName;
    // Unavailable when the compile wrote no database export.
    TherionCompileDatabaseFigures database;
    // A later compile failed or wrote no survey tree, so the figures are older.
    bool laterCompileFailed = false;
    // Restored from the saved snapshot of an earlier window or app run, not from a compile in this window.
    bool restored = false;

    [[nodiscard]] bool isAvailable() const { return compile.isValid(); }
};

// Reads the compile's files off the UI thread after each compile and keeps the last good figures.
class TherionCompileFiguresLoader final : public QObject
{
    Q_OBJECT

public:
    // A disabled store keeps the figures for this window only.
    explicit TherionCompileFiguresLoader(
        TherionCompileFiguresSnapshotStore snapshotStore = TherionCompileFiguresSnapshotStore(),
        QObject *parent = nullptr);

    // An invalid result (project closed) clears the figures. Good figures are also saved to the snapshot store.
    void updateFromCompile(const TherionCompileResult &result);
    // Loads the newest saved snapshot of configPaths when there are no figures yet and no compile is being read.
    // Returns false when it did not start, so the caller can try again later.
    bool restoreFromSnapshot(const QStringList &configPaths);

    [[nodiscard]] const TherionCompileFigures &figures() const;

signals:
    void figuresChanged();

private:
    struct Loaded
    {
        quint64 generation = 0;
        TherionCompileResult compile;
        QVector<TherionXtherionSurvey> surveys;
        TherionCompileDatabaseFigures database;
        bool restored = false;
    };

    [[nodiscard]] static Loaded readCompileFiles(const TherionCompileResult &result, quint64 generation);
    void handleLoadFinished();
    void markLaterCompileFailed();

    TherionCompileFiguresSnapshotStore snapshotStore_;
    quint64 generation_ = 0;
    TherionCompileFigures figures_;
    QFutureWatcher<Loaded> *loadWatcher_ = nullptr;
};
}
