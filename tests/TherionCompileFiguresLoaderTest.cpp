#include "../src/app/TherionCompileFiguresLoader.h"

#include <QSignalSpy>
#include <memory>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace TherionStudio;

namespace
{
const QString kSurveyLine = QStringLiteral(
    "xth_cp_data_tree_insert 18 16 2 inner inner.cave \"Inner\" \"Inner\" \"length: 15.00m\"\n");

const QString kDatabase = QStringLiteral(
    "create table SURVEY (ID integer, PARENT_ID integer, NAME varchar(6), FULL_NAME varchar(10), TITLE varchar(10));\n"
    "create table CENTRELINE (ID integer, SURVEY_ID integer, TITLE varchar(4), TOPO_DATE date, EXPLO_DATE date, "
    "LENGTH real, SURFACE_LENGTH real, DUPLICATE_LENGTH real);\n"
    "create table PERSON (ID integer, NAME varchar(10), SURNAME varchar(8));\n"
    "create table EXPLO (PERSON_ID integer, CENTRELINE_ID integer);\n"
    "create table TOPO (PERSON_ID integer, CENTRELINE_ID integer);\n"
    "create table STATION (ID integer, NAME varchar(4), SURVEY_ID integer, X real, Y real, Z real);\n"
    "create table STATION_FLAG (STATION_ID integer, FLAG char(3));\n"
    "create table SHOT (ID integer, FROM_ID integer, TO_ID integer, CENTRELINE_ID integer, LENGTH real, BEARING real, "
    "GRADIENT real, ADJ_LENGTH real, ADJ_BEARING real, ADJ_GRADIENT real, ERR_LENGTH real, ERR_BEARING real, "
    "ERR_GRADIENT real);\n"
    "create table SHOT_FLAG (SHOT_ID integer, FLAG char(3));\n"
    "create table MAPS (ID integer, SURVEY_ID integer, NAME varchar(6), TITLE varchar(35), PROJID integer, LENGTH real, "
    "DEPTH real);\n"
    "create table SCRAPS (ID integer, SURVEY_ID integer, NAME varchar(6), PROJID integer, MAX_DISTORTION real, "
    "AVG_DISTORTION real);\n"
    "create table MAPITEMS (ID integer, TYPE integer, ITEMID integer);\n"
    "insert into SURVEY values (1, 0, '', '', NULL);\n"
    "insert into SURVEY values (17, 1, 'cave', 'cave', 'Cave');\n"
    "insert into SURVEY values (19, 17, 'inner', 'inner.cave', NULL);\n"
    "insert into SCRAPS values (22, 19, 'SI', 1, 30.77453, 2.75729);\n");

TherionCompileResult compileResult(const QString &xtherionDataPath, bool succeeded)
{
    TherionCompileResult result;
    result.configPath = QStringLiteral("thconfig");
    result.startedAt = QDateTime::currentDateTime();
    result.finishedAt = result.startedAt;
    result.succeeded = succeeded;
    result.xtherionDataPath = xtherionDataPath;
    return result;
}

class TherionCompileFiguresLoaderTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void loadsSurveysOfGoodCompile();
    void keepsFiguresWhenLaterCompileFails();
    void keepsFiguresWhenLaterCompileWritesNoTree();
    void staysUnavailableAfterFirstCompileFails();
    void clearsOnInvalidResult();
    void leavesDatabaseUnavailableWithoutExport();
    void restoresSavedFiguresInNewLoader();
    void restoresNothingWithoutSnapshot();
    void doesNotRestoreOverExistingFigures();
    void keepsSnapshotWhenSourceFilesGoAway();

private:
    QString writeDataFile(const QString &name, const QString &text);
    void loadAndWait(TherionCompileFiguresLoader &loader, const TherionCompileResult &result);

    std::unique_ptr<QTemporaryDir> dir_;
};

void TherionCompileFiguresLoaderTest::init()
{
    dir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(dir_->isValid());
}

QString TherionCompileFiguresLoaderTest::writeDataFile(const QString &name, const QString &text)
{
    const QString path = dir_->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(text.toUtf8());
    return path;
}

