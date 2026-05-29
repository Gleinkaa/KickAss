#include "PluginProcessor.h"
#ifndef KICKASS_HEADLESS_TESTS
 #include "PluginEditor.h"
#endif
#include "PresetManager.h"

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

    // JUCE 8: AudioParameterFloat's positional (label, category, formatter) ctor is
    // deprecated. This helper builds an Attributes object so the layout below stays
    // readable.
    juce::AudioParameterFloatAttributes attrs (juce::String (*formatter) (float, int))
    {
        return juce::AudioParameterFloatAttributes()
            .withCategory (juce::AudioProcessorParameter::genericParameter)
            .withStringFromValueFunction (formatter);
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
        attrs (fmtHz)));

    params.push_back (std::make_unique<AF> (PID { "mid_freq", VER }, "Pitch · Mid",
        rangeSkewed (50.0f, 1000.0f, 1.0f, 0.30f), 150.0f,
        attrs (fmtHz)));

    params.push_back (std::make_unique<AF> (PID { "end_freq", VER }, "Pitch · End",
        rangeSkewed (20.0f, 100.0f, 0.01f, 0.50f), 46.25f,
        attrs (fmtHz)));

    params.push_back (std::make_unique<AF> (PID { "sweep_time_1", VER }, "Pitch · Sweep 1",
        rangeSkewed (0.1f, 50.0f, 0.1f, 0.30f), 10.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "sweep_time_2", VER }, "Pitch · Sweep 2",
        rangeSkewed (1.0f, 250.0f, 0.5f, 0.30f), 80.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "pitch_curve", VER }, "Pitch · Curve",
        rangeSkewed (0.1f, 10.0f, 0.1f, 0.50f), 3.0f,
        attrs (fmtRatio)));

    // ---- AMP ----
    params.push_back (std::make_unique<AF> (PID { "vol_attack", VER }, "Amp · Attack",
        rangeSkewed (0.0f, 30.0f, 0.1f, 0.50f), 2.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "vol_hold", VER }, "Amp · Hold",
        rangeSkewed (0.0f, 50.0f, 0.1f, 0.50f), 10.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "vol_decay_1", VER }, "Amp · Decay 1",
        rangeSkewed (0.0f, 150.0f, 0.5f, 0.50f), 50.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "vol_sustain", VER }, "Amp · Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
        attrs (fmtPct)));

    params.push_back (std::make_unique<AF> (PID { "vol_decay_2", VER }, "Amp · Decay 2",
        rangeSkewed (0.0f, 700.0f, 1.0f, 0.50f), 150.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "vol_curve", VER }, "Amp · Curve",
        rangeSkewed (0.1f, 10.0f, 0.1f, 0.50f), 3.0f,
        attrs (fmtRatio)));

    // ---- SCOOP ----
    params.push_back (std::make_unique<AF> (PID { "scoop_start", VER }, "Scoop · Start",
        rangeSkewed (0.0f, 50.0f, 0.1f, 0.50f), 10.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "scoop_length", VER }, "Scoop · Length",
        rangeSkewed (1.0f, 100.0f, 0.5f, 0.50f), 30.0f,
        attrs (fmtMs)));

    params.push_back (std::make_unique<AF> (PID { "scoop_depth", VER }, "Scoop · Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        attrs (fmtPct)));

    // ---- TRANSIENT ----
    params.push_back (std::make_unique<AF> (PID { "click_vol", VER }, "Transient · Vol",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withCategory (AF::genericParameter)
            .withStringFromValueFunction (
                [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    params.push_back (std::make_unique<ACh> (PID { "click_type", VER }, "Transient · Source",
        juce::StringArray { "Sine", "Noise", "Both" }, 0));

    params.push_back (std::make_unique<AF> (PID { "click_hpf", VER }, "Transient · HPF",
        rangeSkewed (200.0f, 4000.0f, 1.0f, 0.30f), 800.0f,
        attrs (fmtHz)));

    params.push_back (std::make_unique<AF> (PID { "click_tone", VER }, "Transient · Tone",
        rangeSkewed (2000.0f, 16000.0f, 1.0f, 0.30f), 10000.0f,
        attrs (fmtHz)));

    params.push_back (std::make_unique<AF> (PID { "click_decay", VER }, "Transient · Decay",
        rangeSkewed (1.0f, 30.0f, 0.1f, 0.50f), 5.0f,
        attrs (fmtMs)));

    // ---- DRIVE ----
    params.push_back (std::make_unique<AF> (PID { "drive", VER }, "Drive · Base",
        rangeSkewed (1.0f, 10.0f, 0.1f, 0.50f), 1.5f,
        attrs (fmtRatio)));

    params.push_back (std::make_unique<AF> (PID { "tail_drive", VER }, "Drive · Tail",
        rangeSkewed (0.0f, 10.0f, 0.1f, 0.50f), 0.0f,
        attrs (fmtRatio)));

    // Saturation flavour. Default Tanh (index 0) = the v1.0 character, so existing
    // presets render bit-for-bit identically until the user picks another mode.
    params.push_back (std::make_unique<ACh> (PID { "sat_type", VER }, "Drive · Type",
        juce::StringArray { "Tanh", "Soft Clip", "Hard Clip", "Tube", "Foldback" }, 0));

    // ---- MASTER ----
    params.push_back (std::make_unique<AB> (PID { "invert_phase", VER }, "Master · Invert Phase", false));

    params.push_back (std::make_unique<AF> (PID { "output_gain", VER }, "Master · Output",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        attrs (fmtDb)));

    params.push_back (std::make_unique<AF> (PID { "pitch_track", VER }, "Master · Pitch Track",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        attrs (fmtPct)));

    params.push_back (std::make_unique<AF> (PID { "phase_offset", VER }, "Master · Phase Offset",
        juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withCategory (AF::genericParameter)
            .withStringFromValueFunction (
                [] (float v, int) { return juce::String (juce::roundToInt (v)) + " °"; })));

    // ---- GLOBAL ENVELOPE MODE (Phase 6b) ----
    // 0 = Simple (AHDSR knobs drive DSP), 1 = Advanced (custom breakpoint curve drives DSP).
    // Default Advanced per user choice — breakpoint editor visible immediately.
    params.push_back (std::make_unique<ACh> (PID { "envelope_mode", VER }, "Envelope · Mode",
        juce::StringArray { "Simple", "Advanced" }, 1));

    return { params.begin(), params.end() };
}

//==============================================================================
KickAssProcessor::KickAssProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    cacheParamPointers();
    presetManager = std::make_unique<PresetManager> (*this);

    // Phase 6b: seed the breakpoint curve from the AHDSR defaults and persist it
    // in apvts.state under <Curves>/<Curve id="vol_env">. Editor can mutate
    // volEnvCurve in place and call syncVolCurveToValueTree() to persist.
    rebuildVolCurveFromAhdsr();
    syncVolCurveToValueTree();
    setVolCurveMode ("ahdsr");      // default: cached from AHDSR, refreshable on first flip
    publishVolCurveToEngines();
}

KickAssProcessor::~KickAssProcessor() = default;

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
    pSatType     = apvts.getRawParameterValue ("sat_type");
    pInvertPhase = apvts.getRawParameterValue ("invert_phase");
    pOutputGain   = apvts.getRawParameterValue ("output_gain");
    pPitchTrack   = apvts.getRawParameterValue ("pitch_track");
    pPhaseOffset  = apvts.getRawParameterValue ("phase_offset");
    pEnvelopeMode = apvts.getRawParameterValue ("envelope_mode");
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
    p.saturationType = (int) pSatType->load();
    p.invertPhase  = pInvertPhase->load() > 0.5f;
    p.outputGainDb = pOutputGain->load();
    p.pitchTrack   = pPitchTrack->load() * 0.01f;   // % → 0..1
    p.phaseOffset  = pPhaseOffset->load() * (1.0f / 360.0f);   // degrees → 0..1
    engine.setParams (p);
    // Mirror the global envelope-mode switch each block — cheap atomic store.
    engine.setCurveMode (isEnvelopeAdvanced());
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

//==============================================================================
void KickAssProcessor::requestTrigger (int midiNote, float velocity) noexcept
{
    pendingVelocity.store (velocity, std::memory_order_relaxed);
    pendingNote.store (midiNote, std::memory_order_release);   // release pairs with acquire in processBlock
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

    // Drain UI→audio trigger queue (lock-free single-slot).
    const int note = pendingNote.exchange (-1, std::memory_order_acquire);
    if (note >= 0)
        engine.triggerNote (note, pendingVelocity.load (std::memory_order_relaxed), 0);

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
   #ifdef KICKASS_HEADLESS_TESTS
    return nullptr;   // headless test target — no GUI symbol needed
   #else
    return new KickAssEditor (*this);
   #endif
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
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            restoreVolCurveFromStateOrAhdsr();
        }
    }
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
    p.saturationType = (int) pSatType->load();
    p.invertPhase  = pInvertPhase->load() > 0.5f;
    p.outputGainDb = pOutputGain->load();
    p.pitchTrack   = 0.0f;   // visualizer always renders at note 60 (no transpose) for stable view
    p.phaseOffset  = pPhaseOffset->load() * (1.0f / 360.0f);   // degrees → 0..1

    offlineEngine.setParams (p);
    offlineEngine.setCurveMode (isEnvelopeAdvanced());
    offlineEngine.renderOffline (buffer, 48000.0, durationMs);
}

//==============================================================================
// Phase 6b — breakpoint envelope storage helpers
//==============================================================================
void KickAssProcessor::rebuildVolCurveFromAhdsr()
{
    const float att = pVolAttack  ? pVolAttack ->load() : 2.0f;
    const float hld = pVolHold    ? pVolHold   ->load() : 10.0f;
    const float d1  = pVolDecay1  ? pVolDecay1 ->load() : 50.0f;
    const float sus = (pVolSustain ? pVolSustain->load() : 40.0f) * 0.01f;
    const float d2  = pVolDecay2  ? pVolDecay2 ->load() : 150.0f;
    const float crv = pVolCurve   ? pVolCurve  ->load() : 3.0f;

    juce::SpinLock::ScopedLockType l (volCurveLock);
    volEnvCurve = EnvCurve::fromAhdsr (att, hld, d1, sus, d2, crv);
    volEnvCurve.clampToInvariants();
}

void KickAssProcessor::syncVolCurveToValueTree()
{
    auto curves = apvts.state.getChildWithName ("Curves");
    if (! curves.isValid())
    {
        curves = juce::ValueTree ("Curves");
        apvts.state.appendChild (curves, nullptr);
    }
    // Find or create <Curve id="vol_env">
    juce::ValueTree volCurveVt;
    for (int i = 0; i < curves.getNumChildren(); ++i)
    {
        auto c = curves.getChild (i);
        if (c.getType().toString() == "Curve"
            && c.getProperty ("id").toString() == "vol_env")
        {
            volCurveVt = c;
            break;
        }
    }
    if (! volCurveVt.isValid())
    {
        volCurveVt = juce::ValueTree ("Curve");
        curves.appendChild (volCurveVt, nullptr);
    }

    {
        juce::SpinLock::ScopedLockType l (volCurveLock);
        volEnvCurve.toValueTree (volCurveVt, "vol_env");
    }
    // Auto-publish so DSP follows what the editor just persisted.
    publishVolCurveToEngines();
}

bool KickAssProcessor::isEnvelopeAdvanced() const noexcept
{
    return pEnvelopeMode && pEnvelopeMode->load() > 0.5f;
}

void KickAssProcessor::publishVolCurveToEngines()
{
    juce::SpinLock::ScopedLockType l (volCurveLock);
    engine       .publishVolCurve (volEnvCurve);
    offlineEngine.publishVolCurve (volEnvCurve);
}

void KickAssProcessor::restoreVolCurveFromStateOrAhdsr()
{
    auto curves = apvts.state.getChildWithName ("Curves");
    if (curves.isValid())
    {
        for (int i = 0; i < curves.getNumChildren(); ++i)
        {
            auto c = curves.getChild (i);
            if (c.getType().toString() == "Curve"
                && c.getProperty ("id").toString() == "vol_env")
            {
                {
                    juce::SpinLock::ScopedLockType l (volCurveLock);
                    volEnvCurve.fromValueTree (c);
                    volEnvCurve.clampToInvariants();
                }
                publishVolCurveToEngines();
                return;   // mode attribute (if any) preserved as-is
            }
        }
    }
    // No curve in state (old preset, or freshly-applied factory that only set knobs).
    rebuildVolCurveFromAhdsr();
    syncVolCurveToValueTree();   // publishes for us
    setVolCurveMode ("ahdsr");
}

//==============================================================================
juce::String KickAssProcessor::getVolCurveMode() const
{
    auto curves = apvts.state.getChildWithName ("Curves");
    if (! curves.isValid()) return "ahdsr";
    for (int i = 0; i < curves.getNumChildren(); ++i)
    {
        auto c = curves.getChild (i);
        if (c.getType().toString() == "Curve"
            && c.getProperty ("id").toString() == "vol_env")
            return c.getProperty ("mode", "ahdsr").toString();
    }
    return "ahdsr";
}

void KickAssProcessor::setVolCurveMode (const juce::String& mode)
{
    auto curves = apvts.state.getChildWithName ("Curves");
    if (! curves.isValid()) return;
    for (int i = 0; i < curves.getNumChildren(); ++i)
    {
        auto c = curves.getChild (i);
        if (c.getType().toString() == "Curve"
            && c.getProperty ("id").toString() == "vol_env")
        {
            c.setProperty ("mode", mode, nullptr);
            return;
        }
    }
}

void KickAssProcessor::onEnterAdvancedMode()
{
    // Only refresh from AHDSR if the curve has never been customized by the user.
    if (getVolCurveMode() == "custom") return;

    rebuildVolCurveFromAhdsr();
    syncVolCurveToValueTree();      // publishes for us
    setVolCurveMode ("ahdsr");      // toValueTree preserves attributes, but be explicit
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickAssProcessor();
}
