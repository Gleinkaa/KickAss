#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#ifndef KICKASS_HEADLESS_TESTS
 #include "PluginEditor.h"
#endif
#include "PresetManager.h"
#include "CrashLog.h"

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

    // "Sample" appended at index 3 so existing presets/indices (Sine/Noise/Both) are unchanged.
    params.push_back (std::make_unique<ACh> (PID { "click_type", VER }, "Transient · Source",
        juce::StringArray { "Sine", "Noise", "Both", "Sample" }, 0));

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

    params.push_back (std::make_unique<AB> (PID { "safety_limit", VER }, "Master · Safety", true));

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
      apvts (*this, &undoManager, "Parameters", createParameterLayout())
{
    cacheParamPointers();
    presetManager = std::make_unique<PresetManager> (*this);

    // v1.1 — crash logging. Install only for the Standalone wrapper: in a plugin
    // we'd be overwriting the host's crash handler (rude, and the host's is
    // better placed to capture context). Standalone is our own process, so it's
    // fair game. Plugin-host crash capture is deferred (documented in STATUS).
    if (wrapperType == wrapperType_Standalone)
        kickass::crashlog::install();

    // Phase 6b: seed the breakpoint curve from the AHDSR defaults and persist it
    // in apvts.state under <Curves>/<Curve id="vol_env">. Editor can mutate
    // volEnvCurve in place and call syncVolCurveToValueTree() to persist.
    rebuildVolCurveFromAhdsr();
    syncVolCurveToValueTree();
    setVolCurveMode ("ahdsr");      // default: cached from AHDSR, refreshable on first flip
    publishVolCurveToEngines();
}

KickAssProcessor::~KickAssProcessor() = default;

//==============================================================================
// Undo/Redo — delegate to the UndoManager that apvts now owns.
// Callers in the editor refresh the breakpoint curve after undo/redo because the
// curve is stored in apvts.state but NOT as an APVTS parameter, so the
// UndoManager only restores parameter values, not the <Curves> subtree.
void KickAssProcessor::undo()           { undoManager.undo(); }
void KickAssProcessor::redo()           { undoManager.redo(); }
bool KickAssProcessor::canUndo() const  { return undoManager.canUndo(); }
bool KickAssProcessor::canRedo() const  { return undoManager.canRedo(); }

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
    pSafetyLimit = apvts.getRawParameterValue ("safety_limit");
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
    p.safetyLimit  = pSafetyLimit->load() > 0.5f;
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
    // R1: read the CURRENT parameter values BEFORE prepare(). prepare() ends
    // in reset(), which snaps the output-gain smoother to params.outputGainDb
    // — with the old order reset() snapped to whatever the engine held from
    // before (constructor defaults on first call, previous values on a
    // re-prepare), and the first ~10 ms of the next kick ramped from it.
    // readParamsIntoEngine() only loads cached APVTS atomics (bound in the
    // constructor) and copies them into the engine — it does not depend on
    // prepare() having run.
    readParamsIntoEngine();
    engine.prepare (sampleRate, samplesPerBlock);
    // NOTE (A10): do NOT prepare offlineEngine here. prepareToPlay may be called
    // by the host on a background thread while the message thread is rendering
    // with offlineEngine — preparing here would race the visualizer path.
    // renderOffline() self-prepares on first use and on any sample-rate change,
    // so the offline engine is only ever touched from the message thread.
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

    const int numSamples = buffer.getNumSamples();

    // Drain UI→audio trigger queue (lock-free single-slot). This trigger is a
    // UI "preview" request with no sample position, so it fires at block start
    // (offset 0) by design.
    const int note = pendingNote.exchange (-1, std::memory_order_acquire);
    if (note >= 0)
        engine.triggerNote (note, pendingVelocity.load (std::memory_order_relaxed), 0);

    // A2 fix — sample-accurate MIDI note-ons. The old code triggered every
    // note-on at the block start, quantizing onsets to the buffer size (up to
    // ~10 ms jitter at typical settings). Now the block is rendered in
    // segments between note-on events: render [pos, evPos), trigger exactly at
    // evPos (the voice starts on the next rendered sample), then continue.
    // applyPostStages derives the drive ramp from sampleSinceTrigger per
    // sub-block, so sub-ranges are safe, and renderBlock early-returns while
    // the voice is inactive, making pre-trigger segments free.
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (! msg.isNoteOn())
            continue;

        const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (evPos > pos)
        {
            engine.renderBlock (buffer, pos, evPos - pos);
            pos = evPos;
        }
        engine.triggerNote (msg.getNoteNumber(), msg.getFloatVelocity(), 0);
    }
    engine.renderBlock (buffer, pos, numSamples - pos);
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
            auto stateTree = juce::ValueTree::fromXml (*xml);
            removeNonFiniteParameterChildren (stateTree);   // B9: NaN/Inf PARAM values
            apvts.replaceState (stateTree);
            restoreVolCurveFromStateOrAhdsr();
            restoreTransientSampleFromState();              // B3/B4: swap the sample layer too
        }
    }
}

