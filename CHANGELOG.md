# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.6.0`

---

## ✨ Added

- **DirectShow Audio Track & Event FX Graph Parser (Tier 9A)** (`VegasReader`): Parses VEGAS DirectShow audio FX graphs (`f49892f7…` on audio tracks, `ce956ab6…` on audio events) for `Sony Track EQ`, `Sony Track Compressor`, `Sony Track Noise Gate`, `Reverb` / `ExpressFX Reverb`, `ExpressFX Graphic EQ`, `ExpressFX Dynamics`, `Simple Delay`, and `Sony Volume`, skipping untouched default track strip no-ops.
