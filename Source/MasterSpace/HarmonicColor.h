#pragma once
// MASTER SPACE 1.4 — HARMONIC COLOR: harmonic saturation of the reverberant space (own algorithms).
//
// One instance processes one stereo pair in place: the early or the late part of the wet (POST) or the
// signal that feeds the reverb (PRE). Structure, per channel, at the oversampled rate:
//
//   s  -> u = s * gIn -> colour tilt -> CHARACTER -> inverse tilt -> / gIn = sat
//   h  = sat - s                     (only what the nonlinearity adds or takes: harmonics, compression)
//   h  -> 30 Hz high-pass            (no rumble from intermodulation)
//   o  = s + MIX * h
//
// so the linear part of the wet passes untouched and, at low levels (the end of a tail), the module is
// transparent. LEVEL COMP keeps the energy envelope of the pair (slow energy follower): the decay keeps
// its shape, the saturation changes the harmonic structure and density, not the loudness.
// LOW-MID GUARD: the energy of the 200-500 Hz band after the module is held at the band energy of its
// input (slow band followers + a broad bell at 316 Hz, -9..+3 dB): harmonics and intermodulation can
// change what the band contains, never how much of it there is.
// LOW GUARD: intermodulation difference tones would fill the lows (weight the space never had): the band
// below ~150 Hz is held at most at its input energy (a low shelf at 180 Hz, cut only).
// AIR GUARD: what saturation takes from the top (above ~3.5 kHz, compressed with the rest and refilled
// less than the lows) is given back by a high shelf that follows the band energy ratio (boost only,
// up to +4 dB; TAPE keeps part of its softer top as character).
//
// LIVE: 4x oversampling, two polyphase IIR allpass half-bands (elliptic; 8 coefficients ~99 dB, then
//       6 coefficients ~104 dB): no reported latency (the wet gets ~5 samples of phase delay below 10 kHz).
// HQ:   4x oversampling, linear-phase FIR half-bands (127 + 31 taps): latency kLatencyHQ samples,
//       reported; when off, a plain delay of the same length (exact).
//
// Characters (different nonlinear systems, not presets of one curve):
//   SOFT  symmetric algebraic sigmoid: odd harmonics falling fast, clean even when pushed.
//   TUBE  biased triode-like curve, the bias shifts with the signal (grid-current style): even
//         harmonics at moderate levels, more odd as it is driven harder; harmonics slightly darkened.
//   TAPE  record pre-emphasis -> hysteresis on the band below 3 kHz (play operator whose width follows the
//         level, read through a smooth step) -> tanh -> playback de-emphasis: odd harmonics with a
//         level-dependent phase lag, highs saturate first.
// COLOR tilts what drives the nonlinearity (-: lows drive harder, warmer harmonics; +: highs, brighter);
// the inverse tilt after it leaves the linear balance as it was.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace hc
{
constexpr double kPi = 3.14159265358979323846;

// tanh by its continued fraction (7th order, |error| < 2e-7 up to |x| = 4.97, where it reaches 1 - 1e-4)
inline float fastTanh (float x)
{
    if (x > 4.97f) return 0.99990f;
    if (x < -4.97f) return -0.99990f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

// ---------------------------------------------------------------------------------- oversampling
// 2x polyphase IIR half-band: H(z) = 0.5 [A0(z^2) + z^-1 A1(z^2)], A = cascades of first-order allpasses.
template <int NC>
struct IirHalfband2x
{
    struct AP { float a = 0, x1 = 0, y1 = 0; float run (float x) { const float y = a * (x - y1) + x1; x1 = x; y1 = y; return y; } };
    AP up0[NC / 2], up1[NC / 2], dn0[NC / 2], dn1[NC / 2];
    float prevOdd = 0.0f;

    void setCoefs (const float* co)
    {
        for (int i = 0; i < NC / 2; ++i)
        {
            up0[i].a = dn0[i].a = co[2 * i];
            up1[i].a = dn1[i].a = co[2 * i + 1];
        }
    }
    void reset()
    {
        for (int i = 0; i < NC / 2; ++i) { up0[i].x1 = up0[i].y1 = up1[i].x1 = up1[i].y1 = 0; dn0[i].x1 = dn0[i].y1 = dn1[i].x1 = dn1[i].y1 = 0; }
        prevOdd = 0.0f;
    }
    void up (float x, float& o0, float& o1)
    {
        float a = x, b = x;
        for (auto& s : up0) a = s.run (a);
        for (auto& s : up1) b = s.run (b);
        o0 = a; o1 = b;
    }
    float down (float s0, float s1)
    {
        float a = s0, b = prevOdd;
        prevOdd = s1;
        for (auto& s : dn0) a = s.run (a);
        for (auto& s : dn1) b = s.run (b);
        return 0.5f * (a + b);
    }
};

// Linear-phase FIR half-band (odd length N = 4K - 1, centre tap 0.5). G = the 2K taps at even positions.
template <int G>
struct FirHalfband2x
{
    float g[G] {};                     // even-position taps h[0], h[2], ... (symmetric)
    static constexpr int kCentreDelay = G / 2 - 1;   // (N - 1) / 2 = 2G - 1 at the high rate -> odd output = x[n - (G/2 - 1)]
    float upHist[2 * G] {}; int upPos = 0;
    float dnEven[2 * G] {}; int dnPos = 0;
    float oddDelay[G / 2 + 1] {}; int oddPos = 0;
    float upOdd[G / 2] {}; int upOddPos = 0;

    void setTaps (const float* taps)
    {
        double sum = 0;
        for (int i = 0; i < G; ++i) sum += taps[i];
        for (int i = 0; i < G; ++i) g[i] = (float) (taps[i] * 0.5 / sum);   // each phase sums to exactly 0.5
    }
    void reset()
    {
        std::memset (upHist, 0, sizeof (upHist)); std::memset (dnEven, 0, sizeof (dnEven));
        std::memset (oddDelay, 0, sizeof (oddDelay)); std::memset (upOdd, 0, sizeof (upOdd));
        upPos = dnPos = oddPos = upOddPos = 0;
    }
    // y[2n] = 2 sum_k g[k] x[n-k];  y[2n+1] = x[n - (G/2 - 1)]
    void up (float x, float& o0, float& o1)
    {
        upPos = (upPos == 0 ? G : upPos) - 1;
        upHist[upPos] = upHist[upPos + G] = x;
        const float* hp = upHist + upPos;
        float acc = 0.0f;
        for (int k = 0; k < G; ++k) acc += g[k] * hp[k];
        o0 = 2.0f * acc;
        // odd phase: plain delay of G/2 - 1 samples
        constexpr int d = G / 2 - 1;
        o1 = upOdd[upOddPos];
        upOdd[upOddPos] = x;
        upOddPos = (upOddPos + 1) % d;
    }
    // y[n] = sum_k g[k] s[2n - 2k] + 0.5 s[2(n - G/2) + 1]
    float down (float s0, float s1)
    {
        dnPos = (dnPos == 0 ? G : dnPos) - 1;
        dnEven[dnPos] = dnEven[dnPos + G] = s0;
        const float* hp = dnEven + dnPos;
        float acc = 0.0f;
        for (int k = 0; k < G; ++k) acc += g[k] * hp[k];
        constexpr int d = G / 2;
        const float odd = oddDelay[oddPos];
        oddDelay[oddPos] = s1;
        oddPos = (oddPos + 1) % d;
        return acc + 0.5f * odd;
    }
};

// The residue's 30 Hz high-pass runs at 4x the rate: its poles sit within 1e-3 of z = 1, where float
// coefficients and state would turn the DC of the even harmonics into low-frequency noise. Double here.
struct BiquadD
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run (float xf) { const double x = xf, y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
    void highPass (double f, double fs, double q)
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void highPass (double f, double fs, double q)
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void peak (double f, double fs, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al / A;
        b0 = (float) ((1 + al * A) / a0); b1 = (float) (-2 * c / a0); b2 = (float) ((1 - al * A) / a0);
        a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al / A) / a0);
    }
    void lowPass2 (double f, double fs, double q)
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void bandPass (double f, double fs, double q)    // constant 0 dB peak gain
    {
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0); a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void lowShelf (double f, double fs, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), sn = std::sin (w);
        const double al = sn / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) + (A - 1) * c + sq;
        b0 = (float) (A * ((A + 1) - (A - 1) * c + sq) / a0); b1 = (float) (2 * A * ((A - 1) - (A + 1) * c) / a0);
        b2 = (float) (A * ((A + 1) - (A - 1) * c - sq) / a0); a1 = (float) (-2 * ((A - 1) + (A + 1) * c) / a0);
        a2 = (float) (((A + 1) + (A - 1) * c - sq) / a0);
    }
    void highShelf (double f, double fs, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), sn = std::sin (w);
        const double al = sn / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sq;
        b0 = (float) (A * ((A + 1) + (A - 1) * c + sq) / a0); b1 = (float) (-2 * A * ((A - 1) + (A + 1) * c) / a0);
        b2 = (float) (A * ((A + 1) + (A - 1) * c - sq) / a0); a1 = (float) (2 * ((A - 1) - (A + 1) * c) / a0);
        a2 = (float) (((A + 1) - (A - 1) * c - sq) / a0);
    }
    void lowPass1 (double f, double fs)    // first order, as a biquad
    {
        const double K = std::tan (kPi * f / fs);
        b0 = b1 = (float) (K / (1 + K)); b2 = 0; a1 = (float) ((K - 1) / (K + 1)); a2 = 0;
    }
};

