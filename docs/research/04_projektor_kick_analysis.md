# Projektor Kick Analysis

> Parameter-level reference for the KickAss VST3.
> Source notes are inline; full URLs in §8.
> All numbers are concrete targets — the C++ port can compile these directly into factory presets and DSP defaults.

---

## 1. Who Projektor are and their sonic signature

Projektor is a psytrance project rooted in the modern EU/Israeli forest-progressive lineage — themed, sound-design-heavy tracks released across labels like Iono Music, Blue Hour Sounds, Iboga (the wider Israeli/Italian Iono–Iboga–TIP axis) and adjacent forest imprints. They sit between Outsiders' main-room punch and the forest 145–148 BPM stomp: not as bright and snappy as Astrix/Vini Vici main-room, not as muddy as classic darkpsy. Their kick is the load-bearing pillar of that vibe — it has to be subby enough to carry a "twilight" set on a Funktion-One, but punchy enough to slot under a heavy 16th-note bassline at 146 BPM without smearing.

What makes a Projektor-style kick identifiable:

- **Length is medium-long for the genre**: ~280–350 ms total. Longer than a typical 140 BPM techno kick, shorter than a darkpsy kick. The tail bleeds about halfway into the 16th-note grid at 146 BPM (one 16th ≈ 103 ms; the kick decays through ~3 sixteenths).
- **Punch is mid-forward, not click-forward**: the "thump" lives around **300–450 Hz** during the sweep, not at 5–10 kHz. The click is present but never spitty.
- **Sub is deep and sits**: fundamental at **F#1 (46.25 Hz)** or **G1 (49 Hz)** most commonly, with a long sub tail driven by tail-weighted saturation so the harmonics keep generating energy in the 90–150 Hz region even as the fundamental decays.
- **Scooped midrange**: a small but real volume dip around 200–300 Hz in the body (the "fish-tail" scoop, but gentler than Astrix-style main-room). Cleans space for the rolling bass's first harmonic.
- **Dry room**: essentially no audible reverb on the kick itself. Any room sound is on the percussion bus, not the kick.

---

## 2. Frequency anatomy

Concrete Hz targets and dB-relative levels for a Projektor-style kick.
Reference level: peak of the kick = 0 dB FS (or whatever the mix bus is at).
"dB-rel" below = peak SPL of that band relative to the kick's loudest band (the body around 80–120 Hz).

