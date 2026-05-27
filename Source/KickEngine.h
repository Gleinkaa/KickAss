#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

//==============================================================================
// KickEngine — pure DSP class, owned by value inside KickAssProcessor.
//
// PHASE 1 STATUS: stub. renderBlock writes silence.
// Phase 2 will port the Python DSP (pitch env → sine osc → AHDSR+scoop → transient
// layer → 4x oversampled tanh → DC blocker → soft-clip → output gain).
//
// Threading contract: prepare/setParams/triggerNote/renderBlock/reset are all
// called from the audio thread. No allocations / no locks / no logging.
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
    int   clickType    = 0;       // 0=Sine, 1=Noise, 2=Both
    float clickHpfHz   = 800.0f;
    float clickToneHz  = 10000.0f;
    float clickDecayMs = 5.0f;

    // DRIVE
    float drive        = 1.5f;
    float tailDrive    = 0.0f;

    // MASTER
    bool  invertPhase  = false;
    float outputGainDb = 0.0f;
    float pitchTrack   = 0.0f;    // 0..1 (UI is %)
};

//==============================================================================
class KickEngine
{
public:
    KickEngine() = default;

    void prepare (double sampleRate, int /*samplesPerBlock*/);
    void reset();

    void setParams (const KickParams& p) noexcept { params = p; }

    /** Sample-accurate trigger. sampleOffset is the offset within the current block. */
    void triggerNote (int midiNote, float velocity, int sampleOffset) noexcept;

    /** Add (do NOT replace) the kick into the given stereo buffer. */
    void renderBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    /** UI-thread offline render. Writes the entire kick into the buffer. */
    void renderOffline (juce::AudioBuffer<float>& buffer, double sampleRate, double durationMs);

    bool isActive() const noexcept { return active.load(); }
    int  getPlaybackSamplePos() const noexcept { return playbackPos.load(); }

private:
    KickParams params {};
    double currentSampleRate = 44100.0;

    // Voice state
    std::atomic<bool> active { false };
    std::atomic<int>  playbackPos { 0 };

    // Pending trigger (read at the offset within the next renderBlock)
    int   pendingTriggerOffset = -1;
    int   pendingTriggerNote   = 60;
    float pendingTriggerVel    = 1.0f;
};
