// RenderWav headless test — verifies KickAssProcessor::renderToWavFile, the
// shared render-to-disk path behind EXPORT WAV and the v1.1 drag-out affordance.
// Constructs the processor (no GUI), renders a few presets to a temp WAV, and
// checks the file is a valid, readable, non-silent, finite 24-bit/48k stereo WAV.
//
// Mirrors DspSafetyTest.cpp (own REQUIRE macros, g_failures, MessageManager).
//
// Build/run:
//   cmake --build build --config Release --target KickAss_RenderTests
//   build/KickAss_RenderTests_artefacts/Release/KickAss_RenderTests.exe
//
// Returns 0 on PASS, 1 on FAIL.

#include "../Source/PluginProcessor.h"
#include "../Source/PresetManager.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
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

static void runHeader (const char* name) { std::cout << "---- " << name << " ----" << std::endl; }

//==============================================================================
// Read a WAV back and assert it's a sane render. Returns the read sample count.
static void verifyWav (const juce::File& f, double expectedMs)
{
    REQUIRE_MSG (f.existsAsFile(), "WAV not written: " + f.getFullPathName());
    REQUIRE_MSG (f.getSize() > 44, "WAV smaller than a header");

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (f));
    REQUIRE_MSG (reader != nullptr, "WAV not readable by JUCE");
    if (reader == nullptr)
        return;

    REQUIRE_MSG (reader->numChannels == 2, "expected stereo");
    REQUIRE_MSG (juce::approximatelyEqual (reader->sampleRate, 48000.0), "expected 48 kHz");
    REQUIRE_MSG (reader->bitsPerSample == 24, "expected 24-bit");

    const int n = (int) reader->lengthInSamples;
    REQUIRE_MSG (n > 0, "empty render");

    // Length should be in the ballpark of the requested duration (48 samples/ms).
    const int expectedSamples = (int) std::round (expectedMs * 48.0);
    REQUIRE_MSG (std::abs (n - expectedSamples) <= 48, // within ~1 ms
                 juce::String (n) + " samples vs ~" + juce::String (expectedSamples));

    juce::AudioBuffer<float> buf (2, n);
    reader->read (&buf, 0, n, 0, true, true);

    bool anyNonZero = false;
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float s = buf.getSample (ch, i);
            REQUIRE_MSG (std::isfinite (s), "non-finite sample in WAV");
            if (s != 0.0f) anyNonZero = true;
            peak = juce::jmax (peak, std::abs (s));
        }

    REQUIRE_MSG (anyNonZero, "render is pure silence");
    REQUIRE_MSG (peak <= 1.0001f, "render exceeds full scale: peak=" + juce::String (peak));
}

//==============================================================================
static void test_renderToWavFile_writesValidWav()
{
    runHeader ("renderToWavFile writes a valid 24-bit/48k stereo WAV");

    KickAssProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("KickAss_RenderTest_"
                          + juce::String (juce::Random::getSystemRandom().nextInt()));

    // A couple of representative durations.
    for (double ms : { 300.0, 800.0 })
    {
        auto f = tmpDir.getChildFile ("render_" + juce::String ((int) ms) + ".wav");
        const bool ok = proc.renderToWavFile (f, ms);
        REQUIRE_MSG (ok, "renderToWavFile returned false for " + juce::String (ms) + " ms");
        verifyWav (f, ms);
    }

    tmpDir.deleteRecursively();
}

static void test_renderToWavFile_acrossPresets()
{
    runHeader ("renderToWavFile succeeds across factory presets");

    KickAssProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto& pm = proc.getPresetManager();
    auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("KickAss_RenderTestPresets_"
                          + juce::String (juce::Random::getSystemRandom().nextInt()));

    for (auto& name : pm.getFactoryNames())
    {
        pm.applyByName (name);
        auto f = tmpDir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
        const bool ok = proc.renderToWavFile (f, 400.0);
        REQUIRE_MSG (ok, "render failed for preset " + name);
        verifyWav (f, 400.0);
    }

    tmpDir.deleteRecursively();
}

//==============================================================================
int main (int, char**)
{
    juce::MessageManager::getInstance();

    test_renderToWavFile_writesValidWav();
    test_renderToWavFile_acrossPresets();

    if (g_failures == 0)
    {
        std::cout << "\nALL RENDER-WAV TESTS PASSED\n";
        juce::MessageManager::deleteInstance();
        return 0;
    }

    std::cerr << "\n" << g_failures << " FAILURE(S)\n";
    juce::MessageManager::deleteInstance();
    return 1;
}
