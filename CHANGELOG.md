# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.6.0`

---

## ✨ Added

- **Audio Master Bus / Sub-Buses & Media Pool Bins (Tier 9E & 9F)** (`VegasReader`): Parses VEGAS Master Bus FX (`e42b0d22…` / `9d74b872…`) into `Project::masterAudioEffects()`, custom sub-buses into `Project::audioBuses()`, and recursive Media Pool bin folders (`135c58aa…` / `31af8cca…` / `32af8cca…`) into `Project::binFolders()` + `MediaAsset::folderId`.
- **Smart Zoom, Secondary Color Corrector & Unmapped FX Reporting (Tier 9D & 9G)** (`VegasReader`, `ProjectFileController`, `VegasImportSummaryDialog`): Translates `smart_zoom` (`LocationAndZoom`) into clip scale/pan and `colorcorrectorsecondary` into `color.grade`, and surfaces skipped third-party/unmapped OFX and VST3 plugins in `VegasImportSummaryDialog`.
- **Video Track-Level FX & Explicit Transition Plugin Mapping (Tier 9B & 9C)** (`VegasReader`): Parses video track-level OFX chains (`e5e4f01a…`) into `Track::effects` and maps VEGAS transition plugins (`ce3b2bfc…` / `cf3b2bfc…`: `flash` → `dip_white`, `linearwipe` → directional `wipe_*`, `push`/`slide`, `barndoor`/`venetianblinds`, `zoom`, `iris`/`clockwipe`, `dip`) to native Drift transition packages.
- **DirectShow Audio Track & Event FX Graph Parser (Tier 9A)** (`VegasReader`): Parses VEGAS DirectShow audio FX graphs (`f49892f7…` on audio tracks, `ce956ab6…` on audio events) for `Sony Track EQ`, `Sony Track Compressor`, `Sony Track Noise Gate`, `Reverb` / `ExpressFX Reverb`, `ExpressFX Graphic EQ`, `ExpressFX Dynamics`, `Simple Delay`, and `Sony Volume`, skipping untouched default track strip no-ops.
