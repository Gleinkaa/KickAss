// DSP regression net — the safety harness that must exist BEFORE we touch the
// signal chain (saturation types, sub voice, etc.). Friend-Claude correctly
// flagged this gap: the curve round-trip suite is good, but nothing guarded the
// actual audio output. This file does:
//
//   1. Every factory preset renders NON-SILENT, FINITE audio.
//   2. Every factory preset survives a getState/setState round-trip (all params).
//   3. Parameter fuzz: random values across every parameter never produce
//      NaN / Inf / absurd peaks (catches divide-by-zero, 0^negative, etc.).
//   4. Hand-picked extreme combos (max drive, zero sweep times, zero decays).
//
// Headless console test. Built when KICKASS_BUILD_TESTS=ON. Run with:
//   cmake --build build --config Release --target KickAss_DspTests
//   build/KickAss_DspTests_artefacts/Release/KickAss_DspTests.exe
//
// Returns 0 on PASS, 1 on FAIL.

#include "../Source/PluginProcessor.h"
#include "../Source/PresetManager.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>

namespace
{
    int g_failures = 0;

    void reportFail (const char* expr, const char* file, int line, const juce::String& extra = {})
    {
        ++g_failures;
        std::cerr << "FAIL  " << file << ":" << line << "  " << expr;
        if (extra.isNotEmpty()) std::cerr << "  [" << extra.toRawUTF8() << "]";
        std::cerr << std::endl;
    }
}

#define REQUIRE(cond) do { if (! (cond)) reportFail (#cond, __FILE__, __LINE__); } while (0)
#define REQUIRE_MSG(cond, msg) do { if (! (cond)) reportFail (#cond, __FILE__, __LINE__, msg); } while (0)
#define REQUIRE_NEAR(a, b, tol) do { auto _a = (float) (a); auto _b = (float) (b); if (std::abs (_a - _b) > (tol)) \
    reportFail (#a " ~= " #b, __FILE__, __LINE__, juce::String (_a) + " vs " + juce::String (_b)); } while (0)

//==============================================================================
// Buffer analysis helpers
//==============================================================================
struct BufStats
{
    float peak  = 0.0f;
    double rms   = 0.0;
    bool  allFinite = true;
    int   numSamples = 0;
};

static BufStats analyse (const juce::AudioBuffer<float>& buf)
{
    BufStats s;
    s.numSamples = buf.getNumSamples();
    double sumSq = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer (ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float v = d[i];
            if (! std::isfinite (v)) s.allFinite = false;
            const float a = std::abs (v);
            if (a > s.peak) s.peak = a;
            sumSq += (double) v * (double) v;
        }
    }
    const int total = juce::jmax (1, buf.getNumChannels() * buf.getNumSamples());
    s.rms = std::sqrt (sumSq / (double) total);
    return s;
}

static juce::String runHeader (const char* name)
{
    std::cout << "---- " << name << " ----" << std::endl;
    return name;
}

//==============================================================================
// Test 1 — every factory preset produces non-silent, finite audio.
//==============================================================================
static void test_allFactoryPresets_produceNonSilentFiniteAudio()
{
    runHeader ("all factory presets render non-silent + finite audio");
    KickAssProcessor p;
    auto& pm = p.getPresetManager();
    const int n = pm.getFactoryCount();
    REQUIRE (n > 0);

    const auto names = pm.getFactoryNames();
    for (int i = 0; i < n; ++i)
    {
        const bool ok = pm.applyFactory (i);
        REQUIRE_MSG (ok, "applyFactory failed: " + names[i]);

        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 500.0);   // 500 ms render at the offline engine SR
        const auto s = analyse (buf);

        REQUIRE_MSG (s.allFinite, "non-finite sample in preset: " + names[i]);
        REQUIRE_MSG (s.peak  > 1.0e-4f, "preset is silent (peak): " + names[i]
                                        + " peak=" + juce::String (s.peak));
        REQUIRE_MSG (s.rms   > 1.0e-5,  "preset is silent (rms): " + names[i]
                                        + " rms=" + juce::String (s.rms));
        // Sanity ceiling — soft-clip stage means nothing should exceed ~+0 dBFS by much.
        REQUIRE_MSG (s.peak < 4.0f, "preset peak absurdly high: " + names[i]
                                    + " peak=" + juce::String (s.peak));
    }
}

//==============================================================================
// Test 2 — every factory preset survives a full getState/setState round-trip.
//==============================================================================
static void test_allFactoryPresets_stateRoundtrip()
{
    runHeader ("all factory presets survive getState/setState (every param)");
    KickAssProcessor src;
    auto& pm = src.getPresetManager();
    const int n = pm.getFactoryCount();
    const auto names = pm.getFactoryNames();

    for (int i = 0; i < n; ++i)
    {
        REQUIRE (pm.applyFactory (i));

        juce::MemoryBlock blob;
        src.getStateInformation (blob);
        REQUIRE_MSG (blob.getSize() > 0, "empty state blob for: " + names[i]);

        KickAssProcessor dst;
        dst.setStateInformation (blob.getData(), (int) blob.getSize());

        // Compare every parameter's normalized value.
        for (auto* srcParam : src.getParameters())
        {
            if (auto* sp = dynamic_cast<juce::RangedAudioParameter*> (srcParam))
            {
                auto* dp = dynamic_cast<juce::RangedAudioParameter*> (
                    dst.apvts.getParameter (sp->paramID));
                REQUIRE_MSG (dp != nullptr, "missing param in dst: " + sp->paramID);
                if (dp != nullptr)
                    REQUIRE_NEAR (sp->getValue(), dp->getValue(), 1.0e-4f);
            }
        }
    }
}

//==============================================================================
// Test 3 — parameter fuzz. Random values across the whole space, both envelope
// modes, must never yield NaN/Inf or absurd peaks.
//==============================================================================
static void test_paramFuzz_neverProducesNanOrInf()
{
    runHeader ("parameter fuzz — no NaN/Inf/absurd-peak across random param sets");
    KickAssProcessor p;
    juce::Random rng ((juce::int64) 0x1CCA55ED);

    auto* modeParam = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter ("envelope_mode"));

    constexpr int kIterations = 200;
    for (int iter = 0; iter < kIterations; ++iter)
    {
        // Randomize every parameter to a uniform point in its normalized range.
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (rng.nextFloat());

        // Alternate forced envelope modes so both DSP branches get exercised even
        // though the random draw above may rarely flip the choice.
        if (modeParam != nullptr)
            modeParam->setValueNotifyingHost ((iter % 2 == 0) ? 0.0f : 1.0f);

        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 300.0);
        const auto s = analyse (buf);

        REQUIRE_MSG (s.allFinite, "fuzz iter " + juce::String (iter) + " produced non-finite output");
        REQUIRE_MSG (s.peak < 16.0f, "fuzz iter " + juce::String (iter)
                                     + " peak absurd: " + juce::String (s.peak));
    }
}

//==============================================================================
// Test 4 — hand-picked degenerate parameter combos (the classic crash bait).
//==============================================================================
static void test_extremeParams_stayFinite()
{
    runHeader ("extreme/degenerate param combos stay finite");
    KickAssProcessor p;

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
        {
            const float norm = rap->getNormalisableRange().convertTo0to1 (plainValue);
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        }
    };

    struct Combo { const char* name; std::function<void()> apply; };
    std::vector<Combo> combos = {
        { "max drive + max tail", [&]{ setP ("drive", 10.0f); setP ("tail_drive", 10.0f); } },
        { "all sweep times min",  [&]{ setP ("sweep_time_1", 0.1f); setP ("sweep_time_2", 1.0f); } },
        { "all decays zero",      [&]{ setP ("vol_attack", 0.0f); setP ("vol_hold", 0.0f);
                                       setP ("vol_decay_1", 0.0f); setP ("vol_decay_2", 0.0f); } },
        { "scoop full depth",     [&]{ setP ("scoop_depth", 100.0f); setP ("scoop_length", 1.0f); } },
        { "click full + min decay", [&]{ setP ("click_vol", 1.0f); setP ("click_decay", 1.0f); } },
        { "curve exponents min",  [&]{ setP ("pitch_curve", 0.1f); setP ("vol_curve", 0.1f); } },
    };

    for (auto& c : combos)
    {
        c.apply();
        // Render in both envelope modes.
        for (float mode : { 0.0f, 1.0f })
        {
            setP ("envelope_mode", mode);
            juce::AudioBuffer<float> buf;
            p.offlineRender (buf, 400.0);
            const auto s = analyse (buf);
            REQUIRE_MSG (s.allFinite, juce::String (c.name) + " (mode " + juce::String (mode)
                                       + ") produced non-finite output");
            REQUIRE_MSG (s.peak < 16.0f, juce::String (c.name) + " peak absurd: "
                                          + juce::String (s.peak));
        }
    }
}

