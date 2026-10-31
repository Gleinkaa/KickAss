#include "BreakpointEditor.h"
#include "KickAssLookAndFeel.h"
#include <cmath>

//==============================================================================
BreakpointEditor::BreakpointEditor (KickAssProcessor& p) : processor (p)
{
    setOpaque (false);            // we paint over the existing waveform canvas
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setInterceptsMouseClicks (true, false);
}

//==============================================================================
// Thread safety (B2): all reads snapshot the points under the processor's
// spinlock and work on the copy (setStateInformation may mutate the curve
// from a non-message host thread under the same lock). Edits are computed on
// the snapshot and published back under the lock — the critical section only
// ever holds a copy or a move-assign, never a whole pixel loop.
std::vector<EnvPoint> BreakpointEditor::snapshotPoints() const
{
    juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
    return processor.getVolEnvCurve().points;
}

void BreakpointEditor::publishPoints (std::vector<EnvPoint>&& edited)
{
    juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
    processor.getVolEnvCurve().points = std::move (edited);
}

float BreakpointEditor::currentTotalMs() const
{
    const auto pts = snapshotPoints();
    return pts.empty() ? 0.0f : pts.back().timeMs;
}

juce::Point<float>
BreakpointEditor::pointToPixel (const EnvPoint& p, float totalMs) const noexcept
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const float x = (totalMs > 0.0f) ? (p.timeMs / totalMs) * w : 0.0f;
    const float y = h - juce::jlimit (0.0f, 1.0f, p.value) * h;
    return { x, y };
}

EnvPoint
BreakpointEditor::pixelToPoint (juce::Point<float> px, float totalMs) const noexcept
{
    const float w = juce::jmax (1.0f, (float) getWidth());
    const float h = juce::jmax (1.0f, (float) getHeight());
    EnvPoint p;
    p.timeMs  = juce::jlimit (0.0f, totalMs, (px.x / w) * totalMs);
    p.value   = juce::jlimit (0.0f, 1.0f, 1.0f - (px.y / h));
    p.tension = 0.0f;
    return p;
}

int BreakpointEditor::hitPointIndex (juce::Point<float> px) const
{
    const auto pts = snapshotPoints();
    if (pts.empty()) return -1;
    const float total = pts.back().timeMs;
    int bestIdx = -1;
    float bestDist2 = kHitRadiusPx * kHitRadiusPx;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const auto pp = pointToPixel (pts[i], total);
        const float dx = pp.x - px.x;
        const float dy = pp.y - px.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) { bestDist2 = d2; bestIdx = (int) i; }
    }
    return bestIdx;
}

int BreakpointEditor::hitTensionHandle (juce::Point<float> px) const
{
    // Tension handle lives at the midpoint of each segment (visible only when
    // |tension| > 0.05 — but we still hit-test even when invisible so users
    // can grab a near-zero tension and drag it.)
    const auto pts = snapshotPoints();
    if (pts.size() < 2) return -1;
    const float total = pts.back().timeMs;
    const float hitR2 = (kHitRadiusPx * 0.7f) * (kHitRadiusPx * 0.7f);
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const auto& a = pts[i];
        const auto& b = pts[i + 1];
        const float midT = 0.5f * (a.timeMs + b.timeMs);
        const float midV = EnvCurve::sampleSegment (a, b, 0.5f);
        const auto mid = pointToPixel ({ midT, midV, 0.0f }, total);
        const float dx = mid.x - px.x;
        const float dy = mid.y - px.y;
        if (dx * dx + dy * dy < hitR2)
            return (int) i;
    }
    return -1;
}

//==============================================================================
void BreakpointEditor::commitEdit()
{
    {
        // Editor mutates the curve directly via getVolEnvCurve(); take the
        // spinlock only for the clamp pass to keep audio thread reads consistent.
        juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
        processor.getVolEnvCurve().clampToInvariants();
    }
    processor.syncVolCurveToValueTree();   // persists + publishes to engines
    processor.setVolCurveMode ("custom");  // any user edit pins the curve; no more AHDSR refresh
    if (onCurveCommitted) onCurveCommitted();
    repaint();
}

