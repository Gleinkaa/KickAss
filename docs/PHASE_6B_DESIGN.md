# Phase 6b — Breakpoint envelope editor (design)

Status: **approved 2026-05-28** — implementing.
Scope: **volume envelope only** for 6b. Pitch is 6c. Noise/sample overlays are 6d.

## Locked decisions (user choices)

1. **Default mode = Advanced.** New instances + existing presets show the breakpoint editor immediately. AHDSR knobs are greyed-but-visible (host-automation compatibility). Old presets auto-convert AHDSR→6 points on load.
2. **Tension in v1.** Shift-drag a point modifies the curvature of the segment *leaving* that point (toward the next). Visual cue: a small tension dot appears mid-segment when non-zero; click-drag the dot also works.
3. **Global mode toggle.** One plugin-wide `envelope_mode` switch (Simple|Advanced) in the header bar, between the preset combo and note-snap. Flips all current and future envelopes together.

---

## Goal

Replace the parametric AHDSR knobs (`vol_attack`, `vol_hold`, `vol_decay_1`, `vol_sustain`, `vol_decay_2`, `vol_curve`) with a **freely editable point curve** drawn on top of the visualizer. Sanlight-dimmer style: drag points to move, double-click to add, right-click to delete. Linear segments between points with optional per-segment curvature.

A "Simple / Advanced" toggle keeps the old AHDSR knobs alive for users who don't want to draw curves — and keeps **every existing preset valid**.

---

## Data model

```cpp
struct EnvPoint {
    float timeMs;     // 0..maxMs, sorted ascending in the list
    float value;      // 0..1
    float tension;    // -1..+1, segment curvature toward the NEXT point (0 = linear)
};

struct EnvCurve {
    juce::String   id;           // "vol_env" for 6b
    float          maxTimeMs;    // canvas width in ms (driven by host envelope params)
    std::vector<EnvPoint> points;  // size 2..16; first.t = 0, last.v = 0
};
```

Hard rules:
- `points.size() ≥ 2`. Default = 6 points = AHDSR-equivalent.
- `points[0].timeMs == 0` (locked). User can move y only.
- `points.back().value == 0` (locked). User can move x only — sets total length.
- All interior points freely movable, addable (double-click), deletable (right-click context).
- Cap at 16 points (UI clarity + DSP table cost).

## Storage

**Decision:** sibling `ValueTree` inside `apvts.state` — same XML blob, automatic round-trip through `getStateInformation` / `setStateInformation`, no new serialization code.

```xml
<Parameters>
  <PARAM id="vol_attack" value="2.0" />   <!-- AHDSR knobs stay alive -->
  ...
  <Curves>
    <Curve id="vol_env" mode="custom">     <!-- mode: "ahdsr" | "custom" -->
      <Pt t="0"   v="0"    tn="0" />
      <Pt t="2"   v="1"    tn="0.3" />
      <Pt t="12"  v="1"    tn="0" />
      <Pt t="62"  v="0.4"  tn="-0.2" />
      <Pt t="212" v="0"    tn="0" />
    </Curve>
  </Curves>
</Parameters>
```

`<Curves>` is added as a child of `apvts.state` once at construction. Mode = `"ahdsr"` means: ignore the points, generate them on-the-fly from the knobs (default; old presets stay simple). Mode = `"custom"` means: use the points verbatim, knobs become inert greyed-out display only.

## Preset compatibility (the big one)

Old `.kickpreset` and old `.json` files have **no** `<Curves>` section. On load:
- If `<Curves>` absent → behave exactly like today (AHDSR mode, all knobs active). Zero regressions.
- If `<Curves>` present with mode=`ahdsr` → same as above; points are just a cached display copy.
- If `<Curves>` present with mode=`custom` → DSP reads from points list.

Toggling Simple↔Advanced does a one-shot conversion:
- Simple→Advanced: sample the current AHDSR shape into 6 breakpoints (no audible change).
- Advanced→Simple: best-fit AHDSR back from the curve (lossy — show a warning).

The Python `.json` reference format has no concept of breakpoints, so JSON export always uses Simple mode (forces a downgrade with warning if user has a custom curve).

