<p align="center">
  <img src="Drift_icon.png" alt="Drift Mojave icon" width="128" height="128">
</p>

<h1 align="center">Drift Mojave</h1>

<p align="center">
  <strong>“What happens in the timeline, stays in the timeline.”</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.1.0--mojave-feb504?style=flat&labelColor=28272f" alt="Version 1.1.0 Mojave">
  <img src="https://img.shields.io/badge/license-GPL--3.0-feb504?style=flat&labelColor=28272f" alt="License: GPL-3.0">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="Platform: Linux | Windows | macOS | Android">
  <img src="https://img.shields.io/badge/interchange-Sony%20%2F%20MAGIX%20VEGAS%20(.veg)-feb504?style=flat&labelColor=28272f" alt="VEGAS Pro Interchange">
</p>

---

**Drift Mojave** is a definitive, high-velocity fork of Drift built for editors who crave uncompromising timeline responsiveness, edit-friendly intra-frame workflows, and signature desktop editing mechanics.

Born from the need to bring the lightning-quick tactile feel and project compatibility of classic **Sony / MAGIX VEGAS Pro** to a modern, GPU-native, cross-platform video editor, Drift Mojave preserves the best of both worlds: full modern 3D compositing, GPU shaders, on-device Whisper AI, and deep MCP agent control—paired with legendary NLE muscle memory and zero-friction timeline editing.

No subscriptions, no accounts, no watermarks, no cloud lock-in.

---

## What Makes Mojave Different

### 1. Native Sony / MAGIX VEGAS Pro Binary Interchange (`.veg`)
Open and edit legacy and active VEGAS Pro projects directly inside Drift Mojave without needing intermediate XML or EDL exports:
- **In-Engine Wave64 / Compound Binary Parser**: Reads compound RIFF binary `.veg` streams natively (tested across VEGAS 14 through 21).
- **Sample-Accurate Time Conversion**: Translates VEGAS 100ns internal ticks to microsecond timeline positions with sample-accurate alignment.
- **Nested Timelines**: Recursively imports nested child `.veg` projects as composite sequence tabs.
- **Titles & Text Extraction**: Parses rich-text RTF payloads, fonts, styles, tracking, and colors directly into native Drift Text clips.
- **Core Video FX Translation**: Translates VEGAS Levels, 3-Way Color Corrector, Vignette, Softlight Glow, and Sharpen into native GPU shaders.
- **Event Pan/Crop & Keyframe Motion**: Extracts source crop rectangles and 2D pan/zoom animation paths into native transforms.
- **Event Grouping & Companion Linking**: Reconstructs symmetric multi-track audio/video link groups (`G` / `U` / `Ctrl+Shift+U`).
- **Track & Project Metadata**: Honors track Mute, Solo, Volume dB, Pan, timeline Markers, Regions, and RIFF summary metadata (`INAM`, `IART`, `ICMT`).

### 2. Automated Edit-Friendly Intra-Frame Pipeline
Modern editors shouldn't choke on inter-frame Long-GOP codecs:
- **Zero-Prompt Intra-Frame Detection**: Automatically detects Apple ProRes (422, HQ, LT, Proxy, 4444), Avid DNxHD/DNxHR, FFV1, and uncompressed video as edit-friendly on import.
- **Background Batch ProRes 422 Transcoder**: Background multi-threaded conversion of non-edit-friendly media (e.g., AVCHD, H.264/HEVC camera files) into Apple ProRes 422 with automatic timeline clip relinking.
- **Subdirectory Collision Protection**: Preserves multi-day camera folder structures (`Day 1/00001.MTS`, `Day 2/00001.MTS`) during batch transcode.

### 3. Classic VEGAS Editing Ergonomics
Edit at the speed of thought with classic NLE timeline mechanics:
- **Interactive Drag-to-Crossfade**: Drag clips into overlapping positions on the same track to automatically create and resize transition junctions without track splitting.
- **Fade & Transition Curve Selector**: Right-click fade handles or transition junctions to choose between Linear, Smooth / S-Curve, Fast, Slow, Sharp, and Natural curves.
- **Elastic Time-Stretching**: Hold `Ctrl` while dragging clip start or end edges to elastic time-stretch or compress speed with audio pitch preservation.
- **Cursorless Razor Split & Slip Editing**: Tap `S` to instantly slice clips at the playhead without switching tools. Hold `Alt` and drag within a clip to slip its source in/out boundaries.
- **Interactive Velocity Envelopes**: Add spline velocity envelopes to clips with keyframe handles for reverse playback (-100%), freeze-frames (0%), and hyper-speed (up to 1000%).

### 4. Advanced Production Features
- **VEGAS 3-Mode Auto-Ripple (`Ctrl+L`)**: Switch between *Affected Tracks*, *All Tracks*, and *Tracks + Markers & Regions* auto-ripple modes. Close gaps instantly with Ripple Delete (`Shift+Delete`).
- **Interactive Multi-Take Switching (`T` / `Shift+T`)**: Cycle through takes on grouped event takes seamlessly.
- **A/B Split-Screen Video Compare**: Compare live footage against FX bypass, clipboard snapshots, reference frames, or adjacent video tracks with draggable vertical/horizontal wipe splitters.
- **Media Pool FX**: Apply non-destructive GPU effect chains directly to source media assets before they reach the timeline.

