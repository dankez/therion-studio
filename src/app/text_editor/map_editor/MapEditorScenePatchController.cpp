#include "MapEditorScenePatchController.h"

#include "MapEditorObjectDetailsLogic.h"
#include "MapEditorPerformanceCounters.h"
#include "MapEditorSceneHandleIndex.h"
#include "MapEditorSceneInternals.h"
#include "MapEditorSceneItems.h"
#include "MapEditorSceneRefreshController.h"
#include "../../../platform/DiagnosticLogging.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QSignalBlocker>
#include <QStringView>

#include <algorithm>
#include <utility>

namespace TherionStudio
{
namespace
{
QVector<QPair<QString, int>> nonGeometryCommands(const MapEditorSourceProjectionSnapshot &snapshot)
{
    QVector<Th2SourceRange> geometryRanges;
    for (const auto &point : snapshot.geometryProjection.points()) {
        geometryRanges.append(point.command.sourceRange);
    }
    for (const auto &line : snapshot.geometryProjection.lines()) {
        geometryRanges.append(line.command.sourceRange);
    }
    for (const auto &area : snapshot.geometryProjection.areas()) {
        geometryRanges.append(area.command.sourceRange);
    }
    std::sort(geometryRanges.begin(), geometryRanges.end(), [](const auto &left, const auto &right) {
        return left.startLineNumber < right.startLineNumber;
    });
    QVector<QPair<QString, int>> commands;
    qsizetype rangeIndex = 0;
    for (const TherionSourceLogicalCommand &command : snapshot.logicalCommands) {
        while (rangeIndex < geometryRanges.size()
               && geometryRanges.at(rangeIndex).endLineNumber < command.startLineNumber) {
            ++rangeIndex;
        }
        if (rangeIndex < geometryRanges.size()
            && geometryRanges.at(rangeIndex).containsLine(command.startLineNumber)) {
            continue;
        }
        if (command.parsed.rawText.trimmed().isEmpty()) {
            continue;
        }
        // A later scrap must retain its source identity, even when its text is unchanged.
        const int identityLine = command.parsed.directive == QStringLiteral("endscrap")
            ? 0 : command.startLineNumber;
        commands.append({command.parsed.rawText, identityLine});
    }
    return commands;
}

void sortBySourceLine(QVector<MapGeometryFeature> *features)
{
    std::sort(features->begin(), features->end(), [](const auto &left, const auto &right) {
        return left.lineNumber < right.lineNumber;
    });
}

// Mirrors the feature collection of the full scene refresh so baseline and next
// projections compare equal when nothing rendered changed.
QVector<MapGeometryFeature> patchGeometryFeatures(const MapEditorSourceProjectionSnapshot &snapshot,
                                                  const MapEditorOrientationApplicabilityByCommand *orientationApplicability)
{
    QVector<MapGeometryFeature> features =
        collectGeometryFeatures(snapshot.geometryProjection, snapshot.logicalCommands);
    if (orientationApplicability != nullptr) {
        QHash<int, const TherionParsedLine *> commandsByLine;
        for (const auto &command : snapshot.logicalCommands) {
            commandsByLine.insert(command.parsed.lineNumber, &command.parsed);
        }
        for (auto &feature : features) {
            const auto *parsed = commandsByLine.value(feature.lineNumber);
            if (feature.kind == MapGeometryFeature::Kind::Point && parsed != nullptr
                && parsed->directive == QStringLiteral("point")
                && isOrientationSupportedForParsedLine(*parsed, *orientationApplicability)) {
                feature.orientationSupported = true;
                feature.orientationDegrees = pointOrientationFromParsedLine(*parsed);
            }
        }
    }
    sortBySourceLine(&features);
    return features;
}

bool isWallFeature(const MapGeometryFeature &feature)
{
    return feature.kind == MapGeometryFeature::Kind::Line
        && feature.label.trimmed().compare(QStringLiteral("wall"), Qt::CaseInsensitive) == 0;
}

int mappedScrapLine(int scrapLine, const QHash<int, int> &scrapLineMap)
{
    return scrapLine > 0 ? scrapLineMap.value(scrapLine, scrapLine) : scrapLine;
}

bool featureMatchesWithShift(const MapGeometryFeature &before,
                             const MapGeometryFeature &after,
                             const QHash<int, int> &scrapLineMap)
{
    if (before.kind != after.kind
        || mappedScrapLine(before.scrapLineNumber, scrapLineMap) != after.scrapLineNumber) {
        return false;
    }
    MapGeometryFeature shifted = before;
    shifted.lineNumber = after.lineNumber;
    shifted.scrapLineNumber = after.scrapLineNumber;
    return shifted == after;
}

void renumberSceneItems(QGraphicsScene *scene,
                        const QHash<int, int> &renumberedLines,
                        const QHash<int, int> &scrapLineMap)
{
    if (scene == nullptr || (renumberedLines.isEmpty() && scrapLineMap.isEmpty())) {
        return;
    }
    for (QGraphicsItem *item : unsortedSceneItems(scene)) {
        if (item == nullptr) {
            continue;
        }
        if (!renumberedLines.isEmpty()) {
            const int oldLine = item->data(kMapSceneLineNumberRole).toInt();
            const auto mapped = oldLine > 0 ? renumberedLines.constFind(oldLine) : renumberedLines.constEnd();
            if (mapped != renumberedLines.constEnd()) {
                const int newLine = mapped.value();
                item->setData(kMapSceneLineNumberRole, newLine);
                if (auto *pointItem = dynamic_cast<MapEditablePointItem *>(item)) {
                    pointItem->setLineNumber(newLine);
                } else if (auto *vertexItem = dynamic_cast<MapEditableGeometryVertexItem *>(item)) {
                    vertexItem->setLineNumber(newLine);
                } else if (auto *sizeHandle = dynamic_cast<MapLinePointSizeHandleItem *>(item)) {
                    sizeHandle->setLineNumber(newLine);
                } else if (auto *orientationHandle = dynamic_cast<MapPointOrientationHandleItem *>(item)) {
                    orientationHandle->setLineNumber(newLine);
                } else {
                    const QString tooltip = item->toolTip();
                    if (!tooltip.isEmpty()) {
                        const QString oldSuffix = QObject::tr("Source line %1").arg(oldLine);
                        if (tooltip.endsWith(oldSuffix)) {
                            const QString newSuffix = QObject::tr("Source line %1").arg(newLine);
                            item->setToolTip(tooltip.left(tooltip.size() - oldSuffix.size()) + newSuffix);
                        }
                    }
                }
            }
        }
        if (!scrapLineMap.isEmpty()) {
            const int oldScrapLine = item->data(kMapSceneStationScrapRole).toInt();
            const auto mappedScrap = oldScrapLine > 0
                ? scrapLineMap.constFind(oldScrapLine)
                : scrapLineMap.constEnd();
            if (mappedScrap != scrapLineMap.constEnd()) {
                item->setData(kMapSceneStationScrapRole, mappedScrap.value());
            }
        }
    }
}

void remapItemsByLine(QHash<int, QGraphicsItem *> *itemsByLine, const QHash<int, int> &renumberedLines)
{
    if (itemsByLine == nullptr || renumberedLines.isEmpty()) {
        return;
    }
    QHash<int, QGraphicsItem *> remapped;
    remapped.reserve(itemsByLine->size());
    for (auto it = itemsByLine->cbegin(); it != itemsByLine->cend(); ++it) {
        const auto mapped = renumberedLines.constFind(it.key());
        remapped.insert(mapped != renumberedLines.constEnd() ? mapped.value() : it.key(), it.value());
    }
    *itemsByLine = std::move(remapped);
}

void remapVertexItemsByKey(QHash<QString, QGraphicsItem *> *vertexItemsByKey, const QHash<int, int> &renumberedLines)
{
    if (vertexItemsByKey == nullptr || renumberedLines.isEmpty()) {
        return;
    }
    QHash<QString, QGraphicsItem *> remapped;
    remapped.reserve(vertexItemsByKey->size());
    for (auto it = vertexItemsByKey->cbegin(); it != vertexItemsByKey->cend(); ++it) {
        const QString &key = it.key();
        const qsizetype colon = key.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            bool ok = false;
            const int oldLine = QStringView(key).left(colon).toInt(&ok);
            if (ok) {
                const auto mapped = renumberedLines.constFind(oldLine);
                if (mapped != renumberedLines.constEnd()) {
                    remapped.insert(QString::number(mapped.value()) + key.mid(colon), it.value());
                    continue;
                }
            }
        }
        remapped.insert(key, it.value());
    }
    *vertexItemsByKey = std::move(remapped);
}

void remapVisibilityLineSet(QSet<int> *lines,
                            const QSet<int> &removedLines,
                            const QSet<int> &renderedLines,
                            const QHash<int, int> &renumberedLines)
{
    if (lines == nullptr || lines->isEmpty() || (removedLines.isEmpty() && renumberedLines.isEmpty())) {
        return;
    }
    QSet<int> remapped;
    remapped.reserve(lines->size());
    for (int line : std::as_const(*lines)) {
        const auto mapped = renumberedLines.constFind(line);
        if (mapped != renumberedLines.constEnd()) {
            remapped.insert(mapped.value());
            continue;
        }
        if (removedLines.contains(line) && !renderedLines.contains(line)) {
            continue;
        }
        remapped.insert(line);
    }
    *lines = std::move(remapped);
}
}

