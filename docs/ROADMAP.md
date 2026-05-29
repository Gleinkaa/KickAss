# KickAss — ROADMAP

> Phased build plan. Each phase ends with a working, demoable artifact.
> Locked by ARCHITECTURE.md (2026-05-27). All decisions deferred to that doc.

---

## Phase 0 — Scaffold ✅ done

- [x] Project tree under `D:\GoogleDrive\B_projects\KickAss\`
- [x] Python prototype copied to `reference/BazzismRebuild.py`
- [x] README, STATUS, CLAUDE.md, .gitignore
- [x] git init + first commit
- [x] 4-agent brainstorm research (`docs/research/01..04`)
- [x] ARCHITECTURE.md synthesized + locked

---

## Phase 1 — JUCE skeleton (sound: silence)

**Goal:** empty plugin loads in standalone + DAWs without crashing.

- [ ] `CMakeLists.txt` per ARCHITECTURE §1 (juce_add_plugin, juce_dsp, codes Glka/Kick, COPY_PLUGIN_AFTER_BUILD TRUE)
- [ ] `Source/PluginProcessor.{h,cpp}` — empty `KickAssProcessor`, APVTS with all 25 parameters from ARCHITECTURE §3
- [ ] `Source/PluginEditor.{h,cpp}` — empty `KickAssEditor`, 1000×680 black window
- [ ] `Source/KickEngine.{h,cpp}` — stub `prepare/setParams/triggerNote/renderBlock/reset`, outputs silence
- [ ] First build via VS 2022, verify VST3 + Standalone both load
- [ ] DAW smoke test (Ableton or Reaper)

**Artifact:** silent VST3 that loads, exposes 25 parameters in DAW automation list.

---

## Phase 2 — DSP port (sound: working kick, no UI yet)

**Goal:** C++ output matches Python output bit-for-bit (within float rounding) for identical parameter sets.

- [ ] Pitch envelope (`start/mid/end + sweep_time_1/2 + pitch_curve`) → piecewise `(1-x)^curve` interpolation
- [ ] Sine oscillator via `double` phase accumulator, `cumsum(f)/sr`
- [ ] AHDSR amp envelope with `vol_curve` exponent
- [ ] Volume scoop (`1 - sin(π·φ)·depth`)
- [ ] Transient layer: Sine type (Python legacy 10k→2k chirp) — `click_type=Sine`
- [ ] Transient layer: Noise type (filtered white-noise burst with exp decay) — `click_type=Noise`
- [ ] Transient layer: Both (sum at half-level each)
- [ ] Click HPF (1-pole) + Click tone LPF (1-pole)
- [ ] Drive ramp: `base + (linspace²)·tail·5`
- [ ] `juce::dsp::Oversampling` 4× wrapping the tanh stage
- [ ] DC blocker (1-pole HP @ 8 Hz, always on)
- [ ] Soft-clip ceiling tanh @ -0.3 dBFS (always on)
- [ ] Polarity invert
- [ ] Output gain
- [ ] 2 ms retrigger crossfade
- [ ] `setLatencySamples(osSampler.getLatencyInSamples())` after `prepareToPlay`
- [ ] `scripts/compare_audio.py` — render same preset in Python + KickAss standalone, FFT diff, target ≤ -60 dB delta in audible band

**Artifact:** standalone produces a recognizable Projektor Punch when you press any key on the on-screen MIDI keyboard.

---

## Phase 3 — UI shell (look: brand visible, no viz yet)

**Goal:** the editor looks like a real plugin, knobs work, no visualizer yet.

- [ ] `KickAssLookAndFeel` — port `SnareGenLookAndFeel`, swap cyan → `#ff2266`, add 8 px underglow halo on active arc, 64 px knobs
- [ ] Font binary resources (Inter + JetBrains Mono) via `BinaryData`
- [ ] `HeaderBar` — KICKASS wordmark + "by Gleinkaa" subtitle, preset combo (placeholder), note-snap combo, BPM editor, Auto Play toggle, SAVE/LOAD buttons
- [ ] 6 × `ParamPanel` — PITCH / AMP / SCOOP / TRANSIENT / DRIVE / MASTER
- [ ] `KnobWithLabel` struct + `setupKnob` helper ported from SnareGen
- [ ] All 25 parameters attached via `SliderAttachment` / `ButtonAttachment` / `ComboBoxAttachment`
- [ ] Parameter formatters (Hz: <100=1dp / <1000=0dp / ≥1000=2dp kHz; ms: <10=1dp / ≥10=0dp; dB: %+0.1f)
- [ ] `FooterBar` — big PLAY KICK button, EXPORT WAV, A/B toggle

**Artifact:** plugin looks like the ASCII wireframe in `research/03` §2.2 minus the big black box where the visualizer goes.

---

