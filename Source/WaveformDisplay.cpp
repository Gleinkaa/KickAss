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
        "drive","tail_drive","invert_phase","output_gain"
    };
    for (auto* id : kIds)
        processor.apvts.addParameterListener (id, this);

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
        "drive","tail_drive","invert_phase","output_gain"
    };
    for (auto* id : kIds)
        processor.apvts.removeParameterListener (id, this);
}

void WaveformDisplay::parameterChanged (const juce::String&, float)
{
    dirty.store (true);
    debounceCountdown = debounceTicks;
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
}

void WaveformDisplay::mouseDown (const juce::MouseEvent&)
{
    // Click anywhere in the canvas = trigger preview
    processor.getEngine().triggerNote (60, 1.0f, 0);
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
    // 1. Read APVTS for envelope timing → total duration in ms
    const float ta  = *processor.apvts.getRawParameterValue ("vol_attack");
    const float th  = *processor.apvts.getRawParameterValue ("vol_hold");
    const float td1 = *processor.apvts.getRawParameterValue ("vol_decay_1");
    const float td2 = *processor.apvts.getRawParameterValue ("vol_decay_2");
    durationMs = juce::jmax (minRenderMs, (ta + th + td1 + td2) * renderHeadroomMult);

    // 2. Audio render via the offline engine (UI thread, safe)
    processor.offlineRender (renderBuf, durationMs);
    renderSampleRate = 48000.0;

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

        // --- Amp envelope (AHDSR) ---
        float a;
        if (t < p1End) {
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

        // Scoop
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
    g.drawText (juce::String::formatted ("PEAK %+5.1f dBFS    LEN %.0f ms    END %s (%.1f Hz)",
                                          peakDb, durationMs, endNote.toRawUTF8(), endHz),
                leftReads.toNearestInt(), juce::Justification::centredLeft);
    g.setColour (accentHot.withAlpha (0.60f));
    g.setFont (KickFonts::ui (10.0f, true));
    g.drawText ("WAVE", headerStrip.toNearestInt().reduced (12, 4), juce::Justification::centredRight);

    g.setColour (accentHot.withAlpha (0.15f));
    g.drawHorizontalLine ((int) headerStrip.getBottom(), bounds.getX() + 8.0f, bounds.getRight() - 8.0f);

    // ---- Plot area (inside the canvas, accounting for axis gutters) ----
    auto plot = bounds.reduced (0, 4);
    auto leftAxis = plot.removeFromLeft (kLeftAxisPx);
    auto bottomAxis = plot.removeFromBottom (kBottomAxisPx);

    const float plotTop    = plot.getY();
    const float plotBottom = plot.getBottom();
    const float plotLeft   = plot.getX();
    const float plotRight  = plot.getRight();
    const float plotMid    = (plotTop + plotBottom) * 0.5f;

    // Top half = pitch (log Y, 20..20k)
    // Bottom half = amp (linear, -1..+1)

    // ---- 1. Grid ----
    g.setColour (gridLine);
    // Vertical 10 ms minor lines, 50 ms major
    if (durationMs > 0.0f)
    {
        const float pxPerMs = (plotRight - plotLeft) / durationMs;
        for (float ms = 10.0f; ms < durationMs; ms += 10.0f)
        {
            const float x = plotLeft + ms * pxPerMs;
            const bool isMajor = std::fmod (ms, 50.0f) < 0.5f;
            g.setColour (isMajor ? gridBeat : gridLine);
            g.drawVerticalLine ((int) x, plotTop, plotBottom);
        }
    }
    // Horizontal: amp ±1, 0
    g.setColour (gridBeat);
    g.drawHorizontalLine ((int) plotMid,             plotLeft, plotRight);   // amp 0 / pitch+amp divider
    g.setColour (gridLine);
    g.drawHorizontalLine ((int) (plotMid * 0.5f + plotTop * 0.5f), plotLeft, plotRight);  // pitch mid
    g.drawHorizontalLine ((int) (plotMid * 0.5f + plotBottom * 0.5f), plotLeft, plotRight);  // amp +/-0.5

    // ---- Left axis labels: pitch Hz (log) ----
    g.setColour (textGhost);
    g.setFont (KickFonts::mono (8.0f));
    auto drawHzLabel = [&] (float hz, const char* label) {
        const float y = logFreqToY (hz, plotTop, plotMid);
        g.drawText (label, (int) leftAxis.getX(), (int) (y - 6.0f),
                    (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);
        g.setColour (gridLine);
        g.drawHorizontalLine ((int) y, plotLeft, plotRight);
        g.setColour (textGhost);
    };
    drawHzLabel (20.0f,    "20");
    drawHzLabel (100.0f,   "100");
    drawHzLabel (1000.0f,  "1k");
    drawHzLabel (10000.0f, "10k");

    // Amp labels
    g.drawText ("+1", (int) leftAxis.getX(), (int) (plotMid + 2),
                (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);
    g.drawText ("-1", (int) leftAxis.getX(), (int) (plotBottom - 12),
                (int) leftAxis.getWidth() - 2, 12, juce::Justification::centredRight);

    // ---- Bottom axis: time ms ----
    if (durationMs > 0.0f)
    {
        const float pxPerMs = (plotRight - plotLeft) / durationMs;
        for (float ms = 0.0f; ms <= durationMs; ms += 50.0f)
        {
            const float x = plotLeft + ms * pxPerMs;
            g.drawText (juce::String ((int) ms),
                        (int) (x - 14), (int) bottomAxis.getY(),
                        28, (int) bottomAxis.getHeight(),
                        juce::Justification::centred);
        }
    }

    // ---- 2. Scoop wash (BEFORE envelope so envelope draws on top) ----
    {
        const float scStart  = *processor.apvts.getRawParameterValue ("scoop_start");
        const float scLength = *processor.apvts.getRawParameterValue ("scoop_length");
        const float scDepth  = *processor.apvts.getRawParameterValue ("scoop_depth");
        if (scDepth > 0.5f && scLength > 0.0f && durationMs > 0.0f)
        {
            const float pxPerMs = (plotRight - plotLeft) / durationMs;
            const float sx1 = plotLeft + scStart * pxPerMs;
            const float sx2 = plotLeft + (scStart + scLength) * pxPerMs;
            g.setColour (envScoop.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (sx1, plotMid, sx2 - sx1, plotBottom - plotMid));
        }
    }

    // ---- 3. Pitch envelope (cyan, log-Y, top half) ----
    if (! pitchHzTrace.empty())
    {
        juce::Path pitchPath;
        for (size_t i = 0; i < pitchHzTrace.size(); ++i)
        {
            const float x = plotLeft + ((float) i / (float) pitchHzTrace.size()) * (plotRight - plotLeft);
            const float y = logFreqToY (pitchHzTrace[i], plotTop, plotMid);
            if (i == 0) pitchPath.startNewSubPath (x, y);
            else        pitchPath.lineTo (x, y);
        }
        // Halo glow
        g.setColour (envPitch.withAlpha (0.18f));
        g.strokePath (pitchPath, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
        // Crisp line
        g.setColour (envPitch);
        g.strokePath (pitchPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    // ---- 4. Waveform (red mirrored bars, bottom half) ----
    if (renderBuf.getNumSamples() > 0)
    {
        const int   n = renderBuf.getNumSamples();
        const float* d = renderBuf.getReadPointer (0);
        const float halfH = (plotBottom - plotMid);
        const int   plotW = juce::jmax (1, (int) (plotRight - plotLeft));
        const int   samplesPerPx = juce::jmax (1, n / plotW);

        g.setColour (accentHot.withAlpha (0.85f));
        for (int px = 0; px < plotW; ++px)
        {
            const int start = px * samplesPerPx;
            const int stop  = juce::jmin (n, start + samplesPerPx);
            if (start >= n) break;
            float mn =  1.0f, mx = -1.0f;
            for (int i = start; i < stop; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
            const float y1 = plotMid + halfH * mx * -0.95f;
            const float y2 = plotMid + halfH * mn * -0.95f;
            g.drawVerticalLine (plotLeft + px, juce::jmin (y1, y2), juce::jmax (y1, y2));
        }
    }

    // ---- 5. Amp envelope shading + yellow line (bottom half, ABOVE waveform) ----
    if (! ampEnvTrace.empty())
    {
        const float halfH = plotBottom - plotMid;
        juce::Path envFill, envLine;
        envFill.startNewSubPath (plotLeft, plotMid);
        for (size_t i = 0; i < ampEnvTrace.size(); ++i)
        {
            const float x = plotLeft + ((float) i / (float) ampEnvTrace.size()) * (plotRight - plotLeft);
            const float y = plotMid - halfH * ampEnvTrace[i] * 0.95f;
            envFill.lineTo (x, y);
            if (i == 0) envLine.startNewSubPath (x, y);
            else        envLine.lineTo (x, y);
        }
        envFill.lineTo (plotRight, plotMid);
        envFill.closeSubPath();

        g.setColour (envAmp.withAlpha (0.15f));
        g.fillPath (envFill);
        g.setColour (envAmp);
        g.strokePath (envLine, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    }

    // ---- 6. Playhead ----
    if (processor.getEngine().isActive() || voiceWasActive)
    {
        const int playPos = processor.getEngine().getPlaybackSamplePos();
        const float playMs = (float) playPos / 48.0f;   // approx, since realtime SR may differ
        if (durationMs > 0.0f && playMs < durationMs)
        {
            const float x = plotLeft + (playMs / durationMs) * (plotRight - plotLeft);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawVerticalLine ((int) x - 1, plotTop, plotBottom);
            g.drawVerticalLine ((int) x + 1, plotTop, plotBottom);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) x, plotTop, plotBottom);
        }
    }
}
