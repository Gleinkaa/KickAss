#include "WaveformDisplay.h"
#include "KickAssLookAndFeel.h"
#include <cmath>

namespace
{
    constexpr float kHeaderStripPx = 22.0f;     // top readouts strip inside canvas
    constexpr float kLeftAxisPx    = 36.0f;     // pitch axis label gutter
    constexpr float kBottomAxisPx  = 14.0f;     // time axis label gutter
    constexpr float kTwoPi         = 6.28318530717958647692f;
}

//==============================================================================
WaveformDisplay::WaveformDisplay (KickAssProcessor& p) : processor (p)
{
    // Listen to every APVTS param that affects shape
    static const char* kIds[] = {
        "start_freq","mid_freq","end_freq","sweep_time_1","sweep_time_2","pitch_curve",
        "vol_attack","vol_hold","vol_decay_1","vol_sustain","vol_decay_2","vol_curve",
        "scoop_start","scoop_length","scoop_depth",
        "click_vol","click_type","click_hpf","click_tone","click_decay",
        "drive","tail_drive","invert_phase","output_gain",
        "envelope_mode"     // Phase 6b: mode flip drives editor show/hide + viz rebuild
    };
    for (auto* id : kIds)
        processor.apvts.addParameterListener (id, this);

    addChildComponent (breakpointEditor);          // hidden until Advanced mode
    breakpointEditor.onCurveCommitted = [this]
    {
        // Curve edits don't go through APVTS, so we invalidate manually. Run the
        // refresh immediately (no debounce) so the yellow trace + red waveform
        // follow the drag fluidly.
        dirty.store (true);
        debounceCountdown = 1;
    };
    updateBreakpointEditorVisibility();

    startTimerHz (timerHz);
    setOpaque (true);
}

WaveformDisplay::~WaveformDisplay()
{
    static const char* kIds[] = {
        "start_freq","mid_freq","end_freq","sweep_time_1","sweep_time_2","pitch_curve",
        "vol_attack","vol_hold","vol_decay_1","vol_sustain","vol_decay_2","vol_curve",
        "scoop_start","scoop_length","scoop_depth",
        "click_vol","click_type","click_hpf","click_tone","click_decay",
        "drive","tail_drive","invert_phase","output_gain",
        "envelope_mode"
    };
    for (auto* id : kIds)
        processor.apvts.removeParameterListener (id, this);
}

void WaveformDisplay::parameterChanged (const juce::String& id, float)
{
    dirty.store (true);
    debounceCountdown = debounceTicks;
    if (id == "envelope_mode")
    {
        // Defer to the message thread — parameter listener may be called from audio thread.
        juce::MessageManager::callAsync ([this] { updateBreakpointEditorVisibility(); });
    }
}

void WaveformDisplay::updateBreakpointEditorVisibility()
{
    const bool advanced = processor.isEnvelopeAdvanced();
    breakpointEditor.setVisible (advanced);
    if (advanced)
        layoutBreakpointEditor();
    repaint();
}

void WaveformDisplay::layoutBreakpointEditor()
{
    // Mirror the same geometry the paint() routine uses for the bottom-half plot.
    constexpr float kHeaderStripPx = 22.0f;
    constexpr float kLeftAxisPx    = 36.0f;
    constexpr float kBottomAxisPx  = 14.0f;

    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (kHeaderStripPx);
    auto plot = bounds.reduced (0, 4);
    plot.removeFromLeft   (kLeftAxisPx);
    plot.removeFromBottom (kBottomAxisPx);
    const float plotTop    = plot.getY();
    const float plotBottom = plot.getBottom();
    const float plotMid    = (plotTop + plotBottom) * 0.5f;

    juce::Rectangle<int> bottomHalf {
        (int) plot.getX(), (int) plotMid,
        (int) plot.getWidth(), (int) (plotBottom - plotMid)
    };
    breakpointEditor.setBounds (bottomHalf);
}

void WaveformDisplay::timerCallback()
{
    // 1. Debounce: if dirty, count down then render.
    if (dirty.load() && debounceCountdown > 0)
    {
        --debounceCountdown;
        if (debounceCountdown == 0)
        {
            recomputeIfDirty();
            repaint();
        }
    }

    // 2. Playhead: repaint when the audio thread voice is alive.
    const bool isActive = processor.getEngine().isActive();
    if (isActive || voiceWasActive)
        repaint();   // full repaint — playhead-only-rect optimisation is v1.1
    voiceWasActive = isActive;
}

