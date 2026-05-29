# KickAss — Session Handoff

> For the next Claude session picking up this work.
> Last updated: 2026-05-29 (v1.1 ship-prep merged).

---

## ⏩ CURRENT HANDOFF — 2026-05-29 (v1.1)

**This block supersedes everything below it** (kept for history). `STATUS.md` is the
living status; this is the next-session entry point.

### Where things stand
- **Branch/commits:** all work is on **`master` @ `d3085d7`**, **11 commits ahead of
  `origin/master` and UNPUSHED** (user is deliberately holding the push). The
  `v1.1-sonic-variety` and `v1.1-ship-prep` feature branches were merged
  (fast-forward) and **deleted** — master is the only branch.
- **Health:** VST3 + Standalone build **clean**; **`ctest` → 5/5 pass**; working tree clean.
- **v1.0** is functionally complete (Phases 0–8). **v1.1 "Sonic Variety"** added, in two batches:

  **Batch 1 — sonic features (merged + ears-on verified by the user 2026-05-29):**
  DSP regression net · saturation types {Tanh, Soft Clip, Hard Clip, Tube, Foldback} ·
  output safety limiter (−0.1 dBFS, defeatable) · spectrum view (WAVE|SPECTRUM|BOTH) ·
  undo/redo (Ctrl+Z/Y) · sample-slot transient (drag a WAV onto the TRANSIENT panel).

  **Batch 2 — ship-prep (merged 2026-05-29, headless-tested, NOT yet manually verified):**
  - **Crash logging** — `Source/CrashLog.{h,cpp}`. `SystemStats` handler writes a
    timestamped report (app/JUCE/OS/CPU + stack backtrace) to `%APPDATA%\KickAss\Logs`.
    Installed **Standalone-only** (`wrapperType == wrapperType_Standalone`) to avoid
    hijacking a host's handler — plugin-host crash capture is deferred. Test: `KickAss_CrashTests`.
  - **Drag-out WAV** — footer **DRAG WAV ↗** affordance; editor is now a
    `juce::DragAndDropContainer`. Drag it onto the desktop / a DAW track to drop a
    freshly-rendered 24-bit/48k stereo WAV. Render path lives in
    `KickAssProcessor::renderToWavFile()` (shared with EXPORT WAV; duration via
    `computeRenderDurationMs()`). Test: `KickAss_RenderTests`.
  - **Inno installer** — `installer/KickAss.iss` + `scripts/build_installer.bat`
    (VST3→Common Files\VST3, Standalone→Program Files\KickAss, components/uninstall/
    shortcuts). **Authored but NOT compiled** — Inno Setup 6 isn't installed on this
    machine; the `.bat` fails gracefully with install instructions.

### Build / test / run
```powershell
cd D:\GoogleDrive\B_projects\KickAss
cmake -S . -B build -G "Visual Studio 17 2022" -A x64        # only after CMake changes
cmake --build build --config Release --target KickAss_All     # close the Standalone first (file-lock!)
ctest --test-dir build -C Release --output-on-failure         # 5 suites: curve / dsp / spectrum / crash / render
.\build\KickAss_artefacts\Release\Standalone\KickAss.exe      # run standalone
```

### NEXT — pick up here (in priority order)
1. **Manual-verify the two untested batch-2 features:** drag the **DRAG WAV ↗** footer
   button onto the desktop → confirm a playable WAV lands. (Crash handler is hard to
   exercise on purpose — low priority.)
2. **Compile the installer:** install Inno Setup 6 → run `scripts\build_installer.bat`
   → confirm `installer\Output\KickAss-1.1.0-Setup.exe`. Then test install/uninstall.
   NOTE version mismatch: installer says `1.1.0`, CMake `project(... VERSION 0.1.0)` —
   bump the CMake version when you cut the release.
3. **Push to `origin`** (`git push origin master`) once the user OKs it — it's been on hold.
4. **DAW validation matrix** (the long-standing v1.0 open item): load the VST3 in
   Reaper / Ableton / FL → verify params, state recall, latency, MIDI routing.
