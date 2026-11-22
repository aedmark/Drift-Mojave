# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `1.4.0`

---

## ✨ Added

- **True fractional frame rates & SMPTE timecode (Tier 7A)** (`Project`, `VegasReader`, `Exporter`, `VideoSizeControls`): Native rational `24000/1001` (`23.976`), `30000/1001` (`29.97`), and `60000/1001` (`59.94`) project rates, SMPTE Drop-Frame (`;`) and Non-Drop-Frame (`:`) timecode formatting, and project setup FPS presets.
- **Multi-clip audio waveform auto-sync (Tier 7B)** (`AudioAlignController`, `TimelineClipContextMenu`): Sync 2+ selected clips (dual-system audio or multicam angles) by audio waveform with negative-start shift protection, optional auto-grouping, and camera scratch-audio muting.
- **Selective Event Attributes & batch FX chains (Tier 7C)** (`AppControllerEffects`, `PasteAttributesDialog`, `TimelineClipContextMenu`): Paste Color Grade only, Velocity Envelopes, Audio Switches & Pitch, and Event Switches selectively onto multi-clip selections, plus batch `Apply FX Chain Preset` from the timeline context menu.
- **VEGAS Track Channel Strip & Dialogue Auto-Ducking (Tier 7D)** (`AppControllerEffects`, `AudioAlignController`, `AudioMixerStrip`): One-click per-track Gate + EQ + Compressor channel strip presets (*Default*, *Dialogue Cleanup*, *Broadcast Voiceover*, *Music Bed*) and automatic dialogue-driven volume-envelope ducking from the Mixer FX menu.

## ✅ Fixed

- **AVCHD / MPEG-TS `start_time` offset in ProRes transcoding & media editing** (`MediaEditor.cpp`): Subtract `stream->start_time` when reading frame PTS, seeking, keyframe lookup, and remuxing so MPEG-TS inputs (e.g. Sony `.MTS` with `start_time = 1.0417 s`) no longer prepend ~1.04 s of padded video frames during ProRes transcoding.
- **VEGAS playback rate & multi-take active selection** (`VegasReader.cpp`): Parse `playbackRate` (offset 40 of `kChunkEventTime`) into `Clip::speed` and `Clip::srcOut`, and select the active take via `takeFlags & 0x20` / `activeTakeIndex` instead of always defaulting to take 0.
- **VEGAS Pro compositing, still-image overlays & track solo/mute** (`dda9b077`): Fixed swapped VEGAS track header flags (`0x01` = Solo, `0x02` = Mute/Hidden), added visual `track.solo` support in `FrameCompositor`, and classified `.png`/still-image overlays as `ClipType::Image` (`editFriendly = true`) so alpha channels are preserved.
- **Broadcast `.mxf` playback & ProRes/edit-friendly transcoding** (`dda9b077`, `30e1cf2c`): Bypassed hwaccel for 4:2:2 / 4:4:4 chroma streams (`mpeg2video` 4:2:2 `yuv422p`), normalized `AV_CHANNEL_ORDER_UNSPEC` for `libswresample`, and paired dual-mono PCM L/R streams into stereo in both `ClipReader` and `MediaEditor`.
- **ProRes transcode error reporting** (`30e1cf2c`): `MediaBinController::convertToProRes` now logs and surfaces `transcodeError` and locked-track relink warnings instead of failing silently.

## 🎨 Improved

- **VEGAS Levels mapped to built-in `color.grade`** (`dda9b077`): Translated VEGAS Levels (`InputStart`, `InputEnd`, `Gamma`, `OutputStart`, `OutputEnd`) directly to `color.grade` `curveMaster` and removed the redundant `effects/adjust_levels` package.
- **Translation catalogs updated** (`20bfa144`): Regenerated all 19 `i18n/*.ts` files for the new ProRes error/warning strings.
