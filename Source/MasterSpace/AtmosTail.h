#pragma once
// ATMOSPHERIC TAIL — MASTER SPACE late reverberation engine (experimental, MAS_ATMOS builds).
// Included by MSEngine.h after LateReverb: uses its DelayLine, Allpass, Shelf, Tilt, Rng, fastSin2Pi.
//
// Structure (one sample):
//   input  -> mid / side -> input diffusers (DIFFUSION) -> 3-band split (150 / 550 Hz, complementary)
//          -> excitation buffers: P = LF+HI+side, Q = LF+HI-side, LM (low-mid, AIR level)
//   BLOOM  -> every tank line is excited once, at its own time: offsets over SPREAD plus a position
//             in the bloom window drawn from the bloom envelope (SHAPE: more lines start late for a
//             slow, back-loaded opening). Energy enters the network gradually, from different times
//             on different lines: envelope, excitation and density development; the low-mid part of
//             each excitation arrives a little later (AIR).
//   TANK   -> 16 delay lines (geometric 36..110 ms, prime lengths), two absorbent all-passes per
//             line (DENSITY), 3-band decay shelves (DECAY, DECAY EQ, AIR), 16x16 Hadamard mixing.
//             Every line and both all-passes are modulated by its own slow sine + random wander
//             (LUSH MOTION); the line lengths themselves never follow DECAY (no pitch movement).
//   OUTPUT -> line ends + mid taps, slowly rotating pair mixing (spatial MOTION, no pitch change),
//             two orthogonal sign patterns -> L/R, M/S correlation: highs = 1 - decorrelation,
//             lows more correlated (mono compatibility), late colour tilt.
//   CHARACTER LOCK (when on) adapts modulation depth, bloom / spread times and the LF decay ratio to
//             DECAY so that the tail keeps its character from 1 s to 30 s.

