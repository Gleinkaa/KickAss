#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <array>
#include "EnvCurve.h"

//==============================================================================
// KickEngine — pure DSP class, owned by value inside KickAssProcessor.
//
// Threading contract: prepare/setParams/triggerNote/renderBlock are called from
// the audio thread ONLY. No allocations / no locks / no logging inside renderBlock.
// UI-thread code must never call triggerNote directly — use KickAssProcessor::requestTrigger()
// which posts to a lock-free queue drained by processBlock.
// renderOffline is called from the UI thread and is allowed to allocate.
//
// Signal chain (per-sample, see ARCHITECTURE.md §2):
//   pitch env → sine osc → AHDSR + scoop → + transient layer (sine/noise/both, HPF+LPF)
//   → upsample 4x → tanh-with-drive-ramp → downsample 4x → polarity invert
//   → DC blocker → soft-clip ceiling → output gain
//==============================================================================

struct KickParams
{
    // PITCH
    float startFreq    = 8000.0f;
    float midFreq      = 150.0f;
    float endFreq      = 46.25f;
    float sweepTime1Ms = 10.0f;
    float sweepTime2Ms = 80.0f;
    float pitchCurve   = 3.0f;

    // AMP
    float volAttackMs  = 2.0f;
    float volHoldMs    = 10.0f;
    float volDecay1Ms  = 50.0f;
    float volSustain   = 0.40f;   // 0..1 (UI is %)
    float volDecay2Ms  = 150.0f;
    float volCurve     = 3.0f;

    // SCOOP
    float scoopStartMs  = 10.0f;
    float scoopLengthMs = 30.0f;
    float scoopDepth    = 0.0f;   // 0..1 (UI is %)

    // TRANSIENT
    float clickVol     = 0.0f;
    int   clickType    = 0;       // 0=Sine, 1=Noise, 2=Both, 3=Sample
    float clickHpfHz   = 800.0f;
    float clickToneHz  = 10000.0f;
    float clickDecayMs = 5.0f;

    // DRIVE
    float drive        = 1.5f;
    float tailDrive    = 0.0f;
    int   saturationType = 0;     // 0=Tanh, 1=SoftClip, 2=HardClip, 3=Tube, 4=Foldback

    // MASTER
    bool  invertPhase  = false;
    bool  safetyLimit  = true;    // final brickwall safety clip at -0.1 dBFS (ON by default)
    float outputGainDb = 0.0f;
    float pitchTrack   = 0.0f;    // 0..1 (UI is %)
    float phaseOffset  = 0.0f;    // 0..1 cycle fraction (UI is degrees)

    // VISUALIZER-ONLY: when true, the synth body (pitched osc * amp env) is
    // muted so renderOneDrySample emits ONLY the transient/sample layer. The
    // realtime path never sets this (default false); offlineRenderTransient()
    // flips it on the dedicated offline engine so the TRANSIENT view can show
    // the bare click/sample shape without the body drowning it out.
    bool  soloTransient = false;
};

//==============================================================================
class KickEngine
{
public:
    KickEngine() = default;

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    void setParams (const KickParams& p) noexcept { params = p; }

    /** Trigger a new voice.
        Sample-accurate MIDI timing is achieved by the CALLER: processBlock
        renders the block in segments between note-on events and calls this at
        the exact event position, so the voice starts on the very next rendered
        sample. Callers that do not segment should pass sampleOffset = 0. */
    void triggerNote (int midiNote, float velocity, int sampleOffset) noexcept;

    /** Add the kick into the given stereo buffer at [startSample .. startSample+numSamples).
        If numSamples exceeds the prepared block size, it is processed in chunks of the
        prepared size (never larger than the oversampler was initialised with). */
    void renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    /** UI-thread offline render. Writes the entire kick into the buffer at the given sample rate. */
    void renderOffline (juce::AudioBuffer<float>& buffer, double sampleRate, double durationMs);

    /** Total latency from the oversampler — report via setLatencySamples(). */
    int  getLatencySamples() const noexcept;

