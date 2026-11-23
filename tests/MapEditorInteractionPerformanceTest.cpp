#include "../src/app/text_editor/map_editor/MapEditorInspectorData.h"
#include "../src/app/text_editor/map_editor/MapEditorPerformanceCounters.h"
#include "../src/app/text_editor/map_editor/MapEditorSceneHandleIndex.h"
#include "../src/app/text_editor/map_editor/MapEditorSceneInternals.h"
#include "../src/app/text_editor/map_editor/MapEditorSceneSupport.h"
#include "../src/app/text_editor/map_editor/MapEditorTab.h"
#include "../src/app/text_editor/map_editor/MapEditorInteractiveDrawController.h"
#include "../src/app/text_editor/map_editor/MapEditorObjectDetailsEditController.h"
#include "../src/app/text_editor/map_editor/MapEditorScrapVisibility.h"
#include "../src/app/text_editor/TextEditorSourceRewriteController.h"
#include "../src/app/text_editor/TextEditorTab.h"
#include "../src/core/CommandCatalogStore.h"
#include "../src/core/QtFileSystem.h"
#include "../src/core/TherionSourceLogicalDocument.h"
#include "FakeSessionStore.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QTextBlock>
#include <QTemporaryDir>
#include <QTreeView>
#include <QUndoStack>
#include <QtTest/QtTest>

using namespace TherionStudio;

// Work-count regression tests for the interaction hot paths measured in
// PERF-AUDIT-TH2.md (proposals 1, 2, 7, 8). They drive the real MapEditorTab
// and count the expensive work through MapEditorPerformanceCounters.
namespace
{
using LineSet = QSet<int>;

QString describe(const LineSet &lines)
{
    QList<int> sorted(lines.cbegin(), lines.cend());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int line : sorted) {
        parts << QString::number(line);
    }
    return QStringLiteral("{%1}").arg(parts.join(QLatin1Char(',')));
}

QGraphicsPathItem *linePath(QGraphicsScene *scene, int line)
{
    for (QGraphicsItem *item : scene->items()) {
        auto *path = dynamic_cast<QGraphicsPathItem *>(item);
        if (path != nullptr && path->data(kMapItemRole).toInt() == kMapItemGeometryValue
            && path->data(kMapSceneLineNumberRole).toInt() == line
            && path->data(kMapSceneSelectionSubtypeRole).toInt() == kMapSceneSelectionSubtypeGeneric) {
            return path;
        }
    }
    return nullptr;
}

LineSet hoveredLines(QGraphicsScene *scene)
{
    LineSet lines;
    for (QGraphicsItem *item : scene->items()) {
        if (item->data(kMapSceneInteractionHoverRole).toBool()) {
            lines.insert(item->data(kMapSceneLineNumberRole).toInt());
        }
    }
    return lines;
}

LineSet qtSelectedLines(QGraphicsScene *scene)
{
    LineSet lines;
    for (QGraphicsItem *item : scene->selectedItems()) {
        const int line = item->data(kMapSceneLineNumberRole).toInt();
        if (line > 0) {
            lines.insert(line);
        }
    }
    return lines;
}

QPoint pixelOnLine(QGraphicsView *view, int line, qreal percent)
{
    QGraphicsPathItem *path = linePath(view->scene(), line);
    if (path == nullptr) {
        return {};
    }
    return view->mapFromScene(path->mapToScene(path->path().pointAtPercent(percent)));
}

void sendPointer(QWidget *viewport, QEvent::Type type, const QPoint &position,
                 Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(viewport->mapToGlobal(position)), button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &event);
}

void postMove(QWidget *viewport, const QPoint &position)
{
    QCoreApplication::postEvent(viewport,
                                new QMouseEvent(QEvent::MouseMove,
                                                QPointF(position),
                                                QPointF(viewport->mapToGlobal(position)),
                                                Qt::NoButton,
                                                Qt::NoButton,
                                                Qt::NoModifier));
}

