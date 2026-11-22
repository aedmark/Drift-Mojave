# Drift Mojave Roadmap

**Drift Mojave** builds on Drift's GPU-native timeline, AI tooling, and agentic MCP engine to deliver the tactile speed and project interchange of classic **Sony / MAGIX VEGAS Pro** without cloud lock-in or subscriptions.

---

## Shipped in v1.0.0 (`Mojave`)

- **Native `.veg` Interchange**: Compound RIFF binary reader (`VEGAS` 14–21) importing video/audio events, RTF titles/text, nested timelines, Levels / Color Corrector / Vignette / Softlight Glow / Sharpen FX, Event Pan/Crop, markers, regions, track mute/solo/volume/pan, and event link groups.
- **Edit-Friendly Intra-Frame Pipeline**: Automatic detection for ProRes, DNxHD/HR, FFV1, and uncompressed codecs plus background batch ProRes 422 transcoding with collision-safe subdirectory preservation and automatic timeline relinking.
- **Core VEGAS Timeline Muscle Memory**:
  - Drag-to-crossfade automatic overlap transitions with 6 fade/transition curve shapes
  - Elastic time-stretching (`Ctrl+drag` edges) with SoundTouch pitch preservation
  - Cursorless split (`S`), Slip tool (`Y` / `Alt+drag`), and interactive clip **Velocity Envelopes** (-100% reverse, 0% freeze, up to 1000%)
  - 3-mode **Auto-Ripple** (`Ctrl+L`), **Event Grouping** (`G` / `U` / `Ctrl+Shift+U`), and **Multi-Take Switching** (`T` / `Shift+T`)
  - Dynamic **Preview Quality** scaling (`Auto` / `Full` / `Half` / `Quarter` / `Eighth`) and Multicam punching window (`Ctrl+Shift+C`)

---

## Tier 1 — High-Impact Timeline & Editing Muscle Memory (`v1.1.0`)

Status: **Shipped in `v1.1.0`**

1. **Track Envelopes (Automation Lanes)** — ✅ Implemented (`v1.1.0`)
   - Timeline-level automation curves directly on track lanes:
     - **Audio Volume Envelope** (`Shift+V`)
     - **Audio Pan Envelope** (`Shift+P`)
     - **Video Composite Level Envelope** (`Alt+Shift+V`)
     - **Track Mute Envelope** (`Alt+Shift+M`, stepped hold automation)
   - Live evaluation during both playback and export (`AudioMixer` & `FrameCompositor`).
2. **Direct Top-Edge Clip Handles** — ✅ Implemented (`v1.1.0`)
   - Drag the upper edge of visual clips directly on the timeline to adjust **Video Opacity** (`0–100%`) and the upper edge of audio clips to adjust **Audio Event Gain** (`-inf` to `+6.0 dB`) with live waveform amplitude scaling.
3. **Audio Event Switches & Pitch Shifting** — ✅ Implemented (`v1.1.0`)
   - Per-clip **Pitch Shift** (`-24` to `+24` semitones, `Alt+Shift+=` / `Alt+Shift+-`) and **Lock Pitch to Stretch** toggle via Drift's SoundTouch retimer.
   - **Auto-Normalize** (`Shift+N`) peak scanner, **Invert Phase**, and **Stereo Channel Modes** (*Stereo*, *Left Only*, *Right Only*, *Combine Mono*, *Swap Channels*).
4. **Roll & Slide Trimming + VEGAS `D` Tool Cycle** — ✅ Implemented (`v1.1.0`)
   - **Roll Edit** (`Shift+R`) to simultaneously trim an edit cut between touching clips in one atomic step.
   - **Slide Edit** (`Shift+Y`) to reposition a clip between touching neighbors while adjusting adjacent out/in points symmetrically.
   - **`D` Key Edit Tool Cycle** through *Select -> Cut -> Slip -> Roll -> Slide*.

---

## Tier 2 — Pro Mixing, Compositing & Multi-Clip Workflows (`v1.1.0`)

Status: **Shipped in `v1.1.0`**

1. **Audio Sub-Buses, Aux/FX Sends & Integrated LUFS Metering**
   - Route multiple tracks into named stereo **Submix Buses** with dedicated per-bus FX racks, **Pre/Post-Fader Aux Sends**, and real-time EBU R128 Momentary / Short-Term / Integrated **LUFS loudness meters**.
2. **Video Track FX Rack, Master Video Output FX & Track Motion**
   - Persistent per-track **Video Track FX** chains and global **Master Video Output FX**.
   - **Parent/Child Track Compositing** (*Multiply (Mask)*, *Add*, *Difference*) and **3D Track Motion** keyframes above clip-level Pan/Crop.
3. **Inline Multicamera Take-Switching Mode (`Alt+Shift+C`)**
   - Create a multi-take multicamera clip from selected video clips, toggle split-grid live camera tiles directly inside the main preview monitor, and punch live cuts (`1–9` or click) into `ClipTake` events on the fly.

---

## Tier 3 — Finishing, I/O & Deeper `.veg` Interchange (`v1.1.0`)

Status: **Shipped in `v1.1.0`**

1. **Batch Render by Regions**
   - First-class timeline **Regions** (`Alt+Shift+R`) and one-click multi-file export across regions using configurable filename templates (`{Project}_{Region}_{Index}.{ext}`), codecs, and preset profiles.