//==============================================================================
void KickAssProcessor::offlineRender (juce::AudioBuffer<float>& buffer, double durationMs)
{
    offlineRenderImpl (buffer, durationMs, /*soloTransient*/ false);
}

void KickAssProcessor::offlineRenderTransient (juce::AudioBuffer<float>& buffer, double durationMs)
{
    offlineRenderImpl (buffer, durationMs, /*soloTransient*/ true);
}

void KickAssProcessor::offlineRenderImpl (juce::AudioBuffer<float>& buffer, double durationMs, bool soloTransient)
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
    p.safetyLimit  = pSafetyLimit->load() > 0.5f;
    p.outputGainDb = pOutputGain->load();
    p.pitchTrack   = 0.0f;   // visualizer always renders at note 60 (no transpose) for stable view
    p.phaseOffset  = pPhaseOffset->load() * (1.0f / 360.0f);   // degrees → 0..1
    p.soloTransient = soloTransient;

    offlineEngine.setParams (p);
    offlineEngine.setCurveMode (isEnvelopeAdvanced());
    offlineEngine.renderOffline (buffer, 48000.0, durationMs);
}

bool KickAssProcessor::renderToWavFile (const juce::File& dest, double durationMs)
{
    // 1. Render mono at offlineRender's fixed 48 kHz.
    constexpr double sr = 48000.0;
    juce::AudioBuffer<float> mono;
    offlineRender (mono, durationMs);
    if (mono.getNumSamples() <= 0)
        return false;

    // 2. Promote to stereo for broad DAW compatibility.
    juce::AudioBuffer<float> stereo (2, mono.getNumSamples());
    stereo.copyFrom (0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());

    // 3. Write 24-bit PCM WAV (mirrors the EXPORT WAV path).
    dest.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (dest.createOutputStream());
    if (out == nullptr)
        return false;

    out->setPosition (0);
    if (auto* fos = dynamic_cast<juce::FileOutputStream*> (out.get()))
        fos->truncate();

    const auto opts = juce::AudioFormatWriterOptions()
                          .withSampleRate (sr)
                          .withNumChannels (2)
                          .withBitsPerSample (24);

    // JUCE 8 createWriterFor takes the unique_ptr by ref and moves out of it on success.
    if (auto writer = wav.createWriterFor (out, opts))
        return writer->writeFromAudioSampleBuffer (stereo, 0, stereo.getNumSamples());

    return false;
}