5. **Release:** tag + GitHub Release with `KickAss.vst3` + the installer.

### Carried-forward limitations (deliberate, documented)
- Undo does **not** cover freehand breakpoint-curve drags (curve lives in `apvts.state`,
  not as an APVTS param; param undo re-syncs the curve).
- Sample-slot transient persistence is **path-based** (no embedded audio yet).
- Safety limiter is **sample-peak**, not true-peak.
- Crash logging is Standalone-only (no plugin-host capture yet).

---

## TL;DR for the next session  *(historical — pre-v1.1, see CURRENT HANDOFF above)*

KickAss is a **VST3 + Standalone kick-drum synth** for Windows (macOS + CLAP planned for v1.x). The DSP is a faithful port of `reference/BazzismRebuild.py` with critical additions: 4× oversampling around the tanh stage, DC blocker, soft-clip ceiling, transient layer with switchable Sine/Noise/Both sources.

**5 phases complete (Phase 0 scaffold → Phase 5 presets). Phase 6 (polish + DAW validation) is the remaining v1.0 work.**

The plugin loads, plays kicks, has a full UI with 25 attached knobs, a live visualizer that re-renders on parameter changes, and 16 factory presets ship in the binary.

---

## What's working (verified by clean builds + smoke test in Standalone)

| Subsystem | Status |
|---|---|
| Build: VST3 + Standalone (Win x64) | ✅ |
| Build matrix for macOS (CMake) | ✅ (untested — no Mac available in current sessions) |
| 25 APVTS parameters with snake_case IDs (Python-compatible) | ✅ |
| `KickEngine` DSP — Python-port + oversampler + DC blocker + soft-clip + transient layer | ✅ |
| Sample-accurate MIDI trigger | ✅ |
| `setLatencySamples` reported to host | ✅ |
| 1000×680 fixed window with red brand wordmark | ✅ |
| 6 labeled rotary-knob panels (PITCH/AMP/SCOOP/TRANSIENT/DRIVE/MASTER) | ✅ |
| Custom LookAndFeel (knob + button + toggle + combo) | ✅ |
| Visualizer (pitch env / amp env / scoop wash / waveform / playhead / readouts) | ✅ |
| 16 factory presets (6 from Python + 10 from research/04 §7) | ✅ |
| Preset combo in header (factory + user) | ✅ |
| Note-snap dropdown (Off, C1..C2) | ✅ |
| `.kickpreset` (APVTS XML) save/load | ✅ |
| `.json` save/load — round-trips with Python `BazzismRebuild.py` presets | ✅ |
| JSON drag-and-drop onto editor | ✅ |
| DAW state persistence (`getStateInformation` / `setStateInformation`) | ✅ |
| Portability rules (no Windows-specific APIs, `juce::File::getSpecialLocation`, `FontOptions`) | ✅ |

---

## What needs **manual testing** (next session should ask the user to run these)

The previous session built and confirmed compile/link success but the user only briefly opened the standalone once (couldn't trigger MIDI through the standalone's audio settings — a config issue, not a plugin bug). The next session should ask the user to test:

### 1. Audible playback
- [ ] Open `build\KickAss_artefacts\Release\Standalone\KickAss.exe`
- [ ] Click the visualizer area → hear a kick + see the white playhead sweep
- [ ] If no sound: gear icon → Audio/MIDI settings → enable a MIDI input OR use the on-screen MIDI keyboard

### 2. Knobs reshape the kick in real time
- [ ] Turn `Pitch · Start` from 8000 → 1000 → click play → kick should sound dull/woodier
- [ ] Turn `Drive · Tail` up → tail should get fatter / saturated
- [ ] Toggle `Master · Invert Phase` → no audible change in isolation, polarity flips
- [ ] Turn `Scoop · Depth` to 50% → see purple wash in visualizer + hear midrange dip

