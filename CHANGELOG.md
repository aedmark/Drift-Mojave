# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.7.0`

---

## ✨ Added

- **Snapshot Manager UI & Final `.veg` Stragglers (Tier 10A)** (`ProjectFileController`, `ProjectSnapshotsDialog`, `RecentProjectsPopup`, `VegasReader`): Added `ProjectSnapshotsDialog` (`Manage snapshots…`) with labeled creation, one-click `restoreProjectSnapshot()`, and deletion; added native `.veg` translation for `chromakeyer` (`key.chroma`), `invert`, `ExpressFX Equalization`, and `Pitch Shift`.
- **Multicam Sync & Switcher Polish (Tier 10B)** (`AppControllerTakes`, `InlineMulticamOverlay`, `MulticamWindow`, `TimelineClipContextMenu`): Added `syncAndStartInlineMulticam()`, `jumpToAdjacentMulticamCut()`, `multicamCutCount()`, Prev Cut / Next Cut / Undo buttons, and broadcast Drop-Frame timecode formatting in multicam views.

## 🐛 Fixed

- Cherry-picked 11 upstream stability/build fixes from `CutWire-Studios/Drift` (exit-time GPU/QApplication teardown, VAAPI `vaMapBuffer2` guard, long-lived asset drag owner, microsecond rounding, chroma key brightness keying, ONNX Runtime NuGet/Maven fetch, sectioned grid guard, session-default undo history, and save fallback/200-step cap).
