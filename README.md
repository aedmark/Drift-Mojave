<p align="center">
  <img src="Drift_icon.png" alt="Drift Mojave icon" width="128" height="128">
</p>

<h1 align="center">Drift Mojave</h1>

<p align="center">
  <strong>“What happens in the timeline, stays in the timeline.”</strong>
</p>

<p align="center">
  <a href="https://github.com/aedmark/Drift-Mojave/releases/tag/v1.10.0"><img src="https://img.shields.io/badge/version-1.10.0--mojave-feb504?style=flat&labelColor=28272f" alt="Version 1.10.0 Mojave"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-feb504?style=flat&labelColor=28272f" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Platform: Linux | Windows | macOS | Android">
  <img src="https://img.shields.io/badge/interchange-Sony%20%2F%20MAGIX%20VEGAS%20(.veg)-feb504?style=flat&labelColor=28272f" alt="VEGAS Pro Interchange">
</p>

<p align="center">
  <a href="https://oopismcgoopis.com/drift-mojave/">Website</a> ·
  <a href="https://github.com/aedmark/Drift-Mojave/releases/tag/v1.10.0">Download v1.10.0</a> ·
  <a href="docs/ROADMAP.md">Roadmap</a> ·
  <a href="#building-from-source">Build from Source</a>
</p>

---