void WaveformDisplay::resized()
{
    dirty.store (true);
    debounceCountdown = 1;   // re-render immediately on resize
    layoutBreakpointEditor();
}

juce::Rectangle<int> WaveformDisplay::layoutTabs()
{
    // Four small tabs in the TOP-RIGHT of the header strip. Returns the strip
    // they live in so paint() can keep the readouts clear of them.
    constexpr int tabW = 58;
    constexpr int tabH = 14;
    constexpr int gap  = 2;
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop ((int) kHeaderStripPx);

    auto tabsArea = header.removeFromRight (tabW * 4 + gap * 3 + 12).reduced (6, 4);
    tabBoth      = tabsArea.removeFromRight (tabW);
    tabsArea.removeFromRight (gap);
    tabSpectrum  = tabsArea.removeFromRight (tabW);
    tabsArea.removeFromRight (gap);
    tabTransient = tabsArea.removeFromRight (tabW);
    tabsArea.removeFromRight (gap);
    tabWave      = tabsArea.removeFromRight (tabW);
    juce::ignoreUnused (tabH);
    return header;
}

void WaveformDisplay::frameTransientView()
{
    // Fit the zoom window to the detected transient length (+20% headroom),
    // clamped so it's always a meaningful zoom-in but never below the deepest
    // oscilloscope window. Leaves viewWindowMs=full if nothing was detected.
    if (transientLenMs > 0.0f && durationMs > 0.0f)
    {
        const float win = juce::jlimit (kMinZoomMs, durationMs, transientLenMs * 1.2f);
        viewWindowMs = (win >= durationMs - 0.05f) ? 0.0f : win;
    }
}

void WaveformDisplay::mouseDown (const juce::MouseEvent& e)
{
    // Tab hit-test FIRST. A tab click switches the view and must NOT also trigger
    // a preview kick (the canvas-click behavior below).
    layoutTabs();
    const auto p = e.getPosition();
    if (tabWave.contains (p))     { viewMode = ViewMode::Wave;     repaint(); return; }
    if (tabTransient.contains (p))
    {
        viewMode = ViewMode::Transient;
        // The transient buffer is only kept up-to-date while this view is active,
        // so force an immediate (un-debounced) render, then frame to its length.
        dirty.store (true);
        debounceCountdown = 1;
        recomputeIfDirty();
        frameTransientView();
        repaint();
        return;
    }
    if (tabSpectrum.contains (p)) { viewMode = ViewMode::Spectrum; repaint(); return; }
    if (tabBoth.contains (p))     { viewMode = ViewMode::Both;     repaint(); return; }

    // Click anywhere else in the canvas = trigger preview (via UI→audio queue)
    processor.requestTrigger (60, 1.0f);
}

void WaveformDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click on the canvas (not the tabs) resets the WAVE/TRANSIENT zoom to
    // FULL. In TRANSIENT view, re-frame to the transient length instead so a
    // double-click is always a one-tap "fit" gesture rather than a dead reset.
    layoutTabs();
    const auto p = e.getPosition();
    if (tabWave.contains (p) || tabTransient.contains (p)
        || tabSpectrum.contains (p) || tabBoth.contains (p))
        return;

    if (viewMode == ViewMode::Transient) frameTransientView();
    else                                 viewWindowMs = 0.0f;   // full duration
    repaint();
}

void WaveformDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    // Horizontal zoom for transient shaping. Anchored at t=0 (the click/transient
    // sits at the very start). Wheel up = zoom in (smaller window). No re-render
    // needed — the cached renderBuf + envelope traces are sampled by time in paint.
    if (viewMode == ViewMode::Spectrum || durationMs <= 0.0f)
        return;

    float cur = (viewWindowMs > 0.0f) ? viewWindowMs : durationMs;
    cur *= (w.deltaY > 0.0f) ? 0.8f : 1.25f;
    cur = juce::jlimit (kMinZoomMs, durationMs, cur);
    // Snap back to "full" when we reach the whole duration.
    viewWindowMs = (cur >= durationMs - 0.05f) ? 0.0f : cur;
    repaint();
}

//==============================================================================
juce::String WaveformDisplay::hzToNote (float hz)
{
    if (hz < 16.0f) return "—";
    // MIDI: A4 = 69 = 440 Hz
    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int   m = (int) std::round (midi);
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const int  octave = (m / 12) - 1;
    const int  note   = ((m % 12) + 12) % 12;
    return juce::String (names[note]) + juce::String (octave);
}

