# KickAss — STATUS

## Current phase: Architecture locked, ready for JUCE scaffold

| Phase | Status | Output |
|-------|--------|--------|
| 0. Scaffold | ✅ done | folder tree, Python reference copied, git init |
| 1. Research (4-agent brainstorm) | ✅ done | `docs/research/01..04.md` |
| 2. Synthesis | ✅ done | `docs/ARCHITECTURE.md` + `docs/ROADMAP.md`, locked 2026-05-27 |
| 3. JUCE skeleton | ⏳ next | empty plugin loads in DAW with all 25 APVTS params |
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
