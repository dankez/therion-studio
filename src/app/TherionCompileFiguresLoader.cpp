#include "TherionCompileFiguresLoader.h"

#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace TherionStudio
{
TherionCompileFiguresLoader::TherionCompileFiguresLoader(TherionCompileFiguresSnapshotStore snapshotStore,
                                                         QObject *parent)
    : QObject(parent)
    , snapshotStore_(std::move(snapshotStore))
    , loadWatcher_(new QFutureWatcher<Loaded>(this))
{
    connect(loadWatcher_, &QFutureWatcher<Loaded>::finished, this, &TherionCompileFiguresLoader::handleLoadFinished);
}

TherionCompileFiguresLoader::Loaded TherionCompileFiguresLoader::readCompileFiles(const TherionCompileResult &result,
                                                                                  quint64 generation)
{
    Loaded loaded;
    loaded.generation = generation;
    loaded.compile = result;
    QFile file(result.xtherionDataPath);
    if (file.open(QIODevice::ReadOnly)) {
        loaded.surveys = parseTherionXtherionSurveys(QString::fromUtf8(file.readAll()));
    }
    if (!loaded.surveys.isEmpty()) {
        loaded.database = readTherionCompileDatabaseFigures(result.databasePaths);
    }
    return loaded;
}

void TherionCompileFiguresLoader::updateFromCompile(const TherionCompileResult &result)
{
    const quint64 generation = ++generation_;
    if (!result.isValid()) {
        figures_ = TherionCompileFigures();
        emit figuresChanged();
        return;
    }
    if (!result.succeeded || result.xtherionDataPath.isEmpty()) {
        markLaterCompileFailed();
        return;
    }

    loadWatcher_->setFuture(QtConcurrent::run([result, generation, store = snapshotStore_]() {
        Loaded loaded = readCompileFiles(result, generation);
        if (!loaded.surveys.isEmpty()) {
            store.save(result);
        }
        return loaded;
    }));
}

bool TherionCompileFiguresLoader::restoreFromSnapshot(const QStringList &configPaths)
{
    if (!snapshotStore_.isEnabled() || configPaths.isEmpty() || figures_.isAvailable() || loadWatcher_->isRunning()) {
        return false;
    }
    const quint64 generation = ++generation_;
    loadWatcher_->setFuture(QtConcurrent::run([configPaths, generation, store = snapshotStore_]() {
        const TherionCompileResult result = store.loadNewest(configPaths);
        Loaded loaded;
        if (result.isValid()) {
            loaded = readCompileFiles(result, generation);
        }
        loaded.generation = generation;
        loaded.restored = true;
        return loaded;
    }));
    return true;
}

const TherionCompileFigures &TherionCompileFiguresLoader::figures() const
{
    return figures_;
}

void TherionCompileFiguresLoader::handleLoadFinished()
{
    const Loaded loaded = loadWatcher_->result();
    if (loaded.generation != generation_) {
        return;
    }
    // Therion writes the survey tree only when the compile got far enough.
    if (loaded.surveys.isEmpty()) {
        if (!loaded.restored) {
            markLaterCompileFailed();
        }
        return;
    }

    TherionCompileFigures figures;
    figures.compile = loaded.compile;
    figures.database = loaded.database;
    figures.restored = loaded.restored;
    for (const TherionXtherionSurvey &survey : loaded.surveys) {
        figures.surveysByFullName.insert(survey.fullName, survey);
    }
    figures_ = figures;
    emit figuresChanged();
}

void TherionCompileFiguresLoader::markLaterCompileFailed()
{
    if (!figures_.isAvailable() || figures_.laterCompileFailed) {
        return;
    }
    figures_.laterCompileFailed = true;
    emit figuresChanged();
}
}
