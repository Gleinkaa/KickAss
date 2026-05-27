# KickAss — DSP Science Research
**Audience:** the C++/JUCE architecture agent porting `reference/BazzismRebuild.py` to a VST3.
**Goal:** a clinical kick designer for modern psytrance (Projektor, Astrix, Captain Hook, Vini Vici, Outsiders).
**Author:** DSP/research agent, 2026-05-27.
**Tone:** opinionated. Units are ms/Hz/dB. Where I cite the Python prototype, I give line numbers.

---

## 0. TL;DR for the porting engineer

The Python prototype (`reference/BazzismRebuild.py`) is a single-voice, sine-only, AHDSR-shaped, tanh-saturated kick generator with a separate 5 ms click sweep, a sinusoidal scoop on the amp envelope, a tail-weighted drive ramp, and an optional polarity invert. It produces a credible "Bazzism-style" kick for the body. It is **not** a Projektor-grade engine. The big four gaps:

1. **No oversampling.** The tanh stage (`BazzismRebuild.py:399, 401`) folds aliases back into the audible band, especially while the pitch is still above ~1 kHz. This is the single most important thing to fix.
2. **One voice for everything.** A Projektor kick has at least 3 quasi-independent layers (click/top, body/punch, sub/tail). One pitch envelope cannot do all three simultaneously and the tail drive ramp is a half-measure that proves it.
3. **The "click" is a sine chirp**, not a transient. Real psy clicks are noise- or wood-based with HPF at 1–3 kHz. The current 10 kHz→2 kHz sine chirp (`BazzismRebuild.py:383`) sounds like a sci-fi laser, not a kick attack.
4. **No DC removal, no soft-knee limiter, no anti-DC after polarity invert.** The phase-accumulated sine starts at 0, multiplied by an envelope that starts at 0 — fine, but the moment you add a second voice or an envelope with nonzero attack the click-on-note-off and DC bias problems appear.

Everything else (extra harmonics, body↔sub crossfade, harmonic enhancer, transient EQ) is on top of those four.

---

## 1. What makes a modern psytrance kick

A Projektor/Astrix/Captain Hook kick is **short** (typically 1/16th at 140–148 BPM ≈ 100–110 ms total audible body, ~250–400 ms when you count the sub tail to silence), **tuned** (the sub locks to the root or the fifth of the bassline), **fishtail-shaped** (loud snap → brief dip → fat boom), and **heavily layered** so each octave does one job.

Decompose it into four perceptual layers. The DSP engine should respect this decomposition even if some layers share an oscillator internally.

### 1.1 Click / Top layer

- **Band:** 1.5 kHz – 8 kHz, sometimes content up to 12 kHz for "air". HPF at ≥1 kHz is non-negotiable.
- **Source material:** snare/rim transient, wood click ("knack"), filtered white noise burst, or a very short FM zap. **Not a sine chirp** — a sine chirp has zero noise content and reads as "Wii Fit menu sound", not "kick attack".
- **Envelope:** attack ~0 ms (sample-accurate), no hold, decay 3–8 ms, exponential. Total audible duration **3–10 ms**.
- **Role:** gives the kick its "definition" through a mix, especially on club PA where everything below 60 Hz is power-amp territory and the click is what the ears actually lock onto for rhythm. Cuts through layers of bass.
- **Reference numbers from the analysis tutorials:** psy clicks are typically -6 to -3 dB relative to the body peak in the click band, then EQ-tilted so most energy sits at 2–4 kHz.

### 1.2 Body / Punch layer

- **Band:** 80 Hz – 250 Hz fundamental sweep; harmonic content extends to ~1.5 kHz via saturation.
- **Source:** sine (or sine + small triangle/parabolic blend for harmonic richness) with a fast pitch sweep from ~300–500 Hz down to ~80–120 Hz over **5–20 ms**, then holding briefly before handing off to the sub.
- **Envelope:** attack 0–2 ms, hold 5–15 ms, decay 30–80 ms. Total audible duration **40–100 ms**.
- **Role:** the "thump" you feel in the chest. This is where most of the saturation and harmonic enhancement happens — odd-harmonic injection here is what makes a kick read as "aggressive" vs "clean".
- **Critical:** the body's pitch envelope must land cleanly into the sub's tuned fundamental. A 5 Hz mismatch at the handoff sounds detuned and amateur.

### 1.3 Sub / Tail layer

