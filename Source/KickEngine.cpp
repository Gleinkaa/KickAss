#include "KickEngine.h"
#include <cmath>
#include <algorithm>

//==============================================================================
// Constants
namespace
{
    constexpr int   kOversampleLog2 = 2;        // 4x
    constexpr float kTwoPi          = 6.28318530717958647692f;
    constexpr float kDcBlockerCutHz = 8.0f;     // always-on rumble killer
    constexpr float kSoftClipCeilDb = -0.3f;    // ARCHITECTURE §2 step 10

    inline float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    // Safe pow(x, p) for x in [0..1], p > 0. Returns 0 if x==0 (avoid 0^negative).
    inline float safePow01 (float x, float p) noexcept
    {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return std::pow (x, p);
    }
}

//==============================================================================
void KickEngine::prepare (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    blockSize = juce::jmax (samplesPerBlock, 32);

    // Allocate scratch
    dryScratch.assign ((size_t) blockSize, 0.0f);

    // Oversampler — single mono channel, 4x via two stages of polyphase IIR (low latency).
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, kOversampleLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,    // isMaxQuality
        false);  // useIntegerLatency
    oversampler->initProcessing ((size_t) blockSize);
    oversamplerLatency = (int) std::ceil (oversampler->getLatencyInSamples());

    // DC blocker: R = exp(-2π·fc/fs) for a single-pole HP — use 1 - 2π·fc/fs (close enough at low fc).
    dcR = 1.0f - (kTwoPi * kDcBlockerCutHz / (float) sampleRate);

    // Crossfade tail buffer
    crossfadeLen = juce::jmax (8, (int) (crossfadeMs * sampleRate * 0.001));
    crossfadeTail.assign ((size_t) crossfadeLen, 0.0f);

    reset();
}

void KickEngine::reset()
{
    active.store (false);
    playbackPos.store (0);
    phase = 0.0;
    sampleSinceTrigger = 0;
    totalSamples = 0;
    velocity = 1.0f;
    pitchTransposeRatio = 1.0f;

    clickSamplesLeft = 0;
    clickTotalSamples = 0;
    clickPhase = 0.0;
    clickHpfPrevIn = clickHpfPrevOut = 0.0f;
    clickLpfPrev = 0.0f;

    dcPrevIn = dcPrevOut = 0.0f;

    crossfadeRemaining = 0;
    std::fill (crossfadeTail.begin(), crossfadeTail.end(), 0.0f);

    if (oversampler)
        oversampler->reset();
}

int KickEngine::getLatencySamples() const noexcept
{
    return oversamplerLatency;
}

//==============================================================================
int KickEngine::computeTotalSamples() const noexcept
{
    const float totalMs = params.volAttackMs + params.volHoldMs
                        + params.volDecay1Ms + params.volDecay2Ms;
    return juce::jmax (1, (int) std::ceil ((totalMs * 0.001f) * (float) sampleRate));
}

//==============================================================================
void KickEngine::triggerNote (int midiNote, float vel, int /*sampleOffset*/) noexcept
{
    // Snapshot a short tail of the previous voice for crossfade-on-retrigger.
    if (active.load() && crossfadeRemaining == 0)
    {
        // Cheap: just render the next crossfadeLen samples of the prior voice into the tail.
        for (int i = 0; i < crossfadeLen; ++i)
            crossfadeTail[(size_t) i] = renderOneDrySample();
        crossfadeRemaining = crossfadeLen;
    }

    // Reset voice state — phase = 0 gives a clean zero-crossing start.
    phase = 0.0;
    sampleSinceTrigger = 0;
    totalSamples = computeTotalSamples();
    velocity = juce::jlimit (0.0f, 1.0f, vel);
    active.store (true);
    playbackPos.store (0);

    // Pitch tracking: if pitch_track > 0, transpose the pitch envelope by (midi - 36)/12 semitones × tracking
    const float semitoneShift = ((float) midiNote - 36.0f) * params.pitchTrack;
    pitchTransposeRatio = std::pow (2.0f, semitoneShift / 12.0f);

    // Click state
    clickTotalSamples = juce::jmax (1, (int) std::ceil ((params.clickDecayMs * 0.001f) * sampleRate));
    clickSamplesLeft = clickTotalSamples;
    clickPhase = 0.0;
    clickHpfPrevIn = clickHpfPrevOut = 0.0f;
    clickLpfPrev = 0.0f;

    // Do NOT reset DC blocker state — it should ringdown naturally and helps mask the click.
}

