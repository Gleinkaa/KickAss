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

// B10 — a <Curve id="vol_env"> node with 0 or 1 <Pt> children is corrupt or a
// placeholder. The old code accepted it; clampToInvariants() turned it into a
// flat-zero 2-point curve → permanent silence in Advanced mode. It must fall
// through to the AHDSR rebuild instead.
static void test_degenerateCurveNode_fallsBackToAhdsr()
{
    runHeader ("degenerate <Curve> node (0/1 Pt) falls back to AHDSR rebuild");

    auto makeBlobWithDegenerateCurve = [] (const juce::Identifier& rootTag, int numPts)
    {
        juce::ValueTree fake (rootTag);
        auto addParam = [&] (const char* id, float v)
        {
            juce::ValueTree pt ("PARAM");
            pt.setProperty ("id", id, nullptr);
            pt.setProperty ("value", v, nullptr);
            fake.appendChild (pt, nullptr);
        };
        addParam ("vol_attack",  3.0f);
        addParam ("vol_hold",    20.0f);
        addParam ("vol_decay_1", 80.0f);
        addParam ("vol_sustain", 50.0f);
        addParam ("vol_decay_2", 200.0f);
        addParam ("vol_curve",   3.0f);
        addParam ("envelope_mode", 1.0f);   // Advanced — the mode that went silent

        juce::ValueTree curves ("Curves");
        juce::ValueTree curve ("Curve");
        curve.setProperty ("id", "vol_env", nullptr);
        curve.setProperty ("mode", "custom", nullptr);   // pre-fix this was preserved
        for (int i = 0; i < numPts; ++i)
        {
            juce::ValueTree pt ("Pt");
            pt.setProperty ("t", 50.0f, nullptr);
            pt.setProperty ("v", 1.0f, nullptr);
            pt.setProperty ("tn", 0.0f, nullptr);
            curve.addChild (pt, -1, nullptr);
        }
        curves.addChild (curve, -1, nullptr);
        fake.addChild (curves, -1, nullptr);

        auto xml = fake.createXml();
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
        return blob;
    };

    for (int numPts : { 0, 1 })
    {
        KickAssProcessor src;
        const auto blob = makeBlobWithDegenerateCurve (src.apvts.state.getType(), numPts);

        KickAssProcessor dst;
        dst.setStateInformation (blob.getData(), (int) blob.getSize());

        // The AHDSR knobs from the state must have been used to rebuild the
        // curve: attack=3, hold=20, decay_1=80, decay_2=200 → total ≈ 303 ms,
        // 5 points with a 1.0 peak — NOT the flat-zero 2-point curve the old
        // code produced from the degenerate node.
        const juce::String tag = juce::String (numPts) + " Pt";
        {
            juce::SpinLock::ScopedLockType l (dst.getVolCurveLock());
            const auto& c = dst.getVolEnvCurve();
            REQUIRE_MSG (c.points.size() > 2,
                         "B10 (" + tag + "): curve not rebuilt from AHDSR — points="
                         + juce::String ((int) c.points.size()));
            float peak = 0.0f;
            for (const auto& pt : c.points)
                peak = juce::jmax (peak, pt.value);
            REQUIRE_MSG (peak > 0.5f,
                         "B10 (" + tag + "): curve is flat-zero — Advanced mode silent (peak="
                         + juce::String (peak) + ")");
            REQUIRE_NEAR (c.getTotalMs(), 303.0f, 1.0f);
        }

        // A degenerate node is treated as "not customized": mode resets so a
        // later Simple→Advanced flip refreshes from the knobs.
        REQUIRE_EQ (dst.getVolCurveMode(), juce::String ("ahdsr"));

        // End-to-end: Advanced mode must actually make sound with the rebuilt
        // curve (the old flat-zero curve rendered silence).
        juce::AudioBuffer<float> buf;
        dst.offlineRender (buf, 400.0);
        float peak = 0.0f;
        for (int i = 0; i < buf.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));
        REQUIRE_MSG (peak > 1.0e-4f,
                     "B10 (" + tag + "): Advanced-mode render silent with degenerate curve node");
    }
}

