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

## v1.1+ backlog (deferred from research)

Sorted roughly by impact. Promotes to a phase only after v1.0 ships and gets real-world use.

1. **Separate sub voice with phase-coherent crossover** (`research/01` §4.2, `research/04` §6) — the biggest sonic upgrade. Add `sub_level`, `sub_phase_offset`, `body_level` params.
2. **Knack / parallel mid-band saturator** (`research/01` §4.6) — the "Bazzism → Projektor" missing ingredient.
3. **Saturation type enum** {Tanh, SoftClip, HardClip, Tube, Foldback} (`research/01` §4.8).
4. **Built-in micro-reverb / Space** (`research/01` §1.4).
5. **Resizable window** with `setResizeLimits(900, 600, 1400, 900)`.
6. **3D spectrogram strip** below waveform (`research/03` §3.2).
7. **Per-segment pitch curves** (`curve_1`, `curve_2` instead of shared `pitch_curve`).
8. **Sample slot for click** — drag-and-drop a `.wav`, becomes the transient layer.
9. **Transient shaper** (envelope-follower-driven attack/sustain gains) (`research/04` §6).
10. **MIDI Learn** with right-click context menu integration.
11. **Tempo-sync display** — "kick ends at sixteenth 3.2 @ 146 BPM" overlay in viz (`research/04` §6).
12. **Phase-align knob** — 0–20 ms delay to align kick fundamental zero-crossing with bass note (`research/04` §6).
13. **Bezier envelope editing** — draggable breakpoints (`research/03` §0).
14. **Modulation matrix / macro morph** between preset snapshots (`research/02` §10).
15. **macOS build** with AU format.

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
