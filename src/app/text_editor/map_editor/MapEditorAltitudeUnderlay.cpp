#include "MapEditorAltitudeUnderlay.h"
#include "MapEditorSceneItems.h"

#include "MapEditorSceneSupport.h"
#include "../../../core/TherionCommandLineModel.h"
#include "../../../core/TherionStationNameRules.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <algorithm>

namespace TherionStudio
{
QHash<int, MapEditorAltitudeScrap> mapEditorAltitudeScraps(
    const MapEditorSourceProjectionSnapshot &snapshot, const QString &filePath,
    const ProjectIndexSnapshot &index, const ScrapAltitudeStations &stations, int strengthPercent)
{
    QHash<int, QString> identities;
    QHash<int, QString> namespaces;
    QHash<int, TherionStationNameTransform> transforms;
    // One pass over the index. Scraps are matched by line and confirmed by name; when a source edit
    // leaves index line numbers stale, a unique scrap name in this file still identifies the owner
    // so station references keep their namespace instead of falling back to a project-wide search.
    struct IndexedScrap
    {
        QString name;
        QString namespacePath;
    };
    QHash<int, IndexedScrap> scrapsByLine;
    QHash<QString, QString> ownerByScrapName;
    QSet<QString> duplicateScrapNames;
    {
        const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
        QHash<QString, bool> sourceIsCurrentFile;
        for (const auto &entry : index.entries) {
            if (entry.kind != ProjectStructureEntryKind::Scrap) {
                continue;
            }
            auto sameFile = sourceIsCurrentFile.constFind(entry.sourceFile);
            if (sameFile == sourceIsCurrentFile.cend()) {
                sameFile = sourceIsCurrentFile.insert(
                    entry.sourceFile, QFileInfo(entry.sourceFile).absoluteFilePath() == absoluteFilePath);
            }
            if (!*sameFile) {
                continue;
            }
            if (!scrapsByLine.contains(entry.lineNumber)) {
                scrapsByLine.insert(entry.lineNumber, {entry.name, entry.namespacePath});
            }
            if (ownerByScrapName.contains(entry.name)) {
                duplicateScrapNames.insert(entry.name);
            } else {
                ownerByScrapName.insert(entry.name, entry.namespacePath);
            }
        }
    }
    for (const auto &command : snapshot.logicalCommands) {
        if (command.metadata.commandName != QStringLiteral("scrap")) {
            continue;
        }
        const QString name = command.positionalArgumentRanges.isEmpty()
            ? QString::number(command.startLineNumber) : command.positionalArgumentRanges.first().text;
        const auto indexedAtLine = scrapsByLine.constFind(command.startLineNumber);
        QString owner;
        if (indexedAtLine != scrapsByLine.cend() && indexedAtLine->name == name) {
            owner = indexedAtLine->namespacePath;
        } else if (ownerByScrapName.contains(name) && !duplicateScrapNames.contains(name)) {
            owner = ownerByScrapName.value(name);
        } else if (indexedAtLine != scrapsByLine.cend()) {
            owner = indexedAtLine->namespacePath;
        }
        identities.insert(command.startLineNumber, filePath + QLatin1Char('|') + name + QLatin1Char('@') + owner);
        namespaces.insert(command.startLineNumber, owner);
        for (const auto &option : command.optionEntryRanges) {
            if (normalizedCommandOptionName(option.key) == QStringLiteral("station-names")
                && option.valueRanges.size() >= 2) {
                const auto part = [](QString value) {
                    return value == QStringLiteral("[]") ? QString() : value;
                };
                transforms.insert(command.startLineNumber,
                    {part(option.valueRanges[0].text), part(option.valueRanges[1].text)});
            }
        }
    }
    QHash<int, QStringList> references;
    for (const auto &point : snapshot.geometryProjection.points()) {
        if (point.command.type != QStringLiteral("station")) {
            continue;
        }
        int scrap = 0;
        for (const auto &block : snapshot.geometryProjection.blockObjects()) {
            if (block.kind == Th2GeometryObjectKind::Scrap
                && block.sourceRange.containsLine(point.command.sourceRange.startLineNumber)) {
                scrap = block.sourceRange.startLineNumber;
                break;
            }
        }
        if (scrap <= 0) {
            continue;
        }
        const QString reference = therionStationReferenceWithScrapNameTransform(
            point.command.optionsByName.value(QStringLiteral("name")).value(0), transforms.value(scrap));
        const auto resolved = ProjectStructureIndex::resolveStationReference(index, reference, namespaces.value(scrap));
        references[scrap].append(resolved.state == ProjectStationReferenceResolutionState::Unique
            ? resolved.qualifiedReference : QStringLiteral("unresolved:") + reference);
    }
    QHash<int, MapEditorAltitudeScrap> result;
    for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
        const auto altitude = scrapAltitudeForReferences(it.value(), references.value(it.key()), stations);
        MapEditorAltitudeScrap scrap;
        scrap.color = QColor::fromHslF(
            altitude.relativeHeightMetres ? (0.78 - 0.72 * altitude.palettePosition) : altitude.palettePosition,
            0.62, 0.72, strengthPercent / 100.0);
        scrap.description = altitude.relativeHeightMetres
            ? QCoreApplication::translate("MapEditorAltitudeUnderlay",
                "Mean height: %1 m relative to entrance %2 (LOX; not an absolute datum)")
                .arg(*altitude.relativeHeightMetres, 0, 'f', 1).arg(stations.entranceReference)
            : QCoreApplication::translate("MapEditorAltitudeUnderlay",
                "Categorical scrap color — altitude unavailable or incomplete (not altitude)");
        result.insert(it.key(), scrap);
    }
    return result;
}