### 3. Presets cycle correctly
- [ ] Open preset combo → pick "Projektor Punch" → visualizer reshape + audibly different kick
- [ ] Pick "Hi-Tech Snap A" → much snappier kick, brighter click
- [ ] Pick "Deep Twilight G" → long sub tail, soft attack
- [ ] All 16 should be in the dropdown under `--- Factory ---` header

### 4. Save / load round-trip
- [ ] Tweak some knobs → click SAVE → save as `test.kickpreset` (default extension)
- [ ] Switch to a factory preset → kick changes
- [ ] Click LOAD → pick `test.kickpreset` → original tweaked state restored

### 5. JSON Python compatibility
- [ ] In KickAss: click SAVE → choose `*.json` filter → save `test.json`
- [ ] Open `test.json` in a text editor → confirm flat dict like `{"start_freq": ..., "invert_phase": false, ...}`
- [ ] Open `reference\BazzismRebuild.py` separately → load `test.json` via its built-in "Load .json" button → preset should appear with matching knob positions

### 6. JSON drag-and-drop
- [ ] Drag a `.json` file onto the editor window → red border highlight appears during drag → preset loads on drop

### 7. Note snap
- [ ] Pick "F#1" from the note snap dropdown → `End Freq` knob should jump to 46.25 Hz

