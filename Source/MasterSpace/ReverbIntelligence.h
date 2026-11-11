#pragma once
// MASTER SPACE 1.4 — REVERB INTELLIGENCE (MAS_RI builds only): SMART DUCKER and RESONANCE GUARD.
// Included by MSEngine.h after the late engines. Header-only; every buffer is allocated in prepare(),
// process() never allocates, locks or touches files. Both modules act on the finished wet signal (early
// reflections and late field separately), never on the feedback of the reverb networks: the tail keeps
// developing inside while its output is shaped.

namespace ri
{
static constexpr double kPi = 3.14159265358979323846;

inline float fastDbToGain (float db) noexcept { return std::exp (0.11512925465f * db); }   // 10^(db/20)
inline float powerToDb (float p) noexcept { return 4.3429448190f * std::log (std::max (p, 1e-20f)); }   // 10 log10

// RBJ band-pass, constant 0 dB peak gain (b1 = 0, b2 = -b0), transposed direct form II
struct BandPass
{
    float b0 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    void set (double f, double q, double fs)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w0 = 2.0 * kPi * f / fs, al = std::sin (w0) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) (al / a0); a1 = (float) (-2.0 * std::cos (w0) / a0); a2 = (float) ((1.0 - al) / a0);
    }
    inline float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = -a1 * y + s2;
        s2 = -b0 * x - a2 * y;
        return y;
    }
    void clear() { s1 = s2 = 0.0f; }
};

// RBJ peaking section with fixed centre / Q; only the gain moves (cheap per-tick update, smooth)
struct Peak
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double c = 1.0, al = 0.1;
    void design (double f, double q, double fs)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w0 = 2.0 * kPi * f / fs;
        c = std::cos (w0); al = std::sin (w0) / (2.0 * q);
        setGainDb (0.0);
    }
    void setGainDb (double db)
    {
        const double A = std::pow (10.0, db / 40.0), a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0); b1 = (float) (-2.0 * c / a0); b2 = (float) ((1.0 - al * A) / a0);
        a1 = b1; a2 = (float) ((1.0 - al / A) / a0);
    }
    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void clear() { x1 = x2 = y1 = y2 = 0.0f; }
};

// 2nd-order sidechain filter (RBJ high- / low-pass)
struct ScFilter
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (double hz, double fs, bool high)
    {
        hz = std::clamp (hz, 10.0, 0.45 * fs);
        const double w0 = 2.0 * kPi * hz / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.70710678), a0 = 1.0 + al;
        if (high) { b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); }
        else      { b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); }
        b2 = b0; a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void clear() { x1 = x2 = y1 = y2 = 0.0f; }
};

//==============================================================================
// SMART DUCKER
//   CLASSIC      broadband gain on the whole wet signal, compressor-like gain computer + hold.
//   VOCAL FOCUS  phrase detector (peak envelope bridging the gaps between words) and S-shaped release:
//                the reduction stays steady during a phrase (no pumping on syllables) and opens smoothly
//                in the pauses; separate depth for early reflections and late field (EARLY / LATE DUCK).
//   SPECTRAL     the vocal-focus reduction is distributed over frequency where the direct sound is masked
//                by the reverb (sidechain spectral envelope vs. wet spectral envelope, 16 analysis bands,
//                smoothed over frequency and time so harmonics and single partials never steer it).
//   The gain is applied to the formed wet signal only; when the module is off and its reduction has
//   returned to 0 dB the wet passes untouched (bit-exact).
class SmartDucker
{
public:
    enum Mode { Classic = 0, VocalFocus = 1, Spectral = 2 };
    static constexpr int NB = 16;              // spectral bands
    static constexpr int kTick = 16;           // control rate (samples)
    static constexpr double kBandQ = 1.6, kBankScale = 2.49;   // cascade of 16 peaking sections: uniform x dB -> about 2.49 x dB