// B7 — A/B swap must not wipe the undo history. AudioProcessorValueTreeState::
// replaceState() calls clearUndoHistory(), so doToggleAB used to kill undo on
// every press. applyStatePreservingUndo() applies the other slot's state inside
// ONE undo transaction instead: the swap is itself one undo step, edits made
// before the toggle stay undoable, and undo restores the pre-swap parameter
// values and <Curves>/<Sample> children.
static void test_abSwap_preservesUndoHistory()
{
    runHeader ("A/B swap preserves the undo history (B7)");
    KickAssProcessor p;

    // The watched parameter (skewed float range, denormalised default 8000 Hz).
    auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter ("start_freq"));
    REQUIRE (rap != nullptr);
    const auto currentValue = [&p] { return p.apvts.getRawParameterValue ("start_freq")->load(); };

    // Find the <PARAM id="..."> child of a state snapshot (APVTS's own layout).
    const auto findParamChild = [] (const juce::ValueTree& state,
                                    const char* paramId) -> juce::ValueTree
    {
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto c = state.getChild (i);
            if (c.hasType ("PARAM") && c.getProperty ("id").toString() == paramId)
                return c;
        }
        return {};
    };

    // Find <Curves>/<Curve id="vol_env"> in a state snapshot.
    const auto findVolEnvCurve = [] (const juce::ValueTree& state) -> juce::ValueTree
    {
        const auto curves = state.getChildWithName ("Curves");
        if (curves.isValid())
            for (int i = 0; i < curves.getNumChildren(); ++i)
            {
                const auto c = curves.getChild (i);
                if (c.getType().toString() == "Curve"
                    && c.getProperty ("id").toString() == "vol_env")
                    return c;
            }
        return {};
    };

    // ---- Ground truth: PARAM children store the DENORMALISED value ----------
    // applyStatePreservingUndo() must read the "value" property exactly the way
    // APVTS does (JUCE 8's ParameterAdapter::flushToTree writes the
    // denormalised value into the tree). This doubles as a tripwire: if a JUCE
    // upgrade ever changes the storage convention, this fails first with a
    // clear message instead of the swap silently misapplying values.
    {
        (void) p.apvts.copyState();   // ensure the PARAM children + values exist
        rap->setValueNotifyingHost (rap->convertTo0to1 (1000.0f));
        const auto state = p.apvts.copyState();   // flush: adapter writes its value into the child
        const auto pc = findParamChild (state, "start_freq");
        REQUIRE (pc.isValid());
        const double stored = (double) pc.getProperty ("value");
        REQUIRE_MSG (std::abs (stored - 1000.0) < 0.5,
                     "B7: PARAM children must store the DENORMALISED value (stored="
                     + juce::String (stored) + ")");
        REQUIRE_NEAR (currentValue(), 1000.0f, 0.5f);
    }

    // ---- 1) Undoable edit BEFORE the toggle — the editor way: open a
    // transaction, then push the value. The undo record is created when APVTS
    // flushes the parameter into its PARAM child; the GUI has a 10 Hz timer
    // for that, headless tests have no event loop, so flush explicitly.
    const float preSwapValue = 2000.0f;
    p.getUndoManager().beginNewTransaction ("edit before A/B");
    rap->setValueNotifyingHost (rap->convertTo0to1 (preSwapValue));
    (void) p.apvts.copyState();   // flush → the edit lands on the undo stack
    REQUIRE (p.canUndo());
    REQUIRE_NEAR (currentValue(), preSwapValue, 0.5f);

    // ---- 2) The "other slot": differing parameter value + <Curves> child ----
    auto other = p.apvts.copyState();
    const float swappedValue = 5000.0f;
    findParamChild (other, "start_freq").setProperty ("value", swappedValue, nullptr);
    auto otherCurve = findVolEnvCurve (other);
    REQUIRE (otherCurve.isValid());
    otherCurve.setProperty ("mode", "custom", nullptr);   // visible marker for the child swap

    // ---- 3) The swap ---------------------------------------------------------
    p.applyStatePreservingUndo (other);

    REQUIRE_NEAR (currentValue(), swappedValue, 0.5f);   // parameter took the new value
    {
        const auto liveCurve = findVolEnvCurve (p.apvts.state);
        REQUIRE (liveCurve.isValid());
        REQUIRE_EQ (liveCurve.getProperty ("mode").toString(),
                    juce::String ("custom"));            // <Curves> child swapped
    }
    // Structurally valid: the live state now matches the target state.
    REQUIRE (p.apvts.copyState().isEquivalentTo (other));

    // ---- 4) The swap was RECORDED, not a history wipe -------------------------
    REQUIRE (p.canUndo());
    p.undo();
    REQUIRE_NEAR (currentValue(), preSwapValue, 0.5f);   // undo steps back over the SWAP
    REQUIRE_EQ (findVolEnvCurve (p.apvts.state).getProperty ("mode").toString(),
                juce::String ("ahdsr"));                 // child swap undone too
    p.undo();
    REQUIRE_NEAR (currentValue(), 1000.0f, 0.5f);        // the pre-swap edit is still a separate undo step

    // ---- 5) <Sample> node swap semantics --------------------------------------
    // A target without <Sample> removes the node; a target with a path keeps
    // it (B3/B4: engines are cleared when the file is missing, but the node
    // survives so a save/load round-trip doesn't destroy the path).
    {
        juce::ValueTree snode ("Sample");   // the way loadTransientSampleFile stores it
        snode.setProperty ("id", "transient", nullptr);
        snode.setProperty ("path", "/nonexistent/kickass-b7-original.wav", nullptr);
        p.apvts.state.appendChild (snode, nullptr);
        REQUIRE (p.apvts.state.getChildWithName ("Sample").isValid());

        // Target WITHOUT <Sample> → node removed from the live state.
        auto noSample = p.apvts.copyState();
        noSample.removeChild (noSample.getChildWithName ("Sample"), nullptr);
        REQUIRE (! noSample.getChildWithName ("Sample").isValid());
        p.applyStatePreservingUndo (noSample);
        REQUIRE (! p.apvts.state.getChildWithName ("Sample").isValid());

        // Target WITH <Sample path> → node present, path kept.
        auto withSample = p.apvts.copyState();
        juce::ValueTree kept ("Sample");
        kept.setProperty ("id", "transient", nullptr);
        kept.setProperty ("path", "/nonexistent/kickass-b7-kept.wav", nullptr);
        withSample.appendChild (kept, nullptr);
        p.applyStatePreservingUndo (withSample);
        const auto liveSample = p.apvts.state.getChildWithName ("Sample");
        REQUIRE (liveSample.isValid());
        REQUIRE_EQ (liveSample.getProperty ("path").toString(),
                    juce::String ("/nonexistent/kickass-b7-kept.wav"));
    }
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