// First-order filter with an exact inverse: tilt / emphasis around a pivot.
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    float run (float x) { const float y = b0 * x + z; z = b1 * x - a1 * y; return y; }
    void reset() { z = 0; }
    // H(s) = k (s + wz) / (s + wp): gain 1/sqrt(A) at DC, sqrt(A) at the top, 1 at the pivot (tilt);
    // inverse = the same with A -> 1/A. highOnly: 1 at DC, A at the top (emphasis).
    void set (double pivotHz, double fs, double A, bool highOnly)
    {
        const double K = 2 * fs, w0 = 2 * fs * std::tan (kPi * std::min (pivotHz, 0.45 * fs) / fs);
        double wz, wp, k;
        if (highOnly) { wz = w0; wp = w0 * A; k = A; }                  // H(0) = k wz / wp = 1, H(inf) = A
        else          { const double r = std::sqrt (A); wz = w0 / r; wp = w0 * r; k = r; }
        b0 = (float) (k * (K + wz) / (K + wp)); b1 = (float) (k * (wz - K) / (K + wp)); a1 = (float) ((wp - K) / (K + wp));
    }
    // exact inverse of a minimum-phase first-order filter: (1 + a1 z^-1) / (b0 + b1 z^-1)
    void setInverseOf (const Shelf1& f)
    {
        b0 = 1.0f / f.b0; b1 = f.a1 / f.b0; a1 = f.b1 / f.b0;
    }
};