    struct Settings
    {
        bool on = false;
        int mode = VocalFocus;
        float amountDb = 6, threshDb = -30, attackMs = 10, holdMs = 120, releaseMs = 600, ratio = 4, sensitivity = 0.5f;
        float early = 0.35f, late = 1.0f, spectral = 0.6f, scLowHz = 100, scHighHz = 8000;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int k = 0; k < NB; ++k)
        {
            fc[k] = 90.0 * std::pow (12000.0 / 90.0, (double) k / (NB - 1));
            scBand[k].set (fc[k], 2.0, fs); wetBand[k].set (fc[k], 2.0, fs);
            for (int c = 0; c < 4; ++c) bank[c][k].design (fc[k], kBandQ, fs);
        }
        haveSettings = false;
        reset();
    }

    void reset()
    {
        hpf.clear(); lpf.clear();
        for (int k = 0; k < NB; ++k) { scBand[k].clear(); wetBand[k].clear(); dE[k] = wE[k] = 1e-12f; wSm[k] = 0.0f; bandDb[k] = 0.0f; }
        for (auto& ch : bank) for (auto& p : ch) { p.clear(); p.setGainDb (0.0); }
        rms = 0.0f; peakEnv = 0.0f; held = 0.0f; holdLeft = 0; g1 = g2 = 0.0f;
        gE = gL = 1.0f; gEInc = gLInc = 0.0f; tick = 0; bankActive = false; active = false;
        if (haveSettings) set (s, true);
    }

    void set (const Settings& in, bool force = false)
    {
        s = in;
        s.amountDb = std::clamp (s.amountDb, 0.0f, 18.0f);
        s.ratio = std::clamp (s.ratio, 1.0f, 10.0f);
        if (force || ! haveSettings || s.scLowHz != curLo || s.scHighHz != curHi)
        {
            curLo = s.scLowHz; curHi = s.scHighHz;
            hpf.set (std::clamp ((double) s.scLowHz, 20.0, 500.0), fs, true);
            lpf.set (std::clamp ((double) s.scHighHz, 1000.0, 20000.0), fs, false);
        }
        const double ctl = fs / kTick;
        auto coef = [ctl] (double ms) { return (float) (1.0 - std::exp (-1.0 / (std::max (ms, 0.1) * 0.001 / 2.3 * ctl))); };   // ~90 % in the given time
        aAtt = coef (std::clamp ((double) s.attackMs, 1.0, 100.0));
        aRel = coef (std::clamp ((double) s.releaseMs, 50.0, 3000.0));
        aRel2 = coef (0.59 * std::clamp ((double) s.releaseMs, 50.0, 3000.0));     // two stages (S-shaped), 90 % at RELEASE
        holdTicks = (int) (std::clamp ((double) s.holdMs, 0.0, 500.0) * 0.001 * ctl);
        aRms = (float) (1.0 - std::exp (-1.0 / (0.005 * fs)));
        aPeak = (float) (1.0 - std::exp (-1.0 / (0.015 * fs)));                    // fast fall: HOLD bridges the gaps, timing stays as set
        aBand = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));
        haveSettings = true;
    }

    // sidechain (stereo, may be the plugin input), wet buffers processed in place.
    // erGain / lateGain: current levels of the two parts (spectral analysis of what is heard).
    void process (const float* scL, const float* scR, float* erL, float* erR, float* ltL, float* ltR, int n,
                  float erGain, float lateGain)
    {
        if (! s.on && ! active) return;                     // off and fully released: untouched
        const bool spectral = s.mode == Spectral;
        for (int i0 = 0; i0 < n; i0 += kTick)
        {
            const int m = std::min (kTick, n - i0);
            // ---- detection (per sample), sidechain filtered
            for (int i = i0; i < i0 + m; ++i)
            {
                const float x = lpf.process (hpf.process (0.5f * (scL[i] + scR[i])));
                rms += aRms * (x * x - rms);
                const float ax = x * x;
                peakEnv = ax > peakEnv ? ax : peakEnv + aPeak * (ax - peakEnv);
                if (spectral)
                {
                    const float w = 0.5f * (erGain * (erL[i] + erR[i]) + lateGain * (ltL[i] + ltR[i]));
                    for (int k = 0; k < NB; ++k)
                    {
                        const float d = scBand[k].process (x), v = wetBand[k].process (w);
                        dE[k] += aBand * (d * d - dE[k]); wE[k] += aBand * (v * v - wE[k]);
                    }
                }
            }
            controlTick (spectral);
            // ---- apply
            const float eDepth = s.mode == Classic ? 1.0f : std::clamp (s.early, 0.0f, 1.0f);
            const float lDepth = s.mode == Classic ? 1.0f : std::clamp (s.late, 0.0f, 1.0f);
            const float spec = spectral ? std::clamp (s.spectral, 0.0f, 1.0f) : 0.0f;
            const float broad = g2 * (1.0f - spec);
            const float tgE = fastDbToGain (-broad * eDepth), tgL = fastDbToGain (-broad * lDepth);
            gEInc = (tgE - gE) / m; gLInc = (tgL - gL) / m;
            if ((spectral || bankActive) && (++bankTick & 3) == 0) updateBank (spec, eDepth, lDepth);
            for (int i = i0; i < i0 + m; ++i)
            {
                gE += gEInc; gL += gLInc;
                float el = erL[i], er = erR[i], ll = ltL[i], lr = ltR[i];
                if (bankActive)
                    for (int k = 0; k < NB; ++k)
                    {
                        if (eDepth > 0.0f) { el = bank[0][k].process (el); er = bank[1][k].process (er); }
                        ll = bank[2][k].process (ll); lr = bank[3][k].process (lr);
                    }
                erL[i] = el * gE; erR[i] = er * gE; ltL[i] = ll * gL; ltR[i] = lr * gL;
            }
            gE = tgE; gL = tgL;                             // land exactly (1.0 when nothing is reduced)
        }
        // fully released and switched off: snap to bypass (the next block is untouched)
        if (! s.on && g2 <= 0.0f && ! bankActive) { g1 = g2 = 0.0f; gE = gL = 1.0f; active = false; }
    }

    float reductionDb (bool earlyPart) const
    {
        if (! active) return 0.0f;
        const float d = s.mode == Classic ? 1.0f : (earlyPart ? s.early : s.late);
        return g2 * d;
    }
    bool isActive() const { return active && g2 > 0.5f; }

