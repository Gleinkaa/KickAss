# KickAss — Claude instructions

## Project-scoped rules

- **Framework:** JUCE at `C:\Users\glein\JUCE`. Mirror the structure used in `C:\Users\glein\SnareRhythmGen` (sibling JUCE path, CMake-driven, `juce_add_plugin` style).
- **Author:** Gleinkaa. Manufacturer code `Glka`, plugin code `Kick`.
- **Reference DSP:** `reference/BazzismRebuild.py`. Treat it as the spec — when porting to C++, preserve sonic behavior bit-for-bit unless explicitly improving.
- **Audio thread discipline:** no locks, no allocations, no logging on `processBlock`. Use APVTS atomics or lock-free FIFOs for UI ↔ audio.
- **UI thread discipline:** all `juce::Component` work on message thread. For waveform/envelope viz, render to an offline buffer on a background thread, then paint.
- **Presets:** keep JSON load/save compatible with the Python prototype's format (same parameter keys) so users can migrate presets.

## Things NOT to do

- Don't add features the Python prototype doesn't have without asking. Scope first.
- Don't try to make Python the DSP runtime — VST3 means C++.
- Don't break SnareRhythmGen — it's a reference, not a dependency.

## Research outputs live in `docs/research/`

After research phase, `docs/ARCHITECTURE.md` is the single source of truth for build order.
