# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.4.0`

---

## ✅ Fixed

- **VEGAS Pro compositing, still-image overlays & track solo/mute** (`dda9b077`): Fixed swapped VEGAS track header flags (`0x01` = Solo, `0x02` = Mute/Hidden), added visual `track.solo` support in `FrameCompositor`, and classified `.png`/still-image overlays as `ClipType::Image` (`editFriendly = true`) so alpha channels are preserved.
- **Broadcast `.mxf` playback & ProRes/edit-friendly transcoding** (`dda9b077`, `30e1cf2c`): Bypassed hwaccel for 4:2:2 / 4:4:4 chroma streams (`mpeg2video` 4:2:2 `yuv422p`), normalized `AV_CHANNEL_ORDER_UNSPEC` for `libswresample`, and paired dual-mono PCM L/R streams into stereo in both `ClipReader` and `MediaEditor`.
- **ProRes transcode error reporting** (`30e1cf2c`): `MediaBinController::convertToProRes` now logs and surfaces `transcodeError` and locked-track relink warnings instead of failing silently.

## 🎨 Improved

- **VEGAS Levels mapped to built-in `color.grade`** (`dda9b077`): Translated VEGAS Levels (`InputStart`, `InputEnd`, `Gamma`, `OutputStart`, `OutputEnd`) directly to `color.grade` `curveMaster` and removed the redundant `effects/adjust_levels` package.
- **Translation catalogs updated** (`20bfa144`): Regenerated all 19 `i18n/*.ts` files for the new ProRes error/warning strings.