//==============================================================================
// Test 5 — saturation type selector: default Tanh, all 5 modes distinct + safe.
//==============================================================================
static void test_saturationTypes_distinctFiniteAndDefaultTanh()
{
    runHeader ("saturation types — default Tanh, 5 modes distinct + finite");
    KickAssProcessor p;

    auto* satParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("sat_type"));
    REQUIRE_MSG (satParam != nullptr, "sat_type parameter missing");
    if (satParam == nullptr) return;

    // Default must be Tanh (index 0) so v1.0 presets are unchanged.
    REQUIRE_MSG (satParam->getIndex() == 0, "default sat_type is not Tanh");

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // Push drive hard so the waveshaper actually engages and types diverge.
    setP ("drive", 6.0f);
    setP ("tail_drive", 4.0f);
    setP ("envelope_mode", 0.0f);   // Simple AHDSR — stable, deterministic body

    const int numTypes = satParam->choices.size();
    REQUIRE_MSG (numTypes == 5, "expected 5 saturation types, got " + juce::String (numTypes));

    std::vector<double> rmsPerType;
    std::vector<juce::AudioBuffer<float>> renders;

    for (int t = 0; t < numTypes; ++t)
    {
        satParam->setValueNotifyingHost (satParam->convertTo0to1 ((float) t));

        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 400.0);
        const auto s = analyse (buf);

        REQUIRE_MSG (s.allFinite, "sat type " + satParam->choices[t] + " non-finite");
        REQUIRE_MSG (s.peak > 1.0e-3f, "sat type " + satParam->choices[t] + " silent");
        REQUIRE_MSG (s.peak < 4.0f, "sat type " + satParam->choices[t]
                                     + " peak absurd: " + juce::String (s.peak));
        rmsPerType.push_back (s.rms);

        // Keep a copy for pairwise difference comparison.
        renders.emplace_back();
        renders.back().makeCopyOf (buf);
    }

    // Each type must differ audibly from Tanh (index 0). Compare mean-abs sample
    // difference; identical shapers would yield ~0.
    auto meanAbsDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        double acc = 0.0;
        const float* pa = a.getReadPointer (0);
        const float* pb = b.getReadPointer (0);
        for (int i = 0; i < n; ++i) acc += std::abs (pa[i] - pb[i]);
        return acc / juce::jmax (1, n);
    };

    for (int t = 1; t < numTypes; ++t)
    {
        const double d = meanAbsDiff (renders[0], renders[(size_t) t]);
        REQUIRE_MSG (d > 1.0e-4, "sat type " + satParam->choices[t]
                                  + " indistinguishable from Tanh (diff="
                                  + juce::String (d) + ")");
    }
}

//==============================================================================
// Test 6 — output safety limiter enforces the -0.1 dBFS ceiling AFTER output gain.
// Hot drive + +6 dB Output would push peaks over 0 dBFS without the final stage.
//==============================================================================
static void test_safetyLimiter_enforcesCeiling()
{
    runHeader ("safety limiter — enforces -0.1 dBFS ceiling after output gain");
    KickAssProcessor p;

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // Hot signal: max drive + max tail drive + +6 dB output, Simple AHDSR body.
    setP ("drive", 10.0f);
    setP ("tail_drive", 10.0f);
    setP ("output_gain", 6.0f);
    setP ("envelope_mode", 0.0f);

    // Ceiling = 10^(-0.1/20) ≈ 0.98855.
    const float ceiling = std::pow (10.0f, -0.1f * 0.05f);

    // --- Safety ON (default; set explicitly) ---
    setP ("safety_limit", 1.0f);
    juce::AudioBuffer<float> bufOn;
    p.offlineRender (bufOn, 400.0);
    const auto sOn = analyse (bufOn);
    REQUIRE_MSG (sOn.allFinite, "safety ON produced non-finite output");
    REQUIRE_MSG (sOn.peak <= ceiling + 1.0e-3f,
                 "safety ON peak exceeds ceiling: " + juce::String (sOn.peak));

    // --- Safety OFF ---
    setP ("safety_limit", 0.0f);
    juce::AudioBuffer<float> bufOff;
    p.offlineRender (bufOff, 400.0);
    const auto sOff = analyse (bufOff);
    REQUIRE_MSG (sOff.allFinite, "safety OFF produced non-finite output");
    // The stage must actually do something: with the limiter off the peak should be
    // at least as high as when it's on (and, given +6 dB on a hot kick, higher).
    REQUIRE_MSG (sOff.peak >= sOn.peak,
                 "safety OFF peak (" + juce::String (sOff.peak)
                 + ") not >= ON peak (" + juce::String (sOn.peak) + ")");
}

//==============================================================================
// Test 7 — Undo/Redo restores a parameter value (UndoManager wired into APVTS).
//==============================================================================
static void test_undoRedo_restoresParameterValue()
{
    runHeader ("undo/redo restores parameter value");
    KickAssProcessor p;

    auto* drive = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter ("drive"));
    REQUIRE_MSG (drive != nullptr, "drive parameter missing");
    if (drive == nullptr) return;

    const float initialNorm = drive->getValue();

    // One discrete edit = one transaction = one undo step.
    p.getUndoManager().beginNewTransaction();

    // Pick a denormalised value clearly different from the initial one and write it
    // through getParameterAsValue(). That Value is bound to the parameter's tree
    // property WITH the UndoManager, so it records an undo step synchronously — the
    // same path the editor's slider edits take once APVTS flushes to the tree.
    // (We avoid setValueNotifyingHost here because APVTS only flushes that to the
    // tree on its message-thread timer, which doesn't run in this headless test.)
    const auto& range = drive->getNormalisableRange();
    const float changedPlain = (initialNorm < 0.5f) ? range.convertFrom0to1 (0.9f)
                                                     : range.convertFrom0to1 (0.1f);
    // Re-normalise after the snap-to-interval so our reference matches what the
    // parameter will actually store (drive snaps to a 0.1 grid).
    const float changedNorm = range.convertTo0to1 (range.snapToLegalValue (changedPlain));

    p.apvts.getParameterAsValue ("drive").setValue (changedPlain);
    REQUIRE_NEAR (drive->getValue(), changedNorm, 1.0e-3f);

    REQUIRE (p.canUndo());
    p.undo();
    REQUIRE_NEAR (drive->getValue(), initialNorm, 1.0e-3f);

    REQUIRE (p.canRedo());
    p.redo();
    REQUIRE_NEAR (drive->getValue(), changedNorm, 1.0e-3f);
}

//==============================================================================
// Test 8 — drag-a-WAV transient layer: loads, plays, persists (path round-trip),
// and the no-sample fallback stays finite. (v1.1)
//==============================================================================

// Write a short decaying-sine mono WAV to a temp file; returns the file (or a
// non-existent File on failure).
static juce::File writeTempSineWav (int numSamples = 2000, double sr = 44100.0)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("kickass_transient_test_"
                                + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                                + ".wav");

    juce::AudioBuffer<float> buf (1, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float t   = (float) i / (float) sr;
        const float env = std::exp (-t * 60.0f);                 // fast decay
        buf.setSample (0, i, std::sin (juce::MathConstants<float>::twoPi * 1000.0f * t) * env);
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (f.createOutputStream());
    if (out == nullptr) return {};
    out->setPosition (0);
    if (auto* fos = dynamic_cast<juce::FileOutputStream*> (out.get())) fos->truncate();

    const auto opts = juce::AudioFormatWriterOptions()
                          .withSampleRate (sr)
                          .withNumChannels (1)
                          .withBitsPerSample (16);
    if (auto writer = wav.createWriterFor (out, opts))
        writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
    else
        return {};   // writer ctor moves out of `out`; on failure file is empty

    return f;
}