//==============================================================================
// Per-sample DSP — body + click + scoop, BEFORE drive/oversample/DC/clip/gain.
// Returns one mono sample. Advances voice state (phase, sampleSinceTrigger, click counters).
//==============================================================================
float KickEngine::renderOneDrySample() noexcept
{
    if (! active.load() && crossfadeRemaining == 0)
        return 0.0f;

    const float t      = (float) sampleSinceTrigger / (float) sampleRate;        // seconds since trigger
    const float t1     = params.sweepTime1Ms * 0.001f;
    const float t2     = params.sweepTime2Ms * 0.001f;
    const float pCurve = juce::jmax (0.01f, params.pitchCurve);

    // -------- 1. Pitch envelope (matches BazzismRebuild.py:310-322) --------
    float instFreq;
    if (t < t1)
    {
        const float x = (t1 > 0.0f) ? juce::jlimit (0.0f, 1.0f, t / t1) : 1.0f;
        instFreq = params.midFreq + (params.startFreq - params.midFreq) * safePow01 (1.0f - x, pCurve);
    }
    else if (t < t1 + t2)
    {
        const float x = (t2 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - t1) / t2) : 1.0f;
        instFreq = params.endFreq + (params.midFreq - params.endFreq) * safePow01 (1.0f - x, pCurve);
    }
    else
    {
        instFreq = params.endFreq;
    }
    instFreq *= pitchTransposeRatio;  // pitch tracking

    // -------- 2. Sine oscillator (cumulative phase, double precision) --------
    phase += (double) instFreq / sampleRate;
    if (phase >= 1.0) phase -= std::floor (phase);
    const float osc = std::sin (kTwoPi * (float) phase);

    // -------- 3. AHDSR amp envelope (matches BazzismRebuild.py:328-360) --------
    const float tA  = params.volAttackMs  * 0.001f;
    const float tH  = params.volHoldMs    * 0.001f;
    const float tD1 = params.volDecay1Ms  * 0.001f;
    const float tD2 = params.volDecay2Ms  * 0.001f;
    const float sus = juce::jlimit (0.0f, 1.0f, params.volSustain);
    const float vC  = juce::jmax (0.01f, params.volCurve);

    const float p1End = tA;
    const float p2End = p1End + tH;
    const float p3End = p2End + tD1;
    const float p4End = p3End + tD2;

    float ampEnv = 0.0f;
    if (t < p1End)
    {
        const float x = (tA > 0.0f) ? juce::jlimit (0.0f, 1.0f, t / tA) : 1.0f;
        ampEnv = safePow01 (x, 1.0f / vC);
    }
    else if (t < p2End)
    {
        ampEnv = 1.0f;
    }
    else if (t < p3End)
    {
        const float x = (tD1 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - p2End) / tD1) : 1.0f;
        ampEnv = sus + (1.0f - sus) * safePow01 (1.0f - x, vC);
    }
    else if (t < p4End)
    {
        const float x = (tD2 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - p3End) / tD2) : 1.0f;
        ampEnv = sus * safePow01 (1.0f - x, vC);
    }
    else
    {
        ampEnv = 0.0f;
        // voice has finished — mark inactive on next sample after we return
        // (so the caller gets one last 0 sample first)
    }

    // -------- 4. Scoop (matches BazzismRebuild.py:362-372) --------
    const float sStart  = params.scoopStartMs  * 0.001f;
    const float sLen    = params.scoopLengthMs * 0.001f;
    const float sDepth  = juce::jlimit (0.0f, 1.0f, params.scoopDepth);
    if (sDepth > 0.0f && sLen > 0.0f && t >= sStart && t < sStart + sLen)
    {
        const float scoopPhase = (t - sStart) / sLen;  // 0..1
        ampEnv *= 1.0f - (std::sin (juce::MathConstants<float>::pi * scoopPhase) * sDepth);
    }

    float sample = osc * ampEnv;

    // -------- 5. Transient layer (sum-in) --------
    if (clickSamplesLeft > 0 && params.clickVol > 0.0f)
    {
        const float clickProgress01 = (float) (clickTotalSamples - clickSamplesLeft)
                                    / (float) clickTotalSamples;     // 0..1
        const float clickRem01      = 1.0f - clickProgress01;        // 1..0
        const float clickFade       = clickRem01 * clickRem01;       // quadratic (matches Python line 386)

        // Sine chirp 10k → 2k
        const float chirpHz = 10000.0f + (2000.0f - 10000.0f) * clickProgress01;
        clickPhase += (double) chirpHz / sampleRate;
        if (clickPhase >= 1.0) clickPhase -= std::floor (clickPhase);
        const float sineClick = std::sin (kTwoPi * (float) clickPhase) * clickFade;

        // Noise burst, exp decay
        const float noiseClick = (rng.nextFloat() * 2.0f - 1.0f) * clickFade;

        float clickRaw;
        switch (params.clickType)
        {
            case 1:  clickRaw = noiseClick; break;                       // Noise
            case 2:  clickRaw = 0.5f * (sineClick + noiseClick); break;  // Both
            default: clickRaw = sineClick; break;                        // Sine (Python default)
        }

        // 1-pole HPF — y[n] = a*(y[n-1] + x[n] - x[n-1]),  a = 1/(1+2πfc/fs)
        const float hpAlpha = 1.0f / (1.0f + kTwoPi * params.clickHpfHz / (float) sampleRate);
        const float clickHp = hpAlpha * (clickHpfPrevOut + clickRaw - clickHpfPrevIn);
        clickHpfPrevIn  = clickRaw;
        clickHpfPrevOut = clickHp;

        // 1-pole LPF — y[n] = y[n-1] + α(x[n] - y[n-1]),  α = 2πfc/fs / (1 + 2πfc/fs)
        const float lpW    = kTwoPi * params.clickToneHz / (float) sampleRate;
        const float lpAlpha = lpW / (1.0f + lpW);
        const float clickFinal = clickLpfPrev + lpAlpha * (clickHp - clickLpfPrev);
        clickLpfPrev = clickFinal;

        sample += clickFinal * params.clickVol;
        --clickSamplesLeft;
    }

    // Apply retrigger crossfade — mix in dying tail of previous voice
    if (crossfadeRemaining > 0)
    {
        const int idx = crossfadeLen - crossfadeRemaining;
        const float xfade = (float) crossfadeRemaining / (float) crossfadeLen;  // 1..0 over fade
        sample = (1.0f - xfade) * sample + xfade * crossfadeTail[(size_t) idx];
        --crossfadeRemaining;
    }

    // Advance time. After we return, check whether voice should end.
    ++sampleSinceTrigger;
    if (t >= p4End && crossfadeRemaining == 0)
    {
        active.store (false);
    }
    playbackPos.store (sampleSinceTrigger);

    return sample * velocity;
}