void TherionCompileFiguresLoaderTest::loadAndWait(TherionCompileFiguresLoader &loader,
                                                    const TherionCompileResult &result)
{
    QSignalSpy spy(&loader, &TherionCompileFiguresLoader::figuresChanged);
    loader.updateFromCompile(result);
    QVERIFY(spy.count() > 0 || spy.wait());
}

void TherionCompileFiguresLoaderTest::loadsSurveysOfGoodCompile()
{
    TherionCompileFiguresLoader loader;
    const TherionCompileResult result = compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true);
    loadAndWait(loader, result);

    const TherionCompileFigures &statistics = loader.figures();
    QVERIFY(statistics.isAvailable());
    QVERIFY(!statistics.laterCompileFailed);
    QCOMPARE(statistics.compile.startedAt, result.startedAt);
    QCOMPARE(statistics.surveysByFullName.value(QStringLiteral("inner.cave")).title, QStringLiteral("Inner"));
}

void TherionCompileFiguresLoaderTest::keepsFiguresWhenLaterCompileFails()
{
    TherionCompileFiguresLoader loader;
    loadAndWait(loader, compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true));
    loadAndWait(loader, compileResult(QString(), false));

    QVERIFY(loader.figures().isAvailable());
    QVERIFY(loader.figures().laterCompileFailed);
    QVERIFY(loader.figures().surveysByFullName.contains(QStringLiteral("inner.cave")));
}

void TherionCompileFiguresLoaderTest::keepsFiguresWhenLaterCompileWritesNoTree()
{
    TherionCompileFiguresLoader loader;
    loadAndWait(loader, compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true));
    loadAndWait(loader, compileResult(writeDataFile(QStringLiteral("b.dat"), QStringLiteral("xth_cp_data_tree_create\n")), true));

    QVERIFY(loader.figures().laterCompileFailed);
    QVERIFY(loader.figures().surveysByFullName.contains(QStringLiteral("inner.cave")));
}

void TherionCompileFiguresLoaderTest::staysUnavailableAfterFirstCompileFails()
{
    TherionCompileFiguresLoader loader;
    QSignalSpy spy(&loader, &TherionCompileFiguresLoader::figuresChanged);
    loader.updateFromCompile(compileResult(QString(), false));

    QCOMPARE(spy.count(), 0);
    QVERIFY(!loader.figures().isAvailable());
}

void TherionCompileFiguresLoaderTest::clearsOnInvalidResult()
{
    TherionCompileFiguresLoader loader;
    loadAndWait(loader, compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true));
    loadAndWait(loader, TherionCompileResult());

    QVERIFY(!loader.figures().isAvailable());
    QVERIFY(loader.figures().surveysByFullName.isEmpty());
}
void TherionCompileFiguresLoaderTest::leavesDatabaseUnavailableWithoutExport()
{
    TherionCompileFiguresLoader loader;
    TherionCompileResult result = compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true);
    result.databasePaths = {dir_->filePath(QStringLiteral("missing.sql"))};
    loadAndWait(loader, result);

    QVERIFY(loader.figures().isAvailable());
    QVERIFY(!loader.figures().database.isAvailable());
}

namespace
{
bool waitForFigures(TherionCompileFiguresLoader &loader, const QStringList &configPaths)
{
    QSignalSpy spy(&loader, &TherionCompileFiguresLoader::figuresChanged);
    if (!loader.restoreFromSnapshot(configPaths)) {
        return false;
    }
    return spy.wait(5000);
}
}

