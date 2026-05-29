// Phase 6b #7 — save/load round-trip tests for the breakpoint curve.
//
// Headless console test. Built when KICKASS_BUILD_TESTS=ON. Run with:
//   cmake --build build --config Release --target KickAss_Tests
//   build/KickAss_Tests_artefacts/Release/KickAss_Tests.exe
//
// Returns 0 on PASS, 1 on FAIL.

#include "../Source/PluginProcessor.h"
#include "../Source/PresetManager.h"
#include "../Source/EnvCurve.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

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
#define REQUIRE_EQ(a, b) do { auto _a = (a); auto _b = (b); if (! (_a == _b)) \
    reportFail (#a " == " #b, __FILE__, __LINE__, juce::String (_a) + " vs " + juce::String (_b)); } while (0)
#define REQUIRE_NEAR(a, b, tol) do { auto _a = (float) (a); auto _b = (float) (b); if (std::abs (_a - _b) > (tol)) \
    reportFail (#a " ~= " #b, __FILE__, __LINE__, juce::String (_a) + " vs " + juce::String (_b)); } while (0)

//==============================================================================
// Helpers
//==============================================================================
static juce::String runHeader (const char* name)
{
    std::cout << "---- " << name << " ----" << std::endl;
    return name;
}

static bool curvesEqual (const EnvCurve& a, const EnvCurve& b, float tol = 1e-4f)
{
    if (a.points.size() != b.points.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        if (std::abs (a.points[i].timeMs  - b.points[i].timeMs)  > tol) return false;
        if (std::abs (a.points[i].value   - b.points[i].value)   > tol) return false;
        if (std::abs (a.points[i].tension - b.points[i].tension) > tol) return false;
    }
    return true;
}

//==============================================================================
// Tests
//==============================================================================
static void test_defaultState_hasAhdsrSeededCurve()
{
    runHeader ("default state has AHDSR-seeded curve, mode=ahdsr");
    KickAssProcessor p;

    REQUIRE_EQ (p.getVolCurveMode(), juce::String ("ahdsr"));

    juce::SpinLock::ScopedLockType l (p.getVolCurveLock());
    const auto& c = p.getVolEnvCurve();
    REQUIRE (c.points.size() >= 2);
    REQUIRE_NEAR (c.points.front().timeMs, 0.0f, 1e-3f);
    REQUIRE_NEAR (c.points.back ().value , 0.0f, 1e-3f);
}

static void test_customCurve_roundTripsThroughState()
{
    runHeader ("custom curve survives getState/setState");
    KickAssProcessor src;

    // Make a clearly distinguishable custom curve.
    EnvCurve customC;
    customC.points = {
        { 0.0f,  0.0f, 0.4f },
        { 5.0f,  1.0f, 0.0f },
        { 50.0f, 0.7f, -0.3f },
        { 220.0f, 0.0f, 0.0f }
    };
    {
        juce::SpinLock::ScopedLockType l (src.getVolCurveLock());
        src.getVolEnvCurve() = customC;
    }
    src.syncVolCurveToValueTree();
    src.setVolCurveMode ("custom");

    juce::MemoryBlock blob;
    src.getStateInformation (blob);
    REQUIRE (blob.getSize() > 0);

    // Fresh processor, load the blob.
    KickAssProcessor dst;
    dst.setStateInformation (blob.getData(), (int) blob.getSize());

    REQUIRE_EQ (dst.getVolCurveMode(), juce::String ("custom"));

    juce::SpinLock::ScopedLockType l (dst.getVolCurveLock());
    REQUIRE (curvesEqual (customC, dst.getVolEnvCurve()));
}

static void test_oldPresetXml_withoutCurves_fallsBackToAhdsr()
{
    runHeader ("old preset XML (no <Curves>) → mode=ahdsr + curve rebuilt");
    KickAssProcessor src;

    // Capture the parameter root tag name (apvts state type).
    const juce::Identifier rootTag = src.apvts.state.getType();

    // Hand-construct an old-style state: parameters only, no <Curves>.
    juce::ValueTree fakeOld (rootTag);
    auto addParam = [&] (const char* id, float v)
    {
        juce::ValueTree pt ("PARAM");
        pt.setProperty ("id", id, nullptr);
        pt.setProperty ("value", v, nullptr);
        fakeOld.appendChild (pt, nullptr);
    };
    addParam ("vol_attack",  3.0f);
    addParam ("vol_hold",    20.0f);
    addParam ("vol_decay_1", 80.0f);
    addParam ("vol_sustain", 50.0f);
    addParam ("vol_decay_2", 200.0f);
    addParam ("vol_curve",   3.0f);
    addParam ("envelope_mode", 0.0f);   // Simple in this old preset

    auto xml = fakeOld.createXml();
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary (*xml, blob);

    KickAssProcessor dst;
    // Pre-mark dst as custom so we can prove the fallback path resets it.
    dst.setVolCurveMode ("custom");
    dst.setStateInformation (blob.getData(), (int) blob.getSize());

    REQUIRE_EQ (dst.getVolCurveMode(), juce::String ("ahdsr"));

    // Curve should reflect the AHDSR knobs we just loaded.
    juce::SpinLock::ScopedLockType l (dst.getVolCurveLock());
    const auto& c = dst.getVolEnvCurve();
    REQUIRE (c.points.size() >= 2);
    // attack=3, hold=20, decay_1=80, decay_2=200 → total ≈ 303 ms
    REQUIRE_NEAR (c.getTotalMs(), 303.0f, 1.0f);
}

static void test_applyFactory_resetsModeToAhdsr_evenIfWasCustom()
{
    runHeader ("applyFactory resets mode=ahdsr even if was custom");
    KickAssProcessor p;
    p.setVolCurveMode ("custom");
    REQUIRE_EQ (p.getVolCurveMode(), juce::String ("custom"));

    // Apply first factory preset (Psytrance Default).
    const bool ok = p.getPresetManager().applyByName ("Psytrance (Default)");
    REQUIRE (ok);
    REQUIRE_EQ (p.getVolCurveMode(), juce::String ("ahdsr"));
}

static void test_onEnterAdvancedMode_isNoOp_whenCustom()
{
    runHeader ("onEnterAdvancedMode does NOT overwrite a custom curve");
    KickAssProcessor p;

    EnvCurve userC;
    userC.points = {
        { 0.0f, 0.0f, 0.0f },
        { 10.0f, 1.0f, 0.5f },
        { 100.0f, 0.0f, 0.0f }
    };
    {
        juce::SpinLock::ScopedLockType l (p.getVolCurveLock());
        p.getVolEnvCurve() = userC;
    }
    p.syncVolCurveToValueTree();
    p.setVolCurveMode ("custom");

    p.onEnterAdvancedMode();   // should be no-op since mode=custom

    juce::SpinLock::ScopedLockType l (p.getVolCurveLock());
    REQUIRE (curvesEqual (userC, p.getVolEnvCurve()));
    // Lock released by RAII before the next assertion (no further curve access here).
    REQUIRE_EQ (p.getVolCurveMode(), juce::String ("custom"));
}

static void test_onEnterAdvancedMode_refreshes_whenAhdsr()
{
    runHeader ("onEnterAdvancedMode refreshes curve from AHDSR knobs when mode=ahdsr");
    KickAssProcessor p;
    REQUIRE_EQ (p.getVolCurveMode(), juce::String ("ahdsr"));

    // Mutate AHDSR knobs to something distinctive.
    auto setParam = [&] (const char* id, float v)
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
        {
            const float norm = rap->getNormalisableRange().convertTo0to1 (v);
            rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        }
    };
    setParam ("vol_attack",  1.0f);
    setParam ("vol_hold",    5.0f);
    setParam ("vol_decay_1", 30.0f);
    setParam ("vol_sustain", 25.0f);
    setParam ("vol_decay_2", 100.0f);
    setParam ("vol_curve",   2.0f);

    p.onEnterAdvancedMode();

    juce::SpinLock::ScopedLockType l (p.getVolCurveLock());
    REQUIRE_NEAR (p.getVolEnvCurve().getTotalMs(), 136.0f, 1.0f);   // 1+5+30+100
    // mode still "ahdsr" (a snapshot, still refreshable)
    // Re-check mode after releasing lock to keep things tidy isn't necessary —
    // getVolCurveMode reads apvts.state which is unrelated to volCurveLock.
}

//==============================================================================
// EnvCurve unit tests (data structure, no processor needed)
//==============================================================================
static void test_envCurve_lutMatchesSampleAtMs()
{
    runHeader ("EnvCurve fillLut matches sampleAtMs across the LUT range");
    EnvCurve c;
    c.points = {
        { 0.0f,   0.0f, 0.0f },
        { 20.0f,  1.0f, 0.3f },
        { 80.0f,  0.4f, -0.2f },
        { 200.0f, 0.0f, 0.0f }
    };

    std::array<float, EnvCurve::kLutSize> lut {};
    c.fillLut (lut);

    const float total = c.getTotalMs();
    const float step = total / (float) (EnvCurve::kLutSize - 1);
    for (int i = 0; i < EnvCurve::kLutSize; ++i)
    {
        const float expected = juce::jlimit (0.0f, 1.0f, c.sampleAtMs ((float) i * step));
        REQUIRE_NEAR (lut[(size_t) i], expected, 1e-5f);
    }
}

static void test_envCurve_invariants_sortDedupeClamp()
{
    runHeader ("EnvCurve::clampToInvariants sorts, dedupes ties, clamps values + tensions");
    EnvCurve c;
    // Out-of-order, value out of range, tension out of range, two points sharing t.
    c.points = {
        { 50.0f,  1.5f,  2.0f  },
        { 0.0f,   -0.3f, -3.0f },
        { 50.0f,  0.7f,  0.5f  },   // same t as first — should be bumped
        { 100.0f, 0.5f,  0.0f  }
    };
    c.clampToInvariants();

    // First locked to t=0, last locked to v=0.
    REQUIRE_NEAR (c.points.front().timeMs, 0.0f, 1e-3f);
    REQUIRE_NEAR (c.points.back ().value , 0.0f, 1e-3f);
    // Sorted strictly ascending.
    for (size_t i = 1; i < c.points.size(); ++i)
        REQUIRE (c.points[i].timeMs > c.points[i - 1].timeMs);
    // Values clamped to [0..1], tensions to [-1..+1].
    for (const auto& p : c.points)
    {
        REQUIRE (p.value   >= 0.0f && p.value   <= 1.0f);
        REQUIRE (p.tension >= -1.0f && p.tension <= 1.0f);
    }
}

static void test_envCurve_tensionExtremes()
{
    runHeader ("EnvCurve sampleSegment tension extremes behave sensibly");
    EnvPoint a { 0.0f, 0.0f, +1.0f };   // ease-out — fast start
    EnvPoint b { 100.0f, 1.0f, 0.0f };
    // Strong tension +1: at t=0.5 value should be well above 0.5 (fast start).
    REQUIRE (EnvCurve::sampleSegment (a, b, 0.5f) > 0.7f);
    // Endpoints exact.
    REQUIRE_NEAR (EnvCurve::sampleSegment (a, b, 0.0f), 0.0f, 1e-4f);
    REQUIRE_NEAR (EnvCurve::sampleSegment (a, b, 1.0f), 1.0f, 1e-4f);

    a.tension = -1.0f;  // ease-in — slow start
    REQUIRE (EnvCurve::sampleSegment (a, b, 0.5f) < 0.3f);

    a.tension = 0.0f;   // linear
    REQUIRE_NEAR (EnvCurve::sampleSegment (a, b, 0.5f), 0.5f, 1e-4f);
}

//==============================================================================
// File round-trip tests (.kickpreset / .json)
//==============================================================================
static void test_kickpresetFile_roundtripsCustomCurve()
{
    runHeader (".kickpreset file round-trips a custom curve");
    KickAssProcessor src;

    EnvCurve customC;
    customC.points = {
        { 0.0f,   0.0f,  0.2f  },
        { 8.0f,   1.0f,  0.0f  },
        { 65.0f,  0.55f, -0.4f },
        { 180.0f, 0.0f,  0.0f  }
    };
    {
        juce::SpinLock::ScopedLockType l (src.getVolCurveLock());
        src.getVolEnvCurve() = customC;
    }
    src.syncVolCurveToValueTree();
    src.setVolCurveMode ("custom");

    auto tmp = juce::File::createTempFile (".kickpreset");
    REQUIRE (src.getPresetManager().saveKickPreset (tmp));

    KickAssProcessor dst;
    REQUIRE (dst.getPresetManager().loadKickPreset (tmp));
    REQUIRE_EQ (dst.getVolCurveMode(), juce::String ("custom"));

    juce::SpinLock::ScopedLockType l (dst.getVolCurveLock());
    REQUIRE (curvesEqual (customC, dst.getVolEnvCurve()));

    tmp.deleteFile();
}

static void test_jsonFile_resetsCurveModeToAhdsr()
{
    runHeader (".json file load resets curve mode to ahdsr (JSON has no curve concept)");
    KickAssProcessor src;
    auto tmp = juce::File::createTempFile (".json");
    REQUIRE (src.getPresetManager().saveJson (tmp));

    KickAssProcessor dst;
    dst.setVolCurveMode ("custom");   // pretend dst was customized
    REQUIRE (dst.getPresetManager().loadJson (tmp));
    REQUIRE_EQ (dst.getVolCurveMode(), juce::String ("ahdsr"));

    tmp.deleteFile();
}

//==============================================================================
int main (int /*argc*/, char** /*argv*/)
{
    juce::MessageManager::getInstance();   // JUCE expects one to exist for some ValueTree paths

    test_defaultState_hasAhdsrSeededCurve();
    test_customCurve_roundTripsThroughState();
    test_oldPresetXml_withoutCurves_fallsBackToAhdsr();
    test_applyFactory_resetsModeToAhdsr_evenIfWasCustom();
    test_onEnterAdvancedMode_isNoOp_whenCustom();
    test_onEnterAdvancedMode_refreshes_whenAhdsr();

    test_envCurve_lutMatchesSampleAtMs();
    test_envCurve_invariants_sortDedupeClamp();
    test_envCurve_tensionExtremes();

    test_kickpresetFile_roundtripsCustomCurve();
    test_jsonFile_resetsCurveModeToAhdsr();

    if (g_failures == 0)
    {
        std::cout << "\nALL TESTS PASSED\n";
        juce::MessageManager::deleteInstance();
        return 0;
    }

    std::cerr << "\n" << g_failures << " FAILURE(S)\n";
    juce::MessageManager::deleteInstance();
    return 1;
}