private:
    void controlTick (bool spectral)
    {
        active = true;
        // level -> target reduction (soft knee centred at THRESHOLD, width from SENSITIVITY, RATIO, capped by AMOUNT)
        float target = 0.0f;
        if (s.on)
        {
            const float lev = s.mode == Classic ? powerToDb (2.0f * rms) : powerToDb (std::max (2.0f * rms, peakEnv));
            const float over = lev - s.threshDb, W = 2.0f + 28.0f * std::clamp (s.sensitivity, 0.0f, 1.0f);
            const float c = 1.0f - 1.0f / s.ratio;
            float gr = over <= -0.5f * W ? 0.0f : over < 0.5f * W ? c * (over + 0.5f * W) * (over + 0.5f * W) / (2.0f * W) : c * over;
            // vocal focus / spectral: a phrase is either on or off, the depth does not follow every syllable
            if (s.mode != Classic) gr = std::min (s.amountDb, gr * 2.5f);
            target = std::min (s.amountDb, gr);
        }
        // hold
        if (target >= held) { held = target; holdLeft = holdTicks; }
        else if (holdLeft > 0) --holdLeft;
        else held = target;
        // ballistics (dB): attack one stage; release one stage (CLASSIC) or two stages (S-curve)
        if (held > g2) { g2 += aAtt * (held - g2); g1 = g2; }
        else if (s.mode == Classic) { g2 += aRel * (held - g2); g1 = g2; }
        else { g1 += aRel2 * (held - g1); g2 += aRel2 * (g1 - g2); }
        if (g2 < 0.005f && held <= 0.0f) { g1 = g2 = 0.0f; }          // back to exactly 0 dB: wet untouched again

        if (spectral)
        {
            // masking weights: bands where the direct sound has energy (within 36 dB of its strongest band)
            // and the reverb is not far below it
            float dMax = 1e-20f;
            for (int k = 0; k < NB; ++k) dMax = std::max (dMax, dE[k]);
            const float dMaxDb = powerToDb (dMax);
            float w[NB];
            for (int k = 0; k < NB; ++k)
            {
                const float pres = std::clamp ((powerToDb (dE[k]) - dMaxDb + 36.0f) / 30.0f, 0.0f, 1.0f);
                const float mask = wE[k] / (wE[k] + 0.25f * dE[k] + 1e-20f);
                w[k] = pres * (0.3f + 0.7f * mask);
            }
            // 1-2-1 smoothing over frequency; the most masked region gets the full depth (normalised), then slow over time
            float sm[NB], smMax = 0.0f;
            for (int k = 0; k < NB; ++k)
            {
                sm[k] = 0.25f * w[std::max (0, k - 1)] + 0.5f * w[k] + 0.25f * w[std::min (NB - 1, k + 1)];
                smMax = std::max (smMax, sm[k]);
            }
            const float norm = smMax > 0.05f ? 1.0f / smMax : 0.0f;
            for (int k = 0; k < NB; ++k)
                wSm[k] += 0.08f * (sm[k] * norm - wSm[k]);
        }
    }

    void updateBank (float spec, float eDepth, float lDepth)
    {
        bool any = false;
        for (int k = 0; k < NB; ++k)
        {
            const float target = spec * g2 * wSm[k] / (float) kBankScale;
            bandDb[k] += 0.7f * (target - bandDb[k]);
            if (bandDb[k] < 1e-3f && target <= 0.0f) bandDb[k] = 0.0f;
            any = any || bandDb[k] > 0.0f;
            if (eDepth > 0.0f) { bank[0][k].setGainDb (-bandDb[k] * eDepth); bank[1][k] = copyGain (bank[1][k], bank[0][k]); }
            bank[2][k].setGainDb (-bandDb[k] * lDepth); bank[3][k] = copyGain (bank[3][k], bank[2][k]);
        }
        if (any) bankActive = true;
        else if (bankActive) { bankActive = false; for (auto& ch : bank) for (auto& p : ch) p.clear(); }
    }

    static Peak copyGain (Peak dst, const Peak& src)
    {
        dst.b0 = src.b0; dst.b1 = src.b1; dst.b2 = src.b2; dst.a1 = src.a1; dst.a2 = src.a2;
        return dst;
    }

    double fs = 48000.0;
    Settings s;
    bool haveSettings = false;
    unsigned bankTick = 0;
    float curLo = -1, curHi = -1;
    ScFilter hpf, lpf;
    double fc[NB] {};
    BandPass scBand[NB], wetBand[NB];
    Peak bank[4][NB];                       // early L, early R, late L, late R
    float dE[NB] {}, wE[NB] {}, wSm[NB] {}, bandDb[NB] {};
    float rms = 0, peakEnv = 0, held = 0, g1 = 0, g2 = 0;
    int holdLeft = 0, holdTicks = 0, tick = 0;
    float aAtt = 0.1f, aRel = 0.01f, aRel2 = 0.02f, aRms = 0.01f, aPeak = 0.001f, aBand = 0.001f;
    float gE = 1, gL = 1, gEInc = 0, gLInc = 0;
    bool bankActive = false, active = false;
};

//==============================================================================
// radix-2 complex FFT, in place, tables built in prepare(); bitReverse() and pass() let a transform be
// spread over many audio samples (no CPU spike in a single small audio block)
struct Fft
{
    int n = 0, logN = 0;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
    void prepare (int size)
    {
        n = size; logN = 0; while ((1 << logN) < n) ++logN;
        cosT.resize ((size_t) n / 2); sinT.resize ((size_t) n / 2); rev.resize ((size_t) n);
        for (int k = 0; k < n / 2; ++k) { cosT[(size_t) k] = (float) std::cos (2.0 * kPi * k / n); sinT[(size_t) k] = (float) std::sin (2.0 * kPi * k / n); }
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < logN; ++b) if (i & (1 << b)) r |= 1 << (logN - 1 - b); rev[(size_t) i] = r; }
    }
    void bitReverse (float* re, float* im) const noexcept
    {
        for (int i = 0; i < n; ++i) { const int j = rev[(size_t) i]; if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
    }
    // stage s = 1 .. logN
    void pass (float* re, float* im, int stage, bool inverse) const noexcept
    {
        const int len = 1 << stage, half = len >> 1, step = n / len;
        const float sg = inverse ? 1.0f : -1.0f;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                const float wr = cosT[(size_t) (j * step)], wi = sg * sinT[(size_t) (j * step)];
                float* a = re + i + j; float* b = im + i + j;
                const float xr = a[half] * wr - b[half] * wi, xi = a[half] * wi + b[half] * wr;
                a[half] = a[0] - xr; b[half] = b[0] - xi; a[0] += xr; b[0] += xi;
            }
    }
    void transform (float* re, float* im, bool inverse) const noexcept
    {
        bitReverse (re, im);
        for (int st = 1; st <= logN; ++st) pass (re, im, st, inverse);
    }
};

//==============================================================================
// RESONANCE GUARD
//   Finds narrow, persistent excess energy in the reverb field and cuts it gently, only there.
//   Detection (all modes), once per hop (~21 ms) on a log-frequency grid:
//     wet band energy W(b) vs. the SOURCE MEMORY M(b): the signal that feeds the reverb, integrated per
//     band with the decay the reverb is set to have there (DECAY x DECAY EQ x AIR). A spectrally neutral
//     reverb fed with this source would have the shape of M; r = W / M (dB) is what the reverb itself adds.
//     Its broad shape (intended colour, Decay EQ, ER spectrum) is removed with a running median over half
//     an octave; what stays above it and PERSISTS (hundreds of ms) is a resonance. Musical harmonics are
//     part of the source, so they are in M and are not cut; the modulation moves the fine structure of
//     the tail, which never persists, so the motion is kept.
//   TAIL AWARE (experimental): persistence is weighted to the free decay (source stopped) and a growth
//     term is added: a band whose excess keeps growing rings longer than its expected RT(f). A band the
//     Decay EQ asked to ring longer is not flagged: M already decays with the expected RT(f).
//   LIVE        cascade of dynamic band cuts y = x + (g - 1) BP(x) on the 1/12-octave grid; only cutting
//               bands are computed (at most 24 / 32), zero latency, DELTA = x - y exactly.
//   STUDIO HQ   STFT (sqrt-Hann, 75 % overlap), gains per bin from a 1/24-octave grid; latency N + hop
//               (reported to the host), DELTA = delayed input - output.
//   The work of a hop (FFT stages, detection, synthesis) is spread over the hop in small jobs.
class ResonanceGuard
{
public:
    enum Mode { Smooth = 0, Deep = 1, TailAware = 2 };
    enum Target { EarlyAndLate = 0, LateOnly = 1, EarlyOnly = 2 };
    static constexpr int MAXB = 256, MAXJOBS = 128;

