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

## Portability rules (ARCHITECTURE §8b — locked 2026-05-27)

KickAss ships for Windows v1.0 then **macOS + CLAP in v1.x**. Apply on every change:

- **No Windows-specific APIs.** No `<windows.h>`, no `WinMain`. Everything via JUCE.
- **No hardcoded paths.** Use `juce::File::getSpecialLocation` for app data, presets.
- **Bundle fonts as `BinaryData`.** Don't assume Segoe UI (Win-only) or SF Pro (macOS-only) is present. Inter + JetBrains Mono are the chosen fonts; ship them with the plugin.
- **No format-specific code.** No `#ifdef JUCE_VST3` branches in the engine or editor. The CMakeLists handles format differences; the C++ code is format-agnostic.
- **No platform-specific code in DSP.** `KickEngine.cpp` must compile on Win/Mac/Linux unchanged.
- **CLAP comes via `clap-juce-extensions` submodule.** No source-level changes when it's added in v1.x — just a CMake glue call. Stay CLAP-friendly: APVTS state, no host-specific UI tricks.

## Research outputs live in `docs/research/`

After research phase, `docs/ARCHITECTURE.md` is the single source of truth for build order.
