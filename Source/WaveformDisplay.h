#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "BreakpointEditor.h"
#include "SpectrumUtil.h"

//==============================================================================
// The big visualizer canvas — the headline feature.
// WAVE view layers (back to front):
//   1. Minimal grid (50 ms verticals + center / ±0.5 amp lines) + axis labels
//   2. Waveform HERO — full-height mirrored hull: outer glow, center-bright
//      vertical gradient body, dense RMS core, crisp bright edge (red accent)
//   3. Amp envelope — thin yellow overlay line (the shell)
//   4. Pitch envelope — thin cyan overlay line across full height (log-Y) + tag
//   5. Playhead (white, ~30 Hz repaint)
//   6. Top header strip: readouts (peak dBFS, duration, end note + Hz)
//
// Rendering is pure juce::Graphics (CPU). Re-render is parameter-change driven
// + 50 ms debounced. A separate ~30 Hz timer redraws ONLY the playhead rect
// when the engine is active.
//==============================================================================

class WaveformDisplay : public juce::Component,
                        public juce::Timer,
                        public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit WaveformDisplay (KickAssProcessor&);
    ~WaveformDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // APVTS::Listener
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // Visualizer view modes — default Wave so existing screenshots/behavior are
    // unchanged until the user clicks a tab.
    enum class ViewMode { Wave, Spectrum, Both };

private:
    KickAssProcessor& processor;

    // Render buffer (mono, last offline kick).
    juce::AudioBuffer<float> renderBuf;
    double renderSampleRate = 48000.0;

    // View mode + cached spectrum (recomputed only on re-render, not every repaint).
    ViewMode viewMode = ViewMode::Wave;
    // Horizontal zoom for the WAVE view, anchored at t=0 (transient is at the start).
    // 0 = full duration; >0 = visible window in ms. Scroll over the canvas to change.
    float viewWindowMs = 0.0f;
    kickass::SpectrumResult spectrum;
    juce::Rectangle<int> tabWave, tabSpectrum, tabBoth;   // top-right clickable tabs

    void paintWave (juce::Graphics&, juce::Rectangle<float> areaBelowHeader);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int> area);
    juce::Rectangle<int> layoutTabs();                    // computes tab rects, returns the strip used

    // Pre-computed envelope traces for paint() (one entry per pixel column).
    std::vector<float> ampEnvTrace;     // 0..1
    std::vector<float> pitchHzTrace;    // Hz, log-Y in paint()

    // Cached readouts
    float  peakDb       = -120.0f;
    float  durationMs   = 0.0f;
    float  endHz        = 0.0f;
    juce::String endNote;

    // Dirty / debounce state
    std::atomic<bool> dirty { true };
    int   debounceCountdown = 0;       // in timer ticks
    bool  voiceWasActive = false;

    // Constants
    static constexpr int  timerHz             = 30;   // 30 Hz tick: playhead + dirty check
    static constexpr int  debounceTicks       = 2;    // ~66 ms after last param change
    static constexpr float minRenderMs        = 80.0f;
    static constexpr float renderHeadroomMult = 1.10f;

    void recomputeIfDirty();
    void recomputeEnvelopeTraces();
    void recomputeReadouts();
    void updateBreakpointEditorVisibility();
    void layoutBreakpointEditor();

    // Helpers
    static juce::String hzToNote (float hz);
    static float logFreqToY (float hz, float yTop, float yBottom, float fMin = 20.0f, float fMax = 20000.0f);

    // Phase 6b — overlay editor for Advanced envelope mode.
    BreakpointEditor breakpointEditor { processor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