| Band              | Range         | Content                                                                 | dB-rel  | Sustain                                          |
|-------------------|---------------|-------------------------------------------------------------------------|---------|--------------------------------------------------|
| **Sub**           | 20–60 Hz      | Fundamental sine (F#1 = 46.25 Hz, G1 = 49 Hz, A1 = 55 Hz)               | −3 to −1 dB | Longest — sustains 200–300 ms (the kick's "body" once pitch settles) |
| **Low body**      | 60–150 Hz     | 2nd–3rd harmonic of fundamental + sweep residue                         | **0 dB (loudest)** | 80–150 ms, decays into sub                       |
| **Mid body**      | 150–500 Hz    | The "punch" — driven by the sweep landing through this band             | −4 to −8 dB at scoop | 30–80 ms (scooped after that)                    |
| **Upper mid**     | 500 Hz–2 kHz  | "Knock" character; comes from drive/saturation harmonics, not synthesis | −10 to −15 dB | 20–50 ms                                         |
| **High**          | 2–10 kHz      | "Click"/transient; brief sine-sweep chirp                                | −15 to −20 dB | 5–10 ms hard fade                                |
| **Air**           | 10 kHz+       | Mostly absent. Projektor kicks are not "airy"                            | −25 dB+ | Negligible                                       |

Key implementation notes from the references:

- The fundamental at ~40–50 Hz is *felt more than heard*. On near-fields it sounds thin; on a subwoofer it carries the room. KickAss should default-tune to F#1.
- The 150–600 Hz scoop is the genre's "fish-tail" sound — cuts of −6 to −15 dB at 200–300 Hz are typical, but a Projektor kick uses a *moderate* scoop (5–15% in scoop_depth terms, per the Python preset values), not the −20 dB notch some hardpsy producers use.
- Click around 10 kHz is the producer's choice; many Projektor-style kicks have a click around **5 kHz** (chunkier, more "crack") rather than 10 kHz (more "tick"). The Python click sweep from 10 kHz → 2 kHz over 5 ms covers both.

---

## 3. Envelope timing — Projektor kick at 145–150 BPM

At **146 BPM**:
- 1 beat = **410.96 ms**
- 1 sixteenth = **102.7 ms**

A Projektor-style kick at 146 BPM lives in this envelope shape:

| Stage              | Duration   | What it does                                                                  |
|--------------------|------------|-------------------------------------------------------------------------------|
| **Attack**         | 0.5–2 ms   | Near-zero (psytrance kicks need to be on the grid; attack > 5 ms loses punch) |
| **Hold (peak)**    | 10–20 ms   | The "punch window" — pitch is still in the upper-mid range here               |
| **Decay 1**        | 50–80 ms   | Drops from peak to sustain (~30–40% of peak). This is where the punch dies.   |
| **Sustain plateau**| 30–40%     | Steady-state body level. Sub sine is still ringing here.                      |
| **Decay 2 (tail)** | 200–300 ms | The long sub bleed. This is where Projektor differs from main-room.           |
| **Total**          | 280–400 ms | Ends ~3 sixteenths into the next beat                                         |

**Interaction with the rolling bass at 146 BPM:**

The classic psy bassline plays 16ths on the off-beats (positions 2, 3, 4 of every beat). The kick occupies the 1st 16th and bleeds into the 2nd. The bass note on the 2nd 16th must:
- Start *after* the kick's punch has decayed (~80–100 ms in) — otherwise mud.
- Be sidechained to duck −3 to −6 dB during the kick hit, releasing over ~150–200 ms (lines up with the kick's decay 2).
- Be tuned to the **same fundamental** as the kick (both F#1, or kick at F#1 / bass at root of track key if track is in F# Phrygian — the dominant psy mode).

This is why `vol_decay_2 = 250–400 ms` in the Projektor presets — the kick's sub tail intentionally sustains under the first bass note, so the low end never has a gap. Sidechain on the bass, not on the kick, is the canonical psy workflow.

---

## 4. Tuning conventions

Psytrance keys cluster heavily around **E, F, F#, G** (with F# being the single most common). This is because:

1. **F# (46.25 Hz)** sits on the boundary between "feel" sub (20–50 Hz, mostly tactile) and "hear" sub (50–120 Hz, audible pitch). Big rigs reproduce 46 Hz cleanly; club PAs roll off hard below 40 Hz. F# is the lowest note that *consistently translates* on Funktion-One and similar.
2. **G (49 Hz)** and **G# (51.9 Hz)** sit slightly higher and play more cleanly on smaller systems — often used in Iono Music / progressive psy releases that target headphones + festival both.
3. **A (55 Hz)** is hi-tech / twilight territory — Astrix, Vini Vici, harder forest. More audible pitch, more aggressive bass interaction.
4. **E (41.2 Hz)** is darkpsy / forest-deep territory — Burn in Noise, deep forest acts. Borderline reproduction on small systems.

Producers tune the kick to the **root of the track key**, OR to a **fifth/fourth below** the root if they want a "looser" feel. For F# Phrygian tracks (most common scale in the genre), kick at F#1 and bass at F#1 means kick + bass = same fundamental, locked phase. Some producers (Outsiders, often) flip the bass polarity to get a different low-end character — see `invert_phase` in the Python preset for `Projektor Deep Sub`.

**Default for KickAss: F#1 (46.25 Hz).** Snap-to-note dropdown is already in the Python — the C++ port should preserve C1–C2 chromatic snap.

---

## 5. Reverse-engineering the built-in Python presets

### `Projektor Punch` — main-room hard set, 145–148 BPM

```
start_freq=12000  mid_freq=350   end_freq=49 (G1)
sweep_time_1=8    sweep_time_2=80   pitch_curve=4.0
vol_attack=0.5    vol_hold=15  vol_decay_1=60  vol_sustain=30   vol_decay_2=250
scoop_start=15    scoop_length=40   scoop_depth=10
drive=2.5         click_vol=0.15   tail_drive=1.0   invert=False
```

What each choice does, sonically:

- **start_freq 12 kHz** + **sweep_time_1 8 ms**, **pitch_curve 4**: an aggressive but not hardstyle-sharp click chirp. The 4.0 curve means the pitch crashes most of the way down in the first ~3 ms, then eases into the body. This gives a *snappy* click without the "zap" of hardstyle (which uses 15 kHz + curve 5+).
- **mid_freq 350 Hz**: the punch lives at 350 Hz. Not 500 Hz (too main-room/EDM-y), not 200 Hz (too round/techno). 350 Hz is the Projektor sweet spot — vocal-range punch that cuts through a busy mid bus.
- **end_freq 49 Hz (G1)**: G — slightly above F#. Cleaner on smaller systems, common in Iono Music releases.
- **sweep_time_2 80 ms** + total decay ~325 ms: medium-length tail. Lands cleanly before the next beat at 145–148 BPM.
- **vol_sustain 30 %**: noticeable plateau but not too long — keeps the kick distinct from the bassline.
- **scoop_depth 10 %** at 15–55 ms: a gentle midrange dip — opens up space for the bass first harmonic at ~98 Hz (G2), and the bassline rolloff at 200 Hz.
- **drive 2.5** + **tail_drive 1.0**: noticeable but not crushed saturation. The mild tail_drive adds 2nd/3rd harmonic to the sub tail so it reads bigger on small speakers.
- **click_vol 0.15**: present click — about −16 dB relative to peak. Audible "tick" without being annoying.
- **Real-track context**: main-room hard psy set, full Funktion-One. Plays well with rolling 16th bass at A2/F#2. Outsiders-leaning style.

### `Projektor Deep Sub` — forest / deep twilight, 142–145 BPM

```
start_freq=8000   mid_freq=200   end_freq=43.65 (F1)
sweep_time_1=15   sweep_time_2=150   pitch_curve=2.5
vol_attack=2      vol_hold=30  vol_decay_1=100  vol_sustain=70   vol_decay_2=400
scoop_start=20    scoop_length=60   scoop_depth=5
drive=1.2         click_vol=0.05   tail_drive=3.0   invert=True
```

- **start_freq 8 kHz** + **sweep_time_1 15 ms** + **pitch_curve 2.5**: a *softer* click. The 2.5 curve linearises the sweep — much less aggressive. This is the "no spitty click" Projektor signature on deep tracks.
- **mid_freq 200 Hz**: the punch is lower in the spectrum. Less vocal-range slap, more chest-thump.
- **end_freq 43.65 Hz (F1)**: very low — F1 is on the edge of small-speaker reproduction. Deliberate: this is a sub-rig kick.
- **sweep_time_2 150 ms** + **vol_decay_2 400 ms**: very long tail. Total ~530 ms. At 142 BPM (1 beat = 422 ms), the kick *literally bleeds into the next downbeat*. This is intentional for deep twilight — wall-of-sub vibe.
- **vol_sustain 70 %**: heavy sustained plateau. The sub stays loud for a long time. Combined with tail_drive 3.0, you get continuous low-mid harmonic generation through the tail.
- **scoop_depth 5 %**: very gentle scoop. Deep forest doesn't need the aggressive fish-tail — the bass is doing different work in this style.
- **drive 1.2**: clean, almost no clipping. Sub stays clean.
- **tail_drive 3.0**: heavy *tail-weighted* saturation — this is the magic for "loud-sounding sub on a laptop". The base sine stays clean, but as the tail decays the saturation ramp adds harmonics, so the perceived loudness of the sub keeps up.
- **invert_phase True**: polarity flipped — kick will sum differently with a non-inverted bass. Some forest producers want this looser low-end interaction.
- **Real-track context**: forest 142–145 BPM, long-tailed twilight floor at sunrise. Burn in Noise-leaning. Don't use this kick on a busy 148 BPM main-room set — it will mush.

### `Projektor Hard F#` — hi-tech / aggressive forest, 146–150 BPM

```
start_freq=14000  mid_freq=450   end_freq=46.25 (F#1)
sweep_time_1=5    sweep_time_2=110   pitch_curve=4.5
vol_attack=0.0    vol_hold=10  vol_decay_1=70  vol_sustain=40   vol_decay_2=200
scoop_start=10    scoop_length=35   scoop_depth=15
drive=4.0         click_vol=0.2   tail_drive=4.0   invert=False
```

- **start_freq 14 kHz** + **sweep_time_1 5 ms** + **pitch_curve 4.5**: aggressive snap. Approaching hardstyle territory but with the lower end_freq keeping it psytrance.
- **mid_freq 450 Hz**: forward-punch, almost main-room.
- **end_freq 46.25 Hz (F#1)**: canonical F#. Translates everywhere.
- **vol_attack 0.0**: zero attack — kick must hit dead on the grid. Total length ~290 ms is short for psy.
- **vol_decay_2 200 ms**: shorter tail than `Deep Sub` — leaves more room for the bass at 146+ BPM.
- **scoop_depth 15 %**: noticeable fish-tail dip — opens room for a busy bassline.
- **drive 4.0** + **tail_drive 4.0**: heavy saturation overall. Pushes hard. This is where it earns "Hard F#".
- **click_vol 0.2**: prominent click — about −14 dB. Heard clearly on small speakers.
- **Real-track context**: 146–150 BPM hi-tech, dense arrangements (Vini Vici, late Astrix, Captain Hook). Cuts through walls of supersaws and FM leads.

---

## 6. What KickAss needs that Bazzism/the Python doesn't

The Python already covers the synthesis fundamentals well. What's missing or weak for nailing Projektor-style specifically:

| Feature                                   | Why it matters                                                                                                            | Verdict for KickAss                                                              |
|-------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| **Separate sub osc + independent envelope** | The Python's single sine carries both punch and sub. Real Projektor kicks layer a sub sine with its *own* longer decay under the swept body. | **Must-have.** Add a second sine osc at `end_freq`, ADSR independent of body, mixable.                                                  |
| **HP filter on click layer**              | The current click sweep (10k→2k) bleeds into the body band when click_vol > 0.1. A 1.5–2 kHz HP keeps it tight.            | **Add.** Trivial — 1-pole HP on the click signal before sum.                     |
| **Optional second body harmonic (~200 Hz)** | Some Projektor kicks have a noticeable 200 Hz second formant (the "chest" frequency) that adds body without scooping. | **Nice-to-have.** Add a low-amplitude resonant peak at user-set Hz (default 200). |
| **Transient shaper (attack/sustain)**     | Producers always reach for SPL Transient Designer / Native Instruments Transient Master after Bazzism. Building it in saves an insert slot and lets presets ship "finished". | **Add.** Even a simple envelope-follower-driven gain on attack and sustain bands. |
| **Tail-driven analog saturation**         | Already in Python as `tail_drive`. Confirmed correct — this is *the* trick for "loud sub on laptop".                         | **Keep as-is.** Port the tanh + drive ramp directly.                              |
| **Soft-clip ceiling**                     | Mix engineers expect the kick channel to be hot but not over 0 dBFS. A built-in −0.3 dB ceiling soft-clipper prevents clipping when stacking.                     | **Add.** Simple tanh at −0.3 dB.                                                  |
| **Tempo-sync display**                    | At 146 BPM, the user should see "kick ends at sixteenth 3.2" — helps avoid bass-collision design errors.                       | **Nice-to-have.** Visual only.                                                    |
| **Phase-align knob (kick-to-bass-note)**  | Manual delay 0–20 ms to align kick fundamental zero-crossing with bass note start.                                          | **Nice-to-have for v2.**                                                          |

**Priority order for v1:** separate sub osc, HP on click, transient shaper, soft-clip ceiling. Everything else is v2.

The `tail_drive` mechanism in the Python (linear ramp squared, drives tanh) is genuinely good. Don't replace it with a more "modern" saturator — that ramped tanh *is* what produces the Projektor sub character. Port it byte-for-byte.

---

## 7. Factory preset recipes — 10 presets ready for C++

All parameters in the same schema as the Python `BUILTIN_PRESETS` dict. Add these next to the existing 3 Projektor presets.

### 7.1 `Forest Stomp 145`
Deep forest, 144–146 BPM, twilight floor.
```
start_freq=7000   mid_freq=250   end_freq=46.25
sweep_time_1=12   sweep_time_2=130   pitch_curve=2.8
vol_attack=1.0    vol_hold=20  vol_decay_1=80  vol_sustain=55   vol_decay_2=320
scoop_start=18    scoop_length=50   scoop_depth=8
drive=1.8         click_vol=0.07  tail_drive=2.5  invert_phase=False
```

### 7.2 `Hard Psy Main F#`
Main-room hard, 146–148 BPM, big rooms.
```
start_freq=13000  mid_freq=400   end_freq=46.25
sweep_time_1=6    sweep_time_2=90   pitch_curve=4.2
vol_attack=0.0    vol_hold=12  vol_decay_1=65  vol_sustain=35   vol_decay_2=220
scoop_start=12    scoop_length=38   scoop_depth=12
drive=3.2         click_vol=0.18  tail_drive=2.0  invert_phase=False
```

### 7.3 `Deep Twilight G`
Slow twilight, 140–142 BPM, deep sunrise sets.
```
start_freq=6000   mid_freq=180   end_freq=49.0
sweep_time_1=18   sweep_time_2=180   pitch_curve=2.2
vol_attack=2.5    vol_hold=35  vol_decay_1=110  vol_sustain=72   vol_decay_2=420
scoop_start=22    scoop_length=65   scoop_depth=4
drive=1.1         click_vol=0.04  tail_drive=3.2  invert_phase=True
```

### 7.4 `Progressive 138`
Iono Music progressive, 137–139 BPM, headphone-friendly.
```
start_freq=9000   mid_freq=300   end_freq=49.0
sweep_time_1=10   sweep_time_2=110   pitch_curve=3.2
vol_attack=1.0    vol_hold=18  vol_decay_1=75  vol_sustain=45   vol_decay_2=290
scoop_start=16    scoop_length=45   scoop_depth=7
drive=2.0         click_vol=0.10  tail_drive=1.5  invert_phase=False
```

### 7.5 `Hi-Tech Snap A`
Hi-tech / Vini Vici lane, 148–152 BPM, dense mixes.
```
start_freq=15000  mid_freq=500   end_freq=55.0
sweep_time_1=4    sweep_time_2=95   pitch_curve=5.0
vol_attack=0.0    vol_hold=8   vol_decay_1=60  vol_sustain=30   vol_decay_2=180
scoop_start=8     scoop_length=30   scoop_depth=18
drive=4.5         click_vol=0.22  tail_drive=3.5  invert_phase=False
```

### 7.6 `Darkpsy Stomp E`
Dark / forest-deep, 148–155 BPM.
```
start_freq=8000   mid_freq=220   end_freq=41.20
sweep_time_1=10   sweep_time_2=120   pitch_curve=3.0
vol_attack=0.5    vol_hold=15  vol_decay_1=70  vol_sustain=50   vol_decay_2=260
scoop_start=14    scoop_length=42   scoop_depth=12
drive=3.0         click_vol=0.12  tail_drive=3.5  invert_phase=False
```

### 7.7 `Astrix Main F#`
Late-Astrix / Captain Hook bright main-room, 144–146 BPM.
```
start_freq=12500  mid_freq=380   end_freq=46.25
sweep_time_1=7    sweep_time_2=85   pitch_curve=4.0
vol_attack=0.2    vol_hold=14  vol_decay_1=62  vol_sustain=32   vol_decay_2=230
scoop_start=13    scoop_length=38   scoop_depth=11
drive=2.8         click_vol=0.16  tail_drive=1.5  invert_phase=False
```

### 7.8 `Burn In Noise Forest`
Burn-in-Noise style forest, 144–148 BPM, dense low-end.
```
start_freq=7500   mid_freq=240   end_freq=43.65
sweep_time_1=11   sweep_time_2=125   pitch_curve=2.7
vol_attack=1.5    vol_hold=22  vol_decay_1=85  vol_sustain=58   vol_decay_2=340
scoop_start=18    scoop_length=52   scoop_depth=9
drive=2.2         click_vol=0.08  tail_drive=3.0  invert_phase=True
```

### 7.9 `Sub Lover 142`
Sub-rig showcase, 141–143 BPM, festival main stage.
```
start_freq=6500   mid_freq=200   end_freq=43.65
sweep_time_1=15   sweep_time_2=160   pitch_curve=2.4
vol_attack=2.0    vol_hold=28  vol_decay_1=95  vol_sustain=65   vol_decay_2=380
scoop_start=20    scoop_length=58   scoop_depth=5
drive=1.4         click_vol=0.05  tail_drive=3.8  invert_phase=False
```

### 7.10 `Tight Club F#`
Club-translation friendly, 145–147 BPM, plays on PA *and* car speakers.
```
start_freq=11000  mid_freq=340   end_freq=46.25
sweep_time_1=8    sweep_time_2=75   pitch_curve=3.6
vol_attack=0.3    vol_hold=12  vol_decay_1=55  vol_sustain=28   vol_decay_2=200
scoop_start=12    scoop_length=35   scoop_depth=10
drive=2.4         click_vol=0.13  tail_drive=1.2  invert_phase=False
```

---

## 8. References

### Direct production sources

- [Projektor official site](https://projektorsound.com/) — confirms sound design heavy, themed approach; sells a kick sample pack (43 samples) and a Psytrance Masterclass (4h31).
- [Projektor Bandcamp](https://projektortrance.bandcamp.com/)
- [Projektor on Apple Music](https://music.apple.com/us/artist/projektor/1495332981)
- [Projektor SoundCloud](https://soundcloud.com/projektor_music)
- [Projektor YouTube channel](https://www.youtube.com/channel/UC_Rkuu3zRLRpLohSmj7pGlA)
- [Projektor Psytrance Masterclass (MyLoops)](https://www.myloops.net/product/projektor-psytrance-masterclass) — 4h31 video course covering kick/bass synthesis end-to-end.

### Kick & bass technique

- [Psytrance Kick EQ — KVR Audio thread](https://www.kvraudio.com/forum/viewtopic.php?t=458619) — source of the "−15 dB scoop between 100–300 Hz" figure and the "fish-tail" terminology.
- [Top 5 EQ Tips for Psy Trance Producers — MyLoops](https://www.myloops.net/top-5-eq-tips-for-psy-trance-producers-bringing-your-mix-to-life) — confirms sub 20–60 Hz / body 60–200 Hz ranges; "kick 2–3 dB louder than bassline".
- [Psytrance bassline equalization — dsokolovskiy](https://dsokolovskiy.com/blog/all/psytrance-bassline-equalization/) — harmonics math (40 Hz fundamental → 80/120/160 Hz harmonics).
- [Psytrance bassline synthesis — dsokolovskiy](https://dsokolovskiy.com/blog/all/psytrance-bassline-synthesis/) — bass envelope (attack 0, decay 30%, sustain ~0); "kick decay tied to tempo".
- [How to fit kick and bass together — dsokolovskiy](https://dsokolovskiy.com/blog/all/how-to-fit-kick-and-bass-together/) — narrow-bell 1–2 dB cuts at bass harmonics on the kick; phase alignment.
- [Sonic Academy KICK Tutorial — Shadow Chronicles](https://www.sonicacademy.com/courses/psy-trance-with-shadow-chronicles/tutorial-03-kick-drum-570) — kick is a sine with pitch envelope; click around 10 kHz.
- [How to make a proper psytrance kick (YouTube)](https://www.youtube.com/watch?v=XwaUc9TXiRM)
- [The Ultimate Guide to Psytrance Kick & Bass (YouTube)](https://www.youtube.com/watch?v=wvFAgX2fS-8)
- [PsyTrance Kick Drum Creation With Bazzism (YouTube)](https://www.youtube.com/watch?v=KK3ENEbMFT0)
- [PSYTRANCE Kick Synthesis in BazzISM (YouTube)](https://www.youtube.com/watch?v=bgGcxrdn4D4)
- [How the 'pros' make kicks — PsyMusic UK](https://www.psymusic.co.uk/forum/threads/how-the-pros-make-kicks.69095/)
- [BazzISM2 manual (Scribd)](https://www.scribd.com/document/295175355/BazzISM2-5-0-Manual) — confirms tSweep/tEnd/vSweep parameter model; multistage envelope creates the post-attack dip.

### Tuning & scales

- [Understanding Scales & Modes in Psytrance — Outerverse.fm](https://outerverse.fm/blogs/tutorials/understanding-scales-modes-in-psytrance) — confirms F# Phrygian as dominant; E/F/F#/G the most-used keys.
- [Psytrance bassline tutorial — feelyoursound.com](https://feelyoursound.com/articles/psytrance-basslines/) — kick tuned to root or fifth/fourth of track key.

### Forest / dark / hi-tech style references

- [Forest Psytrance genre — Melodigging](https://www.melodigging.com/genre/forest-psytrance) — 145–152 BPM, organic foley, dark mood.
- [The hidden formula for Kick & Bass in Forest Psytrance w/ Anarkick — Future Media Academy](https://future-media.academy/online/hidden-formula-anarkick)
- [Outsiders sample pack — Modulart](https://www.modulartsounds.com/the-stash-vol-1-psytrance-sample-pack/) — confirms Outsiders (Haim Lev & Guy Malka, Israel) as a benchmark for kick/bass production quality in the scene.
- [Mute Production Forest Kicks — SounDirective](https://soundirective.com/product/mute-production-forest-kicks/) — context for forest-specific kick design (80 KICK 2 presets ranging from "swampy" to "driving").
- [Iboga Records 20 Years interview — Trancentral](https://trancentral.tv/2016/11/iboga-records-20-years-of-psytrance-interview/)

### Phase alignment

- [Psytrance Kick & Bass Phase Alignment — YouTube](https://www.youtube.com/watch?v=EafJ2eCCvho)
- [Mastering Psytrance kick and bass phase alignment — YouTube](https://www.youtube.com/watch?v=HOcNk4B3UYU) — polarity flip shifts low end from "chesty/punchy" to "subby/huge" — informs the `invert_phase` choices in `Projektor Deep Sub` and presets 7.3 / 7.8.

### Mixing / EQ

- [How To EQ Kick Drums: The Complete Producer's Guide (2025) — EDMProd](https://www.edmprod.com/eq-kick-drums/) — general kick EQ reference, click 5–10 kHz region.
- [How To Make Techno: Perfecting Your Kick — Toolroom Academy](https://toolroomacademy.com/features/how-to-make-techno-perfecting-your-kick/)

### Internal

- `D:\GoogleDrive\B_projects\KickAss\reference\BazzismRebuild.py` — lines 50–73 for the three reverse-engineered `Projektor *` presets; lines 283–412 for the DSP that the C++ port must match.
