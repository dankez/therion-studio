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
    void invalidate();
    bool tryApply(const MapEditorSceneRefreshContext &context);

private:
    MapEditorSourceProjectionSnapshotPtr snapshot_;
    QString text_;
    QVector<MapGeometryFeature> features_;
};
}
