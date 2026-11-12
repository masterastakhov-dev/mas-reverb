// MASTER SPACE 1.5 EQ LAB — REVERB EQ and the analyzer taps
// (c) masterastakhovsound
//
//   * REVERB EQ: parametric EQ of the finished reverb signal. Three independent banks (WET, EARLY, LATE),
//     eight bands each: Bell, Low Shelf, High Shelf, Low Cut and High Cut (12 / 24 / 36 / 48 dB/oct).
//     Minimum-phase biquads in double precision: zero latency. Frequency, gain and Q are smoothed; a band
//     that is switched, bypassed or changes its type / slope is cross-faded. A bank with nothing to do
//     does not touch the audio at all (bit-exact pass-through).
//   * It is not the DECAY EQ: that one shapes the decay time per band inside the reverb algorithm.
//   * Analyzer: lock-free taps (input, early / late / wet, before and after the EQ) the editor reads and
//     transforms itself; the audio thread only copies samples of the taps the editor asked for.
// Plain C++ (no JUCE) so it can be tested offline.

#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace req
{
enum Type { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, numTypes };
enum BankId { Wet = 0, Early, Late, numBanks };
constexpr int kBands = 8;
constexpr int kMaxStages = 4;
constexpr double kGainMax = 18.0, kQMin = 0.1, kQMax = 18.0;

struct BandParams
{
    bool  used = false;      // the band exists (has a point on the display)
    bool  bypass = false;
    int   type = Bell;
    float freq = 1000.0f;    // Hz, 20 .. 20000 (limited below Nyquist)
    float gainDb = 0.0f;     // -18 .. +18 (Bell and shelves)
    float q = 1.0f;          // 0.1 .. 18
    int   slope = 1;         // cuts: 0 = 12, 1 = 24, 2 = 36, 3 = 48 dB/oct
};

struct Settings
{
    bool on = false;                          // REVERB EQ ON / OFF (all three banks)
    BandParams band[numBanks][kBands];
};

inline bool isCut (int type) { return type == LowCut || type == HighCut; }
inline int slopeDb (int slope) { return 12 * (std::clamp (slope, 0, 3) + 1); }

struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// Designs the sections of one band; returns their number (1 for Bell / shelves, 1..4 for the cuts).
// The display uses the same function, so the curve it draws is the response of the running filters.
inline int design (int type, double freq, double gainDb, double q, int slope, double fs, Coefs* out)
{
    static const double kPi = 3.14159265358979323846;
    const double f = std::clamp (freq, 20.0, std::min (20000.0, 0.49 * fs));
    q = std::clamp (q, kQMin, kQMax);
    const double w = 2.0 * kPi * f / fs, cs = std::cos (w), sn = std::sin (w);
    auto norm = [] (Coefs& c, double b0, double b1, double b2, double a0, double a1, double a2)
    { c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0; };

    if (type == Bell)
    {
        const double A = std::pow (10.0, gainDb / 40.0), al = sn / (2.0 * q);
        norm (out[0], 1.0 + al * A, -2.0 * cs, 1.0 - al * A, 1.0 + al / A, -2.0 * cs, 1.0 - al / A);
        return 1;
    }
    if (type == LowShelf || type == HighShelf)
    {
        const double A = std::pow (10.0, gainDb / 40.0), al = sn / (2.0 * q), k = 2.0 * std::sqrt (A) * al;
        if (type == LowShelf)
            norm (out[0], A * ((A + 1) - (A - 1) * cs + k), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - k),
                  (A + 1) + (A - 1) * cs + k, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - k);
        else
            norm (out[0], A * ((A + 1) + (A - 1) * cs + k), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - k),
                  (A + 1) - (A - 1) * cs + k, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - k);
        return 1;
    }
    // Butterworth cascades; Q scales the resonance of the last section (0.71 = maximally flat)
    static const double butter[4][4] = { { 0.70710678, 0, 0, 0 }, { 0.54119610, 1.30656296, 0, 0 },
                                         { 0.51763809, 0.70710678, 1.93185165, 0 }, { 0.50979558, 0.60134489, 0.89997622, 2.56291545 } };
    const int n = std::clamp (slope, 0, 3) + 1;
    for (int k = 0; k < n; ++k)
    {
        double qs = butter[n - 1][k];
        if (k == n - 1) qs = std::clamp (qs * q / 0.70710678, 0.3, 24.0);
        const double al = sn / (2.0 * qs);
        if (type == LowCut) norm (out[k], (1 + cs) * 0.5, -(1 + cs), (1 + cs) * 0.5, 1 + al, -2 * cs, 1 - al);
        else                norm (out[k], (1 - cs) * 0.5, 1 - cs, (1 - cs) * 0.5, 1 + al, -2 * cs, 1 - al);
    }
    return n;
}

