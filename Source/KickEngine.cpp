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

    //--------------------------------------------------------------------------
    // Saturation waveshapers. All take an already-driven input `x` (dry × drive).
    // Each shapes harmonically differently; the caller normalizes by invNorm so
    // the loudest part of the kick lands near unity regardless of type.
    //   Tanh     — smooth, symmetric. The v1.0 character (default, index 0).
    //   SoftClip — algebraic knee, clean/loud/modern.
    //   HardClip — square knee, punchy/transient-heavy.
    //   Tube     — asymmetric (even harmonics), warm/round. DC offset removed by
    //              the downstream DC blocker.
    //   Foldback — wavefolder, aggressive/hi-tech/darkpsy.
    //--------------------------------------------------------------------------
    enum SatType { Tanh = 0, SoftClip = 1, HardClip = 2, Tube = 3, Foldback = 4 };

    inline float satFoldback (float x) noexcept
    {
        // Reflect across ±1 repeatedly. Bounded output in [-1, 1].
        constexpr float lim = 1.0f;
        if (x > lim || x < -lim)
            x = std::fabs (std::fabs (std::fmod (x - lim, 4.0f * lim)) - 2.0f * lim) - lim;
        return x;
    }

    inline float saturate (int type, float x) noexcept
    {
        switch (type)
        {
            case SoftClip:
            {
                // Cubic soft clip: unity slope at 0, flat past |x|>=1.
                const float a = juce::jlimit (-1.0f, 1.0f, x);
                return 1.5f * a - 0.5f * a * a * a;
            }
            case HardClip:
                return juce::jlimit (-1.0f, 1.0f, x);
            case Tube:
                // Asymmetric tanh — squashes the negative half harder → even harmonics.
                return (x >= 0.0f) ? std::tanh (x)
                                   : std::tanh (x * 0.7f);
            case Foldback:
                return satFoldback (x);
            case Tanh:
            default:
                return std::tanh (x);
        }
    }

    // Normalization so a unit-amplitude dry sample at the loudest drive maps to ~±1.
    // Clamped to a sane band so foldback (whose output can land near 0 at certain
    // drives) never explodes the gain.
    inline float saturationInvNorm (int type, float maxDrive) noexcept
    {
        const float norm = std::fabs (saturate (type, maxDrive));
        return juce::jlimit (0.1f, 10.0f, 1.0f / juce::jmax (1.0e-6f, norm));
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
// Phase 6b — publish curve to inactive LUT slot then atomic-flip the index.
// Called from UI thread. Audio thread reads activeLutIdx with acquire ordering.
//==============================================================================
void KickEngine::publishVolCurve (const EnvCurve& src)
{
    const int inactive = 1 - activeLutIdx.load (std::memory_order_relaxed);
    auto& lut = (inactive == 0) ? ampLutA : ampLutB;
    src.fillLut (lut);
    lutTotalMs[inactive] = src.getTotalMs();
    activeLutIdx.store (inactive, std::memory_order_release);
}

//==============================================================================
// v1.1 — publish a (mono) transient sample into the inactive slot then atomic-flip.
// Called from UI thread. The buffer copy is the ONLY allocation; the audio thread
// reads activeSampleIdx with acquire ordering and never allocates.
//==============================================================================
void KickEngine::publishTransientSample (const juce::AudioBuffer<float>& monoData, double sourceSampleRate)
{
    const int inactive = 1 - activeSampleIdx.load (std::memory_order_relaxed);
    auto& slot = (inactive == 0) ? sampleSlotA : sampleSlotB;

    const int len = juce::jmax (0, monoData.getNumSamples());
    slot.setSize (1, juce::jmax (1, len), false, false, true);   // keep at least 1 sample of storage
    slot.clear();
    if (len > 0 && monoData.getNumChannels() > 0)
        slot.copyFrom (0, 0, monoData, 0, 0, len);

    sampleLength[inactive]     = len;
    sampleSourceRate[inactive] = (sourceSampleRate > 0.0) ? sourceSampleRate : 44100.0;

    activeSampleIdx.store (inactive, std::memory_order_release);
}

//==============================================================================
// v1.1 — publish an empty slot (length 0). Same lock-free flip.
//==============================================================================
void KickEngine::clearTransientSample()
{
    const int inactive = 1 - activeSampleIdx.load (std::memory_order_relaxed);
    sampleLength[inactive]     = 0;
    sampleSourceRate[inactive] = 44100.0;
    activeSampleIdx.store (inactive, std::memory_order_release);
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

    // Reset voice state. phase=0 is a clean zero-crossing start; user phaseOffset
    // (0..1 = 0..360°) lets them dial in click vs. boom on the very first sample.
    phase = (double) juce::jlimit (0.0f, 1.0f, params.phaseOffset);
    sampleSinceTrigger = 0;
    velocity = juce::jlimit (0.0f, 1.0f, vel);

    // Phase 6b: snapshot curve mode + LUT for the voice. Lock-free read of the
    // atomically-published active index; the UI thread can flip it after this
    // point and this voice will still use the snapshotted slot for its lifetime.
    voiceUseCurve   = curveModeAtomic.load (std::memory_order_acquire);
    voiceLutIdx     = activeLutIdx.load    (std::memory_order_acquire);
    voiceLutTotalMs = lutTotalMs[voiceLutIdx];
    if (voiceUseCurve && voiceLutTotalMs <= 0.0f)
        voiceUseCurve = false;   // empty LUT → safe fallback to AHDSR

    // Voice length: in Advanced mode use the curve's own duration; otherwise the AHDSR knobs.
    if (voiceUseCurve)
        totalSamples = juce::jmax (1, (int) std::ceil ((voiceLutTotalMs * 0.001f) * (float) sampleRate));
    else
        totalSamples = computeTotalSamples();

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

    // v1.1: snapshot the active transient-sample slot for the voice lifetime.
    // Lock-free acquire-load mirrors the LUT snapshot above; the UI thread can
    // publish a new sample after this point without affecting this voice.
    voiceSampleIdx    = activeSampleIdx.load (std::memory_order_acquire);
    voiceSampleLength = sampleLength[voiceSampleIdx];
    voiceSampleRate   = sampleSourceRate[voiceSampleIdx];
    samplePlayPos     = 0.0;

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

    // -------- 3. Amp envelope: AHDSR closed-form OR LUT lookup (Phase 6b) --------
    float ampEnv = 0.0f;
    float p4End  = 0.0f;   // total envelope duration in seconds — used by scoop + voice-end check

    if (voiceUseCurve)
    {
        // Advanced mode: linear interpolate into the snapshotted LUT.
        const float totalSec = voiceLutTotalMs * 0.001f;
        p4End = totalSec;
        if (totalSec > 0.0f && t < totalSec)
        {
            const float pos    = (t / totalSec) * (float) (EnvCurve::kLutSize - 1);
            const int   idx    = (int) pos;
            const float frac   = pos - (float) idx;
            const auto& lut    = (voiceLutIdx == 0) ? ampLutA : ampLutB;
            const float a0     = lut[(size_t) juce::jlimit (0, EnvCurve::kLutSize - 1, idx)];
            const float a1     = lut[(size_t) juce::jlimit (0, EnvCurve::kLutSize - 1, idx + 1)];
            ampEnv = a0 + (a1 - a0) * frac;
        }
        else
        {
            ampEnv = 0.0f;
        }
    }
    else
    {
        // Simple mode: AHDSR closed-form (matches BazzismRebuild.py:328-360).
        const float tA  = params.volAttackMs  * 0.001f;
        const float tH  = params.volHoldMs    * 0.001f;
        const float tD1 = params.volDecay1Ms  * 0.001f;
        const float tD2 = params.volDecay2Ms  * 0.001f;
        const float sus = juce::jlimit (0.0f, 1.0f, params.volSustain);
        const float vC  = juce::jmax (0.01f, params.volCurve);

        const float p1End = tA;
        const float p2End = p1End + tH;
        const float p3End = p2End + tD1;
        p4End             = p3End + tD2;

        if (t < p1End)
        {
            const float x = (tA > 0.0f) ? juce::jlimit (0.0f, 1.0f, t / tA) : 1.0f;
            ampEnv = safePow01 (x, 1.0f / vC);
        }
        else if (t < p2End) { ampEnv = 1.0f; }
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
        }
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

    // soloTransient (visualizer TRANSIENT view): mute the body so only the
    // transient/sample layer below survives. `osc`/`ampEnv` are still advanced
    // above so the voice timeline (and the sample read cursor) stays correct.
    float sample = params.soloTransient ? 0.0f : (osc * ampEnv);

    // -------- 5. Transient layer (sum-in) --------
    // clickType 0=Sine 1=Noise 2=Both share the synthesized-click source below.
    // clickType 3=Sample (v1.1) replaces that source with WAV playback, but reuses
    // the EXACT same click HPF → LPF → clickVol chain so all four sources share the
    // transient tone/level controls. The synth click is gated by clickSamplesLeft
    // (clickDecay window); the sample plays its NATURAL length instead — documented
    // choice: a dropped sample is meant to be heard in full, scaled by clickVol.
    const bool sampleMode   = (params.clickType == 3);
    const bool synthActive  = (! sampleMode) && clickSamplesLeft > 0 && params.clickVol > 0.0f;
    const bool sampleActive = sampleMode && params.clickVol > 0.0f
                              && voiceSampleLength > 0
                              && samplePlayPos < (double) voiceSampleLength;

    if (synthActive || sampleActive)
    {
        float clickRaw;

        if (sampleActive)
        {
            // SR-correct linear-interpolated read. Step so the sample plays at its
            // recorded pitch regardless of the engine's current sample rate.
            const auto& slot = (voiceSampleIdx == 0) ? sampleSlotA : sampleSlotB;
            const float* data = slot.getReadPointer (0);
            const int   i0    = (int) samplePlayPos;
            const int   i1    = juce::jmin (i0 + 1, voiceSampleLength - 1);
            const float frac  = (float) (samplePlayPos - (double) i0);
            clickRaw = data[i0] + (data[i1] - data[i0]) * frac;

            const double step = (sampleRate > 0.0) ? (voiceSampleRate / sampleRate) : 1.0;
            samplePlayPos += step;
        }
        else
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

            switch (params.clickType)
            {
                case 1:  clickRaw = noiseClick; break;                       // Noise
                case 2:  clickRaw = 0.5f * (sineClick + noiseClick); break;  // Both
                default: clickRaw = sineClick; break;                        // Sine (Python default)
            }

            --clickSamplesLeft;
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
    }
    // FALLBACK: clickType==Sample with no sample loaded (voiceSampleLength==0) →
    // sampleActive is false and the synth branch is skipped → silent transient,
    // no OOB read, no crash.

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
    const int   satType   = params.saturationType;
    const float invNorm   = saturationInvNorm (satType, maxDrive);

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
        osData[i] = saturate (satType, osData[i] * drive_t) * invNorm;
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

    // --- Safety limiter (final stage, AFTER output gain) ---
    // The -0.3 dBFS soft-clip above is a CHARACTER stage and runs before the
    // output gain, so a positive Output (e.g. +6 dB) can push the signal back
    // over 0 dBFS. This defeatable brickwall is the true safety net.
    // NOTE: this is a SAMPLE-PEAK clamp only — no lookahead / oversampling here,
    // so we make no true-peak (dBTP) / inter-sample claim in this pass.
    if (params.safetyLimit)
    {
        const float ceilLin = dbToGain (-0.1f);   // -0.1 dBFS ceiling
        for (int i = 0; i < n; ++i)
            samples[i] = juce::jlimit (-ceilLin, ceilLin, samples[i]);
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
// CALLER MUST own this engine (i.e. it's not the same engine the audio thread uses).
// The processor's `offlineEngine` is the dedicated instance for this path.
//==============================================================================
void KickEngine::renderOffline (juce::AudioBuffer<float>& buffer,
                                double sr, double durationMs)
{
    if (! oversampler || ! juce::approximatelyEqual (sr, sampleRate))
        prepare (sr, 4096);
    else
        reset();

    const int total = juce::jmax (1, (int) std::ceil ((durationMs * 0.001) * sr));
    buffer.setSize (1, total, false, true, true);
    buffer.clear();

    triggerNote (60, 1.0f, 0);

    const int chunk = (int) dryScratch.size();
    int written = 0;
    while (written < total)
    {
        const int n = juce::jmin (chunk, total - written);
        for (int i = 0; i < n; ++i)
            dryScratch[(size_t) i] = renderOneDrySample();
        applyPostStages (dryScratch.data(), n);

        float* dest = buffer.getWritePointer (0, written);
        std::copy (dryScratch.begin(), dryScratch.begin() + n, dest);
        written += n;
    }
}
