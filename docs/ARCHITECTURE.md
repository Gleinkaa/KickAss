# KickAss — ARCHITECTURE

> **Single source of truth.** Synthesis of `docs/research/01..04`.
> Status: **awaiting Gleinkaa sign-off**. No C++ written until this is approved.
> Date: 2026-05-27

---

## 0. Scope philosophy

**v1 = faithful port of `reference/BazzismRebuild.py` to JUCE C++**, plus a small, surgical set of additions that the research team unanimously called "must-have" for a Projektor-grade result.

Things the DSP-science research wants (dual body+sub voices with phase-coherence math, real noise-burst click, knack/mid-band saturation, 32 parameters, modulation matrix) are **v1.1+** features. They are smart but they would mean rewriting the algorithm, not porting it. v1 ships when the C++ sounds bit-identical to the Python — then we add.

This decision is opinionated. The full DSP wish-list lives in `research/01_dsp_science.md` and we'll cherry-pick from it for v1.1.

---

## 1. Identity

| Field | Value |
|---|---|
| Product name | **KickAss** |
| Subtitle | by Gleinkaa |
| Author/Company | Gleinkaa |
| Plugin manufacturer code | `Glka` |
| Plugin code | `Kick` |
| Formats | VST3, Standalone (no AU on Windows) |
| Channels | Mono synth → stereo output bus (L=R) |
| MIDI input | Yes (any note retriggers from t=0) |
| MIDI output | No |
| Framework | JUCE at `C:\Users\glein\JUCE` (sibling path, mirrors SnareRhythmGen) |
| C++ standard | C++17 |
| Build generator | Visual Studio 17 2022, x64 |
| Repo | `D:\GoogleDrive\B_projects\KickAss\` |

---

## 2. v1 DSP — exactly what ships

The signal chain, in order:

```
   MIDI note-on
       │
       ▼
   ┌─────────────────────────────┐
   │ 1. Pitch envelope           │   start_freq → mid_freq → end_freq
   │    (sweep_time_1+2, curve)  │   piecewise (1-x)^curve
   └──────────────┬──────────────┘
                  │ instantaneous freq f(t)
                  ▼
   ┌─────────────────────────────┐
   │ 2. Sine oscillator          │   phase = cumsum(f)/sr  (double phase accumulator)
   │    sin(2π·phase)            │
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 3. AHDSR amp env + scoop    │   Attack/Hold/Decay1/Sustain/Decay2 with vol_curve
   │                             │   scoop: 1 - sin(π·φ)·depth in [scoop_start..+length]
   └──────────────┬──────────────┘
                  ▼   audio *= amp_env
   ┌─────────────────────────────┐
   │ 4. TRANSIENT LAYER (sum-in) │   Independent layer, kick stays mono
   │    type ∈ {Sine, Noise,     │     - Sine: 5 ms sine chirp 10k→2k (Python legacy)
   │            Both}            │     - Noise: filtered white-noise burst, exp decay
   │    click_vol, click_hpf,    │     - Both: sum sine + noise, each at half-level
   │    click_tone, click_decay  │   click_hpf (1-pole HP) ALWAYS engaged, default 800 Hz
   └──────────────┬──────────────┘   click_tone (1-pole LP) shapes noise color
                  ▼
   ┌─────────────────────────────┐
   │ 5. Oversample ↑ 4×          │   juce::dsp::Oversampling, log2=2, half-band IIR
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 6. Drive / saturation       │   per-sample drive_t = base + (ramp²·tail·5)
   │    tanh(audio·drive_t)/     │   normalised by tanh(max(drive_t))
   │    tanh(max_drive)          │
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 7. Oversample ↓ 4×          │
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 8. Polarity invert (opt)    │   if invert_phase: audio = -audio
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 9. DC blocker [NEW]         │   1-pole HP @ 8 Hz, always on
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 10. Soft-clip ceiling [NEW] │   tanh @ -0.3 dBFS, prevents bus overflow
   └──────────────┬──────────────┘
                  ▼
   ┌─────────────────────────────┐
   │ 11. Output gain             │   output_gain (-24 .. +6 dB)
   └──────────────┬──────────────┘
                  ▼
              stereo out
