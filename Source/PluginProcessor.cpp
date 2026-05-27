#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter value-to-text formatters (per ARCHITECTURE.md §5 and research/03 §6.2)
//==============================================================================
namespace
{
    juce::String fmtHz (float hz, int)
    {
        if (hz < 100.0f)   return juce::String (hz, 1) + " Hz";
        if (hz < 1000.0f)  return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz * 0.001f, 2) + " kHz";
    }
    juce::String fmtMs (float ms, int)
    {
        if (ms < 10.0f) return juce::String (ms, 1) + " ms";
        return juce::String (juce::roundToInt (ms)) + " ms";
    }
    juce::String fmtPct (float v, int)
    {
        return juce::String (juce::roundToInt (v)) + " %";
    }
    juce::String fmtDb (float db, int)
    {
        return (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }
    juce::String fmtRatio (float v, int)
    {
        return juce::String (v, 2);
    }

    juce::NormalisableRange<float> rangeSkewed (float min, float max, float interval, float skew)
    {
        // JUCE 8: skew is passed via the 4-arg constructor; setSkewFactor was removed.
        return juce::NormalisableRange<float> (min, max, interval, skew);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
KickAssProcessor::createParameterLayout()
{
    using AF  = juce::AudioParameterFloat;
    using AB  = juce::AudioParameterBool;
    using ACh = juce::AudioParameterChoice;
    using PID = juce::ParameterID;
    constexpr int VER = 1;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ---- PITCH ----
    params.push_back (std::make_unique<AF> (PID { "start_freq", VER }, "Pitch · Start",
        rangeSkewed (100.0f, 15000.0f, 1.0f, 0.30f), 8000.0f,
        juce::String(), AF::genericParameter, fmtHz));

    params.push_back (std::make_unique<AF> (PID { "mid_freq", VER }, "Pitch · Mid",
        rangeSkewed (50.0f, 1000.0f, 1.0f, 0.30f), 150.0f,
        juce::String(), AF::genericParameter, fmtHz));

    params.push_back (std::make_unique<AF> (PID { "end_freq", VER }, "Pitch · End",
        rangeSkewed (20.0f, 100.0f, 0.01f, 0.50f), 46.25f,
        juce::String(), AF::genericParameter, fmtHz));

    params.push_back (std::make_unique<AF> (PID { "sweep_time_1", VER }, "Pitch · Sweep 1",
        rangeSkewed (0.1f, 50.0f, 0.1f, 0.30f), 10.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "sweep_time_2", VER }, "Pitch · Sweep 2",
        rangeSkewed (1.0f, 250.0f, 0.5f, 0.30f), 80.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "pitch_curve", VER }, "Pitch · Curve",
        rangeSkewed (0.1f, 10.0f, 0.1f, 0.50f), 3.0f,
        juce::String(), AF::genericParameter, fmtRatio));

    // ---- AMP ----
    params.push_back (std::make_unique<AF> (PID { "vol_attack", VER }, "Amp · Attack",
        rangeSkewed (0.0f, 30.0f, 0.1f, 0.50f), 2.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "vol_hold", VER }, "Amp · Hold",
        rangeSkewed (0.0f, 50.0f, 0.1f, 0.50f), 10.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "vol_decay_1", VER }, "Amp · Decay 1",
        rangeSkewed (0.0f, 150.0f, 0.5f, 0.50f), 50.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "vol_sustain", VER }, "Amp · Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
        juce::String(), AF::genericParameter, fmtPct));

    params.push_back (std::make_unique<AF> (PID { "vol_decay_2", VER }, "Amp · Decay 2",
        rangeSkewed (0.0f, 700.0f, 1.0f, 0.50f), 150.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "vol_curve", VER }, "Amp · Curve",
        rangeSkewed (0.1f, 10.0f, 0.1f, 0.50f), 3.0f,
        juce::String(), AF::genericParameter, fmtRatio));

    // ---- SCOOP ----
    params.push_back (std::make_unique<AF> (PID { "scoop_start", VER }, "Scoop · Start",
        rangeSkewed (0.0f, 50.0f, 0.1f, 0.50f), 10.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "scoop_length", VER }, "Scoop · Length",
        rangeSkewed (1.0f, 100.0f, 0.5f, 0.50f), 30.0f,
        juce::String(), AF::genericParameter, fmtMs));

    params.push_back (std::make_unique<AF> (PID { "scoop_depth", VER }, "Scoop · Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        juce::String(), AF::genericParameter, fmtPct));

    // ---- TRANSIENT ----
    params.push_back (std::make_unique<AF> (PID { "click_vol", VER }, "Transient · Vol",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
        juce::String(), AF::genericParameter,
        [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }));

    params.push_back (std::make_unique<ACh> (PID { "click_type", VER }, "Transient · Source",
        juce::StringArray { "Sine", "Noise", "Both" }, 0));

    params.push_back (std::make_unique<AF> (PID { "click_hpf", VER }, "Transient · HPF",
        rangeSkewed (200.0f, 4000.0f, 1.0f, 0.30f), 800.0f,
        juce::String(), AF::genericParameter, fmtHz));

    params.push_back (std::make_unique<AF> (PID { "click_tone", VER }, "Transient · Tone",
        rangeSkewed (2000.0f, 16000.0f, 1.0f, 0.30f), 10000.0f,
        juce::String(), AF::genericParameter, fmtHz));

    params.push_back (std::make_unique<AF> (PID { "click_decay", VER }, "Transient · Decay",
        rangeSkewed (1.0f, 30.0f, 0.1f, 0.50f), 5.0f,
        juce::String(), AF::genericParameter, fmtMs));

    // ---- DRIVE ----
    params.push_back (std::make_unique<AF> (PID { "drive", VER }, "Drive · Base",
        rangeSkewed (1.0f, 10.0f, 0.1f, 0.50f), 1.5f,
        juce::String(), AF::genericParameter, fmtRatio));

    params.push_back (std::make_unique<AF> (PID { "tail_drive", VER }, "Drive · Tail",
        rangeSkewed (0.0f, 10.0f, 0.1f, 0.50f), 0.0f,
        juce::String(), AF::genericParameter, fmtRatio));

    // ---- MASTER ----
    params.push_back (std::make_unique<AB> (PID { "invert_phase", VER }, "Master · Invert Phase", false));

    params.push_back (std::make_unique<AF> (PID { "output_gain", VER }, "Master · Output",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        juce::String(), AF::genericParameter, fmtDb));

    params.push_back (std::make_unique<AF> (PID { "pitch_track", VER }, "Master · Pitch Track",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        juce::String(), AF::genericParameter, fmtPct));

    return { params.begin(), params.end() };
}

//==============================================================================
KickAssProcessor::KickAssProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    cacheParamPointers();
}

