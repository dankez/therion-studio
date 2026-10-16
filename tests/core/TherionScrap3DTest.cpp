#include "../../src/core/TherionScrap3D.h"
#include "../../src/core/TherionCentreline.h"

#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

#include <cmath>

using namespace TherionStudio;

class TherionScrap3DTest : public QObject
{
    Q_OBJECT

private slots:
    void parseScaleOption();
    void fitTransformWithTwoAnchors();
    void fitTransformSingleAnchorFallback();
    void resolveStationKey();
    void projectScrapRealCaveFixture();
    void integratedComputeWithTh2();
};

void TherionScrap3DTest::parseScaleOption()
{
    // Real scale option from clopy01.th2:
    // -scale [-128 -1152 851 -1152 0.0 0.0 24.8666 0.0 m]
    const QStringList tokens = {
        QStringLiteral("[-128"),
        QStringLiteral("-1152"),
        QStringLiteral("851"),
        QStringLiteral("-1152"),
        QStringLiteral("0.0"),
        QStringLiteral("0.0"),
        QStringLiteral("24.8666"),
        QStringLiteral("0.0"),
        QStringLiteral("m]")
    };

    const TherionScrapScale scale = TherionScrap3D::parseScaleOption(tokens);
    QVERIFY(scale.valid);
    QCOMPARE(scale.p1, QPointF(-128, -1152));
    QCOMPARE(scale.p2, QPointF(851, -1152));
    QVERIFY(std::abs(scale.worldLengthMeters - 24.8666) < 1e-4);
    // 24.8666 / (851 - (-128)) = 24.8666 / 979 = 0.0254 m/pt
    QVERIFY(std::abs(scale.metersPerPoint - 0.0254) < 1e-4);

    // Test feet conversion: 100 ft = 30.48 m
    const QStringList feetTokens = {
        QStringLiteral("[0"),
        QStringLiteral("0"),
        QStringLiteral("100"),
        QStringLiteral("0"),
        QStringLiteral("0.0"),
        QStringLiteral("0.0"),
        QStringLiteral("100.0"),
        QStringLiteral("0.0"),
        QStringLiteral("ft]")
    };
    const TherionScrapScale feetScale = TherionScrap3D::parseScaleOption(feetTokens);
    QVERIFY(feetScale.valid);
    QVERIFY(std::abs(feetScale.metersPerPoint - 0.3048) < 1e-4);
}

void TherionScrap3DTest::fitTransformWithTwoAnchors()
{
    // Anchor 1: scrap (0, 0) -> centreline (10, 20, 5)
    // Anchor 2: scrap (100, 0) -> centreline (10, 22.54, 15)
    // Scale: 2.54 / 100 = 0.0254 m/pt
    // Rotation: from +X (East in scrap) to +Y (North in world): +90 degrees = pi/2
    TherionScrapAnchor a1;
    a1.stationName = QStringLiteral("A");
    a1.scrapPoint = QPointF(0, 0);
    a1.centrelinePosition = ThreeDViewerVec3{10.0, 20.0, 5.0};
    a1.matched = true;

    TherionScrapAnchor a2;
    a2.stationName = QStringLiteral("B");
    a2.scrapPoint = QPointF(100, 0);
    a2.centrelinePosition = ThreeDViewerVec3{10.0, 22.54, 15.0};
    a2.matched = true;

    const TherionScrapTransform transform = TherionScrap3D::fitTransform({a1, a2});
    QVERIFY(transform.valid);
    QCOMPARE(transform.matchedAnchorCount, 2);
    QVERIFY(std::abs(transform.scale - 0.0254) < 1e-4);
    QVERIFY(std::abs(transform.rotation - (M_PI / 2.0)) < 1e-4);
    QVERIFY(transform.meanResidual < 1e-4);
    QVERIFY(transform.maxResidual < 1e-4);

    // Test transformed points
    const QPointF p1_fit = transform.transformPoint(a1.scrapPoint);
    QVERIFY(std::abs(p1_fit.x() - 10.0) < 1e-4);
    QVERIFY(std::abs(p1_fit.y() - 20.0) < 1e-4);

    const QPointF p2_fit = transform.transformPoint(a2.scrapPoint);
    QVERIFY(std::abs(p2_fit.x() - 10.0) < 1e-4);
    QVERIFY(std::abs(p2_fit.y() - 22.54) < 1e-4);

    // Test Z-interpolation at midpoint (50, 0): equidistant from both anchors -> average (5 + 15)/2 = 10.0
    const double zMid = transform.interpolateZ(QPointF(50, 0), {a1, a2});
    QVERIFY(std::abs(zMid - 10.0) < 1e-3);
}

void TherionScrap3DTest::fitTransformSingleAnchorFallback()
{
    TherionScrapAnchor a1;
    a1.stationName = QStringLiteral("A");
    a1.scrapPoint = QPointF(100, 200);
    a1.centrelinePosition = ThreeDViewerVec3{50.0, 60.0, 10.0};
    a1.matched = true;

    TherionScrapScale scale;
    scale.valid = true;
    scale.metersPerPoint = 0.05;

    const TherionScrapTransform transform = TherionScrap3D::fitTransform({a1}, scale);
    QVERIFY(transform.valid);
    QCOMPARE(transform.matchedAnchorCount, 1);
    QCOMPARE(transform.scale, 0.05);
    QCOMPARE(transform.rotation, 0.0);

    const QPointF p1_fit = transform.transformPoint(a1.scrapPoint);
    QCOMPARE(p1_fit.x(), 50.0);
    QCOMPARE(p1_fit.y(), 60.0);
}