```

**What's in v1 that's NOT in Python:**
- 4× oversampling around the tanh stage (kills aliasing — `research/01` §4.1)
- **Transient layer redesign** — click_type enum (Sine / Noise / Both), HPF always on, tone LPF for noise — `research/01` §4.3 + user spec
- DC blocker (1-pole @ 8 Hz, always on, no UI) — `research/01` §4.4
- Soft-clip ceiling at -0.3 dBFS (always on, no UI) — `research/04` §6
- `output_gain` parameter (-24..+6 dB) — `research/02` §3
- `pitch_track` parameter (0..100 %, MIDI note transposes pitch envelope) — `research/02` §5
- 2 ms phase-reset crossfade on retrigger (mask click on note-on inside tail) — `research/01` §4.11

**What's in Python but NOT in v1:**
- The 100-sample linear tail-fade hack (replaced by proper envelope completion)

**Deferred to v1.1+:**
- Separate sub voice with phase-coherence math (`research/01` §4.2)
- Noise-burst click type behind enum (`research/01` §4.3)
- Knack / parallel mid-band saturator (`research/01` §4.6)
- Multiple saturator types (`research/01` §4.8)
- Built-in micro-reverb / Space (`research/01` §1.4)
- Per-segment pitch curves (`research/01` §3.1)
- Bezier envelope editing (`research/03` §0)
- 3D spectrogram strip (`research/03` §3.2)
- Resizable window (`research/03` §2.1)
- MIDI Learn (`research/03` §6.4)
- Modulation matrix
- Preset-morph macro

---

## 3. Parameter contract (v1 APVTS layout)

**21 parameters total.** Snake_case IDs match Python prototype keys exactly, so JSON presets round-trip without renaming. This is non-negotiable — it's the entire reason we picked snake_case over JUCE's house camelCase.

| # | ID (APVTS) | Display | Group | Type | Range | Default | Skew | Source |
|---:|---|---|---|---|---|---|---|---|
| 1 | `start_freq` | Start Freq | PITCH | float Hz | 100 .. 15000 | 8000 | 0.30 (log) | Python |
| 2 | `mid_freq` | Mid Freq | PITCH | float Hz | 50 .. 1000 | 150 | 0.30 (log) | Python |
| 3 | `end_freq` | End Freq | PITCH | float Hz | 20 .. 100 | 46.25 (F#1) | 0.50 | Python (default changed: 49→46.25 per `04` §4) |
| 4 | `sweep_time_1` | Sweep 1 | PITCH | float ms | 0.1 .. 50.0 | 10.0 | 0.30 | Python |
| 5 | `sweep_time_2` | Sweep 2 | PITCH | float ms | 1.0 .. 250.0 | 80.0 | 0.30 | Python |
| 6 | `pitch_curve` | Pitch Curve | PITCH | float | 0.1 .. 10.0 | 3.0 | 0.50 | Python |
| 7 | `vol_attack` | Attack | AMP | float ms | 0.0 .. 30.0 | 2.0 | 0.50 | Python |
| 8 | `vol_hold` | Hold | AMP | float ms | 0.0 .. 50.0 | 10.0 | 0.50 | Python |
| 9 | `vol_decay_1` | Decay 1 | AMP | float ms | 0.0 .. 150.0 | 50.0 | 0.50 | Python |
| 10 | `vol_sustain` | Sustain | AMP | float % | 0.0 .. 100.0 | 40.0 | 1.0 | Python |
| 11 | `vol_decay_2` | Decay 2 | AMP | float ms | 0.0 .. 700.0 | 150.0 | 0.50 | Python |
| 12 | `vol_curve` | Volume Curve | AMP | float | 0.1 .. 10.0 | 3.0 | 0.50 | Python |
| 13 | `scoop_start` | Scoop Start | SCOOP | float ms | 0.0 .. 50.0 | 10.0 | 0.50 | Python |
| 14 | `scoop_length` | Scoop Length | SCOOP | float ms | 1.0 .. 100.0 | 30.0 | 0.50 | Python |
| 15 | `scoop_depth` | Scoop Depth | SCOOP | float % | 0.0 .. 100.0 | 0.0 | 1.0 | Python |
| 16 | `click_vol` | Click Vol | TRANSIENT | float | 0.0 .. 1.0 | 0.0 | 1.0 | Python |
| 17 | `click_type` | Click Source | TRANSIENT | choice | Sine / Noise / Both | Sine | — | **NEW** (user spec) |
| 18 | `click_hpf` | Click HPF | TRANSIENT | float Hz | 200 .. 4000 | 800 | 0.30 (log) | **NEW** (research/04 §6) |
| 19 | `click_tone` | Click Tone (LPF) | TRANSIENT | float Hz | 2000 .. 16000 | 10000 | 0.30 (log) | **NEW** (research/01 §4.7) |
| 20 | `click_decay` | Click Decay | TRANSIENT | float ms | 1.0 .. 30.0 | 5.0 | 0.50 | **NEW** (research/01 §4.3) |
| 21 | `drive` | Base Drive | DRIVE | float | 1.0 .. 10.0 | 1.5 | 0.50 | Python |
| 22 | `tail_drive` | Tail Drive | DRIVE | float | 0.0 .. 10.0 | 0.0 | 0.50 | Python |
| 23 | `invert_phase` | Invert Phase | MASTER | bool | — | false | — | Python |
| 24 | `output_gain` | Output | MASTER | float dB | -24 .. +6 | 0.0 | 1.0 | **NEW** (research/02 §3) |
| 25 | `pitch_track` | Pitch Track | MASTER | float % | 0.0 .. 100.0 | 0.0 | 1.0 | **NEW** (research/02 §5) |

**25 parameters total** — Python's 19 + 6 KickAss additions (4 for the transient layer, 2 for master). Snake_case IDs match Python keys verbatim where they exist, so JSON presets round-trip without renaming. KickAss-only IDs are absent in legacy Python presets — importer leaves them at default.

**Panel grouping (UI):** PITCH (6) / AMP (6) / SCOOP (3) / TRANSIENT (5) / DRIVE (2) / MASTER (3). Six panels, not five — the transient layer earns its own panel.

**Non-automatable settings** (stored in APVTS state, not in the parameter list):
- `oversample_factor` — enum {Off, 2×, 4×, 8×}, default 4×. Changes latency; can't safely automate.
- `note_snap` — enum {Off, C1..C2}. Snaps `end_freq` to a note when changed; not an automatable param.

---

## 4. Code organization

```
KickAss/
├── CMakeLists.txt              # JUCE plugin target, juce_dsp included
├── Source/
│   ├── PluginProcessor.h/.cpp  # KickAssProcessor : juce::AudioProcessor
│   ├── PluginEditor.h/.cpp     # KickAssEditor : juce::AudioProcessorEditor
│   ├── KickEngine.h/.cpp       # pure C++ DSP, owned by value
│   ├── WaveformDisplay.h/.cpp  # the big visualizer canvas
│   ├── ParamPanel.h/.cpp       # the 5 labeled knob clusters
│   ├── HeaderBar.h/.cpp        # logo + preset + note-snap + BPM + auto
│   ├── FooterBar.h/.cpp        # play / export / A-B
│   ├── PresetManager.h/.cpp    # factory presets + JSON I/O
│   └── KickAssLookAndFeel.h/.cpp  # rotary + button + colors
├── reference/
│   └── BazzismRebuild.py       # the spec (read-only)
├── docs/
│   ├── ARCHITECTURE.md         # ← this file
│   ├── ROADMAP.md
│   └── research/               # research outputs (read-only)
└── scripts/
    └── compare_audio.py        # bit-compare Python output vs C++ output
