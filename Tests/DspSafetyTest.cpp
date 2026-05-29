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
#include <iostream>
#include <cmath>

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
int main (int, char**)
{
    juce::MessageManager::getInstance();

    test_allFactoryPresets_produceNonSilentFiniteAudio();
    test_allFactoryPresets_stateRoundtrip();
    test_paramFuzz_neverProducesNanOrInf();
    test_extremeParams_stayFinite();
    test_saturationTypes_distinctFiniteAndDefaultTanh();

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
