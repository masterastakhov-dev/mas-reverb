#pragma once
// MASTER SPACE 1.8 - M/S HARMONIC COLOR: one place of HARMONIC COLOR (PRE, POST early or POST late) with two modes.
//
//   STEREO  the existing HarmonicColor on L/R, untouched (bit-identical to 1.4 ... 1.7d).
//   M/S     M = (L + R) / 2, S = (L - R) / 2 -> two independent mono HarmonicColor processors (MID, SIDE),
//           each with its own DRIVE, MIX, LEVEL COMP envelope and guards -> L = M + S, R = M - S.
//           CHARACTER, COLOR, LEVEL COMP on/off, LIVE / HQ are shared; the MIX of each is the main SATURATION
//           MIX times its own MID / SIDE MIX. Same oversampling, same latency as STEREO.
//
// The M/S path is used only while HARMONIC COLOR is on (or fading out) in M/S mode: with the module off the
// place is the STEREO instance again (an exact bypass / HQ delay), so OFF is bit-identical in either mode.
// A change of path is a 30 ms linear cross-fade of the two outputs (they are time-aligned and correlated); the
// path that comes in starts from a clean state. Nothing here allocates; process() takes any block length.
#include "HarmonicColor.h"

namespace hc
{
class ColorSlot
{
public:
    struct MS { float midDrive = 0.3f, sideDrive = 0.3f, midMix = 1.0f, sideMix = 1.0f; };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        st.prepare (fs); mid.prepare (fs); side.prepare (fs);
        step = (float) (1.0 / (0.03 * fs));
        reset();
    }

    void reset()
    {
        st.reset(); mid.reset(); side.reset();
        msGain = msTarget ? 1.0f : 0.0f;
        stIdle = msGain >= 1.0f; msIdle = msGain <= 0.0f; warm = 0;
    }

    // base: the shared settings (DRIVE / MIX of base are the STEREO ones); msMode: M/S selected
    void set (const HarmonicColor::Settings& base, bool msMode, const MS& ms, bool force)
    {
        st.set (base, force);
        auto m = base, s = base;
        m.drive = ms.midDrive;  m.mix = std::clamp (base.mix * ms.midMix, 0.0f, 1.0f);
        s.drive = ms.sideDrive; s.mix = std::clamp (base.mix * ms.sideMix, 0.0f, 1.0f);
        mid.set (m, force); side.set (s, force);
        msSelected = msMode;
        // the M/S path only while the module is on in M/S (or still fading out there)
        msTarget = msMode && (base.on || (msGain > 0.0f && (mid.isActive() || side.isActive())));
        if (force)
        {
            msGain = msTarget ? 1.0f : 0.0f;
            if (msTarget) { if (! stIdle) { st.reset(); stIdle = true; } msIdle = false; }
            else          { if (! msIdle) { mid.reset(); side.reset(); msIdle = true; } stIdle = false; }
        }
    }

    int latencySamples() const { return st.latencySamples(); }
    bool isActive() const { return st.isActive() || mid.isActive() || side.isActive(); }
    bool isMS() const { return msGain > 0.5f; }
    const HarmonicColor& meters() const { return isMS() ? mid : st; }
    const HarmonicColor& midMeters() const { return mid; }
    const HarmonicColor& sideMeters() const { return side; }

    // STEREO and steady (no M/S, no cross-fade)? Then the caller runs stereo().process() itself, with exactly the
    // call 1.7d made (same arguments, constants included): the compiler sees the very same call site, so STEREO
    // stays bit-identical whatever it decides about inlining and FMA contraction.
    bool steadyStereo()
    {
        // a module turned off while in M/S goes back to the exact STEREO bypass once its own fade has ended
        if (msTarget && ! msSelected) msTarget = false;
        if (msTarget && ! st.getSettings().on && ! mid.isActive() && ! side.isActive()) msTarget = false;
        if (! msTarget && msGain <= 0.0f)
        {
            if (! msIdle) { mid.reset(); side.reset(); msIdle = true; }
            if (stIdle) { st.reset(); stIdle = false; }
            return true;
        }
        return false;
    }
    HarmonicColor& stereo() { return st; }

    void process (float* L, float* R, int n, float level)
    {
        if (steadyStereo()) { st.process (L, R, n, level); return; }
        if (msTarget && msGain >= 1.0f)                    // M/S, steady
        {
            if (! stIdle) { st.reset(); stIdle = true; }
            if (msIdle) { mid.reset(); side.reset(); msIdle = false; }
            processMS (L, R, n, level);
            return;
        }
        // cross-fade: both paths run on copies. A path that was idle starts from a clean state and first runs
        // unheard for its latency + 128 samples (its delay line and filters fill), then the 30 ms ramp starts.
        if (stIdle) { st.reset(); stIdle = false; warm = st.latencySamples() + 128; }
        if (msIdle) { mid.reset(); side.reset(); msIdle = false; warm = st.latencySamples() + 128; }
        for (int pos = 0; pos < n; pos += kBlock)
        {
            const int m = std::min (kBlock, n - pos);
            float aL[kBlock], aR[kBlock], bL[kBlock], bR[kBlock];
            std::memcpy (aL, L + pos, sizeof (float) * (size_t) m); std::memcpy (aR, R + pos, sizeof (float) * (size_t) m);
            std::memcpy (bL, aL, sizeof (float) * (size_t) m);      std::memcpy (bR, aR, sizeof (float) * (size_t) m);
            st.process (aL, aR, m, level);
            processMS (bL, bR, m, level);
            for (int i = 0; i < m; ++i)
            {
                if (warm > 0) --warm;
                else msGain = msTarget ? std::min (1.0f, msGain + step) : std::max (0.0f, msGain - step);
                L[pos + i] = aL[i] + msGain * (bL[i] - aL[i]);
                R[pos + i] = aR[i] + msGain * (bR[i] - aR[i]);
            }
        }
        if (warm <= 0 && msGain <= 0.0f && ! msTarget) { mid.reset(); side.reset(); msIdle = true; }
        if (warm <= 0 && msGain >= 1.0f && msTarget)   { st.reset(); stIdle = true; }
    }

private:
    static constexpr int kBlock = 64;

    void processMS (float* L, float* R, int n, float level)
    {
        for (int pos = 0; pos < n; pos += kBlock)
        {
            const int m = std::min (kBlock, n - pos);
            float M[kBlock], S[kBlock];
            for (int i = 0; i < m; ++i)
            {
                const float l = L[pos + i], r = R[pos + i];
                M[i] = 0.5f * (l + r);
                S[i] = 0.5f * (l - r);
            }
            mid.processMono (M, m, level);
            side.processMono (S, m, level);
            for (int i = 0; i < m; ++i)
            {
                L[pos + i] = M[i] + S[i];
                R[pos + i] = M[i] - S[i];
            }
        }
    }

    HarmonicColor st, mid, side;
    double fs = 48000.0;
    float msGain = 0.0f, step = 0.0f;
    int warm = 0;
    bool msTarget = false, msSelected = false, stIdle = false, msIdle = true;
};
} // namespace hc
