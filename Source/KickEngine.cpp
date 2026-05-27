#include "KickEngine.h"

//==============================================================================
// PHASE 1: stub that outputs silence. Phase 2 ports the Python DSP.
//==============================================================================

void KickEngine::prepare (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    reset();
}

void KickEngine::reset()
{
    active.store (false);
    playbackPos.store (0);
    pendingTriggerOffset = -1;
}

void KickEngine::triggerNote (int midiNote, float velocity, int sampleOffset) noexcept
{
    pendingTriggerOffset = sampleOffset;
    pendingTriggerNote   = midiNote;
    pendingTriggerVel    = velocity;
}

void KickEngine::renderBlock (juce::AudioBuffer<float>& /*buffer*/,
                              int /*startSample*/, int /*numSamples*/) noexcept
{
    // Phase 1: silence. Audio thread does nothing.
    // Consume any pending trigger so it doesn't accumulate.
    pendingTriggerOffset = -1;
    active.store (false);
}

void KickEngine::renderOffline (juce::AudioBuffer<float>& buffer,
                                double /*sampleRate*/, double /*durationMs*/)
{
    // Phase 1: clear the buffer. Phase 4 (visualizer) needs this to do real work.
    buffer.clear();
}