float WaveformDisplay::logFreqToY (float hz, float yTop, float yBottom, float fMin, float fMax)
{
    hz = juce::jlimit (fMin, fMax, hz);
    const float t = std::log (hz / fMin) / std::log (fMax / fMin);   // 0..1
    return juce::jmap (t, 0.0f, 1.0f, yBottom, yTop);                // bottom = low freq, top = high
}

//==============================================================================
void WaveformDisplay::recomputeIfDirty()
{
    // 1. Total duration in ms — source depends on envelope mode.
    if (processor.isEnvelopeAdvanced())
    {
        juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
        const float total = processor.getVolEnvCurve().getTotalMs();
        durationMs = juce::jmax (minRenderMs, total * renderHeadroomMult);
    }
    else
    {
        const float ta  = *processor.apvts.getRawParameterValue ("vol_attack");
        const float th  = *processor.apvts.getRawParameterValue ("vol_hold");
        const float td1 = *processor.apvts.getRawParameterValue ("vol_decay_1");
        const float td2 = *processor.apvts.getRawParameterValue ("vol_decay_2");
        durationMs = juce::jmax (minRenderMs, (ta + th + td1 + td2) * renderHeadroomMult);
    }

    // 2. Audio render via the offline engine (UI thread, safe)
    processor.offlineRender (renderBuf, durationMs);
    renderSampleRate = 48000.0;

    // 2a. Transient-only render — only kept current while the TRANSIENT view is
    //     active (avoids paying for a second full render on every param change).
    if (viewMode == ViewMode::Transient)
    {
        processor.offlineRenderTransient (transientBuf, durationMs);

        // Detect the non-silent length so frameTransientView() can fit it.
        transientLenMs = 0.0f;
        if (transientBuf.getNumSamples() > 0)
        {
            const float* d = transientBuf.getReadPointer (0);
            const int    n = transientBuf.getNumSamples();
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (d[i]));
            const float thresh = juce::jmax (1.0e-4f, peak * 0.02f);   // -34 dB rel. peak
            int last = 0;
            for (int i = n - 1; i >= 0; --i)
                if (std::abs (d[i]) > thresh) { last = i; break; }
            transientLenMs = (float) ((double) (last + 1) / renderSampleRate * 1000.0);
        }
    }

    // 2b. Spectrum (cached; recomputed only here, never in paint()).
    if (renderBuf.getNumSamples() > 0)
        spectrum = kickass::computeSpectrum (renderBuf.getReadPointer (0),
                                             renderBuf.getNumSamples(),
                                             renderSampleRate);
    else
        spectrum = {};

    // 3. Envelope traces from APVTS params (UI thread, no engine state)
    recomputeEnvelopeTraces();

    // 4. Readouts
    recomputeReadouts();

    dirty.store (false);
}