//==============================================================================
// Apply drive (4x oversampled), DC blocker, soft-clip, polarity, output gain.
// Operates in place on a mono dryScratch buffer of length n.
//==============================================================================
void KickEngine::applyPostStages (float* samples, int n) noexcept
{
    if (n <= 0) return;

    // Pre-compute drive ramp endpoints for this block.
    // Python: drive_array = base + (linspace(0,1,N)**2) * tail * 5  where N = total kick samples
    const float baseDrive = juce::jmax (1.0f, params.drive);
    const float tailDrv   = juce::jmax (0.0f, params.tailDrive);

    // Estimate max drive over the whole kick lifetime (= at the very end)
    const float maxDrive  = baseDrive + tailDrv * 5.0f;
    const float invTanhMax = 1.0f / juce::jmax (1e-6f, std::tanh (maxDrive));

    // --- Oversample upward ---
    juce::dsp::AudioBlock<float> inputBlock (&samples, 1, (size_t) n);
    auto upBlock = oversampler->processSamplesUp (inputBlock);
    const int   osN = (int) upBlock.getNumSamples();
    float* const osData = upBlock.getChannelPointer (0);
    const int OSfactor = 1 << kOversampleLog2;

    // --- Apply per-supersample drive ramp + tanh ---
    // Map supersample i back to a sample-since-trigger position to compute the ramp value.
    // sampleSinceTrigger has ALREADY advanced past this block — back up by n samples.
    const int blockStartSample = sampleSinceTrigger - n;
    const float invTotal = (totalSamples > 0) ? 1.0f / (float) totalSamples : 0.0f;

    for (int i = 0; i < osN; ++i)
    {
        // Position within the kick lifetime, in [0..1] roughly
        const float originalSampleIdx = (float) blockStartSample + (float) i / (float) OSfactor;
        const float ramp01 = juce::jlimit (0.0f, 1.0f, originalSampleIdx * invTotal);
        const float drive_t = baseDrive + (ramp01 * ramp01) * tailDrv * 5.0f;
        osData[i] = std::tanh (osData[i] * drive_t) * invTanhMax;
    }

    // --- Oversample downward ---
    juce::dsp::AudioBlock<float> outputBlock (&samples, 1, (size_t) n);
    oversampler->processSamplesDown (outputBlock);

    // --- DC blocker (1-pole HP, always on) ---
    for (int i = 0; i < n; ++i)
    {
        const float x = samples[i];
        const float y = x - dcPrevIn + dcR * dcPrevOut;
        dcPrevIn  = x;
        dcPrevOut = y;
        samples[i] = y;
    }

    // --- Soft-clip ceiling at -0.3 dBFS ---
    const float ceil = dbToGain (kSoftClipCeilDb);
    const float invTanhCeil = 1.0f / std::tanh (1.0f);  // normalize so ±1 in → ±1 out approximately
    for (int i = 0; i < n; ++i)
    {
        // soft clip: tanh-based, scaled so |x| << ceil passes through, |x| >= ceil rolls off
        samples[i] = ceil * std::tanh (samples[i] / ceil) * invTanhCeil;
    }

    // --- Polarity invert ---
    if (params.invertPhase)
    {
        for (int i = 0; i < n; ++i)
            samples[i] = -samples[i];
    }

    // --- Output gain ---
    const float gain = dbToGain (params.outputGainDb);
    if (! juce::approximatelyEqual (gain, 1.0f))
    {
        for (int i = 0; i < n; ++i)
            samples[i] *= gain;
    }
}