// ---------------------------------------------------------------------------------- the module
class HarmonicColor
{
public:
    enum Character { Soft = 0, Tube = 1, Tape = 2 };
    static constexpr int kLatencyHQ = 71;     // base-rate samples: 63 (127-tap stage) + 8 (31-tap stage + 2 high-rate samples)
    static constexpr int kMaxBlock = 64;

    struct Settings
    {
        bool on = false;
        int character = Soft;
        float drive = 0.3f;      // 0..1
        float color = 0.0f;      // -1..1
        float mix = 1.0f;        // 0..1
        bool levelComp = true;
        int quality = 0;         // 0 LIVE, 1 HQ
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static constexpr float t1[64] = { -1.794405667407e-06f, 6.095388231092e-06f, -1.421416703215e-05f, 2.807181546926e-05f, -5.016796492737e-05f, 8.366783833021e-05f, -1.324873507047e-04f, 2.013746147663e-04f, -2.959870987979e-04f, 4.229651184503e-04f, -5.900044420159e-04f, 8.059337487325e-04f, -1.080806805712e-03f, 1.426025008887e-03f, -1.854514175519e-03f, 2.380991554201e-03f, -3.022377320533e-03f, 3.798433601837e-03f, -4.732761093937e-03f, 5.854363275163e-03f, -7.200129876171e-03f, 8.818853646812e-03f, -1.077790498992e-02f, 1.317474006252e-02f, -1.615773064678e-02f, 1.996632006590e-02f, -2.501505340731e-02f, 3.208966520459e-02f, -4.287925098196e-02f, 6.178543462439e-02f, -1.049676535931e-01f, 3.179297069245e-01f, 3.179297069245e-01f, -1.049676535931e-01f, 6.178543462439e-02f, -4.287925098196e-02f, 3.208966520459e-02f, -2.501505340731e-02f, 1.996632006590e-02f, -1.615773064678e-02f, 1.317474006252e-02f, -1.077790498992e-02f, 8.818853646812e-03f, -7.200129876171e-03f, 5.854363275163e-03f, -4.732761093937e-03f, 3.798433601837e-03f, -3.022377320533e-03f, 2.380991554201e-03f, -1.854514175519e-03f, 1.426025008887e-03f, -1.080806805712e-03f, 8.059337487325e-04f, -5.900044420159e-04f, 4.229651184503e-04f, -2.959870987979e-04f, 2.013746147663e-04f, -1.324873507047e-04f, 8.366783833021e-05f, -5.016796492737e-05f, 2.807181546926e-05f, -1.421416703215e-05f, 6.095388231092e-06f, -1.794405667407e-06f };
        static constexpr float t2[16] = { -4.963152495409e-05f, 6.422753305175e-04f, -2.734532124793e-03f, 8.020590061658e-03f, -1.922827575231e-02f, 4.153808252110e-02f, -9.122783426325e-02f, 3.130559118267e-01f, 3.130559118267e-01f, -9.122783426325e-02f, 4.153808252110e-02f, -1.922827575231e-02f, 8.020590061658e-03f, -2.734532124793e-03f, 6.422753305175e-04f, -4.963152495409e-05f };
        static constexpr float i1[8] = { 0.0406334609f, 0.1505051290f, 0.3007570560f, 0.4607745050f,     // 2x: transition 0.46-0.54, ~99 dB
                                         0.6095243149f, 0.7385038411f, 0.8492238104f, 0.9497427837f };
        static constexpr float i2[6] = { 0.0391515977f, 0.1473771136f, 0.3026468483f, 0.4824685428f,     // 4x: transition 0.30-0.70, ~104 dB
                                         0.6746159185f, 0.8830050258f };
        for (auto& c : ch) { c.fir1.setTaps (t1); c.fir2.setTaps (t2); c.iir1.setCoefs (i1); c.iir2.setCoefs (i2); }
        aEnv = (float) (1.0 - std::exp (-1.0 / (0.3 * fs)));
        aGain = (float) (1.0 - std::exp (-1.0 / (0.05 * fs)));
        aFast = (float) (1.0 - std::exp (-1.0 / (0.03 * fs)));
        fadeStep = (float) (1.0 / (0.02 * fs));
        set (settings, true);
        reset();
    }