    bool isActive() const noexcept { return active.load(); }
    int  getPlaybackSamplePos() const noexcept { return playbackPos.load(); }

    //--------------------------------------------------------------------------
    // Phase 6b — custom amp envelope via breakpoint curve.
    //
    // publishVolCurve: called from UI thread. Fills a FREE LUT slot from
    // `src`, then atomically flips the active index. Lock-free for the audio
    // thread. Safe to call as often as the editor wants — typical ≤30 Hz.
    //
    // TRIPLE-BUFFER protocol (3 slots, A6 fix). The old 2-slot scheme had an
    // ABA hole: a second publish during one voice picked the very slot the
    // live voice was reading (the first publish had already flipped `active`
    // away from it), so the fill — including its setSize realloc — raced the
    // audio thread (torn audio, use-after-free).
    //
    // Roles are strictly separated: the UI thread is the ONLY writer of
    // activeLutIdx; the audio thread only copies active → voiceLutIdxAtomic
    // at triggerNote. On publish the UI picks a slot that is neither
    // `active` nor the live `voiceLutIdxAtomic` slot and fills it before the
    // release-flip. Because only the UI ever changes `active`, the chosen
    // slot cannot become the live one while it is being filled: a trigger in
    // the meantime snapshots `active` (a different slot), and the flip that
    // would make it live happens strictly after the fill. With 3 slots and 2
    // exclusions there is always exactly one free slot.
    //
    // setCurveMode: atomic flag. When false, the engine uses the existing
    // closed-form AHDSR math (no behavior change vs. pre-6b). When true, it
    // reads the LUT. Mode is snapshotted at noteOn for the voice lifetime so
    // a mid-voice toggle never produces a discontinuity.
    //--------------------------------------------------------------------------
    void publishVolCurve (const EnvCurve& src);
    void setCurveMode (bool useCurve) noexcept { curveModeAtomic.store (useCurve); }

    //--------------------------------------------------------------------------
    // Drag-a-WAV transient layer (v1.1).
    //
    // Same TRIPLE-buffered RT-safe handoff as publishVolCurve() (see above)
    // with sampleLength/sampleSourceRate stored per slot: the UI thread
    // copies the (mono) sample into a FREE slot — not `active`, not the
    // live voice's slot — stores its source rate + length, then atomically
    // flips the active index with release ordering. The audio thread
    // snapshots the active index at triggerNote (into voiceSampleIdxAtomic)
    // and reads only that slot for the voice lifetime, so a mid-voice UI
    // replacement never glitches and never races a setSize realloc.
    //
    // publishTransientSample: UI thread. Copies monoData (1 channel) into the
    //   free slot. Allocation (the AudioBuffer copy) happens HERE, never on
    //   the audio thread.
    // clearTransientSample: UI thread. Publishes an empty slot (length 0).
    //--------------------------------------------------------------------------
    void publishTransientSample (const juce::AudioBuffer<float>& monoData, double sourceSampleRate);
    void clearTransientSample();

private:
    //--------------------------------------------------------------------------
    // Per-sample DSP — shared between realtime and offline render paths.
    // Returns a single mono sample (or 0 if voice inactive). Advances state.
    // Note: drive + DC blocker + soft-clip + polarity + gain are applied at block level,
    // because drive needs oversampling.
    //--------------------------------------------------------------------------
    float renderOneDrySample() noexcept;

    /** Apply drive (oversampled), DC blocker, soft-clip, polarity, gain — all in place. */
    void applyPostStages (float* samples, int n) noexcept;

    // Voice activation: total kick duration in samples (from envelope params)
    int computeTotalSamples() const noexcept;

    // Triple-buffer slot pickers — UI THREAD ONLY. Returns a slot that is
    // neither the active one nor the live voice's snapshot (see the protocol
    // comment above). With 3 slots and 2 exclusions one always exists.
    int pickFreeLutSlot() const noexcept;
    int pickFreeSampleSlot() const noexcept;

    //--------------------------------------------------------------------------
    KickParams params {};
    double sampleRate = 44100.0;
    int    blockSize = 512;

