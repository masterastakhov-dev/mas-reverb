#pragma once
// MASTER SPACE 1.8 - M/S OUTPUT COMPRESSOR (own algorithm).
//
// One instance for the whole plugin. It never replaces a signal: it returns the change it makes,
//      delta = comp(x) - x,      and the caller adds  mixGain * delta  to the output.
//   WET ONLY    x = the wet part of the output (a tap taken where dry and wet are mixed, carried through every
//               cross-fade with the same gains); the dry signal is not touched at all.
//   FULL OUTPUT x = the whole output (after dry / wet, OUTPUT and A/B MATCH).
//
// Per sample:  M = (L + R) / 2,  S = (L - R) / 2   (exact inverse L = M + S, R = M - S)
//   detector    M, S through an optional side-chain high-pass (detector only), mean square over 5 ms
//   gain        soft-knee curve (threshold, ratio, knee) in dB; gain reduction smoothed with ATTACK when it
//               grows and RELEASE when it falls (one-pole, in dB); MAKEUP applied after
//   output      M' = M * gM, S' = S * gS  ->  L' = M' + S', R' = M' - S'
// MID and SIDE are fully independent (no link). No look-ahead, no latency. Every control is smoothed
// (20 ms); ON / OFF and a change of routing are 20 ms fades of the change it makes: no clicks.
// When off and faded out, isActive() is false and the caller does not run it at all (bit-identical).
// No allocation, no locks: safe on the audio thread. GR telemetry through atomics.
#include <algorithm>
#include <atomic>
#include <cmath>

namespace msc
{
struct Band
{
    float thresholdDb = -24.0f;     // -60 .. 0 dBFS
    float ratio = 2.0f;             // 1 .. 10
    float attackMs = 20.0f;         // 0.1 .. 100
    float releaseMs = 250.0f;       // 30 .. 3000
    float kneeDb = 6.0f;            // 0 .. 12
    float makeupDb = 0.0f;          // -12 .. +12
};

struct Settings
{
    bool on = false;
    int routing = 0;                // 0 WET ONLY, 1 FULL OUTPUT
    float mix = 1.0f;               // PARALLEL MIX 0 .. 1
    bool hpfOn = true;
    float hpfHz = 120.0f;           // side-chain high-pass, 20 .. 500 Hz
    Band mid, side { -24.0f, 2.0f, 30.0f, 400.0f, 6.0f, 0.0f };
};

class MSCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        aDet = coef (0.005);
        aCtl = coef (0.020);
        fadeStep = (float) (1.0 / (0.020 * fs));
        reset();
        set (settings, true);
    }

    void reset()
    {
        for (auto& c : ch) c = Chan {};
        fade = 0.0f;
        routingNow = settings.routing;
        grMid.store (0.0f, std::memory_order_relaxed);
        grSide.store (0.0f, std::memory_order_relaxed);
    }

    void set (const Settings& s, bool force = false)
    {
        settings = s;
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
        settings.hpfHz = std::clamp (s.hpfHz, 20.0f, 500.0f);
        for (int b = 0; b < 2; ++b)
        {
            Band& d = b == 0 ? settings.mid : settings.side;
            d.thresholdDb = std::clamp (d.thresholdDb, -60.0f, 0.0f);
            d.ratio = std::clamp (d.ratio, 1.0f, 10.0f);
            d.attackMs = std::clamp (d.attackMs, 0.1f, 100.0f);
            d.releaseMs = std::clamp (d.releaseMs, 30.0f, 3000.0f);
            d.kneeDb = std::clamp (d.kneeDb, 0.0f, 12.0f);
            d.makeupDb = std::clamp (d.makeupDb, -12.0f, 12.0f);
            ch[b].aAtt = coef (d.attackMs * 0.001);
            ch[b].aRel = coef (d.releaseMs * 0.001);
            ch[b].hp.highPass (settings.hpfHz, fs);
        }
        // nothing audible yet (faded out, or just turned on): a new routing is taken at once, with a clean
        // detector - it must never run on the signal of the other routing first
        if (fade <= 0.0f && routingNow != settings.routing)
        {
            routingNow = settings.routing;
            for (auto& c : ch) { c.ms = 0.0; c.grS = 0.0f; c.hp.reset(); }
        }
        if (force)
        {
            routingNow = settings.routing;
            fade = settings.on ? 1.0f : 0.0f;
            mixNow = settings.mix;
            for (int b = 0; b < 2; ++b)
            {
                const Band& d = b == 0 ? settings.mid : settings.side;
                ch[b].thr = d.thresholdDb; ch[b].ratio = d.ratio; ch[b].knee = d.kneeDb; ch[b].makeup = d.makeupDb;
            }
        }
    }

    bool isActive() const { return settings.on || fade > 0.0f; }
    // the routing the compressor is running in right now (a change waits for the fade-out)
    int routing() const { return routingNow; }
    const Settings& getSettings() const { return settings; }

    // x: what is compressed (read only); y: where  mix * (comp(x) - x)  is added. x and y may be the same
    // buffers (FULL OUTPUT, in place). forRouting: the routing of this call; it does nothing in the other one.
    void process (const float* xL, const float* xR, float* yL, float* yR, int n, int forRouting)
    {
        if (! isActive() || forRouting != routingNow) return;      // only the call of the routing it runs in
        float maxGr[2] = { 0.0f, 0.0f };
        const bool switching = settings.routing != routingNow;
        for (int i = 0; i < n; ++i)
        {
            // fade: in when on (and in the wanted routing), out when off or the routing is changing
            const bool up = settings.on && ! switching;
            fade = up ? std::min (1.0f, fade + fadeStep) : std::max (0.0f, fade - fadeStep);
            mixNow += aCtl * (settings.mix - mixNow);
            const float l = xL[i], r = xR[i];
            const float m = 0.5f * (l + r), s = 0.5f * (l - r);
            const float gM = gainOf (ch[0], settings.mid, m, maxGr[0]);
            const float gS = gainOf (ch[1], settings.side, s, maxGr[1]);
            const float dm = m * (gM - 1.0f), ds = s * (gS - 1.0f);         // the change in MID / SIDE
            const float w = fade * mixNow;
            yL[i] += w * (dm + ds);
            yR[i] += w * (dm - ds);
        }
        if (switching && fade <= 0.0f)                                     // faded out: take the new routing
        {
            routingNow = settings.routing;
            for (auto& c : ch) { c.ms = 0.0; c.grS = 0.0f; c.hp.reset(); }
        }
        if (! isActive()) { for (auto& c : ch) { c.ms = 0.0; c.grS = 0.0f; c.hp.reset(); } }
        grMid.store (maxGr[0], std::memory_order_relaxed);
        grSide.store (maxGr[1], std::memory_order_relaxed);
    }

    // gain reduction now (dB, >= 0) of MID and SIDE, for the meters
    std::atomic<float> grMid { 0.0f }, grSide { 0.0f };