//==============================================================================
// v1.1 — drag-a-WAV transient layer (load / clear / persist)
//==============================================================================
bool KickAssProcessor::loadTransientSampleFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg (+ platform codecs)

    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int    numSamples = (int) reader->lengthInSamples;
    const int    numCh      = (int) reader->numChannels;
    const double srcRate    = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    if (numSamples <= 0 || numCh <= 0)
        return false;

    // A7 fix — hard cap + sanitize BEFORE publishing. A single NaN/Inf sample
    // (a corrupt or malicious float WAV) would otherwise poison the click
    // HPF/LPF, the oversampler and the DC blocker state PERMANENTLY: those are
    // recursive filters, so one bad value never rings out. Cap the read at
    // 10 s at the source rate (memory + DSP sanity), replace non-finite
    // samples with silence, and clamp survivors into a sane amplitude band.
    const int maxSamples = (int) std::ceil (10.0 * srcRate);
    const int readLen    = juce::jmin (numSamples, maxSamples);

    // Read into a multi-channel buffer, then sum to mono.
    juce::AudioBuffer<float> raw (numCh, readLen);
    if (! reader->read (&raw, 0, readLen, 0, true, true))
        return false;

    juce::AudioBuffer<float> mono (1, readLen);
    mono.clear();
    for (int ch = 0; ch < numCh; ++ch)
        mono.addFrom (0, 0, raw, ch, 0, readLen);
    if (numCh > 1)
        mono.applyGain (1.0f / (float) numCh);   // average so a stereo file isn't 2x louder

    {
        float* d = mono.getWritePointer (0);
        for (int i = 0; i < readLen; ++i)
        {
            float v = d[i];
            if (! std::isfinite (v))           // NaN / ±Inf → silence
                v = 0.0f;
            d[i] = juce::jlimit (-4.0f, 4.0f, v);   // clamp absurd-but-finite spikes
        }
    }

    // Publish to both engines (lock-free flip; the copy happens inside publish).
    engine       .publishTransientSample (mono, srcRate);
    offlineEngine.publishTransientSample (mono, srcRate);

    // Persist the PATH in apvts.state under <Sample id="transient" path="...">.
    auto sampleNode = apvts.state.getChildWithName ("Sample");
    if (! sampleNode.isValid())
    {
        sampleNode = juce::ValueTree ("Sample");
        apvts.state.appendChild (sampleNode, nullptr);
    }
    sampleNode.setProperty ("id",   "transient",               nullptr);
    sampleNode.setProperty ("path", file.getFullPathName(),    nullptr);
    return true;
}

void KickAssProcessor::clearTransientSample()
{
    engine       .clearTransientSample();
    offlineEngine.clearTransientSample();

    auto sampleNode = apvts.state.getChildWithName ("Sample");
    if (sampleNode.isValid())
        apvts.state.removeChild (sampleNode, nullptr);
}

juce::String KickAssProcessor::getTransientSamplePath() const
{
    auto sampleNode = apvts.state.getChildWithName ("Sample");
    if (sampleNode.isValid())
        return sampleNode.getProperty ("path").toString();
    return {};
}

void KickAssProcessor::restoreTransientSampleFromState()
{
    const juce::String samplePath = getTransientSamplePath();
    if (samplePath.isNotEmpty())
    {
        juce::File f (samplePath);
        // loadTransientSampleFile re-publishes to BOTH engines and re-writes
        // the node's path property (same value) — that's fine here.
        if (f.existsAsFile() && loadTransientSampleFile (f))
            return;
    }

    // No <Sample> node, empty path, or a missing/unreadable file: clear ONLY
    // the engines (B3 — a state swap must not leave the previous patch's
    // sample playing) and keep the <Sample> node itself (B4 — a project opened
    // on a machine where the file is absent must not destroy the path; the
    // node survives the next save and resolves again where the file exists).
    engine       .clearTransientSample();
    offlineEngine.clearTransientSample();
}

void KickAssProcessor::removeNonFiniteParameterChildren (juce::ValueTree& stateTree)
{
    for (int i = stateTree.getNumChildren(); --i >= 0;)
    {
        const auto child = stateTree.getChild (i);
        if (! child.hasType ("PARAM"))
            continue;

        // var → double covers every storage type: a String attribute such as
        // "nan" or "inf" goes through JUCE's readDoubleValue (which parses
        // those to NaN / ±Inf), int/bool/double vars convert natively, and a
        // missing property is the void var (converts to 0 — finite).
        if (! std::isfinite ((double) child.getProperty ("value")))
            stateTree.removeChild (i, nullptr);
    }
}

