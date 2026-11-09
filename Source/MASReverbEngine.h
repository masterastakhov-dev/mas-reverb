// MAS Reverb — reverb engine
// (c) masterastakhovsound
//
// Original implementation of a figure-eight "plate/hall" tank, built on the
// publicly documented topology from J. Dattorro, "Effect Design, Part 1:
// Reverberator and Other Filters", JAES vol. 45 no. 9, 1997 (itself based on
// D. Griesinger's published work). Own additions: true stereo input diffusion,
// RT60-calibrated decay, scalable size, HF damping, low cut, pre-delay and
// dual-LFO modulation with Hermite interpolation.
//
// No JUCE dependency: this file is plain C++ so it can be tested offline.

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace mas
{

static constexpr double kPi = 3.14159265358979323846;

//==============================================================================
class DelayLine
{
public:
    void allocate (int maxDelaySamples)
    {
        int n = 1;
        while (n < maxDelaySamples + 8) n <<= 1;
        buffer.assign ((size_t) n, 0.0f);
        mask = n - 1;
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    // Read the sample written `d` ticks ago (d >= 1). Call before push().
    inline float read (int d) const noexcept { return buffer[(size_t) ((writePos - d) & mask)]; }

    inline float readLinear (float d) const noexcept
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = read (i), b = read (i + 1);
        return a + f * (b - a);
    }

    // 4-point Hermite, for modulated reads
    inline float readHermite (float d) const noexcept
    {
        const int i = (int) d;
        const float t = d - (float) i;
        const float y0 = read (i - 1), y1 = read (i), y2 = read (i + 1), y3 = read (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
// Schroeder all-pass: w = x + g*w[n-D];  y = w[n-D] - g*w
class Allpass
{
public:
    void allocate (int maxD) { line.allocate (maxD); }
    void clear() { line.clear(); }

    inline float process (float x, float delay, float g) noexcept
    {
        const float wd = line.readLinear (delay);
        const float w = x + g * wd;
        line.push (w);
        return wd - g * w;
    }

    inline float processMod (float x, float delay, float g) noexcept
    {
        const float wd = line.readHermite (delay);
        const float w = x + g * wd;
        line.push (w);
        return wd - g * w;
    }

    // tap into the internal line (offset measured like DelayLine::read, before the next push)
    inline float tap (float d) const noexcept { return line.readLinear (d); }

private:
    DelayLine line;
};

//==============================================================================
struct OnePoleLP
{
    float z = 0.0f, a = 1.0f;
    void setCutoff (double hz, double fs) { a = (float) (1.0 - std::exp (-2.0 * kPi * hz / fs)); }
    inline float process (float x) noexcept { z += a * (x - z); return z; }
    void clear() { z = 0.0f; }
};

// RBJ biquad high-pass (Butterworth Q)
struct HighPass
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set (double hz, double fs)
    {
        const double w0 = 2.0 * kPi * hz / fs;
        const double c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 + c) * 0.5) / a0);
        b1 = (float) (-(1.0 + c) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * c) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void clear() { x1 = x2 = y1 = y2 = 0; }
};

//==============================================================================
struct ReverbParams
{
    float preDelayMs = 20.0f;  // 0 .. 250
    float size       = 0.5f;   // 0 .. 1
    float decaySec   = 2.5f;   // 0.2 .. 20 (RT60)
    float hfDamp     = 0.35f;  // 0 (bright) .. 1 (dark)
    float lowCutHz   = 80.0f;  // 20 .. 1000
    float mod        = 0.4f;   // 0 .. 1
    float mix        = 0.3f;   // 0 .. 1
};

//==============================================================================
class ReverbEngine
{
public:
    // reference rate of the published delay lengths
    static constexpr double refRate = 29761.0;
    static constexpr double maxSizeScale = 1.6;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        k = fs / refRate;
        const double maxScale = k * maxSizeScale;

        const int maxExcSamples = (int) std::ceil (2.0 * maxExcursionMs * 0.001 * fs) + 8;
        auto alloc = [&] (auto& obj, double len, int extra = 0) { obj.allocate ((int) std::ceil (len * maxScale) + 64 + extra); };

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                alloc (inDiff[c][i], inDiffLen[c][i]);

        for (int b = 0; b < 2; ++b)
        {
            alloc (modAp[b], modApLen[b], maxExcSamples);   // + modulation headroom
            alloc (delayA[b], delayALen[b]);
            alloc (decayAp[b], decayApLen[b]);
            alloc (delayB[b], delayBLen[b]);
        }

        for (int c = 0; c < 2; ++c)
            preDelay[c].allocate ((int) std::ceil (0.26 * fs) + 8);

        for (int c = 0; c < 2; ++c) inputLP[c].setCutoff (15000.0, fs);

        reset();
        updateCoefficients (true);
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : inDiff[c]) a.clear();
            preDelay[c].clear();
            lowCut[c].clear();
            inputLP[c].clear();
        }
        for (int b = 0; b < 2; ++b)
        {
            modAp[b].clear(); delayA[b].clear(); decayAp[b].clear(); delayB[b].clear();
            damp[b].clear();
        }
        feedback[0] = feedback[1] = 0.0f;
        lfoPhase[0] = 0.0; lfoPhase[1] = 0.25;
    }

    void setParams (const ReverbParams& p) { target = p; }

    void process (float* left, float* right, int numSamples)
    {
        updateCoefficients (false);

        // per-sample smoothing coefficients (~60 ms for size / pre-delay, ~20 ms for mix)
        const float sSlow = (float) (1.0 - std::exp (-1.0 / (0.060 * fs)));
        const float sFast = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));

        const float targetScale = (float) (k * sizeToScale (target.size));
        const float targetPre   = (float) (std::clamp (target.preDelayMs, 0.0f, 250.0f) * 0.001 * fs);
        const float targetMix   = std::clamp (target.mix, 0.0f, 1.0f);
        const float exc         = (float) (target.mod * maxExcursionMs * 0.001 * fs);

        const double lfoInc0 = lfoRate[0] / fs, lfoInc1 = lfoRate[1] / fs;

        for (int n = 0; n < numSamples; ++n)
        {
            curScale += sSlow * (targetScale - curScale);
            curPre   += sSlow * (targetPre - curPre);
            curMix   += sFast * (targetMix - curMix);
            curExc   += sSlow * (exc - curExc);

            const float dryL = left[n], dryR = right[n];

            // --- input conditioning: low cut, bandwidth, pre-delay ------------
            float in[2];
            const float src[2] = { dryL, dryR };
            for (int c = 0; c < 2; ++c)
            {
                float x = lowCut[c].process (src[c]);
                x = inputLP[c].process (x);
                const float pd = std::max (curPre, 1.0f);
                const float y = preDelay[c].readLinear (pd);
                preDelay[c].push (x);
                in[c] = (curPre < 1.0f) ? x : y;
            }

            // --- input diffusion (independent chains for L and R) ------------
            const float dScale = 0.6f + 0.4f * (curScale / (float) k); // diffusers scale gently
            for (int c = 0; c < 2; ++c)
            {
                float x = in[c] * 0.5f;
                for (int i = 0; i < 4; ++i)
                    x = inDiff[c][i].process (x, (float) (inDiffLen[c][i] * k) * dScale, inDiffG[i]);
                in[c] = x;
            }

            // --- LFOs ----------------------------------------------------------
            lfoPhase[0] += lfoInc0; if (lfoPhase[0] >= 1.0) lfoPhase[0] -= 1.0;
            lfoPhase[1] += lfoInc1; if (lfoPhase[1] >= 1.0) lfoPhase[1] -= 1.0;
            const float lfo[2] = { (float) std::sin (2.0 * kPi * lfoPhase[0]),
                                   (float) std::sin (2.0 * kPi * lfoPhase[1]) };

            // --- figure-eight tank --------------------------------------------
            const float fbIn[2] = { feedback[1], feedback[0] }; // cross-coupling
            for (int b = 0; b < 2; ++b)
            {
                float x = in[b] + fbIn[b];

                const float apD = (float) modApLen[b] * curScale + curExc * (1.0f + lfo[b]) + 2.0f;
                x = modAp[b].processMod (x, apD, decayDiff1);

                const float dA = (float) delayALen[b] * curScale;
                const float yA = delayA[b].readLinear (dA);
                delayA[b].push (x);

                float y = damp[b].process (yA) * gainA[b];
                y = decayAp[b].process (y, (float) decayApLen[b] * curScale, decayDiff2);

                const float dB = (float) delayBLen[b] * curScale;
                const float yB = delayB[b].readLinear (dB);
                delayB[b].push (y);

                feedback[b] = yB * gainB[b];
            }

            // --- output taps (decorrelated L / R) -------------------------------
            const float s = curScale;
            float wetL =  delayA[1].readLinear (266.0f * s)
                        + delayA[1].readLinear (2974.0f * s)
                        - decayAp[1].tap (1913.0f * s)
                        + delayB[1].readLinear (1996.0f * s)
                        - delayA[0].readLinear (1990.0f * s)
                        - decayAp[0].tap (187.0f * s)
                        - delayB[0].readLinear (1066.0f * s);

            float wetR =  delayA[0].readLinear (353.0f * s)
                        + delayA[0].readLinear (3627.0f * s)
                        - decayAp[0].tap (1228.0f * s)
                        + delayB[0].readLinear (2673.0f * s)
                        - delayA[1].readLinear (2111.0f * s)
                        - decayAp[1].tap (335.0f * s)
                        - delayB[1].readLinear (121.0f * s);

            wetL *= outGain;
            wetR *= outGain;

            // equal-power-ish dry/wet
            const float wetAmt = std::sin (curMix * 1.5707963f);
            const float dryAmt = std::cos (curMix * 1.5707963f);
            left[n]  = dryL * dryAmt + wetL * wetAmt;
            right[n] = dryR * dryAmt + wetR * wetAmt;
        }
    }

    double getTailSeconds() const { return std::min (30.0, (double) target.decaySec * 1.2 + 0.3); }

