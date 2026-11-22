# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.5.0`

---

## ✨ Added

- **Visual Track Folders UI, Folder Mute/Solo & Timeline Find/Jump (Tier 8A)** (`AppControllerTracks`, `TrackHeaderColumn`, `TrackHeaderRow`, `TrackHeaderContextMenu`, `TimelineToolbar`): Collapsing a Track Folder hides child tracks (`trackRowHeight == 0`), adds a clickable folder header chip (`▸`/`▾` + track count) on the first track in each folder, adds folder-level Mute/Solo in the Track Header context menu, and adds a live Timeline Find & Jump (`searchTimeline` / `jumpToTimelineMatch`) dialog across clips, markers, regions, and tracks.
