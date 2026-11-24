# Map Editor Performance Plan (2026.7.2)

Status: active. Owner workflow: one measured slice at a time, verified on the real v42 map
(`merged-preview.th2`, ~516 KB, 2,427 features, 35,153 scene items) and by focused QTests.

## Invariants

- Lossless `.th2` round-trip (comments, indentation, newline style, encoding) and live Raw/Blocks/Map sync.
- Every source mutation stays one transaction through `TextEditorSourceTransactionController`.
- Safe full-refresh fallback stays available; widen patch paths only with focused regression coverage.
- Compare optimizations on the same Release build and the same metrics (select press, `details_ms`,
  total-to-idle, `ui_*_ms`, `map-altitude-underlay`). Offscreen numbers are not display frame times.

## Done

| Slice | Result (v42 offscreen) | Coverage |
|---|---|---|
| Handle-index vertex lookup | select press 93-108 -> 41-44 ms | `selectionClicksDoNotScanWholeScene` |
| Scene patch on undo of in-place edits | vertex-drag undo 1353 -> 498 ms | `vertexDragUndoRedoPatchesTheScene` |
| Renumber shifted identities (phase 0) | delete/insert/undo mid-file patch instead of full refresh | `shiftedIdentitiesPatchSceneOnDeleteAndUndo` |
| CRLF/LF/CR preservation on save | lossless newline round-trip | `TextEditorDocumentControllerQTests` |
| Altitude underlay scrap owner by name | delete 10.3 s -> 0.94 s; underlay 9.4 s -> ~0.1 s | `staleIndexLinesKeepScrapNamespaceByName` |

Also done: hover ignores hidden Bezier handles of unselected lines (`wallHoverFollowsDrawnPath`) and partial renders
apply current view metrics (`partialSlopeRenderUsesCurrentViewMetrics`).

Baseline (Release, two offscreen runs, magnifier off, 12 selections): select press median 40.1-40.8 ms,
total-to-idle 185.5-186.0 ms; detail refresh per call median 7.0 ms (max 10.0 ms); widget drawing 57% of
select-phase samples. Inclusive samples overlap and are not elapsed-time attribution.

## Next Slices (priority order)

1. **P1 - Objects tree on undo/delete.** `rebuildInspectorObjectsTree()` appears in 28-35% of undo samples.
   Smallest slice: update only affected rows (or defer rebuild to idle) for patched edits; full rebuild on fallback.
   Measure undo/delete total-to-idle before and after.
2. **P1b - Vertex-selection restore after edits** (115-133 ms). Reuse the projection's line lookup instead of
   re-collecting geometry.
3. **P2 - Underlay fallback for maps outside the project graph.** Cache `resolveStationReference` fallback results
   per index revision so a map with no scrap owner does not pay ~10 s on each refresh.
4. **P3 - ObjectId contract (phase 2a).** Specify identity rules first (duplicates, ambiguous reparse, Raw edits,
   delete + undo restoring the original id) in `SPECIFICATION.md` / `ARCHITECTURE.md`; no code until agreed.
5. **P4 - Incremental projection (phase 2b).** Extend `TherionSourceLogicalDocument` /
   `MapEditorSourceProjectionSnapshot` only (no second DOM). One-object reparse with parity
   (incremental == full), undo/redo and round-trip tests. Target: preparation 350-424 ms -> low ms.
6. **P5 - Lazy handles (phase 3a)**, then **batching static lines (phase 3b)** as separate measured experiments with
   exact hit-test/selection verification. No 16 ms promise without display-backed measurements.

## Open Verification

- Real-map manual acceptance of patch paths (delete, insert, undo/redo, save/reopen).
- Windows Release measurement with magnifier on/off before magnifier work.