void WaveformDisplay::recomputeEnvelopeTraces()
{
    const float startF  = *processor.apvts.getRawParameterValue ("start_freq");
    const float midF    = *processor.apvts.getRawParameterValue ("mid_freq");
    const float endF    = *processor.apvts.getRawParameterValue ("end_freq");
    const float t1Ms    = *processor.apvts.getRawParameterValue ("sweep_time_1");
    const float t2Ms    = *processor.apvts.getRawParameterValue ("sweep_time_2");
    const float pCurve  = juce::jmax (0.01f, (float) *processor.apvts.getRawParameterValue ("pitch_curve"));

    const float ta  = *processor.apvts.getRawParameterValue ("vol_attack");
    const float th  = *processor.apvts.getRawParameterValue ("vol_hold");
    const float td1 = *processor.apvts.getRawParameterValue ("vol_decay_1");
    const float sus = juce::jlimit (0.0f, 100.0f, (float) *processor.apvts.getRawParameterValue ("vol_sustain")) * 0.01f;
    const float td2 = *processor.apvts.getRawParameterValue ("vol_decay_2");
    const float vC  = juce::jmax (0.01f, (float) *processor.apvts.getRawParameterValue ("vol_curve"));

    const float scStart  = *processor.apvts.getRawParameterValue ("scoop_start");
    const float scLength = *processor.apvts.getRawParameterValue ("scoop_length");
    const float scDepth  = juce::jlimit (0.0f, 100.0f, (float) *processor.apvts.getRawParameterValue ("scoop_depth")) * 0.01f;

    const int  width = juce::jmax (1, getWidth() - (int) (kLeftAxisPx + 4.0f));
    ampEnvTrace.assign ((size_t) width, 0.0f);
    pitchHzTrace.assign ((size_t) width, endF);

    const float t1  = t1Ms  * 0.001f;
    const float t2  = t2Ms  * 0.001f;
    const float tA  = ta    * 0.001f;
    const float tH  = th    * 0.001f;
    const float tD1 = td1   * 0.001f;
    const float tD2 = td2   * 0.001f;
    const float sStart  = scStart  * 0.001f;
    const float sLen    = scLength * 0.001f;
    const float p1End = tA;
    const float p2End = p1End + tH;
    const float p3End = p2End + tD1;
    const float p4End = p3End + tD2;

    const float dur = durationMs * 0.001f;
    const bool  advanced = processor.isEnvelopeAdvanced();

    // In Advanced mode, snapshot the curve's points under the lock once and sample
    // from the local copy across all pixel columns — avoid holding the spinlock
    // for the whole pixel loop, and keep per-pixel cost low.
    EnvCurve curveSnap;
    if (advanced)
    {
        juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
        curveSnap = processor.getVolEnvCurve();
    }

    for (int i = 0; i < width; ++i)
    {
        const float t = ((float) i / (float) width) * dur;

        // --- Pitch envelope ---
        float f;
        if (t < t1) {
            const float x = (t1 > 0.0f) ? juce::jlimit (0.0f, 1.0f, t / t1) : 1.0f;
            f = midF + (startF - midF) * std::pow (1.0f - x, pCurve);
        } else if (t < t1 + t2) {
            const float x = (t2 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - t1) / t2) : 1.0f;
            f = endF + (midF - endF) * std::pow (1.0f - x, pCurve);
        } else {
            f = endF;
        }
        pitchHzTrace[(size_t) i] = f;

        // --- Amp envelope: Advanced reads from the curve, Simple computes AHDSR ---
        float a;
        if (advanced)
        {
            a = curveSnap.sampleAtMs (t * 1000.0f);
        }
        else if (t < p1End) {
            const float x = (tA > 0.0f) ? juce::jlimit (0.0f, 1.0f, t / tA) : 1.0f;
            a = std::pow (x, 1.0f / vC);
        } else if (t < p2End) {
            a = 1.0f;
        } else if (t < p3End) {
            const float x = (tD1 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - p2End) / tD1) : 1.0f;
            a = sus + (1.0f - sus) * std::pow (1.0f - x, vC);
        } else if (t < p4End) {
            const float x = (tD2 > 0.0f) ? juce::jlimit (0.0f, 1.0f, (t - p3End) / tD2) : 1.0f;
            a = sus * std::pow (1.0f - x, vC);
        } else {
            a = 0.0f;
        }

        // Scoop multiplies the amp envelope in BOTH modes — matches DSP path.
        if (scDepth > 0.0f && sLen > 0.0f && t >= sStart && t < sStart + sLen)
        {
            const float sp = (t - sStart) / sLen;
            a *= 1.0f - std::sin (juce::MathConstants<float>::pi * sp) * scDepth;
        }

        ampEnvTrace[(size_t) i] = juce::jlimit (0.0f, 1.0f, a);
    }

    endHz   = endF;
    endNote = hzToNote (endF);
}

void WaveformDisplay::recomputeReadouts()
{
    float peak = 0.0f;
    if (renderBuf.getNumSamples() > 0)
    {
        const float* d = renderBuf.getReadPointer (0);
        for (int i = 0; i < renderBuf.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (d[i]));
    }
    peakDb = (peak > 0.0f) ? 20.0f * std::log10 (peak) : -120.0f;
}

