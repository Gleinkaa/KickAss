# KickAss — UI / Visualization Design Spec

> Plugin: **KICKASS by Gleinkaa** — JUCE VST3 kick designer
> Document type: design spec (no code)
> Status: draft v1, 2026-05-27
> Baseline: `reference/BazzismRebuild.py` (Python/Tkinter)
> House style ref: `C:\Users\glein\SnareRhythmGen\Source\PluginEditor.{h,cpp}`

The user explicitly asked for **good visualization** — so every decision in this doc defaults to **bold, informative, and screen-filling** over minimal-and-quiet. If you skim only one section, read §3.

---

## 0. Research summary — what to learn / avoid

| Reference | Learn | Avoid |
|---|---|---|
| **BazzismRebuild.py** (baseline) | Single big canvas with waveform + amp envelope overlay; AHDSR + scoop + drive + click model; preset combobox + note-snap dropdown; auto-play at BPM toggle. Yellow envelope on red waveform is *very* readable. | Cramped horizontal sliders stacked in two columns; canvas is only 180px tall (too small for a visualization-first product); no pitch-envelope overlay; no time grid; no frequency labels. |
| **Sonic Academy KICK 2** | Three-layer (clicks / subs / tops) blend is a great mental model; logarithmic envelope axis for percussion timing; in-window graphical EQ. | We are *not* doing layers in v1 — single-osc with scoop/drive is enough. Don't ape their cluttered button-bank header. |
| **Audija KickDrum** | Bézier-curve envelopes with draggable breakpoints **annotated in Hz**; live spectrogram for instant visual feedback; "buffer updates at edit time" — every parameter change re-renders the visualizer. **This is the gold standard for "good visualization" in a kick synth.** | Bézier breakpoint editing is a v2 feature — v1 ships with sliders mapping to a fixed-shape envelope (matches our DSP). |
| **BazzISM 2 (ISM)** | Compact, all-on-one-screen workflow — no tabs, no pages. Kick-tuning to a musical note. | Visualizer is too small; dated skeuomorphic look. |
| **Sinevibes house style** | Hyper-minimal, single-window, instant-character. Confident type. | Often *too* minimal — no waveform, no spectrum. We need more information density than this. |
| **SnareRhythmGen (same author)** | Color palette discipline (cyan accent on near-black with paneled sections); custom rotary `LookAndFeel`; `PatternDisplay` with timer-driven playhead; `SampleDropZone` dashed-border pattern for drop targets; header strip with glow + subtitle; gradient bg. | The dense knob grid is fine for a 19-param rhythm generator but would *waste* KickAss's main asset (the visualizer). KickAss must invert the layout: visualizer dominates, knobs orbit it. |

**Net design thesis:** KICKASS is **Audija's information density × BazzISM's single-screen simplicity × SnareRhythmGen's house palette × a refined version of the Python prototype's red/yellow scheme.**

---

## 1. Visual identity

### 1.1 Color palette

Start from Python (`#1a1a1a` bg, `#ff0055` accent, `#ffcc00` envelope), refine for JUCE depth/contrast and add semantic colors for the multi-layer viz.

