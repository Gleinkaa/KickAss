# KickAss — STATUS

## Current phase: v1.1 (Sonic Variety) in progress — regression net + saturation types + safety limiter + spectrum view landed (2026-05-29). Branch: `v1.1-sonic-variety`. Next: undo/redo, sample-slot transient.

### v1.1 progress (2026-05-29, branch `v1.1-sonic-variety`)
- ✅ **DSP regression net** — `Tests/DspSafetyTest.cpp` (new `KickAss_DspTests` ctest target). Guards: all 16 factory presets render non-silent + finite; full getState/setState round-trip per preset; 200-iteration param fuzz across both envelope modes (no NaN/Inf/absurd peak); degenerate-combo coverage (max drive, zero sweeps/decays, full scoop). Built BEFORE any DSP change.
- ✅ **Saturation type selector** — `sat_type` choice {Tanh, Soft Clip, Hard Clip, Tube, Foldback}. Default = Tanh (index 0) so existing presets are bit-for-bit unchanged. Waveshapers + per-type normalization in `KickEngine::applyPostStages`; DRIVE-panel dropdown; test `test_saturationTypes_distinctFiniteAndDefaultTanh`. (commit 846580d)
- ✅ **Output safety limiter** — `safety_limit` bool (default ON), final clamp at −0.1 dBFS AFTER output gain (closes the gap where +Output dB pushed peaks back over 0 dBFS past the −0.3 dB character soft-clip). Sample-peak only (not true-peak — documented). MASTER-panel toggle; test `test_safetyLimiter_enforcesCeiling`. (commit df35118)
- ✅ **Spectrum FFT view** — `[WAVE | SPECTRUM | BOTH]` tabs in the visualizer (default WAVE = no visual regression). FFT math in header-only `Source/SpectrumUtil.h` (Hann window, log-freq 20Hz–20kHz, dB axis), tested headlessly via new `KickAss_SpectrumTests` target. (commit 6695846)

## Phase 8 (Polish): EXPORT WAV + A/B compare + modified-preset indicator + Auto Play 4/4 done (2026-05-28). Open: DAW validation matrix.

**Comprehensive smoke-test checklist in [docs/HANDOFF.md](docs/HANDOFF.md). Next session starts there.**

**Smoke test:**
1. Run `build\KickAss_artefacts\Release\Standalone\KickAss.exe`
2. Plug a MIDI keyboard or click the on-screen kbd
3. Hear a kick. Default = Psytrance preset (F#1, 8k → 150 → 49 Hz sweep)
4. Tweak parameters via the (still placeholder) automation panel — the kick reshapes immediately

`reference\renders\python_*.wav` holds the Python reference renders for A/B comparison.


**Build outputs** (after `cmake --build build --config Release`):
- `build/KickAss_artefacts/Release/VST3/KickAss.vst3/` — drag into your DAW's VST3 folder, or run `scripts/deploy_vst3.bat` from an elevated shell to copy to `C:\Program Files\Common Files\VST3\`
- `build/KickAss_artefacts/Release/Standalone/KickAss.exe` — run directly for dev iteration
- `build/KickAss_Tests_artefacts/Release/KickAss_Tests.exe` — headless test suite (Phase 6b curve round-trip). 11 tests, ~0.1s total.
- `build/KickAss_DspTests_artefacts/Release/KickAss_DspTests.exe` — DSP regression net (non-silence / state round-trip / param fuzz / extremes / saturation types / safety limiter). ~1s.
- `build/KickAss_SpectrumTests_artefacts/Release/KickAss_SpectrumTests.exe` — FFT helper (`SpectrumUtil.h`) bin-accuracy + edge cases. ~0.03s.
- Run all three at once: `ctest --test-dir build -C Release --output-on-failure`


| Phase | Status | Output |
|-------|--------|--------|
| 0. Scaffold | ✅ done | folder tree, Python reference copied, git init |
| 1. Research (4-agent brainstorm) | ✅ done | `docs/research/01..04.md` |
| 2. Synthesis | ✅ done | `docs/ARCHITECTURE.md` + `docs/ROADMAP.md`, locked 2026-05-27 |
| 3. JUCE skeleton | ✅ done | VST3 + Standalone build (Release), 25 APVTS params wired, 1000×680 placeholder window |
| 4. DSP port | ✅ done | KickEngine implements full Python DSP + 4× oversampled tanh + DC blocker + soft-clip + transient layer |
| 5. UI shell | ✅ done | KickAssLookAndFeel + 6 labeled panels + 25 attached widgets + PLAY button + footer |
| 6. Visualizer | ✅ done | WaveformDisplay: pitch env (cyan log-Y) + amp env (yellow) + scoop wash + red waveform + white playhead + readouts. Click-to-play. |
| 7. Preset system | ✅ done | 16 factory presets baked in, .kickpreset + Python-compatible .json round-trip, drag-drop, note-snap |
| 6b. Breakpoint envelope editor | ✅ done | EnvCurve data model + LUT, BreakpointEditor child of WaveformDisplay, [Simple\|Advanced] AMP-header toggle with knob greying, AHDSR↔curve auto-conversion, mode-tagged persistence (ahdsr/custom), 11-test headless suite (`KickAss_Tests.exe` / `ctest`). Tension UI deferred — data model supports it; v1 ships linear-only. |
| 8. Polish + DAW test | 🔄 in progress | ✅ EXPORT WAV (24-bit PCM, JUCE 8 API) · ✅ A/B compare (ValueTree snapshot, Shift-click copy) · ✅ Modified-preset `*` indicator · ✅ Auto Play 4/4 (BPM 60–200, LinearBar slider, juce::Timer retrigger) · ⏳ DAW validation matrix (Reaper/Ableton/FL) |

## Decisions locked

- **Framework:** JUCE (same as SnareRhythmGen — proven on this machine)
- **Manufacturer code:** `Glka` (matches author convention)
- **Plugin code:** `Kick` (4-char JUCE requirement)
- **Formats:** VST3 + Standalone (no AU on Windows)
- **DSP origin:** port from `reference/BazzismRebuild.py`

## Open questions (resolve in research phase)

- Polyphony? Bazzism is mono-trigger; psytrance use case = MIDI trigger only, so 1-voice is enough but verify
- Oversampling for the saturation stage? (tanh aliases hard at high drive)
- Preset format: stay JSON (matches Python prototype) or switch to JUCE binary state?
- UI toolkit: pure juce::Component or pull in juce::OpenGLContext for fancy spectrum/envelope viz?