2. **Dedicated Trimmer Window (`Alt+Shift+T`) & Dynamic RAM Preview (`Shift+B`)**
   - Dedicated dual-shuttle **Trimmer** window for subclipping (`I`/`O`, `J`/`K`/`L`, `Video+Audio` / `Video Only` / `Audio Only` -> Insert / Overwrite to timeline) and `Shift+B` **Dynamic RAM Preview** wired into range bakes for stutter-free looped playback of heavy FX sections.
3. **Deeper `.veg` Binary Interchange**
   - Direct extraction of VEGAS **Regions**, **Volume/Pan/Composite/Mute Track Envelopes**, **Bezier Mask** paths, and per-track **Audio FX** parameters from `.veg` project streams.

---

## Tier 4 — Pro Finishing, Media Generators, Subclips & Bidirectional Interchange (`v1.2.0`)

Status: **Shipped in `v1.2.0`**

1. **Tier 4A — Expanded Timeline & Editing Power Tools**
   - **Expanded Edit Mode (`F5`)**: A/B roll split-track & dual-frame cut inspection overlay with `-1f / +1f` cut trimming.
   - **Event Lock (`Alt+Shift+L`) & Event Mute (`Alt+Shift+Q`)**: Per-clip lock flag protecting individual events from move/trim/split/delete and quick mute toggling.
   - **Quantize to Frames (`Alt+F8`) & Snapping Granularity**: Toggleable frame-boundary quantization plus independent `snapToGrid`, `snapToMarkers`, and `snapToEvents` controls.
2. **Tier 4B — Media Generators, Subclips & Project Media Bin Upgrades**
   - **VEGAS-Style Media Generators**: Built-in generators for *SMPTE Color Bars*, *Color Gradient*, *Scrolling Credit Roll*, *Timecode Burn-In Overlay*, and *Solid Color Matte*.
   - **Create Subclip in Media Bin (`U` in Trimmer / Timeline Context Menu)**: Save reusable in/out subclip items directly into the Media Bin.
   - **Smart Bin Filters & Media Usage Audit**: Filter by *All*, *Unused*, *Unconverted / Long-GOP*, *Missing*, or *Subclips*, view per-asset usage counts, and *"Select Timeline Events Using Media"*.
3. **Tier 4C — Audio & Video Finishing (Clip Audio Gain Envelopes, Bus Routing & Stem/Smart Export)**
   - **Per-Clip Audio Gain Envelopes**: Keyframed per-clip volume envelope point authoring (`clipVolumeEnvelopePoints` / add / move / remove / clear).
   - **Bus-to-Bus Routing & Stem Export**: Cycle-guarded `AudioBus::outputBusId` routing in `AudioMixer` and one-click multi-track / sub-bus WAV stem export (`exportAudioStems`).
   - **Smart Render (No-Recompress Pass-Through)**: Segment analyzer (`Exporter::findSmartRenderSegments`) identifying matching unmodified intra-frame Apple ProRes 422 `.mov` segments eligible for zero-generation pass-through.
4. **Tier 4D — Two-Way Interchange & Deeper `.veg` Import Parity**
   - **Bidirectional Timeline Export**: `TimelineExporter` exporting active sequences to `.fcpxml` (Final Cut Pro XML), `.edl` (CMX 3600 EDL), `.otio` (OpenTimelineIO), and `.txt` (VEGAS Marker/Region TSV).
   - **Deeper `.veg` Import Parity**: Parse per-event lock/mute flags, event velocity envelopes, track composite blend modes, and VEGAS Media Generator chunks.

---

## Tier 5 — Multi-NLE Keymap Presets, True Smart Render, Project Nesting & Loudness Mastering (`v1.3.0`)

Status: **Shipped in `v1.3.0`**

1. **Multi-NLE Keymap Presets (VEGAS Pro Default)**
   - Built-in conflict-free keymap switcher in Settings (`VEGAS Pro`, `Drift Classic`, `Premiere Pro`, `DaVinci Resolve`, `Final Cut Pro`, `CapCut`, `Kdenlive`, `Shotcut`).
2. **Tier 5A — Interactive On-Clip Audio Gain Envelope (`TimelineAudioGainEnvelope`)**
   - Direct Canvas gain curve and draggable dB/gain nodes on audio and embedded-audio video clips (`0–200%` with unity `0 dB` magnetic detent, double-click add, right-click delete).
3. **Tier 5B — True Zero-Generation Smart Render Pass-Through**
   - In-process `AVPacket` stream-copy muxing inside `Exporter::run` for qualifying unmodified Apple ProRes 422 `.mov` segments with seamless fallback for composited segments.
4. **Tier 5C — Project Nesting (`.drift` / `.veg` / `.json` as a Clip)**
   - Import another Drift `.drift` bundle, VEGAS `.veg` project, or `.json` project directly onto the active timeline as a nested `Composite` clip with full asset and sub-sequence remapping.
5. **Tier 5D — Target EBU R128 Loudness Normalization**
   - Optional automatic Integrated LUFS mastering (`-14 LUFS`, `-16 LUFS`, `-23 LUFS`) with `-1.0 dBTP` True Peak limiting across full video exports, audio-only renders, and multi-track/bus WAV stems.