private:
    static double sizeToScale (float size01)
    {
        // 0 -> 0.45x (small room), 0.5 -> 1.0x (published size), 1 -> 1.6x (large hall)
        const double s = std::clamp ((double) size01, 0.0, 1.0);
        return s < 0.5 ? 0.45 + (1.0 - 0.45) * (s / 0.5)
                       : 1.0 + (maxSizeScale - 1.0) * ((s - 0.5) / 0.5);
    }

    void updateCoefficients (bool force)
    {
        const ReverbParams& p = target;

        // decay gains from RT60, using the (target) loop segment lengths
        const double rt = std::clamp ((double) p.decaySec, 0.1, 30.0);
        const double scale = k * sizeToScale (p.size);
        for (int b = 0; b < 2; ++b)
        {
            const double tA = (modApLen[b] + delayALen[b]) * scale / fs;
            const double tB = (decayApLen[b] + delayBLen[b]) * scale / fs;
            gainA[b] = (float) std::pow (10.0, -3.0 * tA / rt);
            gainB[b] = (float) std::pow (10.0, -3.0 * tB / rt);
        }

        // HF damping: 0 -> 18 kHz (bright), 1 -> 1.2 kHz (dark), exponential
        const double d = std::clamp ((double) p.hfDamp, 0.0, 1.0);
        const double fc = std::min (18000.0 * std::pow (1200.0 / 18000.0, d), 0.45 * fs);
        for (auto& f : damp) f.setCutoff (fc, fs);

        // low cut (smoothed across blocks to avoid zipper)
        const double lc = std::clamp ((double) p.lowCutHz, 20.0, 1000.0);
        curLowCut = force ? lc : curLowCut + 0.3 * (lc - curLowCut);
        for (auto& h : lowCut) h.set (curLowCut, fs);

        if (force)
        {
            curScale = (float) scale;
            curPre = (float) (std::clamp (p.preDelayMs, 0.0f, 250.0f) * 0.001 * fs);
            curMix = std::clamp (p.mix, 0.0f, 1.0f);
            curExc = (float) (p.mod * maxExcursionMs * 0.001 * fs);
        }
    }

    // ---- topology constants (samples @ 29761 Hz) ----
    static constexpr double inDiffLen[2][4] = { { 142.0, 107.0, 379.0, 277.0 },
                                                { 151.0, 113.0, 397.0, 263.0 } };
    static constexpr float  inDiffG[4]      = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr double modApLen[2]     = { 672.0, 908.0 };
    static constexpr double delayALen[2]    = { 4453.0, 4217.0 };
    static constexpr double decayApLen[2]   = { 1800.0, 2656.0 };
    static constexpr double delayBLen[2]    = { 3720.0, 3163.0 };
    static constexpr float  decayDiff1      = -0.70f;
    static constexpr float  decayDiff2      = 0.50f;
    static constexpr double maxExcursionMs  = 1.1;
    static constexpr double lfoRate[2]      = { 0.83, 0.61 };
    static constexpr float  outGain         = 0.60f;

    double fs = 48000.0, k = 48000.0 / refRate;
    ReverbParams target;

    Allpass   inDiff[2][4];
    DelayLine preDelay[2];
    HighPass  lowCut[2];
    OnePoleLP inputLP[2];

    Allpass   modAp[2], decayAp[2];
    DelayLine delayA[2], delayB[2];
    OnePoleLP damp[2];

    float gainA[2] {}, gainB[2] {};
    float feedback[2] {};
    double lfoPhase[2] {};

    float curScale = 1.0f, curPre = 0.0f, curMix = 0.3f, curExc = 0.0f;
    double curLowCut = 80.0;
};

} // namespace mas
