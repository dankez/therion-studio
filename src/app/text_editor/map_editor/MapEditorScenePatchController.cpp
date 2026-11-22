#include "MapEditorScenePatchController.h"

#include "MapEditorObjectDetailsLogic.h"
#include "MapEditorPerformanceCounters.h"
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
    if (next->sourceBounds != snapshot_->sourceBounds) {
        return fallback("changed-source-bounds");
    }
    if (nonGeometryCommands(*snapshot_) != nonGeometryCommands(*next)) {
        return fallback("changed-nongeometry-or-identities");
    }
    if (featuresPending_) {
        features_ = patchGeometryFeatures(*snapshot_, context.orientationApplicabilityByCommand);
        featuresPending_ = false;
    }
    QVector<MapGeometryFeature> nextFeatures =
        patchGeometryFeatures(*next, context.orientationApplicabilityByCommand);
    const QString nextText = context.documentText();
    QSet<int> changedLines;
    QSet<int> removedLines;
    QVector<MapGeometryFeature> renderedFeatures;
    if (features_.size() == nextFeatures.size()) {
        // Same feature identities: re-render only the changed features. Shifted line
        // numbers keep the full-refresh fallback instead of leaving stale callbacks.
        QSet<int> wallChangedScraps;
        for (qsizetype index = 0; index < nextFeatures.size(); ++index) {
            const auto &before = features_.at(index);
            const auto &after = nextFeatures.at(index);
            if (before.lineNumber != after.lineNumber) {
                return fallback("shifted-feature-identities");
            }
            if (before == after) {
                continue;
            }
            changedLines.insert(after.lineNumber);
            if (isWallFeature(before)) {
                wallChangedScraps.insert(before.scrapLineNumber);
            }
            if (isWallFeature(after)) {
                wallChangedScraps.insert(after.scrapLineNumber);
            }
        }
        removedLines = changedLines;
        for (const auto &feature : std::as_const(nextFeatures)) {
            const bool updateClip = feature.kind == MapGeometryFeature::Kind::Area
                && wallChangedScraps.contains(feature.scrapLineNumber);
            if (updateClip) {
                removedLines.insert(feature.lineNumber);
            }
            if (removedLines.contains(feature.lineNumber)) {
                renderedFeatures.append(feature);
            }
        }
    } else {
        const qsizetype commonCount = qMin(features_.size(), nextFeatures.size());
        for (qsizetype index = 0; index < commonCount; ++index) {
            if (!(features_.at(index) == nextFeatures.at(index))) {
                return fallback("changed-retained-geometry");
            }
        }

        // Only append/remove complete trailing geometry. Shifted identities and arbitrary
        // rewrites retain the full-refresh fallback instead of leaving stale callbacks.
        const bool inserting = nextFeatures.size() > features_.size();
        const auto &tail = inserting ? nextFeatures : features_;
        const auto &tailSnapshot = inserting ? next : snapshot_;
        const auto &firstChanged = tail.at(commonCount);
        const auto ref = tailSnapshot->geometryProjection.objectRefAtLineNumber(firstChanged.lineNumber);
        const auto *command = tailSnapshot->geometryProjection.commandForObjectRef(ref);
        if (command == nullptr || command->sourceRange.startOffset < 0
            || QStringView(text_).left(command->sourceRange.startOffset)
                != QStringView(nextText).left(command->sourceRange.startOffset)) {
            return fallback("changed-source-prefix");
        }
        const int scrapLine = firstChanged.scrapLineNumber;
        bool wallChanged = false;
        for (qsizetype index = commonCount; index < tail.size(); ++index) {
            const auto &feature = tail.at(index);
            if (scrapLine <= 0 || feature.scrapLineNumber != scrapLine) {
                return fallback("cross-scrap-tail");
            }
            changedLines.insert(feature.lineNumber);
            wallChanged |= isWallFeature(feature);
        }
        removedLines = inserting ? QSet<int>{} : changedLines;
        for (const auto &feature : std::as_const(nextFeatures)) {
            const bool updateClip = wallChanged && feature.kind == MapGeometryFeature::Kind::Area
                && feature.scrapLineNumber == scrapLine;
            if (updateClip) {
                removedLines.insert(feature.lineNumber);
            }
            if ((inserting && changedLines.contains(feature.lineNumber)) || updateClip) {
                renderedFeatures.append(feature);
            }
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
        QHash<int, QGraphicsItem *> addedItems;
        QHash<QString, QGraphicsItem *> addedVertices;
        renderMapWorkspaceScene(scene, QString(), {}, renderedFeatures, next->sourceBounds, false,
                                &addedItems, &addedVertices, {}, {}, context.recordPointGeometryMove,
                                context.recordLineAreaVertexMove, context.recordPointOrientationHandleChange,
                                context.recordLinePointLeftHandleChange, *context.styleCatalog, false, &nextFeatures);
        context.itemsByLine->insert(addedItems);
        context.vertexItemsByKey->insert(addedVertices);
    }
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
        qInfo().nospace() << "map-scene-patch outcome=applied changed_features=" << changedLines.size()
                         << " preparation_ns=" << preparationNs << " scene_ns=" << sceneNs
                         << " ui_ns=" << timer.nsecsElapsed() - preparationNs - sceneNs
                         << " elapsed_ns=" << timer.nsecsElapsed();
    }
    return true;
}
}
