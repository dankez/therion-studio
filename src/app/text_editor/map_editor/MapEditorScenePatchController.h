#pragma once

#include "MapEditorSceneSupport.h"
#include "MapEditorSourceProjectionCache.h"

namespace TherionStudio
{
struct MapEditorSceneRefreshContext;

class MapEditorScenePatchController final
{
public:
    void recordProjection(MapEditorSourceProjectionSnapshotPtr snapshot,
                          QString text,
                          QVector<MapGeometryFeature> features);
    // The scene was edited in place to match this projection; its features are
    // collected only if a later patch needs them.
    void recordInPlaceEditProjection(MapEditorSourceProjectionSnapshotPtr snapshot, QString text);
    void invalidate();
    bool tryApply(const MapEditorSceneRefreshContext &context);

private:
    MapEditorSourceProjectionSnapshotPtr snapshot_;
    QString text_;
    QVector<MapGeometryFeature> features_;
    bool featuresPending_ = false;
};
}
