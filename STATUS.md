# KickAss — STATUS

## Current phase: Phase 1 done — silent plugin builds. Next: DSP port (Phase 2)

**Build outputs** (after `cmake --build build --config Release`):
- `build/KickAss_artefacts/Release/VST3/KickAss.vst3/` — drag into your DAW's VST3 folder, or run `scripts/deploy_vst3.bat` from an elevated shell to copy to `C:\Program Files\Common Files\VST3\`
- `build/KickAss_artefacts/Release/Standalone/KickAss.exe` — run directly for dev iteration


| Phase | Status | Output |
|-------|--------|--------|
| 0. Scaffold | ✅ done | folder tree, Python reference copied, git init |
| 1. Research (4-agent brainstorm) | ✅ done | `docs/research/01..04.md` |
| 2. Synthesis | ✅ done | `docs/ARCHITECTURE.md` + `docs/ROADMAP.md`, locked 2026-05-27 |
| 3. JUCE skeleton | ✅ done | VST3 + Standalone build (Release), 25 APVTS params wired, 1000×680 placeholder window |
| 4. DSP port | ⏳ pending | KickEngine + parity test vs Python |
| 5. UI shell | ⏳ pending | LookAndFeel + header + 6 param panels + footer (no viz yet) |
| 6. Visualizer | ⏳ pending | the headline feature — wave + envelopes + FFT + playhead |
| 7. Preset system | ⏳ pending | 16 factory presets, .kickpreset + .json round-trip |
| 8. Polish + DAW test | ⏳ pending | A/B, accessibility, Ableton/FL/Reaper/Bitwig validation |

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