class AtmosTail
{
public:
    static constexpr int N = 16;              // tank lines
    static constexpr int K = 1;               // excitation taps per line (one: several copies of the
                                              // same signal in one line make a comb that colours the whole tail)
    static constexpr float kApR1 = 0.153f, kApR2 = 0.237f, kApGMax = 0.62f;
    static constexpr float kMidTap = 0.41f, kMidW = 0.7f;
    static constexpr double kMaxScale = 1.45, kMinScale = 0.75;
    static constexpr double kMaxBloomSec = 0.36, kMaxSpreadSec = 0.5, kMaxLateSec = 0.3, kMaxAirSec = 0.03;
    static constexpr int kTick = 32;          // control rate (samples)

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static constexpr int perm[N] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
        for (int k = 0; k < N; ++k)
            baseMs[perm[k]] = 36.0 * std::pow (3.0, (double) k / (N - 1));
        const double maxLineMs = 110.1 * kMaxScale + 6.0;
        for (int i = 0; i < N; ++i)
        {
            lines[i].allocate ((int) std::ceil (maxLineMs * 0.001 * fs) + 16);
            ap1[i].allocate ((int) std::ceil (kApR1 * maxLineMs * 0.001 * fs) + 16);
            ap2[i].allocate ((int) std::ceil (kApR2 * maxLineMs * 0.001 * fs) + 16);
        }
        const int excLen = (int) std::ceil ((kMaxLateSec + kMaxSpreadSec + kMaxBloomSec * 1.6 + kMaxAirSec + 0.02) * fs);
        bufP.allocate (excLen); bufQ.allocate (excLen); bufLM.allocate (excLen);
        for (auto& a : difM) a.allocate ((int) std::ceil (0.012 * fs));
        for (auto& a : difS) a.allocate ((int) std::ceil (0.012 * fs));
        for (auto& c : color) c.prepare (fs);
        a150 = (float) (1.0 - std::exp (-2.0 * kPi * 150.0 / fs));
        a550 = (float) (1.0 - std::exp (-2.0 * kPi * 550.0 / fs));
        lpA = (float) (1.0 - std::exp (-2.0 * kPi * 180.0 / fs));
        eA = (float) (1.0 - std::exp (-1.0 / (0.35 * fs)));
        // fixed per-tap random numbers (tap jitter, low-mid lag) and per-line phases
        Rng r { 0xA7105u };
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < K; ++k) { tapJit[i][k] = r.bi(); tapLag[i][k] = r.uni(); }
        haveTargets = false;
        reset();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i)
        {
            lines[i].clear(); ap1[i].clear(); ap2[i].clear();
            shLow[i].clear(); shMid[i].clear(); shHigh[i].clear();
            spin[i] = std::fmod (0.381966 * i, 1.0);
            wanT[i] = wan1[i] = wan2[i] = 0.0f;
            wanTimer[i] = 0.0;
            modCur[i] = modInc[i] = 0.0f;
        }
        for (int j = 0; j < N / 2; ++j) { rotPhase[j] = std::fmod (0.2763 * j, 1.0); rc[j] = 1.0f; rs[j] = 0.0f; rcInc[j] = rsInc[j] = 0.0f; }
        bufP.clear(); bufQ.clear(); bufLM.clear();
        for (auto& a : difM) a.clear();
        for (auto& a : difS) a.clear();
        for (auto& c : color) c.clear();
        lp150 = lp550 = mLP = sLP = 0.0f;
        eM = eS = 0.0; bwM.clear(); bwS.clear();
        rng = Rng { 0x5A7E11u };
        tickCount = 0;
        if (haveTargets) snapToTargets();
    }

    // Character Lock factors (exposed for the tests and the report)
    static double lockModFactor (double rt)    { return std::clamp (std::pow (rt / 1.6, -0.25), 0.45, 1.25); }
    static double lockTimeFactor (double rt)   { return std::clamp (std::pow (rt / 1.6, 0.2), 0.85, 1.6); }
    static double lockLfFactor (double rt)     { return std::clamp (1.0 - 0.35 * std::log2 (std::max (rt, 0.1) / 3.0), 0.3, 1.0); }
    static double bloomSeconds (float bloom)   { const double b = std::clamp ((double) bloom, 0.0, 1.0); return 0.006 + (kMaxBloomSec - 0.006) * b * b; }
    static double atmosScale (double lateScale) { return std::clamp (std::sqrt (lateScale), kMinScale, kMaxScale); }

    void update (const Params& p, double lateScale, double rt, bool force)
    {
        const bool lock = p.charLock;
        const double air = std::clamp ((double) p.air, 0.0, 1.0);

        // ---- geometry: line lengths (prime samples), only from SIZE / room, never from DECAY
        const double scale = atmosScale (lateScale);
        if (! haveTargets || std::abs (scale - tScale) > 1e-6)
        {
            tScale = scale;
            int used[N];
            for (int i = 0; i < N; ++i)
            {
                int t = (int) std::lround (baseMs[i] * scale * 0.001 * fs);
                while (! isPrime (t) || std::find (used, used + i, t) != used + i) ++t;
                used[i] = t;
                tLen[i] = (float) t;
            }
        }

        // ---- decay per band (seconds), DECAY EQ, AIR, Character Lock
        tRT = std::clamp (rt, 0.1, 60.0);
        double lf = p.lfMult, lm = p.lmMult, hm = p.hmMult, hf = p.hfMult;
        if (lock) lf = 1.0 + (lf - 1.0) * lockLfFactor (tRT);          // long tails: no LF boom
        hf *= 0.8 + 0.4 * air;                                         // AIR opens the top ...
        hm *= 0.9 + 0.2 * air;
        const double rtAirHi = 3.0 + 9.0 * air;                        // ... and limits how long it rings
        auto capped = [] (double r, double cap) { return 1.0 / (1.0 / r + 1.0 / cap); };
        tBandRT[0] = tRT * std::max (0.05, lf);
        tBandRT[1] = tRT * std::max (0.05, lm);
        tBandRT[2] = capped (tRT * std::max (0.05, hm), 5.0 * rtAirHi);
        tBandRT[3] = capped (tRT * std::max (0.05, hf), rtAirHi);
        xLo = std::clamp ((double) p.xLow, 40.0, 1000.0);
        xMi = std::clamp ((double) p.xMid, 1.5 * xLo, 8000.0);
        xHi = std::clamp ((double) p.xHigh, 1.5 * xMi, 18000.0);
        tApG = (float) (kApGMax * std::clamp ((double) p.density, 0.0, 1.0));
        tDifG = (float) (0.74 * std::clamp ((double) p.diffusion, 0.0, 1.0));

        // ---- modulation (LUSH MOTION): MOTION = amount, MODULATION page = rate / random / smoothing / character
        const double motion = std::clamp ((double) p.motion, 0.0, 1.0);
        static constexpr double charRate[4] = { 1.0, 1.0, 1.4, 0.7 }, charRand[4] = { 0.0, 0.3, 0.1, -0.1 };
        const int ch = std::clamp (p.modCharacter, 0, 3);
        const double depthMs = (0.04 + 1.4 * motion * motion) * std::clamp (p.modDepth / 0.35, 0.0, 2.0) * (lock ? lockModFactor (tRT) : 1.0);
        tModA = (float) (depthMs * 0.001 * fs);
        modRate = std::clamp ((double) p.modRate, 0.01, 5.0) * charRate[ch];
        modRand = (float) std::clamp ((double) p.modRandom + charRand[ch], 0.0, 1.0);
        modSmooth = std::clamp ((double) p.modSmooth, 0.0, 1.0);
        tRotDepth = (float) (0.55 * motion);
        rotRate = 0.06 * std::sqrt (std::clamp ((double) p.modRate, 0.05, 5.0) / 0.6);

        // ---- excitation taps (BLOOM / SHAPE / SPREAD / LATE DELAY / AIR)
        const double tf = lock ? lockTimeFactor (tRT) : 1.0;
        tLevel = (float) std::pow (tf, 0.27);       // Character Lock: same initial level when the opening is stretched
        const double tb = bloomSeconds (p.bloom) * tf;
        const double spr = std::min (kMaxSpreadSec, std::clamp ((double) p.spreadMs, 0.0, 500.0) * 0.001 * tf);
        const double difLat = 0.5 * 0.0145 * (tDifG / 0.74);           // energy centroid of the input diffusers
        const double t0 = std::max (0.0, std::clamp ((double) p.lateDelayMs, 0.0, 300.0) * 0.001 - difLat);
        // bloom envelope e(x) ~ x^pw over the window -> energy CDF x^(2pw+1): line start times are its quantiles
        const double pw = 0.15 + 2.2 * std::clamp ((double) p.shape, 0.0, 1.0);
        const double q = 1.0 / (2.0 * pw + 1.0);
        const double lag = air * 0.022;
        for (int i = 0; i < N; ++i)
        {
            const double off = spr * std::fmod (0.5 + 0.618034 * i, 1.0);
            const double u = (kBloomRank[i] + 0.5 + 0.3 * tapJit[i][0]) / N;
            const double x = std::pow (std::clamp (u, 0.001, 1.0), q);
            tTap[i][0] = (float) ((t0 + off + x * tb) * fs) + 1.0f;
            tTapLM[i][0] = tTap[i][0] + (float) (lag * (0.6 + 0.8 * tapLag[i][0]) * fs);

        }
        tLmGain = (float) (1.0 - 0.25 * air);                         // AIR: less low-mid mass
        tBandwidth = 4500.0 * std::pow (4.0, air);                    // AIR: bandwidth of the excitation, 4.5 .. 18 kHz

        // ---- output: correlation (DECORRELATION, AIR: wider highs, lows more correlated)
        const double dec = std::clamp ((double) p.decorrelation, 0.0, 1.0);
        const double rHi = std::clamp (1.0 - dec * (0.9 + 0.2 * air), 0.0, 1.0), rLo = std::min (1.0, rHi + 0.4);
        tAHi = (float) std::sqrt (0.5 * (1.0 + rHi)); tBHi = (float) std::sqrt (0.5 * (1.0 - rHi));
        tALo = (float) std::sqrt (0.5 * (1.0 + rLo)); tBLo = (float) std::sqrt (0.5 * (1.0 - rLo));
        rLoTarget = rLo;
        color[0].set (p.lateColor); color[1].set (p.lateColor);

        haveTargets = true;
        if (force) snapToTargets();
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        float z[N], yo[N];
        const float gOut = kOutCal / std::sqrt (1.0f + kMidW * kMidW);
        for (int s = 0; s < n; ++s)
        {
            if (tickCount == 0) controlTick();
            tickCount = (tickCount + 1) & (kTick - 1);

            // ---- input: mid / side, diffusion, bands, excitation buffers
            float m = 0.5f * (inL[s] + inR[s]), sd = 0.5f * (inL[s] - inR[s]);
            for (int k = 0; k < 4; ++k) m = difM[k].process (m, difLen[k], difG);
            for (int k = 0; k < 2; ++k) sd = difS[k].process (sd, difLenS[k], difG);
            lp150 += a150 * (m - lp150);
            lp550 += a550 * (m - lp550);
            const float lmid = lp550 - lp150;
            float lfhi = m - lmid;
            if (bwOn) { lfhi = bwM.process (lfhi); sd = bwS.process (sd); }
            bufP.push (lfhi + sd); bufQ.push (lfhi - sd); bufLM.push (lmid * lmGain);

            // ---- tank
            for (int i = 0; i < N; ++i)
            {
                modCur[i] += modInc[i];
                const float md = modA * modCur[i];
                const float y = lines[i].readHermite (len[i] + md);
                yo[i] = y + kMidW * lines[i].read (midTap[i]);
                float v = ap1[i].process (y, ap1Len[i] - 0.5f * md, apG, apA1[i]);
                v = ap2[i].process (v, ap2Len[i] + 0.5f * modA * modCur[(i + 5) & (N - 1)], apG, apA2[i]);
                z[i] = shHigh[i].process (shMid[i].process (shLow[i].process (v * baseGain[i])));
            }
            hadamard (z);
            for (int i = 0; i < N; ++i)
            {
                const DelayLine& src = kSide[i] > 0 ? bufP : bufQ;
                // excitation taps never move (a moving read head would shift the pitch): a change of
                // position is a 40 ms cross-fade from the old tap to the new one
                float inj = src.read (tapA[i]) + bufLM.read (tapLMA[i]);
                if (xfActive[i])
                {
                    xf[i] += xfStep;
                    const float other = src.read (tapB[i]) + bufLM.read (tapLMB[i]);
                    inj += xf[i] * (other - inj);
                    if (xf[i] >= 1.0f) { tapA[i] = tapB[i]; tapLMA[i] = tapLMB[i]; xfActive[i] = false; xf[i] = 0.0f; }
                }
                lines[i].push (z[i] + kInjW * kSign[i] * inj);
            }

            // ---- output: rotating pairs, two orthogonal sign patterns, M/S correlation
            for (int j = 0; j < N / 2; ++j)
            {
                rc[j] += rcInc[j]; rs[j] += rsInc[j];
                const float a = yo[j], b = yo[j + N / 2];
                yo[j] = a * rc[j] - b * rs[j];
                yo[j + N / 2] = a * rs[j] + b * rc[j];
            }
            float L0 = 0.0f, R0 = 0.0f;
            for (int i = 0; i < N; ++i) { L0 += kOutL[i] * yo[i]; R0 += kOutR[i] * yo[i]; }
            const float M = (L0 + R0) * 0.70710678f, S = (L0 - R0) * 0.70710678f;
            mLP += lpA * (M - mLP); sLP += lpA * (S - sLP);
            // highs: fixed M/S weights (L0, R0 are uncorrelated there); lows (< 180 Hz): the few low modes
            // can leave L0 / R0 correlated either way, so their M / S weights follow the measured energies
            // and the low-frequency correlation stays at its target (mono compatibility at any setting)
            eM += eA * (mLP * mLP - eM); eS += eA * (sLP * sLP - eS);
            cM += cMInc; cS += cSInc;
            const float Mo = aHi * (M - mLP) + cM * mLP, So = bHi * (S - sLP) + cS * sLP;
            outL[s] = color[0].process ((Mo + So) * 0.25f * gOut * level);
            outR[s] = color[1].process ((Mo - So) * 0.25f * gOut * level);
        }
        // safety net: a non-finite value never stays in the network
        if (! std::isfinite (outL[n - 1] + outR[n - 1])) { reset(); for (int s = 0; s < n; ++s) outL[s] = outR[s] = 0.0f; }
    }

    double extraTailSeconds() const { return bloomSeconds (1.0f) * 1.6 + kMaxSpreadSec; }