    // Voice state (mono single-voice)
    std::atomic<bool> active { false };
    std::atomic<int>  playbackPos { 0 };

    double phase = 0.0;              // sine phase accumulator (double precision)
    int    sampleSinceTrigger = 0;
    int    totalSamples = 0;         // envelope duration in samples — normalizes the drive ramp
    // A5: end-of-voice sample count. Equals totalSamples except in Sample click
    // mode with a loaded sample, where the voice must outlive the amp envelope
    // so the sample plays its natural length instead of being hard-cut. Used
    // ONLY for the end-of-voice check — never for the drive ramp or scoop.
    int    voiceEndSamples = 0;
    float  velocity = 1.0f;
    float  pitchTransposeRatio = 1.0f;

    // Click state
    int    clickSamplesLeft = 0;
    int    clickTotalSamples = 0;
    double clickPhase = 0.0;
    // Click filter state (1-pole HP + 1-pole LP)
    float  clickHpfPrevIn = 0.0f, clickHpfPrevOut = 0.0f;
    float  clickLpfPrev   = 0.0f;

    // DC blocker (1-pole HP @ ~8 Hz, always on)
    float dcPrevIn = 0.0f, dcPrevOut = 0.0f;
    float dcR = 0.999f;                  // computed in prepare()

    // 2 ms retrigger crossfade — saves a snapshot of the prior voice's most recent samples
    static constexpr int crossfadeMs = 2;
    int    crossfadeLen = 0;
    int    crossfadeRemaining = 0;
    std::vector<float> crossfadeTail;

    // Noise generator
    juce::Random rng { 1337 };

    // Oversampler (juce::dsp::Oversampling)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int oversamplerLatency = 0;

    // Pre-allocated scratch buffers (renderBlock path)
    std::vector<float> dryScratch;            // size = blockSize
    juce::AudioBuffer<float> overSampledBuf;  // size = blockSize × OSfactor, mono

    // ---- Phase 6b: custom amp envelope LUT (TRIPLE-buffered, see publishVolCurve) ----
    std::array<std::array<float, EnvCurve::kLutSize>, 3> ampLuts {};
    float                                 lutTotalMs[3] { 0.0f, 0.0f, 0.0f };
    std::atomic<int>                      activeLutIdx { 0 };
    std::atomic<bool>                     curveModeAtomic { false };

    // Voice-lifetime snapshot of curve state (so mid-voice UI edits don't glitch).
    // voiceLutIdxAtomic is written by the audio thread at triggerNote (copy of
    // activeLutIdx) and read by the UI thread's slot picker — see the protocol
    // comment above. voiceLutTotalMs is audio-thread-only after the snapshot.
    bool  voiceUseCurve   = false;
    std::atomic<int> voiceLutIdxAtomic { 0 };
    float voiceLutTotalMs = 0.0f;

    // ---- v1.1: drag-a-WAV transient sample (TRIPLE-buffered, mirrors the LUT) ----
    std::array<juce::AudioBuffer<float>, 3> sampleSlots;   // mono
    double            sampleSourceRate[3] { 44100.0, 44100.0, 44100.0 };
    int               sampleLength[3]     { 0, 0, 0 };
    std::atomic<int>  activeSampleIdx     { 0 };

    // Voice-lifetime snapshot of the sample slot + a read cursor (fractional, for
    // SR-correct linear-interpolated playback). voiceSampleIdxAtomic is written
    // at triggerNote (copy of activeSampleIdx) and read by the UI slot picker;
    // the rest are audio-thread-only after the snapshot.
    std::atomic<int>  voiceSampleIdxAtomic { 0 };
    int    voiceSampleLength  = 0;
    double voiceSampleRate    = 44100.0;
    double samplePlayPos      = 0.0;

    // ---- A9: output gain smoothing (~10 ms, linear in gain) ----
    // Applied per-sample instead of as a per-block constant so automated gain
    // changes don't zipper. reset()/prepare() set current AND target so
    // constant-gain renders are bit-identical to the pre-smoothing behaviour.
    juce::SmoothedValue<float> outputGainSmoothed;
};