```

### Class responsibilities

- **`KickAssProcessor`** — owns `juce::AudioProcessorValueTreeState apvts`, owns `KickEngine engine` by value, handles MIDI, owns `PresetManager`. Implements `processBlock`, `prepareToPlay`, `getStateInformation`/`setStateInformation`. Exposes an `offlineRender(buffer, durationMs)` method for the UI visualizer.
- **`KickEngine`** — pure DSP. No JUCE GUI deps. `prepare(sr, blockSize)`, `setParams(const KickParams&)`, `triggerNote(midiNote, velocity, sampleOffset)`, `renderBlock(buffer, numSamples)`, `reset()`. Owns the oversampler, the DC blocker, the phase accumulator, the limiter.
- **`KickAssEditor`** — main UI. Owns `HeaderBar`, `WaveformDisplay`, 5 × `ParamPanel`, `FooterBar`. Implements `juce::FileDragAndDropTarget` for JSON drop.
- **`WaveformDisplay`** — listens to APVTS, debounces 50 ms, calls `processor.offlineRender()` on UI thread, draws result. Pure `juce::Graphics` (no OpenGL). View modes: WAVE / SPECTRUM / BOTH.
- **`PresetManager`** — bundled factory presets (the 6 from Python + the 10 from `research/04` §7 = **16 factory presets**). Save/load `.kickpreset` (APVTS XML wrapped). Save/load `.json` (flat dict, Python-compatible).
- **`KickAssLookAndFeel`** — direct descendant of `SnareGenLookAndFeel`. Same `drawRotarySlider`, `drawToggleButton`, `drawButtonBackground`. Cyan → `accentHot #ff2266` swap. 64px knobs (down from SnareGen's 70px). 8px panel radius. New: 8 px underglow halo on active arc.

