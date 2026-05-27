# KickAss

VST3 kick drum synthesizer for clinical kick design — psytrance-first, Projektor-grade.

**Author:** Gleinkaa
**Framework:** JUCE (C++17, CMake)
**Formats:** VST3, Standalone
**Status:** Research / pre-scaffold (see [STATUS.md](STATUS.md))

## Vision

A focused, single-purpose kick designer inspired by Bazzism, Audija Bazzism, and Sonic Academy KICK 2 — but built around the workflow needed to design modern psytrance kicks (Projektor, Astrix, Captain Hook, etc.).

The starting DSP model is the Python prototype at [reference/BazzismRebuild.py](reference/BazzismRebuild.py):
pitch envelope (start → mid → end) with curve, AHDSR amp envelope with optional scoop,
transient click, drive (base + tail-weighted tanh saturation), polarity invert.

## Project layout

```
KickAss/
├── Source/              JUCE plugin code (PluginProcessor, PluginEditor, KickEngine)
├── reference/           Original Python prototype + audio references
├── docs/
│   ├── research/        4-agent brainstorm outputs
│   ├── ARCHITECTURE.md  Synthesized design doc (post-research)
│   └── ROADMAP.md       Phased build plan
├── scripts/             Build / deploy helpers
└── CMakeLists.txt       JUCE plugin target
```

## Reference plugins studied

- **SnareRhythmGen** (`C:\Users\glein\SnareRhythmGen`) — own JUCE VST3, used as architecture template
- **Sinevibes Gridmorph** — UI/workflow reference
- **Bazzism / Audija Bazzism** — feature-parity baseline
- **Projektor releases** — kick analysis (frequency content, envelope shapes, harmonic stacking)

## Build (placeholder — finalized after research phase)

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Requires JUCE at `C:\Users\glein\JUCE` (sibling path, mirrors SnareRhythmGen layout).