    void reset()
    {
        for (auto& c : ch) c.reset();
        pFin = pFinF = 0.0f; trim = 1.0f;
        pIn = pOut = pInF = pOutF = 0.0f; gComp = 1.0f; bandEIn = bandEOut = airEIn = airEOut = lowEIn = lowEOut = 0.0f; guardDb = airDb = lowDb = 0.0f; guardCount = 0;
        for (auto& c : ch) { c.guard.peak (316.0, fs, 0.75, 0.0); c.air.highShelf (std::min (3000.0, 0.35 * fs), fs, 0.0); c.low.lowShelf (180.0, fs, 0.0); }
        fade = settings.on ? 1.0f : 0.0f;
        curMix = settings.mix; curGain = targetGain; curQ = settings.quality;
        meterResidue.store (-120.0f, std::memory_order_relaxed);
        meterComp.store (0.0f, std::memory_order_relaxed);
    }

    void set (const Settings& s, bool force)
    {
        const bool qChanged = s.quality != settings.quality;
        if (! force && ! qChanged && s.color == settings.color && s.drive == settings.drive && s.character == settings.character
            && s.mix == settings.mix && s.on == settings.on && s.levelComp == settings.levelComp)
            return;                                   // called every block: nothing to recompute
        settings = s;
        settings.drive = std::clamp (s.drive, 0.0f, 1.0f);
        settings.color = std::clamp (s.color, -1.0f, 1.0f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
        targetGain = (float) std::pow (10.0, (-12.0 + 36.0 * settings.drive) / 20.0);    // DRIVE 0..100 % = -12..+24 dB
        const double osRate = fs * 4.0;
        const double tilt = std::pow (10.0, 12.0 * settings.color / 20.0);           // COLOR +-1 = +-12 dB tilt (+-6 at the ends)
        for (auto& c : ch)
        {
            c.tiltPre.set (1000.0, osRate, tilt, false);
            c.tiltDe.setInverseOf (c.tiltPre);
            c.recPre.set (3500.0, osRate, 2.0, true);                                 // TAPE record emphasis +6 dB
            c.recDe.setInverseOf (c.recPre);
            c.dc.highPass (30.0, osRate, 0.707);
            c.hyLP.lowPass2 (3000.0, osRate, 0.707);
            c.plate.lowPass1 (9000.0, osRate);
            c.bandIn.bandPass (316.0, fs, 1.05);
            // 6th-order Butterworth at 150 Hz: the low band of a wet is often 50 dB under its low mids, which
            // must not leak into the measurement
            c.lowIn.lowPass2 (150.0, fs, 0.5176); c.lowIn2.lowPass2 (150.0, fs, 0.7071); c.lowIn3.lowPass2 (150.0, fs, 1.9319);
            c.lowOut.lowPass2 (150.0, fs, 0.5176); c.lowOut2.lowPass2 (150.0, fs, 0.7071); c.lowOut3.lowPass2 (150.0, fs, 1.9319);
            c.bandOut.bandPass (316.0, fs, 1.05);
            c.airIn.highPass (std::min (3500.0, 0.4 * fs), fs, 0.707);
            c.airOut.highPass (std::min (3500.0, 0.4 * fs), fs, 0.707);
            c.airIn2.lowPass2 (std::min (15000.0, 0.42 * fs), fs, 0.707);     // below the oversampling filters' edge
            c.airOut2.lowPass2 (std::min (15000.0, 0.42 * fs), fs, 0.707);
        }
        osFs = osRate;
        aTubeAtt = (float) (1.0 - std::exp (-1.0 / (0.002 * osRate)));
        aTubeRel = (float) (1.0 - std::exp (-1.0 / (0.08 * osRate)));
        aTapeMs = (float) (1.0 - std::exp (-1.0 / (0.003 * osRate)));
        aSmooth = (float) (1.0 - std::exp (-1.0 / (0.01 * osRate)));
        if (force || qChanged) { curQ = settings.quality; for (auto& c : ch) c.reset(); }
        if (force) { curMix = settings.mix; curGain = targetGain; fade = settings.on ? 1.0f : 0.0f; }
    }

    static int latencyHQ() { return kLatencyHQ; }
    int latencySamples() const { return curQ == 1 ? kLatencyHQ : 0; }
    bool isActive() const { return settings.on || fade > 0.0f; }

    // In place on a stereo pair. level = the gain the pair is heard at (drive is relative to it).
    void process (float* L, float* R, int n, float level)
    {
        float* io[2] = { L, R };
        const bool hq = curQ == 1;
        if (! settings.on && fade <= 0.0f)
        {
            if (hq) for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) io[c][i] = ch[c].refDelay (io[c][i]);   // exact delay
            else if (wasRunning)
            {
                for (auto& c : ch) { c.reset(); c.guard.peak (316.0, fs, 0.75, 0.0); c.air.highShelf (std::min (3000.0, 0.35 * fs), fs, 0.0); c.low.lowShelf (180.0, fs, 0.0); }
                pIn = pOut = pInF = pOutF = bandEIn = bandEOut = airEIn = airEOut = lowEIn = lowEOut = guardDb = airDb = lowDb = pFin = pFinF = 0; gComp = trim = 1; wasRunning = false;
            }
            return;
        }
        wasRunning = true;
        level = std::max (level, 1.0e-4f);
        const float invLevel = 1.0f / level;
        for (int pos = 0; pos < n; pos += kMaxBlock)
        {
            const int m = std::min (kMaxBlock, n - pos);
            float ref[2][kMaxBlock], out[2][kMaxBlock];
            float eRes = 0, eIn = 0;
            for (int c = 0; c < 2; ++c)
            {
                Chan& k = ch[c];
                float g = curGain, mx = curMix;
                for (int i = 0; i < m; ++i)
                {
                    const float x = io[c][pos + i];
                    ref[c][i] = hq ? k.refDelay (x) : x;
                    float y;
                    if (hq)
                    {
                        float a0, a1, b[4];
                        k.fir1.up (x, a0, a1);
                        k.fir2.up (a0, b[0], b[1]);
                        k.fir2.up (a1, b[2], b[3]);
                        for (int j = 0; j < 4; ++j)
                        {
                            g += aSmooth * (targetGain - g); mx += aSmooth * (settings.mix - mx);
                            const float o = core (k, b[j], g * level, invLevel / g, mx, eRes, eIn);
                            b[j] = k.hiDelay (o);                // +2 high-rate samples: integer total latency
                        }
                        const float c0 = k.fir2.down (b[0], b[1]);      // in time order (argument order is unspecified)
                        const float c1 = k.fir2.down (b[2], b[3]);
                        y = k.fir1.down (c0, c1);
                    }
                    else
                    {
                        float a0, a1, b[4];
                        k.iir1.up (x + 1.0e-20f, a0, a1);
                        k.iir2.up (a0, b[0], b[1]);
                        k.iir2.up (a1, b[2], b[3]);
                        for (int j = 0; j < 4; ++j)
                        {
                            g += aSmooth * (targetGain - g); mx += aSmooth * (settings.mix - mx);
                            b[j] = core (k, b[j], g * level, invLevel / g, mx, eRes, eIn);
                        }
                        const float c0 = k.iir2.down (b[0], b[1]);
                        const float c1 = k.iir2.down (b[2], b[3]);
                        y = k.iir1.down (c0, c1);
                    }
                    out[c][i] = y;
                }
                if (c == 1) { curGain = g; curMix = mx; }
            }
            // LEVEL COMP: the pair keeps its energy envelope (slow follower on L + R); on/off cross-fade
            for (int i = 0; i < m; ++i)
            {
                const float xi = ref[0][i] * ref[0][i] + ref[1][i] * ref[1][i];
                const float yi = out[0][i] * out[0][i] + out[1][i] * out[1][i];
                pIn += aEnv * (xi - pIn);
                pOut += aEnv * (yi - pOut);
                pInF += aFast * (xi - pInF);
                pOutF += aFast * (yi - pOutF);
                if (pIn < 1.0e-30f) pIn = 0.0f;
                if (pOut < 1.0e-30f) pOut = 0.0f;
                if (pInF < 1.0e-30f) pInF = 0.0f;
                if (pOutF < 1.0e-30f) pOutF = 0.0f;
                float target = 1.0f;
                // the slow ratio (0.3 s) gives a steady gain; the fast one (30 ms) caps it, so the make-up gain of a
                // loud, compressed moment is not carried into the uncompressed tail that follows (no swell, no
                // change of the decay)
                if (settings.levelComp)
                    target = std::clamp (std::sqrt (std::min ((pIn + 1.0e-12f) / (pOut + 1.0e-12f), (pInF + 1.0e-12f) / (pOutF + 1.0e-12f))), 0.5f, 4.0f);
                gComp += aGain * (target - gComp);
                fade = settings.on ? std::min (1.0f, fade + fadeStep) : std::max (0.0f, fade - fadeStep);
                float z[2];
                z[0] = out[0][i] * gComp;
                z[1] = out[1][i] * gComp;
                {   // band balance measured on the mid signal (one set of analysis filters per pair)
                    Chan& a = ch[0];
                    const float mi = ref[0][i] + ref[1][i], mo = z[0] + z[1];
                    const float bi = a.bandIn.run (mi), bo = a.bandOut.run (mo);
                    bandEIn += aEnv * (bi * bi - bandEIn);
                    bandEOut += aEnv * (bo * bo - bandEOut);
                    const float ai = a.airIn2.run (a.airIn.run (mi)), ao = a.airOut2.run (a.airOut.run (mo));
                    airEIn += aEnv * (ai * ai - airEIn);
                    airEOut += aEnv * (ao * ao - airEOut);
                    const float li = a.lowIn3.run (a.lowIn2.run (a.lowIn.run (mi))), lo = a.lowOut3.run (a.lowOut2.run (a.lowOut.run (mo)));
                    lowEIn += aEnv * (li * li - lowEIn);
                    lowEOut += aEnv * (lo * lo - lowEOut);
                }
                if (lowEIn < 1.0e-30f) lowEIn = 0.0f;
                if (lowEOut < 1.0e-30f) lowEOut = 0.0f;
                if (bandEIn < 1.0e-30f) bandEIn = 0.0f;
                if (bandEOut < 1.0e-30f) bandEOut = 0.0f;
                if (airEIn < 1.0e-30f) airEIn = 0.0f;
                if (airEOut < 1.0e-30f) airEOut = 0.0f;
                if (--guardCount <= 0)      // LOW-MID GUARD: the bell follows the band energy ratio
                {
                    guardCount = 32;
                    // corrections scale with MIX: at MIX 0 nothing is changed, so nothing is corrected. The bands are
                    // measured before the final level trim, which is included here (the band is held where it is heard).
                    const float t2 = trim * trim;
                    const float want = curMix * 10.0f * std::log10 ((bandEIn + 1.0e-14f) / (bandEOut * t2 + 1.0e-14f));
                    guardDb += 0.25f * (std::clamp (want, -9.0f, 3.0f) - guardDb);
                    const float share = (settings.character == Tape ? 0.6f : 1.0f) * curMix;
                    const float wantAir = share * 10.0f * std::log10 ((airEIn + 1.0e-16f) / (airEOut * t2 + 1.0e-16f));
                    airDb += 0.25f * (std::clamp (wantAir, 0.0f, 4.0f) - airDb);
                    // LOW GUARD: intermodulation fills the lows with difference tones (weight the space never had):
                    // the band below ~150 Hz is held at most at its input energy (cut only)
                    const float wantLow = curMix * 10.0f * std::log10 ((lowEIn + 1.0e-14f) / (lowEOut * t2 + 1.0e-14f));
                    lowDb += 0.25f * (std::clamp (wantLow, -12.0f, 0.0f) - lowDb);
                    for (auto& k : ch) { k.guard.peak (316.0, fs, 0.75, guardDb); k.air.highShelf (std::min (3000.0, 0.35 * fs), fs, airDb); k.low.lowShelf (180.0, fs, lowDb); }
                }
                float f[2];
                for (int c = 0; c < 2; ++c) f[c] = ch[c].air.run (ch[c].guard.run (ch[c].low.run (z[c])));
                if (settings.levelComp)     // the guards add or take a little: the final energy is held too
                {
                    const float fi = f[0] * f[0] + f[1] * f[1];
                    pFin += aEnv * (fi - pFin);
                    pFinF += aFast * (fi - pFinF);
                    if (pFin < 1.0e-30f) pFin = 0.0f;
                    if (pFinF < 1.0e-30f) pFinF = 0.0f;
                    const float want = std::min ((pIn + 1.0e-12f) / (pFin + 1.0e-12f), (pInF + 1.0e-12f) / (pFinF + 1.0e-12f));
                    trim += aGain * (std::clamp (std::sqrt (want), 0.7f, 1.4f) - trim);
                }
                else trim += aGain * (1.0f - trim);
                for (int c = 0; c < 2; ++c)
                    io[c][pos + i] = ref[c][i] + fade * (f[c] * trim - ref[c][i]);
            }
            resAcc += eRes; inAcc += eIn;
        }
        if (++meterCount >= 32)
        {
            meterResidue.store (inAcc > 1.0e-20f ? 10.0f * std::log10 ((resAcc + 1.0e-30f) / inAcc) : -120.0f, std::memory_order_relaxed);
            meterComp.store (20.0f * std::log10 (gComp), std::memory_order_relaxed);
            meterGuard.store (guardDb, std::memory_order_relaxed);
            resAcc = inAcc = 0; meterCount = 0;
        }
    }