### Threading contract

- **`processBlock`** is hard-realtime: no allocations, no locks, no logging, no `juce::String` builds, no `getRawParameterValue()` chain inside inner loops (cache the atomic pointers once in `prepareToPlay`).
- **Phase accumulator is `double`**, audio is `float`. Prevents drift on long tails and oversampled chains.
- **UI ↔ engine** uses the **offline-preview pattern**: on parameter change, the editor calls `processor.offlineRender(buffer, ~500ms)` on the UI thread, into a UI-owned `juce::AudioBuffer<float>`. **No live ring buffer.** The kick is a one-shot, ≤700 ms; the user needs to see "the kick implied by current knob positions" even when no MIDI is playing.
- **Live MIDI playback** does its own realtime render in `processBlock`. The visualizer's playhead reads `engine.getPlaybackSamplePos()` (`std::atomic<int>`) at 30 Hz to draw the moving cursor.
- **No mutexes on the audio thread.** Explicitly rejecting SnareRhythmGen's `SnareEngine::processBlock` lock pattern.

---

## 5. Visualization (the headline feature)

Adopt `research/03` mostly verbatim. Concrete v1 commitments:

- **Window:** 1000 × 680 fixed.
- **Vertical budget:** Header 52 / Visualizer 340 (50 %) / Param panels 224 / Footer 44 / outer pad 20.
- **Visualizer layers (back to front):** time/freq grid → pitch envelope (cyan, log-Y, top half) → amp envelope shading + line (yellow, bottom half) → scoop overlay wash (purple) → rendered waveform (red mirrored vertical bars, bottom half) → playhead (white, 30 Hz) → top header strip with view tabs + readouts.
- **View tabs:** WAVE (default) / SPECTRUM (FFT of last offline render) / BOTH.
- **Tech:** pure `juce::Graphics`. Parameter-change-driven re-render, debounced 16 ms. Separate 30 Hz timer for playhead-only repaint.
- **Color palette (locked):**

| Role | Hex |
|---|---|
| Window bg | `#0c0c10` |
| Panel | `#15151c` |
| Canvas bg | `#07070a` |
| Grid minor / major | `#1e1e2a` / `#2c2c40` |
| `accentHot` (brand red) | `#ff2266` |
| `envAmp` (yellow line) | `#ffcc00` |
| `envPitch` (cyan) | `#00d4ff` |
| `envScoop` (purple wash) | `#b388ff` |
| `spectrum` (green) | `#00ff88` |
| Text bright / dim / ghost | `#e8e8f0` / `#7a7a8a` / `#4a4a55` |

- **Fonts:** Inter (logo, labels), JetBrains Mono (numeric readouts). Bundled as `BinaryData`. Segoe UI fallback.
- **Knob style:** rotary, 64 px, `Slider::RotaryHorizontalVerticalDrag`, double-click → default, right-click context menu, value-on-hover via `TextBoxBelow`.