void KickAssProcessor::cacheParamPointers()
{
    pStartFreq   = apvts.getRawParameterValue ("start_freq");
    pMidFreq     = apvts.getRawParameterValue ("mid_freq");
    pEndFreq     = apvts.getRawParameterValue ("end_freq");
    pSweepTime1  = apvts.getRawParameterValue ("sweep_time_1");
    pSweepTime2  = apvts.getRawParameterValue ("sweep_time_2");
    pPitchCurve  = apvts.getRawParameterValue ("pitch_curve");
    pVolAttack   = apvts.getRawParameterValue ("vol_attack");
    pVolHold     = apvts.getRawParameterValue ("vol_hold");
    pVolDecay1   = apvts.getRawParameterValue ("vol_decay_1");
    pVolSustain  = apvts.getRawParameterValue ("vol_sustain");
    pVolDecay2   = apvts.getRawParameterValue ("vol_decay_2");
    pVolCurve    = apvts.getRawParameterValue ("vol_curve");
    pScoopStart  = apvts.getRawParameterValue ("scoop_start");
    pScoopLength = apvts.getRawParameterValue ("scoop_length");
    pScoopDepth  = apvts.getRawParameterValue ("scoop_depth");
    pClickVol    = apvts.getRawParameterValue ("click_vol");
    pClickType   = apvts.getRawParameterValue ("click_type");
    pClickHpf    = apvts.getRawParameterValue ("click_hpf");
    pClickTone   = apvts.getRawParameterValue ("click_tone");
    pClickDecay  = apvts.getRawParameterValue ("click_decay");
    pDrive       = apvts.getRawParameterValue ("drive");
    pTailDrive   = apvts.getRawParameterValue ("tail_drive");
    pInvertPhase = apvts.getRawParameterValue ("invert_phase");
    pOutputGain  = apvts.getRawParameterValue ("output_gain");
    pPitchTrack  = apvts.getRawParameterValue ("pitch_track");
}

