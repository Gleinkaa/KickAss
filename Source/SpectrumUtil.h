#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

//==============================================================================
// SpectrumUtil — header-only, GUI-free FFT helper for the visualizer's spectrum
// view and its headless test. Pure functions: no shared state, no audio-thread
// use. Depends only on juce_dsp + std.
//==============================================================================
namespace kickass
{
    struct SpectrumResult
    {
        std::vector<float> magsDb;   // one entry per FFT bin (0..fftSize/2), dB, normalized peak = 0 dB
        double binHz   = 0.0;        // frequency width of one bin = sampleRate / fftSize
        int    fftSize = 0;
    };

    //==========================================================================
    // Convert a mono time-domain buffer into normalized log-magnitude (dB) data.
    //   - Takes the first min(numSamples, maxFft) samples.
    //   - Applies a Hann window.
    //   - Zero-pads up to the next power of two (capped at maxFft).
    //   - Runs a real FFT, computes magnitude = sqrt(re^2+im^2).
    //   - Converts to dB, normalizes so the loudest bin is 0 dB, floors at -120 dB.
    //   - Empty / zero / non-finite-only input -> empty result (no NaN, no crash).
    //==========================================================================
    inline SpectrumResult computeSpectrum (const float* samples, int numSamples,
                                           double sampleRate, int maxFft = 32768)
    {
        SpectrumResult result;

        if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0 || maxFft < 2)
            return result;

        // Determine usable sample count, then the FFT size = next pow2 >= that,
        // capped at maxFft (which we also round down to a power of two).
        auto floorPow2 = [] (int v) noexcept
        {
            int p = 1;
            while ((p << 1) <= v) p <<= 1;
            return p;
        };
        auto ceilPow2 = [] (int v) noexcept
        {
            int p = 1;
            while (p < v) p <<= 1;
            return p;
        };

        const int maxPow2 = floorPow2 (maxFft);                  // e.g. 32768
        const int usable   = juce::jmin (numSamples, maxPow2);
        const int fftSize  = juce::jmin (maxPow2, ceilPow2 (usable));
        if (fftSize < 2) return result;

        const int order = (int) std::round (std::log2 ((double) fftSize));

        // Detect all-zero / non-finite input -> empty result.
        bool anyNonZeroFinite = false;
        for (int i = 0; i < usable; ++i)
        {
            const float s = samples[i];
            if (std::isfinite (s) && s != 0.0f) { anyNonZeroFinite = true; break; }
        }
        if (! anyNonZeroFinite) return result;

        // Windowed, zero-padded real input laid into a 2*fftSize buffer for
        // performFrequencyOnlyForwardTransform.
        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);

        juce::dsp::WindowingFunction<float> window (
            (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);

        for (int i = 0; i < usable; ++i)
        {
            const float s = samples[i];
            fftData[(size_t) i] = std::isfinite (s) ? s : 0.0f;
        }
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const int numBins = fftSize / 2 + 1;
        result.magsDb.resize ((size_t) numBins);
        result.fftSize = fftSize;
        result.binHz   = sampleRate / (double) fftSize;

        // performFrequencyOnlyForwardTransform writes magnitudes (not dB) into
        // the first fftSize/2 (+1) entries. Convert to dB, track the max.
        float maxDb = -300.0f;
        for (int b = 0; b < numBins; ++b)
        {
            const float mag = fftData[(size_t) b];
            const float db  = juce::Decibels::gainToDecibels (mag, -300.0f);
            result.magsDb[(size_t) b] = db;
            if (db > maxDb) maxDb = db;
        }

        // Normalize so the loudest bin = 0 dB, clamp floor at -120 dB.
        for (auto& db : result.magsDb)
            db = juce::jlimit (-120.0f, 0.0f, db - maxDb);

        return result;
    }
}