    struct Settings
    {
        bool on = false;
        int mode = Smooth, target = LateOnly, quality = 0;          // quality: 0 LIVE, 1 STUDIO HQ
        float smooth = 0.5f, detail = 0.5f, selectivity = 0.5f, attackMs = 40.0f, releaseMs = 250.0f;
        float maxRedDb = 4.0f, loHz = 150.0f, hiHz = 12000.0f, earlyAmt = 0.5f, lateAmt = 1.0f;
        bool delta = false, outputMatch = true;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        N = fs <= 50000.0 ? 4096 : fs <= 100000.0 ? 8192 : 16384;
        H = N / 4;
        R = 2 * N;                                         // rings hold N + H samples of history / pending output
        fft.prepare (N);
        auto z = [] (std::vector<float>& v, size_t sz) { v.assign (sz, 0.0f); };
        z (wetRing, (size_t) R); z (dryRing, (size_t) R);
        for (auto& b : fre) z (b, (size_t) N);
        for (auto& b : fim) z (b, (size_t) N);
        z (pw, (size_t) N / 2 + 1); z (pd, (size_t) N / 2 + 1); z (logBin, (size_t) N / 2 + 1); z (binGain[0], (size_t) N / 2 + 1); z (binGain[1], (size_t) N / 2 + 1);
        z (hann, (size_t) N); z (sqHann, (size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);
            hann[(size_t) i] = (float) w; sqHann[(size_t) i] = (float) std::sqrt (w);
        }
        for (int k = 1; k <= N / 2; ++k) logBin[(size_t) k] = (float) std::log2 (k * fs / N);
        logBin[0] = logBin[1] - 4.0f;
        for (auto& r : hqIn) z (r, (size_t) R);
        for (auto& r : hqOla) z (r, (size_t) R);
        setGrid (false);
        haveSettings = false;
        reset();
    }

    void reset()
    {
        std::fill (wetRing.begin(), wetRing.end(), 0.0f); std::fill (dryRing.begin(), dryRing.end(), 0.0f);
        for (auto& r : hqIn) std::fill (r.begin(), r.end(), 0.0f);
        for (auto& r : hqOla) std::fill (r.begin(), r.end(), 0.0f);
        pos = 0; hop = 0; nJobs = 0; jobIdx = 0; hqWarm = 0; hqMix = 0.0f; hqTarget = 0.0f; stftOn = false;
        for (int b = 0; b < MAXB; ++b)
        {
            Wt[b] = M[b] = 1e-20f; X[b] = 0.0f; slope[b] = 0.0f; xPrev[b] = 0.0f; cut[b] = 0.0f; xb[b] = 0.0f;
            xFast[b] = xSlow[b] = 0.0f; effBins[b] = 1.0f;
            for (int p = 0; p < 2; ++p) { g[p][b] = 1.0f; gInc[p][b] = 0.0f; rampEnd[p][b] = 1.0f; }
            for (auto& ch : bp) ch[b].clear();
            wasActive[b] = false;
            vizLevel[b].store (-120.0f, std::memory_order_relaxed); vizCut[b].store (0.0f, std::memory_order_relaxed);
        }
        nActive = 0; dPeak = 0.0f; dNow = 0.0f; makeup = 1.0f; makeupT = 1.0f; mkE = mkL = 1.0f;
        anyCut = false; active = false; pendingLand = false; freeDecay = false; silent = true;
        if (haveSettings) set (s, true);
    }

    void set (const Settings& in, bool force = false)
    {
        const bool hqWas = s.quality == 1;
        s = in;
        s.maxRedDb = std::clamp (s.maxRedDb, 0.0f, 12.0f);
        const double q = 8.0 + 16.0 * std::clamp ((double) s.detail, 0.0, 1.0);
        if (force || ! haveSettings || std::abs (q - curQ) > 1e-6 || (s.quality == 1) != hqWas)
        {
            if ((s.quality == 1) != gridHQ) setGrid (s.quality == 1);
            curQ = q;
            for (int b = 0; b < nb; ++b) for (auto& ch : bp) ch[b].set (fc[b], q, fs);
        }
        const double hopSec = (double) H / fs, tm = 0.5 + std::clamp ((double) s.smooth, 0.0, 1.0);
        aAtt = (float) (1.0 - std::exp (-hopSec / (std::clamp ((double) s.attackMs, 5.0, 500.0) * 0.001 * tm)));
        aRel = (float) (1.0 - std::exp (-hopSec / (std::clamp ((double) s.releaseMs, 20.0, 3000.0) * 0.001 * tm)));
        const double tp = s.mode == Deep ? 0.2 : s.mode == TailAware ? 0.6 : 0.4;
        aPers = (float) (1.0 - std::exp (-hopSec / tp));
        aW = (float) (1.0 - std::exp (-hopSec / 0.04));
        haveSettings = true;
    }