- **Band:** 40 Hz – 90 Hz, with the fundamental nailed to a musical note (see §2).
- **Source:** pure sine, optionally with a tiny amount of 2nd harmonic for translation on small speakers.
- **Envelope:** attack 0 ms (phase-locked to the body's end), no hold, decay 80–300 ms exponential. For psytrance specifically the sub tail is **deliberately short** (80–150 ms decay) so it dies before the next 1/16th — otherwise the rolling bassline that sits between kicks turns to mud.
- **Role:** the tuned low-end weight. Lives or dies on **phase coherence** with the body and with the bassline.
- **Critical:** must be perfectly phase-aligned with the body. A 5–10 sample (≈0.1–0.2 ms) misalignment at 44.1 kHz between body and sub at the crossover frequency can cause 3–6 dB of cancellation in the 80–120 Hz region. This is the #1 cause of "my kick sounds thin in the mix".

### 1.4 Room / Space layer (optional but characteristic)

- **Band:** broadband, low-mids emphasised 200–600 Hz.
- **Source:** very short bright reverb tail or a sampled room impulse, gated.
- **Envelope:** triggered post-body, decay 30–100 ms.
- **Role:** glue. Astrix kicks have it, Outsiders kicks have it tuned tighter. This is the difference between a sterile synth kick and a "produced" one.
- **For v1.0:** make this a single "Space" knob (dry/wet on a built-in micro-reverb) — full IR loading is v2.

#### Layer summary table

| Layer | Band (Hz) | Attack | Hold | Decay | Total | Source |
|---|---|---|---|---|---|---|
| Click | 1.5k – 8k+ | 0 ms | 0 ms | 3–8 ms exp | 3–10 ms | noise burst + transient sample |
| Body | 80 – 1500 | 0–2 ms | 5–15 ms | 30–80 ms exp | 40–100 ms | sine + pitch sweep + sat |
| Sub | 40 – 90 | 0 ms | 0 ms | 80–300 ms exp | 80–300 ms | pure sine, tuned |
| Space | 200 – 6k | 0 ms | 0 ms | 30–100 ms | 30–100 ms | gated reverb |

---

## 2. Tuning — why F#1 / G1 / A1

### 2.1 The frequencies

| Note | Hz | Period (ms) | Samples/cycle @ 44.1k |
|---|---|---|---|
| E1  | 41.20 | 24.27 | 1070 |
| F1  | 43.65 | 22.91 | 1010 |
| **F#1** | **46.25** | **21.62** | **954** |
| **G1**  | **49.00** | **20.41** | **900** |
| G#1 | 51.91 | 19.27 | 850 |
| **A1**  | **55.00** | **18.18** | **802** |
| A#1 | 58.27 | 17.16 | 757 |
| B1  | 61.74 | 16.20 | 714 |
| C2  | 65.41 | 15.29 | 674 |

(Values match the Python prototype's `NOTES` dict at `BazzismRebuild.py:17-22`.)

### 2.2 Why those three specifically

**Psytrance bass lines live in F#/G/A minor more than any other keys.** Three reasons that compound:

1. **Sub-bass loudspeaker physics.** Below ~40 Hz most club PA systems are running out of usable headroom and most home/headphone setups produce mostly the second harmonic. 46–55 Hz is the sweet spot where a tuned sub plays loud on big rigs **and** translates as a real pitch (not just rumble) on consumer gear. E1 (41 Hz) is too low for most rooms; B1 (62 Hz) starts to feel "boxy" rather than chest-thumping.
2. **Synth-bass register convention.** The classic psy rolling triplet (Hoover, Reese, Pendulum-FM, Bazz) sits an octave or two above the kick. With the kick on F#1 (46 Hz), the bass plays F#2/F#3 (92/185 Hz) where most preset bass patches sound thick without competing with the kick fundamental.
3. **Historical inertia.** Astrix, Infected Mushroom, Vini Vici routinely release in F#m and Gm; new producers reach for those keys because their sample libraries are already tuned there. F#1 is *the* default. G1 is the safe alternative. A1 is for harder/peak-time stuff because the slightly higher fundamental cuts more aggressively.

### 2.3 Implementation rules

- **Snap end-frequency to a musical note**, like the prototype's `note_cb` (`BazzismRebuild.py:138-141, 222-226`) — keep this UX. The user should never have to think in Hz for tuning.
- **Show note + Hz simultaneously** in the UI (KICK 2 does this). It teaches the user the relationship.
- **Tune the sub layer, not the body's start frequency.** The body's start (200–500 Hz) is a perceptual transient, not a pitch the brain locks onto. The sub's sustained fundamental is what matters.
- **Allow ±50 cents detune** on the sub for "thickness" tricks (two slightly detuned subs sum to a fatter mono sound — but only if their phases lock at note-on).

---

## 3. What the Python prototype does well

For a ~500-line single-file proof of concept, `BazzismRebuild.py` gets the bones right. Specifically:

### 3.1 Two-segment pitch envelope with adjustable curve

Lines `310-322`. Three frequencies (`start`, `mid`, `end`) connected by two time segments (`sweep_time_1`, `sweep_time_2`) with a shared exponent `pitch_curve`. The exponent uses `(1 - x) ** p_curve` so values >1 give a fast-fall-then-tail-off (correct intuition: pitch falls fast at the start, then settles).

This is **better** than Bazzism 2's single-exponent sweep because it lets the user place the "knee" of the sweep where the body transitions to sub. Keep this in the C++ engine but:
- Consider per-segment curve exponents (separate `curve_1`, `curve_2`) — currently both segments share `pitch_curve` which couples decisions you'd want decoupled.
- Add a "linear / exponential / power" enum or, better, expose the curve as a Bezier handle like KICK 2 does. Bezier > exponent for clarity-to-the-user, but exponent is fine for v1.

### 3.2 Five-segment AHDSR amplitude envelope (Attack–Hold–Decay1–Sustain–Decay2)

Lines `328-360`. Two-decay structure is the right call for percussion: D1 is the "punch decay" (loud snap → sustained level), D2 is the "tail decay" (sustained level → silence). Curve exponent is shared but applied as `x ** (1/curve)` on attack and `(1 - x) ** curve` on decays which is the standard psychoacoustic shape (perceptually-linear attack, fast-then-slow decay).

Keep this exact structure. The two-decay AHDSR is more useful for percussion than the standard ADSR or DAHDSR.

### 3.3 Sinusoidal scoop / dip

Lines `362-372`. `dip_shape = 1 - sin(πφ) * depth`, where φ is 0→1 across the scoop window. This produces a smooth, hump-shaped attenuation that's the right curve for the "fishtail" — it doesn't introduce zero-crossings or clicks, and it has continuous first derivative so it doesn't show up as a transient itself.

Real Projektor/Outsiders kicks **do** have this dip; it's audible on careful spectral analysis around 15–40 ms after the transient. The Python implementation is exactly right. **Don't** replace this with a triangular or rectangular gate — those would click.

One refinement: expose `scoop_curve` (a power on the `sin(πφ)` shape) so the user can pull the dip earlier or later asymmetrically.

### 3.4 Tail-weighted drive ramp

Lines `392-399`. `drive_array = base_drive + (drive_ramp**2 * tail_drive * 5)` where `drive_ramp = linspace(0,1,N)`. This means the tanh saturation is gentle at the start (preserves transient punch) and gets aggressive into the tail (adds harmonics to the body→sub region, fattens the boom).

This is **clever and correct.** Real psy producers do this with parallel saturation + envelope-followed clipper plugins. Keep the algorithm; just oversample the tanh stage (see §4.1).

The normalisation `np.tanh(audio * drive_array) / np.tanh(np.max(drive_array))` (line 399) keeps unity gain at the loudest drive value. Good.

### 3.5 Tail de-click fade

Lines `408-410`. 100-sample linear fade at the buffer end prevents the "pop" from truncating a non-zero sample. Fine for offline rendering. In a VST you'll need a real release envelope instead, since the buffer doesn't end at the same time the note does.

### 3.6 Things that are merely fine

- Polarity invert (`BazzismRebuild.py:404-405`) is just `audio = -audio`. Useful for layering when the producer wants the kick to push the speaker out instead of in on the first half-cycle. Cheap to implement.
- The `phase = cumsum(freqs) / sample_rate` integration (`line 325`) is the correct way to do a pitch-swept sine. Don't replace this with a phasor approach unless you also handle the discontinuity at note-on.

---

## 4. What the Python prototype is missing for a Projektor-grade kick

This is the meat of the document. Order is roughly "biggest impact first".

### 4.1 Oversampling for the tanh stage (CRITICAL)

The bug: at 44.1 kHz, the Nyquist is 22.05 kHz. A tanh shaper generates harmonics up to **infinity** in principle, but practically falls off around the 7th–11th harmonic depending on drive. With a body whose fundamental is 200–400 Hz during the sweep, the harmonics fit easily. **But** the click chirp at 10 kHz with even modest drive generates harmonics at 30 kHz, 50 kHz, 70 kHz... all of which alias back as 14.1 kHz, 5.9 kHz, 26.0 kHz→ 14.1 kHz again etc. The result is a metallic, fizzy, slightly out-of-tune top end on every kick. This is the #1 reason DIY kick synths sound "digital" while commercial ones don't.

**Fix:** wrap the saturation stage in `juce::dsp::Oversampling`. Specific guidance:

- **Factor:** 4x is the minimum that gets audibly clean tanh. 8x sounds noticeably silkier. 16x is overkill for production but good for an "Ultra" quality switch.
- **Filter type:** FIR equiripple for offline/render, IIR Butterworth for realtime — `juce::dsp::Oversampling` defaults are sane (`FilterType::filterHalfBandFIREquiripple` for FIR, `filterHalfBandPolyphaseIIR` for IIR).
- **Latency reporting:** the FIR adds samples-of-latency that you must report via `setLatencySamples()` for the DAW to compensate. IIR has near-zero latency but smears phase near Nyquist.
- **Scope:** oversample **only the saturation block** (drive→tanh→drive-normalise), not the whole signal chain. Saves CPU. The sine generator and envelopes don't alias because they're band-limited by construction.
- **Default quality:** 4x IIR for realtime preview, 8x FIR for bounce/render. Expose a `Quality` menu (Eco / Normal / High / Ultra).

The Python prototype runs at 44.1 kHz with no oversampling and you can hear the aliasing as a faint shimmer above the kick's natural top end. In a busy psy mix it gets lost, but on solo and on a critical PA system it's audible. Fix it.

### 4.2 Separate body + sub voices with crossfade

The single-oscillator design (`BazzismRebuild.py:324-326`) forces a tradeoff: long pitch sweep gives you a real transient but a slow attack into the sub; short sweep gives you a tight sub but a weak punch. Pro kicks solve this by **running two voices in parallel**:

- **Body voice:** pitch envelope sweeping 400 Hz → 90 Hz over ~20 ms, AHDSR with fast decay (50–80 ms total).
- **Sub voice:** fixed-pitch sine at the tuned note (e.g. 46.25 Hz), AHDSR with attack 0 ms and a slow decay (150–300 ms).
- **Crossfade:** at the body's end frequency, both voices play simultaneously; an automation curve fades the body out as the sub fades in. Or simpler: both run unconditionally and the user mixes their levels.

Phase coherence is the trap (see §6.3). The cleanest implementation:
- Both voices reset phase to 0 at note-on.
- The body's end frequency = the sub's pitch (i.e. when the body's pitch envelope arrives at its `end_freq`, it's the same Hz the sub is playing).
- Then the sub's phase at the moment-of-crossover equals the body's phase at that moment, modulo the integral of the body's pitch sweep.

In practice you'll get a few samples of phase difference. Either (a) lock them by computing the body's final phase analytically and offsetting the sub, or (b) shrug and let the producer adjust a "sub phase offset" knob (0–360°). Option (b) is what KICK 2 effectively does and it works fine.

### 4.3 Real click layer (not a sine chirp)

The current click (`BazzismRebuild.py:376-390`) is a 5 ms sine sweep from 10 kHz to 2 kHz with `**2` fade. It sounds like a sine sweep, because it is one. Real psytrance clicks are one of:

1. **Filtered noise burst:** white noise → bandpass at 2–5 kHz, Q ~2, exponential decay 3–8 ms. Cheapest to implement, sounds aggressive.
2. **Wood/snare sample:** the "knack". A real snare top/rim sample, HPF at 1–2 kHz, gated to 5–10 ms. This is what Projektor uses on his current sound design.
3. **FM zap:** carrier ~3 kHz, modulator ~5 kHz, fast modulation-index decay. Cleaner than noise, more controlled. Captain Hook's signature.
4. **All three layered:** what KICK 2 does — three sample slots plus the sub generator.

**Minimum viable click for v1.0:** filtered noise burst (option 1) with HPF cutoff + decay + level + tone (LP cutoff) knobs. The sine chirp can stay as a separate "Tonal click" knob if you want to keep the prototype's behaviour for compatibility.

Critical detail: **always HPF the click at ≥800 Hz** before mixing into the body. Otherwise the click's low-frequency tail (which exists for any short noise burst due to the spectral spread of a short window) will phase-cancel with the body's transient at low frequencies.

### 4.4 Anti-DC after polarity invert (and in general)

The polarity invert at `BazzismRebuild.py:404-405` is just `audio = -audio`. That itself doesn't introduce DC. **But** the combination of `audio * amp_env` (line 374) where `audio = sin(phase)` and `amp_env` is asymmetric in time can produce a non-zero mean if the envelope's centre-of-mass doesn't sit on a zero-crossing of the sine. With pitch sweeps that's almost always true: the integral of `sin(2π · ∫f(t) dt)` weighted by an arbitrary envelope is generically non-zero.

Add a **DC blocker** after the saturation stage. Standard first-order high-pass at 5–10 Hz:

```
y[n] = x[n] - x[n-1] + R * y[n-1],  R ≈ 0.995 at 44.1 kHz
```

(Or use `juce::dsp::IIR::Filter` with a first-order high-pass biquad.) Cheap, always on, no user-facing parameter. Without this, layered kicks in a mix accumulate DC offset and the bus limiter starts behaving weirdly.

### 4.5 Soft-knee output limiter

The Python prototype normalises tanh output by `tanh(max_drive)` (line 399) which gives unity peak gain on the saturated section but **does not** prevent the un-saturated section from clipping if the user pushes envelope levels. Also: the abrupt 100-sample tail fade (lines 408-410) is a hack.

Add a true brick-wall soft-knee limiter at the output, e.g. a feed-forward limiter with:
- Threshold: -0.3 dBFS (just below 0)
- Knee: 3 dB soft
- Attack: 0.1 ms (must catch the transient)
- Release: 50–100 ms
- Lookahead: ~5 samples (≈0.1 ms) to catch the click

JUCE's `dsp::Limiter` works but is fairly basic. Better: hand-roll a feed-forward limiter with peak detector + soft-knee, since you have offline knowledge of the entire kick waveform anyway (it's a one-shot per note — you could even render-then-limit in one pass and cache).

### 4.6 Harmonic enhancer / odd-harmonic injection (the "knack")

Modern psy kicks have a **pitched click in the 800 Hz – 2 kHz band** that sits between the bright noise click and the body. It's what gives Projektor's kicks their "wood plank" character. Two ways to generate it:

1. **Parallel waveshaper on the body, HPF the result.** Take the body voice, run it through a hard-knee soft-clipper or asymmetric distortion (the asymmetry gives you both odd AND even harmonics — tanh alone is purely odd-symmetric, which sounds smooth but lacks bite), HPF the output at 600–1500 Hz, mix back in at -6 to -12 dB.
2. **Resonant filter at 1.2 kHz with very short ping decay.** Mathematically equivalent to a sine burst at the resonant frequency triggered by the transient. KICK 2's "click sample 2" slot is often used for this.

For v1.0, **option 1 is simpler and more "Bazzism-like"**. Implement it as a second tanh stage with its own drive amount, fed by the body voice, HPF'd, then summed into the output. Expose as a "Knack" or "Bite" knob.

### 4.7 Transient EQ / HPF on the click

Independent of §4.3, even if you keep the existing sine-chirp click, **it needs a HPF**. The 2 kHz tail end of the chirp has audible low-spillover from the windowed sine (Gibbs/spectral-leakage). A simple 2nd-order Butterworth HPF at 800 Hz on the click channel cleans this up without affecting the perceived brightness.

While you're at it, expose:
- **Click HPF** (200 Hz – 4 kHz, log) — defaults to 800 Hz
- **Click LPF / tone** (2 kHz – 16 kHz, log) — defaults to 12 kHz (preserves brightness)
- **Click colour** (a single-knob tilt EQ that adds/removes high-frequency emphasis)

### 4.8 Soft-clip vs tanh vs waveshaper choice

The Python prototype uses `np.tanh` exclusively. Tanh is odd-symmetric → only odd harmonics → smooth, slightly warm distortion. That's good for the body's saturated tail but it's a **single colour**.

Expose a saturation type enum:

| Mode | Function | Character | Harmonics |
|---|---|---|---|
| **Tanh** | `tanh(x)` | smooth, warm | odd only |
| **Soft clip** | `x - x³/3` (clipped at ±1) | tight, punchy | odd only |
| **Hard clip** | `clamp(x, -1, 1)` | aggressive, buzzy | odd, lots of high harmonics |
| **Tube** | `x / (1 + |x|)` or asymmetric: `tanh(a*x) + tanh(b*x²)` | warm with even harmonics | both |
| **Foldback** | `sin(πx/2)` cycled | metallic, weird | both, complex |

For psytrance, **Tanh + Soft Clip cover 80% of cases**. Tube is the "warmer / vintage" option. Foldback is for the experimental darkpsy / hi-tek crowd. Hard clip is mainly for hardstyle but useful at low drive amounts to add bite.

All must be oversampled (see §4.1). Hard clip in particular aliases catastrophically without oversampling.

### 4.9 Tail body→sub crossfade curve

Currently the body's pitch envelope just hits its `end_freq` and stays there (`BazzismRebuild.py:320-322`). If you implement separate body and sub voices (§4.2), you need a crossfade curve. Options:

- **Linear:** equal-power loss at midpoint. Audible "scoop" at the crossover.
- **Equal-power (sin/cos):** the right choice for uncorrelated signals.
- **Phase-coherent (linear sum):** the right choice for phase-locked signals (which the body and sub will be if you set up §4.2 properly).

For phase-locked body+sub, **linear sum with both at full level** is fine — they don't cancel because they're in phase. Just gate the body's contribution to zero after its decay completes.

### 4.10 Click pop on note-off

Not currently a problem because the Python prototype renders a complete buffer per kick. In the VST, if the user releases the note while the envelope is still high (e.g. plays a very short note), you'll truncate the envelope and click. Two mitigations:

- **Force a minimum release of 5–10 ms** that the user can't override. The envelope's final decay segment must complete at this minimum rate even if the gate goes off mid-decay.
- **Snap the cut to the nearest zero-crossing** if minimum-release is exceeded. Cheap and reliable.

### 4.11 Per-voice voicing (mono vs poly)

A kick synth doesn't need polyphony but it does need a **voice-stealing** policy when the player triggers two notes within the decay tail of the first. Options:

- **Cut-and-retrigger** (most natural for kicks): a new note immediately stops the previous voice with a 1 ms fadeout, then starts the new note. This is the default behaviour you want.
- **Mono legato** (don't retrigger if the previous note is still held): irrelevant for a kick.

For v1.0: just implement cut-and-retrigger. Two-voice polyphony is wasted on a kick — if the user wants a flam they should use two MIDI notes 5 ms apart.

### 4.12 Things I am explicitly NOT recommending for v1.0

- **Wavetable oscillators** for the body. The pitch-swept sine is right; a wavetable would just add aliasing and complexity. Save wavetables for v2's harmonic editor.
- **Convolution reverb / IR loading.** Use a built-in micro-reverb instead. IR loading is a v2 feature.
- **Spectral processing** (FFT-based EQ, spectral pitch). Way too much CPU and complexity for the marginal gain.
- **Modulation matrix.** Maybe in v2. v1 ships with fixed routing.
- **MIDI velocity mapping beyond volume**. The user can pre-shape velocity in their DAW.

---

## 5. Parameter list for the C++ engine

Group into 7 sections. All parameters are AudioProcessorParameter-derived, with descriptive names, ranges, defaults, units, and curves. Default values reflect the "Projektor Punch" preset from the Python prototype (`BazzismRebuild.py:50-57`) as a starting point.

Curve key: **lin** = linear, **log** = logarithmic (good for frequency, time), **exp** = exponential (good for gain/level), **skew** = JUCE NormalisableRange with skew factor (e.g. 0.5 to bias toward low end).

### 5.1 Pitch (6 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `pitch.startFreq` | 100 – 15000 | 12000 | Hz | log | Top of the chirp. Same as Python `start_freq`. |
| `pitch.midFreq` | 50 – 1500 | 350 | Hz | log | Knee of the two-segment sweep. |
| `pitch.endFreq` | 20 – 120 | 46.25 | Hz | log | Sub fundamental. Snap-to-note via §5.6. |
| `pitch.time1` | 0.1 – 50 | 8.0 | ms | log | Start→Mid sweep duration. |
| `pitch.time2` | 1 – 250 | 80.0 | ms | log | Mid→End sweep duration. |
| `pitch.curve` | 0.1 – 10 | 4.0 | - | skew(0.5) | Sweep exponent. >1 = fast-fall. |

Consider splitting `pitch.curve` into `curve1` and `curve2` (v1.1 feature).

### 5.2 Amplitude (6 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `amp.attack` | 0 – 30 | 0.5 | ms | lin | Often 0. Non-zero softens the click. |
| `amp.hold` | 0 – 50 | 15.0 | ms | lin | Plateau at peak. |
| `amp.decay1` | 0 – 150 | 60.0 | ms | log | Decay to sustain level. |
| `amp.sustain` | 0 – 100 | 30.0 | % | lin | Sustain level as % of peak. |
| `amp.decay2` | 0 – 1000 | 250.0 | ms | log | Sustain → silence. Extended over Python (700 ms max) for room kicks. |
| `amp.curve` | 0.1 – 10 | 3.5 | - | skew(0.5) | Shared envelope exponent. |

### 5.3 Scoop (3 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `scoop.start` | 0 – 50 | 15.0 | ms | lin | Offset from note-on. |
| `scoop.length` | 1 – 100 | 40.0 | ms | log | Width of the dip. |
| `scoop.depth` | 0 – 100 | 10.0 | % | lin | 0 = no scoop, 100 = full kill. |

### 5.4 Click (5 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `click.level` | -inf – +6 | -16 | dB | exp | Volume of the click layer relative to body. |
| `click.type` | enum | Noise | - | - | {Noise, Tonal, Wood, Off}. Tonal = the Python sine chirp. |
| `click.hpf` | 200 – 4000 | 800 | Hz | log | High-pass cutoff. Always engaged. |
| `click.tone` | 2000 – 16000 | 12000 | Hz | log | LPF / brightness. |
| `click.decay` | 1 – 30 | 6.0 | ms | log | Click envelope decay. |

### 5.5 Drive (5 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `drive.base` | 1 – 10 | 2.5 | - | skew(0.5) | Base saturation amount. Same as Python `drive`. |
| `drive.tail` | 0 – 10 | 1.0 | - | skew(0.5) | Tail-weighted drive. Same as Python `tail_drive`. |
| `drive.type` | enum | Tanh | - | - | {Tanh, SoftClip, HardClip, Tube, Foldback}. |
| `drive.knack` | 0 – 100 | 0 | % | lin | Parallel mid-band saturation (§4.6). |
| `drive.oversample` | enum | 4x | - | - | {Off, 2x, 4x, 8x, 16x}. Off for CPU debug only. |

### 5.6 Tone (4 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `tone.snapNote` | enum | Off | - | - | {Off, C1, C#1, ... C2}. Snaps `pitch.endFreq` to musical note. |
| `tone.subLevel` | -inf – +6 | 0 | dB | exp | Mix of the dedicated sub voice (§4.2). 0 dB = full. |
| `tone.bodyLevel` | -inf – +6 | 0 | dB | exp | Mix of the body voice. |
| `tone.space` | 0 – 100 | 0 | % | lin | Built-in room reverb send (§1.4). |

### 5.7 Master (3 params)

| Param | Range | Default | Unit | Curve | Notes |
|---|---|---|---|---|---|
| `master.gain` | -inf – +12 | 0 | dB | exp | Output trim. |
| `master.invertPhase` | bool | false | - | - | Polarity flip. Same as Python `invert_phase`. |
| `master.limiterCeiling` | -3 – 0 | -0.3 | dBFS | lin | Soft-knee limiter ceiling (§4.5). |

### 5.8 Param count and DAW automation

Total: ~32 parameters. JUCE handles this trivially. Keep parameter IDs stable across versions (use `juce::AudioProcessorValueTreeState`). Persist the `Quality` setting (oversampling) as a non-automatable property — it changes latency and shouldn't change mid-render.

---

## 6. Numerical / algorithmic pitfalls

### 6.1 DC offset from envelope × sine

The product `sin(2π·phase) * amp_env(t)` has nonzero mean whenever the envelope is asymmetric in time relative to the sine's phase. With a pitch sweep this is almost always true.

**Symptom:** the rendered waveform sits visibly above (or below) the zero line. In isolation inaudible, but accumulated across many layers in a mix, the bus limiter clamps too early on one side of the waveform and you lose 1–3 dB of headroom.

**Fix:** the DC blocker from §4.4. Always-on, post-saturation, pre-limiter. First-order HPF at ~8 Hz:
```
R = 1 - (2 * π * 8 / sampleRate)  // ≈ 0.9988 @ 44.1 kHz
y[n] = x[n] - x[n-1] + R * y[n-1]
```

### 6.2 Phase initialisation at note-on

The Python prototype's `phase = cumsum(freqs) / sample_rate` (line 325) starts the integration from zero, so `sin(2π·0) = 0` and the waveform starts cleanly at the zero line. **In a VST you must replicate this.** Specifically:

- On every note-on, reset the phase accumulator to 0.
- Reset all envelope state to 0.
- Reset all filter states (DC blocker, click HPF, click LPF, oversampling filter, limiter) to 0.

Failure to reset the filter states is the classic source of "the first kick after a stop sounds different from the rest". Symptom: the first hit has a tiny click or a slightly different transient. The user blames their MIDI controller. It's the IIR filter ringdown from the previous render.

**Implementation:** every DSP block needs a `reset()` method that zeros state. JUCE's `dsp::ProcessorBase` enforces this; use the framework.

### 6.3 Phase coherence between body and sub layers

If you implement separate body + sub voices (§4.2), the body's final-frequency phase at the crossover point won't equal the sub's phase at that point unless you do something about it.

The math: at time `t_cross` (end of body's pitch sweep), the body's phase is `θ_body = 2π · ∫₀^t_cross f_body(τ) dτ`. The sub, started at note-on with frequency `f_sub`, has phase `θ_sub = 2π · f_sub · t_cross`. Generally `θ_body ≠ θ_sub`.

**Three options:**

1. **Start the sub later.** Delay sub's note-on to `t_cross` and start it with phase 0. This gives a discontinuity in the sub waveform at note-on (sub starts at 0 amplitude but the body is non-zero). The amp envelope will mask it if attack > 0.5 ms.
2. **Pre-compute the body's final phase, then offset the sub.** At voice initialisation, integrate the body's pitch envelope analytically (you have it; it's piecewise power functions) to compute `θ_body(t_cross)`. Start the sub with phase = `θ_body(t_cross)`. Now both voices are phase-aligned at the crossover.
3. **Let the user adjust a phase offset knob.** "Sub Phase: 0–360°". KICK 2 does this. Honest about the problem, gives the user control, no math.

For v1.0 I recommend **option 2 (compute) with option 3 (knob) as a trim**, default knob = 0°. Best of both worlds.

### 6.4 Click pop on note-off

Already covered in §4.10. Minimum 5–10 ms forced release + zero-crossing snap.

### 6.5 Aliasing from steep pitch envelopes

If `pitch.time1` is very short (say 0.1 ms) and `pitch.startFreq` is very high (15 kHz), the pitch sweep itself can have a derivative high enough that the resulting sine looks more like a chirp pulse than a tone, and the spectral content extends well above Nyquist.

For typical psy kick parameters (start 8–15 kHz, time1 ≥1 ms) this is not a problem because the sine generator is band-limited by definition (it's just a sine, never above its instantaneous frequency). But: if you later add wavetables or square/saw waves to the body oscillator, you'll need BLEP/polyBLEP or oversampling on the oscillator itself.

For v1.0 sticking to sines: no problem.

### 6.6 Floating-point precision for long phase accumulators

`cumsum(freqs) / sample_rate` over a 700 ms buffer at 44.1 kHz = ~31,000 samples. With `float` precision the phase accumulator can drift by ~1e-3 radians over that period — inaudible. With heavy oversampling (16x → 500k samples) it can drift further. Use `double` for the phase accumulator internally, cast to `float` for the `sin()` call. Trivial cost, removes the issue forever.

### 6.7 The `np.tanh` denominator near zero

`BazzismRebuild.py:399` divides by `np.tanh(np.max(drive_array))`. If the user sets `drive.base = 1` and `drive.tail = 0`, `max(drive_array) = 1` and `tanh(1) ≈ 0.762`. Fine. If they could set drive to 0 (they can't in the current UI — minimum is 1), the denominator would be `tanh(0) = 0` → divide by zero.

**In the C++ port, clamp `drive ≥ 1.0` and `tail_drive ≥ 0.0` at the parameter range level**, not at the DSP level. Defensive: still add a `max(denom, 1e-6)` guard in the divide.

### 6.8 Note-on inside the previous kick's tail

If the user triggers a new note while the previous decay-2 is still ringing out, what happens? Three options:

- **Stop-and-restart (recommended):** apply a 1 ms cosine fadeout to the previous voice, then start the new one. Implements voice-stealing cleanly.
- **Sum-into:** let the previous tail continue and add the new kick on top. Sounds like a flam, can clip, generally not what the user wants.
- **Ignore:** drop the new note if previous is still active. Never do this.

Stop-and-restart needs voice state. Implement a `VoiceManager` that tracks the active voice, even if there's only ever one.

---

## 7. Concrete implementation order for the architecture agent

If I were writing the C++ I'd land features in this order:

1. **Single-voice body engine** matching Python prototype 1:1, no oversampling yet. Verify parity by rendering the same params and bit-comparing within rounding error.
2. **Add oversampling** around the saturation block (§4.1). Compare rendered output A/B on a -60 dB noise floor — aliasing components should drop by ≥20 dB.
3. **Add DC blocker** (§4.4) and limiter (§4.5). Verify no DC drift on long-tail kicks.
4. **Add real noise-burst click** (§4.3). Replace the sine chirp behind the scenes; expose `click.type` enum with "Tonal" as the legacy mode.
5. **Add separate sub voice** (§4.2) with phase-coherence handling (§6.3).
6. **Add Knack / mid-band saturation** (§4.6).
7. **Polish:** Space reverb (§1.4), additional saturation types (§4.8), preset bank matching the Python `BUILTIN_PRESETS`.
8. **UI** — out of scope for this doc; KICK 2's three-pane layout (pitch / amp / click) is a proven model.

Stages 1–3 alone produce a kick that is **already** Bazzism-grade. Stages 4–5 push it toward Projektor-grade. Stages 6–7 are the polish that distinguishes "competent" from "clinical".

---

## 8. References (URLs actually consulted)

### Highly useful

- [Sonic Academy KICK 2 — Audiotent Ultimate Guide](https://www.audiotent.com/blogs/production-tips/production-tipsultimate-guide-sonic-academy-kick-2) — gold standard reference for KICK 2 architecture (pitch/amp/click separation, Bezier envelopes, sub harmonic editor).
- [Sonic Academy KICK 2 review — MusicRadar](https://www.musicradar.com/reviews/tech/sonic-academy-kick-2-640392) — confirms 3 sample-slot click + sub generator architecture.
- [BazzISM 2 Manual on Scribd](https://www.scribd.com/document/295175355/BazzISM2-5-0-Manual) — original Bazzism parameter list, what we're reimagining.
- [BazzISM product page on KVR](https://www.kvraudio.com/product/bazzism-by-intelligent-sounds-and-music-ism) — feature summary.
- [How to fit kick and bass together — dsokolovskiy.com](https://dsokolovskiy.com/blog/all/how-to-fit-kick-and-bass-together/) — psytrance-specific kick/bass tuning relationships, frequency-region carving.
- [3 ways to make a kick drum — dsokolovskiy.com](https://dsokolovskiy.com/blog/all/kick-synthesis/) — synthesis approaches including the sine-pitch-sweep method the Python prototype implements.
- [Psytrance Kick Processing — eclipmusic.com](https://www.eclipmusic.com/post/psytrance-kick-processing-key-techniques-and-insights) — fishtail shape, 1/8th note length, transient + body limiter trick.
- [Psy Trance Learning Mega-thread — Loopy Pro Forum](https://forum.loopypro.com/discussion/57321/the-psy-trance-learning-potentially-mega-thread) — fishtail explanation, sub-genre kick differences (Goa/Progressive/Darkpsy/Full-on).
- [How to make modern psytrance kick & bass — YouTube](https://www.youtube.com/watch?v=cVkgV2bm0fw) — 2024-era production walkthrough.

### DSP technique references

- [JUCE Oversampling class docs](https://docs.juce.com/master/classjuce_1_1dsp_1_1Oversampling.html) — class reference for the C++ oversampling implementation.
- [How to implement oversampling in JUCE — daudio.dev](https://daudio.dev/explore/HowToImplementOversamplingInJuce) — practical guide.
- [KVR: Tanh approximations](https://www.kvraudio.com/forum/viewtopic.php?t=262823) and [Tanh aliasing](https://www.kvraudio.com/forum/viewtopic.php?t=388650&start=15) — confirms 4x minimum / 8x recommended oversampling for tanh.
- [KVR: Even and odd harmonic distortion](https://www.kvraudio.com/forum/viewtopic.php?t=123354) — tanh is purely odd-symmetric, hence purely odd harmonics. Asymmetric distortion needed for even harmonics.
- [Black Ghost Audio: Phase Alignment 101](https://www.blackghostaudio.com/blog/how-to-create-punchy-songs-using-phase-alignment) — kick/sub phase alignment, the 0.1–0.2 ms sample-nudge trick.
- [Cycling74 forum: Kick drum click problem in MaxMSP](https://cycling74.com/forums/kick-drum-synthesis-click-problem) — phase reset at note-on solves the click problem.

### Tuning / mixing references

- [Loopmasters: Tuning a Kickdrum to the Bassline](https://www.loopmasters.com/articles/2382-Tuning-A-Kickdrum-To-The-Bassline) — practical kick-bass tuning rules.
- [ModeAudio: Finding the Kick Drum Fundamental](https://modeaudio.com/magazine/eq-trick-finding-the-kick-drum-fundamental) — 50–80 Hz fundamental range; how to find it with EQ.
- [Soundbridge: Mixing Kick and Bass](https://www.soundbridge.io/mixing-kick-and-bass) — kick 2–3 dB above bassline in psy.

### Layering / click design references

- [Audiotent: Parallel processing kick drums](https://www.audiotent.com/blogs/production-tips/kick-drumhow-to-use-parallel-processing-beef-kick-drums) — 3-aux (sub/punch/click) parallel processing structure that mirrors the §1 layer decomposition.
- [Hobotech: Kick drum high-pass workshop](https://hobo-tech.com/technologies/livetips/kick-drum-workshop-high-pass-filter/) — HPF on click layers at 100–300 Hz minimum, 1–2 kHz for parallel click bus.
- [Ali Jamieson: Kick drum layering with Eurorack](https://alijamieson.co.uk/2015/11/19/kick-drum-layering-eurorack-modular/) — wood click / "knack" layer concept from modular synth practice.

### Less useful but consulted

- [Splice: KICK by Nicky Romero](https://splice.com/plugins/523-kick-by-nicky-romero-vst-au-by-sonic-academy) — confirms KICK 2 architecture lineage.
- [Steemit: Psytrance kick with Kick 2](https://steemit.com/music/@mume/music-production-software-psytrance-kick-drum-with-kick-2) — basic walkthrough, mostly screenshots.
- [Audija KickDrum page](https://audija.com/kickdrum) — competitor product, similar feature set to what we're building. Not "Bazzism" despite the user's prompt phrasing; separate product by Audija.

---

## 9. Open questions for the architecture agent

1. **Realtime vs offline rendering.** Will the plugin render the entire kick per note-on (offline, into a buffer) and then play it back, or will it run sample-by-sample in the processBlock? Offline is simpler, allows lookahead limiter and analytical phase coherence; realtime is more "VST-native" and supports modulation but requires careful state management. **Recommendation: offline render-per-note**, cached, with a flag to invalidate on parameter change.
2. **Latency strategy.** With 4x FIR oversampling + 5-sample limiter lookahead the plugin will report ~10 samples of latency at 44.1 kHz. Acceptable for a kick (which is typically heavily quantised anyway), but the host needs to know. **Report it honestly via `setLatencySamples()`.**
3. **Preset format.** The Python prototype uses a flat JSON dict (`BazzismRebuild.py:233-243, 244-253`). Keep the same format for compatibility — let users load Python-prototype presets directly. Add new fields with sensible defaults so old presets keep working.
4. **MIDI mapping.** Default mapping: any note triggers the kick at its own pitch (transposing `pitch.endFreq` accordingly), velocity scales `master.gain`. Optional "fixed pitch" mode where MIDI note is ignored and `pitch.endFreq` is set only by the parameter.

End of document.
