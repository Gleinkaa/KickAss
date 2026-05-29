#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KickEngine.h"
#include "EnvCurve.h"
#include <atomic>

class PresetManager;  // fwd

//==============================================================================
class KickAssProcessor : public juce::AudioProcessor
{
public:
    KickAssProcessor();
    ~KickAssProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** UI-thread offline render for the visualizer (Phase 4). */
    void offlineRender (juce::AudioBuffer<float>& buffer, double durationMs);

    /** UI reads this at ~30Hz to draw the playback cursor in the waveform display. */
    int getPlaybackSamplePos() const noexcept { return engine.getPlaybackSamplePos(); }

    /** Thread-safe trigger request. UI thread stores note+velocity here; the audio
        thread drains it at the start of processBlock and calls engine.triggerNote()
        where it is contractually safe. Never call engine.triggerNote() from the UI. */
    void requestTrigger (int midiNote = 60, float velocity = 1.0f) noexcept;

    KickEngine& getEngine() noexcept { return engine; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

    //==============================================================================
    // Phase 6b — breakpoint envelope storage.
    // The editor mutates this directly; call syncVolCurveToValueTree() afterwards
    // so the curve is persisted in apvts.state for save/load. Audio thread takes
    // its own snapshot at noteOn via getVolEnvCurveSnapshot() under the spin-lock.
    EnvCurve& getVolEnvCurve()                  noexcept { return volEnvCurve; }
    const EnvCurve& getVolEnvCurve() const      noexcept { return volEnvCurve; }
    juce::SpinLock& getVolCurveLock()           noexcept { return volCurveLock; }

    /** Editor calls this after any edit to persist the curve into apvts.state. */
    void syncVolCurveToValueTree();

    /** Build the curve from the current AHDSR APVTS values; used on first run + Simple→Advanced. */
    void rebuildVolCurveFromAhdsr();

    /** Try to load curve from apvts.state <Curves>/vol_env, falling back to AHDSR rebuild.
        Called after any path that mutates APVTS state (preset load, replaceState, factory apply). */
    void restoreVolCurveFromStateOrAhdsr();

    /** True iff envelope_mode = Advanced (uses custom curve), false = Simple (uses AHDSR knobs). */
    bool isEnvelopeAdvanced() const noexcept;

    /** Publish the current volEnvCurve to both audio and offline engines (lock-free flip). */
    void publishVolCurveToEngines();

    // Curve-source bookkeeping. "ahdsr" = derived from current knob values, refreshable.
    // "custom" = user-edited via BreakpointEditor, do not auto-overwrite.
    juce::String getVolCurveMode() const;            // returns "ahdsr" if missing
    void setVolCurveMode (const juce::String& mode); // "ahdsr" | "custom"

    /** Called when the user enters Advanced mode. If the curve was never customized,
        refresh it from current AHDSR knob values so the displayed Advanced curve
        matches what was just heard in Simple mode. No-op if mode is already "custom". */
    void onEnterAdvancedMode();

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    KickEngine engine;
    KickEngine offlineEngine;   // dedicated for UI-thread visualizer renders (decoupled from RT state)
    std::unique_ptr<PresetManager> presetManager;

    // Cached atomic pointers — read once, avoid the lookup on the audio thread.
    void cacheParamPointers();
    void readParamsIntoEngine();

    // PITCH
    std::atomic<float>* pStartFreq    = nullptr;
    std::atomic<float>* pMidFreq      = nullptr;
    std::atomic<float>* pEndFreq      = nullptr;
    std::atomic<float>* pSweepTime1   = nullptr;
    std::atomic<float>* pSweepTime2   = nullptr;
    std::atomic<float>* pPitchCurve   = nullptr;
    // AMP
    std::atomic<float>* pVolAttack    = nullptr;
    std::atomic<float>* pVolHold      = nullptr;
    std::atomic<float>* pVolDecay1    = nullptr;
    std::atomic<float>* pVolSustain   = nullptr;
    std::atomic<float>* pVolDecay2    = nullptr;
    std::atomic<float>* pVolCurve     = nullptr;
    // SCOOP
    std::atomic<float>* pScoopStart   = nullptr;
    std::atomic<float>* pScoopLength  = nullptr;
    std::atomic<float>* pScoopDepth   = nullptr;
    // TRANSIENT
    std::atomic<float>* pClickVol     = nullptr;
    std::atomic<float>* pClickType    = nullptr;   // choice param backs onto float
    std::atomic<float>* pClickHpf     = nullptr;
    std::atomic<float>* pClickTone    = nullptr;
    std::atomic<float>* pClickDecay   = nullptr;
    // DRIVE
    std::atomic<float>* pDrive        = nullptr;
    std::atomic<float>* pTailDrive    = nullptr;
    std::atomic<float>* pSatType      = nullptr;   // choice param backs onto float
    // MASTER
    std::atomic<float>* pInvertPhase  = nullptr;   // bool backs onto float
    std::atomic<float>* pSafetyLimit  = nullptr;   // bool backs onto float
    std::atomic<float>* pOutputGain   = nullptr;
    std::atomic<float>* pPitchTrack   = nullptr;
    std::atomic<float>* pPhaseOffset  = nullptr;
    // GLOBAL
    std::atomic<float>* pEnvelopeMode = nullptr;   // 0 = Simple, 1 = Advanced

    // Phase 6b — breakpoint envelope storage (UI-thread authoritative)
    EnvCurve       volEnvCurve;
    juce::SpinLock volCurveLock;

    // UI→audio trigger queue (lock-free single-slot).
    // pendingNote stores the MIDI note; pendingVelocity stores velocity.
    // A pendingNote value of -1 means "no pending trigger".
    std::atomic<int>   pendingNote     { -1 };
    std::atomic<float> pendingVelocity { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickAssProcessor)
};
