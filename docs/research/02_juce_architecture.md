# KickAss — JUCE / C++ Architecture Research

Author: research agent
Date: 2026-05-27
Inputs read:
- `D:\GoogleDrive\B_projects\KickAss\reference\BazzismRebuild.py` (DSP spec to port)
- `C:\Users\glein\SnareRhythmGen\CMakeLists.txt`
- `C:\Users\glein\SnareRhythmGen\Source\PluginProcessor.{h,cpp}`
- `C:\Users\glein\SnareRhythmGen\Source\PluginEditor.{h,cpp}`
- `C:\Users\glein\SnareRhythmGen\Source\SnareEngine.{h,cpp}`
- Web research on Sinevibes (see §10)

This doc is decision-grade. The next agent should be able to start writing `CMakeLists.txt` and `PluginProcessor.h` from this doc alone, no code in this doc.

---

## 0. Project at a glance

KickAss is a **mono, one-shot kick-drum VST3 instrument**. Any MIDI note-on retriggers the kick from t=0. The DSP is a direct port of `BazzismRebuild.py`:

1. Pitch envelope: `f_start → f_mid → f_end` with two sweep times and a shared curve exponent.
2. Sine oscillator driven by the cumulative-phase integration of that pitch envelope.
3. AHDSR-style volume envelope with curve exponent (`v_curve`), plus an optional scoop dip.
4. Optional 5 ms click transient (10 kHz → 2 kHz sine sweep with quadratic fade).
5. Drive section: `tanh(audio * drive) / tanh(drive)`, with optional tail-weighted extra drive ramp (`base + ramp**2 * tail*5`).
6. Optional polarity invert.
7. 100-sample linear fade-out at the tail (de-click).

The kick is **mono in the Python prototype** and stays mono in C++ until proven otherwise; output bus is stereo (both channels carry the same signal).

---

## 1. CMake skeleton

Mirrors `SnareRhythmGen/CMakeLists.txt`. JUCE is expected at `../JUCE` (same convention as the sibling project — confirmed at `C:\Users\glein\JUCE`).

```
cmake_minimum_required(VERSION 3.22)
project(KickAss VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives one level up (same as SnareRhythmGen convention)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

juce_add_plugin(KickAss
    COMPANY_NAME              "Gleinkaa"
    PLUGIN_MANUFACTURER_CODE  Glka
    PLUGIN_CODE               Kick
    PRODUCT_NAME              "KickAss"
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE       # auto-deploy to system VST3 folder
)

target_sources(KickAss PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/KickEngine.cpp
    Source/WaveformDisplay.cpp
)

target_include_directories(KickAss PRIVATE Source)

target_compile_definitions(KickAss PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(KickAss
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp                 # for juce::dsp::Oversampling + saturator helpers
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
```

Modules confirmed required:
- `juce_audio_utils` — `AudioProcessor`, `AudioProcessorValueTreeState`, GUI base classes.
- `juce_audio_formats` — only needed if we let users drag a custom click sample later; currently NOT used by the algorithm, but include it cheaply now so we don't have to retouch CMake when v1.1 adds optional click samples.
- `juce_dsp` — `juce::dsp::Oversampling` for the saturator stage. SnareRhythmGen does NOT link this; KickAss does.

Differences vs SnareRhythmGen CMake:
- `NEEDS_MIDI_OUTPUT FALSE` (we are not a MIDI generator).
- `COPY_PLUGIN_AFTER_BUILD TRUE` (SnareRhythmGen has it FALSE; flipping it speeds iteration).
- Plugin codes: `Glka` / `Kick`.
- Add `juce_dsp` (the differentiator).

DECISION: Use the CMake block above verbatim, with `juce_dsp` included from day one and `COPY_PLUGIN_AFTER_BUILD TRUE`.

---

## 2. Class layout