### 8. DAW integration (the big test)
- [ ] Copy `build\KickAss_artefacts\Release\VST3\KickAss.vst3\` to `C:\Program Files\Common Files\VST3\` (run `scripts\deploy_vst3.bat` from an elevated shell)
- [ ] Open Reaper / Ableton / FL Studio
- [ ] Add KickAss as an instrument plugin
- [ ] Verify: shows up under Gleinkaa, opens 1000×680 window, all 25 params automatable
- [ ] Trigger MIDI → hear kick
- [ ] Save project → close → reopen → state restored

### 9. Latency reporting
- [ ] In Reaper, view the plugin's reported latency — should be small (oversampler ~5–10 samples)
- [ ] Render to disk → confirm the kick lands sample-accurately on the grid

---

## Known issues / weirdness

1. **Standalone .exe can become file-locked** if it's still running when CMake tries to rebuild. Always close the running standalone before running `cmake --build`.
2. **Auto-deploy to `C:\Program Files\Common Files\VST3\` requires an elevated shell** — the previous session set `COPY_PLUGIN_AFTER_BUILD FALSE` and added `scripts\deploy_vst3.bat` for the manual elevated copy step.
3. **Build target syntax matters**: use `cmake --build build --config Release --target KickAss_All`. Multi-target `--target X Y` is interpreted as a single target by MSBuild.
4. **Several deprecation warnings on JUCE 8** in places where the previous session didn't switch to `FontOptions` — `Source/KickAssLookAndFeel.cpp` and `WaveformDisplay.cpp` are clean, but check `PluginEditor.cpp::paint()` for any remaining `juce::Font (size, bold)` calls. These are non-blocking.
5. **`scripts/compare_audio.py` is a stub** for the C++ side — it renders Python reference WAVs but doesn't yet drive the standalone CLI to compare. Phase 6 should add a `--render-preset NAME OUT.wav` CLI flag to the standalone for automated parity testing.
6. **No FFT spectrum view yet** — visualizer is WAVE only. `[WAVE | SPECTRUM | BOTH]` tabs from research/03 §3.2 are deferred to v1.1.
7. **The standalone's MIDI keyboard isn't shown by default** — user has to enable it via Options → Audio/MIDI Settings or use a hardware controller. The big PLAY KICK button in the footer + clicking the visualizer canvas both bypass this for testing.

---

## Phase 8 (Polish): completed items

All items below landed in the 2026-05-28 session. Build is clean, 11/11 tests pass.

| # | Item | Status |
|---|---|---|
| 1 | **EXPORT WAV** — 24-bit stereo PCM, duration from AHDSR/curve, JUCE 8 `AudioFormatWriterOptions` | ✅ done |
| 2 | **A/B compare** — two `ValueTree` slots; plain-click toggles, Shift-click copies current into inactive slot | ✅ done |
| 3 | **Modified-preset `*` indicator** — APVTS listener on all params; suppression flag prevents false trips during load/A-B | ✅ done |
| 4 | **Knob right-click popup menu** — `setPopupMenuEnabled(true)` on every `Slider` (JUCE built-in Reset/Edit/Copy/Paste) | ✅ done |
| 5 | **Auto Play 4/4** — `LinearBar` BPM slider (60–200, default 145) + AUTO toggle; `juce::Timer` calls `triggerNote` every `60000/bpm` ms; restarts on BPM change | ✅ done |
| 6 | **JUCE 8 deprecation sweep** — `AudioParameterFloat` positional ctors → `attrs()` helper; `createWriterFor` → `AudioFormatWriterOptions` | ✅ done |

## Remaining v1.0 work (next session)

1. **DAW validation matrix**: load in Reaper, Ableton Live, FL Studio, Bitwig. Confirm parameters, state recall, latency, MIDI routing. Document any DAW-specific issues.
2. **Arrow-key cycling**: when preset combo is focused, ← / → cycles through presets with live audition.
3. **`setTitle`/`setDescription`** on every Slider/Button for screen-reader hosts.
4. **CI build script** in `scripts/build.bat` + GitHub Actions Windows runner (optional, useful for v1.0.0 release tag).
5. **v1.0.0 git tag + GitHub Release** with a zip of `KickAss.vst3` + `KickAss.exe`.

---

## v1.x roadmap (after v1.0 ships)

From `docs/ROADMAP.md` § v1.1+ backlog. Not the next session's job unless explicitly asked.

- macOS build (universal binary, VST3 + AU + Standalone)
- CLAP support via `clap-juce-extensions` submodule
- FFT spectrum view in visualizer (the SPECTRUM tab)
- Separate sub voice with phase-coherent crossover (research/01 §4.2)
- Knack / mid-band saturator (research/01 §4.6)
- Saturation type enum (Tanh/SoftClip/HardClip/Tube/Foldback)
- Built-in micro-reverb / Space
- Resizable window
- 3D spectrogram strip
- Sample slot for transient layer
- Transient shaper (envelope-driven attack/sustain gains)
- MIDI Learn
- Bezier envelope editing

---

## Architecture cheat-sheet

```
KickAssProcessor
├── apvts (juce::AudioProcessorValueTreeState) — 25 params, snake_case IDs
├── engine (KickEngine) — realtime DSP, called from processBlock
├── offlineEngine (KickEngine) — UI-thread render for the visualizer
└── presetManager (std::unique_ptr<PresetManager>)
                  ├── 16 factory presets (in PresetManager.cpp kFactoryPresets[])
                  ├── user presets at %APPDATA%\KickAss\Presets\*.kickpreset / *.json
                  └── apply* methods write into apvts via setValueNotifyingHost