    // display values: added harmonics relative to the input (dB), LEVEL COMP gain (dB)
    std::atomic<float> meterResidue { -120.0f }, meterComp { 0.0f }, meterGuard { 0.0f };
    const Settings& getSettings() const { return settings; }

private:
    struct Chan
    {
        IirHalfband2x<8> iir1;
        IirHalfband2x<6> iir2;
        FirHalfband2x<64> fir1;
        FirHalfband2x<16> fir2;
        Shelf1 tiltPre, tiltDe, recPre, recDe;
        BiquadD dc;
        Biquad hyLP;
        Biquad plate, bandIn, bandOut, guard, airIn, airOut, airIn2, airOut2, air, lowIn, lowOut, lowIn2, lowOut2, lowIn3, lowOut3, low;
        float env = 0, env2 = 0, play = 0, bias = 0.45f, tb = 0.42189901f, invD = 1.2167f;
        int biasCount = 0;
        float ref[kLatencyHQ + 1] {}; int refPos = 0;
        float hi[2] {}; int hiPos = 0;
        float refDelay (float x) { const float y = ref[refPos]; ref[refPos] = x; refPos = (refPos + 1) % kLatencyHQ; return y; }
        float hiDelay (float x) { const float y = hi[hiPos]; hi[hiPos] = x; hiPos ^= 1; return y; }
        void reset()
        {
            iir1.reset(); iir2.reset(); fir1.reset(); fir2.reset();
            tiltPre.reset(); tiltDe.reset(); recPre.reset(); recDe.reset(); dc.reset(); plate.reset(); hyLP.reset();
            bandIn.reset(); bandOut.reset(); guard.reset(); airIn.reset(); airOut.reset(); airIn2.reset(); airOut2.reset(); air.reset(); lowIn.reset(); lowOut.reset(); lowIn2.reset(); lowOut2.reset(); lowIn3.reset(); lowOut3.reset(); low.reset();
            env = env2 = play = 0; biasCount = 0; std::memset (ref, 0, sizeof (ref)); refPos = 0; hi[0] = hi[1] = 0; hiPos = 0;
        }
    };

