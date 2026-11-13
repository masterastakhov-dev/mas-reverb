// MASTER SPACE 1.7d DYNAMIC TEST: user-confirmed dynamic cut bands (Dynamic Cut).
// Separate from RESONANCE GUARD and opt-in per band; when no band is armed the audio is not touched (bit-exact).
// Audio thread: no allocation, no locks, no UI work.
//
// Each armed band = a Bell of the band's frequency and Q whose gain goes from 0 dB down to -max dB:
//   detector: RBJ constant-peak band-pass at the band's frequency / Q on the bus as the analyzer shows it
//             (the bus gain applied), stereo-linked (L+R power), 60 ms RMS;
//   gain law: reduction = 1.5 x (level - threshold), clamped to 0 .. max;
//   ballistics: attack 20 ms / release 300 ms on the reduction, then the REVERB EQ bank's own 25 ms
//               coefficient glide (per-sample interpolated: no zipper, no clicks).
// The WET bank runs linked: one detector on early + late together, the same reduction applied to both buses,
// so a resonance shared by both is measured at its real level and cut identically.
#pragma once
#include "ReverbEQ.h"
#include <array>
#include <algorithm>
#include <cmath>

#ifdef MAS_STUDIO_DYN
namespace req
{
class ManualDynamicBank
{
public:
    static constexpr float kAttackS = 0.020f, kReleaseS = 0.300f, kRmsS = 0.060f, kSlope = 1.5f;

    void prepare (double sampleRate)
    {
        fs = std::max (8000.0, sampleRate);
        for (auto& e : eq) e.prepare (fs);
        reset();
    }

    void reset()
    {
        for (auto& e : eq) e.reset();
        for (auto& s : state) s = State {};   // the engine re-applies the settings (applyEq (true)) right after
    }

    void set (const BandParams* bands, bool on, bool force = false)
    {
        hasWanted = false;
        for (int k = 0; k < kBands; ++k)
        {
            auto& s = state[(size_t) k];
            const auto& b = bands[k];
            if (on && b.used && ! b.bypass && b.dynamic && b.type == Bell)
            {
                const bool fresh = ! s.active;
                s.band = b;
                s.band.used = true; s.band.bypass = false; s.band.gainDb = 0.0f;
                s.band.dynamicThresholdDb = std::clamp (b.dynamicThresholdDb, -72.0f, -6.0f);
                s.band.dynamicMaxDb = std::clamp (b.dynamicMaxDb, 0.0f, 6.0f);
                const float f = (float) std::clamp ((double) b.freq, 20.0, std::min (20000.0, fs * 0.45));
                const float q = std::clamp (b.q, 0.4f, 12.0f);
                if (fresh) { s.zL1 = s.zL2 = s.zR1 = s.zR2 = s.power = 0.0; s.reductionDb = 0.0f; }
                if (fresh || s.f != f || s.q != q) { s.f = f; s.q = q; design (s); }   // the detector keeps its state
                s.active = true;
                hasWanted = true;
            }
            else
            {
                s.active = false;
                s.reductionDb = 0.0f;
            }
        }
        if (force)
        {
            // state snap (preset / project load): no fades, start from no reduction
            std::array<BandParams, kBands> out {};
            fill (out);
            for (auto& e : eq) e.set (out.data(), hasWanted, true);
        }
    }

    bool isActive() const { return hasWanted || eq[0].isActive() || eq[1].isActive(); }

    // one bus (EARLY or LATE bank); detGain = the bus gain the analyzer applies (EARLY / LATE level)
    void process (float* l, float* r, int n, float detGain = 1.0f)
    {
        if (! isActive()) return;
        run (l, r, nullptr, nullptr, detGain, 0.0f, n);
    }

    // the WET bank on the early and late buses together: linked detection, the same cut on both
    void processLinked (float* eL, float* eR, float gE, float* lL, float* lR, float gL, int n)
    {
        if (! isActive()) return;
        run (eL, eR, lL, lR, gE, gL, n);
    }

    float reductionDb (int band) const { return state[(size_t) band].active ? state[(size_t) band].reductionDb : 0.0f; }

private:
    struct State
    {
        bool active = false;
        float f = 1000.0f, q = 3.0f, reductionDb = 0.0f;
        BandParams band;
        double b0 = 0, a1 = 0, a2 = 0, zL1 = 0, zL2 = 0, zR1 = 0, zR2 = 0, power = 0;
    };