KickAssEditor (resizable 1280×820, FileDragAndDropTarget, Timer)
├── lnf (KickAssLookAndFeel)
├── visualizer (WaveformDisplay — listens to APVTS, debounce 66ms, offline-renders)
├── header: presetCombo, noteSnapCombo, SAVE, LOAD
├── 6× ParamPanel: PITCH(6 knobs), AMP(6), SCOOP(3), TRANSIENT(5), DRIVE(2), MASTER(3)
└── footer: PLAY KICK, EXPORT WAV, A/B
```

Threading model:
- `processBlock`: hard-realtime. No allocs, no locks, no logging. Reads APVTS via cached atomic pointers, calls `engine.renderBlock`.
- UI thread: WaveformDisplay listens to APVTS, debounces 66 ms, calls `processor.offlineRender` which uses the dedicated `offlineEngine` instance (no contention with realtime `engine`).
- Phase accumulator is `double`, audio is `float`. Oversampler latency is reported to host.

---

## Files map

```
KickAss/
├── CMakeLists.txt                # JUCE plugin target, per-OS FORMATS matrix
├── README.md                     # User-facing overview
├── STATUS.md                     # Current phase + smoke-test instructions
├── CLAUDE.md                     # Project-scoped rules for AI sessions (includes §8b portability rules)
├── HANDOFF.md                    # ← THIS FILE (next session entry point)
├── .gitignore
├── Source/
│   ├── PluginProcessor.{h,cpp}   # KickAssProcessor + APVTS layout
│   ├── PluginEditor.{h,cpp}      # KickAssEditor + header/footer + drag-drop
│   ├── KickEngine.{h,cpp}        # Pure DSP, oversampler, all the audio math
│   ├── KickAssLookAndFeel.{h,cpp}  # Palette + rotary/button/combo drawing + KickFonts
│   ├── WaveformDisplay.{h,cpp}   # Visualizer canvas
│   └── PresetManager.{h,cpp}     # 16 factory + .kickpreset + .json round-trip
├── reference/
│   ├── BazzismRebuild.py         # Spec (the Python prototype)
│   └── renders/python_*.wav      # Reference renders from compare_audio.py
├── docs/
│   ├── ARCHITECTURE.md           # Source of truth — all decisions live here (incl. §8b portability)
│   ├── ROADMAP.md                # Phased plan + v1.1 backlog
│   ├── HANDOFF.md                # (this file)
│   └── research/01..04.md        # Brainstorm outputs (read-only)
├── scripts/
│   ├── deploy_vst3.bat           # Manual elevated-shell deploy to Program Files VST3
│   └── compare_audio.py          # Python-side renders + band-energy summary (parity test stub)
└── build/                        # CMake build dir (gitignored)
    └── KickAss_artefacts/Release/
        ├── VST3/KickAss.vst3/    # The plugin bundle
        └── Standalone/KickAss.exe  # Standalone
```

---

## How to build (from a fresh clone)

```powershell
# Requires:
#   - JUCE at C:\Users\glein\JUCE  (or set -DJUCE_DIR=...)
#   - Visual Studio 2022 build tools

cd D:\GoogleDrive\B_projects\KickAss
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target KickAss_All

# To deploy VST3 system-wide (one-time, elevated):
scripts\deploy_vst3.bat
```

First build will take ~3-5 minutes (JUCE compiles from scratch). Subsequent builds are ~30 s.

---

## How to verify Phase 5 specifically

If the next session wants to confirm the previous Phase 5 work landed correctly:

```powershell
cd D:\GoogleDrive\B_projects\KickAss
git log --oneline  # should show commits up through Phase 5
ls Source\PresetManager.{h,cpp}  # should exist
ls reference\renders\python_*.wav  # 2 files: psytrance_default + projektor_punch
```

In the running plugin:
- Preset combo should have a `--- Factory ---` divider and 16 entries below
- Picking any preset should change the visualizer + the knobs visibly
- SAVE then LOAD should round-trip a `test.kickpreset` file
- Dragging a `.json` file onto the window should load it

---

## Contact / repo

- GitHub: <https://github.com/Gleinkaa/KickAss>
- Owner: Gleinkaa (`gleinkaa@gmail.com`)
- Local path: `D:\GoogleDrive\B_projects\KickAss\`

---

## TL;DR for the user (Gleinkaa) reading this

You finished a working VST3 kick-drum synth in one session: 5 phases, 9 commits, ~3000 lines of C++. The plugin builds clean, loads in DAWs, has 16 Projektor-grade factory presets, a real-time visualizer, and a Python-compatible preset format. **The next session should help you do hands-on DAW testing (Reaper/Ableton) and Phase 6 polish (Auto Play, A/B, WAV export).** macOS + CLAP are baked into the architecture but not built (no Mac available).