static void test_transientSample_loadsPlaysAndPersists()
{
    runHeader ("transient sample — loads, plays, persists, no-sample fallback safe");

    auto tmpWav = writeTempSineWav();
    REQUIRE_MSG (tmpWav.existsAsFile(), "failed to write temp WAV");
    if (! tmpWav.existsAsFile()) return;

    auto setP = [] (KickAssProcessor& proc, const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // 1. Load the sample.
    KickAssProcessor p;
    REQUIRE_MSG (p.loadTransientSampleFile (tmpWav), "loadTransientSampleFile failed");

    // 2. clickType=Sample (idx 3) + clickVol≈1.0, normal body → non-silent + finite,
    //    and the sample path differs from clickType=Sine.
    setP (p, "click_vol", 1.0f);
    setP (p, "envelope_mode", 0.0f);   // Simple AHDSR — stable deterministic body

    auto* clickTypeParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("click_type"));
    REQUIRE_MSG (clickTypeParam != nullptr, "click_type param missing");
    REQUIRE_MSG (clickTypeParam != nullptr && clickTypeParam->choices.size() == 4,
                 "click_type should have 4 choices (Sine/Noise/Both/Sample)");

    auto renderWithClickType = [&] (int idx)
    {
        if (clickTypeParam != nullptr)
            clickTypeParam->setValueNotifyingHost (clickTypeParam->convertTo0to1 ((float) idx));
        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 400.0);
        return buf;
    };

    auto sampleBuf = renderWithClickType (3);   // Sample
    const auto sStats = analyse (sampleBuf);
    REQUIRE_MSG (sStats.allFinite, "sample-mode render non-finite");
    REQUIRE_MSG (sStats.peak > 1.0e-4f, "sample-mode render silent (peak="
                                        + juce::String (sStats.peak) + ")");

    auto sineBuf = renderWithClickType (0);     // Sine
    // Mean-abs difference proves the sample path actually changed the output.
    {
        const int n = juce::jmin (sampleBuf.getNumSamples(), sineBuf.getNumSamples());
        double acc = 0.0;
        const float* a = sampleBuf.getReadPointer (0);
        const float* b = sineBuf.getReadPointer (0);
        for (int i = 0; i < n; ++i) acc += std::abs (a[i] - b[i]);
        const double diff = acc / juce::jmax (1, n);
        REQUIRE_MSG (diff > 1.0e-5, "sample path indistinguishable from Sine (diff="
                                    + juce::String (diff) + ")");
    }

    // 3. State persistence: <Sample> path node round-trips through get/setState.
    setP (p, "click_vol", 1.0f);
    clickTypeParam->setValueNotifyingHost (clickTypeParam->convertTo0to1 (3.0f));
    REQUIRE_MSG (p.getTransientSamplePath() == tmpWav.getFullPathName(),
                 "stored path mismatch before round-trip");

    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    REQUIRE_MSG (blob.getSize() > 0, "empty state blob");

    KickAssProcessor p2;
    p2.setStateInformation (blob.getData(), (int) blob.getSize());
    REQUIRE_MSG (p2.getTransientSamplePath() == tmpWav.getFullPathName(),
                 "sample path did not round-trip: got '" + p2.getTransientSamplePath() + "'");

    // Restored processor should still render finite, non-silent sample audio.
    {
        juce::AudioBuffer<float> buf;
        p2.offlineRender (buf, 400.0);
        const auto s = analyse (buf);
        REQUIRE_MSG (s.allFinite, "restored sample render non-finite");
        REQUIRE_MSG (s.peak > 1.0e-4f, "restored sample render silent");
    }

    // 4. Safety: fresh processor, clickType=Sample with NO sample loaded → finite, no crash.
    {
        KickAssProcessor p3;
        setP (p3, "click_vol", 1.0f);
        setP (p3, "envelope_mode", 0.0f);
        if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (p3.apvts.getParameter ("click_type")))
            ct->setValueNotifyingHost (ct->convertTo0to1 (3.0f));
        juce::AudioBuffer<float> buf;
        p3.offlineRender (buf, 400.0);
        const auto s = analyse (buf);
        REQUIRE_MSG (s.allFinite, "no-sample fallback produced non-finite output");
    }

    // 5. Clean up.
    tmpWav.deleteFile();
}

//==============================================================================
// Buffer/range helpers shared by the RT-safety regression tests below.
//==============================================================================
static double rmsOfRange (const juce::AudioBuffer<float>& buf, int ch,
                          int startSample, int endSample)
{
    double acc = 0.0;
    const float* d = buf.getReadPointer (ch);
    for (int i = startSample; i < endSample; ++i)
        acc += (double) d[i] * (double) d[i];
    return std::sqrt (acc / (double) juce::jmax (1, endSample - startSample));
}

// 16-bit PCM WAV with a CONSTANT-amplitude 1 kHz sine. (writeTempSineWav decays
// to inaudibility within ~100 ms, which is useless for natural-length tests.)
static juce::File writeTempConstantToneWav (int numSamples = 44100, double sr = 44100.0)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("kickass_const_tone_"
                                + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                                + ".wav");

    juce::AudioBuffer<float> buf (1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                              * (float) i / (float) sr));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (f.createOutputStream());
    if (out == nullptr) return {};
    out->setPosition (0);
    if (auto* fos = dynamic_cast<juce::FileOutputStream*> (out.get())) fos->truncate();

    const auto opts = juce::AudioFormatWriterOptions()
                          .withSampleRate (sr)
                          .withNumChannels (1)
                          .withBitsPerSample (16);
    if (auto writer = wav.createWriterFor (out, opts))
        writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
    else
        return {};

    return f;
}

// Hand-rolled 32-bit IEEE-float (WAVE_FORMAT_IEEE_FLOAT) mono WAV containing
// NaN / ±Inf / absurdly-huge samples. Written byte-exactly rather than through
// JUCE's writer so the bad values genuinely reach the loader (an int-PCM
// conversion would silently turn NaNs into finite garbage).
static juce::File writeTempFloatWavWithBadSamples (int numSamples = 2048, double sr = 44100.0)
{
    std::vector<float> data ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                   * (float) i / (float) sr);
        if      (i % 128 == 10) v = std::numeric_limits<float>::quiet_NaN();
        else if (i % 128 == 20) v =  std::numeric_limits<float>::infinity();
        else if (i % 128 == 30) v = -std::numeric_limits<float>::infinity();
        else if (i % 128 == 40) v = 1.0e30f;   // finite but absurd — must clamp
        data[(size_t) i] = v;
    }

    juce::MemoryOutputStream mo (4096);
    auto putU32 = [&mo] (juce::uint32 v)
    {
        const juce::uint8 b[4] = { (juce::uint8) (v & 0xff),        (juce::uint8) ((v >> 8)  & 0xff),
                                   (juce::uint8) ((v >> 16) & 0xff), (juce::uint8) ((v >> 24) & 0xff) };
        mo.write (b, 4);
    };
    auto putU16 = [&mo] (juce::uint16 v)
    {
        const juce::uint8 b[2] = { (juce::uint8) (v & 0xff), (juce::uint8) ((v >> 8) & 0xff) };
        mo.write (b, 2);
    };

    const juce::uint32 dataBytes = (juce::uint32) (data.size() * sizeof (float));
    mo.write ("RIFF", 4); putU32 (36 + dataBytes); mo.write ("WAVE", 4);
    mo.write ("fmt ", 4); putU32 (16); putU16 (3); putU16 (1);            // IEEE float, mono
    putU32 ((juce::uint32) sr); putU32 ((juce::uint32) (sr * 4.0)); putU16 (4); putU16 (32);
    mo.write ("data", 4); putU32 (dataBytes);
    mo.write (data.data(), (size_t) dataBytes);

    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("kickass_nan_inf_"
                                + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                                + ".wav");
    if (! f.replaceWithData (mo.getData(), (size_t) mo.getDataSize()))
        return {};
    return f;
}

//==============================================================================
// Test 9 (A1) — transient-sample slot handoff: publishing TWO replacements
// mid-voice must not touch the slot the live voice is reading. The old 2-slot
// handoff let the second publish overwrite the live slot (torn audio; and its
// setSize realloc would be a use-after-free racing the audio thread).
//==============================================================================
static void test_transientSample_publishTwiceMidVoice_keepsVoiceData()
{
    runHeader ("A1: transient sample — double publish mid-voice keeps voice data");

    constexpr double sr = 44100.0;
    KickEngine e;
    e.prepare (sr, 256);

    KickParams p;               // AHDSR defaults → 212 ms envelope
    p.clickType     = 3;        // Sample
    p.clickVol      = 1.0f;
    p.clickHpfHz    = 200.0f;   // let the test tones through
    p.clickToneHz   = 16000.0f;
    p.soloTransient = true;     // mute the body → observe ONLY the sample layer
    e.setParams (p);

    auto makeTone = [&] (float freq, int numSamples)
    {
        juce::AudioBuffer<float> b (1, numSamples);
        for (int i = 0; i < numSamples; ++i)
            b.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * freq
                                                * (float) i / (float) sr));
        return b;
    };

    const auto toneA = makeTone (1000.0f, 22050);   // what the live voice reads
    const auto toneB = makeTone (5000.0f, 22050);
    const auto toneC = makeTone (8000.0f, 22050);

    e.publishTransientSample (toneA, sr);
    e.triggerNote (60, 1.0f, 0);

    juce::AudioBuffer<float> buf (2, 8820);         // 200 ms
    buf.clear();
    e.renderBlock (buf, 0, 2205);                   // 50 ms into the voice

    // The exact ABA sequence that broke the 2-slot code.
    e.publishTransientSample (toneB, sr);
    e.publishTransientSample (toneC, sr);

    e.renderBlock (buf, 2205, 8820 - 2205);

    const auto s = analyse (buf);
    REQUIRE_MSG (s.allFinite, "A1: non-finite output after mid-voice publishes");
    REQUIRE_MSG (s.peak > 1.0e-4f, "A1: sample layer silent");

    // The voice must still read toneA (1 kHz). Old code: publish C overwrote
    // the live slot → the voice jumped to 8 kHz. Count zero crossings in
    // [60 ms, 190 ms]: 1 kHz ≈ 260, 8 kHz ≈ 2080. Threshold in between.
    const float* d = buf.getReadPointer (0);
    int crossings = 0;
    for (int i = 2646; i < 8379; ++i)
        if ((d[i] >= 0.0f) != (d[i - 1] >= 0.0f))
            ++crossings;
    REQUIRE_MSG (crossings < 600,
                 "A1: voice switched to a newly published sample mid-voice (crossings="
                 + juce::String (crossings) + ")");
}