| Role | Hex | ARGB literal | Notes |
|---|---|---|---|
| `bg` (window) | `#0c0c10` | `0xff0c0c10` | Slightly bluer/darker than Python's `#1a1a1a` — sits better next to bright accents and matches SnareRhythmGen depth |
| `panel` | `#15151c` | `0xff15151c` | Parameter-panel fill |
| `panelHi` | `#1c1c26` | `0xff1c1c26` | Hover / active panel |
| `canvasBg` | `#07070a` | `0xff07070a` | Deepest — visualizer interior, makes traces pop |
| `gridLine` | `#1e1e2a` | `0xff1e1e2a` | Minor time grid |
| `gridBeat` | `#2c2c40` | `0xff2c2c40` | 10ms / labeled grid |
| **`accentHot`** | `#ff2266` | `0xffff2266` | **Primary brand red** (Python's `#ff0055` nudged warmer; "KICK" feeling) — waveform fill, logo, active toggles |
| `accentHotDim` | `0x66ff2266` | — | Waveform glow halo (40% alpha) |
| **`envAmp`** | `#ffcc00` | `0xffffcc00` | **Amp envelope line** — kept identical to Python because it reads so well |
| **`envPitch`** | `#00d4ff` | `0xff00d4ff` | **Pitch envelope line** — cyan, log-Y, distinct from yellow so they never get confused |
| `envScoop` | `#b388ff` | `0xffb388ff` | Scoop overlay shading (purple, borrowed from SnareGen) |
| `spectrum` | `#00ff88` | `0xff00ff88` | FFT spectrum trace (green = "the result") |
| `playhead` | `#ffffff` | `0xccffffff` | 80% white, glowing line |
| `textBright` | `#e8e8f0` | `0xffe8e8f0` | Param values, slider readouts |
| `textDim` | `#7a7a8a` | `0xff7a7a8a` | Labels, axis ticks |
| `textGhost` | `#4a4a55` | `0xff4a4a55` | Section dividers, "by Gleinkaa" subtitle |
| `warn` | `#ff8844` | `0xffff8844` | Clip indicator, drive-overload, drag-and-drop active border |
| `ok` | `#00e676` | `0xff00e676` | Sample loaded / preset saved confirm |

**Accent strategy:** ONE hot color (`accentHot`) for brand and interaction. Visualizer traces use their own dedicated colors (yellow / cyan / green / purple) — that's not "more accents", that's **data encoding**. Each trace = one meaning; never reuse them for chrome.

### 1.2 Typography

| Use | Font | Size | Weight |
|---|---|---|---|
| Logo "KICKASS" | **Inter** (fallback: Segoe UI) | 22pt | 900 / black, +1px tracking |
| Subtitle "by Gleinkaa" | Inter | 9pt | 400 italic, `textGhost` |
| Section headers ("PITCH", "AMP"…) | Inter | 10pt | 700, all-caps, `accentHot @ 60%` |
| Knob labels | Inter | 10pt | 500, `textDim` |
| Slider value readout | **JetBrains Mono** | 11pt | 500, `textBright` — monospaced so values don't jitter as you drag |
| Visualizer axis ticks | JetBrains Mono | 9pt | 400, `textDim` |
| Buttons (PLAY / EXPORT / SAVE / LOAD) | Inter | 11pt | 700, all-caps |
| Preset combobox | Inter | 11pt | 500 |

JUCE: bundle Inter and JetBrains Mono as binary resources via Projucer's `BinaryData` so the plugin looks identical on every machine. Segoe UI fallback for sanity.

### 1.3 Spacing, radius, strokes

- **8px grid.** All gaps and paddings are multiples of 4 (preferred 8, 16, 24).
- **Outer window padding:** 16px.
- **Panel corner radius:** 8px (matches SnareGen drop-zone aesthetic — 6px there was a touch tight).
- **Knob track stroke:** 3px (same as SnareGen — already tuned).
- **Visualizer border:** 1px `accentHot @ 20%` rounded 8px — a faint hot frame that says "look here".
- **Section divider rule:** 1px `accentHot @ 15%` horizontal hairline.

### 1.4 Brand mark

"**KICKASS**" wordmark with the **·ass** rendered in `accentHot` and **KICK** in `textBright`. Subtitle line below: `by Gleinkaa` in `textGhost` italic. Header strip ~52px tall, full width, on `panel` over `bg`, with a `gradient bg` glow underneath the wordmark (same trick as SnareGen lines 604-619).

---

## 2. Layout

### 2.1 Window size

**1000 × 680, fixed (not resizable in v1).** Rationale:
- SnareRhythmGen is 900×620 → 1000×680 is the next natural step on the same machine and matches modern plugin laptop displays.
- 680 vertical gives 50%+ of vertical space to the visualizer (340px+) — *the* design constraint.
- 1000 wide gives room for FIVE parameter columns instead of Python's 2.

In v2, add `setResizeLimits(900, 600, 1400, 900)` with a corner resizer.

### 2.2 ASCII wireframe (full window)

```
+--------------------------------------------------------------------------------------------+
|  KICK·ASS                                                                                   |  <- HEADER (52px)
|  by Gleinkaa     [Preset: Psytrance ▾]  [♪ End→Note: A1 ▾]  [BPM: 140] [☐ Auto 4/4] [SAVE][LOAD] |
+--------------------------------------------------------------------------------------------+ <- divider hairline
|                                                                                            |
|  +------------------------------------------------------------------------------------+   |
|  |  [WAVE | SPECTRUM | BOTH]  view tabs                              -3 dBFS  ~189ms  |   |
|  |  ╔════════════════════════════════════════════════════════════════════════════════╗ |   |
|  |  ║ 15k Hz ┐                                                                       ║ |   |
|  |  ║        │·· pitch env (cyan, log-Y) ··                                          ║ |   |
|  |  ║   1k Hz┤    ╲                                                                  ║ |   |
|  |  ║        │     ╲___                                                              ║ |   |
|  |  ║  100 Hz┤         ╲_____________________________________________ end freq      ║ |   |  <- VISUALIZER
|  |  ║   20 Hz┘                                                                       ║ |   |     (340px)
|  |  ║        ╔═══════════════════════════════════════════════════════════════╗  amp ║ |   |
|  |  ║   1.0 ─║ ┌─yellow env─┐                                                ║      ║ |   |
|  |  ║        ║ │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ red waveform (mirrored) ▓▓▓▓▓        ║      ║ |   |
|  |  ║   0.0 ─║ │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░       ║      ║ |   |
|  |  ║        ║ │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░                       ║      ║ |   |
|  |  ║  -1.0 ─║ └────────────────────────────────────────────────────────────┘      ║ |   |
|  |  ║   0ms     20      40      60      80      100     120     140     160   180ms║ |   |
|  |  ╚════════════════════════════════════════════════════════════════════════════════╝ |   |
|  |                              [▶ play position cursor when triggered]                 |   |
|  +------------------------------------------------------------------------------------+   |
|                                                                                            |
+----------------+----------------+----------------+--------------------+------------------+
|   PITCH        |   AMP          |   SCOOP        |   CLICK & DRIVE    |   MASTER          |  <- PARAM PANELS
|                |                |                |                    |                   |     (~220px)
|  Start (Hz)    |  Attack (ms)   |  Start (ms)    |  Click Vol         |  ⊕ Invert Phase   |
|  ●━━━━━○━━     |  ●━━━━○━━━━    |  ●━━━○━━━━━    |  ●━━○━━━━━━━       |  ☐                |
|   8000         |    2.0         |    10          |    0.00            |                   |
|                |                |                |                    |  Note Snap        |
|  Mid (Hz)      |  Hold (ms)     |  Length (ms)   |  Base Drive        |  ▾ Off            |
|  ●━━○━━━━━━    |  ●━━○━━━━━━    |  ●━━━━○━━━     |  ●━━━○━━━━━━       |                   |
|    150         |    10          |    30          |    1.5             |  BPM              |
|                |                |                |                    |  [ 140 ]          |
|  End (Hz)      |  Decay 1 (ms)  |  Depth (%)     |  Tail Drive        |                   |
|  ●○━━━━━━━     |  ●━━━○━━━━     |  ●○━━━━━━━     |  ●○━━━━━━━━        |  ☐ Auto Play 4/4  |
|    49          |    50          |    0           |    0.0             |                   |
|                |                |                |                    |  Output dB        |
|  Sweep 1 (ms)  |  Sustain (%)   |                |                    |  ●━━━━○━━━━       |
|  ●━━○━━━━━━    |  ●━━━━○━━      |                |                    |    0.0            |
|    10          |    40          |                |                    |                   |
|                |                |                |                    |                   |
|  Sweep 2 (ms)  |  Decay 2 (ms)  |                |                    |                   |
|  ●━━━○━━━━     |  ●━━━○━━━━     |                |                    |                   |
|    80          |    150         |                |                    |                   |
|                |                |                |                    |                   |
|  Curve         |  Curve         |                |                    |                   |
|  ●━━━○━━━━     |  ●━━━○━━━━     |                |                    |                   |
|    3.0         |    3.0         |                |                    |                   |
+----------------+----------------+----------------+--------------------+------------------+
|     [▶ PLAY KICK]                                  [💾 EXPORT WAV]  [↻ A/B]              |  <- FOOTER (44px)
+--------------------------------------------------------------------------------------------+
```

### 2.3 Vertical budget (680px total)

| Strip | Height | % |
|---|---|---|
| Header | 52 | 7.6% |
| Visualizer (incl. tab bar) | 340 | **50.0%** ✓ |
| Param panels | 224 | 32.9% |
| Footer | 44 | 6.5% |
| Outer padding (top/bot 8) | 20 | 3% |

### 2.4 Horizontal budget (1000px total)

- Outer padding: 16 + 16 = 32
- Param area: 5 columns × ~190px = 950px, with 8px inner gaps (4 gaps × 8 = 32)
- Net: 32 + 950 = 982 ≈ 1000 ✓

---

## 3. The visualizer — THE feature

This is what separates KickAss from Python-prototype-in-a-window. It must look like the most expensive thing on screen.

### 3.1 What it draws (single canvas, overlaid layers)

From back to front:

1. **Time/freq grid.**
   - Vertical lines every 10ms (minor, `gridLine`); every 50ms (major, `gridBeat`) with label "50ms" "100ms" … in `textDim` along the bottom.
   - Horizontal: amp half draws `±1.0` and `0.0`; pitch half draws **20 / 100 / 1k / 10k Hz** ticks in `JetBrains Mono 9pt` on the **left** edge.

2. **Pitch envelope (TOP half of canvas, log Y).**
   - Cyan (`envPitch`) line, 2px, with `withAlpha(0.4)` halo glow 5px wide behind it.
   - Y-axis: **logarithmic** from 20 Hz to 20 kHz. This is non-negotiable — linear would crush the audible range of the kick body (40-200 Hz) into the bottom 1% of the canvas. Audija/KICK2 both do this; baseline.
   - Annotates the three breakpoints (`start_freq`, `mid_freq`, `end_freq`) with tiny `4px × 4px` cyan diamonds + tooltip "8000 Hz" on hover.

3. **Amp envelope shading (BOTTOM half).**
   - Filled polygon from `amp = 0` baseline up to envelope value, `envAmp @ 20%` fill — gives the waveform a "container" feel.
   - Solid 2px `envAmp` line on top of the fill (matches Python's yellow line).

4. **Scoop overlay.** Where scoop is active, **dim** the amp envelope shading with a purple (`envScoop @ 30%`) wash → users can literally see where the volume is being ducked. New idea not in Python — high payoff for "good viz".

5. **Rendered waveform (BOTTOM half, on top of amp shading).**
   - **Mirrored vertical line bars** — same technique as Python `update_visualizer` (line 446 — min/max per pixel column drawn as a vertical line). This is the cheapest and most readable waveform style — keep it.
   - Color: `accentHot` (red).
   - Add: 1px white centerline at `y = ampMidline` so you can see DC offset / asymmetry.

6. **Playback cursor.** When the kick is triggered (`PLAY` button or auto-loop), a 1px `playhead` (80% white) vertical line sweeps from left to right over the duration of the sample, with a soft 4px glow (`withAlpha(0.08)`). Re-uses the SnareGen `PatternDisplay` playhead pattern exactly. 30 Hz timer is enough.

7. **Header strip inside the canvas (top 24px).**
   - Left: view-mode tabs — `[ WAVE | SPECTRUM | BOTH ]`.
   - Right: live readouts — peak dBFS (red if > -0.1), total duration in ms, fundamental at end (note name + Hz: `A1 (55.0 Hz)`).

### 3.2 Alternate views (view-mode tabs)

| Mode | Top half | Bottom half |
|---|---|---|
| **WAVE** (default) | Pitch envelope | Amp envelope + waveform |
| **SPECTRUM** | Live FFT (4096-point, Hann-windowed, log-X 20Hz-20kHz, dB-Y, `spectrum` green trace with `withAlpha(0.25)` fill underneath) | Same waveform/amp as WAVE |
| **BOTH** | Split: pitch env left half, FFT right half | Waveform full width |

FFT is computed off the **last-rendered offline kick buffer** (not the realtime DAW output) — instant, cheap, no audio-thread coupling. Recompute on parameter change with a 16ms debounce.

**Stretch (v1.1, not v1):** 3D-ish spectrogram strip below the waveform — colormap from `bg` → `accentHot` → `envAmp` → white, with time on X and freq on Y (log). 64-bin × 200-frame grid. Only enable if FFT view feels under-informative after user testing.

### 3.3 Rendering tech — Graphics vs OpenGL

**Recommendation: pure `juce::Graphics` (CPU), no `OpenGLContext`.**

Math: 1000×340 canvas redrawn at 30 Hz on parameter change ≈ 10 Mpx/s of fills. SnareGen's `PatternDisplay` already does similar work at 30 Hz on a similar-size canvas without breaking a sweat. JUCE's software renderer is fast enough — and OpenGL on Windows hosts (especially Ableton / FL with multiple plugin instances) has known driver-flakiness costs that aren't worth it for a static-ish viz.

**Frame budget:**
- The waveform itself is **not** redrawn at 60fps — only when a parameter changes (parameter-change-driven re-render, debounced ~16ms). Same model as Audija's "buffer updates at edit time".
- A separate 30Hz timer redraws ONLY the playhead overlay region (`repaint(playheadRect)` — not full repaint) during playback.

Use `juce::Image` cached background trick if profiling shows the grid drawing is hot: render grid+labels once into a cached `Image`, blit, then overlay traces.

### 3.4 Visualizer interactions (v1)

- **Mouse hover anywhere in canvas:** crosshair appears, tooltip shows `"t = 42.0 ms · 184 Hz · -6.2 dB"` for hovered (time, pitch-at-that-time, amp-at-that-time).
- **Click in canvas:** triggers `PLAY` (entire canvas is a giant play button).
- **Double-click:** toggles WAVE / SPECTRUM.
- **Right-click:** view-mode submenu + "Copy current view as PNG" (writes to clipboard via `juce::Image::toPNGData`).

---

## 4. Knob / slider style

### 4.1 Rotary, not horizontal

Python used horizontal `ttk.Scale` because Tk's rotary support is poor. **JUCE → rotary.** Reasons:
1. Five vertical columns × ~6 sliders = 30 controls. Horizontal sliders eat width; rotaries are square and dense.
2. Matches SnareRhythmGen house style → same `LookAndFeel` class lineage.
3. DAW users expect rotaries on a kick designer.

### 4.2 KickAssLookAndFeel subclass

Direct evolution of `SnareGenLookAndFeel::drawRotarySlider` (PluginEditor.cpp lines 47-97), with these changes:

| Property | SnareGen | KickAss |
|---|---|---|
| Knob fill arc | cyan `accent1` | `accentHot` red |
| Knob track | dark navy | `gridLine` (`#1e1e2a`) |
| Pointer dot | cyan | `accentHot` (matches arc) |
| Center cap | `bg` | `panel` with 1px `accentHot @ 30%` border |
| Arc thickness | 3px | 3px (same) |
| Glow halo | none | **NEW:** 8px `accentHot @ 15%` underglow on the active arc — the SnareGen `lines 80-82` trick, copied in. |
| Diameter (placed) | 70px | **64px** (more knobs to fit) |

### 4.3 Slider behavior

- **Drag mode:** `Slider::RotaryHorizontalVerticalDrag` (same as SnareGen — already user-tested with same author).
- **Text box:** `Slider::TextBoxBelow`, **editable**, 60×14, `JetBrains Mono` 11pt, `textBright`. Hidden until hover unless `isMouseOverOrDragging()` — keeps the panel clean.
- **Value-on-hover:** YES. Show under the knob always when mouse is over, fade out 300ms after leave.
- **Double-click → default.** Defaults come from the `Psytrance` preset (matches Python's "Default" preset). Implemented via `slider.setDoubleClickReturnValue(true, defaultVal)`.
- **Right-click context menu:** `[Reset to default | Copy value | Paste value | Edit value… | MIDI Learn (v2)]`.
- **Modifier keys:**
  - `Ctrl` (or `Cmd` on macOS) + drag = fine adjust (`slider.setVelocityModeParameters(...)` or `setMouseDragSensitivity`).
  - `Shift` + drag = lock to nearest step (e.g. 10ms increments on time params).
- **Logarithmic skews** for frequency knobs: `setSkewFactorFromMidPoint(midVal)` — `start_freq` mid at 1000 Hz, `mid_freq` mid at 200 Hz, etc. So the knobs *feel* musical.

### 4.4 Section panels (the container around each knob group)

- Filled `panel` background, 8px radius.
- 1px `accentHot @ 12%` border.
- Section header (`"PITCH" "AMP" "SCOOP" "CLICK & DRIVE" "MASTER"`) drawn inside the top of the panel, 10pt bold caps, `accentHot @ 70%`, left-aligned with 12px inset.
- Hairline 1px divider between header and first knob.

### 4.5 Toggle button (Invert Phase, Auto Play)

Reuse SnareGenLookAndFeel `drawToggleButton` verbatim, swap accent → `accentHot`. It's good.

---

## 5. Interaction patterns

### 5.1 Preset combobox

- Populated from `BUILTIN_PRESETS` (the 6 from Python: Psytrance / Techno Deep / Hardstyle Zap / Projektor Punch / Projektor Deep Sub / Projektor Hard F#) plus **`---User---`** separator, then any `.json` files in `~/Documents/KickAss/Presets/`.
- On selection: apply via `apvts.replaceState` / batch parameter set, mark combobox text as "Psytrance ✱" with asterisk if any param has since been modified.
- Bonus: arrow keys (`←` / `→`) cycle through presets when combobox focused, with live audition.

### 5.2 Note-snap dropdown

- Dropdown with `[Off, C1, C#1, D1, …, C2]` (the Python `NOTES` map exactly).
- Picking a note **sets `end_freq` to the note's Hz** and **disables** the `End Freq` knob (greyed, shows the locked Hz value) until `Off` is reselected. Makes the relationship explicit.

### 5.3 BPM + Auto Play

- `BPM` is a small text-entry (40px wide) + tiny up/down arrows. Range 60-300. Default 140.
- `Auto Play 4/4` toggle: starts a `HighResolutionTimer` that triggers `playKick()` every `60000/BPM` ms.
- **Plus:** when Auto is on, the **playhead in the visualizer loops continuously** — the user sees the kick's full envelope happening in time with the metronome they hear.

### 5.4 Drag-and-drop preset JSON

- The **entire editor** is a `juce::FileDragAndDropTarget` (same pattern as `SnareRhythmGenEditor` lines 561-574 forwarding to `SampleDropZone`).
- Accept `.json` files. Drag-over → window border glows `accentHot` 2px (mirrors SnareGen `dropActive`).
- Dropped → parse → `applyPresetDict` → flash the preset combobox label with `ok` green for 800ms.

### 5.5 Play button (footer left, 50% width)

- Big — 240×32, all-caps "▶ PLAY KICK", `accentHot` fill, white text. On press: `accentHot.brighter(0.2)`, on release back. Triggers `processor.previewKick()` AND restarts the visualizer playhead.

### 5.6 Export WAV / Save preset / Load preset / A-B

- Footer right cluster of 4 secondary buttons, `panel` fill, `accentHot @ 30%` border, hover → `panelHi`. Lift the look from SnareGen `drawButtonBackground` (lines 131-157).
- `A/B`: holds a second hidden parameter snapshot; click toggles "Now showing A" / "Now showing B". Lets users compare two settings instantly. Bonus feature, not in Python.

---

## 6. Accessibility & DAW integration

### 6.1 APVTS host automation

Every visible parameter is an `AudioProcessorValueTreeState` parameter so the DAW can automate it. Parameter IDs and friendly names:

| ID | Name (host shows this) | Range | Default | Skew |
|---|---|---|---|---|
| `startFreq` | "Pitch · Start" | 100…15000 Hz | 8000 | log mid=1000 |
| `midFreq` | "Pitch · Mid" | 50…1000 Hz | 150 | log mid=200 |
| `endFreq` | "Pitch · End" | 20…100 Hz | 49 | linear |
| `sweep1` | "Pitch · Sweep 1" | 0.1…50 ms | 10 | linear |
| `sweep2` | "Pitch · Sweep 2" | 1…250 ms | 80 | linear |
| `pitchCurve` | "Pitch · Curve" | 0.1…10 | 3 | linear |
| `volAttack` | "Amp · Attack" | 0…30 ms | 2 | linear |
| `volHold` | "Amp · Hold" | 0…50 ms | 10 | linear |
| `volDecay1` | "Amp · Decay 1" | 0…150 ms | 50 | linear |
| `volSustain` | "Amp · Sustain" | 0…100 % | 40 | linear |
| `volDecay2` | "Amp · Decay 2" | 0…700 ms | 150 | linear |
| `volCurve` | "Amp · Curve" | 0.1…10 | 3 | linear |
| `scoopStart` | "Scoop · Start" | 0…50 ms | 10 | linear |
| `scoopLength` | "Scoop · Length" | 1…100 ms | 30 | linear |
| `scoopDepth` | "Scoop · Depth" | 0…100 % | 0 | linear |
| `clickVol` | "FX · Click" | 0…1 | 0 | linear |
| `drive` | "FX · Drive" | 1…10 | 1.5 | linear |
| `tailDrive` | "FX · Tail Drive" | 0…10 | 0 | linear |
| `invertPhase` | "FX · Invert" | bool | false | — |
| `output` | "Master · Output" | -24…+6 dB | 0 | linear |

### 6.2 Parameter formatting

A single `valueToTextFunction` for frequency:

```
Hz < 100   → "%.1f Hz"   e.g. "49.0 Hz"      (1 dp)
Hz < 1000  → "%.0f Hz"   e.g. "150 Hz"       (0 dp)
Hz ≥ 1000  → "%.2f kHz"  e.g. "8.00 kHz"     (2 dp)
```

For time:
```
ms < 10    → "%.1f ms"
ms ≥ 10    → "%.0f ms"
```

For dB:
```
"%+0.1f dB" e.g. "+0.0 dB", "-6.5 dB"
```

### 6.3 Accessibility

- Every `juce::Slider` and `juce::Button` gets a `setTitle` + `setDescription` for screen-reader hosts (some DAWs query this).
- Keyboard nav: `Tab` cycles through knobs in reading order; `Up/Down` adjusts focused knob by 1% step.
- Min contrast: all text on `bg` or `panel` is ≥ `#7a7a8a` → contrast ratio ≥ 4.5:1 against `#0c0c10`. (`textGhost @ #4a4a55` is only used for non-functional decorative text — explicit exception.)
- **Color-blind:** the four visualizer trace colors (red waveform / yellow amp / cyan pitch / green spectrum) are chosen to remain distinguishable in deuteranopia simulation (red↔green can collide; we mitigate by giving them **different shapes**: waveform = vertical bars, amp = thick line + fill, pitch = thin line + dots, spectrum = filled curve in different half of canvas).

### 6.4 MIDI learn

**Out of scope for v1.** Stub in the right-click menu for v2. Document in README.

---

## 7. Component tree

```
KickAssEditor : juce::AudioProcessorEditor, juce::FileDragAndDropTarget
├── KickAssLookAndFeel               (singleton, set on editor)
├── HeaderBar                        : juce::Component
│   ├── LogoLabel                    (custom paint, "KICK·ASS" + "by Gleinkaa")
│   ├── PresetComboBox               : juce::ComboBox
│   ├── NoteSnapComboBox             : juce::ComboBox
│   ├── BpmEditor                    : juce::Label (editable)
│   ├── AutoPlayToggle               : juce::ToggleButton
│   ├── SaveButton, LoadButton       : juce::TextButton
│
├── VisualizerCanvas                 : juce::Component, juce::Timer
│   ├── viewModeTabs                 (3-state toggle: WAVE/SPECTRUM/BOTH)
│   ├── readouts                     (peak dB, duration, end note)
│   ├── (internal) renderedBuffer    : juce::AudioBuffer<float>
│   ├── (internal) ampEnvBuffer      : std::vector<float>
│   ├── (internal) pitchEnvBuffer    : std::vector<float>
│   ├── (internal) fftBuffer         : std::array<float, 2048>
│   ├── (internal) playheadSamplePos : std::atomic<int>
│   └── recomputeOnParamChange()     (debounced via juce::Timer 16ms)
│
├── ParamPanel ×5                    : juce::Component  (PITCH / AMP / SCOOP / CLICK&DRIVE / MASTER)
│   └── each owns:
│       ├── KnobWithLabel { Slider, Label, SliderAttachment }  ×N
│       ├── ToggleButton + ButtonAttachment   (where applicable: Invert Phase)
│       └── ComboBox + ComboBoxAttachment     (Note Snap lives in MASTER)
│
└── FooterBar                        : juce::Component
    ├── PlayButton                   : juce::TextButton (big)
    ├── ExportWavButton              : juce::TextButton
    ├── AbButton                     : juce::TextButton (toggle-style)
    └── fileChooser                  : std::unique_ptr<juce::FileChooser> (persistent for async)
```

`KnobWithLabel` is structurally identical to SnareRhythmGen's (lines 86-91) — keep that struct verbatim, swap LookAndFeel.

---

## 8. What to copy from SnareRhythmGen vs do differently

### Copy verbatim

1. **`KnobWithLabel` struct** — exact same `{Slider, Label, unique_ptr<SliderAttachment>}` shape.
2. **`setupKnob` / `setupIntKnob` helpers** (lines 576-599) — rename `SnareGenLookAndFeel` → `KickAssLookAndFeel` and they work as-is.
3. **`drawButtonBackground`** (lines 131-157) — the rounded button with hover/press states is already good. Just swap accent.
4. **`drawToggleButton`** (lines 99-129) — same.
5. **Async `FileChooser` pattern** (lines 464-497, 519-535) — `unique_ptr` member that persists, `launchAsync` with lambda, swap `.srpreset` → `.json`.
6. **Drag-and-drop forwarding pattern** (lines 561-574) — editor implements `FileDragAndDropTarget`, forwards to a subcomponent. We can either route to a `PresetDropZone` (analogous to `SampleDropZone`) or just handle drops at the editor level if we don't want a visible drop zone (probably the latter — the whole window accepts drops, the canvas glow is feedback enough).
7. **Header strip with glow** (lines 610-624) — title-with-glow trick (draw text at `+1,+1` in dim accent, then on top in bright accent) is sharp and free.
8. **Playhead pattern in `PatternDisplay`** (lines 387-410) — glowing 6px-wide rounded rect at low alpha + 1px crisp line + small triangle top. **Reuse 1:1** for visualizer playhead.
9. **`startTimerHz(30)` for redraws** + `setSize(W, H)` last in ctor.

### Do differently

1. **Invert the layout hierarchy.** SnareGen is "knobs are the star, pattern strip is a side dish at the bottom." KickAss is "visualizer is the star, knobs orbit it." The visualizer must be ≥ 50% of vertical real-estate.
2. **Bigger viewer, fewer-but-richer knob columns.** SnareGen has 18+ knobs in a 9-wide × 3-tall grid. KickAss has 19 controls organized as 5 *labeled* panels — semantic grouping > raw density.
3. **No "DICE" / randomize-seed button.** Kick design is deterministic; randomization makes no sense here. Replace footer slot with `A/B` compare button.
4. **No sample drop zone in the editor** — KickAss is a *synth* (no sample input). The drop zone disappears; that region is now visualizer.
5. **Different accent color.** Cyan→red. Brand differentiation between the two plugins in the same author's catalog matters; a user with both open should immediately know which is which.
6. **Log Y-axis on pitch viz.** SnareGen doesn't have a frequency axis at all; this is genuinely new territory.
7. **Live FFT.** SnareGen has no spectral view; KickAss needs one because tonal kick design is about where the energy lives.
8. **APVTS for *everything*.** SnareGen has APVTS but the seed / preset state is partly outside it. Don't repeat — every audible parameter goes through APVTS and the visualizer reads from APVTS only.
9. **Drop the "MIDI Pattern Generator v1.3" subtitle pattern** — KickAss subtitle is the modest `by Gleinkaa` (signature, not version).

---

## 9. Open questions for the build phase

These are explicitly *not* answered here and should be settled when the DSP/processor doc is written:

1. Does the offline kick render block the audio thread? (Almost certainly should run on a `juce::ThreadPool` job; visualizer reads the rendered buffer when the job posts back.)
2. Stereo or mono? Visualizer assumes mono — probably ship mono v1, add stereo width in v2.
3. Sample rate for the offline render — DAW SR or fixed 48k? Probably fixed 48k for visualizer consistency, then DAW-SR for the realtime DSP.
4. Where do user `.json` presets live on Windows? Recommend `%APPDATA%\KickAss\Presets\`. macOS: `~/Library/Application Support/KickAss/Presets/`.

---

## 10. Build-order suggestion (so visualization lands first)

If the implementing agent wants a working prototype fast, build in this order:

1. Empty editor with `KickAssLookAndFeel` and header strip (no controls, just paint).
2. Five empty `ParamPanel`s with section headers (still no knobs).
3. APVTS wired with all 19 parameters + their formatters.
4. Knobs in panels, attached, no audio yet.
5. `VisualizerCanvas` with just the grid + axis labels.
6. Offline render → waveform + amp envelope (the Python `update_visualizer` ported 1:1).
7. Pitch envelope overlay.
8. Playhead.
9. View tabs + FFT spectrum mode.
10. Preset combobox + JSON IO.
11. Note snap, BPM, auto-play.
12. Drag-and-drop, A/B compare, polish.

Each step gives a screenshot-worthy intermediate state, so progress is visible and the "good visualization" feeling shows up by step 7.

---

**End of spec.** Reviewer: confirm color palette (§1.1), window size (§2.1), and the visualizer feature list (§3.1) before any C++ is written.