void TherionScrap3DTest::resolveStationKey()
{
    QHash<QString, ThreeDViewerVec3> stationPositions;
    stationPositions.insert(QStringLiteral("0@hp.1302"), {0.0, 0.0, 0.0});
    stationPositions.insert(QStringLiteral("1@hp.1302"), {1.0, 2.0, 3.0});
    stationPositions.insert(QStringLiteral("entrance"), {100.0, 200.0, 50.0});

    // Exact match
    QCOMPARE(TherionScrap3D::resolveStationKey(QStringLiteral("entrance"), QString(), stationPositions),
             QStringLiteral("entrance"));

    // Scoped resolution: "0@hp" in survey "1302" resolves to "0@hp.1302"
    QCOMPARE(TherionScrap3D::resolveStationKey(QStringLiteral("0@hp"), QStringLiteral("1302"), stationPositions),
             QStringLiteral("0@hp.1302"));

    // Prefix/suffix match: "0@hp" matches "0@hp.1302" even without scrap survey path
    QCOMPARE(TherionScrap3D::resolveStationKey(QStringLiteral("0@hp"), QString(), stationPositions),
             QStringLiteral("0@hp.1302"));

    // Nonexistent station
    QVERIFY(TherionScrap3D::resolveStationKey(QStringLiteral("nonexistent"), QString(), stationPositions).isEmpty());
}

void TherionScrap3DTest::projectScrapRealCaveFixture()
{
    // Real cave fixture data: hp.th and clopy01.th2
    const QString hpPath = QDir(QStringLiteral(THERION_STUDIO_TEST_FIXTURE_ROOT))
        .filePath(QStringLiteral("projects/map_editor_regression/real_cave_raster/data/hp.th"));
    const QString th2Path = QDir(QStringLiteral(THERION_STUDIO_TEST_FIXTURE_ROOT))
        .filePath(QStringLiteral("projects/map_editor_regression/real_cave_raster/data/clopy01.th2"));

    QVERIFY(QFile::exists(hpPath));
    QVERIFY(QFile::exists(th2Path));

    // 1. Solve centreline
    const TherionCentrelineResult centrelineResult = computeTherionCentreline({hpPath}, defaultTherionCentrelineFileReader());
    QVERIFY(centrelineResult.diagnostics.isEmpty());
    QCOMPARE(centrelineResult.statistics.stationCount, 10);

    // 2. Read and project clopy01.th2
    QFile th2File(th2Path);
    QVERIFY(th2File.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString th2Contents = QString::fromUtf8(th2File.readAll());

    const QVector<TherionScrap3DModel> scrapModels = TherionScrap3D::projectTh2File(
        th2Path, th2Contents, QStringLiteral("hp"), centrelineResult.stationPositions);

    QCOMPARE(scrapModels.size(), 1);
    const TherionScrap3DModel &scrap = scrapModels.first();

    QCOMPARE(scrap.scrapId, QStringLiteral("clopy01"));
    QVERIFY(scrap.transform.valid);
    QCOMPARE(scrap.transform.matchedAnchorCount, 10);

    // Verify mean and max residual against real cave hand-drawn survey (1985 paper survey):
    // As calculated earlier, mean residual is ~0.034m (3.4 cm) and max is ~0.065m (6.5 cm).
    QVERIFY(scrap.transform.meanResidual < 0.05);
    QVERIFY(scrap.transform.maxResidual < 0.07);

    // Verify wall polylines were extracted
    QVERIFY(!scrap.wallPolylines.isEmpty());

    // Verify 3D ribbon mesh was generated
    QVERIFY(!scrap.meshGroup.vertices.isEmpty());
    QVERIFY(!scrap.meshGroup.triangles.isEmpty());

    // Every triangle index must reference a valid vertex
    const quint32 vertexCount = quint32(scrap.meshGroup.vertices.size());
    for (const auto &tri : scrap.meshGroup.triangles) {
        QVERIFY(tri[0] < vertexCount);
        QVERIFY(tri[1] < vertexCount);
        QVERIFY(tri[2] < vertexCount);
    }
}

void TherionScrap3DTest::integratedComputeWithTh2()
{
    // Test that computing 1302.th (which inputs hp.th and clopy01.th2)
    // automatically creates both the centreline shots AND the scrap 3D mesh!
    const QString thPath = QDir(QStringLiteral(THERION_STUDIO_TEST_FIXTURE_ROOT))
        .filePath(QStringLiteral("projects/map_editor_regression/real_cave_raster/1302.th"));

    QVERIFY(QFile::exists(thPath));

    const TherionCentrelineResult result = computeTherionCentreline({thPath}, defaultTherionCentrelineFileReader());

    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.statistics.stationCount, 10);
    QCOMPARE(result.statistics.shotCount, 9);

    // Mesh group created from clopy01.th2!
    QCOMPARE(result.scene.meshGroups.size(), 1);
    QCOMPARE(result.scrapModels.size(), 1);
    QCOMPARE(result.scrapModels.first().scrapId, QStringLiteral("clopy01"));

    // Verify scene bounds include both centreline and walls
    const ThreeDViewerBounds bounds = result.scene.bounds();
    QVERIFY(bounds.valid);
    QVERIFY(bounds.minimum.x < bounds.maximum.x);
    QVERIFY(bounds.minimum.y < bounds.maximum.y);
    QVERIFY(bounds.minimum.z < bounds.maximum.z);
}

int runTherionScrap3DTest(int argc, char **argv)
{
    TherionScrap3DTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TherionScrap3DTest.moc"