//==============================================================================
// Test 10 (A6) — amp-envelope LUT handoff: same ABA hazard as A1. Publishing
// two short curves mid-voice must not replace the curve the live voice reads.
//==============================================================================
static void test_volCurve_publishTwiceMidVoice_keepsVoiceCurve()
{
    runHeader ("A6: vol curve — double publish mid-voice keeps voice curve");

    constexpr double sr = 44100.0;
    KickEngine e;
    e.prepare (sr, 256);
    e.setParams (KickParams {});
    e.setCurveMode (true);      // Advanced (LUT-driven envelope)

    EnvCurve curveX     = EnvCurve::fromAhdsr (2.0f, 10.0f, 50.0f, 0.4f, 150.0f, 3.0f);  // 212 ms
    EnvCurve curveShort = EnvCurve::fromAhdsr (1.0f, 1.0f, 1.0f, 0.0f, 17.0f, 1.0f);      // 20 ms

    e.publishVolCurve (curveX);
    e.triggerNote (60, 1.0f, 0);

    juce::AudioBuffer<float> buf (2, 13200);   // 300 ms
    buf.clear();
    e.renderBlock (buf, 0, 3528);              // 80 ms into the voice

    // Mid-voice double publish of a 20 ms curve.
    e.publishVolCurve (curveShort);
    e.publishVolCurve (curveShort);

    e.renderBlock (buf, 3528, 13200 - 3528);

    const auto s = analyse (buf);
    REQUIRE_MSG (s.allFinite, "A6: non-finite output after mid-voice curve publishes");

    // If the voice still reads curveX the body is clearly audible at 100-160 ms;
    // if it was switched to the 20 ms curve, the envelope is dead there.
    const double rmsWin = rmsOfRange (buf, 0, 4410, 7056);
    REQUIRE_MSG (rmsWin > 5.0e-3,
                 "A6: voice envelope switched to a newly published curve mid-voice (rms="
                 + juce::String (rmsWin) + ")");
}

//==============================================================================
// Test 11 (A2) — processBlock must render MIDI note-ons at their exact sample
// position. Old code fired every note-on at the block start (up to a full
// buffer of onset jitter).
//==============================================================================
static void test_processBlock_midiNoteOnIsSampleAccurate()
{
    runHeader ("A2: processBlock — MIDI note-on starts at its exact sample position");

    KickAssProcessor p;
    p.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 300);
    p.processBlock (buf, midi);

    // Before the note-on position the output must be EXACTLY zero: all filter
    // states start at zero after prepare, so a causal chain renders true
    // silence there. The old code triggered at the block start, so energy
    // appeared as soon as the (short) oversampler latency allowed.
    double maxBefore = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer (ch);
        for (int i = 0; i < 300; ++i)
            maxBefore = std::max (maxBefore, (double) std::abs (d[i]));
    }
    // (maxBefore is a max of absolute values, so <= 0.0 is the exact-zero test —
    // warning-free under -Wfloat-equal, and a NaN would still fail it.)
    REQUIRE_MSG (maxBefore <= 0.0,
                 "A2: output nonzero before the note-on sample position (max="
                 + juce::String (maxBefore) + ")");

    // And the note must actually sound after its position (well inside the
    // 512-sample block, oversampler latency included).
    float peakAfter = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer (ch);
        for (int i = 300; i < buf.getNumSamples(); ++i)
            peakAfter = std::max (peakAfter, std::abs (d[i]));
    }
    REQUIRE_MSG (peakAfter > 1.0e-4f,
                 "A2: no audible output after the note-on position (peak="
                 + juce::String (peakAfter) + ")");
}

//==============================================================================
// Test 12 (A3) — a host may hand processBlock a buffer larger than the size we
// were prepared with. The old renderBlock bailed out silently → frozen voice.
//==============================================================================
static void test_processBlock_bufferLargerThanPrepared_renders()
{
    runHeader ("A3: renderBlock — buffer larger than the prepared block still renders");

    KickAssProcessor p;
    p.prepareToPlay (44100.0, 64);           // tiny prepared block

    juce::AudioBuffer<float> buf (2, 1024);  // host sends 16x the prepared size
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    p.processBlock (buf, midi);

    const auto s = analyse (buf);
    REQUIRE_MSG (s.allFinite, "A3: oversized block produced non-finite output");
    REQUIRE_MSG (s.peak > 1.0e-4f,
                 "A3: oversized block rendered silent — voice frozen (peak="
                 + juce::String (s.peak) + ")");
}

//==============================================================================
// Test 13 (A4) — retrigger crossfade: the dying tail already carries the OLD
// velocity. The old code scaled it by the NEW velocity again (vel_old ×
// vel_new ≈ 10× too quiet) → audible click on ghost notes.
//==============================================================================
static void test_retrigger_tailKeepsOldVelocity()
{
    runHeader ("A4: retrigger crossfade tail is not double-scaled by velocity");

    constexpr double sr = 44100.0;
    KickEngine e;
    e.prepare (sr, 1024);                    // one chunk per call — isolates A4 from A3 chunking

    // Constant 50 Hz body → the oscillator phase is fully deterministic:
    // phase(t) = 50·t cycles, so at the retrigger point (5 ms) the old voice
    // sits exactly at its positive peak (sin 90°), still inside the amp HOLD
    // segment (env = 1.0). No phase luck, no envelope decay between the
    // before/after windows — the ratio below measures ONLY the crossfade
    // velocity scaling.
    KickParams p;                            // defaults (clickVol 0 → click path off, deterministic)
    p.startFreq = p.midFreq = p.endFreq = 50.0f;
    e.setParams (p);

    juce::AudioBuffer<float> buf (1, 1000);
    buf.clear();
    e.triggerNote (60, 1.0f, 0);
    e.renderBlock (buf, 0, 220);             // 5 ms at velocity 1.0 — voice at its peak

    // The old code's bug creates a hard STEP in the dry signal at the
    // retrigger (tail suddenly ×0.1), which makes the oversampler's half-band
    // IIR ring at near-Nyquist — fake "tail energy" that defeats a plain RMS
    // threshold. So the measurement is smoothed with an 8-sample boxcar
    // first: it has nulls at 5.5/11/16.5 kHz (exactly the ringdown region),
    // finite memory (no settling transient of its own), and passes the smooth
    // 50 Hz tail nearly untouched. The first 8 samples after the retrigger are
    // skipped — the wet output lags the dry signal by the oversampler latency.
    auto boxcarRms = [&] (int start, int end)
    {
        const float* d = buf.getReadPointer (0);
        double acc = 0.0;
        int n = 0;
        for (int i = start; i < end; ++i)
        {
            double m = 0.0;
            for (int k = 0; k < 8; ++k)
                m += d[i - 7 + k];
            m /= 8.0;
            acc += m * m;
            ++n;
        }
        return std::sqrt (acc / (double) std::max (1, n));
    };

    const double rmsBefore = boxcarRms (140, 220);   // 3-5 ms, env = 1.0

    // Ghost retrigger at velocity 0.1, exactly at the osc peak. The crossfade
    // mixes in the dying tail (level ≈ 0.93, OLD velocity already applied);
    // the new voice's body contributes at most 0.1×. Correct code keeps the
    // tail near the old level; the old code scaled the tail by the NEW
    // velocity too (vel_old × vel_new = 10× too quiet → audible click).
    e.triggerNote (60, 0.1f, 0);
    e.renderBlock (buf, 220, 88);            // first 2 ms = crossfade window

    const double rmsAfter = boxcarRms (228, 308);

    const auto s = analyse (buf);
    REQUIRE_MSG (s.allFinite, "A4: retrigger produced non-finite output");
    REQUIRE_MSG (rmsAfter > 0.25 * rmsBefore,
                 "A4: retrigger tail double-scaled by velocity — ghost-note click (ratio="
                 + juce::String (rmsAfter / std::max (1.0e-12, rmsBefore)) + ")");
}