//==============================================================================
void BreakpointEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto px = e.position;
    const bool shift = e.mods.isShiftDown();
    const bool right = e.mods.isRightButtonDown();

    const int idx = hitPointIndex (px);

    if (right)
    {
        // Delete interior points only. `idx` was hit-tested against a snapshot;
        // re-validate the range under the lock before erasing (the curve may
        // have been replaced by a host setStateInformation meanwhile).
        bool erased = false;
        {
            juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
            auto& live = processor.getVolEnvCurve().points;
            if (idx > 0 && idx + 1 < (int) live.size())
            {
                live.erase (live.begin() + idx);
                erased = true;
            }
        }
        if (erased)
            commitEdit();
        return;
    }

    if (shift)
    {
        // Tension drag from THIS point's outgoing segment (if any).
        const int tnIdx = (idx >= 0) ? idx : hitTensionHandle (px);
        if (tnIdx >= 0 && tnIdx + 1 < (int) snapshotPoints().size())
        {
            dragPointIdx       = -1;
            dragTensionSeg     = tnIdx;
            isDraggingTension  = true;
            return;
        }
    }

    if (idx >= 0)
    {
        dragPointIdx      = idx;
        isDraggingTension = false;
        dragTensionSeg    = -1;
        return;
    }

    // Click on empty space — also try a tension handle.
    const int tnIdx = hitTensionHandle (px);
    if (tnIdx >= 0)
    {
        dragPointIdx      = -1;
        dragTensionSeg    = tnIdx;
        isDraggingTension = true;
    }
}

void BreakpointEditor::mouseDrag (const juce::MouseEvent& e)
{
    // Work on a snapshot so the clamp/drag math below never races a concurrent
    // setStateInformation mutation of the live vector (B2). The edited copy is
    // published back under the lock; commitEdit() then re-clamps invariants.
    auto pts = snapshotPoints();
    const float total = pts.empty() ? 0.0f : pts.back().timeMs;
    if (pts.size() < 2) return;

    if (isDraggingTension && dragTensionSeg >= 0
        && dragTensionSeg + 1 < (int) pts.size())
    {
        // Tension is set from the vertical offset of the cursor relative to the
        // straight-line midpoint of the segment. Positive offset (cursor higher
        // than line midpoint) → tension > 0 (ease-out / bowed up).
        const auto& a = pts[(size_t) dragTensionSeg];
        const auto& b = pts[(size_t) dragTensionSeg + 1];
        const auto midPx = pointToPixel ({ 0.5f * (a.timeMs + b.timeMs),
                                            0.5f * (a.value + b.value), 0.0f }, total);
        const float dyPx = midPx.y - e.position.y;   // up = positive
        const float h = juce::jmax (1.0f, (float) getHeight());
        // Map ± half-height to ±1.
        const float tn = juce::jlimit (-1.0f, 1.0f, (dyPx / (h * 0.5f)));
        pts[(size_t) dragTensionSeg].tension = tn;
        publishPoints (std::move (pts));
        commitEdit();
        return;
    }

    if (dragPointIdx >= 0 && dragPointIdx < (int) pts.size())
    {
        auto& p = pts[(size_t) dragPointIdx];
        const bool isFirst = (dragPointIdx == 0);
        const bool isLast  = (dragPointIdx == (int) pts.size() - 1);

        // Convert pixel to candidate point coords using the CURRENT total.
        // For the last point we let totalMs grow/shrink with drag-x; clamp later.
        float targetTotal = total;
        if (isLast)
        {
            const float w = juce::jmax (1.0f, (float) getWidth());
            const float newTotal = juce::jlimit (kMinTotalMs, kMaxTotalMs,
                                                  (e.position.x / w) * juce::jmax (total, kMinTotalMs));
            targetTotal = juce::jmax (kMinTotalMs, newTotal);
        }

        const auto candidate = pixelToPoint (e.position, juce::jmax (1.0f, targetTotal));
        if (isFirst)
        {
            // x locked at 0; only y editable
            p.value = candidate.value;
            p.timeMs = 0.0f;
        }
        else if (isLast)
        {
            // y locked at 0; only x editable (sets total duration)
            p.value  = 0.0f;
            p.timeMs = juce::jlimit (kMinTotalMs, kMaxTotalMs, targetTotal);
            // Re-clamp interior points so none exceed the new last — CUMULATIVELY,
            // walking backwards from the last point so every gap stays ≥ 0.1 ms
            // (B11: the old last-0.5 clamp could stack interior points at equal
            // times, leaving a later interior drag with minT > maxT).
            for (int i = (int) pts.size() - 2; i >= 1; --i)
                pts[(size_t) i].timeMs = juce::jmin (pts[(size_t) i].timeMs,
                                                     pts[(size_t) (i + 1)].timeMs - 0.1f);
        }
        else
        {
            // Free 2-D drag; respect neighbour times. If a degenerate curve ever
            // leaves minT > maxT, fall back to the midpoint instead of handing
            // jlimit an inverted range (jassert in debug builds).
            const float minT = pts[(size_t) dragPointIdx - 1].timeMs + 0.1f;
            const float maxT = pts[(size_t) dragPointIdx + 1].timeMs - 0.1f;
            p.timeMs = (minT <= maxT) ? juce::jlimit (minT, maxT, candidate.timeMs)
                                       : 0.5f * (minT + maxT);
            p.value  = candidate.value;
        }
        publishPoints (std::move (pts));
        commitEdit();
    }
}