// |H| in dB of a set of sections at a frequency
inline double magnitudeDb (const Coefs* c, int n, double freq, double fs)
{
    const double w = 2.0 * 3.14159265358979323846 * freq / fs;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    double db = 0.0;
    for (int k = 0; k < n; ++k)
    {
        const double nr = c[k].b0 + c[k].b1 * c1 + c[k].b2 * c2, ni = -(c[k].b1 * s1 + c[k].b2 * s2);
        const double dr = 1.0 + c[k].a1 * c1 + c[k].a2 * c2, di = -(c[k].a1 * s1 + c[k].a2 * s2);
        db += 10.0 * std::log10 (std::max (1e-30, (nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di)));
    }
    return db;
}

// response of one band as it is set (0 dB when unused / bypassed)
inline double bandMagnitudeDb (const BandParams& b, double freq, double fs)
{
    if (! b.used || b.bypass) return 0.0;
    Coefs c[kMaxStages];
    const int n = design (b.type, b.freq, b.gainDb, b.q, b.slope, fs, c);
    return magnitudeDb (c, n, freq, fs);
}

//==============================================================================
// One bank on one stereo signal.
class Bank
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        fadeStep = (float) (1.0 / (0.010 * fs));
        smooth = 1.0 - std::exp (-(double) kSub / (0.025 * fs));
        reset();
    }

    void reset()
    {
        for (auto& b : bands) { b.clearState(); b.mix = 0.0f; b.live = false; }
        anyLive = false;
    }

    // force: jump to the settings with no fades (after prepare / a state snap)
    void set (const BandParams* p, bool on, bool force = false)
    {
        bool any = false;
        for (int i = 0; i < kBands; ++i)
        {
            auto& b = bands[i];
            b.target = p[i];
            b.want = on && p[i].used && ! p[i].bypass;
            if (force)
            {
                b.clearState();
                b.live = b.want;
                b.mix = b.want ? 1.0f : 0.0f;
                if (b.want) b.adopt (fs);
            }
            any = any || b.want || b.live;
        }
        anyLive = any;
    }

    bool isActive() const { return anyLive; }

    void process (float* left, float* right, int n)
    {
        if (! anyLive) return;                       // nothing to do: the audio is not touched
        bool any = false;
        for (auto& b : bands)
        {
            if (! b.live && ! b.want) continue;
            if (! b.live) { b.clearState(); b.adopt (fs); b.mix = 0.0f; b.live = true; }
            for (int pos = 0; pos < n; pos += kSub)
            {
                const int m = std::min (kSub, n - pos);
                // a different type or slope: fade the band out, switch, fade in
                const bool structural = b.type != b.target.type || (isCut (b.target.type) && b.slope != b.target.slope);
                float mixTarget = (b.want && ! structural) ? 1.0f : 0.0f;
                if (structural && b.mix <= 0.0f && b.want) { b.clearState(); b.adopt (fs); mixTarget = 1.0f; }
                // parameters glide; the coefficients are interpolated per sample across the sub-block (no zipper)
                const bool gliding = ! structural && b.glide (fs, smooth, 1.0 / m);
                float* L = left + pos; float* R = right + pos;
                for (int i = 0; i < m; ++i)
                {
                    double yl = L[i], yr = R[i];
                    for (int k = 0; k < b.stages; ++k)
                    {
                        Coefs& c = b.c[k];
                        if (gliding)
                        {
                            const Coefs& d = b.dc[k];
                            c.b0 += d.b0; c.b1 += d.b1; c.b2 += d.b2; c.a1 += d.a1; c.a2 += d.a2;
                        }
                        double* z = b.z[k];
                        const double xl = yl, xr = yr;
                        yl = c.b0 * xl + z[0]; z[0] = c.b1 * xl - c.a1 * yl + z[1]; z[1] = c.b2 * xl - c.a2 * yl;
                        yr = c.b0 * xr + z[2]; z[2] = c.b1 * xr - c.a1 * yr + z[3]; z[3] = c.b2 * xr - c.a2 * yr;
                    }
                    if (b.mix != mixTarget)
                        b.mix = mixTarget > b.mix ? std::min (mixTarget, b.mix + fadeStep) : std::max (mixTarget, b.mix - fadeStep);
                    if (b.mix >= 1.0f) { L[i] = (float) yl; R[i] = (float) yr; }
                    else
                    {
                        const float w = b.mix * b.mix * b.mix * (10.0f + b.mix * (6.0f * b.mix - 15.0f));   // smooth S-curve
                        L[i] += w * ((float) yl - L[i]); R[i] += w * ((float) yr - R[i]);
                    }
                }
                if (gliding) for (int k = 0; k < b.stages; ++k) b.c[k] = b.cn[k];
                if (b.mix <= 0.0f && ! b.want) { b.live = false; break; }
            }
            for (int k = 0; k < b.stages; ++k)       // denormals
                for (double& s : b.z[k]) if (std::abs (s) < 1e-30) s = 0.0;
            any = any || b.live || b.want;
        }
        anyLive = any;
    }