//==============================================================================
// Test 14 (A5) — Sample click mode: the loaded sample must play its NATURAL
// length. The old code ended the voice at the amp-envelope end, hard-cutting
// a 1 s sample at a 100 ms envelope.
//==============================================================================
static void test_sampleMode_voicePlaysSampleNaturalLength()
{
    runHeader ("A5: sample click mode — sample plays its natural length");

    auto wav = writeTempConstantToneWav (44100, 44100.0);   // 1 s of constant 1 kHz
    REQUIRE_MSG (wav.existsAsFile(), "A5: failed to write constant-tone WAV");
    if (! wav.existsAsFile()) return;

    KickAssProcessor p;
    p.prepareToPlay (44100.0, 512);

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // Envelope totals 100 ms; the sample is 1 s → the voice must outlive the
    // envelope by 10x in Sample mode.
    setP ("vol_attack", 0.0f);
    setP ("vol_hold", 0.0f);
    setP ("vol_decay_1", 0.0f);
    setP ("vol_decay_2", 100.0f);
    setP ("envelope_mode", 0.0f);            // Simple AHDSR
    setP ("click_vol", 1.0f);
    setP ("click_hpf", 200.0f);              // keep the 1 kHz tone audible
    setP ("click_tone", 16000.0f);
    if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("click_type")))
        ct->setValueNotifyingHost (ct->convertTo0to1 (3.0f));   // Sample

    REQUIRE_MSG (p.loadTransientSampleFile (wav), "A5: loadTransientSampleFile failed");

    // Render 1.5 s through the realtime path, one note at t=0.
    constexpr int blockSize = 512;
    constexpr int numBlocks  = 130;
    juce::AudioBuffer<float> out (2, blockSize * numBlocks);
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> blk (2, blockSize);
        juce::MidiBuffer m;
        if (b == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        p.processBlock (blk, m);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, b * blockSize, blk, ch, 0, blockSize);
    }

    const auto s = analyse (out);
    REQUIRE_MSG (s.allFinite, "A5: non-finite output in sample-mode render");

    // Energy 300-900 ms after the trigger: well past the 100 ms envelope, well
    // inside the 1 s sample. Old code: dead silence there.
    const double rmsWin = rmsOfRange (out, 0, 13230, 39690);
    REQUIRE_MSG (rmsWin > 1.0e-4,
                 "A5: sample hard-cut at the amp-envelope end — no energy at 300-900 ms (rms="
                 + juce::String (rmsWin) + ")");

    wav.deleteFile();
}

//==============================================================================
// Test 15 (A7) — a 32-bit float WAV containing NaN/±Inf/huge samples must be
// sanitized at load time. Without it ONE bad sample permanently poisons the
// click HPF/LPF, the oversampler and the DC blocker (all recursive filters),
// so every subsequent render is non-finite.
//==============================================================================
static void test_nanInfTransientWav_rendersFinite()
{
    runHeader ("A7: NaN/Inf transient WAV sanitized — all output stays finite");

    auto wav = writeTempFloatWavWithBadSamples();
    REQUIRE_MSG (wav.existsAsFile(), "A7: failed to write NaN/Inf float WAV");
    if (! wav.existsAsFile()) return;

    KickAssProcessor p;
    p.prepareToPlay (44100.0, 256);

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    setP ("envelope_mode", 0.0f);
    setP ("click_vol", 1.0f);
    if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("click_type")))
        ct->setValueNotifyingHost (ct->convertTo0to1 (3.0f));   // Sample

    REQUIRE_MSG (p.loadTransientSampleFile (wav), "A7: loadTransientSampleFile rejected the WAV");

    // Two notes through the realtime path, the second well after the first
    // note's sample finished — a poisoned recursive filter state would still
    // be poisoning output there.
    constexpr int blockSize = 256;
    constexpr int numBlocks = 200;
    juce::AudioBuffer<float> out (2, blockSize * numBlocks);
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> blk (2, blockSize);
        juce::MidiBuffer m;
        if (b == 0 || b == 100)
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        p.processBlock (blk, m);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, b * blockSize, blk, ch, 0, blockSize);
    }

    const auto s = analyse (out);
    REQUIRE_MSG (s.allFinite, "A7: NaN/Inf sample WAV produced non-finite output");
    REQUIRE_MSG (s.peak > 1.0e-4f, "A7: render silent after sanitized load");

    wav.deleteFile();
}

//==============================================================================
// Test 16 (A9 + R1) — output gain is applied per-sample through a ~10 ms
// smoother, not as a per-block constant (zipper on automation). A gain change
// while a voice is SOUNDING must ramp over ~10 ms (A9); a gain change while
// the engine is IDLE must take effect from the very first sample of the NEXT
// kick (R1). Pre-fix, the smoother only ever advanced inside applyPostStages,
// which never runs while idle — a between-kicks change was ramped straight
// across the next kick's transient (~its first 10 ms).
//==============================================================================
static void test_outputGain_smoothedNotBlockStepped()
{
    runHeader ("A9/R1: output gain — ramps while sounding, snaps while idle");

    constexpr double sr = 44100.0;
    constexpr int blockSize = 256;

    KickParams p;                  // defaults; clickVol 0 → deterministic
    p.safetyLimit = false;         // keep the gain math pure (no limiter clamp)

    //----------------------------------------------------------------------
    // Part 1 (R1, engine level): engine "late" is prepared under the default
    // gain and receives outputGainDb = -6 only AFTER prepare() — the stale
    // smoother state the old prepareToPlay order produced (prepare/reset ran
    // before the params were read). It then renders one idle block and
    // triggers. Engine "early" had -6 before prepare(), so reset() snapped
    // its smoother correctly. The idle snap must make "late"'s first block
    // match "early"'s — without it, "late" ramps ~1.0 → 10^(-6/20) across
    // the first ~441 samples, i.e. straight across the transient.
    //----------------------------------------------------------------------
    {
        KickParams pQuiet = p;
        pQuiet.outputGainDb = -6.0f;

        KickEngine late, early;
        late.prepare (sr, blockSize);          // smoother snaps to the default 0 dB
        early.setParams (pQuiet);              // -6 dB known BEFORE prepare
        early.prepare (sr, blockSize);        // reset() snaps the smoother to -6 dB
        late.setParams (pQuiet);              // -6 dB arrives only AFTER prepare

        juce::AudioBuffer<float> idle (1, blockSize);
        idle.clear();
        late.renderBlock (idle, 0, blockSize);   // idle block — no voice sounding

        late.triggerNote (60, 1.0f, 0);
        early.triggerNote (60, 1.0f, 0);

        juce::AudioBuffer<float> lateBuf (1, blockSize), earlyBuf (1, blockSize);
        lateBuf.clear();
        earlyBuf.clear();
        late.renderBlock (lateBuf, 0, blockSize);
        early.renderBlock (earlyBuf, 0, blockSize);

        // The kicks must actually sound, or the comparison below is vacuous.
        REQUIRE_MSG (analyse (earlyBuf).peak > 1.0e-4f,
                     "R1: -6 dB reference kick unexpectedly silent");

        double maxDiff = 0.0;
        for (int i = 0; i < blockSize; ++i)
            maxDiff = std::max (maxDiff,
                                (double) std::abs (lateBuf.getSample (0, i)
                                                  - earlyBuf.getSample (0, i)));
        REQUIRE_MSG (maxDiff <= 1.0e-7,
                     "R1: idle gain change ramped the next kick's first block "
                     "(maxDiff=" + juce::String (maxDiff) + ")");

        // Anti-vacuity: the -6 dB render must differ from a default-gain
        // render — proves outputGainDb actually shaped both engines above.
        KickEngine loud;
        loud.setParams (p);
        loud.prepare (sr, blockSize);
        loud.triggerNote (60, 1.0f, 0);
        juce::AudioBuffer<float> loudBuf (1, blockSize);
        loudBuf.clear();
        loud.renderBlock (loudBuf, 0, blockSize);
        double gainDiff = 0.0;
        for (int i = 0; i < blockSize; ++i)
            gainDiff = std::max (gainDiff,
                                 (double) std::abs (loudBuf.getSample (0, i)
                                                   - earlyBuf.getSample (0, i)));
        REQUIRE_MSG (gainDiff > 1.0e-3,
                     "R1: outputGainDb = -6 did not change the render (vacuous compare)");
    }

    //----------------------------------------------------------------------
    // Part 1b (R1, processor level): kick 1 fires in the FIRST processBlock
    // after output_gain was set to -6 dB; the processor is then re-prepared
    // (as a host does on a sample-rate change) and kick 2 fires. Both kicks
    // start from a fully reset engine, so they must match sample-for-sample —
    // unless kick 1 ramped from a stale smoother value (the pre-R1 bug).
    //----------------------------------------------------------------------
    {
        KickAssProcessor proc;
        auto setP = [&] (const char* id, float plainValue)
        {
            if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)))
                rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                    rap->getNormalisableRange().convertTo0to1 (plainValue)));
        };
        setP ("output_gain", -6.0f);           // set BEFORE the first prepareToPlay
        proc.prepareToPlay (sr, blockSize);

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);

        juce::AudioBuffer<float> buf1 (2, blockSize), buf2 (2, blockSize);
        proc.processBlock (buf1, midi);

        proc.prepareToPlay (sr, blockSize);   // re-prepare → engine fully reset
        proc.processBlock (buf2, midi);

        REQUIRE_MSG (analyse (buf2).peak > 1.0e-4f,
                     "R1: processor kick unexpectedly silent");

        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                maxDiff = std::max (maxDiff,
                                    (double) std::abs (buf1.getSample (ch, i)
                                                      - buf2.getSample (ch, i)));
        REQUIRE_MSG (maxDiff <= 1.0e-7,
                     "R1: first kick after prepareToPlay ramped from a stale gain "
                     "(maxDiff=" + juce::String (maxDiff) + ")");
    }

    //----------------------------------------------------------------------
    // Part 2 (A9): a gain change DURING a sounding voice must still ramp
    // smoothly over ~10 ms (kept from the original test).
    //----------------------------------------------------------------------
    KickEngine ref, sut;
    ref.prepare (sr, 256);
    sut.prepare (sr, 256);
    ref.setParams (p);
    sut.setParams (p);
    ref.triggerNote (60, 1.0f, 0);
    sut.triggerNote (60, 1.0f, 0);

    const int pre   = 17 * 256;    // ~98.9 ms
    const int total = 25 * 256;
    juce::AudioBuffer<float> refBuf (1, total), sutBuf (1, total);
    refBuf.clear();
    sutBuf.clear();
    ref.renderBlock (refBuf, 0, pre);
    sut.renderBlock (sutBuf, 0, pre);

    // SUT drops to -24 dB here (as a host automation change would mid-stream).
    KickParams p2 = p;
    p2.outputGainDb = -24.0f;
    sut.setParams (p2);

    int written = pre;
    while (written < total)
    {
        ref.renderBlock (refBuf, written, 256);
        sut.renderBlock (sutBuf, written, 256);
        written += 256;
    }

    REQUIRE_MSG (analyse (refBuf).allFinite && analyse (sutBuf).allFinite,
                 "A9: non-finite output during gain ramp");

    // First 64 samples after the change: the gain must still be NEAR the old
    // value (ramping). Old code stepped the whole block to the new gain at once
    // → ratio ≈ 0.063.
    const double earlyRatio = rmsOfRange (sutBuf, 0, pre, pre + 64)
                              / std::max (1.0e-12, rmsOfRange (refBuf, 0, pre, pre + 64));
    REQUIRE_MSG (earlyRatio > 0.5,
                 "A9: output gain stepped at the block boundary (zipper); ratio="
                 + juce::String (earlyRatio) + ")");

    // And the ~10 ms ramp must converge to the new gain (10^(-24/20) ≈ 0.0631).
    const int conv = pre + 512;   // past the 441-sample ramp
    const double lateRatio = rmsOfRange (sutBuf, 0, conv, conv + 256)
                             / std::max (1.0e-12, rmsOfRange (refBuf, 0, conv, conv + 256));
    REQUIRE_MSG (lateRatio > 0.045 && lateRatio < 0.085,
                 "A9: smoothed gain did not converge to -24 dB; ratio="
                 + juce::String (lateRatio) + ")");
}

