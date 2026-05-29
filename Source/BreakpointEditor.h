#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "EnvCurve.h"

//==============================================================================
// BreakpointEditor — Sanlight-dimmer-style point editor for an EnvCurve.
//
// Lives as a child component of WaveformDisplay, sized exactly to the bottom-
// half plot area (where the amp envelope lives). All interaction is here;
// the parent waveform widget just shows/hides it based on envelope_mode.
//
// Interactions:
//   left-drag a point      → move (point[0] is x-locked; back() is y-locked)
//   shift + left-drag      → bend the segment LEAVING this point (tension)
//   double-click empty     → add a new point at the cursor
//   right-click on point   → delete (interior points only)
//
// Mutations go: edit volEnvCurve → clampToInvariants → syncVolCurveToValueTree
// which both persists into apvts.state and publishes to both engines.
//==============================================================================

class BreakpointEditor : public juce::Component
{
public:
    explicit BreakpointEditor (KickAssProcessor& p);
    ~BreakpointEditor() override = default;

    /** Fired after every committed edit. Parent (WaveformDisplay) wires this to
        invalidate its cached traces + render buffer so the yellow trace and the
        red waveform refresh to match the new curve. */
    std::function<void()> onCurveCommitted;

    void paint    (juce::Graphics& g) override;
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;

private:
    KickAssProcessor& processor;

    // -1 if none. -2 means "tension drag from segment N" (decoded via dragTensionSeg).
    int  dragPointIdx     = -1;
    int  dragTensionSeg   = -1;
    bool isDraggingTension = false;

    // ----- Mapping helpers (component-local pixels ↔ curve coords) -----
    juce::Point<float> pointToPixel (const EnvPoint& p, float totalMs) const noexcept;
    EnvPoint           pixelToPoint (juce::Point<float> px, float totalMs) const noexcept;

    // Returns index of point under cursor (within hit radius), else -1.
    int hitPointIndex (juce::Point<float> px) const noexcept;
    // Returns index of segment whose tension handle is under cursor, else -1.
    int hitTensionHandle (juce::Point<float> px) const noexcept;

    // Snapshot of the curve's totalMs after a drag, used to keep the back
    // point's x-position editable while the rest are clamped relative to it.
    float currentTotalMs() const noexcept;

    // After any edit: clamp, persist, repaint.
    void commitEdit();

    static constexpr float kHitRadiusPx       = 12.0f;
    static constexpr float kPointRadiusPx     = 5.5f;
    static constexpr float kTensionHandleRadPx = 4.0f;
    // The drag x of the LAST point sets total duration — clamp it so total stays usable.
    static constexpr float kMinTotalMs        = 20.0f;
    static constexpr float kMaxTotalMs        = 2000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakpointEditor)
};