    void design (State& s) const
    {
        // RBJ band-pass, constant 0 dB peak: b = { alpha, 0, -alpha } / a0
        const double w = 6.283185307179586 * s.f / fs, alpha = std::sin (w) / (2.0 * s.q), a0 = 1.0 + alpha;
        s.b0 = alpha / a0; s.a1 = -2.0 * std::cos (w) / a0; s.a2 = (1.0 - alpha) / a0;
    }

    void fill (std::array<BandParams, kBands>& out) const
    {
        for (int k = 0; k < kBands; ++k)
        {
            const auto& s = state[(size_t) k];
            if (! s.active) continue;
            out[(size_t) k] = s.band;
            out[(size_t) k].gainDb = -s.reductionDb;
        }
    }

    void run (float* aL, float* aR, float* bL, float* bR, float gA, float gB, int n)
    {
        constexpr int hop = 32;
        const double meter = 1.0 - std::exp (-1.0 / (kRmsS * fs));
        for (int pos = 0; pos < n; pos += hop)
        {
            const int count = std::min (hop, n - pos);
            const float aAtt = (float) (1.0 - std::exp (-count / (kAttackS * fs)));
            const float aRel = (float) (1.0 - std::exp (-count / (kReleaseS * fs)));
            for (auto& s : state)
            {
                if (! s.active) continue;
                for (int j = 0; j < count; ++j)
                {
                    double xL = gA * aL[pos + j], xR = gA * aR[pos + j];
                    if (bL != nullptr) { xL += gB * bL[pos + j]; xR += gB * bR[pos + j]; }
                    const double yL = s.b0 * xL + s.zL1;
                    s.zL1 = -s.a1 * yL + s.zL2;
                    s.zL2 = -s.b0 * xL - s.a2 * yL;
                    const double yR = s.b0 * xR + s.zR1;
                    s.zR1 = -s.a1 * yR + s.zR2;
                    s.zR2 = -s.b0 * xR - s.a2 * yR;
                    s.power += meter * (0.5 * (yL * yL + yR * yR) - s.power);
                }
                if (! std::isfinite (s.power) || ! std::isfinite (s.zL1 + s.zL2 + s.zR1 + s.zR2))
                {   // never let a bad sample (NaN / inf from upstream) freeze the detector or the cut filters
                    s.zL1 = s.zL2 = s.zR1 = s.zR2 = s.power = 0.0;
                    s.reductionDb = 0.0f;
                    poisoned = true;
                }
                if (std::abs (s.zL1) < 1e-30) s.zL1 = 0.0;
                if (std::abs (s.zL2) < 1e-30) s.zL2 = 0.0;
                if (std::abs (s.zR1) < 1e-30) s.zR1 = 0.0;
                if (std::abs (s.zR2) < 1e-30) s.zR2 = 0.0;
                if (s.power < 1e-30) s.power = 0.0;
                const float levelDb = (float) (10.0 * std::log10 (s.power + 1e-20));
                if (! std::isfinite (s.reductionDb)) s.reductionDb = 0.0f;
                const float target = std::clamp (kSlope * (levelDb - s.band.dynamicThresholdDb), 0.0f, s.band.dynamicMaxDb);
                s.reductionDb += (target > s.reductionDb ? aAtt : aRel) * (target - s.reductionDb);
                if (target == 0.0f && s.reductionDb < 1e-4f) s.reductionDb = 0.0f;   // settle exactly at 0 dB
            }
            if (poisoned) { for (auto& e : eq) e.reset(); poisoned = false; }
            std::array<BandParams, kBands> out {};
            fill (out);
            eq[0].set (out.data(), hasWanted);
            eq[0].process (aL + pos, aR + pos, count);
            if (! std::isfinite (aL[pos + count - 1] + aR[pos + count - 1])) poisoned = true;   // filter state hit by bad input
            if (bL != nullptr)
            {
                eq[1].set (out.data(), hasWanted);
                eq[1].process (bL + pos, bR + pos, count);
                if (! std::isfinite (bL[pos + count - 1] + bR[pos + count - 1])) poisoned = true;
            }
        }
    }

    std::array<State, kBands> state {};
    req::Bank eq[2];
    double fs = 48000.0;
    bool hasWanted = false, poisoned = false;
};
} // namespace req
#endif