```
+----------------------------------+
| KickAssProcessor                 |   juce::AudioProcessor
|  - apvts (APVTS)                 |
|  - engine : KickEngine           |
|  - processBlock()                |   pulls params -> engine.setParams()
|  - prepareToPlay()               |   -> engine.prepare()
|  - get/setStateInformation()     |
|  - savePresetToFile/JSON()       |
|  - loadPresetFromFile/JSON()     |
|  - offlinePreview() -> AudioBuffer<float>  (UI thread, NOT realtime)
+----------------------------------+
            |
            v owns
+----------------------------------+
| KickEngine                       |   pure C++ DSP, no JUCE GUI deps
|  - prepare(sr, blockSize)        |
|  - setParams(const KickParams&)  |
|  - triggerNote(midiNote, vel)    |   resets phase, schedules note
|  - renderBlock(buffer, n)        |   adds to (or writes) stereo buffer
|  - renderOffline(out, durMs)     |   for the UI visualizer
|  - isActive() const              |
|  Private:                        |
|  - phase accumulator (double)    |
|  - sampleIndex (int64_t)         |
|  - per-sample interpolated env   |
|  - juce::dsp::Oversampling drvOS |
|  - tanh saturator state          |
+----------------------------------+
            ^
            | reads engine state for preview
+----------------------------------+
| KickAssEditor                    |   juce::AudioProcessorEditor
|  - rotary knobs (APVTS attached) |
|  - WaveformDisplay child         |
|  - PresetBar (save/load JSON)    |
|  - Trigger button (MIDI test)    |
+----------------------------------+
            |
            v owns
+----------------------------------+
| WaveformDisplay                  |   juce::Component + juce::Timer (~10 Hz)
|  - subscribes to APVTS via       |
|    AudioProcessorValueTreeState::|
|    Listener for "dirty" flag     |
|  - on dirty -> processor         |
|     .offlinePreview() -> draws   |
+----------------------------------+
```

### Polyphony

The Python algorithm is mono and the kick is a one-shot. Bass-music kicks are commonly retriggered before the previous one finishes — choking is correct for sub-heavy kicks (legato sub) but voice-stealing crossfade is more musical for percussive use. **Pick: single-voice, retrigger restarts phase from 0 with a 2 ms crossfade to mask the discontinuity.** No `Voice` class, no `juce::Synthesiser`. If polyphony is ever needed it can be retrofitted by giving `KickEngine` an inline-pool of N state structs, but ship v1 mono.

DECISION:
- `KickEngine` is a plain class owned by value inside the processor (not a `juce::SynthesiserVoice`).
- Single voice with phase-reset + 2 ms crossfade on retrigger.
- `KickAssEditor` is one window, no tabs, with one `WaveformDisplay` child component.

---

## 3. Parameter layout (APVTS)

Every parameter ID matches the Python prototype key exactly so JSON presets round-trip without renaming. Ranges and defaults come from `BUILTIN_PRESETS["Psytrance (Default)"]` and the `create_slider` calls in `BazzismRebuild.py`.

Skew: where the Python `ttk.Scale` has a logarithmic feel (frequencies, sweep times), we use `NormalisableRange` with a skew factor < 1 so the UI knob feels like the Python one.