void click(QGraphicsView *view, const QPoint &pixel)
{
    sendPointer(view->viewport(), QEvent::MouseMove, pixel, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
    sendPointer(view->viewport(), QEvent::MouseButtonPress, pixel, Qt::LeftButton, Qt::LeftButton);
    sendPointer(view->viewport(), QEvent::MouseButtonRelease, pixel, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
}

int counter(std::atomic<int> &value)
{
    return value.load(std::memory_order_relaxed);
}

const QString kFixture = QStringLiteral(
    "encoding utf-8\n"                       // 1
    "scrap s -projection plan\n"             // 2
    "line wall -id first\n"                  // 3
    "  0 0\n  100 0\nendline\n"              // 4-6
    "line wall -id second\n"                 // 7
    "  0 -60\n  100 -60\nendline\n"          // 8-10
    "line pit -id third\n"                   // 11
    "  0 -120\n  100 -120\nendline\n"        // 12-14
    "line border -id areaborder -close on\n" // 15
    "  150 0\n  250 0\n  250 -120\n  150 -120\n  150 0\nendline\n" // 16-21
    "area water\n  areaborder\nendarea\n"    // 22-24
    "endscrap\n");
}

class MapEditorInteractionPerformanceTest final : public QObject
{
    Q_OBJECT

private:
    struct Harness
    {
        QTemporaryDir directory{QDir::current().filePath(QStringLiteral(".verification/interaction-perf-XXXXXX"))};
        QtFileSystem fileSystem;
        FakeSessionStore sessionStore;
        std::unique_ptr<MapEditorTab> tab;
        QGraphicsView *view = nullptr;
        QUndoStack *stack = nullptr;
    };

    bool open(Harness &harness, const QString &contents = kFixture, int firstLine = 3)
    {
        QDir().mkpath(QStringLiteral(".verification"));
        if (!harness.directory.isValid()) {
            return false;
        }
        const QString path = harness.directory.filePath(QStringLiteral("perf.th2"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(contents.toUtf8());
        file.close();
        harness.tab = std::make_unique<MapEditorTab>(harness.fileSystem, harness.sessionStore, CommandCatalogStore());
        harness.tab->resize(1280, 900);
        harness.tab->show();
        QString error;
        if (!harness.tab->loadFile(path, &error)) {
            qWarning() << error;
            return false;
        }
        harness.view = harness.tab->findChild<QGraphicsView *>(QStringLiteral("mapCanvasView"));
        harness.stack = harness.tab->findChild<QUndoStack *>();
        if (harness.view == nullptr || harness.stack == nullptr) {
            return false;
        }
        QTest::qWait(50);
        harness.tab->triggerFit();
        QTest::qWait(50);
        return harness.view->scene() != nullptr
            && (firstLine == 0 || linePath(harness.view->scene(), firstLine) != nullptr);
    }

    static QCheckBox *closedCheck(Harness &harness)
    {
        for (QCheckBox *check : harness.tab->findChildren<QCheckBox *>()) {
            if (check->text().contains(QStringLiteral("-close"))) {
                return check;
            }
        }
        return nullptr;
    }

private slots:
    void previewRetainsTypedMarkerPoolAcrossSnapAndControlTransitions()
    {
        QGraphicsScene scene;
        QGraphicsView view(&scene);
        QVector<QGraphicsItem *> markers;
        QGraphicsPathItem *path = nullptr;
        QVector<MapEditorInteractiveLineDraftVertex> vertices(2);
        vertices[0].anchorScene = QPointF(20, 20);
        vertices[0].outgoingControlScene = QPointF(30, 25);
        vertices[1].anchorScene = QPointF(60, 40);
        vertices[1].incomingControlScene = QPointF(50, 35);
        const auto curvedVertices = vertices;
        QVector<QPointF> guides{QPointF(10, 10), QPointF(70, 50)};
        const auto snapGuides = guides;
        bool inactive = false;
        bool hover = true;
        bool snap = true;
        QPointF hoverPosition(80, 60);
        MapEditorInteractiveDrawContext context;
        context.scene = &scene;
        context.view = &view;
        context.lineVertices = &vertices;
        context.anchorPressActive = &inactive;
        context.anchorDragActive = &inactive;
        context.hoverActive = &hover;
        context.hoverScenePoint = &hoverPosition;
        context.hoverSnapActive = &snap;
        context.hoverSnapScenePoint = &hoverPosition;
        context.hoverSnapGuideScenePoints = &guides;
        context.previewMarkers = &markers;
        context.previewPath = &path;
        context.drawMode = [] { return MapEditorInteractiveDrawMode::Line; };
        MapEditorInteractiveDrawController controller(context);
        controller.updateInteractiveDrawPreview();
        const auto warmedPool = markers;
        QVERIFY(!markers.isEmpty());
        for (int repeat = 0; repeat != 4; ++repeat) {
            guides.clear();
            snap = false;
            vertices[0].outgoingControlScene.reset();
            vertices[1].incomingControlScene.reset();
            controller.updateInteractiveDrawPreview();
            QCOMPARE(markers, warmedPool);
            QVERIFY(std::any_of(markers.cbegin(), markers.cend(), [](const auto *item) {
                return item != nullptr && !item->isVisible();
            }));
            guides = snapGuides;
            snap = true;
            vertices = curvedVertices;
            controller.updateInteractiveDrawPreview();
            QCOMPARE(markers, warmedPool);
        }
    }

    void snapshotReplayChangesOnlyTheLocalTextRange()
    {
        QPlainTextEdit editor;
        const QString before = QStringLiteral("# retained block\npoint 10 20 label -text \"\U0001F600\"\n# tail\n");
        const QString after = QStringLiteral("# retained block\npoint 10 20 label -text \"\U0001F601\"\n# tail\n");
        editor.setPlainText(before);
        editor.document()->firstBlock().setUserState(123);
        QSignalSpy changes(editor.document(), &QTextDocument::contentsChange);
        TextEditorSourceRewriteController controller({.editor = &editor});
        for (const QString &snapshot : {after, before, after}) {
            changes.clear();
            controller.applySourceSnapshotForTransaction(snapshot);
            QCOMPARE(editor.toPlainText(), snapshot);
            QCOMPARE(editor.document()->firstBlock().userState(), 123);
            QVERIFY(!editor.document()->isUndoAvailable());
            QVERIFY(!editor.document()->isRedoAvailable());
            QCOMPARE(changes.size(), 1);
            const auto change = changes.first();
            QVERIFY(change.at(0).toInt() > before.indexOf(QStringLiteral("point")));
            QCOMPARE(change.at(1).toInt(), 2);
            QCOMPARE(change.at(2).toInt(), 2);
        }
    }

    void trailingDrawInsertAndReplayPatchTheScene_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("point") << QStringLiteral("point");
        QTest::newRow("line") << QStringLiteral("line");
        QTest::newRow("area") << QStringLiteral("area");
    }

    void trailingDrawInsertAndReplayPatchTheScene()
    {
        QFETCH(QString, kind);
        Harness harness;
        const QString before = QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n") + kFixture;
        QVERIFY(open(harness, before, 4));
        auto *retained = linePath(harness.view->scene(), 4);
        retained->setData(9999, QStringLiteral("retained"));
        harness.tab->setMapScrapVisibilityForTest(3, MapEditorScrapVisibilityTarget::ThisScrap,
                                                 MapEditorObjectVisibility::Faded);
        const QTransform transform = harness.view->transform();
        const int fullBefore = counter(mapEditorPerformanceCounters().sceneFullRefreshes);
        const int patchBefore = counter(mapEditorPerformanceCounters().sceneGeometryPatches);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        if (kind == QStringLiteral("point")) {
            harness.tab->triggerAddPoint();
            click(harness.view, harness.view->mapFromScene(
                      harness.tab->testScenePointFromSourcePosition(QPointF(45, -30))));
        } else {
            if (kind == QStringLiteral("area")) {
                harness.tab->triggerAddArea();
            } else {
                harness.tab->triggerAddLine();
            }
            for (const auto &point : {QPointF(25, -25), QPointF(45, -25), QPointF(45, -45)}) {
                MapEditorInteractiveLineDraftVertex vertex;
                vertex.anchorSource = point;
                vertex.anchorScene = harness.tab->testScenePointFromSourcePosition(point);
                harness.tab->testAppendInteractiveDrawLineVertex(vertex);
            }
            harness.tab->triggerCompleteDraft();
        }
        QTRY_VERIFY(!generations.isEmpty());
        const QString after = harness.tab->text();
        QVERIFY(after != before);
        const auto projection = Th2GeometryProjection::fromText(after);
        if (kind == QStringLiteral("point")) {
            QVERIFY(!projection.points().isEmpty());
        } else if (kind == QStringLiteral("area")) {
            QVERIFY(!projection.areas().isEmpty());
        } else {
            QVERIFY(!projection.lines().isEmpty());
        }
        const int insertedLine = kind == QStringLiteral("point")
            ? projection.points().last().command.sourceRange.startLineNumber
            : kind == QStringLiteral("area") ? projection.areas().last().command.sourceRange.startLineNumber
                                            : projection.lines().last().command.sourceRange.startLineNumber;
        QVERIFY(harness.tab->testHasMapSceneItemForLine(insertedLine));
        QCOMPARE(harness.stack->count(), 1);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchBefore + 1);
        QVERIFY(!generations.isEmpty());
        QCOMPARE(linePath(harness.view->scene(), 4)->data(9999).toString(), QStringLiteral("retained"));
        QCOMPARE(retained->opacity(), kMapEditorFadedObjectOpacity);
        QCOMPARE(harness.view->transform(), transform);

        for (int repeat = 0; repeat != 2; ++repeat) {
            generations.clear();
            harness.tab->triggerUndo();
            QTRY_VERIFY(!generations.isEmpty());
            QCOMPARE(harness.tab->text(), before);
            QVERIFY(!harness.tab->testHasMapSceneItemForLine(insertedLine));
            generations.clear();
            harness.tab->triggerRedo();
            QTRY_VERIFY(!generations.isEmpty());
            QCOMPARE(harness.tab->text(), after);
            QVERIFY(harness.tab->testHasMapSceneItemForLine(insertedLine));
        }
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchBefore + 5);
        QCOMPARE(linePath(harness.view->scene(), 4)->data(9999).toString(), QStringLiteral("retained"));
        QCOMPARE(retained->opacity(), kMapEditorFadedObjectOpacity);
        QString error;
        QVERIFY2(harness.tab->save(&error), qPrintable(error));
        QFile saved(harness.tab->filePath());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), after);
    }

    void firstPointInEmptyScrapAndReplayUsePatches()
    {
        Harness harness;
        const QString before = QStringLiteral(
            "##XTHERION## xth_me_area_adjust -10 -140 270 20\n"
            "encoding utf-8\nscrap empty -projection plan\nendscrap\n");
        QVERIFY(open(harness, before, 0));
        QCOMPARE(harness.tab->text(), before);
        const auto initial = Th2GeometryProjection::fromText(before);
        QVERIFY(initial.points().isEmpty());
        QVERIFY(initial.lines().isEmpty());
        const int fullBefore = counter(mapEditorPerformanceCounters().sceneFullRefreshes);
        const int patchesBefore = counter(mapEditorPerformanceCounters().sceneGeometryPatches);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        harness.tab->triggerAddPoint();
        click(harness.view, harness.view->mapFromScene(
                  harness.tab->testScenePointFromSourcePosition(QPointF(45, -30))));
        QTRY_VERIFY(!generations.isEmpty());
        const QString after = harness.tab->text();
        const auto projection = Th2GeometryProjection::fromText(after);
        QCOMPARE(projection.points().size(), 1);
        const int insertedLine = projection.points().first().command.sourceRange.startLineNumber;
        QVERIFY(harness.tab->testHasMapSceneItemForLine(insertedLine));
        QCOMPARE(harness.stack->count(), 1);
        generations.clear();
        harness.tab->triggerUndo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), before);
        QVERIFY(!harness.tab->testHasMapSceneItemForLine(insertedLine));
        generations.clear();
        harness.tab->triggerRedo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), after);
        QVERIFY(harness.tab->testHasMapSceneItemForLine(insertedLine));
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchesBefore + 3);
    }

    void earlierScrapInsertPatchesWithoutStaleSourceIdentities()
    {
        Harness harness;
        const QString before = QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n")
            + kFixture + QStringLiteral("scrap later -projection plan\npoint 30 -30 station -name A\nendscrap\n");
        QVERIFY(open(harness, before, 4));
        const auto initialProjection = Th2GeometryProjection::fromText(before);
        QCOMPARE(initialProjection.points().size(), 1);
        const int initialStationLine = initialProjection.points().first().command.sourceRange.startLineNumber;
        QVERIFY(harness.tab->testHasMapSceneItemForLine(initialStationLine));
        harness.tab->triggerAddLine();
        auto *target = harness.tab->findChild<QComboBox *>(QStringLiteral("mapObjectQuickTargetScrapCombo"));
        QVERIFY(target != nullptr);
        const int index = target->findText(QStringLiteral("s"));
        QVERIFY(index >= 0);
        target->setCurrentIndex(index);
        emit target->textActivated(target->itemText(index));
        for (const auto &point : {QPointF(25, -25), QPointF(45, -25)}) {
            MapEditorInteractiveLineDraftVertex vertex;
            vertex.anchorSource = point;
            vertex.anchorScene = harness.tab->testScenePointFromSourcePosition(point);
            harness.tab->testAppendInteractiveDrawLineVertex(vertex);
        }
        const int fullBefore = counter(mapEditorPerformanceCounters().sceneFullRefreshes);
        const int patchBefore = counter(mapEditorPerformanceCounters().sceneGeometryPatches);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        harness.tab->triggerCompleteDraft();
        QTRY_VERIFY(!generations.isEmpty());
        const QString after = harness.tab->text();
        QVERIFY(after != before);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchBefore + 1);
        const auto projection = Th2GeometryProjection::fromText(after);
        QCOMPARE(projection.points().size(), 1);
        const int shiftedStationLine = projection.points().first().command.sourceRange.startLineNumber;
        QVERIFY(shiftedStationLine > initialStationLine);
        QVERIFY(harness.tab->testHasMapSceneItemForLine(shiftedStationLine));
        generations.clear();
        harness.tab->triggerUndo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), before);
        QVERIFY(harness.tab->testHasMapSceneItemForLine(initialStationLine));
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchBefore + 2);
        generations.clear();
        harness.tab->triggerRedo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), after);
        QVERIFY(harness.tab->testHasMapSceneItemForLine(shiftedStationLine));
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchBefore + 3);
    }

    void shiftedVertexDragChangesTheIntendedObject()
    {
        Harness harness;
        const QString before = QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n") + kFixture;
        QVERIFY(open(harness, before, 4));
        harness.tab->goToLine(4);
        auto *deleteObject = harness.tab->findChild<QPushButton *>(QStringLiteral("mapDeleteObjectButton"));
        if (deleteObject == nullptr) {
            for (auto *button : harness.tab->findChildren<QPushButton *>()) {
                if (button->text() == QStringLiteral("Delete Object")) {
                    deleteObject = button;
                    break;
                }
            }
        }
        QVERIFY(deleteObject != nullptr);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        deleteObject->click();
        QTRY_VERIFY(!generations.isEmpty());
        const QString afterDelete = harness.tab->text();
        QVERIFY(afterDelete != before);

        const auto projection = Th2GeometryProjection::fromText(afterDelete);
        QVERIFY(projection.lines().size() >= 2);
        const int shiftedLine = projection.lines().first().command.sourceRange.startLineNumber;
        harness.tab->goToLine(shiftedLine);
        QTest::qWait(30);
        MapEditableGeometryVertexItem *anchor = nullptr;
        for (QGraphicsItem *item : harness.view->scene()->items()) {
            auto *candidate = dynamic_cast<MapEditableGeometryVertexItem *>(item);
            if (candidate != nullptr && candidate->lineNumber() == shiftedLine
                && candidate->geometryKind() == QStringLiteral("line") && candidate->vertexIndex() == 0) {
                anchor = candidate;
                break;
            }
        }
        QVERIFY(anchor != nullptr);
        harness.view->centerOn(anchor->scenePos());
        QWidget *viewport = harness.view->viewport();
        const QPoint start = harness.view->mapFromScene(anchor->scenePos());
        sendPointer(viewport, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
        QTest::qWait(30);
        sendPointer(viewport, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 5; ++step) {
            sendPointer(viewport, QEvent::MouseMove, start + QPoint(step * 6, step * 2), Qt::NoButton, Qt::LeftButton);
            QCoreApplication::processEvents();
        }
        sendPointer(viewport, QEvent::MouseButtonRelease, start + QPoint(30, 10), Qt::LeftButton, Qt::NoButton);
        QTRY_VERIFY(harness.tab->text() != afterDelete);
        const QString afterDrag = harness.tab->text();
        const QString secondHeader = QStringLiteral("line wall -id second\n");
        const QString thirdHeader = QStringLiteral("line pit -id third\n");
        QVERIFY(afterDelete.contains(secondHeader));
        QVERIFY(afterDrag.contains(secondHeader));
        QCOMPARE(afterDrag.section(thirdHeader, 1), afterDelete.section(thirdHeader, 1));
        QVERIFY(afterDrag.section(secondHeader, 1, 1).section(thirdHeader, 0, 0)
                != afterDelete.section(secondHeader, 1, 1).section(thirdHeader, 0, 0));
        harness.tab->triggerUndo();
        QTRY_COMPARE(harness.tab->text(), afterDelete);
        harness.tab->triggerRedo();
        QTRY_COMPARE(harness.tab->text(), afterDrag);
    }

    void shiftedVisibilitySurvivesDeleteUndoRedo_data()
    {
        QTest::addColumn<bool>("deleteFromTree");
        QTest::addColumn<bool>("changedBounds");
        QTest::newRow("details-patch") << false << false;
        QTest::newRow("objects-tree-patch") << true << false;
        QTest::newRow("details-full-refresh") << false << true;
        QTest::newRow("objects-tree-full-refresh") << true << true;
    }

    void shiftedVisibilitySurvivesDeleteUndoRedo()
    {
        QFETCH(bool, deleteFromTree);
        QFETCH(bool, changedBounds);
        Harness harness;
        QString fixture = kFixture;
        if (changedBounds) {
            fixture.replace(QStringLiteral("  0 0\n  100 0\nendline\n"),
                            QStringLiteral("  -1000 0\n  100 0\nendline\n"));
        }
        const QString before = (changedBounds
                                    ? QStringLiteral("# automatic bounds\n")
                                    : QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n")) + fixture
            + QStringLiteral("scrap later -projection plan\npoint 200 -60 station -name 1\nendscrap\n");
        QVERIFY(open(harness, before, 4));
        auto *tree = harness.tab->findChild<QTreeView *>(QStringLiteral("mapObjectsTree"));
        QVERIFY(tree != nullptr);
        const auto visibilityIndex = [&](int lineNumber) {
            const QAbstractItemModel *model = tree->model();
            const auto find = [&](auto &&self, const QModelIndex &parent) -> QModelIndex {
                for (int row = 0; row < model->rowCount(parent); ++row) {
                    const QModelIndex name = model->index(row, 0, parent);
                    if (name.data(Qt::UserRole + 700).toInt() == lineNumber) {
                        return model->index(row, 2, parent);
                    }
                    const QModelIndex child = self(self, name);
                    if (child.isValid()) {
                        return child;
                    }
                }
                return {};
            };
            return find(find, {});
        };
        const auto clickVisibility = [&](int lineNumber) {
            const QModelIndex index = visibilityIndex(lineNumber);
            QVERIFY(index.isValid());
            emit tree->clicked(index);
            QCoreApplication::processEvents();
        };
        const auto checkVisibility = [&](const char *stage, int fadedLine, int hiddenLine, int hiddenArea, int hiddenScrap) {
            QGraphicsPathItem *faded = linePath(harness.view->scene(), fadedLine);
            QVERIFY(faded != nullptr);
            QVERIFY(faded->isVisible());
            QVERIFY2(faded->opacity() == kMapEditorFadedObjectOpacity, stage);
            const auto handles = MapEditorSceneHandleIndex::itemsForLine(harness.view->scene(), fadedLine);
            QVERIFY2(!handles.isEmpty(), stage);
            bool foundVertex = false;
            for (QGraphicsItem *handle : handles) {
                QCOMPARE(handle->data(kMapSceneLineNumberRole).toInt(), fadedLine);
                if (auto *vertex = dynamic_cast<MapEditableGeometryVertexItem *>(handle)) {
                    QCOMPARE(vertex->lineNumber(), fadedLine);
                    foundVertex = true;
                }
            }
            QVERIFY2(foundVertex, stage);
            QVERIFY(harness.tab->hiddenObjectLines().contains(hiddenLine));
            QGraphicsPathItem *hidden = linePath(harness.view->scene(), hiddenLine);
            QVERIFY(hidden != nullptr);
            QVERIFY(!hidden->isVisible());
            QVERIFY2(harness.tab->hiddenObjectLines().contains(hiddenArea), stage);
            QVERIFY2(harness.tab->hiddenObjectLines().contains(hiddenScrap), stage);
            bool foundArea = false;
            for (QGraphicsItem *item : harness.view->scene()->items()) {
                if (item->data(kMapSceneLineNumberRole).toInt() == hiddenArea
                    && item->data(kMapSceneSelectionSubtypeRole).toInt() == kMapSceneSelectionSubtypeAreaFill) {
                    foundArea = true;
                    QVERIFY2(!item->isVisible(), stage);
                }
            }
            QVERIFY2(foundArea, stage);
        };
        clickVisibility(8);
        clickVisibility(12);
        clickVisibility(12);
        clickVisibility(23);
        clickVisibility(23);
        clickVisibility(27);
        clickVisibility(27);
        checkVisibility("before delete", 8, 12, 23, 27);

        harness.tab->goToLine(4);
        const int fullBefore = counter(mapEditorPerformanceCounters().sceneFullRefreshes);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        if (deleteFromTree) {
            const QModelIndex index = visibilityIndex(4);
            QVERIFY(index.isValid());
            emit tree->clicked(index.siblingAtColumn(3));
        } else {
            QPushButton *deleteObject = nullptr;
            for (auto *button : harness.tab->findChildren<QPushButton *>()) {
                if (button->text() == QStringLiteral("Delete Object")) {
                    deleteObject = button;
                    break;
                }
            }
            QVERIFY(deleteObject != nullptr);
            deleteObject->click();
        }
        QTRY_VERIFY(!generations.isEmpty());
        const QString afterDelete = harness.tab->text();
        QVERIFY(afterDelete != before);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore + (changedBounds ? 1 : 0));
        checkVisibility("after delete", 4, 8, 19, 23);

        generations.clear();
        harness.tab->triggerUndo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), before);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore + (changedBounds ? 2 : 0));
        checkVisibility("after undo", 8, 12, 23, 27);
        generations.clear();
        harness.tab->triggerRedo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), afterDelete);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneFullRefreshes), fullBefore + (changedBounds ? 3 : 0));
        checkVisibility("after redo", 4, 8, 19, 23);
    }

    void rejectedDeleteRestoresVisibility()
    {
        Harness harness;
        const QString before = QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n") + kFixture;
        QVERIFY(open(harness, before, 4));
        auto *textEditor = harness.tab->findChild<TextEditorTab *>();
        QVERIFY(textEditor != nullptr);

        int selectedLine = 4;
        QSet<int> hidden{4, 12};
        QSet<int> faded{8};
        const QSet<int> hiddenBefore = hidden;
        const QSet<int> fadedBefore = faded;
        bool transactionCalled = false;
        MapEditorObjectDetailsContext context;
        context.textEditor = textEditor;
        context.selectedObjectLineNumber = &selectedLine;
        context.hiddenInspectorObjectLines = &hidden;
        context.fadedInspectorObjectLines = &faded;
        context.logicalSource.logicalCommandsForCurrentDocument = [textEditor]() {
            return TherionSourceLogicalDocument::fromText(textEditor->text()).commands();
        };
        context.applySourceTextChangeWithSnapshot =
            [&transactionCalled](const QString &, const QString &, const QString &, int, std::function<void()>) {
                transactionCalled = true;
                return TextEditorSourceTransactionResult::Stale;
            };

        MapEditorObjectDetailsEditController(context).deleteSelectedObjectFromSelection();
        QVERIFY(transactionCalled);
        QCOMPARE(textEditor->text(), before);
        QCOMPARE(hidden, hiddenBefore);
        QCOMPARE(faded, fadedBefore);
    }

    void patchedDeletePreservesSourceOnSaveUndoRedo()
    {
        Harness harness;
        const QString before = QStringLiteral("##XTHERION## xth_me_area_adjust -10 -140 270 20\n") + kFixture
            + QStringLiteral("# zachovan\u00fd koment\u00e1r\n");
        const QString removed = QStringLiteral("line wall -id first\n  0 0\n  100 0\nendline\n");
        QString after = before;
        QVERIFY(after.contains(removed));
        after.remove(removed);
        QVERIFY(open(harness, before, 4));
        harness.tab->goToLine(4);
        QPushButton *deleteObject = nullptr;
        for (auto *button : harness.tab->findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Delete Object")) {
                deleteObject = button;
                break;
            }
        }
        QVERIFY(deleteObject != nullptr);
        const int patchesBefore = counter(mapEditorPerformanceCounters().sceneGeometryPatches);
        QSignalSpy generations(harness.tab.get(), &MapEditorTab::sceneProjectionRefreshCompleted);
        deleteObject->click();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), after);
        QCOMPARE(counter(mapEditorPerformanceCounters().sceneGeometryPatches), patchesBefore + 1);
        const auto verifySaved = [&](const QString &expected) {
            QString error;
            QVERIFY2(harness.tab->save(&error), qPrintable(error));
            QFile saved(harness.tab->filePath());
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(saved.readAll(), expected.toUtf8());
        };
        verifySaved(after);
        generations.clear();
        harness.tab->triggerUndo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), before);
        verifySaved(before);
        generations.clear();
        harness.tab->triggerRedo();
        QTRY_VERIFY(!generations.isEmpty());
        QCOMPARE(harness.tab->text(), after);
        verifySaved(after);
    }

    void draftUndoRedoDoesNotEnumerateScene()
    {
        QGraphicsScene scene;
        scene.setSceneRect(0, 0, 12000, 2000);
        for (int index = 0; index < 10000; ++index) {
            scene.addRect(QRectF(index, 0, 1, 1));
        }
        QVector<QGraphicsRectItem *> drafts;
        auto *draft = new MapDraftGeometryItem(1, DraftGeometryKind::Point);
        QUndoStack stack;
        auto &scans = mapEditorPerformanceCounters().fullSceneItemScans;
        const int before = counter(scans);
        stack.push(createMapDraftAddCommand(&scene, &drafts, draft, QPointF(500, 500)));
        for (int index = 0; index < 20; ++index) {
            QCOMPARE(draft->scene(), &scene);
            QCOMPARE(drafts.size(), 1);
            stack.undo();
            QVERIFY(draft->scene() == nullptr);
            QVERIFY(drafts.isEmpty());
            stack.redo();
        }
        QCOMPARE(draft->scene(), &scene);
        QCOMPARE(draft->pos(), QPointF(500, 500));
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(counter(scans) - before, 0);
    }

    // Proposal 1: the tab used to filter viewport events both as an application
    // filter and as a viewport/view object filter, so every pointer event ran
    // the full input controller (hover hit test, press hit test) twice.
    void eachViewportPointerEventIsDispatchedOnce()
    {
        Harness harness;
        QVERIFY(open(harness));
        QWidget *viewport = harness.view->viewport();
        const QPoint onLine = pixelOnLine(harness.view, 3, 0.5);
        const QPoint empty(viewport->width() - 20, viewport->height() - 20);

        auto &dispatches = mapEditorPerformanceCounters().viewportPointerDispatches;
        int before = counter(dispatches);
        sendPointer(viewport, QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
        QCOMPARE(counter(dispatches) - before, 1);

        before = counter(dispatches);
        sendPointer(viewport, QEvent::MouseButtonPress, onLine, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(counter(dispatches) - before, 1);
        before = counter(dispatches);
        sendPointer(viewport, QEvent::MouseButtonRelease, onLine, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(counter(dispatches) - before, 1);
        QCoreApplication::processEvents();
        // Behaviour is unchanged: the click still selects exactly the hit line.
        QTRY_COMPARE_WITH_TIMEOUT(describe(qtSelectedLines(harness.view->scene())), describe({3}), 1000);
    }

    // Proposal 2: resolving the selected line for the object details panel must
    // reuse the per-revision geometry instead of re-collecting the whole
    // document, and must still follow text edits and undo.
    void selectionDetailsDoNotRebuildWholeDocumentGeometry()
    {
        Harness harness;
        QVERIFY(open(harness));
        auto &builds = mapEditorPerformanceCounters().wholeDocumentGeometryBuilds;

        int before = counter(builds);
        click(harness.view, pixelOnLine(harness.view, 3, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(describe(qtSelectedLines(harness.view->scene())), describe({3}), 1000);
        QCOMPARE(counter(builds) - before, 0);

        before = counter(builds);
        click(harness.view, pixelOnLine(harness.view, 7, 0.5));
        QTRY_COMPARE_WITH_TIMEOUT(describe(qtSelectedLines(harness.view->scene())), describe({7}), 1000);
        QCOMPARE(counter(builds) - before, 0);

        QCheckBox *closed = closedCheck(harness);
        QVERIFY(closed != nullptr);
        QTRY_VERIFY(closed->isVisible());
        QVERIFY(!closed->isChecked());

        // A source edit invalidates the cached lookup: the details panel must
        // report the edited line state, and undo must restore it.
        closed->click();
        QTRY_VERIFY_WITH_TIMEOUT(harness.tab->text().contains(QStringLiteral("line wall -id second -close on")), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(closedCheck(harness)->isChecked(), 2000);
        harness.tab->triggerUndo();
        QTRY_VERIFY_WITH_TIMEOUT(!harness.tab->text().contains(QStringLiteral("-id second -close")), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!closedCheck(harness)->isChecked(), 2000);
        QCOMPARE(harness.tab->text(), kFixture);
    }

    // Proposal 7: inspector tree rebuilds used to parse and rasterise the
    // Lucide SVG icons twice per row on every rebuild.
    void inspectorIconsAreCachedAcrossTreeRebuilds()
    {
        Harness harness;
        QVERIFY(open(harness));
        auto *tree = harness.tab->findChild<QTreeView *>(QStringLiteral("mapObjectsTree"));
        QVERIFY(tree != nullptr && tree->model() != nullptr);
        QTRY_VERIFY(tree->model()->rowCount() > 0);
        auto &renders = mapEditorPerformanceCounters().inspectorIconRenders;

        // Edit + undo rebuild the Objects tree twice through the production
        // source-driven refresh path.
        QPlainTextEdit *sourceEditor = nullptr;
        for (QPlainTextEdit *edit : harness.tab->findChildren<QPlainTextEdit *>()) {
            if (edit->toPlainText() == kFixture) {
                sourceEditor = edit;
            }
        }
        QVERIFY(sourceEditor != nullptr);
        const int before = counter(renders);
        sourceEditor->moveCursor(QTextCursor::End);
        sourceEditor->insertPlainText(QStringLiteral("\n# rebuild inspector\n"));
        QTest::qWait(400);
        sourceEditor->moveCursor(QTextCursor::End);
        sourceEditor->insertPlainText(QStringLiteral("# rebuild inspector again\n"));
        QTest::qWait(400);
        QTRY_VERIFY(tree->model()->rowCount() > 0);
        QCOMPARE(counter(renders) - before, 0);

        // Theme change must not reuse icons rendered for the old palette.
        const QPalette originalPalette = QApplication::palette();
        const int themeBefore = counter(renders);
        const QIcon cached = inspectorActionIcon(QStringLiteral("eye"));
        QCOMPARE(counter(renders) - themeBefore, 0);
        QPalette changed = originalPalette;
        changed.setColor(QPalette::Text, QColor(QStringLiteral("#ff00ff")));
        QApplication::setPalette(changed);
        const QIcon recoloured = inspectorActionIcon(QStringLiteral("eye"));
        QVERIFY(counter(renders) - themeBefore > 0);
        QVERIFY(cached.cacheKey() != recoloured.cacheKey());
        QApplication::setPalette(originalPalette);
    }

    // Proposal 8: no hover hit testing while a button is held (drag), at most
    // one hover hit test per frame for a burst of moves, hover correct after
    // the drag and cleared on leave; the magnifier must not re-derive the
    // draft source bounds (whole-text parse) on every pointer move.
    void hoverIsSkippedDuringDragAndCoalescedPerFrame()
    {
        Harness harness;
        QVERIFY(open(harness));
        QWidget *viewport = harness.view->viewport();
        auto &hoverTests = mapEditorPerformanceCounters().hoverHitTests;
        const QPoint empty(viewport->width() - 30, viewport->height() - 30);
        const QPoint line7 = pixelOnLine(harness.view, 7, 0.5);
        const QPoint line11 = pixelOnLine(harness.view, 11, 0.5);

        // Drag from empty canvas over line 7 with the left button held.
        sendPointer(viewport, QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
        QTest::qWait(30);
        int before = counter(hoverTests);
        sendPointer(viewport, QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
        constexpr int steps = 8;
        for (int step = 1; step <= steps; ++step) {
            const QPoint position = empty + ((line7 - empty) * step) / steps;
            sendPointer(viewport, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
            QTest::qWait(20);
        }
        QCOMPARE(counter(hoverTests) - before, 0);
        sendPointer(viewport, QEvent::MouseButtonRelease, line7, Qt::LeftButton, Qt::NoButton);
        // After the drag ends the hover reflects the line under the pointer.
        QTRY_COMPARE_WITH_TIMEOUT(describe(hoveredLines(harness.view->scene())), describe({7}), 1000);

        // A burst of queued moves is coalesced to one hit test for the frame.
        QTest::qWait(40);
        before = counter(hoverTests);
        for (int step = 1; step <= 20; ++step) {
            postMove(viewport, line7 + ((line11 - line7) * step) / 20);
        }
        QCoreApplication::processEvents();
        QVERIFY2(counter(hoverTests) - before <= 1,
                 qPrintable(QStringLiteral("hover hit tests for 20 queued moves: %1").arg(counter(hoverTests) - before)));
        QTRY_COMPARE_WITH_TIMEOUT(describe(hoveredLines(harness.view->scene())), describe({11}), 1000);
        QVERIFY(counter(hoverTests) - before <= 2);

        // Leaving the viewport clears hover, and no stale coalesced update
        // re-applies it afterwards.
        postMove(viewport, line7);
        QEvent leave(QEvent::Leave);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QCoreApplication::sendEvent(viewport, &leave);
        QTest::qWait(60);
        QCOMPARE(describe(hoveredLines(harness.view->scene())), describe({}));

        // Magnifier: one draft-bounds derivation per document revision at most.
        harness.tab->setMagnifierEnabled(true);
        QTest::qWait(30);
        auto &boundsComputations = mapEditorPerformanceCounters().draftSourceBoundsComputations;
        const int boundsBefore = counter(boundsComputations);
        auto *magnifier = harness.tab->findChild<QWidget *>(QStringLiteral("mapMagnifierOverlay"));
        for (int step = 0; step < 8; ++step) {
            sendPointer(viewport, QEvent::MouseMove, line7 + QPoint(step * 3, 0), Qt::NoButton, Qt::NoButton);
            QTest::qWait(25);
        }
        if (magnifier != nullptr) {
            QVERIFY(magnifier->isVisible());
        }
        QVERIFY2(counter(boundsComputations) - boundsBefore <= 1,
                 qPrintable(QStringLiteral("draft bounds computations for 8 magnifier frames: %1")
                                .arg(counter(boundsComputations) - boundsBefore)));
    }
};

int runMapEditorInteractionPerformanceTest(int argc, char **argv)
{
    MapEditorInteractionPerformanceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "MapEditorInteractionPerformanceTest.moc"