    // expected decay per band of the late engine (4 bands + crossovers), for the source memory
    void setExpectedRT (const double* rt4, double x1, double x2, double x3)
    {
        for (int b = 0; b < nb; ++b)
        {
            const double l = std::log2 (fc[b]);
            auto step = [l] (double x) { const double t = std::clamp (l - std::log2 (x) + 0.5, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); };
            const double rt = rt4[0] + (rt4[1] - rt4[0]) * step (x1) + (rt4[2] - rt4[1]) * step (x2) + (rt4[3] - rt4[2]) * step (x3);
            rtBand[b] = std::max (0.05, rt);
            decayR[b] = (float) std::exp (-13.8155 * (double) H / fs / rtBand[b]);
        }
    }

    int latencySamples() const { return s.quality == 1 ? N + H : 0; }
    int latencyHQ() const { return N + H; }
    // STUDIO HQ latency for a sample rate (FFT size N + hop N/4), known before prepare()
    static int hqLatencyFor (double sampleRate) { const int n = sampleRate <= 50000.0 ? 4096 : sampleRate <= 100000.0 ? 8192 : 16384; return n + n / 4; }
    int fftSize() const { return N; }

    // dry: the signal that feeds the reverb (source reference). Wet parts processed in place.
    // When delta buffers are given, they receive (input aligned to the output) - output.
    void process (const float* dryL, const float* dryR, float* erL, float* erR, float* ltL, float* ltR, int n,
                  float erGain, float lateGain, float* dEL, float* dER, float* dLL, float* dLR)
    {
        const bool hq = s.quality == 1;
        if (! hq && ! s.on && ! active)
        {
            if (dEL != nullptr) for (int i = 0; i < n; ++i) dEL[i] = dER[i] = dLL[i] = dLR[i] = 0.0f;
            return;
        }
        active = true;
        doEarly = s.target != LateOnly && s.earlyAmt > 0.0f;
        doLate = s.target != EarlyOnly && s.lateAmt > 0.0f;
        const int mask = R - 1;
        for (int i = 0; i < n; ++i)
        {
            const size_t w = (size_t) pos;
            float hqOut[4] = { 0, 0, 0, 0 }, dly[4] = { 0, 0, 0, 0 };
            if (hq)
            {
                // time t - (N + H): delayed input and the completed STFT output
                const size_t rd = (size_t) ((pos - N - H) & mask);
                for (int c = 0; c < 4; ++c) { dly[c] = hqIn[(size_t) c][rd]; hqOut[c] = hqOla[(size_t) c][rd]; hqOla[(size_t) c][rd] = 0.0f; }
                hqIn[0][w] = erL[i]; hqIn[1][w] = erR[i]; hqIn[2][w] = ltL[i]; hqIn[3][w] = ltR[i];
            }
            wetRing[w] = 0.5f * (erGain * (erL[i] + erR[i]) + lateGain * (ltL[i] + ltR[i]));
            dryRing[w] = 0.5f * (dryL[i] + dryR[i]);
            pos = (pos + 1) & mask;
            if (++hop >= H) { hop = 0; startHop (hq); }
            while (jobIdx < nJobs && hop >= jobAt[jobIdx]) runJob (jobIdx++, hq);

            if (hq)
            {
                const float xm = hqMix;
                const float yEL = dly[0] + xm * (hqOut[0] - dly[0]), yER = dly[1] + xm * (hqOut[1] - dly[1]);
                const float yLL = dly[2] + xm * (hqOut[2] - dly[2]), yLR = dly[3] + xm * (hqOut[3] - dly[3]);
                if (dEL != nullptr) { dEL[i] = dly[0] - yEL; dER[i] = dly[1] - yER; dLL[i] = dly[2] - yLL; dLR[i] = dly[3] - yLR; }
                erL[i] = yEL * mkE; erR[i] = yER * mkE; ltL[i] = yLL * mkL; ltR[i] = yLR * mkL;
                hqMix = hqTarget > hqMix ? std::min (hqTarget, hqMix + hqStep) : std::max (hqTarget, hqMix - hqStep);
            }
            else
            {
                float el = erL[i], er = erR[i], ll = ltL[i], lr = ltR[i];
                const float xel = el, xer = er, xll = ll, xlr = lr;
                for (int a = 0; a < nActive; ++a)
                {
                    const int b = activeList[a];
                    g[0][b] += gInc[0][b]; g[1][b] += gInc[1][b];
                    if (doEarly) { el += (g[0][b] - 1.0f) * bp[0][b].process (el); er += (g[0][b] - 1.0f) * bp[1][b].process (er); }
                    if (doLate)  { ll += (g[1][b] - 1.0f) * bp[2][b].process (ll); lr += (g[1][b] - 1.0f) * bp[3][b].process (lr); }
                }
                if (dEL != nullptr) { dEL[i] = xel - el; dER[i] = xer - er; dLL[i] = xll - ll; dLR[i] = xlr - lr; }
                erL[i] = el * mkE; erR[i] = er * mkE; ltL[i] = ll * mkL; ltR[i] = lr * mkL;
            }
        }
        if (! hq && ! s.on && ! anyCut && nActive == 0 && makeup == 1.0f) active = false;
    }

    // ---- visualization (written by the audio thread once per hop, read by the editor)
    int numBands() const { return nb; }
    double bandHz (int b) const { return fc[std::clamp (b, 0, nb - 1)]; }
    float vizLevelDb (int b) const { return vizLevel[std::clamp (b, 0, MAXB - 1)].load (std::memory_order_relaxed); }
    float vizCutDb (int b) const { return vizCut[std::clamp (b, 0, MAXB - 1)].load (std::memory_order_relaxed); }
    float maxCutDb() const { float m = 0.0f; for (int b = 0; b < nb; ++b) m = std::max (m, vizCut[b].load (std::memory_order_relaxed)); return m; }
    float makeupDb() const { return 20.0f * std::log10 (std::max (1e-6f, makeup)); }
    bool  isProcessing() const { return anyCut; }

private:
    enum JobKind { JCapture, JBitRev, JPass, JBands, JDetect, JRamps, JBinGains, JGain, JOla };
    struct Job { int kind, buf, arg; };

    void addJob (int kind, int buf = 0, int arg = 0) { if (nJobs < MAXJOBS) jobs[nJobs++] = { kind, buf, arg }; }