private:
    static constexpr float kOutCal = 1.26f;   // +2 dB: at 1.8 s between Hall and Chamber at the same Late Level
    static constexpr float kInjW = 0.25f;     // 1 / sqrt (N)
    static constexpr int kBloomRank[N] = { 5, 12, 0, 9, 14, 3, 10, 7, 1, 15, 6, 11, 2, 13, 8, 4 };
    static constexpr float kSign[N] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1 };
    static constexpr float kSide[N] = { 1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, 1 };
    static constexpr float kOutL[N] = { 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1 };   // orthogonal patterns
    static constexpr float kOutR[N] = { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr float difLenMs[4] = { 1.7f, 2.9f, 4.1f, 6.3f }, difLenSMs[2] = { 2.3f, 5.3f };

    static bool isPrime (int v)
    {
        if (v < 2) return false;
        for (int d = 2; d * d <= v; ++d) if (v % d == 0) return false;
        return true;
    }

    static void hadamard (float* x) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b; x[j + h] = a - b;
                }
        for (int i = 0; i < N; ++i) x[i] *= 0.25f;
    }

    // loop gains and decay shelves from the current (smoothed) band decay times
    void computeLoop()
    {
        apG = curApG;
        double rate[4];
        for (int b = 0; b < 4; ++b) rate[b] = 60.0 / std::max (0.05, curBandRT[b]);
        for (int i = 0; i < N; ++i)
        {
            len[i] = curLen[i];
            midTap[i] = std::max (2, (int) (curLen[i] * kMidTap));
            ap1Len[i] = std::max (4.0f, curLen[i] * kApR1);
            ap2Len[i] = std::max (4.0f, curLen[i] * kApR2);
            const double apSec1 = ap1Len[i] / fs, apSec2 = ap2Len[i] / fs;
            const double loopSec = (curLen[i] + ap1Len[i] + ap2Len[i]) / fs;
            // absorbent all-passes lose energy at the mid rate; the line gains make up the rest per band
            apA1[i] = (float) std::pow (10.0, -rate[1] * apSec1 / 20.0);
            apA2[i] = (float) std::pow (10.0, -rate[1] * apSec2 / 20.0);
            double g[4];
            for (int b = 0; b < 4; ++b)
                g[b] = std::pow (10.0, -(rate[b] * loopSec - rate[1] * (apSec1 + apSec2)) / 20.0);
            // stability (as in the 1.2 engines): band spread limit and peak-gain bound of the all-passes
            const double gMax = std::max ({ g[0], g[1], g[2], g[3] });
            const double ga = apG;
            auto apPeak = [ga] (double a) { return std::max ((ga + a) / (1.0 + ga * a), std::abs (a - ga) / (1.0 - ga * a)); };
            const double cap = 0.995 / std::max (1e-9, apPeak (apA1[i]) * apPeak (apA2[i]));
            for (int b = 0; b < 4; ++b) g[b] = std::min (std::max (g[b], gMax * 1e-3), cap);
            baseGain[i] = (float) g[1];
            shLow[i].set (xLo, g[0] / g[1], fs, false);
            shMid[i].set (xMi, g[2] / g[1], fs, true);
            shHigh[i].set (xHi, g[3] / g[2], fs, true);
        }
    }

    void snapToTargets()
    {
        for (int i = 0; i < N; ++i) curLen[i] = tLen[i];
        for (int b = 0; b < 4; ++b) curBandRT[b] = tBandRT[b];
        curApG = tApG; difG = tDifG; modA = tModA; level = tLevel; rotDepth = tRotDepth; lmGain = tLmGain;
        aHi = tAHi; bHi = tBHi; aLo = tALo; bLo = tBLo;
        bandwidth = tBandwidth;
        bwM.setLowPass (bandwidth, fs, 0.6); bwS.setLowPass (bandwidth, fs, 0.6);
        bwOn = bandwidth < 0.42 * fs && bandwidth < 17900.0;
        cM = aLo; cS = bLo; cMInc = cSInc = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            tapA[i] = tapB[i] = (int) std::lround (tTap[i][0]);
            tapLMA[i] = tapLMB[i] = (int) std::lround (tTapLM[i][0]);
            xf[i] = 0.0f; xfActive[i] = false;
        }
        xfStep = (float) (1.0 / (0.04 * fs));
        for (int k = 0; k < 4; ++k) difLen[k] = std::max (2, (int) (difLenMs[k] * 0.001 * fs));
        for (int k = 0; k < 2; ++k) difLenS[k] = std::max (2, (int) (difLenSMs[k] * 0.001 * fs));
        computeLoop();
    }

    // every kTick samples: smoothing of all coefficients, modulation and rotation targets
    void controlTick()
    {
        const double dt = kTick / fs;
        bool loopDirty = false;

        // decay: per band, in the log domain (~60 ms), recomputed while moving
        const double aRT = 1.0 - std::exp (-dt / 0.06);
        for (int b = 0; b < 4; ++b)
        {
            const double d = std::log (tBandRT[b] / curBandRT[b]);
            if (std::abs (d) > 1e-5) { curBandRT[b] *= std::exp (aRT * d); loopDirty = true; }
            else curBandRT[b] = tBandRT[b];
        }
        // size: line lengths glide (~80 ms)
        const float aLen = (float) (1.0 - std::exp (-dt / 0.08));
        for (int i = 0; i < N; ++i)
        {
            const float d = tLen[i] - curLen[i];
            if (std::abs (d) > 1e-3f) { curLen[i] += aLen * d; loopDirty = true; } else curLen[i] = tLen[i];
        }
        if (std::abs (tApG - curApG) > 1e-5f) { curApG += 0.1f * (tApG - curApG); loopDirty = true; }
        if (loopDirty) computeLoop();

        const float aF = (float) (1.0 - std::exp (-dt / 0.03));
        level += aF * (tLevel - level);
        difG += aF * (tDifG - difG); modA += aF * (tModA - modA); rotDepth += aF * (tRotDepth - rotDepth); lmGain += aF * (tLmGain - lmGain);
        aHi += aF * (tAHi - aHi); bHi += aF * (tBHi - bHi); aLo += aF * (tALo - aLo); bLo += aF * (tBLo - bLo);

        // excitation taps: start a cross-fade to the new position when it moved (see process)
        for (int i = 0; i < N; ++i)
        {
            const int t = (int) std::lround (tTap[i][0]), tl = (int) std::lround (tTapLM[i][0]);
            if (! xfActive[i] && (t != tapA[i] || tl != tapLMA[i]))
            {
                tapB[i] = t; tapLMB[i] = tl; xf[i] = 0.0f; xfActive[i] = true;
            }
        }

        // modulation: sine spin + random wander per line
        for (int i = 0; i < N; ++i)
        {
            const double ri = modRate * (0.45 + 0.9 * std::fmod (0.618034 * (i + 1), 1.0));
            spin[i] += ri * dt; if (spin[i] >= 1.0) spin[i] -= 1.0;
            wanTimer[i] -= dt;
            if (wanTimer[i] <= 0.0) { wanT[i] = rng.bi(); wanTimer[i] = (0.8 + 0.4 * rng.uni()) / (ri * 0.8); }
            const float aw = (float) (1.0 - std::exp (-dt * ri * (6.0 - 5.0 * modSmooth)));
            wan1[i] += aw * (wanT[i] - wan1[i]);
            wan2[i] += aw * (wan1[i] - wan2[i]);
            const float target = (1.0f - modRand) * fastSin2Pi (spin[i]) + modRand * 1.4f * wan2[i];
            modInc[i] = (target - modCur[i]) / kTick;
        }
        // low-frequency correlation weights (see process)
        {
            const double e = eM + eS;
            float tcM = cM, tcS = cS;
            if (e > 1e-14)
            {
                tcM = (float) std::min (2.5, std::sqrt (0.25 * (1.0 + rLoTarget) * e / std::max (eM, 1e-20)));
                tcS = (float) std::min (2.5, std::sqrt (0.25 * (1.0 - rLoTarget) * e / std::max (eS, 1e-20)));
            }
            cMInc = (tcM - cM) / kTick; cSInc = (tcS - cS) / kTick;
        }
        if (std::abs (tBandwidth - bandwidth) > 1.0)
        {
            bandwidth += 0.2 * (tBandwidth - bandwidth);
            bwM.setLowPass (bandwidth, fs, 0.6); bwS.setLowPass (bandwidth, fs, 0.6);
        }
        bwOn = bandwidth < 0.42 * fs && bandwidth < 17900.0;

        // spatial rotation
        for (int j = 0; j < N / 2; ++j)
        {
            rotPhase[j] += rotRate * (0.8 + 0.5 * std::fmod (0.618034 * (j + 3), 1.0)) * dt;
            if (rotPhase[j] >= 1.0) rotPhase[j] -= 1.0;
            const float ang = rotDepth * fastSin2Pi (rotPhase[j]);
            rcInc[j] = (std::cos (ang) - rc[j]) / kTick;
            rsInc[j] = (std::sin (ang) - rs[j]) / kTick;
        }
    }

    double fs = 48000.0;
    double baseMs[N] {};
    DelayLine lines[N];
    Allpass ap1[N], ap2[N], difM[4], difS[2];
    Shelf shLow[N], shMid[N], shHigh[N];
    DelayLine bufP, bufQ, bufLM;
    Tilt color[2];
    Rng rng { 0x5A7E11u };

    // targets (update) and current values (control rate)
    bool haveTargets = false;
    double tScale = 1.0, tRT = 1.8, tBandRT[4] { 1.8, 1.8, 1.8, 1.8 }, curBandRT[4] { 1.8, 1.8, 1.8, 1.8 };
    double xLo = 200, xMi = 1500, xHi = 6000;
    float tLen[N] {}, curLen[N] {}, len[N] {}, ap1Len[N] {}, ap2Len[N] {}, apA1[N] {}, apA2[N] {}, baseGain[N] {};
    int midTap[N] {};
    float tApG = 0.4f, curApG = 0.4f, apG = 0.4f, tDifG = 0.5f, difG = 0.5f;
    int difLen[4] { 80, 140, 200, 300 }, difLenS[2] { 110, 250 };
    float tLevel = 1, level = 1, tModA = 0, modA = 0, tRotDepth = 0, rotDepth = 0, tLmGain = 1, lmGain = 1;
    double modRate = 0.6, modSmooth = 0.6;
    float modRand = 0.5f;
    double rotRate = 0.06;
    float tAHi = 0.7f, tBHi = 0.7f, tALo = 0.9f, tBLo = 0.4f, aHi = 0.7f, bHi = 0.7f, aLo = 0.9f, bLo = 0.4f;
    float tTap[N][K] {}, tTapLM[N][K] {};
    int tapA[N] {}, tapB[N] {}, tapLMA[N] {}, tapLMB[N] {};
    float xf[N] {}, xfStep = 1.0f / 1920.0f;
    bool xfActive[N] {};
    float tapJit[N][K] {}, tapLag[N][K] {};

    // state
    double spin[N] {}, wanTimer[N] {}, rotPhase[N / 2] {};
    float wanT[N] {}, wan1[N] {}, wan2[N] {}, modCur[N] {}, modInc[N] {};
    float rc[N / 2] {}, rs[N / 2] {}, rcInc[N / 2] {}, rsInc[N / 2] {};
    float lp150 = 0, lp550 = 0, mLP = 0, sLP = 0, a150 = 0.02f, a550 = 0.07f, lpA = 0.02f, eA = 1e-4f;
    double eM = 0.0, eS = 0.0, rLoTarget = 0.55, tBandwidth = 9000.0, bandwidth = 9000.0;
    float cM = 0.85f, cS = 0.5f, cMInc = 0.0f, cSInc = 0.0f;
    Biquad bwM, bwS;
    bool bwOn = true;
    int tickCount = 0;
};