Full design spec: `docs/research/03_ui_visualization.md`.

---

## 6. MIDI & playback model

- **Any MIDI note-on** retriggers the kick from `t = 0`. Sample-accurate offset via `metadata.samplePosition`.
- **Note-offs are ignored.** The kick plays its own envelope to completion.
- **Velocity** scales output linearly 0..1. No velocity curve (the saturator non-linearity provides taste).
- **Channel** is ignored. Any channel triggers.
- **`pitch_track` parameter (0..100 %):** if > 0, MIDI note number transposes the entire pitch envelope by `(midiNote - 36) / 12` semitones × tracking%. At 0 % the plugin behaves exactly like the Python prototype (note number ignored, `end_freq` knob is the only pitch control). At 100 % it's fully chromatic.
- **Voice stealing:** new note-on while previous is still playing → 2 ms cosine fadeout on previous voice's residual, then reset phase + envelope + DC blocker + limiter state to 0, then start new voice. **Single voice always.**
- **Transport is not consulted.** Tempo-sync display in the visualizer is informational only, computed from the host's `getPlayHead()` if available, falling back to the `BPM` editor in the header.

---

## 7. State / preset persistence

Three layers, all supported in v1:

1. **DAW state** — APVTS XML serialized to `juce::MemoryBlock` via `copyXmlToBinary`. Standard JUCE pattern. Same shape as `SnareRhythmGenProcessor::getStateInformation`.
2. **`.kickpreset` files** — APVTS XML written to disk. The DAW-portable preset format.
3. **`.json` files (Python-compatible)** — flat dict, one key per APVTS parameter ID. Round-trips with the Python prototype's JSON format. Unknown keys ignored on import (so KickAss-only `output_gain`/`pitch_track`/`click_hpf` are gracefully absent in legacy Python presets). Missing keys keep current value.

Preset folder: `%APPDATA%\KickAss\Presets\` on Windows. Factory presets are embedded via `BinaryData`, not on disk.

Factory bank (v1, **16 presets** total):
- 6 from Python (`research/00`): Psytrance (Default), Techno Deep, Hardstyle Zap, Projektor Punch, Projektor Deep Sub, Projektor Hard F#
- 10 from `research/04` §7: Forest Stomp 145, Hard Psy Main F#, Deep Twilight G, Progressive 138, Hi-Tech Snap A, Darkpsy Stomp E, Astrix Main F#, Burn In Noise Forest, Sub Lover 142, Tight Club F#

---

## 8. Build & deploy

- **Generator:** `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- **Build:** `cmake --build build --config Release`
- **`COPY_PLUGIN_AFTER_BUILD TRUE`** → VST3 lands in `C:\Program Files\Common Files\VST3\KickAss.vst3\` (first build needs an elevated shell once; after that, normal builds work).
- **Standalone exe:** `build\KickAss_artefacts\Release\Standalone\KickAss.exe` — daily dev runs from here, fastest iteration.
- **DAW integration test:** Ableton Live + FL Studio + Reaper. Report `setLatencySamples(osSampler.getLatencyInSamples())` honestly so PDC works.

---

## 9. Decisions locked by user (2026-05-27)

| Q | Decision |
|---|---|
| Gridmorph reference | **Dropped.** Not a real product. |
| Window size | **1000 × 680 fixed for v1.** Resizable comes in v1.x. |
| Polyphony | **Mono single voice for the kick body.** Click is its own transient layer (see §2 + §3). 2 ms retrigger crossfade on note-on inside tail. |
| Click design | **Transient layer with `click_type` enum: Sine / Noise / Both**, HPF + tone + decay per layer. Sine = Python legacy chirp. Noise = filtered white-noise burst with exp decay. Both = sum of the two. |
| Factory bank | 16 presets (6 Python + 10 from `research/04` §7). |
| `pitch_track` default | 0 % (Python-faithful behavior — note number ignored unless user opts in). |

Architecture is **locked**. Proceeding to ROADMAP and JUCE scaffold.