### 5. New in v1.1.0 (VEGAS Parity Tiers 1–3)
- **Top-Edge Handles & Track Envelopes**: Drag top-edge opacity/gain handles (`Shift+drag` fine trim, double-click reset) and automate track Volume (`Shift+V`), Pan (`Shift+P`), Composite Level (`Alt+Shift+V`), and Mute (`Alt+Shift+M`).
- **Audio Switches, Pitch & Peak Normalize**: Per-clip Invert Phase, Mono Mixdown, Left/Right Only, Channel Swap, Pitch Shift (`Alt+Shift+=`/`-`), and Peak Normalize (`Shift+N`).
- **Roll & Slide Trim Tools**: Precision dual-roller Roll (`Shift+R`) and Slide (`Shift+Y`) editing with linked group synchronization.
- **Sub-Buses, Aux Sends & Live LUFS Metering**: Dedicated audio sub-buses with FX racks, pre/post-fader aux sends, and real-time ITU-R BS.1770 / EBU R128 LUFS + True Peak hold.
- **Track Compositing, Track FX & Track Motion**: Video Track FX, Master Video Output FX, 2D/3D Track Motion, and cycle-guarded Parent/Child Track Compositing blend trees.
- **Inline Multicamera (`Alt+Shift+C`)**: Split-grid angle preview right in the monitor with keys `1–9` or click-to-cut in a single undo step.
- **Timeline Regions, Trimmer & Dynamic RAM Preview**: Named span Regions (`Alt+Shift+R`) with Batch Region Render templates, dedicated Trimmer window (`Alt+Shift+T`, `I`/`O`, `J`/`K`/`L`), Dynamic RAM Preview (`Shift+B`), and deeper `.veg` import (Regions, Track Envelopes, Bezier Masks, and Track Audio FX).

> See the **[Drift Mojave Roadmap](docs/ROADMAP.md)** for upcoming VEGAS Pro parity tiers (`v1.1+` through `v1.3+`).

---

## Drift Mojave Versioning Scheme

Drift Mojave follows **Semantic Versioning 2.0.0** (`MAJOR.MINOR.PATCH`):

```
MAJOR . MINOR . PATCH
  1   .   0   .   0
```

- **MAJOR (e.g., 1.x -> 2.x)**: Major generational milestones, fundamental timeline architecture shifts, or format breaking changes.
- **MINOR (e.g., 1.0.x -> 1.1.x)**: New interchange formats, substantial timeline tool additions, major render pipeline enhancements, or UI paradigm evolutions.
- **PATCH (e.g., 1.0.0 -> 1.0.1)**: Targeted bug fixes, stability updates, performance optimizations, and codec compatibility refinements.

### Codename Tradition
Drift Mojave's 1.0 release series is codenamed **Mojave**. Subsequent feature branches and minor milestone series carry desert and southwestern landmark designations celebrating endurance, vast horizons, and high-velocity workflow.

---

## Core Features Inherited from Drift

- **Agent Access (MCP)**: Edit the open project via an autonomous LLM agent or run fully headless.
- **3D on the Timeline**: Tilt clips in 3D space, position scene lights, orbit cameras, and drop in `.glb` models or gaussian splats.
- **Skia GPU Vector Engine**: Ultra-crisp vector shapes, text styles, and Lottie animations rendered via GPU.
- **Local Machine AI**: On-device Whisper subtitle generation, background matte removal, live face retouch, and voice synthesis—no API keys required.
- **Color Grading**: 3-Way color wheels, Curves, HSL, 3D LUTs (`.cube`), vectorscopes, histograms, and waveform monitors.
- **Professional Audio**: Equalizer, compressor, loudness meter, voiceover recording, pitch correction, track FX racks, and master bus processing.

---

## Building from Source

### Prerequisites
- **C++20** compatible compiler (GCC 12+, Clang 15+, MSVC 2022)
- **CMake** 3.21 or later
- **Ninja** build system
- **Qt 6.10+** (Quick, QuickControls2, Multimedia, OpenGL, Widgets, Concurrent, Svg, Xml)
- **FFmpeg 6.0+** (libavcodec, libavformat, libavutil, libswscale, libswresample)

### Build Steps

```bash
# Clone the repository
git clone https://github.com/aedmark/Drift.git drift-mojave
cd drift-mojave

# Configure build with Ninja
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDRIFT_BUILD_TESTS=ON

# Compile the executable
ninja -C build drift

# Run tests
ninja -C build test
```

### Launching

```bash
./build/drift
```

---

## License & Heritage

Drift Mojave is open-source software licensed under the **GNU General Public License v3.0** ([GPL-3.0](LICENSE)).

Forked with gratitude from [Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios.