//==============================================================================
// Test 17 (A8) — the -0.3 dBFS character soft-clip must be a TRUE ceiling.
// The old stage multiplied the tanh by 1/tanh(1) (+2.4 dB "±1 in → ±1 out"
// make-up), giving a unity-slope×1.313 stage whose asymptote sat at
// ceil/tanh(1) ≈ +2.1 dBFS — i.e. NOT a -0.3 dBFS ceiling at all. Only the
// defeatable -0.1 dBFS safety limiter hid that; with Safety OFF the output
// could exceed 0 dBFS. The fixed stage is out = ceil·tanh(in/ceil): slope 1
// for |in| << ceil, asymptote ceil, so |out| < -0.3 dBFS for ANY finite in.
//==============================================================================
static void test_softClipCeiling_isTrueMinus03DbfsCeiling()
{
    runHeader ("A8: character soft-clip is a true -0.3 dBFS ceiling");

    // 10^(-0.3/20) ≈ 0.966052 — the ceiling in linear gain.
    const float ceilLin = std::pow (10.0f, -0.3f * 0.05f);
    const float bound   = ceilLin + 1.0e-4f;   // tolerance for float round-off at the asymptote

    KickAssProcessor p;

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // Every render in this test neutralises the two stages that could mask
    // the character ceiling: output gain exactly 0 dB (×1.0) and the -0.1 dBFS
    // safety limiter OFF. The buffer then contains exactly the soft-clip
    // stage's output (polarity invert only flips the sign).
    auto neutraliseMaster = [&]()
    {
        setP ("output_gain", 0.0f);
        setP ("safety_limit", 0.0f);
    };

    //----------------------------------------------------------------------
    // Part 1 — every factory preset: finite, non-silent, and |peak| never
    // above -0.3 dBFS with nothing after the stage to hide an overshoot.
    //----------------------------------------------------------------------
    auto& pm = p.getPresetManager();
    const int n = pm.getFactoryCount();
    REQUIRE_MSG (n == 16, "A8: expected 16 factory presets, got " + juce::String (n));

    const auto names = pm.getFactoryNames();
    float maxPreClip = 0.0f;   // largest PRE-clip peak over all presets

    for (int i = 0; i < n; ++i)
    {
        REQUIRE_MSG (pm.applyFactory (i), "A8: applyFactory failed: " + names[i]);
        neutraliseMaster();

        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 1500.0);   // full kick lifetime incl. the tail-drive ramp
        const auto s = analyse (buf);

        REQUIRE_MSG (s.allFinite, "A8: non-finite sample in preset: " + names[i]);
        REQUIRE_MSG (s.peak <= bound, "A8: preset '" + names[i] + "' exceeds the -0.3 dBFS "
                     "ceiling with safety OFF: peak=" + juce::String (s.peak)
                     + " (" + juce::String (20.0 * std::log10 (s.peak), 3) + " dBFS)");
        REQUIRE_MSG (s.peak > 1.0e-4f, "A8: preset is silent: " + names[i]);

        std::cout << "  A8  " << names[i] << ": peak " << juce::String (s.peak, 6)
                  << "  (" << juce::String (20.0 * std::log10 (s.peak), 3) << " dBFS)"
                  << std::endl;

        // Pre-clip peak of this render: the stage is a strictly monotonic
        // bijection out = ceil·tanh(in/ceil), and with 0 dB gain + safety OFF
        // the buffer IS the stage output, so the peak sample inverts exactly:
        // in = ceil·atanh(out/ceil). (Peak |out| and peak |in| sit on the same
        // sample because the map is odd and increasing.)
        const float ratio = juce::jmin (s.peak, ceilLin * (1.0f - 1.0e-6f)) / ceilLin;
        maxPreClip = std::max (maxPreClip,
                               ceilLin * (float) std::atanh ((double) ratio));
    }

    // Anti-vacuity (a): at least one preset's PRE-clip dry signal must top
    // 0.9 × ceil — the ceiling has to clamp a real signal, or the bound above
    // would pass trivially on quiet renders. (Loud Tanh presets feed the
    // stage ~±1.0: the drive normalisation maps a unit dry peak to ±1.)
    REQUIRE_MSG (maxPreClip > 0.9f * ceilLin,
                 "A8: no factory preset drives the ceiling into its saturation region "
                 "(max pre-clip peak=" + juce::String (maxPreClip) + ") — bound is vacuous");

    //----------------------------------------------------------------------
    // Part 2 — worst case in the parameter space: max base drive, max tail
    // drive, click at max. The tail ramp tops out at 10 + 5·10 = 60×.
    //----------------------------------------------------------------------
    REQUIRE_MSG (pm.applyFactory (0), "A8: applyFactory(0) for the worst-case combo failed");
    setP ("envelope_mode", 0.0f);   // Simple AHDSR — stable, deterministic body
    setP ("drive", 10.0f);
    setP ("tail_drive", 10.0f);
    setP ("click_vol", 1.0f);
    if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("click_type")))
        ct->setValueNotifyingHost (ct->convertTo0to1 (0.0f));   // Sine click
    neutraliseMaster();

    {
        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 1500.0);
        const auto s = analyse (buf);
        REQUIRE_MSG (s.allFinite, "A8: worst-case drive render non-finite");
        REQUIRE_MSG (s.peak <= bound, "A8: worst-case drive exceeds the -0.3 dBFS ceiling: "
                     "peak=" + juce::String (s.peak));
        REQUIRE_MSG (s.peak > 1.0e-4f, "A8: worst-case drive render silent");
        std::cout << "  A8  worst-case (drive 10 + tail 10 + click 1.0): peak "
                  << juce::String (s.peak, 6) << "  ("
                  << juce::String (20.0 * std::log10 (s.peak), 3) << " dBFS)" << std::endl;
    }

    //----------------------------------------------------------------------
    // Part 3 — prove the bound is TIGHT, not vacuous: slam the stage into
    // full saturation and require the output to hug the asymptote from
    // below. Foldback at drive 2.0 folds its own normalisation to the 10×
    // clamp (satFoldback(2) == 0), so the pre-clip signal reaches ~±10 and
    // the tanh is fully saturated — output ≈ ceil. Under the OLD +2.4 dB
    // make-up this exact render peaked at ceil·tanh(10/ceil)·1.313 ≈ +2.1
    // dBFS — the bound violation A8 was filed for.
    //----------------------------------------------------------------------
    REQUIRE_MSG (pm.applyFactory (0), "A8: applyFactory(0) for the saturation combo failed");
    setP ("envelope_mode", 0.0f);
    if (auto* sat = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("sat_type")))
        sat->setValueNotifyingHost (sat->convertTo0to1 (4.0f));   // Foldback
    setP ("drive", 2.0f);
    setP ("tail_drive", 0.0f);
    neutraliseMaster();

    {
        juce::AudioBuffer<float> buf;
        p.offlineRender (buf, 1500.0);
        const auto s = analyse (buf);
        REQUIRE_MSG (s.allFinite, "A8: saturated render non-finite");
        REQUIRE_MSG (s.peak <= bound, "A8: saturated render exceeds the -0.3 dBFS ceiling: "
                     "peak=" + juce::String (s.peak));
        // Anti-vacuity (b): the fully saturated render must sit just UNDER
        // the ceiling (> 0.9 × ceil) — the bound is exercised at its limit,
        // not on a quiet signal that would pass trivially.
        REQUIRE_MSG (s.peak > 0.9f * ceilLin,
                     "A8: saturated render does not approach the ceiling — bound not "
                     "exercised (peak=" + juce::String (s.peak) + ")");
        std::cout << "  A8  saturated (Foldback drive 2.0): peak "
                  << juce::String (s.peak, 6) << "  ("
                  << juce::String (20.0 * std::log10 (s.peak), 3) << " dBFS)" << std::endl;
    }
}