void MapEditorScenePatchController::recordProjection(MapEditorSourceProjectionSnapshotPtr snapshot,
                                                     QString text,
                                                     QVector<MapGeometryFeature> features)
{
    snapshot_ = std::move(snapshot);
    text_ = std::move(text);
    features_ = std::move(features);
    featuresPending_ = false;
    sortBySourceLine(&features_);
}

void MapEditorScenePatchController::recordInPlaceEditProjection(MapEditorSourceProjectionSnapshotPtr snapshot,
                                                                QString text)
{
    snapshot_ = std::move(snapshot);
    text_ = std::move(text);
    features_.clear();
    featuresPending_ = snapshot_ != nullptr;
}

void MapEditorScenePatchController::invalidate()
{
    snapshot_.reset();
    text_.clear();
    features_.clear();
    featuresPending_ = false;
}

bool MapEditorScenePatchController::tryApply(const MapEditorSceneRefreshContext &context)
{
    const bool logTiming = diagnosticLoggingEnabled();
    QElapsedTimer timer;
    if (logTiming) {
        timer.start();
    }
    const auto fallback = [&](const char *reason) {
        ++mapEditorPerformanceCounters().scenePatchFallbacks;
        if (logTiming) {
            qInfo().nospace() << "map-scene-patch outcome=fallback reason=" << reason
                             << " elapsed_ns=" << timer.nsecsElapsed();
        }
        return false;
    };
    if (snapshot_ == nullptr) {
        return fallback("missing-baseline");
    }
    if (context.scene == nullptr
        || *context.scene == nullptr || context.sceneGeneration == nullptr
        || context.itemsByLine == nullptr
        || context.vertexItemsByKey == nullptr || context.styleCatalog == nullptr
        || !context.logicalSource.projectionSnapshotForCurrentDocument || !context.documentText) {
        return fallback("missing-context");
    }
    if (!context.sceneGeneration->isSettled()) {
        return fallback("unsettled-generation");
    }
    const auto next = context.logicalSource.projectionSnapshotForCurrentDocument();
    if (next == nullptr) {
        return fallback("missing-projection");
    }
    const bool changedSourceBounds = next->sourceBounds != snapshot_->sourceBounds;
    const QVector<QPair<QString, int>> beforeNonGeom = nonGeometryCommands(*snapshot_);
    const QVector<QPair<QString, int>> afterNonGeom = nonGeometryCommands(*next);
    if (beforeNonGeom.size() != afterNonGeom.size()) {
        return fallback("changed-nongeometry-or-identities");
    }
    QHash<int, int> scrapLineMap;
    for (qsizetype index = 0; index < beforeNonGeom.size(); ++index) {
        if (beforeNonGeom.at(index).first != afterNonGeom.at(index).first) {
            return fallback("changed-nongeometry-or-identities");
        }
        const int beforeLine = beforeNonGeom.at(index).second;
        const int afterLine = afterNonGeom.at(index).second;
        if (beforeLine > 0 && beforeLine != afterLine) {
            scrapLineMap.insert(beforeLine, afterLine);
        }
    }
    if (featuresPending_) {
        features_ = patchGeometryFeatures(*snapshot_, context.orientationApplicabilityByCommand);
        featuresPending_ = false;
    }
    QVector<MapGeometryFeature> nextFeatures =
        patchGeometryFeatures(*next, context.orientationApplicabilityByCommand);
    const QString nextText = context.documentText();

    const qsizetype beforeCount = features_.size();
    const qsizetype afterCount = nextFeatures.size();
    qsizetype prefix = 0;
    while (prefix < beforeCount && prefix < afterCount && features_.at(prefix) == nextFeatures.at(prefix)) {
        ++prefix;
    }
    qsizetype suffix = 0;
    while (prefix + suffix < beforeCount && prefix + suffix < afterCount
           && featureMatchesWithShift(features_.at(beforeCount - 1 - suffix),
                                      nextFeatures.at(afterCount - 1 - suffix),
                                      scrapLineMap)) {
        ++suffix;
    }

    const qsizetype beforeMidEnd = beforeCount - suffix;
    const qsizetype afterMidEnd = afterCount - suffix;
    constexpr qsizetype kMaxChangedWindowFeatures = 64;
    if ((beforeMidEnd - prefix) + (afterMidEnd - prefix) > kMaxChangedWindowFeatures) {
        return fallback("large-change-window");
    }

    QSet<int> removedLines;
    QSet<int> renderedLines;
    QSet<int> wallChangedScraps;
    QHash<int, int> renumberedLines;
    QHash<int, int> suffixAfterToBefore;
    for (qsizetype index = prefix; index < beforeMidEnd; ++index) {
        const auto &before = features_.at(index);
        removedLines.insert(before.lineNumber);
        if (isWallFeature(before)) {
            wallChangedScraps.insert(mappedScrapLine(before.scrapLineNumber, scrapLineMap));
        }
    }
    for (qsizetype index = prefix; index < afterMidEnd; ++index) {
        const auto &after = nextFeatures.at(index);
        renderedLines.insert(after.lineNumber);
        if (isWallFeature(after)) {
            wallChangedScraps.insert(after.scrapLineNumber);
        }
    }
    for (qsizetype index = 0; index < suffix; ++index) {
        const auto &before = features_.at(beforeCount - 1 - index);
        const auto &after = nextFeatures.at(afterCount - 1 - index);
        if (before.lineNumber != after.lineNumber) {
            renumberedLines.insert(before.lineNumber, after.lineNumber);
        }
        suffixAfterToBefore.insert(after.lineNumber, before.lineNumber);
    }

    QHash<int, int> visibilityRenumberedLines = renumberedLines;
    visibilityRenumberedLines.insert(scrapLineMap);
    if (changedSourceBounds) {
        remapVisibilityLineSet(context.hiddenInspectorObjectLines, removedLines, renderedLines, visibilityRenumberedLines);
        remapVisibilityLineSet(context.fadedInspectorObjectLines, removedLines, renderedLines, visibilityRenumberedLines);
        return fallback("changed-source-bounds");
    }
    QVector<MapGeometryFeature> renderedFeatures;
    for (const auto &feature : std::as_const(nextFeatures)) {
        const bool updateClip = feature.kind == MapGeometryFeature::Kind::Area
            && wallChangedScraps.contains(feature.scrapLineNumber);
        if (updateClip && !renderedLines.contains(feature.lineNumber)) {
            const int oldLine = suffixAfterToBefore.value(feature.lineNumber, feature.lineNumber);
            removedLines.insert(oldLine);
            renumberedLines.remove(oldLine);
            renderedLines.insert(feature.lineNumber);
        }
        if (renderedLines.contains(feature.lineNumber)) {
            renderedFeatures.append(feature);
        }
    }

    const qint64 preparationNs = logTiming ? timer.nsecsElapsed() : 0;
    QGraphicsScene *scene = *context.scene;
    const quint64 generation = context.sceneGeneration->beginRefresh();
    {
        const QSignalBlocker blocker(scene);
        for (int line : std::as_const(removedLines)) {
            removeMapGeometryItemGroupForLine(scene, line, context.itemsByLine, context.vertexItemsByKey);
        }
        if (!renumberedLines.isEmpty() || !scrapLineMap.isEmpty()) {
            renumberSceneItems(scene, renumberedLines, scrapLineMap);
            MapEditorSceneHandleIndex::remapLineNumbers(scene, renumberedLines);
            remapItemsByLine(context.itemsByLine, renumberedLines);
            remapVertexItemsByKey(context.vertexItemsByKey, renumberedLines);
        }
        QHash<int, QGraphicsItem *> addedItems;
        QHash<QString, QGraphicsItem *> addedVertices;
        renderMapWorkspaceScene(scene, QString(), {}, renderedFeatures, next->sourceBounds, false,
                                &addedItems, &addedVertices, {}, {}, context.recordPointGeometryMove,
                                context.recordLineAreaVertexMove, context.recordPointOrientationHandleChange,
                                context.recordLinePointLeftHandleChange, *context.styleCatalog, false, &nextFeatures);
        context.itemsByLine->insert(addedItems);
        context.vertexItemsByKey->insert(addedVertices);
    }
    remapVisibilityLineSet(context.hiddenInspectorObjectLines, removedLines, renderedLines, visibilityRenumberedLines);
    remapVisibilityLineSet(context.fadedInspectorObjectLines, removedLines, renderedLines, visibilityRenumberedLines);
    recordProjection(next, nextText, std::move(nextFeatures));
    const qint64 sceneNs = logTiming ? timer.nsecsElapsed() - preparationNs : 0;
    if (!restoreSceneRefreshSelection(context) && context.selectMapLine && context.currentLineNumber) {
        context.selectMapLine(context.currentLineNumber(), false);
    }
    context.applyInspectorObjectVisibility();
    context.updateGeometrySelectionPresentation();
    context.updateInteractiveDrawPreview();
    context.refreshStatus();
    context.updateCommandSurfaceState();
    context.updateHelpPanel();
    context.refreshObjectDetailsPanel();
    if (context.recordSceneProjectionRefreshCompleted) {
        context.recordSceneProjectionRefreshCompleted(generation);
    }
    mapEditorPerformanceCounters().sceneGeometryPatches.fetch_add(1, std::memory_order_relaxed);
    if (logTiming) {
        qInfo().nospace() << "map-scene-patch outcome=applied changed_features=" << renderedLines.size()
                         << " removed_features=" << removedLines.size()
                         << " renumbered_features=" << renumberedLines.size()
                         << " preparation_ns=" << preparationNs << " scene_ns=" << sceneNs
                         << " ui_ns=" << timer.nsecsElapsed() - preparationNs - sceneNs
                         << " elapsed_ns=" << timer.nsecsElapsed();
    }
    return true;
}
}
