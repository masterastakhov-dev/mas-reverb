#pragma once
// MASTER SPACE: static stereo-linked LEVEL MATCH core.
// No host, JUCE, allocation, locks, audio detector or latency. Not yet wired to the plugin.
// Calibration must be produced outside the realtime callback and published as a coherent snapshot.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mas::alm
{
struct Calibration
{
    double inputEnergy = 0.0;
    // K-weighted reference inner products for DIRECT, EARLY, LATE, after static path gains.
    // Packed symmetric Gram matrix: dd, ee, ll, de, dl, el.
    std::array<double, 6> q {};
};

struct Estimate
{
    float db = 0.0f;
    bool valid = false;
    bool limited = false;
};

// All inputs here are linear amplitude weights, not dB. Late scaling is an approximation
// if DECAY is changed; the caller must invalidate/recalibrate when the model is not applicable.
inline Estimate estimate (const Calibration& c, double dry, double early, double late) noexcept
{
    if (!std::isfinite (c.inputEnergy) || c.inputEnergy <= 0.0) return {};
    if (!std::isfinite (dry) || !std::isfinite (early) || !std::isfinite (late)
        || dry < 0.0 || early < 0.0 || late < 0.0) return {};
    // Normalise to input energy to avoid an absolute signal-level-dependent floor.
    std::array<double, 6> q {};
    for (unsigned i = 0; i < q.size(); ++i)
    {
        q[i] = c.q[i] / c.inputEnergy;
        if (!std::isfinite (q[i])) return {};
    }
    if (q[0] < 0.0 || q[1] < 0.0 || q[2] < 0.0) return {};
    // Reject incoherent or corrupt calibration snapshots. Check pairwise bounds and
    // the determinant of the normalized correlation matrix (positive semidefinite).
    const auto correlation = [] (double cross, double a, double b) noexcept
    {
        if (a == 0.0 || b == 0.0)
            return cross == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
        return (cross / std::sqrt (a)) / std::sqrt (b);
    };
    const double x = correlation (q[3], q[0], q[1]);
    const double y = correlation (q[4], q[0], q[2]);
    const double z = correlation (q[5], q[1], q[2]);
    constexpr double tol = 1.0e-8;
    if (!std::isfinite (x) || !std::isfinite (y) || !std::isfinite (z)
        || std::abs (x) > 1.0 + tol || std::abs (y) > 1.0 + tol || std::abs (z) > 1.0 + tol
        || 1.0 + 2.0*x*y*z - x*x - y*y - z*z < -tol) return {};

    const double e = dry*dry*q[0] + early*early*q[1] + late*late*q[2]
        + 2.0*(dry*early*q[3] + dry*late*q[4] + early*late*q[5]);
    // Silence, cancellation or invalid data must never request maximum boost.
    if (!std::isfinite (e) || e <= 1.0e-12) return {};
    const double db = -10.0 * std::log10 (e);
    return { static_cast<float> (std::clamp (db, -6.0, 12.0)), true, db < -6.0 || db > 12.0 };
}

class StereoGain
{
public:
    // Audio-thread-owned object. prepare/reset only when processing is stopped.
    void prepare (double sampleRate) noexcept
    {
        fs = std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0
             ? sampleRate : 48000.0;
        reset();
    }
    void reset() noexcept { current = target = 1.0f; step = 0.0; remaining = 0; }

    // Call once per parameter update on the audio thread with a coherent calibration.
    // Unknown/invalid calibration fades to unity; never estimates from program material.
    void setTarget (Estimate e, bool enabled, bool insert, double seconds = 0.05) noexcept
    {
        // AUX is a hard unity path. The host integration must coordinate INSERT/AUX
        // switching with its existing routing crossfade; do not call mid-transition.
        if (!insert) { reset(); return; }
        const float db = enabled && e.valid && std::isfinite (e.db)
            ? std::clamp (e.db, -6.0f, 12.0f) : 0.0f;
        const float g = db == 0.0f ? 1.0f : std::pow (10.0f, db / 20.0f);
        if (g == target) return; // repeated block updates never restart the ramp
        target = g;
        const double duration = std::isfinite (seconds) ? std::clamp (seconds, 0.01, 10.0) : 0.05;
        remaining = static_cast<int> (std::ceil (fs * duration));
        step = (static_cast<double> (target) - current) / remaining;
    }

    // Mono: right == nullptr. L and R must not alias unless they represent a single channel.
    void process (float* left, float* right, int n) noexcept
    {
        if (!left || n <= 0 || (remaining == 0 && current == 1.0f)) return;
        if (right == left) right = nullptr;
        for (int i = 0; i < n; ++i)
        {
            if (remaining > 0)
            {
                current += step;
                if (--remaining == 0) current = target;
            }
            const float g = static_cast<float> (current);
            if (g != 1.0f)
            {
                left[i] *= g;
                if (right) right[i] *= g;
            }
        }
    }
    float gain() const noexcept { return static_cast<float> (current); }

private:
    double fs = 48000.0, current = 1.0, step = 0.0;
    float target = 1.0f;
    int remaining = 0;
};
} // namespace mas::alm