    // at every hop boundary: capture the frames, then schedule the work of the hop over ~90 % of it
    void startHop (bool hq)
    {
        nJobs = 0; jobIdx = 0;
        captured = pos;                                    // the frames of this hop end here (the rings keep them)
        const bool analyseNow = s.on || anyCut;
        if (analyseNow)
        {
            for (int q = 0; q < 4; ++q) addJob (JCapture, 0, q);
            addJob (JBitRev, 0);
            for (int st = 1; st <= fft.logN; ++st) addJob (JPass, 0, st);
            addJob (JBands); addJob (JDetect);
        }
        else decayToZero();
        if (! hq) addJob (JRamps);
        else
        {
            const bool proc = s.on || anyCut;
            if (proc && hqWarm < N + 2 * H) hqWarm += H;
            hqTarget = (proc && hqWarm > N + H) ? 1.0f : 0.0f;
            hqStep = 1.0f / (0.01f * (float) fs);
            stftOn = proc || hqMix > 0.0f;
            if (! stftOn) hqWarm = 0;
            else
            {
                addJob (JBinGains);
                for (int part = 0; part < 2; ++part)
                {
                    const int b = 1 + part;
                    for (int q = 0; q < 4; ++q) addJob (JCapture, b, q);
                    addJob (JBitRev, b);
                    for (int st = 1; st <= fft.logN; ++st) addJob (JPass, b, st);
                    addJob (JGain, b, part);
                    addJob (JBitRev, b);
                    for (int st = 1; st <= fft.logN; ++st) addJob (JPass, b, -st);   // negative: inverse
                    addJob (JOla, b, part);
                }
            }
        }
        const int span = std::max (1, (int) (0.9 * H));
        for (int j = 0; j < nJobs; ++j) jobAt[j] = std::min (H - 1, (j * span) / std::max (1, nJobs));
    }

    void runJob (int j, bool)
    {
        const Job& jb = jobs[j];
        float* re = fre[(size_t) jb.buf].data(); float* im = fim[(size_t) jb.buf].data();
        switch (jb.kind)
        {
            case JCapture:
            {
                // a quarter of the frame [captured - N, captured): analysis = wet (real) + source (imaginary),
                // HQ parts = left (real) + right (imaginary)
                const int mask = R - 1, i0 = jb.arg * (N / 4), i1 = i0 + N / 4;
                if (jb.buf == 0)
                    for (int i = i0; i < i1; ++i) { const size_t k = (size_t) ((captured - N + i) & mask); re[i] = wetRing[k] * hann[(size_t) i]; im[i] = dryRing[k] * hann[(size_t) i]; }
                else
                {
                    const auto& inL = hqIn[(size_t) (2 * (jb.buf - 1))]; const auto& inR = hqIn[(size_t) (2 * (jb.buf - 1) + 1)];
                    for (int i = i0; i < i1; ++i) { const size_t k = (size_t) ((captured - N + i) & mask); re[i] = inL[k] * sqHann[(size_t) i]; im[i] = inR[k] * sqHann[(size_t) i]; }
                }
                break;
            }
            case JBitRev:   fft.bitReverse (re, im); break;
            case JPass:     fft.pass (re, im, std::abs (jb.arg), jb.arg < 0); break;
            case JBands:    bands(); break;
            case JDetect:   detect(); break;
            case JRamps:    prepareRamps(); break;
            case JBinGains: binGains(); break;
            case JGain:
            {
                const auto& gk = binGain[(size_t) jb.arg];
                const bool doIt = jb.arg == 0 ? doEarly : doLate;
                if (doIt)
                    for (int k = 0; k < N; ++k) { const float gg = gk[(size_t) (k <= N / 2 ? k : N - k)]; re[k] *= gg; im[k] *= gg; }
                break;
            }
            case JOla:
            {
                // the frame covers input times [captured - N, captured): add into the output ring there
                const int mask = R - 1;
                const float norm = 0.5f / (float) N;             // 1/N of the inverse FFT, sqrt-Hann^2 at 75 % overlap sums to 2
                auto& oL = hqOla[(size_t) (2 * jb.arg)]; auto& oR = hqOla[(size_t) (2 * jb.arg + 1)];
                for (int i = 0; i < N; ++i)
                {
                    const size_t o = (size_t) ((captured - N + i) & mask);
                    oL[o] += re[i] * sqHann[(size_t) i] * norm; oR[o] += im[i] * sqHann[(size_t) i] * norm;
                }
                break;
            }
            default: break;
        }
    }

    void setGrid (bool hq)
    {
        gridHQ = hq;
        const double step = hq ? 1.0 / 24.0 : 1.0 / 12.0, fTop = std::min (18000.0, 0.45 * fs);
        nb = 0;
        for (double l = std::log2 (40.0); l <= std::log2 (fTop) && nb < MAXB; l += step) fc[nb++] = std::pow (2.0, l);
        gridStep = (float) step;
        for (int b = 0; b < nb; ++b)
        {
            const double lo = fc[b] * std::pow (2.0, -step), hi = fc[b] * std::pow (2.0, step);
            kLo[b] = std::max (1, (int) std::floor (lo * N / fs)); kHi[b] = std::min (N / 2, (int) std::ceil (hi * N / fs));
            lfc[b] = (float) std::log2 (fc[b]);
            Wt[b] = M[b] = 1e-20f; X[b] = 0.0f; slope[b] = 0.0f; xPrev[b] = 0.0f; cut[b] = 0.0f;
            g[0][b] = g[1][b] = 1.0f; gInc[0][b] = gInc[1][b] = 0.0f; rampEnd[0][b] = rampEnd[1][b] = 1.0f;
        }
        nActive = 0;
        double rt4[4] = { 2, 2, 2, 2 };
        setExpectedRT (rt4, 250, 2000, 7000);
    }

    void decayToZero()
    {
        bool any = false;
        for (int b = 0; b < nb; ++b) { cut[b] += aRel * (0.0f - cut[b]); if (cut[b] < 0.01f) cut[b] = 0.0f; any = any || cut[b] > 0.0f; X[b] = 0.0f; }
        anyCut = any;
        makeupT = 1.0f; makeup += 0.2f * (makeupT - makeup); if (std::abs (makeup - 1.0f) < 1e-4f) makeup = 1.0f;
        mkE = mkL = makeup;
        publish();
    }