## DSP integration

Two paths into the engine, gated by mode:

**ahdsr mode** (today's code, unchanged):
```cpp
ampEnv = computeAhdsr(t, params);   // closed-form, per-sample
```

**custom mode** (new):
```cpp
// At noteOn: snapshot the EnvCurve into an audio-thread-owned LUT of N entries.
// Per-sample: binary-search or interpolate the LUT.
ampEnv = sampleCurveLut(t, voiceCurveLut);
```

LUT size: 512 entries spanning `maxTimeMs`. ~2 KB per voice. Audio thread never touches the editor's `std::vector` — UI fills a triple-buffered snapshot at `noteOn` time.

**RT safety**: LUT swap uses an atomic pointer flip in `triggerNote`. Editor writes always go to the "next" buffer; audio thread reads "current". Same pattern as `PresetManager`'s parameter-apply path.

## Visualizer integration

The breakpoint editor is a **child component overlaying the existing visualizer's bottom half** (where the amp envelope already draws). It owns its own mouse handling and paints **on top of** the existing yellow envelope line (which becomes the segment polyline derived from the points instead of from AHDSR knobs).

Hit targets: 12 px radius around each point. Cursor changes (move, add via crosshair, delete via X) for affordance.

Mode toggle lives in the AMP panel header as a small text button: `[ Simple | Advanced ]`. In Simple, knobs work as today, points are hidden, the yellow line comes from AHDSR. In Advanced, knobs are dim/disabled, points are visible and draggable.

## Automation / host integration

Honest tradeoff: **breakpoint shapes are NOT host-automatable.** APVTS only does floats. We accept this — same as Vital's LFO editors, Serum 2's macro shapes, Drumforge envelopes. The 6 AHDSR knobs remain visible parameters (so old DAW projects with knob automation keep working), but in Advanced mode they're ignored by DSP. We document this clearly.

If a user wants automation in Advanced mode, they can automate a separate macro param (Phase 6e: "Env Morph" 0..1 that crossfades between two stored curves). Out of scope for 6b.

## What 6b ships

1. `EnvCurve` data model + `<Curves>` ValueTree integration in `KickAssProcessor`
2. LUT snapshot path in `KickEngine` + atomic swap on `triggerNote`
3. `BreakpointEditor` JUCE component: hit-test, drag, add (double-click), delete (right-click)
4. `[Simple|Advanced]` toggle in AMP panel header, with greying of knobs
5. AHDSR→points conversion when entering Advanced mode the first time
6. Updated `WaveformDisplay` to read the same curve source in `recomputeEnvelopeTraces` (so visualizer matches what DSP plays)
7. Save/load tests: old presets load identically, new presets with `<Curves>` round-trip

## What 6b does NOT ship (deferred)

- Pitch envelope as breakpoints (6c — needs log-Y handling, hardest UI)
- Noise/sample overlay envelope (6d)
- Curve morph macro automation (6e)
- Best-fit Advanced→Simple AHDSR conversion (6b initially just warns + zeros; revisit)
- Tension/curvature handles in UI (data model supports it; UI uses linear-only in v1; tension via shift-drag added later)

Estimated touch: ~400 lines added across `KickEngine.{h,cpp}`, `PluginProcessor.{h,cpp}`, new `BreakpointEditor.{h,cpp}`, small edits in `PluginEditor.cpp` and `WaveformDisplay.cpp`.

---

## Open questions for the user

1. **Default mode for new instances**: Simple (today's behavior, knobs work, curves hidden) or Advanced (curves visible, knobs greyed)? I'd recommend **Simple** to avoid scaring users who liked the AHDSR knobs.
2. **Tension/curvature in v1 UI**: ship linear-only first and add curves later, or ship with per-segment curvature (shift-drag a midpoint) from day one? I'd recommend **linear-only v1** — curvature doubles the UI work and you can preview shape with extra points.
3. **Mode toggle scope**: per-envelope (each envelope picks its own Simple/Advanced) or global plugin-wide? I'd recommend **per-envelope** — vol can be Advanced while pitch stays Simple in 6c.