void KickAssProcessor::readParamsIntoEngine()
{
    KickParams p;
    p.startFreq    = pStartFreq->load();
    p.midFreq      = pMidFreq->load();
    p.endFreq      = pEndFreq->load();
    p.sweepTime1Ms = pSweepTime1->load();
    p.sweepTime2Ms = pSweepTime2->load();
    p.pitchCurve   = pPitchCurve->load();
    p.volAttackMs  = pVolAttack->load();
    p.volHoldMs    = pVolHold->load();
    p.volDecay1Ms  = pVolDecay1->load();
    p.volSustain   = pVolSustain->load() * 0.01f;   // % → 0..1
    p.volDecay2Ms  = pVolDecay2->load();
    p.volCurve     = pVolCurve->load();
    p.scoopStartMs  = pScoopStart->load();
    p.scoopLengthMs = pScoopLength->load();
    p.scoopDepth    = pScoopDepth->load() * 0.01f;  // % → 0..1
    p.clickVol     = pClickVol->load();
    p.clickType    = (int) pClickType->load();
    p.clickHpfHz   = pClickHpf->load();
    p.clickToneHz  = pClickTone->load();
    p.clickDecayMs = pClickDecay->load();
    p.drive        = pDrive->load();
    p.tailDrive    = pTailDrive->load();
    p.invertPhase  = pInvertPhase->load() > 0.5f;
    p.outputGainDb = pOutputGain->load();
    p.pitchTrack   = pPitchTrack->load() * 0.01f;   // % → 0..1
    engine.setParams (p);
}

//==============================================================================
void KickAssProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    // Offline engine runs at a fixed 48k for stable visualizer output across DAW SR changes.
    offlineEngine.prepare (48000.0, 4096);
    readParamsIntoEngine();
    // Report oversampler latency to the host for PDC compensation.
    setLatencySamples (engine.getLatencySamples());
}

void KickAssProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();
    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Clear all output channels — synth replaces, doesn't add.
    buffer.clear();

    readParamsIntoEngine();

    // Sample-accurate MIDI handling
    for (const auto meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.triggerNote (msg.getNoteNumber(),
                                msg.getFloatVelocity(),
                                meta.samplePosition);
    }

    engine.renderBlock (buffer, 0, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* KickAssProcessor::createEditor()
{
    return new KickAssEditor (*this);
}

//==============================================================================
void KickAssProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KickAssProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
void KickAssProcessor::offlineRender (juce::AudioBuffer<float>& buffer, double durationMs)
{
    // UI-thread render for the visualizer. Uses a DEDICATED offline engine so realtime
    // state (phase, env, oversampler) is never touched by the UI.
    KickParams p;
    p.startFreq    = pStartFreq->load();
    p.midFreq      = pMidFreq->load();
    p.endFreq      = pEndFreq->load();
    p.sweepTime1Ms = pSweepTime1->load();
    p.sweepTime2Ms = pSweepTime2->load();
    p.pitchCurve   = pPitchCurve->load();
    p.volAttackMs  = pVolAttack->load();
    p.volHoldMs    = pVolHold->load();
    p.volDecay1Ms  = pVolDecay1->load();
    p.volSustain   = pVolSustain->load() * 0.01f;
    p.volDecay2Ms  = pVolDecay2->load();
    p.volCurve     = pVolCurve->load();
    p.scoopStartMs  = pScoopStart->load();
    p.scoopLengthMs = pScoopLength->load();
    p.scoopDepth    = pScoopDepth->load() * 0.01f;
    p.clickVol     = pClickVol->load();
    p.clickType    = (int) pClickType->load();
    p.clickHpfHz   = pClickHpf->load();
    p.clickToneHz  = pClickTone->load();
    p.clickDecayMs = pClickDecay->load();
    p.drive        = pDrive->load();
    p.tailDrive    = pTailDrive->load();
    p.invertPhase  = pInvertPhase->load() > 0.5f;
    p.outputGainDb = pOutputGain->load();
    p.pitchTrack   = 0.0f;   // visualizer always renders at note 60 (no transpose) for stable view

    offlineEngine.setParams (p);
    offlineEngine.renderOffline (buffer, 48000.0, durationMs);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickAssProcessor();
}
