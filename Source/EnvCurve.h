#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <array>
#include <cmath>

//==============================================================================
// EnvCurve — a freely-edited envelope: ordered list of (time, value, tension)
// breakpoints, sampled to a 512-entry LUT for the audio thread.
//
// Invariants (enforced by the editor; not policed here):
//   - points.size() >= 2
//   - points[0].timeMs   == 0
//   - points.back().value == 0
//   - points are sorted ascending by timeMs
//
// Tension semantics: tension on point A controls the shape of the segment
// from A → next.  Range [-1..+1].
//   tn = 0    → linear segment
//   tn > 0    → ease-out  (fast start, slow finish — concave-up if rising)
//   tn < 0    → ease-in   (slow start, fast finish — concave-down if rising)
// Magnitude controls how aggressive the bias is.
//
// This file is pure data + math + serialization. No threading. The audio
// thread takes its own copy via fillLut() at noteOn; see KickEngine.
//==============================================================================

struct EnvPoint
{
    float timeMs  = 0.0f;
    float value   = 0.0f;   // 0..1
    float tension = 0.0f;   // -1..+1, segment leaving THIS point toward the next
};

class EnvCurve
{
public:
    EnvCurve() = default;

    std::vector<EnvPoint> points;

    static constexpr int kLutSize = 512;

    //----------------------------------------------------------------------
    float getTotalMs() const noexcept
    {
        return points.empty() ? 0.0f : points.back().timeMs;
    }

    //----------------------------------------------------------------------
    // Single segment evaluator. `t01` is 0..1 across the segment from a→b.
    static float sampleSegment (const EnvPoint& a, const EnvPoint& b, float t01) noexcept
    {
        t01 = juce::jlimit (0.0f, 1.0f, t01);
        const float tn = juce::jlimit (-1.0f, 1.0f, a.tension);
        // Bias curve: t' = t01 ^ k.
        //   tn = +1 → k ≈ 0.25 (fast start, slow finish)
        //   tn =  0 → k = 1 (linear)
        //   tn = -1 → k ≈ 4 (slow start, fast finish)
        const float k = std::pow (4.0f, -tn);
        const float warped = (t01 <= 0.0f) ? 0.0f
                          : (t01 >= 1.0f) ? 1.0f
                                          : std::pow (t01, k);
        return a.value + (b.value - a.value) * warped;
    }

    //----------------------------------------------------------------------
    // Linear-in-time sample. OK because envelopes have ≤16 points.
    float sampleAtMs (float t) const noexcept
    {
        if (points.size() < 2) return 0.0f;
        if (t <= points.front().timeMs) return points.front().value;
        if (t >= points.back ().timeMs) return points.back ().value;
        for (size_t i = 1; i < points.size(); ++i)
        {
            if (t <= points[i].timeMs)
            {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                const float span = juce::jmax (1e-6f, b.timeMs - a.timeMs);
                return sampleSegment (a, b, (t - a.timeMs) / span);
            }
        }
        return points.back().value;
    }

    //----------------------------------------------------------------------
    // 512-entry LUT spanning [0..totalMs]. Audio thread uses index = floor(t * 511/totalMs).
    void fillLut (std::array<float, kLutSize>& lut) const noexcept
    {
        const float total = getTotalMs();
        if (total <= 0.0f || points.size() < 2)
        {
            lut.fill (0.0f);
            return;
        }
        const float step = total / (float) (kLutSize - 1);
        for (int i = 0; i < kLutSize; ++i)
            lut[(size_t) i] = juce::jlimit (0.0f, 1.0f, sampleAtMs ((float) i * step));
    }

    //----------------------------------------------------------------------
    // AHDSR → 5-point curve. Used for first-load auto-convert and for the
    // "go Advanced" toggle when no <Curve> exists yet.
    //   curve > 1 → concave decay (Python default); curve < 1 → convex.
    static EnvCurve fromAhdsr (float attackMs, float holdMs, float decay1Ms,
                                float sustain01, float decay2Ms, float curveExp)
    {
        // Map exponent to tension. The decay code does `pow(1-x, curve)`, so
        // curve > 1 = slow start then fast drop = tension < 0 on the falling segment.
        const float decayTn  = juce::jlimit (-1.0f, 1.0f, (1.0f - curveExp) / (1.0f + curveExp));
        // Attack does `pow(x, 1/curve)` — flips sign.
        const float attackTn = -decayTn;

        const float p1 = attackMs;
        const float p2 = p1 + holdMs;
        const float p3 = p2 + decay1Ms;
        const float p4 = p3 + decay2Ms;

        EnvCurve c;
        c.points.reserve (5);
        c.points.push_back ({ 0.0f, 0.0f,       attackTn });   // origin
        c.points.push_back ({ p1,   1.0f,       0.0f     });   // attack peak
        c.points.push_back ({ p2,   1.0f,       decayTn  });   // hold end
        c.points.push_back ({ p3,   sustain01,  decayTn  });   // decay1 end (sustain)
        c.points.push_back ({ p4,   0.0f,       0.0f     });   // tail end
        return c;
    }

    //----------------------------------------------------------------------
    // Serialization into an apvts.state child ValueTree.
    // Layout: <Curve id="vol_env"><Pt t v tn/>...</Curve>
    void toValueTree (juce::ValueTree& vt, const juce::String& id) const
    {
        vt.removeAllChildren (nullptr);
        vt.setProperty ("id", id, nullptr);
        for (const auto& p : points)
        {
            juce::ValueTree pt ("Pt");
            pt.setProperty ("t",  p.timeMs,  nullptr);
            pt.setProperty ("v",  p.value,   nullptr);
            pt.setProperty ("tn", p.tension, nullptr);
            vt.addChild (pt, -1, nullptr);
        }
    }

    void fromValueTree (const juce::ValueTree& vt)
    {
        points.clear();
        for (int i = 0; i < vt.getNumChildren(); ++i)
        {
            auto pt = vt.getChild (i);
            if (pt.getType().toString() == "Pt")
                points.push_back ({
                    (float) pt.getProperty ("t",  0.0f),
                    (float) pt.getProperty ("v",  0.0f),
                    (float) pt.getProperty ("tn", 0.0f)
                });
        }
    }

    //----------------------------------------------------------------------
    // Mutation helpers used by the editor. Keep invariants intact.
    void clampToInvariants (float minTotalMs = 20.0f)
    {
        if (points.size() < 2)
        {
            points.clear();
            points.push_back ({ 0.0f, 0.0f, 0.0f });
            points.push_back ({ minTotalMs, 0.0f, 0.0f });
            return;
        }
        // Lock first point time and last point value.
        points.front().timeMs = 0.0f;
        points.back ().value  = 0.0f;
        // Sort + dedupe-ish (no two points at identical t).
        std::sort (points.begin(), points.end(),
                   [] (const EnvPoint& a, const EnvPoint& b) { return a.timeMs < b.timeMs; });
        for (size_t i = 1; i < points.size(); ++i)
            if (points[i].timeMs <= points[i - 1].timeMs)
                points[i].timeMs = points[i - 1].timeMs + 0.1f;
        // Clamp values to [0..1] and tensions to [-1..+1].
        for (auto& p : points)
        {
            p.value   = juce::jlimit (0.0f, 1.0f, p.value);
            p.tension = juce::jlimit (-1.0f, 1.0f, p.tension);
        }
    }
};