int renderMapEditorAltitudeUnderlay(
    QGraphicsScene *scene, const MapEditorSourceProjectionSnapshot &snapshot,
    const QHash<int, MapEditorAltitudeScrap> &scraps, int *approximateCount)
{
    if (approximateCount != nullptr) {
        *approximateCount = 0;
    }
    if (scene == nullptr) {
        return 0;
    }
    for (auto *item : unsortedSceneItems(scene)) {
        if (item->data(kMapAltitudeUnderlayRole).toBool()) {
            delete item;
        }
    }
    if (scraps.isEmpty()) {
        return 0;
    }
    const auto features = collectGeometryFeatures(snapshot.geometryProjection, snapshot.logicalCommands);
    const auto paths = scrapClipPathsForFeatures(features, snapshot.sourceBounds, mapEditorCanvasPreviewBounds(), true);
    const auto approximatePaths = scrapClipPathsForFeatures(
        features, snapshot.sourceBounds, mapEditorCanvasPreviewBounds(), true, true);
    QList<int> ordered = scraps.keys();
    std::sort(ordered.begin(), ordered.end());
    int rendered = 0;
    for (const int line : ordered) {
        const bool approximate = paths.value(line).isEmpty();
        const QPainterPath path = approximate ? approximatePaths.value(line) : paths.value(line);
        if (path.isEmpty()) {
            continue;
        }
        auto *item = scene->addPath(path, Qt::NoPen, scraps.value(line).color);
        item->setZValue(1.9);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setAcceptHoverEvents(false);
        item->setData(kMapAltitudeUnderlayRole, true);
        item->setData(kMapAltitudeUnderlayApproximateRole, approximate);
        item->setData(kMapSceneLineNumberRole, line);
        item->setToolTip(scraps.value(line).description + (approximate
            ? QLatin1Char('\n') + QCoreApplication::translate("MapEditorAltitudeUnderlay",
                "Approximate passage outline — gaps inferred for preview only.") : QString()));
        if (approximate && approximateCount != nullptr) {
            ++*approximateCount;
        }
        ++rendered;
    }
    return rendered;
}
}
