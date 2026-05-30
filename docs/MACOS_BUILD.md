# KickAss — macOS build & test plan

> Goal: produce a macOS build (VST3 + AU + Standalone), get it to a Mac user, and
> confirm it loads + sounds correct. The C++ is already portable (no Win32 APIs,
> all paths via `juce::File::getSpecialLocation`, CMake has the `APPLE` branch).
> The only blocker is that **JUCE compiles natively — a Mac build must be built on
> a Mac or a macOS CI runner.** The Windows `.exe`/installer will NOT run on macOS.

This doc is both the human guide and the implementation checklist for the build
session.

---

## Strategy: GitHub Actions macOS runner (no Mac required to *build*)

The repo is public, so macOS CI minutes are free. A `macos-14` runner (Apple
Silicon) compiles the universal binary, runs ctest, and uploads a zip we can hand
to the Mac user. Signing/notarization is deferred — the first test build is
**unsigned**, and the Mac user removes the Gatekeeper quarantine flag manually
(steps in `INSTALL_macos.txt`).

### Implementation checklist (do these in the build session)

1. **Add `.github/workflows/build-macos.yml`** (content below). Pin the JUCE `ref`
   to the **same JUCE major/minor the local Windows build uses** — this project is
   on **JUCE 8.x**. Confirm the exact local tag with:
   `git -C C:/Users/glein/JUCE describe --tags` (or check JUCE's `CMakeLists.txt`
   `project(JUCE VERSION …)`), and set the workflow `ref:` to match. Default below
   is `8.0.4`.
2. **Fix the macOS mono-font fallback.** `Source/KickAssLookAndFeel.h` hardcodes
   `"Consolas"` (Windows-only) in `KickFonts::mono`. Change it to a cross-platform
   monospace, e.g. `juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain))`
   — or a stack `"Consolas"` on Win / `"Menlo"` on mac. Cosmetic only; readouts
   currently fall back to a default mono on Mac.
3. **Add `INSTALL_macos.txt`** at repo root (content below).
4. **Commit + push.** The workflow triggers on `workflow_dispatch` and on `v*` tags.
5. **Run it:** `gh workflow run build-macos.yml` then watch with
   `gh run watch` / `gh run list`. On green, download:
   `gh run download <run-id> -n KickAss-macOS` → `KickAss-macOS.zip`.
6. **Report back** the workflow status + the artifact, and post the zip somewhere
   the Mac user can grab it (attach to the `v1.1.0` release with
   `gh release upload v1.1.0 KickAss-macOS.zip`, or share the run's artifact link).

### Workflow file — `.github/workflows/build-macos.yml`

```yaml
name: build-macos
on:
  workflow_dispatch:
  push:
    tags: ['v*']

jobs:
  macos:
    runs-on: macos-14            # Apple Silicon; builds universal arm64+x86_64
    steps:
      - uses: actions/checkout@v4

      - name: Checkout JUCE
        uses: actions/checkout@v4
        with:
          repository: juce-framework/JUCE
          ref: 8.0.4             # <-- align to the local JUCE version
          path: JUCE

      - name: Configure
        run: cmake -S . -B build -G Xcode -DJUCE_DIR=${{ github.workspace }}/JUCE

      - name: Build
        run: cmake --build build --config Release --target KickAss_All

      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure

      - name: Stage artifacts
        run: |
          set -e
          OUT="build/KickAss_artefacts/Release"
          mkdir -p dist
          cp -R "$OUT/VST3/KickAss.vst3"        dist/
          cp -R "$OUT/AU/KickAss.component"     dist/
          cp -R "$OUT/Standalone/KickAss.app"   dist/
          cd dist && zip -ry KickAss-macOS.zip KickAss.vst3 KickAss.component KickAss.app

      - uses: actions/upload-artifact@v4
        with:
          name: KickAss-macOS
          path: dist/KickAss-macOS.zip
          if-no-files-found: error
```

Notes:
- `-G Xcode` (multi-config) is the representative generator for plugin bundles +
  signing on macOS. If the AU build complains, try without `-G Xcode` (Makefiles).
- If `KickAss_All` isn't a valid target on the Xcode generator, build the format
  targets explicitly: `--target KickAss_VST3 KickAss_AU KickAss_Standalone`.
- AU/VST3 SDKs ship inside JUCE — no extra SDK download needed.

---

## `INSTALL_macos.txt` (repo root) — for the Mac user

```
KickAss — macOS install (UNSIGNED test build)

This is a v1.1 test build with no Apple code-signing yet, so macOS Gatekeeper
will block it until you clear the quarantine flag (steps 3–4). One-time only.

1. Unzip KickAss-macOS.zip.

2. Copy the plug-ins into your user plug-in folders:
   - KickAss.vst3      -> ~/Library/Audio/Plug-Ins/VST3/
   - KickAss.component -> ~/Library/Audio/Plug-Ins/Components/   (AU: Logic/GarageBand)
   (KickAss.app is a standalone you can just double-click — see step 4.)

3. Clear Gatekeeper quarantine. Open Terminal and run (adjust the .app path):
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/KickAss.vst3
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/KickAss.component
     xattr -dr com.apple.quarantine ~/Downloads/KickAss.app

4. First launch of the Standalone: right-click KickAss.app -> Open -> Open
   (bypasses the "unidentified developer" warning, one time).

5. Restart your DAW. Find it under:  Gleinkaa -> KickAss  (Instrument / Synth / Drum).
   - Logic only shows AUs that pass validation. If it doesn't appear, run:
        auval -v aumu Kick Glka
     and send me the output.

6. Apple Silicon: the build is universal (arm64 + x86_64), so it works native on
   M-series and under Rosetta.

Cosmetic note: the small monospace readouts may render in a different font on Mac.
```

---

## What to ask the Mac user to verify (test checklist)

1. **Loads** in the DAW (Reaper/Ableton = VST3; Logic/GarageBand = AU) and the
   Standalone `.app` opens.
2. **Sound:** click the visualizer / PLAY KICK -> hear a kick, see the playhead.
3. **Knobs** reshape the kick live (e.g. Pitch·Start, Drive·Tail, Scoop·Depth).
4. **Presets** cycle and visibly/audibly change the kick (16 factory presets).
5. **Visualizer:** WAVE/SPECTRUM/BOTH tabs + the new **TRANS** tab (scroll to zoom,
   double-click to fit), and the deep zoom.
6. **Save/Load** a `.kickpreset` round-trips; **EXPORT WAV** writes a file.
7. **State recall:** save the DAW project, reopen -> settings restored.
8. Note any **crashes, graphical glitches, font issues, or AU-validation failures.**

---

## Later (frictionless distribution — not needed for the first test)

- **Code-sign + notarize** with an Apple **Developer ID** ($99/yr): `codesign`
  (Developer ID Application) -> `notarytool submit --wait` -> `stapler staple`.
  Removes the quarantine dance for everyone. Wire the cert + Apple ID as repo
  secrets and add a sign/notarize step to the workflow.
- **Mac installer:** package as `.pkg` (`productbuild`) or `.dmg` (`create-dmg`).
  The current Inno Setup `.iss` is Windows-only.
- **`auval` gate in CI** once notarization lands.
