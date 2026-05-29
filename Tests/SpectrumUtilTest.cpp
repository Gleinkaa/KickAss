// SpectrumUtil headless test — verifies the GUI-free FFT helper used by the
// visualizer's spectrum view. Mirrors the style of DspSafetyTest.cpp (its own
// REQUIRE macros, g_failures, return 0/1). No MessageManager needed: the helper
// only touches juce_dsp + std, no GUI/message-thread state.
//
// Build/run:
//   cmake --build build --config Release --target KickAss_SpectrumTests
//   build/KickAss_SpectrumTests_artefacts/Release/KickAss_SpectrumTests.exe
//
// Returns 0 on PASS, 1 on FAIL.

#include "../Source/SpectrumUtil.h"
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <vector>
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
#define REQUIRE_NEAR(a, b, tol) do { auto _a = (double) (a); auto _b = (double) (b); if (std::abs (_a - _b) > (tol)) \
    reportFail (#a " ~= " #b, __FILE__, __LINE__, juce::String (_a) + " vs " + juce::String (_b)); } while (0)

static juce::String runHeader (const char* name)
{
    std::cout << "---- " << name << " ----" << std::endl;
    return name;
}

//==============================================================================
// Helpers
//==============================================================================
static std::vector<float> makeSine (double freqHz, double sampleRate, int numSamples, float amp = 1.0f)
{
    std::vector<float> v ((size_t) numSamples);
    const double w = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i)
        v[(size_t) i] = amp * (float) std::sin (w * (double) i);
    return v;
}

static int peakBin (const kickass::SpectrumResult& r)
{
    int best = -1;
    float bestVal = -1.0e9f;
    for (size_t b = 0; b < r.magsDb.size(); ++b)
        if (r.magsDb[b] > bestVal) { bestVal = r.magsDb[b]; best = (int) b; }
    return best;
}

//==============================================================================
// Test 1 — pure 1000 Hz sine: peak bin center freq within one bin of 1000 Hz.
//==============================================================================
static void test_sinePeakAt1000Hz()
{
    runHeader ("1000 Hz sine -> peak bin within one bin of 1000 Hz");
    const double sr = 48000.0;
    const int    n  = 16384;
    auto sig = makeSine (1000.0, sr, n);

    auto r = kickass::computeSpectrum (sig.data(), n, sr);
    REQUIRE_MSG (! r.magsDb.empty(), "empty result for valid sine");
    REQUIRE_MSG (r.binHz > 0.0, "binHz not set");

    const int pk = peakBin (r);
    REQUIRE_MSG (pk >= 0, "no peak bin found");
    if (pk >= 0)
    {
        const double peakHz = (double) pk * r.binHz;
        REQUIRE_NEAR (peakHz, 1000.0, r.binHz);   // within one bin
    }
}

//==============================================================================
// Test 2 — 50 Hz vs 5000 Hz resolve to clearly different (and ordered) peak bins.
//==============================================================================
static void test_lowVsHighDistinctBins()
{
    runHeader ("50 Hz vs 5000 Hz -> clearly different peak bins");
    const double sr = 48000.0;
    const int    n  = 16384;

    auto low  = makeSine (50.0,   sr, n);
    auto high = makeSine (5000.0, sr, n);

    auto rLow  = kickass::computeSpectrum (low.data(),  n, sr);
    auto rHigh = kickass::computeSpectrum (high.data(), n, sr);

    REQUIRE_MSG (! rLow.magsDb.empty()  && ! rHigh.magsDb.empty(), "empty result");

    const int pkLow  = peakBin (rLow);
    const int pkHigh = peakBin (rHigh);
    REQUIRE_MSG (pkLow >= 0 && pkHigh >= 0, "peak bin not found");
    REQUIRE_MSG (pkHigh > pkLow, "high-freq peak bin not above low-freq peak bin");
    // Comfortably separated (not adjacent bins).
    REQUIRE_MSG ((pkHigh - pkLow) > 10, "peak bins not clearly separated: low="
                 + juce::String (pkLow) + " high=" + juce::String (pkHigh));
}

//==============================================================================
// Test 3 — zero / empty input -> empty result, no crash, no NaN.
//==============================================================================
static void test_zeroAndEmptyInput()
{
    runHeader ("zero / empty / null input -> empty result, no NaN");
    const double sr = 48000.0;

    // All-zero input.
    std::vector<float> zeros (4096, 0.0f);
    auto rZero = kickass::computeSpectrum (zeros.data(), (int) zeros.size(), sr);
    REQUIRE_MSG (rZero.magsDb.empty(), "all-zero input should give empty result");

    // numSamples == 0.
    auto rEmpty = kickass::computeSpectrum (zeros.data(), 0, sr);
    REQUIRE_MSG (rEmpty.magsDb.empty(), "zero numSamples should give empty result");

    // null pointer.
    auto rNull = kickass::computeSpectrum (nullptr, 1024, sr);
    REQUIRE_MSG (rNull.magsDb.empty(), "null samples should give empty result");

    // No NaN/Inf anywhere in the (empty) results — trivially true, but assert the
    // contract holds for a valid-but-tiny render too.
    std::vector<float> tiny = { 0.0f, 1.0e-9f, 0.0f, -1.0e-9f, 0.0f, 0.0f, 0.0f, 0.0f };
    auto rTiny = kickass::computeSpectrum (tiny.data(), (int) tiny.size(), sr);
    for (auto db : rTiny.magsDb)
        REQUIRE_MSG (std::isfinite (db), "non-finite dB in tiny render");
}

//==============================================================================
// Test 4 — all magsDb finite and <= 0 (normalized to peak 0 dB) within epsilon.
//==============================================================================
static void test_allMagsFiniteAndBelowZero()
{
    runHeader ("all magsDb finite and <= 0 dB (normalized peak)");
    const double sr = 48000.0;
    const int    n  = 8192;

    // A richer signal (two tones) to exercise many bins.
    auto a = makeSine (220.0,  sr, n, 0.7f);
    auto b = makeSine (3300.0, sr, n, 0.4f);
    std::vector<float> mix ((size_t) n);
    for (int i = 0; i < n; ++i) mix[(size_t) i] = a[(size_t) i] + b[(size_t) i];

    auto r = kickass::computeSpectrum (mix.data(), n, sr);
    REQUIRE_MSG (! r.magsDb.empty(), "empty result for mixed signal");

    constexpr float eps = 1.0e-3f;
    float maxSeen = -1.0e9f;
    for (auto db : r.magsDb)
    {
        REQUIRE_MSG (std::isfinite (db), "non-finite dB value");
        REQUIRE_MSG (db <= eps, "dB value above 0: " + juce::String (db));
        REQUIRE_MSG (db >= -120.0f - eps, "dB value below floor: " + juce::String (db));
        if (db > maxSeen) maxSeen = db;
    }
    // The normalization guarantees the loudest bin sits at ~0 dB.
    REQUIRE_NEAR (maxSeen, 0.0, eps);
}

//==============================================================================
int main (int, char**)
{
    test_sinePeakAt1000Hz();
    test_lowVsHighDistinctBins();
    test_zeroAndEmptyInput();
    test_allMagsFiniteAndBelowZero();

    if (g_failures == 0)
    {
        std::cout << "\nALL SPECTRUM UTIL TESTS PASSED\n";
        return 0;
    }

    std::cerr << "\n" << g_failures << " FAILURE(S)\n";
    return 1;
}
