#pragma once

#include "MapEditorObjectDetailsContext.h"

#include <QStringList>

namespace TherionStudio
{
class MapEditorObjectDetailsEditController final
{
public:
    explicit MapEditorObjectDetailsEditController(MapEditorObjectDetailsContext context);

    void applyObjectOrientationEdits();
    void handleObjectOrientationValueChanged(double value);
    void handleObjectOrientationEnabledToggled(bool checked);
    void handleLinePointLeftSizeEnabledToggled(bool checked);
    void handleLinePointLeftSizeValueChanged(double value);
    void deleteSelectedObjectFromSelection();
    void applyObjectQuickFieldEdits();
    void applyScrapProjectionEdit();
    void updateObjectQuickSubtypeChoices();
    void insertVertexBeforeFromSelectionPanel();
    void insertVertexAfterFromSelectionPanel();
    void splitLineFromSelectionPanel();
    void deleteVertexFromSelectionPanel();
    void handleLinePointPreviousControlToggled(bool checked);
    void handleLinePointSmoothToggled(bool checked);
    void handleLinePointNextControlToggled(bool checked);
    void handleLinePointSegmentSubtypeChanged();
    void handleLinePointAltitudeAutoToggled(bool checked);
    void applyLinePointFlagsEdits();
    void populateScrapScaleFromSourceBounds();
    void applyScrapScaleEdits();
    void handleConfigureObjectSettingsTriggered();
    void handleLineClosedToggled(bool checked);
    void handleLineReversedToggled(bool checked);
    void handleLineOutlineChanged();
    void handleMakeWallPillarClicked();
    void handleObjectClipDisabledToggled(bool checked);
    void handlePointAlignChanged();

private:
    QString tr(const char *text) const;
    const InspectorSymbolCatalog &inspectorSymbolCatalog() const;
    const MapEditorOrientationApplicabilityByCommand &orientationApplicabilityByCommand() const;
    bool applyLinePointStandaloneRowsEdits(const QStringList &updatedRows);

    MapEditorObjectDetailsContext context_;
};
}