    // spectra of the wet (real part) and the source (imaginary part), band energies, source memory
    void bands()
    {
        const float* re = fre[0].data(); const float* im = fim[0].data();
        for (int k = 1; k <= N / 2; ++k)
        {
            const size_t a = (size_t) k, c = (size_t) ((N - k) & (N - 1));
            const float wr = 0.5f * (re[a] + re[c]), wi = 0.5f * (im[a] - im[c]);
            const float dr = 0.5f * (im[a] + im[c]), di = -0.5f * (re[a] - re[c]);
            pw[a] = wr * wr + wi * wi; pd[a] = dr * dr + di * di;
        }
        float wTot = 0.0f, dTot = 0.0f;
        for (int b = 0; b < nb; ++b)
        {
            float sw = 0.0f, sd = 0.0f, sn = 0.0f;
            for (int k = kLo[b]; k <= kHi[b]; ++k)
            {
                const float w = std::max (0.0f, 1.0f - std::abs (logBin[(size_t) k] - lfc[b]) / gridStep);
                sw += w * pw[(size_t) k]; sd += w * pd[(size_t) k]; sn += w;
            }
            if (sn < 1e-6f) { const int k = std::clamp ((int) std::lround (fc[b] * N / fs), 1, N / 2); sw = pw[(size_t) k]; sd = pd[(size_t) k]; sn = 1.0f; }
            sw /= sn; sd /= sn;
            effBins[b] = std::max (1.0f, sn);
            Wt[b] += aW * (sw - Wt[b]);
            M[b] = M[b] * decayR[b] + sd;
            wTot += Wt[b]; dTot += sd;
        }
        // source activity: free decay when the source is 30 dB below its recent peak
        dNow += 0.5f * (dTot - dNow);
        dPeak = std::max (dNow, dPeak * 0.995f);
        freeDecay = dNow < 1e-3f * dPeak;
        silent = wTot < 1e-14f;
        float mTot = 0.0f; for (int b = 0; b < nb; ++b) mTot += M[b];
        for (int b = 0; b < nb; ++b) rb[b] = powerToDb (Wt[b] + 1e-24f) - powerToDb (M[b] + 1e-9f * mTot + 1e-24f);
        // broad shape removed: running median over half an octave
        const int S = gridHQ ? 12 : 6;
        float tmp[2 * 12 + 1];
        for (int b = 0; b < nb; ++b)
        {
            int m = 0;
            for (int j = std::max (0, b - S); j <= std::min (nb - 1, b + S); ++j) tmp[m++] = rb[j];
            std::nth_element (tmp, tmp + m / 2, tmp + m);
            xb[b] = rb[b] - tmp[m / 2];
        }
    }

    // persistence, tail-aware growth, cut targets, ballistics, output match
    void detect()
    {
        const float sel = std::clamp (s.selectivity, 0.0f, 1.0f);
        const float T = s.mode == Deep ? 2.0f + 5.0f * sel : s.mode == TailAware ? 2.5f + 5.0f * sel : 3.0f + 6.0f * sel;
        const float km = s.mode == Deep ? 0.9f : s.mode == TailAware ? 0.8f : 0.5f;
        const float hopSec = (float) H / (float) fs;
        const float lo = (float) std::log2 (std::max (20.0f, s.loHz)), hi = (float) std::log2 (std::max (s.loHz * 1.5f, s.hiHz));
        float tgt[MAXB];
        const float aF = 1.0f - std::exp (-hopSec / 0.25f), aS = 1.0f - std::exp (-hopSec / 1.0f);
        for (int b = 0; b < nb; ++b)
        {
            // SMOOTH / DEEP judge the field while the source plays (in the free decay the reference has
            // nothing left to say, the excess is frozen); TAIL AWARE judges mainly the free decay
            const float ap = s.mode == TailAware ? (freeDecay ? aPers : 0.25f * aPers) : (freeDecay ? 0.0f : aPers);
            if (! silent) X[b] += ap * (xb[b] - X[b]);
            float grow = 0.0f;
            if (s.mode == TailAware && freeDecay && ! silent)
            {
                // growth of the excess during the free decay: two averages 0.25 s / 1 s apart, their
                // difference / 0.75 s is the slope of a steady ramp, without the frame-to-frame noise
                xFast[b] += aF * (xb[b] - xFast[b]); xSlow[b] += aS * (xb[b] - xSlow[b]);
                slope[b] = (xFast[b] - xSlow[b]) / 0.75f;
                grow = std::max (0.0f, slope[b] - 1.5f) * 0.5f;          // dB/s above tolerance x 0.5 s
            }
            else { xFast[b] = xSlow[b] = X[b]; slope[b] = 0.0f; }
            // few FFT bins in a band (low frequencies) = noisier estimate: ask for more there
            const float Tb = T + 4.0f / std::sqrt (effBins[b]) - 1.3f;
            const float inRange = std::clamp (std::min (lfc[b] - lo, hi - lfc[b]) * 3.0f + 1.0f, 0.0f, 1.0f);   // 1/3-octave taper
            tgt[b] = s.on ? std::clamp (km * (X[b] + grow - Tb), 0.0f, s.maxRedDb) * inRange : 0.0f;
        }
        // spread over neighbours (SMOOTH), at most K cutting bands in LIVE
        const float nu = 0.3f + 0.6f * std::clamp (s.smooth, 0.0f, 1.0f);
        const int Rr = gridHQ ? 4 : 2;
        float sp[MAXB];
        for (int b = 0; b < nb; ++b)
        {
            float v = tgt[b], f = 1.0f;
            for (int j = 1; j <= Rr; ++j) { f *= nu; v = std::max ({ v, f * tgt[std::max (0, b - j)], f * tgt[std::min (nb - 1, b + j)] }); }
            sp[b] = v;
        }
        if (! gridHQ)
        {
            const int K = s.mode == Smooth ? 24 : 32;
            int cnt = 0; for (int b = 0; b < nb; ++b) cnt += sp[b] > 0.0f;
            if (cnt > K)
            {
                float tv[MAXB]; int m = 0;
                for (int b = 0; b < nb; ++b) if (sp[b] > 0.0f) tv[m++] = sp[b];
                std::nth_element (tv, tv + (m - K), tv + m);
                const float thr = tv[m - K];
                for (int b = 0; b < nb; ++b) if (sp[b] < thr) sp[b] = 0.0f;
            }
        }
        bool any = false; float lost = 0.0f, tot = 0.0f;
        for (int b = 0; b < nb; ++b)
        {
            cut[b] += (sp[b] > cut[b] ? aAtt : aRel) * (sp[b] - cut[b]);
            if (cut[b] < 0.01f && sp[b] <= 0.0f) cut[b] = 0.0f;
            any = any || cut[b] > 0.0f;
            const float gl = fastDbToGain (-cut[b] * s.lateAmt);
            lost += Wt[b] * (1.0f - gl * gl); tot += Wt[b];
        }
        anyCut = any;
        makeupT = s.outputMatch && tot > 1e-20f ? 1.0f / std::sqrt (std::max (0.25f, 1.0f - lost / tot)) : 1.0f;
        makeup += 0.05f * (makeupT - makeup);
        if (std::abs (makeup - 1.0f) < 1e-4f && makeupT == 1.0f) makeup = 1.0f;
        mkE = doEarly ? makeup : 1.0f;
        mkL = doLate ? makeup : 1.0f;
        publish();
    }

