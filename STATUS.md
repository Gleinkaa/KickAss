# KickAss — STATUS

## Current phase: Research

| Phase | Status | Output |
|-------|--------|--------|
| 0. Scaffold | ✅ done | folder tree, Python reference copied, git init |
| 1. Research (4-agent brainstorm) | 🔄 in progress | `docs/research/*.md` |
| 2. Synthesis | ⏳ pending | `docs/ARCHITECTURE.md`, `docs/ROADMAP.md` |
| 3. JUCE scaffold | ⏳ pending | `CMakeLists.txt`, `Source/Plugin*.{h,cpp}`, `KickEngine.{h,cpp}` |
| 4. DSP port | ⏳ pending | KickEngine implements the Python DSP in C++ |
| 5. UI + visualizer | ⏳ pending | parameter UI + waveform/envelope canvas |
| 6. Preset system | ⏳ pending | factory presets, JSON load/save (compatible with Python format) |
| 7. Test in DAW | ⏳ pending | Ableton/FL/Reaper validation |

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