| Group  | ID            | Display name        | Type    | Min   | Max    | Default | Skew | Notes |
|--------|---------------|---------------------|---------|-------|--------|---------|------|-------|
| PITCH  | `start_freq`  | Start Freq          | Float Hz| 100   | 15000  | 8000    | 0.30 | log feel |
| PITCH  | `mid_freq`    | Mid Freq            | Float Hz| 50    | 1000   | 150     | 0.30 | log feel |
| PITCH  | `end_freq`    | End Freq            | Float Hz| 20    | 100    | 49      | 0.50 | tuning-critical |
| PITCH  | `sweep_time_1`| Sweep Time 1        | Float ms| 0.1   | 50.0   | 10.0    | 0.30 | log feel |
| PITCH  | `sweep_time_2`| Sweep Time 2        | Float ms| 1.0   | 250.0  | 80.0    | 0.30 | log feel |
| PITCH  | `pitch_curve` | Pitch Curve         | Float   | 0.1   | 10.0   | 3.0     | 0.50 | exponent |
| AMP    | `vol_attack`  | Attack              | Float ms| 0.0   | 30.0   | 2.0     | 0.50 |  |
| AMP    | `vol_hold`    | Hold                | Float ms| 0.0   | 50.0   | 10.0    | 0.50 |  |
| AMP    | `vol_decay_1` | Decay 1             | Float ms| 0.0   | 150.0  | 50.0    | 0.50 |  |
| AMP    | `vol_sustain` | Sustain             | Float % | 0.0   | 100.0  | 40.0    | 1.0  | linear |
| AMP    | `vol_decay_2` | Decay 2             | Float ms| 0.0   | 700.0  | 150.0   | 0.50 |  |
| AMP    | `vol_curve`   | Volume Curve        | Float   | 0.1   | 10.0   | 3.0     | 0.50 | exponent |
| SCOOP  | `scoop_start` | Scoop Start         | Float ms| 0.0   | 50.0   | 10.0    | 0.50 |  |
| SCOOP  | `scoop_length`| Scoop Length        | Float ms| 1.0   | 100.0  | 30.0    | 0.50 |  |
| SCOOP  | `scoop_depth` | Scoop Depth         | Float % | 0.0   | 100.0  | 0.0     | 1.0  | linear |
| CLICK  | `click_vol`   | Click Vol           | Float   | 0.0   | 1.0    | 0.0     | 1.0  | linear |
| DRIVE  | `drive`       | Base Drive          | Float   | 1.0   | 10.0   | 1.5     | 0.50 |  |
| DRIVE  | `tail_drive`  | Tail Saturation     | Float   | 0.0   | 10.0   | 0.0     | 0.50 |  |
| DRIVE  | `invert_phase`| Invert Phase        | Bool    | —     | —      | false   | —    | `AudioParameterBool` |
| MASTER | `output_gain` | Output Gain         | Float dB| -24   | +6     | 0.0     | 1.0  | NEW, not in Python |
| MASTER | `pitch_track` | Pitch Tracking      | Float % | 0.0   | 100.0  | 0.0     | 1.0  | NEW, MIDI note → end_freq |

Notes:
- IDs use **snake_case to match the Python keys** for JSON portability. SnareRhythmGen uses camelCase — we intentionally diverge for preset compatibility.
- `output_gain` and `pitch_track` are KickAss-only additions (Python has none). They live in MASTER and don't break JSON load (unknown keys are tolerated in the import path, missing keys fall back to defaults).
- Parameter version-hint `1` (second arg of `juce::ParameterID`) — same as SnareRhythmGen.
- Parameter count: 19 from Python + 2 KickAss-only = 21.

DECISION: One static `createParameterLayout()` method on `KickAssProcessor`, same pattern as SnareRhythmGen's `PluginProcessor.cpp:5-127`. IDs are snake_case to preserve Python JSON round-trip.

---

## 4. Threading model

### `processBlock` rules (hard contract)

