# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.5.0`

---

## ✨ Added

- **Persistent Render Queue, Delivery Presets & Embedded Chapters (Tier 8D)** (`AppControllerProject`, `Exporter`, `ExportDialog`): Automatic `QSettings` persistence for queued batch render jobs (`saveRenderQueue` / `loadRenderQueue`), 5 curated `deliveryPresets()`, FFmpeg `AVChapter` muxing from Regions/Markers (`embedChapters`), and one-click `Copy YouTube Chapters` in the Export dialog.
- **Smart Scene Cut Actions & Configurable Silence Trimmer (Tier 8C)** (`SceneDetectController`, `AudioAlignController`, `TimelineClipContextMenu`): One-click `markDetectedScenes()` and `splitClipAtDetectedScenes()` (with single-step Undo), `removeSilenceWithOptions()` with dB threshold / minimum silence / padding control, and timeline context menu presets (*Standard*, *Tight Dialogue*, *Gentle / Natural*).
- **Reference Still Gallery & A/B Split-Screen Wipe Controls (Tier 8B)** (`PreviewController`, `PreviewOptionsMenu`): Multi-still Reference Gallery (`grabReferenceStill`, `addReferenceStillFromFile`, `selectReferenceStill`, `removeReferenceStill`, `exportReferenceStill`, `clearReferenceGallery`), `toggleFxBypassWipe()`, and quick A/B Wipe Source + thumbnail strip in Preview Options.
- **Visual Track Folders UI, Folder Mute/Solo & Timeline Find/Jump (Tier 8A)** (`AppControllerTracks`, `TrackHeaderColumn`, `TrackHeaderRow`, `TrackHeaderContextMenu`, `TimelineToolbar`): Collapsing a Track Folder hides child tracks (`trackRowHeight == 0`), adds a clickable folder header chip (`▸`/`▾` + track count) on the first track in each folder, adds folder-level Mute/Solo in the Track Header context menu, and adds a live Timeline Find & Jump (`searchTimeline` / `jumpToTimelineMatch`) dialog across clips, markers, regions, and tracks.