//==============================================================================
// Test 18 (B4) — a stored <Sample> path that points to a missing file must
// survive setStateInformation. The old code called clearTransientSample(),
// which REMOVED the <Sample> node: opening the project on a machine where the
// file is absent and saving destroyed the path forever (unrecoverable even
// back on the machine where the file exists).
//==============================================================================
static void test_missingSampleFile_keepsStatePath()
{
    runHeader ("B4: missing sample file — engines cleared, <Sample> path kept");

    auto tmpWav = writeTempSineWav();
    REQUIRE_MSG (tmpWav.existsAsFile(), "B4: failed to write temp WAV");
    if (! tmpWav.existsAsFile()) return;

    // Produce a normal state WITH a <Sample> node, then point the stored path
    // at a file that does not exist (what a project move looks like).
    KickAssProcessor src;
    REQUIRE_MSG (src.loadTransientSampleFile (tmpWav), "B4: loadTransientSampleFile failed");

    juce::MemoryBlock blob;
    src.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    REQUIRE_MSG (xml != nullptr, "B4: could not re-parse the saved state XML");

    const juce::String missingPath = "/this/file/does/not/exist.wav";
    bool rewrote = false;
    for (auto* e : xml->getChildIterator())
    {
        if (e->hasTagName ("Sample"))
        {
            e->setAttribute ("path", missingPath);
            rewrote = true;
        }
    }
    REQUIRE_MSG (rewrote, "B4: saved state has no <Sample> node");
    if (! rewrote) { tmpWav.deleteFile(); return; }

    juce::MemoryBlock badBlob;
    juce::AudioProcessor::copyXmlToBinary (*xml, badBlob);

    KickAssProcessor dst;
    dst.setStateInformation (badBlob.getData(), (int) badBlob.getSize());

    // The path must STILL be in the state — only the engines are cleared.
    // Old code: clearTransientSample() deleted the node → "".
    REQUIRE_MSG (dst.getTransientSamplePath() == missingPath,
                 "B4: <Sample> path lost on missing file: '"
                 + dst.getTransientSamplePath() + "'");

    // And saving the project again must keep it (the original bug destroyed
    // the path on the second machine).
    juce::MemoryBlock reblob;
    dst.getStateInformation (reblob);
    KickAssProcessor dst2;
    dst2.setStateInformation (reblob.getData(), (int) reblob.getSize());
    REQUIRE_MSG (dst2.getTransientSamplePath() == missingPath,
                 "B4: <Sample> path lost through a save/load round-trip");

    // click_type=Sample with a cleared slot must render finite audio.
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (
                dst.apvts.getParameter ("click_vol")))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (1.0f)));
        if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (
                dst.apvts.getParameter ("click_type")))
            ct->setValueNotifyingHost (ct->convertTo0to1 (3.0f));   // Sample

        juce::AudioBuffer<float> buf;
        dst.offlineRender (buf, 400.0);
        const auto s = analyse (buf);
        REQUIRE_MSG (s.allFinite, "B4: non-finite render after missing-file restore");
    }

    tmpWav.deleteFile();
}

//==============================================================================
// Test 19 (B3) — every path that replaces apvts.state must also swap the
// transient-sample layer to match. Only setStateInformation reloaded the
// sample: loadKickPreset left the engines holding the previous patch's sample
// (or none), so a preset WITH a sample clicked wrong, and a preset WITHOUT
// one kept playing the old sample.
//==============================================================================
static void test_presetLoad_swapsTransientSample()
{
    runHeader ("B3: loadKickPreset reloads the preset's <Sample> (or clears the slot)");

    auto tmpWav = writeTempSineWav();
    REQUIRE_MSG (tmpWav.existsAsFile(), "B3: failed to write temp WAV");
    if (! tmpWav.existsAsFile()) return;

    auto tempPresetFile = [] (const char* tag)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile (juce::String (tag) + "_"
                       + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                       + ".kickpreset");
    };

    auto setClickSampleMode = [] (KickAssProcessor& proc)
    {
        auto setP = [&] (const char* id, float plainValue)
        {
            if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (
                    proc.apvts.getParameter (id)))
                rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                    rap->getNormalisableRange().convertTo0to1 (plainValue)));
        };
        setP ("click_vol", 1.0f);
        setP ("envelope_mode", 0.0f);   // Simple AHDSR — stable, deterministic body
        if (auto* ct = dynamic_cast<juce::AudioParameterChoice*> (
                proc.apvts.getParameter ("click_type")))
            ct->setValueNotifyingHost (ct->convertTo0to1 (3.0f));   // Sample
    };

    // Save one preset WITH a sample and one WITHOUT.
    KickAssProcessor withSample;
    REQUIRE_MSG (withSample.loadTransientSampleFile (tmpWav), "B3: sample load failed");
    setClickSampleMode (withSample);
    auto withFile = tempPresetFile ("kickass_b3_with");
    REQUIRE_MSG (withSample.getPresetManager().saveKickPreset (withFile),
                 "B3: saveKickPreset(with-sample) failed");

    KickAssProcessor withoutSample;
    setClickSampleMode (withoutSample);
    auto withoutFile = tempPresetFile ("kickass_b3_without");
    REQUIRE_MSG (withoutSample.getPresetManager().saveKickPreset (withoutFile),
                 "B3: saveKickPreset(without-sample) failed");

    auto meanAbsDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        double acc = 0.0;
        const float* pa = a.getReadPointer (0);
        const float* pb = b.getReadPointer (0);
        for (int i = 0; i < n; ++i) acc += std::abs (pa[i] - pb[i]);
        return acc / juce::jmax (1, n);
    };

    // Part 1 — fresh processor + preset WITH a sample: the sample must really
    // be playing in the engine (a render with the sample must differ from a
    // render after the sample is cleared).
    {
        KickAssProcessor dst;
        REQUIRE_MSG (dst.getPresetManager().loadKickPreset (withFile),
                     "B3: loadKickPreset(with-sample) failed");
        REQUIRE_MSG (dst.getTransientSamplePath() == tmpWav.getFullPathName(),
                     "B3: preset <Sample> path not in state after load");

        juce::AudioBuffer<float> withBuf;
        dst.offlineRender (withBuf, 400.0);

        dst.clearTransientSample();
        juce::AudioBuffer<float> withoutBuf;
        dst.offlineRender (withoutBuf, 400.0);

        const double diff = meanAbsDiff (withBuf, withoutBuf);
        REQUIRE_MSG (diff > 1.0e-5,
                     "B3: preset sample not reloaded into the engine (diff="
                     + juce::String (diff) + ")");
    }

    // Part 2 — processor holding a LIVE sample + preset WITHOUT a sample: the
    // engine must stop playing the old sample (render identical to the
    // explicitly-cleared slot).
    {
        KickAssProcessor dst;
        REQUIRE_MSG (dst.loadTransientSampleFile (tmpWav), "B3: sample load failed");
        REQUIRE_MSG (dst.getPresetManager().loadKickPreset (withoutFile),
                     "B3: loadKickPreset(without-sample) failed");

        juce::AudioBuffer<float> afterLoadBuf;
        dst.offlineRender (afterLoadBuf, 400.0);

        dst.clearTransientSample();
        juce::AudioBuffer<float> clearedBuf;
        dst.offlineRender (clearedBuf, 400.0);

        const double diff = meanAbsDiff (afterLoadBuf, clearedBuf);
        REQUIRE_MSG (diff < 1.0e-6,
                     "B3: old sample kept playing after preset load (diff="
                     + juce::String (diff) + ")");
    }

    withFile.deleteFile();
    withoutFile.deleteFile();
    tmpWav.deleteFile();
}