    void publish()
    {
        for (int b = 0; b < nb; ++b)
        {
            vizLevel[b].store (powerToDb (Wt[b] + 1e-24f), std::memory_order_relaxed);
            vizCut[b].store (cut[b], std::memory_order_relaxed);
        }
    }

    // LIVE: per-band gain ramps over the next hop; the active list holds the bands that cut or ramp
    void prepareRamps()
    {
        for (int a = 0; a < nActive; ++a) { const int b = activeList[a]; g[0][b] = rampEnd[0][b]; g[1][b] = rampEnd[1][b]; gInc[0][b] = gInc[1][b] = 0.0f; }
        nActive = 0;
        for (int b = 0; b < nb; ++b)
        {
            const float tE = doEarly ? fastDbToGain (-cut[b] * s.earlyAmt) : 1.0f;
            const float tL = doLate ? fastDbToGain (-cut[b] * s.lateAmt) : 1.0f;
            const bool idle = tE == 1.0f && tL == 1.0f && g[0][b] == 1.0f && g[1][b] == 1.0f;
            if (idle) { if (wasActive[b]) { for (auto& ch : bp) ch[b].clear(); wasActive[b] = false; } continue; }
            // a section that wakes up starts from a clear state with g = 1: no transient
            gInc[0][b] = (tE - g[0][b]) / H; gInc[1][b] = (tL - g[1][b]) / H;
            rampEnd[0][b] = tE; rampEnd[1][b] = tL;
            wasActive[b] = true;
            activeList[nActive++] = b;
        }
    }

    // STUDIO HQ: per-bin gains from the grid (log-frequency interpolation), early and late
    void binGains()
    {
        for (int k = 0; k <= N / 2; ++k)
        {
            const float p = (logBin[(size_t) k] - lfc[0]) / gridStep;
            const int b0 = std::clamp ((int) std::floor (p), 0, nb - 1), b1 = std::min (nb - 1, b0 + 1);
            const float t = std::clamp (p - (float) b0, 0.0f, 1.0f);
            const float c = p < 0.0f ? cut[0] : cut[b0] + t * (cut[b1] - cut[b0]);
            binGain[0][(size_t) k] = fastDbToGain (-c * s.earlyAmt);
            binGain[1][(size_t) k] = fastDbToGain (-c * s.lateAmt);
        }
    }

    double fs = 48000.0;
    int N = 4096, H = 1024, R = 8192, nb = 0, pos = 0, hop = 0, hqWarm = 0, nActive = 0, captured = 0;
    int nJobs = 0, jobIdx = 0;
    Job jobs[MAXJOBS] {};
    int jobAt[MAXJOBS] {};
    float gridStep = 1.0f / 12.0f;
    bool gridHQ = false, haveSettings = false, anyCut = false, active = false, pendingLand = false, stftOn = false;
    bool doEarly = false, doLate = true, freeDecay = false, silent = true;
    double curQ = -1.0;
    Settings s;
    Fft fft;
    std::vector<float> wetRing, dryRing, pw, pd, logBin, hann, sqHann;
    std::array<std::vector<float>, 3> fre, fim;            // 0 analysis, 1 early (HQ), 2 late (HQ)
    std::array<std::vector<float>, 2> binGain;
    std::array<std::vector<float>, 4> hqIn, hqOla;
    double fc[MAXB] {}, rtBand[MAXB] {};
    float lfc[MAXB] {};
    int kLo[MAXB] {}, kHi[MAXB] {}, activeList[MAXB] {};
    bool wasActive[MAXB] {};
    float Wt[MAXB] {}, M[MAXB] {}, X[MAXB] {}, slope[MAXB] {}, xPrev[MAXB] {}, cut[MAXB] {}, decayR[MAXB] {}, rb[MAXB] {}, xb[MAXB] {};
    float xFast[MAXB] {}, xSlow[MAXB] {}, effBins[MAXB] {};
    float g[2][MAXB] {}, gInc[2][MAXB] {}, rampEnd[2][MAXB] {};
    BandPass bp[4][MAXB];
    float aAtt = 0.1f, aRel = 0.05f, aPers = 0.05f, aW = 0.3f;
    float dPeak = 0.0f, dNow = 0.0f, makeup = 1.0f, makeupT = 1.0f, mkE = 1.0f, mkL = 1.0f;
    float hqMix = 0.0f, hqTarget = 0.0f, hqStep = 0.002f;
    std::atomic<float> vizLevel[MAXB], vizCut[MAXB];
};

} // namespace ri