void BreakpointEditor::mouseUp (const juce::MouseEvent&)
{
    dragPointIdx      = -1;
    dragTensionSeg    = -1;
    isDraggingTension = false;
}

void BreakpointEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Add a new point at the cursor if not on top of an existing one.
    if (hitPointIndex (e.position) >= 0) return;

    const auto pts = snapshotPoints();
    const float total = pts.empty() ? 0.0f : pts.back().timeMs;
    if (pts.size() >= 16) return;   // cap

    const auto np = pixelToPoint (e.position, juce::jmax (1.0f, total));
    bool added = false;
    {
        // Re-check the cap under the lock — the live curve may have changed
        // since the snapshot.
        juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
        auto& live = processor.getVolEnvCurve().points;
        if (live.size() < 16)
        {
            live.push_back (np);
            std::sort (live.begin(), live.end(),
                       [] (const EnvPoint& a, const EnvPoint& b) { return a.timeMs < b.timeMs; });
            added = true;
        }
    }
    if (added)
        commitEdit();
}

void BreakpointEditor::mouseMove (const juce::MouseEvent& e)
{
    // Cursor feedback: pointing-hand on a point, crosshair otherwise.
    if (hitPointIndex (e.position) >= 0 || hitTensionHandle (e.position) >= 0)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//==============================================================================
void BreakpointEditor::paint (juce::Graphics& g)
{
    using namespace KickColors;

    // Snapshot the whole curve under the spinlock (same pattern as
    // WaveformDisplay::recomputeEnvelopeTraces); painting then works purely on
    // the copy so a host-thread setStateInformation can't tear the read or
    // reallocate the points vector mid-loop (B2).
    EnvCurve curveSnap;
    {
        juce::SpinLock::ScopedLockType l (processor.getVolCurveLock());
        curveSnap = processor.getVolEnvCurve();
    }
    const auto& pts = curveSnap.points;
    if (pts.size() < 2) return;

    const float total = curveSnap.getTotalMs();
    const float w = (float) getWidth();
    const float h = (float) getHeight();

    // ---- 1. Curve polyline (samples the live interpolator at pixel resolution) ----
    juce::Path line;
    {
        const int px0 = 0;
        const int pxN = juce::jmax (2, (int) w);
        for (int px = px0; px < pxN; ++px)
        {
            const float t = ((float) px / (float) pxN) * total;
            const float v = curveSnap.sampleAtMs (t);
            const float y = h - juce::jlimit (0.0f, 1.0f, v) * h;
            if (px == px0) line.startNewSubPath ((float) px, y);
            else           line.lineTo          ((float) px, y);
        }
    }

    // Fill the area under the line with a subtle wash.
    {
        juce::Path fill = line;
        fill.lineTo (w, h);
        fill.lineTo (0.0f, h);
        fill.closeSubPath();
        g.setColour (envAmp.withAlpha (0.10f));
        g.fillPath (fill);
    }

    // Halo + crisp stroke
    g.setColour (envAmp.withAlpha (0.22f));
    g.strokePath (line, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
    g.setColour (envAmp);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    // ---- 2. Tension handles (visible when |tn| > 0.05) ----
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const float tn = pts[i].tension;
        if (std::abs (tn) < 0.05f) continue;
        const float midT = 0.5f * (pts[i].timeMs + pts[i + 1].timeMs);
        const float midV = EnvCurve::sampleSegment (pts[i], pts[i + 1], 0.5f);
        const auto mp = pointToPixel ({ midT, midV, 0.0f }, total);
        g.setColour (envAmp.withAlpha (0.55f));
        g.fillEllipse (mp.x - kTensionHandleRadPx, mp.y - kTensionHandleRadPx,
                       2.0f * kTensionHandleRadPx, 2.0f * kTensionHandleRadPx);
    }

    // ---- 3. Points: filled outer dot + dark inner dot for contrast ----
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const auto p = pointToPixel (pts[i], total);
        const bool isEnd = (i == 0 || i + 1 == pts.size());

        // Outer disc
        g.setColour (isEnd ? envAmp.withAlpha (0.85f) : envAmp);
        g.fillEllipse (p.x - kPointRadiusPx, p.y - kPointRadiusPx,
                       2.0f * kPointRadiusPx, 2.0f * kPointRadiusPx);

        // Inner hole (canvas colour) for contrast
        const float ir = kPointRadiusPx * 0.45f;
        g.setColour (canvasBg);
        g.fillEllipse (p.x - ir, p.y - ir, 2.0f * ir, 2.0f * ir);
    }
}