//==============================================================================
// Test 20 (B5) — applyFactory/loadJson must be FULL applies: any parameter
// the preset data doesn't list (sat_type, safety_limit, phase_offset, …) is
// reset to its default first. The old partial apply leaked the user's
// previous values — e.g. Foldback saturation into 'Psytrance (Default)'.
//==============================================================================
static void test_presetApply_resetsUnlistedParams()
{
    runHeader ("B5: applyFactory/loadJson reset params the preset data doesn't list");

    KickAssProcessor p;

    auto rawOf = [] (KickAssProcessor& proc, const char* id)
    { return proc.apvts.getRawParameterValue (id)->load(); };

    auto setP = [&] (const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // Sabotage: Foldback saturation, safety limiter off, phase offset 90°,
    // Simple envelope mode (must SURVIVE the apply — UI preference).
    setP ("sat_type", 4.0f);
    setP ("safety_limit", 0.0f);
    setP ("phase_offset", 90.0f);
    setP ("envelope_mode", 0.0f);
    REQUIRE_MSG (rawOf (p, "sat_type") == 4.0f, "B5 setup: sat_type not Foldback");
    REQUIRE_MSG (rawOf (p, "safety_limit") < 0.5f, "B5 setup: safety_limit not off");

    // Factory apply must reset everything it doesn't list…
    REQUIRE_MSG (p.getPresetManager().applyFactory (0), "B5: applyFactory(0) failed");
    REQUIRE_MSG (rawOf (p, "sat_type") == 0.0f,
                 "B5: sat_type leaked through applyFactory: "
                 + juce::String (rawOf (p, "sat_type")));
    REQUIRE_MSG (rawOf (p, "safety_limit") > 0.5f,
                 "B5: safety_limit leaked through applyFactory");
    REQUIRE_MSG (rawOf (p, "phase_offset") == 0.0f,
                 "B5: phase_offset leaked through applyFactory");
    // …except the envelope_mode UI preference.
    REQUIRE_MSG (rawOf (p, "envelope_mode") < 0.5f,
                 "B5: envelope_mode (UI preference) was reset by applyFactory");

    // Same contract for a minimal Python-format .json (subset of keys).
    setP ("sat_type", 4.0f);
    setP ("safety_limit", 0.0f);

    auto jsonFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("kickass_b5_minimal_"
                       + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                       + ".json");
    jsonFile.replaceWithText (juce::String ("{ \"start_freq\": 8000.0, \"end_freq\": 49.0 }"));
    REQUIRE_MSG (p.getPresetManager().loadJson (jsonFile), "B5: loadJson failed");
    REQUIRE_MSG (rawOf (p, "sat_type") == 0.0f,
                 "B5: sat_type leaked through loadJson: "
                 + juce::String (rawOf (p, "sat_type")));
    REQUIRE_MSG (rawOf (p, "safety_limit") > 0.5f, "B5: safety_limit leaked through loadJson");
    // The listed keys must actually be applied.
    REQUIRE_NEAR (rawOf (p, "start_freq"), 8000.0f, 0.5f);

    jsonFile.deleteFile();
}

//==============================================================================
// Test 21 (B9) — a corrupt state or .kickpreset with <PARAM id="…"
// value="nan"> must not yield a NaN parameter (JUCE's number parser happily
// reads "nan"/"inf" into a double). The PARAM child is dropped before
// replaceState, so the parameter falls back to its default instead of
// poisoning the audio path.
//==============================================================================
static void test_nanParamValue_isSanitized()
{
    runHeader ("B9: PARAM value='nan' sanitized before replaceState");

    auto setP = [] (KickAssProcessor& proc, const char* id, float plainValue)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (
                proc.apvts.getParameter (id)))
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                rap->getNormalisableRange().convertTo0to1 (plainValue)));
    };

    // 1) State blob with a NaN drive → setStateInformation → finite default.
    {
        KickAssProcessor src;
        auto xml = src.apvts.copyState().createXml();
        REQUIRE_MSG (xml != nullptr, "B9: copyState().createXml() failed");

        bool corrupted = false;
        for (auto* e : xml->getChildIterator())
        {
            if (e->hasTagName ("PARAM") && e->getStringAttribute ("id") == "drive")
            {
                e->setAttribute ("value", "nan");
                corrupted = true;
            }
        }
        REQUIRE_MSG (corrupted, "B9: no drive PARAM in the state XML");
        if (! corrupted) return;

        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);

        KickAssProcessor dst;
        dst.setStateInformation (blob.getData(), (int) blob.getSize());

        const float driveRaw = dst.apvts.getRawParameterValue ("drive")->load();
        REQUIRE_MSG (std::isfinite (driveRaw),
                     "B9: drive is non-finite after setStateInformation");
        if (auto* driveParam = dynamic_cast<juce::RangedAudioParameter*> (
                dst.apvts.getParameter ("drive")))
        {
            const float expectedDefault = driveParam->getNormalisableRange()
                                              .convertFrom0to1 (driveParam->getDefaultValue());
            REQUIRE_NEAR (driveRaw, expectedDefault, 1.0e-4f);
        }

        juce::AudioBuffer<float> buf;
        dst.offlineRender (buf, 300.0);
        REQUIRE_MSG (analyse (buf).allFinite,
                     "B9: non-finite audio after NaN state restore");
    }

    // 2) .kickpreset with a NaN drive → loadKickPreset into a processor holding
    //    a valid drive → the drive must stay finite (falls back to default).
    {
        KickAssProcessor saver;
        setP (saver, "drive", 8.0f);
        auto presetFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("kickass_b9_nan_"
                           + juce::String (juce::Random::getSystemRandom().nextInt (1'000'000))
                           + ".kickpreset");
        REQUIRE_MSG (saver.getPresetManager().saveKickPreset (presetFile),
                     "B9: saveKickPreset failed");

        auto xml = juce::parseXML (presetFile);
        REQUIRE_MSG (xml != nullptr, "B9: preset re-parse failed");
        bool corrupted = false;
        for (auto* e : xml->getChildIterator())
        {
            if (e->hasTagName ("PARAM") && e->getStringAttribute ("id") == "drive")
            {
                e->setAttribute ("value", "nan");
                corrupted = true;
            }
        }
        REQUIRE_MSG (corrupted, "B9: no drive PARAM in the saved preset");
        if (! corrupted) { presetFile.deleteFile(); return; }
        presetFile.replaceWithText (xml->toString());

        KickAssProcessor holder;
        setP (holder, "drive", 8.0f);
        REQUIRE_MSG (holder.getPresetManager().loadKickPreset (presetFile),
                     "B9: loadKickPreset rejected the corrupt preset");

        const float driveRaw = holder.apvts.getRawParameterValue ("drive")->load();
        REQUIRE_MSG (std::isfinite (driveRaw),
                     "B9: drive non-finite after corrupt preset load");
        // The corrupt PARAM child is dropped, so APVTS falls back to the
        // default for that parameter (it does not keep the pre-load value).
        if (auto* driveParam = dynamic_cast<juce::RangedAudioParameter*> (
                holder.apvts.getParameter ("drive")))
        {
            const float expectedDefault = driveParam->getNormalisableRange()
                                              .convertFrom0to1 (driveParam->getDefaultValue());
            REQUIRE_NEAR (driveRaw, expectedDefault, 1.0e-4f);
        }

        presetFile.deleteFile();
    }
}

//==============================================================================
int main (int, char**)
{
    juce::MessageManager::getInstance();

    test_allFactoryPresets_produceNonSilentFiniteAudio();
    test_allFactoryPresets_stateRoundtrip();
    test_paramFuzz_neverProducesNanOrInf();
    test_extremeParams_stayFinite();
    test_saturationTypes_distinctFiniteAndDefaultTanh();
    test_safetyLimiter_enforcesCeiling();
    test_undoRedo_restoresParameterValue();
    test_transientSample_loadsPlaysAndPersists();

    // RT-safety / DSP-bug regression net (A1..A9 review findings).
    test_transientSample_publishTwiceMidVoice_keepsVoiceData();   // A1
    test_volCurve_publishTwiceMidVoice_keepsVoiceCurve();         // A6
    test_processBlock_midiNoteOnIsSampleAccurate();               // A2
    test_processBlock_bufferLargerThanPrepared_renders();         // A3
    test_retrigger_tailKeepsOldVelocity();                        // A4
    test_sampleMode_voicePlaysSampleNaturalLength();               // A5
    test_nanInfTransientWav_rendersFinite();                       // A7
    test_outputGain_smoothedNotBlockStepped();                     // A9 + R1
    test_softClipCeiling_isTrueMinus03DbfsCeiling();               // A8

    // Preset/state bug regression net (B3/B4/B5/B9 review findings).
    test_missingSampleFile_keepsStatePath();                        // B4
    test_presetLoad_swapsTransientSample();                        // B3
    test_presetApply_resetsUnlistedParams();                        // B5
    test_nanParamValue_isSanitized();                               // B9

    if (g_failures == 0)
    {
        std::cout << "\nALL DSP SAFETY TESTS PASSED\n";
        juce::MessageManager::deleteInstance();
        return 0;
    }

    std::cerr << "\n" << g_failures << " FAILURE(S)\n";
    juce::MessageManager::deleteInstance();
    return 1;
}
