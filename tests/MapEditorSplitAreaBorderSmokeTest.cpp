#include "../src/app/text_editor/map_editor/MapEditorTab.h"
#include "../src/app/text_editor/map_editor/MapEditorSceneInternals.h"
#include "../src/app/text_editor/map_editor/MapEditorSceneSupport.h"
#include "../src/app/text_editor/TextEditorTab.h"
#include "../src/core/CommandCatalogStore.h"
#include "../src/core/QtFileSystem.h"
#include "FakeSessionStore.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <iostream>

using namespace TherionStudio;

namespace
{
bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void sendMouse(QWidget *widget,
               QEvent::Type type,
               const QPoint &localPos,
               Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    const QPoint globalPos = widget->mapToGlobal(localPos);
    QMouseEvent event(type,
                      QPointF(localPos),
                      QPointF(globalPos),
                      button,
                      buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

QPushButton *findSplitHereButton(QWidget *root)
{
    if (root == nullptr) {
        return nullptr;
    }

    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button != nullptr && button->text().contains(QStringLiteral("Split Here"), Qt::CaseInsensitive)) {
            return button;
        }
    }
    return nullptr;
}

MapEditableGeometryVertexItem *findLineVertexItem(QGraphicsScene *scene, int lineNumber, int sourceVertexIndex)
{
    if (scene == nullptr || lineNumber <= 0 || sourceVertexIndex < 0) {
        return nullptr;
    }

    const QList<QGraphicsItem *> items = scene->items();
    for (QGraphicsItem *item : items) {
        auto *vertex = dynamic_cast<MapEditableGeometryVertexItem *>(item);
        if (vertex == nullptr) {
            continue;
        }
        if (vertex->lineNumber() == lineNumber
            && vertex->vertexIndex() == sourceVertexIndex
            && vertex->geometryKind().startsWith(QStringLiteral("line"))) {
            return vertex;
        }
    }
    return nullptr;
}

QGraphicsPathItem *findPathItemForLine(QGraphicsScene *scene, int lineNumber)
{
    if (scene == nullptr || lineNumber <= 0) {
        return nullptr;
    }

    const QList<QGraphicsItem *> items = scene->items();
    for (QGraphicsItem *item : items) {
        auto *pathItem = dynamic_cast<QGraphicsPathItem *>(item);
        if (pathItem == nullptr || !pathItem->isVisible()) {
            continue;
        }
        if (pathItem->data(kMapSceneLineNumberRole).toInt() != lineNumber) {
            continue;
        }
        if (pathItem->data(kMapSceneSelectionSubtypeRole).toInt() != kMapSceneSelectionSubtypeGeneric) {
            continue;
        }
        return pathItem;
    }

    return nullptr;
}

int runSplitReferencedOpenLineFromSelectionPanelSmoke()
{
    QTemporaryDir tempDir;
    if (!expect(tempDir.isValid(), "Failed to create temporary directory for split-area-border smoke test.")) {
        return 1;
    }

    const QString filePath = tempDir.filePath(QStringLiteral("split_area_border_smoke.th2"));
    QFile file(filePath);
    if (!expect(file.open(QIODevice::WriteOnly | QIODevice::Text),
                "Failed to create temporary TH2 file for split-area-border smoke test.")) {
        return 1;
    }

    const QByteArray th2Contents =
        "encoding utf-8\n"
        "\n"
        "scrap split-smoke -projection plan\n"
        "line border -id border\n"
        "  0 0\n"
        "  50 0\n"
        "  100 0\n"
        "endline\n"
        "area water\n"
        "  border\n"
        "endarea\n"
        "endscrap\n";
    file.write(th2Contents);
    file.close();

    QtFileSystem fileSystem;
    FakeSessionStore sessionStore;
    QMainWindow hostWindow;
    hostWindow.resize(960, 720);
    auto *central = new QWidget(&hostWindow);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *mapTab = new MapEditorTab(fileSystem, sessionStore, CommandCatalogStore(), central);
    layout->addWidget(mapTab);
    hostWindow.setCentralWidget(central);
    hostWindow.show();
    pumpEvents();

    QString errorMessage;
    if (!expect(mapTab->loadFile(filePath, &errorMessage),
                "MapEditorTab failed to load TH2 file for split-area-border smoke test.")) {
        if (!errorMessage.isEmpty()) {
            std::cerr << errorMessage.toStdString() << '\n';
        }
        return 1;
    }
    pumpEvents();

    auto *textEditor = mapTab->findChild<TextEditorTab *>();
    if (!expect(textEditor != nullptr, "Text editor was not found in MapEditorTab.")) {
        return 1;
    }
    auto *mapView = mapTab->findChild<QGraphicsView *>();
    if (!expect(mapView != nullptr && mapView->scene() != nullptr,
                "Map view/scene was not found in map tab.")) {
        return 1;
    }
    mapTab->triggerSelectMode();
    pumpEvents();

    // Select line path first so line-point handles are active/selectable.
    QGraphicsPathItem *linePath = findPathItemForLine(mapView->scene(), 4);
    if (!expect(linePath != nullptr, "Failed to find line path item for split smoke test.")) {
        return 1;
    }
    const QPoint lineViewportPoint = mapView->mapFromScene(linePath->mapToScene(linePath->path().pointAtPercent(0.5)));
    sendMouse(mapView->viewport(), QEvent::MouseButtonPress, lineViewportPoint, Qt::LeftButton, Qt::LeftButton);
    sendMouse(mapView->viewport(), QEvent::MouseButtonRelease, lineViewportPoint, Qt::LeftButton, Qt::NoButton);
    pumpEvents();

    // Move cursor to interior coordinate row and select the matching vertex handle.
    textEditor->goToLineColumn(6, 3);
    pumpEvents();
    MapEditableGeometryVertexItem *vertexItem = findLineVertexItem(mapView->scene(), 4, 1);
    if (!expect(vertexItem != nullptr, "Failed to find interior line vertex for split smoke test.")) {
        return 1;
    }

    auto *splitButton = findSplitHereButton(mapTab);
    if (!expect(splitButton != nullptr, "Split Here button was not found in map object details panel.")) {
        return 1;
    }
    if (!expect(splitButton->isEnabled(),
                "Split Here should be enabled for interior vertex of an open area-referenced border line.")) {
        return 1;
    }

    const QString originalText = mapTab->text();
    splitButton->click();
    pumpEvents();

    const QString expectedText = QStringLiteral(
        "encoding utf-8\n"
        "\n"
        "scrap split-smoke -projection plan\n"
        "line border -id border\n"
        "  0.0 0.0\n"
        "  50.0 0.0\n"
        "endline\n"
        "line border -id border-split-1\n"
        "  50.0 0.0\n"
        "  100.0 0.0\n"
        "endline\n"
        "area water\n"
        "  border border-split-1\n"
        "endarea\n"
        "endscrap\n");
    if (!expect(mapTab->text() == expectedText,
                "Split Here should split open border line and update area border references in source text.")) {
        std::cerr << "Actual source after split:\n" << mapTab->text().toStdString() << '\n';
        return 1;
    }

    if (!expect(mapTab->canUndo(), "Split Here operation should be undoable.")) {
        return 1;
    }
    mapTab->triggerUndo();
    pumpEvents();
    if (!expect(mapTab->text() == originalText, "Undo after Split Here should restore original source text.")) {
        return 1;
    }
    if (!expect(mapTab->canRedo(), "Split Here operation should be redoable after undo.")) {
        return 1;
    }
    mapTab->triggerRedo();
    pumpEvents();
    if (!expect(mapTab->text() == expectedText, "Redo after Split Here should restore split rewrite.")) {
        return 1;
    }

    hostWindow.close();
    pumpEvents();
    return 0;
}

int runHiddenLineControlDoesNotStealPathSelectionSmoke()
{
    QTemporaryDir tempDir;
    if (!expect(tempDir.isValid(), "Failed to create temporary directory for hidden-control selection smoke test.")) {
        return 1;
    }

    const QString filePath = tempDir.filePath(QStringLiteral("hidden_control_selection.th2"));
    QFile file(filePath);
    if (!expect(file.open(QIODevice::WriteOnly | QIODevice::Text),
                "Failed to create temporary TH2 file for hidden-control selection smoke test.")) {
        return 1;
    }

    const QByteArray th2Contents =
        "encoding utf-8\n"
        "\n"
        "scrap hidden-control-selection -projection plan\n"
        "line contour\n"
        "  0 50\n"
        "  100 50\n"
        "endline\n"
        "line wall\n"
        "  40 80\n"
        "  50 50 60 80 80 80\n"
        "endline\n"
        "endscrap\n";
    file.write(th2Contents);
    file.close();

    QtFileSystem fileSystem;
    FakeSessionStore sessionStore;
    QMainWindow hostWindow;
    hostWindow.resize(960, 720);
    auto *central = new QWidget(&hostWindow);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *mapTab = new MapEditorTab(fileSystem, sessionStore, CommandCatalogStore(), central);
    layout->addWidget(mapTab);
    hostWindow.setCentralWidget(central);
    hostWindow.show();
    pumpEvents();

    QString errorMessage;
    if (!expect(mapTab->loadFile(filePath, &errorMessage),
                "MapEditorTab failed to load TH2 file for hidden-control selection smoke test.")) {
        if (!errorMessage.isEmpty()) {
            std::cerr << errorMessage.toStdString() << '\n';
        }
        return 1;
    }
    pumpEvents();

    auto *mapView = mapTab->findChild<QGraphicsView *>();
    if (!expect(mapView != nullptr && mapView->scene() != nullptr,
                "Map view/scene was not found in hidden-control selection smoke test.")) {
        return 1;
    }
    mapTab->triggerSelectMode();
    pumpEvents();

    QGraphicsPathItem *contourPath = findPathItemForLine(mapView->scene(), 4);
    if (!expect(contourPath != nullptr, "Failed to find contour path item for hidden-control selection smoke test.")) {
        return 1;
    }

    mapView->centerOn(contourPath);
    pumpEvents();
    const QPoint contourViewportPoint =
        mapView->mapFromScene(contourPath->mapToScene(contourPath->path().pointAtPercent(0.5)));
    sendMouse(mapView->viewport(), QEvent::MouseButtonPress, contourViewportPoint, Qt::LeftButton, Qt::LeftButton);
    sendMouse(mapView->viewport(), QEvent::MouseButtonRelease, contourViewportPoint, Qt::LeftButton, Qt::NoButton);
    pumpEvents();

    if (!expect(mapTab->currentLineNumber() == 6,
                "Clicking a contour path should not select a hidden wall control point at the same position.")) {
        std::cerr << "Selected source line: " << mapTab->currentLineNumber() << '\n';
        return 1;
    }

    hostWindow.close();
    pumpEvents();
    return 0;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (const int result = runSplitReferencedOpenLineFromSelectionPanelSmoke(); result != 0) {
        return result;
    }
    return runHiddenLineControlDoesNotStealPathSelectionSmoke();
}