//==============================================================================
// B7 — undo-preserving state swap (A/B compare).
//
// AudioProcessorValueTreeState::replaceState() calls undoManager->clearUndoHistory(),
// so the A/B toggle used to wipe the undo stack on every press. This applies a
// snapshot state to the live APVTS inside ONE UndoManager transaction instead:
// the swap itself is a single undo step, edits made before the toggle remain
// undoable, and undoing the swap also restores the <Curves>/<Sample> children
// (the pre-B7 replaceState path could never do that — it was all-or-nothing).
void KickAssProcessor::applyStatePreservingUndo (const juce::ValueTree& newState)
{
    // Message-thread only: pushes parameter values to the host and mutates
    // apvts.state (the editor's doToggleAB is the only caller).
    jassert (juce::MessageManager::existsAndIsCurrentThread());

    // Flush any in-flight parameter edits into their OWN transaction first (the
    // GUI normally relies on APVTS's 10 Hz flush timer). This also syncs
    // apvts.state's PARAM "value" properties with the live parameters, so the
    // undo actions recorded below capture the true pre-swap values.
    (void) apvts.copyState();

    // ONE undo transaction for the whole swap. beginNewTransaction() only
    // marks the boundary — the first recorded mutation after this line opens
    // the named transaction and everything else in this function joins it,
    // exactly like the per-gesture brackets the editor uses around knob
    // drags (see the click_type path) and undo() itself.
    undoManager.beginNewTransaction ("A/B swap");

    // ---- 1) Parameters ------------------------------------------------------
    // Push each value out of newState into the live parameter. The <PARAM>
    // children of a copyState() snapshot hold the DENORMALISED value in their
    // "value" property (verified against copyState() output: JUCE 8's
    // ParameterAdapter::flushToTree writes unnormalisedValue, and setNewState
    // reads it back as denormalised). A parameter with no child in newState
    // goes to its default. apvts.state's own PARAM children are left alone —
    // APVTS owns them and flushes the new values in itself (below).
    for (auto* param : apvts.processor.getParameters())
    {
        auto* rap = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (rap == nullptr)
            continue;

        const float defaultDenormalised = rap->convertFrom0to1 (rap->getDefaultValue());

        juce::ValueTree paramChild;   // the <PARAM id="..."> child in newState
        for (int i = 0; i < newState.getNumChildren(); ++i)
        {
            const auto child = newState.getChild (i);
            if (child.hasType ("PARAM")
                && child.getProperty ("id").toString() == rap->paramID)
            {
                paramChild = child;
                break;
            }
        }

        const float denormalised = paramChild.isValid()
            ? (float) paramChild.getProperty ("value", defaultDenormalised)
            : defaultDenormalised;

        rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, rap->convertTo0to1 (denormalised)));
    }

    // copyState() flushes the new parameter values into apvts.state's PARAM
    // children with the undo manager attached, recording them inside the open
    // "A/B swap" transaction (headless callers have no 10 Hz timer to do it
    // later). Without this the swap would not be on the undo stack yet.
    (void) apvts.copyState();

    // ---- 2) Non-PARAM children the A/B slots swap: <Curves> and <Sample> ----
    // Remove the live child (recorded), then append a COPY of the target's
    // child (recorded) — a copy, never the slot's own tree, so later edits of
    // apvts.state cannot alias a stored slot. The parameter children are not
    // touched — they are owned by APVTS.
    for (const auto& tag : { juce::Identifier ("Curves"), juce::Identifier ("Sample") })
    {
        const auto existing = apvts.state.getChildWithName (tag);
        if (existing.isValid())
            apvts.state.removeChild (existing, &undoManager);

        const auto incoming = newState.getChildWithName (tag);
        if (incoming.isValid())
            apvts.state.appendChild (incoming.createCopy(), &undoManager);
    }

    // ---- 3) Make the engines match the swapped state (same pair of calls as
    // setStateInformation; neither records anything onto the undo stack).
    restoreVolCurveFromStateOrAhdsr();
    restoreTransientSampleFromState();
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
                // B10: a curve with fewer than 2 usable <Pt> children is
                // corrupt or a placeholder. clampToInvariants() would
                // silently turn it into a flat-zero 2-point curve →
                // permanent silence in Advanced mode. Fall through to the
                // AHDSR rebuild instead.
                int validPts = 0;
                for (int k = 0; k < c.getNumChildren(); ++k)
                {
                    auto pt = c.getChild (k);
                    if (pt.hasType ("Pt")
                        && std::isfinite ((float) pt.getProperty ("t", 0.0f))
                        && std::isfinite ((float) pt.getProperty ("v", 0.0f)))
                        ++validPts;
                }

                if (validPts >= 2)
                {
                    {
                        juce::SpinLock::ScopedLockType l (volCurveLock);
                        volEnvCurve.fromValueTree (c);
                        volEnvCurve.clampToInvariants();
                    }
                    publishVolCurveToEngines();
                    return;   // mode attribute (if any) preserved as-is
                }

                break;   // degenerate vol_env node → AHDSR rebuild below
            }
        }
    }
    // No usable curve in state (old preset, degenerate node, or a freshly
    // applied factory that only set knobs).
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
