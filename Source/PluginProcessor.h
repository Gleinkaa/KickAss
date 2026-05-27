#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KickEngine.h"

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

    KickEngine& getEngine() noexcept { return engine; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

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
    // MASTER
    std::atomic<float>* pInvertPhase  = nullptr;   // bool backs onto float
    std::atomic<float>* pOutputGain   = nullptr;
    std::atomic<float>* pPitchTrack   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickAssProcessor)
};
