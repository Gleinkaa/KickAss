# KickAss

VST3 kick drum synthesizer for clinical kick design — psytrance-first, Projektor-grade.

**Author:** Gleinkaa
**Framework:** JUCE (C++17, CMake)
**Formats:** Windows VST3 + Standalone; macOS VST3 + AU + Standalone (universal)
**Version:** 1.1.0
**Status:** Implemented — DSP, UI, visualizer, preset system and a headless test suite are in
`Source/` and `Tests/`. Open items are the Inno Setup installer (script written, never compiled)
and the DAW validation matrix. [STATUS.md](STATUS.md) has the detailed state,
[docs/HANDOFF.md](docs/HANDOFF.md) the smoke-test checklist.

## Vision

A focused, single-purpose kick designer inspired by Bazzism, Audija Bazzism, and Sonic Academy KICK 2 — but built around the workflow needed to design modern psytrance kicks (Projektor, Astrix, Captain Hook, etc.).

The starting DSP model is the Python prototype at [reference/BazzismRebuild.py](reference/BazzismRebuild.py):
pitch envelope (start → mid → end) with curve, AHDSR amp envelope with optional scoop,
transient click, drive (base + tail-weighted tanh saturation), polarity invert.

## Project layout

```
KickAss/
├── Source/              JUCE plugin code (PluginProcessor, PluginEditor, KickEngine,
│                        WaveformDisplay, BreakpointEditor, PresetManager, KickAssLookAndFeel,
│                        CrashLog, EnvCurve, SpectrumUtil)
├── Tests/               Headless console tests: CurveRoundtrip, DspSafety, SpectrumUtil,
│                        CrashLog, RenderWav
├── reference/           Original Python prototype + audio references
├── docs/
│   ├── research/        4-agent brainstorm outputs
│   ├── ARCHITECTURE.md  Synthesized design doc (post-research)
│   ├── ROADMAP.md       Phased build plan
│   ├── PHASE_6B_DESIGN.md
│   ├── HANDOFF.md       Session handoff + smoke-test checklist
│   └── MACOS_BUILD.md   macOS build & test plan
├── installer/           Inno Setup script (KickAss.iss — not compiled yet)
├── scripts/             build_installer.bat, deploy_vst3.bat, compare_audio.py
├── .github/workflows/   build-macos.yml (macOS CI build + ctest, JUCE 8.0.12)
└── CMakeLists.txt       JUCE plugin + 5 test targets
```

## Reference plugins studied

- **SnareRhythmGen** (`C:\Users\glein\SnareRhythmGen`) — own JUCE VST3, used as architecture template
- **Sinevibes Gridmorph** — UI/workflow reference
- **Bazzism / Audija Bazzism** — feature-parity baseline
- **Projektor releases** — kick analysis (frequency content, envelope shapes, harmonic stacking)

## Build

JUCE is expected at C:/Users/glein/JUCE by default (sibling layout, mirrors SnareRhythmGen layout);
override with `-DJUCE_DIR=...`. C++17, JUCE 8.x (CI pins 8.0.12).

Windows (VST3 + Standalone):

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Outputs land under build/KickAss_artefacts/Release/: the VST3 bundle (copy it to your VST3 folder
with `scripts/deploy_vst3.bat` from an elevated shell) and the standalone executable. End-user
install steps are in `INSTALL.txt`.

macOS (VST3 + AU + Standalone, universal arm64 + x86_64): build on a Mac, or via the `build-macos`
GitHub Actions workflow (`workflow_dispatch` or a `v*` tag) — it checks out JUCE 8.0.12, builds the
plugin plus the test targets, runs ctest and uploads KickAss-macOS.zip. Details and the manual
test checklist are in `docs/MACOS_BUILD.md`; end-user steps in `INSTALL_macos.txt`. There is no AU
on Windows.

## Tests

Five headless console targets, wired as ctest entries (`KICKASS_BUILD_TESTS` is ON by default):

```
cmake --build build --config Release --target KickAss_Tests KickAss_DspTests KickAss_SpectrumTests KickAss_CrashTests KickAss_RenderTests
ctest --test-dir build -C Release --output-on-failure
```

`curve_roundtrip`, `dsp_safety`, `spectrum_util`, `crash_log`, `render_wav`. The DSP net renders all
16 factory presets (non-silence, finite, state round-trip, parameter fuzz) and was written before
the DSP changes it guards.

## Distribution

`ClickMe_InstallKickAss.bat` and `INSTALL.txt` / `INSTALL_macos.txt` are for users. The Windows
installer is `installer/KickAss.iss` + `scripts/build_installer.bat`; it has never been compiled
(Inno Setup 6 is not installed on the build machine) and the script fails gracefully with
instructions. Installation is not signed or notarized on either platform.

## License

MIT — see LICENSE.