- No allocations: no `new`, no `std::vector::push_back`, no `juce::String` builds.
- No locks: all parameter reads are `apvts.getRawParameterValue(id)->load()` (lock-free atomic).
- No logging, no file I/O, no `juce::MessageManager` calls.
- No `std::sin`/`std::tanh` in tight inner loops if a SIMD helper exists; `juce::dsp::FastMathApproximations::tanh` is acceptable. (The Python uses `np.tanh` — accuracy doesn't matter audibly.)
- Phase accumulator is `double` to avoid drift on long tails. Audio is `float`.

### UI ↔ engine communication

The kick is **one-shot, short (<1 s)**, and the visualiser wants to show the entire waveform shape, not "what played in the last hit". This makes the offline-preview approach correct:

```
                +--------------------+
                |  KickAssEditor     |
                |  (UI thread)       |
                +---------+----------+
                          |  param changed (APVTS::Listener::parameterChanged)
                          v
                +--------------------+
                | WaveformDisplay    |
                | "dirty" flag = 1   |
                +---------+----------+
                          |  on next Timer tick (10 Hz coalesce)
                          v
       +-----------------------------------------+
       | KickAssProcessor::offlinePreview()      |
       |  - takes a snapshot of APVTS values     |
       |  - constructs a temporary KickEngine    |
       |    (or a copy with engine.snapshot())   |
       |  - renders into a juce::AudioBuffer     |
       |    on the UI thread (not realtime)     |
       |  - returns the buffer to the UI         |
       +-----------------------------------------+
                          |
                          v
                +--------------------+
                | WaveformDisplay    |
                | downsamples,       |
                | paints min/max     |
                | per pixel column   |
                +--------------------+

         +---------------------+
         |  REALTIME PATH      |
         |  processBlock()     |
         +----------+----------+
                    |  MIDI note-on ?
                    v
         +---------------------+
         |  engine.trigger()   |
         |  engine.renderBlock |
         +---------------------+
                    no ring buffer to UI, no atomic write to UI for visualisation
```

Rationale for offline preview over live ring buffer:
- A kick is ~200-700 ms; live ring buffer would show only "the most recent hit". The user needs to see the **shape implied by current knob positions** even when not triggering.
- Mirrors the Python visualiser exactly (`generate_audio_data()` is called on every slider movement).
- Avoids the realtime→UI sample copy and the associated atomicity headache.
- Cost: one full DSP render every ~100 ms during knob drag. At 44.1 kHz × ~500 ms × mono = ~22 k samples. Cheap.

To coalesce a drag, the WaveformDisplay should debounce: on dirty, start a 50 ms one-shot timer and only render at the end. SnareRhythmGen's `PatternDisplay` uses `startTimerHz(30)` and unconditionally repaints; that's fine for cheap paints but wasteful for our case — explicitly debounce.

### Engine state vs realtime audio

`engine.setParams()` is called from `processBlock` at the top of every block (read APVTS, copy into a small POD `KickParams` struct, hand to engine). The engine **must not** rebuild expensive lookup tables in `setParams()`; everything is recomputed cheaply per-sample. Unlike `SnareEngine::rebuildPhrase()` which is allowed to be expensive because it's gated by a "needRebuild" check — KickEngine has no such heavy phase.

DECISION:
- Offline preview, no live ring buffer.
- `processBlock` follows the standard no-alloc / no-lock / lock-free-atomic rules.
- `WaveformDisplay` debounces 50 ms on parameter change before calling `offlinePreview()`.

---

## 5. MIDI trigger model

Bazzism-style: **any MIDI note-on retriggers the kick from t = 0.** Note-offs are ignored (the kick plays out its own envelope). Velocity scales output amplitude linearly (0..1). All notes trigger; channel is ignored.

Optional `pitch_track` parameter (0..100 %): MIDI note number transposes `end_freq` (and proportionally `mid_freq`, `start_freq`) by `(midiNote - 36) / 12` semitones × tracking%. At 0 % the plugin behaves like the Python prototype (note number ignored). At 100 % the kick is fully chromatic — useful for tonal sub kicks where the user wants C1 = 49 Hz, C2 = 98 Hz, etc.

### Comparison with SnareRhythmGen

`SnareRhythmGen::processBlock` (lines 248-255) sets a `triggered = true/false` bool on any note-on/off and uses the **transport** (`getPlayHead()->getPosition()`) to schedule pattern hits. It's a sequencer; MIDI input is a gate, not a per-note trigger.

KickAss is the opposite: **every note-on is a trigger event**, transport is irrelevant. Implementation in pseudocode (no real code per instructions):

```
for each midi message in this block:
    if it's a note-on:
        engine.triggerNote(noteNumber, velocityNormalized,
                           sampleOffset = metadata.samplePosition)
```

The engine renders into the output buffer respecting the per-sample trigger offset (sample-accurate MIDI). SnareRhythmGen's `triggerVoice()` does the same offset-respecting render for sample playback (PluginProcessor.cpp:309-333) — we copy that pattern.

DECISION:
- Any MIDI note-on, any channel, any note → retrigger from t=0.
- Velocity scales output 0..1 (no curve; the drive nonlinearity provides taste).
- `pitch_track` parameter controls how much MIDI note transposes the pitch envelope (default 0 = legacy Python behavior).
- Note-offs are ignored.
- Sample-accurate trigger via the MIDI metadata's `samplePosition`.

---

## 6. State / preset persistence

Three layers:

### 6.1 DAW state — APVTS XML in binary block

Identical to `SnareRhythmGenProcessor::getStateInformation` / `setStateInformation` (PluginProcessor.cpp:442-470). Write APVTS state as XML, push through `copyXmlToBinary`. No sample paths to worry about (KickAss has no samples in v1).

### 6.2 User preset files — XML `.kickpreset`

Same as SnareRhythmGen's `.srpreset` flow. APVTS state XML written to file. This is the DAW-portable format.

### 6.3 Python-compatible JSON import/export — `.json`

The Python prototype reads and writes flat JSON dicts like:

```
{"start_freq": 8000.0, "mid_freq": 150.0, ..., "invert_phase": false}
```

Implement two methods:
- `bool exportPythonJson(const juce::File&)` — iterate APVTS parameters, write a flat JSON dict using `juce::var` / `juce::JSON::toString`. Bool params become `true`/`false`, all others become numbers.
- `bool importPythonJson(const juce::File&)` — `juce::JSON::parse`, for each key call `apvts.getParameter(key)->setValueNotifyingHost(normalisedValue)`. Unknown keys ignored, missing keys leave the current value.

This means the user can prototype a kick in Python, save JSON, drop into KickAss, and continue. **Major UX win for free** because we already chose snake_case parameter IDs in §3.

The KickAss-only parameters (`output_gain`, `pitch_track`) are exported but absent in legacy Python JSONs — that's fine, importer leaves them at default.

DECISION: Support all three. APVTS XML for DAW state (`getStateInformation`/`setStateInformation`). `.kickpreset` for DAW-portable presets. `.json` for Python round-trip. File chooser in the editor offers both ".kickpreset" and ".json" as save targets.

---

## 7. Oversampling for the tanh saturator

The Python uses `np.tanh(audio * drive) / np.tanh(np.max(drive))` with no oversampling — it relies on the fact that the kick's energy is below ~500 Hz so aliasing products from the soft-clip are mostly below Nyquist. That's a luxury we can't count on in a VST: at 44.1 kHz with `drive=10`, harmonics extend well past 10 kHz and will fold.

Use `juce::dsp::Oversampling` around **only the drive stage**:

```
[pitch env] -> [sine osc] -> [amp env + scoop] -> [+ click] -> [OS up 4x] -> [tanh saturator with ramp] -> [OS down 4x] -> [polarity invert] -> [tail fade] -> [output gain]
```

Configuration:
- Factor: **4x** (2 stages of 2x IIR filtering). 2x is cheaper but the Python's `drive=8..10` produces audible aliasing at 44.1 kHz; 4x is the sweet spot. `juce::dsp::Oversampling<float>(numChannels=1, factor=2, FilterType=filterHalfBandPolyphaseIIR, isMaxQuality=true)` — note JUCE's factor argument is `log2`, so `factor=2` means 4x.
- Latency: query `osSampler.getLatencyInSamples()` in `prepareToPlay`, report via `getLatencySamples()` → `setLatencySamples()`. This matters for DAW PDC; do NOT skip it.
- Allocate the OS once in `prepare()`; never in `processBlock`.

The click transient (high-frequency 5 ms sweep) is generated **before** the OS-up — it benefits from being smoothed by the upsampling filter. Polarity invert is post-OS-down (sign flip is bandlimit-safe). The tail fade is post-everything.

The `tail_drive` parameter ramps drive over the kick duration (Python: `base + (linspace**2) * tail*5`). Inside the engine, maintain a per-sample drive value that interpolates over the note's lifetime — feed it as a sample-by-sample multiplier into the upsampled buffer.

DECISION: 4x oversampling (`juce::dsp::Oversampling` log2-factor=2, half-band polyphase IIR) wrapped tightly around the tanh stage. Report latency to the host. Click is pre-OS; polarity and tail fade are post-OS.

---

## 8. Risks pulled from SnareRhythmGen experience

### Patterns to copy (proven to work)

1. **APVTS-attachment pattern in editor** — `SliderAttachment` per knob with a `KnobWithLabel` struct (`PluginEditor.h:86-91`). Trivially adaptable; copy `setupKnob`/`setupIntKnob` verbatim.
2. **LookAndFeel subclass for cohesive theme** — `SnareGenLookAndFeel` with custom `drawRotarySlider` (`PluginEditor.cpp:47-97`). Adopt the same approach with KickAss's accent colors (suggest cyan/orange palette matching the Python `ACCENT_COLOR = "#ff0055"` magenta).
3. **`createParameterLayout()` as a static method** returning a vector of `unique_ptr<RangedAudioParameter>` (`PluginProcessor.cpp:5-127`).
4. **`updateEngineParams()` called at the top of `processBlock`** — single source of truth for engine state. Cheap.
5. **Async `juce::FileChooser` with member persistence** — the chooser must outlive the callback (`PluginEditor.cpp:466-481`). Same shape for save/load preset and JSON import/export.
6. **State persistence sets `apvts.replaceState` carefully** — same `xml->hasTagName(apvts.state.getType())` guard (`PluginProcessor.cpp:457-458`).
7. **`juce::AudioBuffer<float>` ownership in engine, no raw pointers**.

### Patterns to NOT copy

1. **`triggered` bool gated by transport playback** (`PluginProcessor.cpp:259-277`). Snare needs transport; KickAss doesn't care about transport. Remove all `getPlayHead()` calls.
2. **The whole sample-loading subsystem** (`loadSample`, `triggerVoice`, `SpinLock sampleLock`, voice array). KickAss has no user-loaded samples. Delete this entire concern.
3. **`producesMidi() = true` / `swapWith(generatedMidi)`** (`PluginProcessor.cpp:31`, `337`). KickAss generates audio, not MIDI. `producesMidi() = false`.
4. **Multiple voices** — SnareRhythmGen has 8 polyphonic sample voices for stacked snare hits. KickAss is 1 voice with phase-reset crossfade. Don't bring in `std::array<SampleVoice, 8>`.
5. **`startTimerHz(30)` unconditional repaint** — fine for the sequencer playhead, wasteful for our static waveform. Use debounced render-on-change instead.

### Gaps in SnareRhythmGen that KickAss must add

1. **`juce::dsp::Oversampling`** — SnareRhythmGen has no nonlinear stage and no oversampling. New territory; budget extra integration time.
2. **Reporting `getLatencySamples()`** — SnareRhythmGen returns 0 implicitly. KickAss MUST call `setLatencySamples(osSampler.getLatencyInSamples())` after `prepare()`.
3. **JSON import/export** (Python compat) — SnareRhythmGen only does XML. We need `juce::JSON` round-trip code in the processor.
4. **Offline preview render path** — SnareRhythmGen's pattern display reads a copy of `phrasePattern` under a mutex (`SnareEngine.h:79-83`). We need a different thing: a UI-thread DSP renderer. The engine class should expose a `renderOffline(buffer, durationMs)` that does NOT touch the realtime state.
5. **Sample-accurate MIDI trigger offset** — SnareRhythmGen *does* use sample offsets when generating MIDI (line 701-703); we'll consume them in the same shape but on the audio render side.

### Bugs and smells in SnareRhythmGen worth not repeating

1. `processBlock` reads APVTS pointers via `*apvts.getRawParameterValue(id)` every block — fine, but the pattern of doing it inside `updateEngineParams()` rebuilds a whole `SnareParams` struct each block. For KickAss this is still cheap (21 params); keep it.
2. `SnareEngine::processBlock` takes a `std::lock_guard<std::mutex>` on the realtime thread (`SnareEngine.cpp:616`). That's a soft real-time violation — the mutex is uncontended ~always, but a UI-thread `rebuildPhrase()` under the same lock means a glitch is possible. **KickEngine must not take any locks on the realtime thread.** Use lock-free atomics or double-buffering for any UI↔RT data.

DECISION: Copy editor/APVTS/LookAndFeel patterns; drop the sample/voice/MIDI-generation infrastructure; add oversampling, JSON I/O, offline preview, and a strictly lock-free realtime path.

---

## 9. Build / deploy

- **Generator**: Visual Studio 2022 (`-G "Visual Studio 17 2022" -A x64`). Same as SnareRhythmGen.
- **Build configs**: Debug for development, Release for testing in DAW. AudioPluginHost (built from JUCE/extras) for sandboxed dev. Standalone target for fastest iteration.
- **`COPY_PLUGIN_AFTER_BUILD TRUE`** — on Windows the VST3 lands in `C:\Program Files\Common Files\VST3\KickAss.vst3\Contents\x86_64-win\KickAss.vst3` (write permissions required on the first build — may need an elevated VS shell or a one-time chown of that folder). Standalone target builds to the project's build dir.
- **Output paths** to know:
  - VST3 install: `C:\Program Files\Common Files\VST3\KickAss.vst3\`
  - Standalone exe: `build\KickAss_artefacts\Release\Standalone\KickAss.exe`
  - VST3 in build (pre-copy): `build\KickAss_artefacts\Release\VST3\KickAss.vst3\`
- **Suggested build commands** (PowerShell, not run by this agent):
  ```
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release --target KickAss_Standalone
  cmake --build build --config Release --target KickAss_VST3
  ```
- **Smoke test workflow**: build Standalone → run → trigger note via on-screen MIDI keyboard or `KickAss_StandalonePlugin` UI → confirm sound. Then build VST3 → rescan in Bitwig/Ableton/FL Studio.

DECISION: VS 2022 x64, `COPY_PLUGIN_AFTER_BUILD TRUE`, Standalone target for daily dev, VST3 for integration testing. First build may need an elevated shell to write the system VST3 folder.

---

## 10. Notes on the "Sinevibes Gridmorph" reference

**Finding: a Sinevibes product literally named "Gridmorph" does not appear to exist.** I ran three web searches (full product name, name + "kick drum synth", name + "VST grid morphing") and found no matches in Sinevibes' catalog, no reviews, and no KVR / Plugin Boutique listings. Sinevibes' product line is dominated by Korg `logue`-SDK hardware plugins (Groove, Bent, Malfunction, Ring, Atom, Drift, Droplet) and a handful of desktop VSTs that are not grid- or morph-themed.

The user may be conflating:
- **Sinevibes' design *philosophy*** — Sinevibes plugins are known for compact, minimal UIs with two parameters per page, built-in lag filters on every modulation source for glitch-free parameter motion, and tempo-synced LFOs/sequencers acting as morph drivers. These are real Sinevibes traits worth borrowing.
- **A grid-morph concept from another vendor** — e.g. Sentinel AV's **Folda** has a literal 2D morph grid where four distortion groups blend by a draggable node. The Bitwig Grid (modular environment) is also "grid + morph" but isn't a plugin.
- **MeldaProduction MMorph or Zynaptiq Morph** — frequency-domain audio morphing, unrelated to kick design.

### JUCE-relevant lessons we can still take from "the Sinevibes style"

1. **Smoothed parameters end-to-end.** Every APVTS-bound parameter should be wrapped in a `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>` with a 5–10 ms ramp inside the engine. Avoids zipper noise when the user drags `drive` or `end_freq` during a held kick. SnareRhythmGen doesn't do this (its params are discrete pattern triggers), so it's a real KickAss-only addition.
2. **Minimal UI, big visualiser.** Sinevibes plugins frequently dedicate the whole UI to one large waveform/spectrum panel with knobs around the edge. The Python prototype already does this (canvas on top, sliders below). Reproduce that layout, don't invent a busier one.
3. **Modulation as first-class shape, not an afterthought.** If KickAss ever grows a v2 with macros or morphable snapshots (interpolate between Preset A and Preset B by a single knob), the right abstraction is: store snapshots as full `KickParams` structs, lerp between two of them per-sample → push into `engine.setParams()`. This is plausible because every parameter is a float and the engine has no internal mode switches. **Not in v1**, but design the engine so it's not blocked.

DECISION: Treat "Sinevibes Gridmorph" as an unverified reference — likely a misremembered name. Borrow the Sinevibes design philosophy (parameter smoothing, big visualiser, minimal UI) explicitly. Leave room in the engine architecture for future preset-morphing without committing to it in v1. **Confirm with the user which plugin they actually meant** before doing more research here.

---

## Summary of decisions (for the next agent)

1. CMake: VS 2022, x64, JUCE at `../JUCE`, modules `juce_audio_utils` + `juce_audio_formats` + `juce_dsp`, codes `Glka`/`Kick`, `COPY_PLUGIN_AFTER_BUILD TRUE`.
2. Three classes: `KickAssProcessor` (APVTS + I/O), `KickEngine` (pure DSP, owned by value), `KickAssEditor` + `WaveformDisplay` (UI). Mono single-voice with retrigger crossfade.
3. 21 APVTS parameters with snake_case IDs (Python-compatible), grouped PITCH/AMP/SCOOP/CLICK/DRIVE/MASTER. Two KickAss-only additions: `output_gain`, `pitch_track`.
4. Realtime path is strict: no allocs, no locks, atomic param reads only. UI visualiser uses an offline-preview render on the UI thread, debounced 50 ms.
5. Any MIDI note-on retriggers from t=0; velocity scales output; optional pitch tracking via `pitch_track`. No transport dependency.
6. State persistence: APVTS XML (DAW), `.kickpreset` (portable), `.json` (Python round-trip). All three.
7. 4x `juce::dsp::Oversampling` around the tanh stage only; report latency to host.
8. Copy from SnareRhythmGen: APVTS/attachment patterns, LookAndFeel, file chooser idiom. Reject: sample voices, MIDI generation, transport gating, RT-thread mutex.
9. Build via VS 2022, `COPY_PLUGIN_AFTER_BUILD TRUE` deploys to system VST3 folder.
10. "Sinevibes Gridmorph" reference unverified; borrow general Sinevibes philosophy (smoothing, big visualiser, room for future morphing) without committing to a specific product's design.

---

## Sources (web research)

- [Bent — Sinevibes](https://www.sinevibes.com/korgbent/)
- [KORG — Sinevibes](https://www.sinevibes.com/korg/)
- [Sinevibes Korg Complete Collection — Plugin Boutique](https://www.pluginboutique.com/product/2-Effects/76-Expansion-Packs/5779-Sinevibes-Korg-Complete-Collection)
- [Malfunction — Sinevibes](https://www.sinevibes.com/malfunction/)
- [Groove multitimbral bass & drum machine — Sinevibes](https://www.sinevibes.com/korggroove/)
- [Folda by Sentinel AV — grid-based morph distortion](https://www.kvraudio.com/product/folda-by-sentinel-av)
- [Five of the best kick drum plugins in 2025 — MusicTech](https://musictech.com/guides/buyers-guide/best-kick-drum-plugins-2025-tested-reviewed/)
- [MORPH 3 by Zynaptiq](https://www.kvraudio.com/product/morph-3-by-zynaptiq)