private:
    static constexpr int kSub = 16;

    struct BandState
    {
        BandParams target;
        bool want = false, live = false;
        int type = Bell, slope = 1, stages = 1;
        double f = 1000.0, g = 0.0, q = 1.0;
        float mix = 0.0f;
        Coefs c[kMaxStages], cn[kMaxStages], dc[kMaxStages];
        double z[kMaxStages][4] = {};

        void clearState() { std::memset (z, 0, sizeof (z)); }
        void adopt (double fs)
        {
            type = target.type; slope = target.slope;
            f = target.freq; g = target.gainDb; q = target.q;
            stages = design (type, f, g, q, slope, fs, c);
        }
        // one smoothing step towards the target; returns true when the coefficients move (cn = where they
        // arrive at the end of the sub-block, dc = the per-sample step)
        bool glide (double fs, double a, double invLen)
        {
            const double tf = target.freq, tg = target.gainDb, tq = target.q;
            if (f == tf && g == tg && q == tq) return false;
            f = std::abs (std::log (tf / f)) < 1e-4 ? tf : f * std::exp (a * std::log (tf / f));
            g = std::abs (tg - g) < 1e-3 ? tg : g + a * (tg - g);
            q = std::abs (std::log (tq / q)) < 1e-4 ? tq : q * std::exp (a * std::log (tq / q));
            design (type, f, g, q, slope, fs, cn);
            for (int k = 0; k < stages; ++k)
            {
                dc[k].b0 = (cn[k].b0 - c[k].b0) * invLen; dc[k].b1 = (cn[k].b1 - c[k].b1) * invLen; dc[k].b2 = (cn[k].b2 - c[k].b2) * invLen;
                dc[k].a1 = (cn[k].a1 - c[k].a1) * invLen; dc[k].a2 = (cn[k].a2 - c[k].a2) * invLen;
            }
            return true;
        }
    };

    double fs = 48000.0, smooth = 0.1;
    float fadeStep = 0.004f;
    BandState bands[kBands];
    bool anyLive = false;
};

//==============================================================================
// Analyzer taps: the audio thread writes mono samples of up to two requested taps into rings,
// the editor copies the newest block out and runs the FFT itself. No locks, no allocation.
struct Analyzer
{
    enum Tap { Input = 0, EarlyPre, EarlyPost, LatePre, LatePost, WetPre, WetPost, numTaps };
    static constexpr int kSize = 1 << 15, kMask = kSize - 1;

    std::atomic<int> request[2] { { -1 }, { -1 } };   // which tap feeds each ring (-1 = none); set by the editor
    std::atomic<unsigned> writePos[2] { { 0 }, { 0 } };
    std::atomic<double> sampleRate { 48000.0 };
    float ring[2][kSize] = {};

    // audio thread
    int wanted (int slot) const { return request[slot].load (std::memory_order_relaxed); }
    void push (int slot, const float* l, const float* r, int n, float gain = 1.0f)
    {
        unsigned w = writePos[slot].load (std::memory_order_relaxed);
        const float g = 0.5f * gain;
        for (int i = 0; i < n; ++i) ring[slot][(w + (unsigned) i) & kMask] = g * (l[i] + r[i]);
        writePos[slot].store (w + (unsigned) n, std::memory_order_release);
    }
    void pushSum (int slot, const float* al, const float* ar, float ga, const float* bl, const float* br, float gb, int n)
    {
        unsigned w = writePos[slot].load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) ring[slot][(w + (unsigned) i) & kMask] = 0.5f * (ga * (al[i] + ar[i]) + gb * (bl[i] + br[i]));
        writePos[slot].store (w + (unsigned) n, std::memory_order_release);
    }
    // editor: the newest n samples (n < kSize / 2)
    void read (int slot, float* dst, int n) const
    {
        const unsigned w = writePos[slot].load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dst[i] = ring[slot][(w - (unsigned) n + (unsigned) i) & kMask];
    }
};

} // namespace req
