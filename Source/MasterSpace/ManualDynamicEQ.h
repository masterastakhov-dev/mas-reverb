// MASTER SPACE STUDIO TEST: user-confirmed dynamic notch bands.
// Separate from RESONANCE GUARD; silent / bit-exact when no bands are armed.
// No memory allocation, locks, or UI work in the audio callback.
#pragma once
#include "ReverbEQ.h"
#include <array>
#include <algorithm>
#include <cmath>

#ifdef MAS_STUDIO
namespace req
{
class ManualDynamicBank
{
public:
    void prepare (double sampleRate)
    {
        fs = std::max (8000.0, sampleRate);
        eq.prepare (fs);
        reset();
    }
    void reset()
    {
        eq.reset();
        for (auto& s : state) s = State {};
        hasWanted = false;
    }
    void set (const BandParams* bands, bool on, bool force = false)
    {
        if (force) reset();
        hasWanted = false;
        for (int k = 0; k < kBands; ++k)
        {
            auto& s = state[(size_t) k];
            const auto& b = bands[k];
            const bool want = on && b.used && ! b.bypass && b.dynamic && b.type == Bell;
            if (want)
            {
                const float f = (float) std::clamp ((double) b.freq, 20.0, std::min (20000.0, fs * 0.45));
                const float q = std::clamp (b.q, 0.4f, 12.0f);
                if (! s.active || std::abs (s.f - f) > 0.01f || std::abs (s.q - q) > 0.001f)
                {
                    // RBJ constant-peak bandpass. Feedforward b0, 0, -b0.
                    const double w = 6.283185307179586 * f / fs;
                    const double alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
                    s.b0 = alpha / a0;
                    s.a1 = -2.0 * std::cos (w) / a0;
                    s.a2 = (1.0 - alpha) / a0;
                    s.zl1 = s.zl2 = s.zr1 = s.zr2 = 0.0;
                    s.power = 0.0;
                    s.f = f; s.q = q;
                }
                s.active = true;
                s.band = b; s.band.used = true; s.band.bypass = false; s.band.gainDb = 0;
                hasWanted = true;
            }
            else
            {
                s.active = false;
                s.band = BandParams {};
                s.reductionDb = 0.0f;
            }
        }
    }
    void process (float* left, float* right, int n)
    {
        if (! hasWanted && ! eq.isActive()) return; // completely transparent when unarmed
        constexpr int hop = 32;
        const double meter = 1.0 - std::exp (-1.0 / (0.060 * fs));
        for (int pos = 0; pos < n; pos += hop)
        {
            const int count = std::min (hop, n - pos);
            const float aAtt = (float) (1.0 - std::exp (-count / (0.020 * fs)));
            const float aRel = (float) (1.0 - std::exp (-count / (0.300 * fs)));
            std::array<BandParams, kBands> output {};
            for (int k = 0; k < kBands; ++k)
            {
                auto& s = state[(size_t) k];
                if (! s.active) continue;
                for (int j = 0; j < count; ++j)
                {
                    const double xL = left[pos + j], xR = right[pos + j];
                    const double yL = s.b0 * xL + s.zl1;
                    s.zl1 = -s.a1 * yL + s.zl2;
                    s.zl2 = -s.b0 * xL - s.a2 * yL;
                    const double yR = s.b0 * xR + s.zr1;
                    s.zr1 = -s.a1 * yR + s.zr2;
                    s.zr2 = -s.b0 * xR - s.a2 * yR;
                    s.power += meter * (0.5 * (yL * yL + yR * yR) - s.power);
                }
                const float levelDb = (float) (10.0 * std::log10 (s.power + 1e-20));
                const float target = std::clamp (1.5f * (levelDb - s.band.dynamicThresholdDb),
                                                 0.0f, s.band.dynamicMaxDb);
                s.reductionDb += (target > s.reductionDb ? aAtt : aRel) * (target - s.reductionDb);
                output[(size_t) k] = s.band;
                output[(size_t) k].gainDb = -s.reductionDb;
            }
            eq.set (output.data(), hasWanted);
            eq.process (left + pos, right + pos, count);
        }
    }
    bool isActive() const { return hasWanted || eq.isActive(); }
private:
    struct State
    {
        bool active = false;
        float f = 0, q = 0, reductionDb = 0;
        BandParams band;
        double b0 = 0, a1 = 0, a2 = 0, zl1 = 0, zl2 = 0, zr1 = 0, zr2 = 0, power = 0;
    };
    std::array<State, kBands> state {};
    req::Bank eq;
    double fs = 48000.0;
    bool hasWanted = false;
};
} // namespace req
#endif