// A corrupt state (mangled XML: t="nan", v="-inf", ...) must not survive
// clampToInvariants(): a NaN total time means the LUT is never filled and the
// voice never ends.
static void test_envCurve_nonFiniteStateIsSanitized()
{
    runHeader ("EnvCurve::clampToInvariants flushes NaN/Inf points from a corrupt state");

    // Build a <Curve> the way a mangled preset looks: string properties that
    // JUCE's String -> double reader parses as nan/inf, plus a native NaN
    // double as an in-memory state would hold.
    juce::ValueTree curve ("Curve");
    curve.setProperty ("id", "vol_env", nullptr);

    auto addPt = [] (juce::ValueTree& parent, const juce::var& t,
                     const juce::var& v, const juce::var& tn)
    {
        juce::ValueTree pt ("Pt");
        pt.setProperty ("t",  t,  nullptr);
        pt.setProperty ("v",  v,  nullptr);
        pt.setProperty ("tn", tn, nullptr);
        parent.addChild (pt, -1, nullptr);
    };
    addPt (curve, juce::String ("nan"),  0.0f,  0.0f);
    addPt (curve, 10.0f,                 1.0f,  juce::String ("inf"));
    addPt (curve, juce::String ("inf"),  juce::String ("-inf"), 0.0f);
    addPt (curve, 40.0f,                 0.5f,  0.0f);
    addPt (curve, std::numeric_limits<double>::quiet_NaN(), 0.0f, 0.0f);

    EnvCurve c;
    c.fromValueTree (curve);
    REQUIRE_EQ ((int) c.points.size(), 5);

    // Precondition: the payload really did produce non-finite data, otherwise
    // this test would not be exercising the sanitizer at all.
    bool sawNonFinite = false;
    for (const auto& p : c.points)
        if (! std::isfinite (p.timeMs) || ! std::isfinite (p.value) || ! std::isfinite (p.tension))
            sawNonFinite = true;
    REQUIRE (sawNonFinite);

    c.clampToInvariants();

    REQUIRE (std::isfinite (c.getTotalMs()));
    REQUIRE (c.getTotalMs() >= 0.0f);

    for (const auto& p : c.points)
    {
        REQUIRE (std::isfinite (p.timeMs));
        REQUIRE (std::isfinite (p.value));
        REQUIRE (std::isfinite (p.tension));
    }

    // Still a usable, well-formed envelope afterwards.
    REQUIRE_NEAR (c.points.front().timeMs, 0.0f, 1e-6f);
    for (size_t i = 1; i < c.points.size(); ++i)
        REQUIRE (c.points[i].timeMs > c.points[i - 1].timeMs);

    std::array<float, EnvCurve::kLutSize> lut {};
    c.fillLut (lut);
    for (auto v : lut)
        REQUIRE (std::isfinite (v));
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
    test_degenerateCurveNode_fallsBackToAhdsr();     // B10
    test_abSwap_preservesUndoHistory();              // B7
    test_applyFactory_resetsModeToAhdsr_evenIfWasCustom();
    test_onEnterAdvancedMode_isNoOp_whenCustom();
    test_onEnterAdvancedMode_refreshes_whenAhdsr();

    test_envCurve_lutMatchesSampleAtMs();
    test_envCurve_invariants_sortDedupeClamp();
    test_envCurve_nonFiniteStateIsSanitized();
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