void TherionCompileFiguresLoaderTest::restoresSavedFiguresInNewLoader()
{
    const QString snapshotRoot = dir_->filePath(QStringLiteral("snapshots"));
    const QString configPath = writeDataFile(QStringLiteral("thconfig"), QStringLiteral("source cave.th\n"));
    TherionCompileResult result = compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true);
    result.configPath = configPath;
    result.databasePaths = {writeDataFile(QStringLiteral("database.sql"), kDatabase)};
    {
        TherionCompileFiguresLoader loader{TherionCompileFiguresSnapshotStore(snapshotRoot)};
        loadAndWait(loader, result);
        QVERIFY(loader.figures().database.isAvailable());
        QVERIFY(!loader.figures().restored);
    }
    // The one-off database export folder goes away with the window that compiled.
    QVERIFY(QFile::remove(result.databasePaths.constFirst()));
    QVERIFY(QFile::remove(result.xtherionDataPath));

    TherionCompileFiguresLoader restoredLoader{TherionCompileFiguresSnapshotStore(snapshotRoot)};
    QVERIFY(waitForFigures(restoredLoader, {dir_->filePath(QStringLiteral("other.thconfig")), configPath}));

    const TherionCompileFigures &figures = restoredLoader.figures();
    QVERIFY(figures.isAvailable());
    QVERIFY(figures.restored);
    QVERIFY(!figures.laterCompileFailed);
    QCOMPARE(figures.compile.configPath, configPath);
    QCOMPARE(figures.compile.startedAt, result.startedAt);
    QVERIFY(figures.surveysByFullName.contains(QStringLiteral("inner.cave")));
    QVERIFY(figures.database.isAvailable());
    const TherionScrapFigures scrap = figures.database.scrapsByKey.value(QStringLiteral("SI@inner.cave"));
    QVERIFY(scrap.maximumDistortion);
    QCOMPARE(*scrap.maximumDistortion, 30.77453);
    QVERIFY(scrap.averageDistortion);
    QCOMPARE(*scrap.averageDistortion, 2.75729);
    // Staleness still compares the real sources with the time of the saved compile.
    QVERIFY(!therionCompileResultIsOutOfDate(figures.compile, {configPath}));
}

void TherionCompileFiguresLoaderTest::restoresNothingWithoutSnapshot()
{
    TherionCompileFiguresLoader disabled;
    QVERIFY(!disabled.restoreFromSnapshot({QStringLiteral("thconfig")}));

    TherionCompileFiguresLoader loader{TherionCompileFiguresSnapshotStore(dir_->filePath(QStringLiteral("empty")))};
    QSignalSpy spy(&loader, &TherionCompileFiguresLoader::figuresChanged);
    QVERIFY(loader.restoreFromSnapshot({dir_->filePath(QStringLiteral("thconfig"))}));
    QVERIFY(!spy.wait(300));
    QVERIFY(!loader.figures().isAvailable());
}

void TherionCompileFiguresLoaderTest::doesNotRestoreOverExistingFigures()
{
    const QString snapshotRoot = dir_->filePath(QStringLiteral("snapshots"));
    TherionCompileFiguresLoader loader{TherionCompileFiguresSnapshotStore(snapshotRoot)};
    loadAndWait(loader, compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true));

    QVERIFY(!loader.restoreFromSnapshot({QStringLiteral("thconfig")}));
    QVERIFY(!loader.figures().restored);
}

void TherionCompileFiguresLoaderTest::keepsSnapshotWhenSourceFilesGoAway()
{
    const QString snapshotRoot = dir_->filePath(QStringLiteral("snapshots"));
    const QString configPath = writeDataFile(QStringLiteral("thconfig"), QStringLiteral("source cave.th\n"));
    TherionCompileResult result = compileResult(writeDataFile(QStringLiteral("a.dat"), kSurveyLine), true);
    result.configPath = configPath;
    TherionCompileFiguresSnapshotStore store(snapshotRoot);
    QVERIFY(store.save(result));

    // A failed save of a later compile leaves the previous snapshot in place.
    TherionCompileResult missing = result;
    missing.xtherionDataPath = dir_->filePath(QStringLiteral("gone.dat"));
    QVERIFY(!store.save(missing));
    const TherionCompileResult loaded = store.load(configPath);
    QVERIFY(loaded.isValid());
    QCOMPARE(loaded.finishedAt, result.finishedAt);
    QVERIFY(loaded.databasePaths.isEmpty());
    QVERIFY(!store.load(dir_->filePath(QStringLiteral("other.thconfig"))).isValid());
}
}

int runTherionCompileFiguresLoaderTest(int argc, char **argv)
{
    TherionCompileFiguresLoaderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TherionCompileFiguresLoaderTest.moc"