//==============================================================================
void KickEngine::renderBlock (juce::AudioBuffer<float>& buffer,
                              int startSample, int numSamples) noexcept
{
    if (! active.load() && crossfadeRemaining == 0)
        return;

    if ((int) dryScratch.size() < numSamples)
        return;  // shouldn't happen — prepare() sized this to blockSize

    // 1. Render dry samples
    for (int i = 0; i < numSamples; ++i)
        dryScratch[(size_t) i] = renderOneDrySample();

    // 2. Apply post-stages (oversampled drive + DC blocker + soft-clip + invert + gain)
    applyPostStages (dryScratch.data(), numSamples);

    // 3. Sum into output buffer (stereo, both channels)
    const int numCh = buffer.getNumChannels();
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = buffer.getWritePointer (ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            out[i] += dryScratch[(size_t) i];
    }
}

//==============================================================================
// UI-thread offline render — used by the visualizer (Phase 4).
//==============================================================================
void KickEngine::renderOffline (juce::AudioBuffer<float>& buffer,
                                double sr, double durationMs)
{
    const double prevSr = sampleRate;
    const int prevBlock = blockSize;

    // Render at the requested SR, possibly different from the realtime SR.
    if (! juce::approximatelyEqual (sr, sampleRate) || ! oversampler)
        prepare (sr, 4096);

    const int total = juce::jmax (1, (int) std::ceil ((durationMs * 0.001) * sr));
    buffer.setSize (1, total, false, true, true);
    buffer.clear();

    // Fresh trigger from t=0
    triggerNote (60, 1.0f, 0);

    const int chunk = 1024;
    int written = 0;
    while (written < total)
    {
        const int n = juce::jmin (chunk, total - written);
        // Render into scratch
        for (int i = 0; i < n; ++i)
            dryScratch[(size_t) i] = renderOneDrySample();
        applyPostStages (dryScratch.data(), n);

        float* dest = buffer.getWritePointer (0, written);
        std::copy (dryScratch.begin(), dryScratch.begin() + n, dest);
        written += n;
    }

    // Restore prior SR/blocksize for the realtime path.
    if (! juce::approximatelyEqual (prevSr, sr))
        prepare (prevSr, prevBlock);
    else
        reset();
}
