// MASTER SPACE 1.7d - SIGNAL RAILS: INPUT / OUTPUT level meters.
//
// Audio side (IOMeters): measured in the audio thread with no allocation, no locks and no GUI access.
//  - RMS: mean square of each channel, one-pole averaging with a 300 ms time constant, updated once per block
//    (exact for any block size: the block mean is folded in with the decay of the whole block). Plain RMS:
//    a sine with its peaks at -18 dBFS reads -21.0 dBFS RMS (no +3 dB "sine" offset).
//  - Sample peak: the largest |x| since the UI last took it (fetch-max on an atomic; the UI exchanges it with 0).
//  Results go to the UI through atomics only. Nothing here writes to the audio buffer.
//
// UI side (RailBallistics): smoothing and animation only - the peak marker jumps up at once, holds 0.8 s, then
// returns smoothly; overload (red) means the held sample peak is at or above -3 dBFS. If the host stops calling the
// plugin (transport stopped, plugin removed from the graph) the readings fall to silence instead of freezing.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace mas
{
struct IOMeters
{
    static constexpr double kRmsSeconds = 0.3;
    static constexpr float kFloorDb = -120.0f;
    enum Io { input = 0, output = 1 };

    // ---- read by the UI
    std::atomic<float> rmsDb[2][2] { { { kFloorDb }, { kFloorDb } }, { { kFloorDb }, { kFloorDb } } };   // [io][L/R]
    std::atomic<float> peakLin[2][2] { { { 0.0f }, { 0.0f } }, { { 0.0f }, { 0.0f } } };                 // max |x| since taken
    std::atomic<std::uint32_t> blocks { 0 };     // advances with every measured block (lets the UI see a stopped host)
    // ---- written by the UI: the audio thread measures only while a display shows the meters
    std::atomic<bool> wanted { false };

    // ---- audio thread
    // Call once at the start of a block; returns whether to measure it. A meter that was off restarts from silence.
    bool beginBlock() noexcept
    {
        const bool on = wanted.load (std::memory_order_relaxed);
        if (on && ! running)
            for (auto& io : ms) io[0] = io[1] = 0.0;
        running = on;
        return on;
    }

    // l / r: the channels of one side; for a mono signal pass the same pointer twice
    void measure (Io io, const float* l, const float* r, int n, double sampleRate) noexcept
    {
        if (n <= 0 || l == nullptr) return;
        if (r == nullptr) r = l;
        const double k = 1.0 - std::exp (-(double) n / (kRmsSeconds * (sampleRate > 0.0 ? sampleRate : 48000.0)));
        const float* ch[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const float* x = ch[c];
            if (c == 1 && r == l)
            {
                ms[io][1] = ms[io][0];
                rmsDb[io][1].store (rmsDb[io][0].load (std::memory_order_relaxed), std::memory_order_relaxed);
                fetchMax (peakLin[io][1], lastPeak);
                continue;
            }
            double sum = 0.0;
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float v = x[i];
                sum += (double) v * (double) v;
                const float a = std::abs (v);
                pk = a > pk ? a : pk;            // NaN never wins this comparison
            }
            double mean = sum / (double) n;
            if (! std::isfinite (mean)) { mean = 1.0; pk = 1.0f; }     // a broken signal reads as full scale, never poisons the meter
            if (! std::isfinite (pk)) pk = 1.0f;
            double& m = ms[io][c];
            m += k * (mean - m);
            if (m < 1.0e-14) m = 0.0;             // below -140 dBFS: exact silence (no denormals)
            rmsDb[io][c].store (m > 0.0 ? (float) (10.0 * std::log10 (m)) : kFloorDb, std::memory_order_relaxed);
            fetchMax (peakLin[io][c], pk);
            lastPeak = pk;
        }
    }

    void endBlock() noexcept { blocks.fetch_add (1, std::memory_order_relaxed); }

    // ---- UI thread
    float takePeak (Io io, int c) noexcept { return peakLin[io][c].exchange (0.0f, std::memory_order_relaxed); }

    static void fetchMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

private:
    double ms[2][2] {};          // audio thread only
    float lastPeak = 0.0f;
    bool running = false;
};

// One channel of a rail as it is drawn. Pure UI state, advanced by a timer.
struct RailBallistics
{
    static constexpr float kHoldSeconds = 0.8f;
    static constexpr float kFallDbPerSec = 24.0f;      // return speed of the peak marker after the hold
    static constexpr float kEaseSeconds = 0.12f;        // the return starts gently, reaches full speed after this
    static constexpr float kOverDb = -3.0f;             // marker turns red at / above this sample peak
    static constexpr float kShowFloorDb = -70.0f;       // below the -60 dBFS scale: nothing is drawn

    float rmsDb = kShowFloorDb, peakDb = kShowFloorDb;
    float hold = 0.0f, falling = 0.0f;

    // targetRmsDb: the measured RMS; newPeakLin: the largest sample since the last tick (0 = none); dt in seconds
    void tick (float targetRmsDb, float newPeakLin, float dt) noexcept
    {
        if (! (dt > 0.0f)) return;
        if (! std::isfinite (targetRmsDb)) targetRmsDb = kShowFloorDb;
        targetRmsDb = targetRmsDb < kShowFloorDb ? kShowFloorDb : targetRmsDb;
        // the body is already averaged over 300 ms by the meter; this only removes the steps between blocks
        rmsDb += (1.0f - std::exp (-dt / 0.035f)) * (targetRmsDb - rmsDb);
        const float p = newPeakLin > 1.0e-7f ? 20.0f * std::log10 (newPeakLin) : kShowFloorDb;
        if (p >= peakDb)
        {
            peakDb = p; hold = kHoldSeconds; falling = 0.0f;
        }
        else if (hold > 0.0f)
        {
            hold -= dt;
        }
        else
        {
            falling += dt;
            const float ease = falling >= kEaseSeconds ? 1.0f : falling / kEaseSeconds;
            peakDb -= kFallDbPerSec * ease * dt;
            if (peakDb < p) peakDb = p;
        }
        if (peakDb < kShowFloorDb) peakDb = kShowFloorDb;
    }
    bool over() const noexcept { return peakDb >= kOverDb; }
};
} // namespace mas