//==============================================================================
void WaveformDisplay::paint (juce::Graphics& g)
{
    using namespace KickColors;

    auto bounds = getLocalBounds().toFloat();

    // ---- 0. Background ----
    g.setColour (canvasBg);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (accentHot.withAlpha (0.20f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    // ---- Header strip (top) ----
    auto headerStrip = bounds.removeFromTop (kHeaderStripPx);
    g.setColour (textDim);
    g.setFont (KickFonts::mono (10.0f));
    auto leftReads  = headerStrip.reduced (10, 4);
    juce::String reads;
    if (viewMode == ViewMode::Transient)
        reads = juce::String::formatted ("TRANSIENT solo    SHAPE %.2f ms    PEAK %+5.1f dBFS",
                                         transientLenMs, peakDb);
    else
        reads = juce::String::formatted ("PEAK %+5.1f dBFS    LEN %.0f ms    END %s (%.1f Hz)",
                                         peakDb, durationMs, endNote.toRawUTF8(), endHz);
    if (viewMode != ViewMode::Spectrum)
        reads << (viewWindowMs > 0.0f ? juce::String::formatted ("    ZOOM %.2f ms", viewWindowMs)
                                      : juce::String ("    \xe2\x9f\xb2 scroll to zoom"));
    g.drawText (reads, leftReads.toNearestInt(), juce::Justification::centredLeft);

    // ---- View-mode tabs (top-right, clickable; hit-tested in mouseDown) ----
    layoutTabs();
    auto drawTab = [&] (juce::Rectangle<int> r, const char* label, ViewMode m)
    {
        const bool active = (viewMode == m);
        g.setColour (active ? accentHot.withAlpha (0.22f) : panelHi.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (active ? accentHot : accentHot.withAlpha (0.30f));
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);
        g.setColour (active ? accentHot : textDim);
        g.setFont (KickFonts::ui (9.0f, active));
        g.drawText (label, r, juce::Justification::centred);
    };
    drawTab (tabWave,      "WAVE",      ViewMode::Wave);
    drawTab (tabTransient, "TRANS",     ViewMode::Transient);
    drawTab (tabSpectrum,  "SPECTRUM",  ViewMode::Spectrum);
    drawTab (tabBoth,      "BOTH",      ViewMode::Both);

    g.setColour (accentHot.withAlpha (0.15f));
    g.drawHorizontalLine ((int) headerStrip.getBottom(), bounds.getX() + 8.0f, bounds.getRight() - 8.0f);

    // ---- Dispatch by view mode ----
    if (viewMode == ViewMode::Spectrum)
    {
        paintSpectrum (g, bounds.toNearestInt());
        return;
    }
    if (viewMode == ViewMode::Transient)
    {
        // Bare transient/sample layer, forced oscilloscope so the shape reads.
        paintWave (g, bounds, transientBuf, /*forceScope*/ true, /*isTransientView*/ true);
        return;
    }
    if (viewMode == ViewMode::Both)
    {
        auto top = bounds;
        auto bottom = top.removeFromBottom (bounds.getHeight() * 0.5f);
        paintWave (g, top, renderBuf, /*forceScope*/ false, /*isTransientView*/ false);
        paintSpectrum (g, bottom.toNearestInt());
        return;
    }

    // ViewMode::Wave (default)
    paintWave (g, bounds, renderBuf, /*forceScope*/ false, /*isTransientView*/ false);
}

//==============================================================================
// WAVE view — the audio waveform is the hero: it fills the WHOLE plot (centered
// + mirrored) with a center-bright vertical gradient, a soft outer glow, a dense
// RMS core, and a crisp bright edge. The pitch and amp envelopes are demoted to
// thin, low-alpha overlay lines so they still inform without competing.
//==============================================================================
void WaveformDisplay::paintWave (juce::Graphics& g, juce::Rectangle<float> bounds,
                                 const juce::AudioBuffer<float>& buf, bool forceScope, bool isTransientView)
{
    using namespace KickColors;

    // ---- Plot area (inside the canvas, accounting for axis gutters) ----
    auto plot = bounds.reduced (0, 4);
    auto leftAxis = plot.removeFromLeft (kLeftAxisPx);
    auto bottomAxis = plot.removeFromBottom (kBottomAxisPx);

    const float plotTop    = plot.getY();
    const float plotBottom = plot.getBottom();
    const float plotLeft   = plot.getX();
    const float plotRight  = plot.getRight();
    const float plotMid    = (plotTop + plotBottom) * 0.5f;
    const float halfH      = (plotBottom - plotTop) * 0.5f;
    const float ampH       = halfH * 0.95f;
    const float plotWf     = plotRight - plotLeft;

    // Visible window (zoom anchored at t=0). 0 = full duration.
    const float winMs = (viewWindowMs > 0.0f) ? juce::jlimit (kMinZoomMs, durationMs, viewWindowMs)
                                              : durationMs;

    auto xForMs = [&] (float ms) noexcept
    { return plotLeft + (winMs > 0.0f ? (ms / winMs) : 0.0f) * plotWf; };
    auto yFor   = [&] (float v)  noexcept { return plotMid - ampH * v; };   // +1 → up

    // "Nice" axis step so we get ~4–6 labels regardless of zoom.
    auto niceStep = [] (float win) noexcept
    {
        const float cands[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1, 2, 5, 10, 20, 25, 50, 100, 200, 500 };
        for (float s : cands) if (win / s <= 6.0f) return s;
        return 1000.0f;
    };
    const float step = (winMs > 0.0f) ? niceStep (winMs) : 50.0f;

    // ---- 1. Grid (minimal) — time verticals + center & ±0.5 amp lines ----
    if (winMs > 0.0f)
    {
        g.setColour (gridLine);
        for (float ms = step; ms < winMs; ms += step)
            g.drawVerticalLine ((int) xForMs (ms), plotTop, plotBottom);
    }
    g.setColour (gridLine);
    g.drawHorizontalLine ((int) (plotMid - halfH * 0.5f), plotLeft, plotRight);   // +0.5
    g.drawHorizontalLine ((int) (plotMid + halfH * 0.5f), plotLeft, plotRight);   // -0.5
    g.setColour (gridBeat);
    g.drawHorizontalLine ((int) plotMid, plotLeft, plotRight);                    // amp 0 axis

    // ---- Left axis: amplitude labels ----
    g.setColour (textGhost);
    g.setFont (KickFonts::mono (8.0f));
    g.drawText ("+1", (int) leftAxis.getX(), (int) (plotTop - 1),
                (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);
    g.drawText ("0",  (int) leftAxis.getX(), (int) (plotMid - 6.0f),
                (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);
    g.drawText ("-1", (int) leftAxis.getX(), (int) (plotBottom - 11),
                (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);

    // ---- Bottom axis: time ms (adaptive step) ----
    if (winMs > 0.0f)
    {
        for (float ms = 0.0f; ms <= winMs + 0.01f; ms += step)
        {
            const float x = xForMs (ms);
            const juce::String lbl = (step < 0.5f) ? juce::String (ms, 2)
                                   : (step < 1.0f) ? juce::String (ms, 1)
                                                   : juce::String ((int) ms);
            g.drawText (lbl, (int) (x - 16), (int) bottomAxis.getY(),
                        32, (int) bottomAxis.getHeight(), juce::Justification::centred);
        }
    }

    // ---- 2. Waveform — the hero. Adaptive: oscilloscope when zoomed in enough to
    //      resolve individual cycles, mirrored min/max hull when zoomed out. ----
    if (buf.getNumSamples() > 0 && winMs > 0.0f)
    {
        const int    n = buf.getNumSamples();
        const float* d = buf.getReadPointer (0);
        const int    plotW = juce::jmax (1, (int) std::ceil (plotWf));
        const int    visN  = juce::jlimit (1, n, (int) std::round (winMs * 0.001f * (float) renderSampleRate));
        const float  samplesPerPx = (float) visN / (float) plotW;

        if (forceScope || samplesPerPx < 2.5f)
        {
            // ---- OSCILLOSCOPE: trace the actual samples (modern, shows the wiggle
            //      — essential for transient shaping). Filled to the center axis. ----
            juce::Path fill, line;
            fill.startNewSubPath (plotLeft, plotMid);
            const int pts = juce::jmax (visN, 2);
            for (int i = 0; i < pts; ++i)
            {
                const float frac = (float) i / (float) (pts - 1);
                const int   si   = juce::jlimit (0, n - 1, (int) std::round (frac * (float) (visN - 1)));
                const float x = plotLeft + frac * plotWf;
                const float y = yFor (d[si]);
                fill.lineTo (x, y);
                if (i == 0) line.startNewSubPath (x, y);
                else        line.lineTo (x, y);
            }
            fill.lineTo (plotRight, plotMid);
            fill.closeSubPath();

            juce::ColourGradient grad (accentHot.withAlpha (0.06f), plotLeft, plotTop,
                                       accentHot.withAlpha (0.06f), plotLeft, plotBottom, false);
            grad.addColour (0.5, accentHot.withAlpha (0.42f));
            g.setGradientFill (grad);
            g.fillPath (fill);

            g.setColour (accentHot.withAlpha (0.18f));        // glow
            g.strokePath (line, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
            g.setColour (accentHot.brighter (0.35f));         // crisp trace
            g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
        else
        {
            // ---- HULL: per-column min/max mirrored silhouette (zoomed out). ----
            std::vector<float> colMin ((size_t) plotW, 0.0f), colMax ((size_t) plotW, 0.0f);
            for (int px = 0; px < plotW; ++px)
            {
                const int start = (int) (px * samplesPerPx);
                const int stop  = juce::jmin (visN, (int) std::ceil ((px + 1) * samplesPerPx));
                if (start >= stop) continue;
                float mn = 1.0f, mx = -1.0f;
                for (int i = start; i < stop; ++i)
                {
                    const float s = d[i];
                    if (s < mn) mn = s;
                    if (s > mx) mx = s;
                }
                colMin[(size_t) px] = mn;
                colMax[(size_t) px] = mx;
            }

            juce::Path peakHull;
            bool started = false;
            for (int px = 0; px < plotW; ++px)
            {
                const float x = plotLeft + (float) px;
                if (! started) { peakHull.startNewSubPath (x, yFor (colMax[(size_t) px])); started = true; }
                else             peakHull.lineTo (x, yFor (colMax[(size_t) px]));
            }
            for (int px = plotW - 1; px >= 0; --px)
                peakHull.lineTo (plotLeft + (float) px, yFor (colMin[(size_t) px]));
            peakHull.closeSubPath();

            g.setColour (accentHot.withAlpha (0.14f));        // glow
            g.strokePath (peakHull, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            juce::ColourGradient grad (accentHot.withAlpha (0.08f), plotLeft, plotTop,
                                       accentHot.withAlpha (0.08f), plotLeft, plotBottom, false);
            grad.addColour (0.5, accentHot.withAlpha (0.50f));
            g.setGradientFill (grad);
            g.fillPath (peakHull);
            g.setColour (accentHot.brighter (0.25f).withAlpha (0.95f));  // crisp edge
            g.strokePath (peakHull, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
        }
    }

    // Overlays sample the (full-duration) traces by TIME so they stay correct when
    // zoomed. traceFracForX maps a pixel column → index into a per-pixel trace.
    auto sampleTrace = [&] (const std::vector<float>& tr, float ms) -> float
    {
        if (tr.empty() || durationMs <= 0.0f) return 0.0f;
        const float f = juce::jlimit (0.0f, 1.0f, ms / durationMs);
        return tr[(size_t) juce::jlimit (0, (int) tr.size() - 1, (int) (f * (float) tr.size()))];
    };
    const int plotWpx = juce::jmax (2, (int) std::ceil (plotWf));

    // The amp/pitch overlays describe the BODY envelope — meaningless (and
    // misleading) over the soloed transient, so suppress them in that view.
    // ---- 3. Amp envelope — thin yellow overlay shell (top edge) ----
    if (! isTransientView && ! ampEnvTrace.empty() && winMs > 0.0f)
    {
        juce::Path envLine;
        for (int px = 0; px < plotWpx; ++px)
        {
            const float ms = (float) px / (float) (plotWpx - 1) * winMs;
            const float x  = plotLeft + (float) px / (float) (plotWpx - 1) * plotWf;
            const float y  = plotMid - ampH * sampleTrace (ampEnvTrace, ms);
            if (px == 0) envLine.startNewSubPath (x, y);
            else         envLine.lineTo (x, y);
        }
        g.setColour (envAmp.withAlpha (0.55f));
        g.strokePath (envLine, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
    }

    // ---- 4. Pitch envelope — thin cyan overlay across full height (log-Y) ----
    if (! isTransientView && ! pitchHzTrace.empty() && winMs > 0.0f)
    {
        juce::Path pitchPath;
        for (int px = 0; px < plotWpx; ++px)
        {
            const float ms = (float) px / (float) (plotWpx - 1) * winMs;
            const float x  = plotLeft + (float) px / (float) (plotWpx - 1) * plotWf;
            const float y  = logFreqToY (sampleTrace (pitchHzTrace, ms), plotTop, plotBottom);
            if (px == 0) pitchPath.startNewSubPath (x, y);
            else         pitchPath.lineTo (x, y);
        }
        g.setColour (envPitch.withAlpha (0.10f));
        g.strokePath (pitchPath, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
        g.setColour (envPitch.withAlpha (0.55f));
        g.strokePath (pitchPath, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
        g.setColour (envPitch.withAlpha (0.6f));
        g.setFont (KickFonts::mono (8.0f));
        g.drawText ("PITCH", (int) (plotRight - 42), (int) (plotTop + 2), 40, 10,
                    juce::Justification::centredRight);
    }

    // ---- 5. Playhead ----
    if (processor.getEngine().isActive() || voiceWasActive)
    {
        const int playPos = processor.getEngine().getPlaybackSamplePos();
        const float playMs = (float) playPos / 48.0f;   // approx, since realtime SR may differ
        if (winMs > 0.0f && playMs <= winMs)
        {
            const float x = xForMs (playMs);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawVerticalLine ((int) x - 1, plotTop, plotBottom);
            g.drawVerticalLine ((int) x + 1, plotTop, plotBottom);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) x, plotTop, plotBottom);
        }
    }
}

//==============================================================================
// Spectrum view: log-frequency X (20 Hz..20 kHz), dB Y (0 dB top .. -120 dB
// bottom), filled curve in the green spectrum accent. Uses the cached
// SpectrumResult (recomputed only on re-render, never here).
//==============================================================================
void WaveformDisplay::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> areaInt)
{
    using namespace KickColors;

    auto area = areaInt.toFloat().reduced (0.0f, 4.0f);
    auto leftAxis   = area.removeFromLeft (kLeftAxisPx);
    auto bottomAxis = area.removeFromBottom (kBottomAxisPx);

    const float pTop    = area.getY();
    const float pBottom = area.getBottom();
    const float pLeft   = area.getX();
    const float pRight  = area.getRight();

    constexpr float fMin = 20.0f;
    constexpr float fMax = 20000.0f;
    constexpr float dbTop = 0.0f;
    constexpr float dbBot = -120.0f;

    auto xForHz = [&] (float hz) noexcept
    {
        hz = juce::jlimit (fMin, fMax, hz);
        const float t = std::log (hz / fMin) / std::log (fMax / fMin);   // 0..1
        return juce::jmap (t, 0.0f, 1.0f, pLeft, pRight);
    };
    auto yForDb = [&] (float db) noexcept
    {
        db = juce::jlimit (dbBot, dbTop, db);
        return juce::jmap (db, dbTop, dbBot, pTop, pBottom);             // 0 dB top, -120 bottom
    };

    // ---- Grid: dB horizontals ----
    g.setColour (textGhost);
    g.setFont (KickFonts::mono (8.0f));
    for (float db = 0.0f; db >= -120.0f; db -= 30.0f)
    {
        const float y = yForDb (db);
        g.setColour (gridLine);
        g.drawHorizontalLine ((int) y, pLeft, pRight);
        g.setColour (textGhost);
        g.drawText (juce::String ((int) db), (int) leftAxis.getX(), (int) (y - 6.0f),
                    (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);
    }

    // ---- Grid: frequency verticals + labels ----
    struct FLabel { float hz; const char* txt; };
    static const FLabel flabels[] = { {20.0f,"20"}, {100.0f,"100"}, {1000.0f,"1k"}, {10000.0f,"10k"} };
    for (auto& fl : flabels)
    {
        const float x = xForHz (fl.hz);
        g.setColour (gridLine);
        g.drawVerticalLine ((int) x, pTop, pBottom);
        g.setColour (textGhost);
        g.drawText (fl.txt, (int) (x - 14), (int) bottomAxis.getY(),
                    28, (int) bottomAxis.getHeight(), juce::Justification::centred);
    }

    // ---- Spectrum curve (filled + stroked) ----
    if (! spectrum.magsDb.empty() && spectrum.binHz > 0.0)
    {
        const int numBins = (int) spectrum.magsDb.size();
        juce::Path fill, line;
        bool started = false;
        float firstX = pLeft, lastX = pLeft;

        // Skip the DC bin (b=0); start at b=1.
        for (int b = 1; b < numBins; ++b)
        {
            const float hz = (float) ((double) b * spectrum.binHz);
            if (hz < fMin) continue;
            if (hz > fMax) break;

            const float x = xForHz (hz);
            const float y = yForDb (spectrum.magsDb[(size_t) b]);
            if (! started)
            {
                line.startNewSubPath (x, y);
                fill.startNewSubPath (x, pBottom);
                fill.lineTo (x, y);
                firstX = x;
                started = true;
            }
            else
            {
                line.lineTo (x, y);
                fill.lineTo (x, y);
            }
            lastX = x;
        }

        if (started)
        {
            fill.lineTo (lastX, pBottom);
            fill.lineTo (firstX, pBottom);
            fill.closeSubPath();

            // Member `spectrum` (the SpectrumResult) shadows KickColors::spectrum,
            // so qualify the colour explicitly.
            g.setColour (KickColors::spectrum.withAlpha (0.22f));
            g.fillPath (fill);
            g.setColour (KickColors::spectrum.withAlpha (0.95f));
            g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }
    }
    else
    {
        g.setColour (textGhost);
        g.setFont (KickFonts::ui (10.0f));
        g.drawText ("no signal", areaInt, juce::Justification::centred);
    }
}