    // one oversampled sample: s -> s + mix * shaped residue
    float core (Chan& k, float s, float gIn, float gOut, float mix, float& eRes, float& eIn)
    {
        float v = k.tiltPre.run (s * gIn);
        float w;
        switch (settings.character)
        {
            case Tube:
            {
                const float a = std::abs (v);
                k.env += (a > k.env ? aTubeAtt : aTubeRel) * (a - k.env);
                if (--k.biasCount <= 0)                                          // bias falls as the stage is driven
                {
                    k.biasCount = 8;
                    const float b = 0.45f / (1.0f + 1.5f * k.env);
                    k.bias = b; k.tb = fastTanh (b); k.invD = 1.0f / (1.0f - k.tb * k.tb);
                }
                w = (fastTanh (v + k.bias) - k.tb) * k.invD;
                break;
            }
            case Tape:
            {
                v = k.recPre.run (v);
                // The loop is driven by the band below 3 kHz (where tape's phase lag and "glue" are heard): its sharp
                // closures then make harmonics of low notes only, which stay below the 4x fold; the band above
                // saturates through the tanh. Loop width from a smooth level (mean square through two 3 ms one-poles).
                const float vl = k.hyLP.run (v);
                k.env += aTapeMs * (vl * vl - k.env);
                k.env2 += aTapeMs * (k.env - k.env2);
                const float a = std::sqrt (2.0f * k.env2);
                const float r = 0.35f * a * fastTanh (a);                         // loop opens with the level: none at low levels
                k.play = std::clamp (k.play, vl - r, vl + r);
                // the play operator turns a corner where the loop closes (infinitely many harmonics, which fold
                // back even at 4x); read through a smooth odd step with S'(+-1) = S''(+-1) = 0 it closes in a C2 curve
                const float e = r > 1.0e-9f ? (k.play - vl) / r : 0.0f, e2 = e * e;
                const float dev = r * e * (15.0f - 10.0f * e2 + 3.0f * e2 * e2) * 0.125f;
                w = k.recDe.run (fastTanh (v + 0.35f * dev));
                break;
            }
            default:
                w = v / std::sqrt (1.0f + v * v);
                break;
        }
        const float sat = k.tiltDe.run (w) * gOut;
        float h = sat - s;
        if (settings.character == Tube) h = k.plate.run (h);
        h = k.dc.run (h);
        eRes += h * h; eIn += s * s;
        return s + mix * h;
    }

    Settings settings;
    Chan ch[2];
    double fs = 48000.0, osFs = 96000.0;
    float targetGain = 1.0f, curGain = 1.0f, curMix = 1.0f, gComp = 1.0f, pIn = 0, pOut = 0, pInF = 0, pOutF = 0, fade = 0;
    float aEnv = 0, aGain = 0, aFast = 0, fadeStep = 0, aSmooth = 0, aTubeAtt = 0, aTubeRel = 0, aTapeMs = 0;
    float resAcc = 0, inAcc = 0, bandEIn = 0, bandEOut = 0, guardDb = 0, airEIn = 0, airEOut = 0, airDb = 0, lowEIn = 0, lowEOut = 0, lowDb = 0, pFin = 0, pFinF = 0, trim = 1;
    int guardCount = 0;
    int curQ = 0, meterCount = 0;
    bool wasRunning = false;
};
} // namespace hc