private:
    struct HP2     // 2nd-order Butterworth high-pass (RBJ), side-chain only
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void highPass (double hz, double fs)
        {
            const double w = 2.0 * 3.14159265358979323846 * hz / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071067811865476);
            const double a0 = 1.0 + al;
            b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
        }
        float run (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0.0; }
    };
    struct Chan
    {
        HP2 hp;
        double ms = 0.0;                        // detector mean square
        float grS = 0.0f;                       // smoothed gain reduction, dB
        float aAtt = 0.0f, aRel = 0.0f;
        float thr = -24.0f, ratio = 2.0f, knee = 6.0f, makeup = 0.0f;   // smoothed controls
    };

    float coef (double seconds) const { return (float) (1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-5) * fs))); }

    float gainOf (Chan& c, const Band& b, float x, float& maxGr)
    {
        c.thr += aCtl * (b.thresholdDb - c.thr);
        c.ratio += aCtl * (b.ratio - c.ratio);
        c.knee += aCtl * (b.kneeDb - c.knee);
        c.makeup += aCtl * (b.makeupDb - c.makeup);
        const float d = settings.hpfOn ? c.hp.run (x) : x;
        c.ms += aDet * ((double) d * d - c.ms);
        if (! (c.ms >= 0.0) || ! std::isfinite (c.ms)) { c.ms = 0.0; c.hp.reset(); }      // never stuck after a NaN
        if (c.ms < 1.0e-14) c.ms = 0.0;
        // level of a sine read as its peak (mean square + 3 dB): the threshold means what the meters show
        const float lvl = c.ms > 0.0 ? (float) (10.0 * std::log10 (2.0 * c.ms)) : -140.0f;
        // soft knee
        const float over = lvl - c.thr, w = c.knee, slope = 1.0f - 1.0f / std::max (1.0f, c.ratio);
        float gr;
        if (2.0f * over <= -w) gr = 0.0f;
        else if (w > 0.0f && 2.0f * std::abs (over) < w) { const float t = over + 0.5f * w; gr = slope * t * t / (2.0f * w); }
        else gr = slope * over;
        gr = std::max (0.0f, gr);
        c.grS += (gr > c.grS ? c.aAtt : c.aRel) * (gr - c.grS);
        if (c.grS < 1.0e-6f) c.grS = 0.0f;
        maxGr = std::max (maxGr, c.grS);
        return std::pow (10.0f, (c.makeup - c.grS) * 0.05f);
    }

    Settings settings;
    Chan ch[2];
    double fs = 48000.0;
    float aDet = 0.0f, aCtl = 0.0f, fadeStep = 0.0f, fade = 0.0f, mixNow = 1.0f;
    int routingNow = 0;
};
} // namespace msc