## Phase 4 — The visualizer (the headline feature)

**Goal:** the canvas dominates the window and shows everything that matters.

- [ ] `WaveformDisplay` component, 340 px tall, in the slot reserved in Phase 3
- [ ] `KickAssProcessor::offlineRender(buffer, durationMs)` — UI-thread DSP render
- [ ] Debounced re-render on APVTS parameter change (16 ms one-shot)
- [ ] Grid: vertical 10ms minor / 50ms major + labels, horizontal `±1.0 / 0.0` amp + log-Y `20 / 100 / 1k / 10k Hz` pitch
- [ ] Amp envelope shading + 2px yellow line
- [ ] Red mirrored vertical-bar waveform (Python's `min/max` per pixel column pattern)
- [ ] Cyan pitch envelope line on log-Y top half + diamonds at the 3 breakpoints
- [ ] Purple scoop wash where scoop is active
- [ ] Playhead — 30 Hz timer, repaints only the playhead rect, reads `engine.getPlaybackSamplePos()`
- [ ] View tabs: WAVE (default) / SPECTRUM (FFT of last offline render) / BOTH
- [ ] Top-strip readouts: peak dBFS (red >-0.1), total duration ms, end note + Hz
- [ ] Hover crosshair + `t/Hz/dB` tooltip
- [ ] Click-anywhere-in-canvas = trigger PLAY

**Artifact:** every knob movement instantly redraws the kick shape. The viz is the most expensive-looking thing on screen.

---

## Phase 5 — Preset system

- [ ] `PresetManager` class
- [ ] 16 factory presets baked via `BinaryData` (6 from Python + 10 from `research/04` §7)
- [ ] Preset combobox populated: factory + `---User---` separator + `%APPDATA%\KickAss\Presets\*.kickpreset`
- [ ] `.kickpreset` save/load (APVTS XML)
- [ ] `.json` save/load (flat dict, Python-compatible)
- [ ] Drag-and-drop `.json` onto editor → import + flash combobox label green
- [ ] Note-snap dropdown sets `end_freq` to chosen Hz, greys out `end_freq` knob until "Off"
- [ ] Modified-preset indicator (asterisk in combobox label)
- [ ] Preset arrow-key cycling when combobox focused

**Artifact:** ship-quality preset workflow. Drop a JSON, hear the kick.

---

## Phase 6 — Polish + DAW compatibility

- [ ] Auto Play 4/4 timer + visualizer playhead loop sync
- [ ] A/B compare (snapshot two states, toggle)
- [ ] Right-click context menu on knobs (reset / copy / paste / edit)
- [ ] Ctrl+drag = fine, Shift+drag = step-snap
- [ ] `setTitle`/`setDescription` accessibility metadata
- [ ] DAW integration test matrix: Ableton, FL Studio, Reaper, Bitwig
- [ ] Validate PDC reports correct latency (oversampler samples)
- [ ] Validate state recall (save/close/reopen project preserves preset state)
- [ ] CI build script (`scripts/build.bat` + maybe GitHub Actions Windows runner)
- [ ] `v1.0.0` git tag + GitHub Release with VST3 zip

**Artifact:** shippable v1.0.0.

---

## Post-v1.0 — themed releases

Restructured 2026-05-29 from a flat 16-item backlog into three themed releases, each with a story.
Effort numbers are honest (calendar, not optimistic). Test-first: every DSP change lands behind the
`KickAss_DspTests` regression net.

### v1.1 — Sonic Variety (~2.5–3 weeks)
*"Sounds 3× more versatile + finishes v1.0's promise."*

- [x] **DSP regression net** (`Tests/DspSafetyTest.cpp`) — non-silence / state round-trip / param fuzz / extremes. *Done 2026-05-29.*
- [x] **Saturation type selector** {Tanh, SoftClip, HardClip, Tube, Foldback} (`research/01` §4.8). Default Tanh = v1.0 parity. *Done 2026-05-29.*
- [x] **Output safety limiter** — sample-peak ceiling at −0.1 dBFS after output gain, defeatable (`safety_limit`, default ON). *Done 2026-05-29. NOTE: sample-peak, not true-peak/dBTP — inter-sample/lookahead deferred.*
- [x] **Spectrum FFT view** (2D, `[WAVE|SPECTRUM|BOTH]` tabs) — finishes the punted Phase 4 spec. FFT in header-only `SpectrumUtil.h`, headless-tested. *Done 2026-05-29.*
- [ ] **Undo/redo** via `juce::UndoManager` wired to APVTS — cheap, protects breakpoint-editor work.
- [ ] **Sample-slot transient** — drag a `.wav` onto the transient section (`AudioFormatReader` → buffer playback). KICK 2's killer feature; `clickType` enum + filter chain already there.
- [ ] **Resizable window** — `setResizeLimits(900, 600, 1400, 900)`. ~10 lines.
- [ ] **Crash logging** to `%APPDATA%\KickAss\crashes\` via `SystemStats::getStackBacktrace()` — needed to debug beta reports.
- [ ] **Inno Setup / NSIS installer** — replaces the `.bat` xcopy; registers VST3 path.
- [ ] **Polarity invert is present; add drag-out WAV** (`DragAndDropContainer`) — drag rendered kick straight into the DAW timeline.

### v1.2 — Deep Sound Design (~3 weeks)
*"Serum-level envelope + layering control."*

- [ ] **Sub voice with phase-coherent crossover** (`research/01` §4.2, `research/04` §6) — `sub_level`, `sub_phase_offset`, `body_level`. Architectural: re-checks gain staging.
- [ ] **Knack / parallel mid-band saturator** (`research/01` §4.6) — the "Bazzism → Projektor" missing ingredient.
- [ ] **Bezier envelope editing** — draggable control-point handles (`research/03` §0). Scope honestly: curve math + hit-testing + persistence bump (ahdsr/linear/bezier) + non-monotonic LUT projection. 1–2 weeks, not an afternoon.
- [ ] **Per-segment pitch curves** (`curve_1`, `curve_2` instead of shared `pitch_curve`).
- [ ] **Velocity → drive/pitch-env depth** — makes triggers feel alive.
- [ ] **Reference-kick FFT overlay** — drop a WAV on the spectrum, see it ghosted behind yours. A/B sound-design killer.
- [ ] **Preset tagging/filtering** (Hardstyle / Techno / Hi-tech / Sub / Tonal chips).

### v1.3 — Platform + Polish (~3 weeks)
*"Runs everywhere, controllable everywhere."*

- [ ] **macOS build** — universal binary (arm64 + x86_64), VST3 + AU + Standalone. Budget 2–3 focused days (codesign, notarization, `auval`), NOT an hour. CMake already prepped.
- [ ] **CLAP support** via [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) submodule — CMake glue only (ARCHITECTURE §8b).
- [ ] **MIDI Learn** — right-click → Learn CC (mostly host-handled today; standalone benefit).
- [ ] **Phase-align knob** — 0–20 ms delay to align kick fundamental zero-crossing with bass (`research/04` §6).
- [ ] **Tempo-sync display** — "kick ends at sixteenth 3.2 @ 146 BPM" overlay (`research/04` §6).
- [ ] **Transient shaper** (envelope-follower attack/sustain gains) (`research/04` §6).
- [ ] **Micro-reverb / Space** (`research/01` §1.4) — small "Air" knob; pros use a send, so low priority.
- [ ] **Metering strip** — peak / RMS / short-term LUFS readout.
- [ ] **Modulation matrix / macro morph** between snapshots (`research/02` §10).

### Cut / parked
- **3D spectrogram waterfall** — vanity; 2D spectrum is what kick designers actually use. Park until requested.

### Before declaring v1.0 "shipped"
- [ ] 2-week closed beta with 3–5 producers in real projects.
- [ ] Performance baseline (e.g. "16 instances < 30% CPU @ 256 buf on Ryzen 5 3600").

---

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Oversampler reports wrong latency → bad DAW PDC | Phase 2 explicit verification step: render kick at tempo, snap to grid, compare against pre-oversampler render. |
| C++ DSP doesn't match Python output | `scripts/compare_audio.py` is a Phase 2 deliverable, not an afterthought. Ship parity before adding features. |
| `COPY_PLUGIN_AFTER_BUILD TRUE` fails without admin | Document: first build needs elevated VS shell once. After that, the folder permission persists. |
| Editor performance on 4K display | Phase 4 includes a `juce::Image` cached-background fallback if grid rendering shows up in profiler hot spots. |
| Factory preset names trademark issues ("Projektor", "Astrix", etc.) | Rename if any are flagged. Internal names already use generic styles ("Forest Stomp 145" etc.) — only the 3 inherited from Python carry artist references. Acceptable for personal/free release; rename for paid distribution. |
| Aliasing still audible at 4× OS at extreme drive | Phase 2 step: render an A-list and B-list (4× vs 8×) at `drive=10`, listen. Promote default to 8× if 4× isn't clean. |

---

## Timeline (estimated, optimistic)

| Phase | Effort | Cumulative |
|---|---|---|
| 1. JUCE skeleton | 0.5 day | 0.5 d |
| 2. DSP port + parity | 2 days | 2.5 d |
| 3. UI shell | 1.5 days | 4 d |
| 4. Visualizer | 1.5 days | 5.5 d |
| 5. Preset system | 0.5 day | 6 d |
| 6. Polish | 1 day | **7 d** |

One focused week. Real calendar will stretch because of build/DAW debugging and the inevitable "this knob feels wrong" iterations.
