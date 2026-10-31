# KickFit — fit a KickAss preset to a reference kick

Headless harness plus fit scripts used to recreate Audija KickDrum presets in KickAss.
First result: `presets/Gleinkaa PsyKick E1 152 1-8.kickpreset` (2026-10-31).

## Harness

`KickFit <spec.json|preset.kickpreset> <out.wav> [durationMs] [out.kickpreset]`

The spec is `{"params": {"<param id>": value, ...}, "curve": [[t_ms, v, tension], ...]}`.
Params are set via the APVTS. A curve switches the amp envelope to custom (Advanced) mode.
Output is a 48 kHz mono float WAV rendered by `offlineRender`. Pass a `.kickpreset` as input to
re-render a saved preset.

```
cmake -S tools/kickfit -B tools/kickfit/build -DCMAKE_BUILD_TYPE=Release   # -DJUCE_DIR=... if not ~/apps/JUCE
cmake --build tools/kickfit/build -j
```

## Fit scripts

Run them with `uv run -q --with numpy --with scipy --with soundfile python <script> ...`.
Set `KICKFIT=<path to the KickFit binary>`.
`<work>` is a scratch directory; the scripts read and write their JSON there.

1. `fit2.py <ref.wav> <work>/fit2_w30.json`: model fit of the pitch sweep (log-frequency and
   body phase).
2. `engfit.py <work> <ref.wav>`: Nelder-Mead fit of pitch, curve and phase, rendering the real
   engine at every step. Seed it with a `<work>/final_spec.json`.
3. `envfit.py <work> <ref.wav> [out.kickpreset] [phaseDeg]`: iterative correction of the amp
   curve at 16 fixed times. It sets the output gain for a −0.5 dBFS peak and writes the preset.

## PsyKick E1 152 1-8: how close it is

The reference is the Audija render of `Gleinkaa PsyKick E1 152 1-8`: E1 = 41.2 Hz, length 1/8 at
152 BPM = 197.37 ms.

| What | Result |
|---|---|
| Amp envelope | within 0.14 dB |
| Waveform correlation, 100–197 ms | 0.999 |
| Waveform correlation, 20–100 ms | 0.85 |
| Waveform correlation, 0–20 ms | 0.04 |
| Last upward zero crossing | 197.25 ms (reference 197.17 ms) |

Settings:
- Pitch 3474 → 324 Hz over 11.6 ms, then → 41.20 Hz over 143 ms, curve 5.4.
- Invert phase on, phase offset 358°.
- Hard Clip at drive 1.0 (linear), output +4.6 dB.

Known gap: the first 20 ms. KickAss has two pitch segments sharing one curve exponent, so it
cannot follow Audija's zap: up to ±5 semitones off between 1 and 20 ms, starting near 3.5 kHz
instead of 6 kHz.