**Drift Mojave** is a high-velocity, cross-platform fork of [Drift](https://github.com/CutWire-Studios/Drift) built for editors who want native **Sony / MAGIX VEGAS Pro (`.veg`)** compatibility, edit-friendly **Apple ProRes 422** intra-frame workflows, and classic VEGAS timeline muscle memory—paired with Drift's GPU compositor, 3D scene layers, on-device Whisper AI, beat-matched music remixing, and MCP agent automation.

No subscriptions, no accounts, no watermarks, no cloud lock-in.

---

## Download Prebuilt Binaries (`v1.10.0`)

| Platform | Package | Link |
|---|---|---|
| **Linux (x86_64)** | AppImage + `.zsync` | [Drift-1.10.0-x86_64.AppImage](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/Drift-1.10.0-x86_64.AppImage) · [`.zsync`](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/Drift-1.10.0-x86_64.AppImage.zsync) |
| **Windows (x64)** | Installer (`.exe`) | [Drift-Setup-1.10.0-x64.exe](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/Drift-Setup-1.10.0-x64.exe) |
| **Windows (x64)** | Portable Archive (`.zip`) | [Drift-Portable-1.10.0-x64.zip](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/Drift-Portable-1.10.0-x64.zip) |
| **macOS (Apple Silicon)** | Disk Image (`.dmg`) | [Drift-1.10.0-arm64.dmg](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/Drift-1.10.0-arm64.dmg) |
| **Verification** | Checksums | [SHA256SUMS](https://github.com/aedmark/Drift-Mojave/releases/download/v1.10.0/SHA256SUMS) |

---

## What Makes Mojave Different

### 1. Native Sony / MAGIX VEGAS Pro Binary Interchange (`.veg`)
Open and edit VEGAS Pro projects directly inside Drift Mojave without intermediate XML or EDL exports:
- **In-Engine Compound RIFF Binary Parser**: Reads VEGAS 14–21 `.veg` streams natively (`VegasReader`).
- **Sample-Accurate Timing**: Translates VEGAS 100ns internal ticks to microsecond timeline positions.
- **Nested Timelines & Project Nesting**: Imports nested `.veg`, `.drift`, or `.json` timelines onto the active timeline as reusable `Composite` clips.
- **Titles, FX, Pan/Crop & Overlays**: Extracts RTF titles, RGBA still-image overlays (`Image` clips with alpha), Levels (mapped to Drift's built-in `color.grade` `curveMaster`), 3-Way Color Corrector, Vignette, Softlight Glow, Sharpen FX, Event Pan/Crop motion, Velocity Envelopes, Track Envelopes, Bezier Masks, Event Mute/Lock flags, Video Track Solo/Mute compositing, Blend Modes, Markers, and Regions.
- **Event Grouping & Companion Linking**: Reconstructs symmetric multi-track audio/video link groups (`G` / `U` / `Ctrl+Shift+U`).

### 2. Edit-Friendly Apple ProRes 422, Broadcast MXF & Smart Render Pipeline
- **Zero-Prompt Intra-Frame Detection**: Automatically recognizes Apple ProRes (422, HQ, LT, Proxy, 4444), Avid DNxHD/DNxHR, FFV1, and uncompressed codecs as edit-friendly on import.
- **Broadcast `.mxf` Support**: Software-decodes XDCAM HD422 (`mpeg2video` 4:2:2 `yuv422p`) and pairs dual-mono PCM L/R audio streams (`pcm_s24le` / `pcm_s16le`) into stereo for both timeline playback and ProRes/edit-friendly transcoding.
- **Native In-Process Batch ProRes 422 Transcoder**: Parallel multi-threaded `libav*` conversion of AVCHD/H.264/HEVC camera files into `Transcodes_ProRes/` with subdirectory collision protection (`Day 1/00001.MTS`, `Day 2/00001.MTS`), automatic timeline relinking, live progress dialog, and clickable top-header progress badge.
- **True Zero-Generation ProRes 422 Smart Render**: `AVPacket` stream-copy pass-through in `Exporter::run` for unmodified ProRes 422 `.mov` segments with seamless fallback for composited segments.

### 3. Classic VEGAS Pro Timeline Muscle Memory & Multi-NLE Keymaps
- **Multi-NLE Keymap Presets**: Ships with **VEGAS Pro** as the Mojave default alongside **Drift Classic**, **Premiere Pro**, **DaVinci Resolve**, **Final Cut Pro**, **CapCut**, **Kdenlive**, and **Shotcut** in Settings.
- **Drag-to-Crossfade & Curve Shapes**: Overlap clips to create transitions with 6 curve shapes (*Linear*, *Smooth/Spline*, *Fast*, *Slow*, *Sharp*, *Hold*).
- **Elastic Time-Stretching, Razor Split & 4-Up Slip Monitor**: `Ctrl+drag` clip edges to stretch with SoundTouch pitch preservation; tap `S` for cursorless split; `Alt+drag` to slip source bounds with a live 4-up preview (`Prev Out`, `Slip In`, `Slip Out`, `Next In`); `D` cycles *Select → Cut → Slip → Roll (`Shift+R`) → Slide (`Shift+Y`)*.
- **Expanded Edit Mode (`F5`)**: A/B roll split-track cut inspection with `-1f / +1f` cut trimming.
- **Event Lock (`Alt+Shift+L`), Event Mute (`Alt+Shift+Q`) & Frame Quantize (`Alt+F8`)**.
- **Catch-Up Frame Skipping & Headless Benchmark**: Drops disposable H.264/HEVC non-reference packets when catching up past a 4-frame safety margin, throttles read-ahead on decoder-bound clips, exposes `perf_stats` via MCP, and runs `drift --headless --bench [--json]`.

### 4. Pro Audio, Mixing & Beat-Matched Music Auto-Remix
- **Beat-Matched Music Auto-Remix**: Automatically re-sequences music beds to hit a target duration using beat detection, 12-bin chroma + log-spectral timbre similarity, 4-beat context matching, and 20 ms equal-power crossfades without changing pitch or tempo (`Remix to duration…` / `remix_clip` MCP).
- **Track & Clip Automation Envelopes**: Track Volume (`Shift+V`), Pan (`Shift+P`), Composite Level (`Alt+Shift+V`), Mute (`Alt+Shift+M`), and interactive on-clip Audio Gain Envelopes—all supporting Bezier curve presets (*Linear*, *Smooth*, *Fast*, *Slow*, *Sharp*, *Hold*).
- **Audio Switches, Pitch & Peak Normalize**: Top-edge clip opacity/gain drag handles, Pitch Shift (`Alt+Shift+=`/`-`), Lock Pitch to Stretch, Invert Phase, Channel Modes, and Peak Normalize (`Shift+N`).
- **Sub-Buses, Stem Export & EBU R128 Loudness**: Cycle-guarded bus-to-bus routing, multi-track/bus/master WAV stem export, live ITU-R BS.1770 LUFS metering, and target loudness mastering (`-14`, `-16`, `-23` LUFS with `-1.0 dBTP` ceiling).

### 5. Finishing, Scopes, Generators & Bidirectional Interchange
- **Live Video Scopes HUD**: Dockable preview scopes for Rec.709 RGB/Luma Histogram, Luma Waveform, RGB Parade, and Vectorscope.
- **8 Built-In VEGAS-Style Media Generators**: SMPTE Color Bars, Solid Color, Color Gradient, Credit Roll, Timecode Burn-In, Checkerboard Pattern, Calibration Grid & Safe Zones, and Film Grain / Noise Texture.
- **Media Bin Audit, Sweep & Consolidation**: Smart Bin Filters (*Unused*, *Unconverted*, *Missing*, *Subclips*), timeline usage counts, undoable *Remove Unused Media*, and *Consolidate Project Media*.
- **Bidirectional Timeline Export**: Export active sequences to `.fcpxml` (Final Cut Pro XML), `.edl` (CMX 3600 EDL), `.otio` (OpenTimelineIO), and `.txt` (VEGAS Marker/Region TSV).

> See **[docs/ROADMAP.md](docs/ROADMAP.md)** for the full tier-by-tier changelog from `v1.0.0` through `v1.10.0`.

---

## Honest Limits

- **Proprietary VEGAS Plugins**: DirectShow / OFX third-party VEGAS plugins that have no built-in Drift equivalent are skipped safely rather than emulated; core VEGAS Levels, 3-Way Color Corrector, Vignette, Glow, Sharpen, Pan/Crop, envelopes, blend modes, and generators are translated.
- **External Media Files**: `.veg` project files store timeline structure and paths, not embedded raw video; if source media moved drives, Drift Mojave probes `Transcodes_ProRes/` and adjacent folders automatically or lets you relink missing media in the bin.
- **Smart Render Scope**: Zero-generation `AVPacket` stream-copy pass-through applies to unmodified single-layer Apple ProRes 422 `.mov` segments matching the export resolution and frame rate; segments with compositing, speed ramps, or color effects fall back cleanly to full encode.

---

## Building from Source

### Prerequisites
- **C++20** compiler (GCC 12+, Clang 15+, or MSVC 2022)
- **CMake** 3.21+ and **Ninja**
- **Qt 6.11+** (`Quick`, `QuickControls2`, `Multimedia`, `OpenGL`, `Widgets`, `Concurrent`, `Svg`, `Xml`)
- **FFmpeg 6.0+** (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`)

```bash
git clone https://github.com/aedmark/Drift-Mojave.git drift-mojave
cd drift-mojave

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDRIFT_BUILD_TESTS=ON

cmake --build build --target drift -j$(nproc)
ctest --test-dir build --output-on-failure
./build/drift
```

---

## License & Heritage

Drift Mojave is open-source software licensed under the **GNU General Public License v3.0** ([GPL-3.0](LICENSE)).

Forked with gratitude from [Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios.
