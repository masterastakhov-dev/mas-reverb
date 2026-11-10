// MASTER SPACE — spatial engine
// (c) masterastakhovsound            "Don't add reverb. Create space."
//
//   * Spatial DNA (early reflections): image-source model of a shoebox room with
//     real dimensions, source and listener positions and a material per surface.
//     Every reflection gets its own delay, direction, level and spectrum from the
//     geometry (surface absorption per band, air absorption by path length).
//   * Depth: one control moves the source away from the listener; geometry,
//     direct/reflected ratio, spectrum and spatial spread follow. Level / Tone /
//     Decay / Width locks keep chosen properties constant.
//   * Late reverb: 16-line feedback delay network (Hadamard), input diffusion,
//     in-loop density all-passes, Shape/Spread build-up, 4-band RT60 via shelves
//     inside the loop, smooth/random modulation. Size follows the room volume.
// Zero latency, plain C++ (no JUCE) so it can be tested offline.

#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mspace
{

static constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// sin (2 pi phase), phase in [0, 1): parabolic approximation with one correction step (error < 0.001),
// enough for an LFO and much cheaper than std::sin for 16 lines per sample
inline float fastSin2Pi (double phase) noexcept
{
    const float q = (float) (phase - std::floor (phase + 0.5));     // [-0.5, 0.5)
    const float y = 8.0f * q - 16.0f * q * std::abs (q);
    return 0.225f * (y * std::abs (y) - y) + y;
}

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
    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    int capacity() const { return mask - 4; }

    inline float read (int d) const noexcept { return buffer[(size_t) ((writePos - d) & mask)]; }

    inline float readLinear (float d) const noexcept
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = read (i), b = read (i + 1);
        return a + f * (b - a);
    }

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

// Schroeder all-pass with a fractional, possibly modulated delay.
// The delay is read with cubic (Hermite) interpolation: with linear interpolation the feedback and
// feed-forward paths no longer match at high frequencies, the section stops being an all-pass and
// becomes a comb with peaks every 1/delay (about 2.5 dB of ripple at 8 kHz for g = 0.43).
// In a feedback network that ripple accumulates every pass and is heard as metallic ringing.
class Allpass
{
public:
    void allocate (int maxD) { line.allocate (maxD); }
    void clear() { line.clear(); }
    // a: gain of the internal delay ("absorbent" all-pass, Jot). Inside a decaying feedback network
    // the all-pass must lose energy at the same rate per second as the delay lines; otherwise the
    // energy that circulates in its own loop decays slower and the poles leave the common radius
    // (narrow bands that ring several times longer than the set decay).
    inline float process (float x, float d, float g, float a = 1.0f) noexcept
    {
        const float wd = a * line.readHermite (std::max (2.0f, d));
        const float w = x + g * wd;
        line.push (w);
        return wd - g * w;
    }
    // static delay in whole samples (exact all-pass, no interpolation)
    inline float process (float x, int d, float g) noexcept
    {
        const float wd = line.read (std::max (1, d));
        const float w = x + g * wd;
        line.push (w);
        return wd - g * w;
    }
private:
    DelayLine line;
};

struct OnePoleLP
{
    float z = 0.0f, a = 1.0f;
    void setCutoff (double hz, double fs) { a = (float) (1.0 - std::exp (-2.0 * kPi * std::min (hz, 0.49 * fs) / fs)); }
    inline float process (float x) noexcept { z += a * (x - z); return z; }
    void clear() { z = 0.0f; }
};

// RBJ biquad, high- or low-pass (Butterworth Q)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void setHighPass (double hz, double fs, double q = 0.70710678) { set (hz, fs, true, q); }
    void setLowPass (double hz, double fs, double q = 0.70710678)  { set (hz, fs, false, q); }

    void set (double hz, double fs, bool highPass, double q = 0.70710678)
    {
        hz = std::clamp (hz, 5.0, 0.45 * fs);
        const double w0 = 2.0 * kPi * hz / fs, c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
        if (highPass) { b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); }
        else          { b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); }
        b2 = b0;
        a1 = (float) (-2.0 * c / a0);
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

// RBJ shelving biquad (slope S = 1: monotonic, no overshoot). gain = linear magnitude of the shelf.
struct Shelf
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set (double hz, double gain, double fs, bool high)
    {
        hz = std::clamp (hz, 10.0, 0.45 * fs);
        const double A = std::sqrt (std::max (gain, 1e-6));
        const double w0 = 2.0 * kPi * hz / fs, c = std::cos (w0);
        const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
        const double sa = 2.0 * std::sqrt (A) * alpha;
        double B0, B1, B2, A0, A1, A2;
        if (! high)
        {
            B0 = A * ((A + 1) - (A - 1) * c + sa);  B1 = 2 * A * ((A - 1) - (A + 1) * c);  B2 = A * ((A + 1) - (A - 1) * c - sa);
            A0 = (A + 1) + (A - 1) * c + sa;        A1 = -2 * ((A - 1) + (A + 1) * c);     A2 = (A + 1) + (A - 1) * c - sa;
        }
        else
        {
            B0 = A * ((A + 1) + (A - 1) * c + sa);  B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sa);
            A0 = (A + 1) - (A - 1) * c + sa;        A1 = 2 * ((A - 1) - (A + 1) * c);      A2 = (A + 1) - (A - 1) * c - sa;
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void clear() { x1 = x2 = y1 = y2 = 0; }
};

// Tilt EQ around ~900 Hz: t in [-1, 1] -> about +-6 dB at the extremes
struct Tilt
{
    OnePoleLP lp;
    float gLow = 1.0f, gHigh = 1.0f;
    void prepare (double fs) { lp.setCutoff (900.0, fs); }
    void set (float t)
    {
        t = std::clamp (t, -1.0f, 1.0f);
        gLow  = std::pow (10.0f, (-6.0f * t) / 20.0f);
        gHigh = std::pow (10.0f, ( 6.0f * t) / 20.0f);
    }
    inline float process (float x) noexcept { const float l = lp.process (x); return l * gLow + (x - l) * gHigh; }
    void clear() { lp.clear(); }
};

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1) {}
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    inline float bi() noexcept  { return uni() * 2.0f - 1.0f; }                            // -1..1
};

//==============================================================================
enum Algorithm    { StudioRoom = 0, Chamber, Hall, numAlgorithms };
enum ModCharacter { ModSmooth = 0, ModRandom, ModVintage, ModModern };
enum Material     { Absorber = 0, Curtain, Wood, Plaster, Concrete, Glass, numMaterials };
enum Surface      { Front = 0, Back, Left, Right, Floor, Ceiling, numSurfaces };
enum Routing      { Insert = 0, Aux };

struct MaterialData { float rLow, rHigh; };   // pressure reflection factor below / above ~1.5 kHz

inline MaterialData materialData (int m)
{
    // 1.2: pressure reflection factors sqrt(1 - alpha) from standard absorption tables (low: 125 Hz-1 kHz,
    // high: 2-4 kHz). 1.1 used far lower high-band values (plaster 0.82, wood 0.62), which made every
    // higher-order reflection several dB darker than a real room and gave the early field a low-mid weight.
    static constexpr MaterialData table[numMaterials] = {
        { 0.45f, 0.20f },   // absorber (porous)
        { 0.75f, 0.55f },   // curtain / heavy textile
        { 0.90f, 0.95f },   // wood (floor / panelling)
        { 0.95f, 0.98f },   // plaster
        { 0.99f, 0.985f },  // concrete / stone
        { 0.88f, 0.97f },   // glass (panel absorbs lows, reflects highs)
    };
    return table[std::clamp (m, 0, (int) numMaterials - 1)];
}

static constexpr double kMaxRoomScale = 2.2;     // late network size limit
static constexpr double kRefVolume    = 300.0;   // m^3, room with late scale 1.0
static constexpr float  kRefDist      = 1.5f;    // m, direct gain 1.0
static constexpr float  kDepthRef     = 0.35f;   // Level / Width lock reference depth
static constexpr float  kAirDbPerM    = 0.08f;   // high-band air absorption
static constexpr float  kErCal        = 1.146f;  // early level calibration (1.2: same loudness as 1.1 with the new materials)

struct Params
{
    int   algorithm   = Chamber;
    int   routing     = Insert;
    float depth       = 0.35f;   // 0 near .. 1 far
    float decay       = 1.8f;    // RT60 (s), 0.1 .. 30
    float size        = 0.5f;    // room scale: 0 -> 0.5x, 0.5 -> 1x, 1 -> 2x
    float preDelayMs  = 0.0f;    // 0 .. 500
    float erLevelDb   = 0.0f;    // -60 (off) .. +12
    float lateLevelDb = 0.0f;
    bool  erOn = true, lateOn = true;
    float diffusion   = 0.7f;    // late
    float density     = 0.7f;    // late
    float shape       = 0.35f;
    float spreadMs    = 60.0f;
    float width       = 1.0f;    // 0 .. 2
    float mix         = 0.35f;   // insert mode
    float outputDb    = 0.0f;

    // Spatial DNA
    float roomW = 7.5f, roomD = 10.0f, roomH = 4.0f;   // metres at size 50 %
    float srcX = 0.5f;                                 // 0 left .. 1 right
    float lisX = 0.5f, lisY = 0.72f;                   // fractions of width / depth (y = 0 is the front wall)
    std::array<int, numSurfaces> materials { Plaster, Wood, Plaster, Plaster, Wood, Plaster };
    float erDensity   = 0.6f;    // reflection order / count
    float erDiffusion = 0.4f;
    float erColor     = 0.0f;
    float erWidth     = 1.0f;
    float erStartMs   = 0.0f;
    float erStopMs    = 150.0f;
    float erPreDelayMs = 0.0f;

    // late
    float lateDelayMs = 20.0f, lateWidth = 1.0f, lateColor = 0.0f;

    // decay EQ / filters
    float lfMult = 1.2f, lmMult = 1.0f, hmMult = 0.85f, hfMult = 0.55f;
    float xLow = 200.0f, xMid = 1500.0f, xHigh = 6000.0f;
    float lowCutHz = 20.0f, highCutHz = 20000.0f;

    // modulation
    float modRate = 0.6f, modDepth = 0.35f, modRandom = 0.5f, modSmooth = 0.6f;
    int   modCharacter = ModSmooth;

    // stereo
    float balance = 0.0f, crossfeed = 0.2f, decorrelation = 0.85f;

    // locks
    bool lockLevel = true, lockTone = false, lockDecay = true, lockWidth = false;
};

inline double sizeScale (float size01) { return 0.5 * std::pow (4.0, (double) std::clamp (size01, 0.0f, 1.0f)); }

struct Geometry
{
    float W, D, H;          // effective room dimensions (m)
    float lx, ly, lz;       // listener
    float sx, sy, sz;       // source
    float direct;           // source-listener distance (m)
};

inline Geometry computeGeometry (const Params& p, float depth)
{
    const float s = (float) sizeScale (p.size);
    Geometry g;
    g.W = std::clamp (p.roomW, 2.0f, 40.0f) * s;
    g.D = std::clamp (p.roomD, 2.0f, 60.0f) * s;
    g.H = std::clamp (p.roomH, 2.0f, 20.0f) * s;
    g.lx = std::clamp (p.lisX, 0.05f, 0.95f) * g.W;
    g.ly = std::clamp (p.lisY, 0.10f, 0.95f) * g.D;
    g.lz = std::min (1.2f, 0.45f * g.H);
    const float avail = std::max (0.3f, g.ly - 0.4f);           // keep 0.4 m from the front wall
    const float dmin = std::min (0.6f, avail);
    const float dist = dmin + (avail - dmin) * std::pow (std::clamp (depth, 0.0f, 1.0f), 1.15f);
    g.sx = std::clamp (p.srcX, 0.05f, 0.95f) * g.W;
    g.sy = g.ly - dist;
    g.sz = std::min (1.6f, 0.6f * g.H);
    const float dx = g.sx - g.lx, dy = g.sy - g.ly, dz = g.sz - g.lz;
    g.direct = std::max (0.3f, std::sqrt (dx * dx + dy * dy + dz * dz));
    return g;
}

inline float directGain (float dist) { return std::min (1.6f, kRefDist / dist); }

//==============================================================================
// Spatial DNA: early reflections from the virtual room.
class SpatialEarly
{
public:
    static constexpr int kMaxTaps = 48;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : input) l.allocate ((int) std::ceil (0.64 * fs));
        for (int c = 0; c < 2; ++c)
        {
            color[c].prepare (fs);
            lowBus[c].setLowPass (1500.0, fs);
            scatterBus[c].setHighPass (1500.0, fs);
        }
        reset();
    }

    void reset()
    {
        for (auto& l : input) l.clear();
        for (int c = 0; c < 2; ++c) { color[c].clear(); lowBus[c].clear(); scatterBus[c].clear(); }
        haveSet = false; fading = false;
    }

    // Recomputes the reflection pattern when anything spatial changed (cross-faded, ~40 ms).
    void update (const Params& p)
    {
        color[0].set (p.erColor); color[1].set (p.erColor);

        Key k;
        k.v = { p.roomW, p.roomD, p.roomH, p.size, p.srcX, p.lisX, p.lisY, p.depth, p.erDensity,
                p.erStartMs, p.erStopMs, p.erPreDelayMs, p.lockTone ? 1.0f : 0.0f, p.erDiffusion };
        for (int s = 0; s < numSurfaces; ++s) k.v[(size_t) (14 + s)] = (float) p.materials[(size_t) s];

        if (haveSet && (fading || k == key))
            return;

        TapSet& target = haveSet ? sets[1 - active] : sets[active];
        build (p, p.depth, target);
        build (p, kDepthRef, refSet);           // reference for the Level / Width locks
        key = k;

        if (! haveSet) { haveSet = true; return; }
        fading = true;
        fadePos = 0.0f;
    }

    // energy (sum of squared gains, both channels) and pan spread of the current / reference patterns
    float currentEnergy() const   { return sets[fading ? 1 - active : active].energy; }
    float referenceEnergy() const { return refSet.energy; }
    float currentSpread() const   { return sets[fading ? 1 - active : active].spread; }
    float referenceSpread() const { return refSet.spread; }
    float currentLow() const      { return sets[fading ? 1 - active : active].energyLow; }
    float currentHigh() const     { return sets[fading ? 1 - active : active].energyHigh; }
    float referenceLow() const    { return refSet.energyLow; }
    float referenceHigh() const   { return refSet.energyHigh; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const float fadeStep = (float) (1.0 / (0.04 * fs));

        for (int i = 0; i < n; ++i)
        {
            input[0].push (inL[i]);
            input[1].push (inR[i]);

            float fL = 0, fR = 0, lL = 0, lR = 0, hL = 0, hR = 0;
            accumulate (sets[active], fading ? 1.0f - fadePos : 1.0f, fL, fR, lL, lR, hL, hR);
            if (fading)
            {
                accumulate (sets[1 - active], fadePos, fL, fR, lL, lR, hL, hR);
                fadePos += fadeStep;
                if (fadePos >= 1.0f) { fading = false; active = 1 - active; fadePos = 0.0f; }
            }

            // no recursive diffusion here: short all-passes on the early pattern ring at the harmonics
            // of their delay (heard as a metallic tone right on the direct sound). Diffusion is done
            // by the scattering clusters built in build().
            // scatter bus: Butterworth high-pass, power complementary to the low bus split
            const float yl = fL + lowBus[0].process (lL) + scatterBus[0].process (hL);
            const float yr = fR + lowBus[1].process (lR) + scatterBus[1].process (hR);
            outL[i] = color[0].process (yl);
            outR[i] = color[1].process (yr);
        }
    }

    // first-order reflection points for the UI (x, y in metres, surface index)
    struct Point { float x, y; int surface; };

private:
    static constexpr int kSide = 6;       // scattered arrivals per reflection and channel (half before, half after)
    struct MainTap { int delay = 1, src = 0; float full = 0, low = 0; };
    struct SideTap { int delay = 1, src = 0; float w = 0; };
    // Tap delays are whole samples: a pattern never moves while it plays (changes are cross-faded
    // between two patterns), so fractional delays would only add an interpolation low-pass.
    struct TapSet
    {
        std::array<MainTap, kMaxTaps> main[2] {};           // specular arrival per channel
        std::array<SideTap, kMaxTaps * kSide> side[2] {};   // scattered arrivals per channel (high band)
        int count = 0, sideCount = 0;
        float energy = 0, energyLow = 0, energyHigh = 0, spread = 0;
    };
    struct Cand { float delayMs = 0, gLow = 0, gHigh = 0, pan = 0, delayL = 0, delayR = 0; };
    struct Key
    {
        std::array<float, 20> v {};
        bool operator== (const Key& o) const
        {
            for (size_t i = 0; i < v.size(); ++i)
                if (std::abs (v[i] - o.v[i]) > 1e-4f * (1.0f + std::abs (v[i]))) return false;
            return true;
        }
    };

    // main (specular) arrivals feed the full-band and low buses, scattered arrivals the scatter bus
    inline void accumulate (const TapSet& s, float w, float& fL, float& fR, float& lL, float& lR, float& hL, float& hR) const noexcept
    {
        if (w <= 0.0f) return;
        float f[2] = { 0, 0 }, l[2] = { 0, 0 }, h[2] = { 0, 0 };
        for (int c = 0; c < 2; ++c)
        {
            for (int t = 0; t < s.count; ++t)
            {
                const MainTap& tp = s.main[c][(size_t) t];
                const float x = input[tp.src].read (tp.delay);
                f[c] += x * tp.full;
                l[c] += x * tp.low;
            }
            for (int t = 0; t < s.sideCount; ++t)
            {
                const SideTap& tp = s.side[c][(size_t) t];
                h[c] += input[tp.src].read (tp.delay) * tp.w;
            }
        }
        fL += w * f[0]; fR += w * f[1]; lL += w * l[0]; lR += w * l[1]; hL += w * h[0]; hR += w * h[1];
    }

    void build (const Params& p, float depth, TapSet& out) const
    {
        const Geometry g = computeGeometry (p, depth);
        const float start = std::clamp (p.erStartMs, 0.0f, 100.0f);
        const float stop  = std::clamp (std::max (p.erStopMs, start + 5.0f), 10.0f, 300.0f);
        const float dens  = std::clamp (p.erDensity, 0.0f, 1.0f);
        const int maxOrder = 2 + (int) std::lround (3.0f * dens);
        const int maxTaps  = std::min (kMaxTaps, 16 + (int) std::lround (32.0f * dens));
        const bool air = ! p.lockTone;

        MaterialData m[numSurfaces];
        for (int s = 0; s < numSurfaces; ++s) m[s] = materialData (p.materials[(size_t) s]);

        auto img = [] (int n, float L, float s) { return (n % 2 == 0) ? n * L + s : (n + 1) * L - s; };
        auto hitsHigh = [] (int n) { return n > 0 ? (n + 1) / 2 : (-n) / 2; };     // wall at L
        auto hitsLow  = [] (int n) { return n > 0 ? n / 2 : (-n + 1) / 2; };        // wall at 0

        int numCands = 0;
        for (int nx = -maxOrder; nx <= maxOrder; ++nx)
            for (int ny = -maxOrder; ny <= maxOrder; ++ny)
                for (int nz = -3; nz <= 3; ++nz)
                {
                    const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);
                    if (order == 0 || order > maxOrder) continue;

                    const float ix = img (nx, g.W, g.sx), iy = img (ny, g.D, g.sy), iz = img (nz, g.H, g.sz);
                    const float dx = ix - g.lx, dy = iy - g.ly, dz = iz - g.lz;
                    const float d = std::sqrt (dx * dx + dy * dy + dz * dz);
                    const float t = (d - g.direct) / 343.0f * 1000.0f;
                    if (t < std::max (start, 2.5f) || t > stop) continue;

                    // per-surface absorption (x: left / right, y: front / back, z: floor / ceiling)
                    float gl = kErCal * kRefDist / d, gh = gl;
                    const int hits[numSurfaces] = { hitsLow (ny), hitsHigh (ny), hitsLow (nx), hitsHigh (nx), hitsLow (nz), hitsHigh (nz) };
                    for (int s = 0; s < numSurfaces; ++s)
                        if (hits[s] > 0) { gl *= std::pow (m[s].rLow, (float) hits[s]); gh *= std::pow (m[s].rHigh, (float) hits[s]); }

                    if (air) gh *= std::pow (10.0f, -kAirDbPerM * d / 20.0f);
                    if (dy > 0.0f) gh *= 0.75f;                     // from behind: head shadow

                    const float horiz = std::sqrt (dx * dx + dy * dy) + 1e-6f;
                    const float pan = std::clamp (dx / horiz, -1.0f, 1.0f);   // +x = right

                    // arrival at two receivers (ear distance apart on the x axis): per-reflection time difference
                    const float dL = std::sqrt ((dx + kRecvHalf) * (dx + kRecvHalf) + dy * dy + dz * dz);
                    const float dR = std::sqrt ((dx - kRecvHalf) * (dx - kRecvHalf) + dy * dy + dz * dz);
                    const float tL = (dL - g.direct) / 343.0f * 1000.0f, tR = (dR - g.direct) / 343.0f * 1000.0f;
                    if (numCands < (int) candBuf.size())
                        candBuf[(size_t) numCands++] = { t, gl, gh, pan, std::max (0.0f, tL), std::max (0.0f, tR) };
                }

        auto* c = candBuf.data();
        std::sort (c, c + numCands, [] (const Cand& a, const Cand& b) { return a.gLow + a.gHigh > b.gLow + b.gHigh; });
        const int count = std::min (numCands, maxTaps);

        const float window = std::max (stop - start, 1.0f);
        const float scatter = std::clamp (p.erDiffusion, 0.0f, 1.0f);
        double eLow = 0, eHigh = 0, sideNum = 0, midNum = 0;
        int t = 0, sc = 0;
        for (int i = 0; i < count; ++i)
        {
            const Cand& cd = c[i];
            const float rel = (cd.delayMs - start) / window;
            const float taper = rel > 0.75f ? std::max (0.0f, 1.0f - (rel - 0.75f) / 0.25f) : 1.0f;
            const float gh = cd.gHigh * taper, gl = cd.gLow * taper;
            const float pl = std::sqrt (0.5f * (1.0f - cd.pan)), pr = std::sqrt (0.5f * (1.0f + cd.pan));
            const int src = cd.pan < 0.0f ? 0 : 1;

            // ER DIFFUSION: every reflection becomes a short cluster with the time / energy profile of
            // a diffusing surface: a main arrival delayed by D0 plus scattered arrivals before and after
            // it. Positions and signs are drawn per reflection and per channel, so the clusters share no
            // period and the pattern cannot ring (a recursive all-pass diffuser applies the same filter
            // to every reflection: a fixed comb at the harmonics of its delays).
            //
            // Stereo (1.2): each reflection reaches the two receivers at its own geometric times, and its
            // main arrival is moved by a small random amount, drawn separately per channel (a real room is
            // never an exact shoebox). The interchannel coherence of the early field then follows
            // sinc^2: positive at low frequencies, about zero above ~300 Hz, never negative.
            // 1.1 delayed the whole right channel by 13 % of D0 (1.3 ms), which put every reflection in
            // anti-phase around 385 Hz (coherence -0.9 at 350-500 Hz, a dip in mono).
            const float eb = scatter > 0.001f ? erShareBefore (scatter) : 0.0f;
            const float ea = scatter > 0.001f ? erShareAfter (scatter) : 0.0f;
            const float wMain = std::sqrt (std::max (0.0f, 1.0f - eb - ea));
            const float pan[2] = { pl, pr };
            for (int ch = 0; ch < 2; ++ch)
            {
                const float d0 = erDiffDelayMs (p);
                std::uint32_t hj = (0x85EBCA6Bu * (std::uint32_t) (i + 7)) ^ (std::uint32_t) (cd.delayMs * 613.0f) ^ (ch ? 0xC2B2AE35u : 0x27D4EB2Fu);
                hj ^= hj << 13; hj ^= hj >> 17; hj ^= hj << 5;
                const float jitter = kErJitterMs * (2.0f * (float) (hj & 0xffffu) / 65535.0f - 1.0f);
                const float base = std::max (0.0f, (ch == 0 ? cd.delayL : cd.delayR) + jitter) + p.erPreDelayMs;
                auto toSamples = [this] (float ms) { return std::max (1, (int) std::lround (ms * 0.001 * fs)); };

                // Scattering is a mid / high frequency effect: below ~1.5 kHz the wavelength is large
                // against the surface detail and the reflection stays specular. So the main arrival
                // carries all of the low band, the scattered arrivals only the band above the split.
                MainTap& mt = out.main[ch][(size_t) t];
                mt.delay = toSamples (base + d0);
                mt.src = src;
                mt.full = wMain * gh * pan[ch];
                mt.low = (gl - wMain * gh) * pan[ch];

                if (scatter > 0.001f)
                {
                    std::uint32_t hs = (0x9E3779B9u * (std::uint32_t) (i + 1)) ^ (std::uint32_t) (cd.delayMs * 977.0f) ^ (ch ? 0x5bd1e995u : 0u);
                    auto rnd = [&hs]() { hs ^= hs << 13; hs ^= hs >> 17; hs ^= hs << 5; return (float) (hs & 0xffffu) / 65535.0f; };
                    for (int k = 0; k < kSide; ++k)
                    {
                        const bool before = k < kSide / 2;
                        const int slot = before ? k : k - kSide / 2;                     // stratified
                        const float u = ((float) slot + 0.15f + 0.7f * rnd()) / (float) (kSide / 2);
                        const float off = before ? d0 * (0.12f + 0.80f * u) : d0 * (1.08f + 0.67f * u);
                        SideTap& st = out.side[ch][(size_t) (sc + k)];
                        st.delay = toSamples (base + off);
                        st.src = src;
                        st.w = std::sqrt ((before ? eb : ea) / (float) (kSide / 2)) * (rnd() < 0.5f ? -1.0f : 1.0f) * gh * pan[ch];
                    }
                }
            }
            ++t;
            if (scatter > 0.001f) sc += kSide;

            eLow += (double) gl * gl;
            eHigh += (double) gh * gh;
            const double w = 0.5 * ((double) gl * gl + (double) gh * gh);
            const double cosTerm = std::sqrt (std::max (0.0, 1.0 - (double) cd.pan * cd.pan));
            sideNum += w * (1.0 - cosTerm);     // mono source: (pl - pr)^2
            midNum  += w * (1.0 + cosTerm);     //              (pl + pr)^2
        }
        out.count = t;
        out.sideCount = sc;
        out.energyLow = (float) eLow;
        out.energyHigh = (float) eHigh;
        out.energy = (float) (0.5 * (eLow + eHigh));
        out.spread = midNum > 1e-12 ? (float) (sideNum / midNum) : 0.0f;
    }

    static constexpr float kRecvHalf = 0.10f;     // receivers 20 cm apart (ear distance)
    static constexpr float kErJitterMs = 1.5f;    // per-channel arrival jitter of each reflection

    // diffusion delay: 0 at ER DIFFUSION 0, full from 0.1 on (scaled with SIZE)
    static float erDiffRamp (float scatter) { return std::clamp (scatter / 0.1f, 0.0f, 1.0f); }
    static float erDiffDelayMs (const Params& p)
    {
        const float sc = (float) std::clamp (std::sqrt (sizeScale (p.size)), 0.7, 1.4);
        return 10.1f * sc * erDiffRamp (std::clamp (p.erDiffusion, 0.0f, 1.0f));
    }
    static float interp (const float* x, const float* y, int n, float v)
    {
        if (v <= x[0]) return y[0];
        for (int k = 1; k < n; ++k)
            if (v <= x[k]) return y[k - 1] + (y[k] - y[k - 1]) * (v - x[k - 1]) / (x[k] - x[k - 1]);
        return y[n - 1];
    }
    static float erShareBefore (float d) { static const float x[6] = { 0, .1f, .2f, .4f, .7f, 1 }, y[6] = { 0, .02f, .07f, .25f, .56f, .75f }; return interp (x, y, 6, d); }
    static float erShareAfter (float d)  { static const float x[6] = { 0, .1f, .2f, .4f, .7f, 1 }, y[6] = { 0, .01f, .05f, .17f, .29f, .24f }; return interp (x, y, 6, d); }

    double fs = 48000.0;
    DelayLine input[2];
    Tilt color[2];
    Biquad scatterBus[2];
    Biquad lowBus[2];
    TapSet sets[2], refSet;
    mutable std::array<Cand, 1200> candBuf {};
    int active = 0;
    bool haveSet = false, fading = false;
    float fadePos = 0.0f;

    Key key {};
};

//==============================================================================
class LateReverb
{
public:
    static constexpr int N = 16;
    static constexpr int K = 8;           // build-up taps per channel
    // In-loop all-passes (Density): two sections per line, delay as a fraction of the line length,
    // modulated in opposite directions so the frequencies where they stretch the loop keep moving
    // (a static high-gain all-pass makes the harmonics of its delay decay slower than their
    // neighbours: a fixed comb in the tail).
    static constexpr float kApR1 = 0.19f, kApR2 = 0.31f, kApGMax = 0.50f;
    // Minimum total loop delay (s). Below this the modes are too sparse and the tail rings
    // (small rooms, Studio Room); independent of Decay so turning Decay never moves the pitch.
    static constexpr double kMinLoopSec = 0.55;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double maxLineMs = 100.0 * kMaxRoomScale * 1.25 + 6.0;
        for (int i = 0; i < N; ++i)
        {
            lines[i].allocate ((int) std::ceil (maxLineMs * 0.001 * fs));
            loopAP[i].allocate ((int) std::ceil (kApR1 * 1.5 * maxLineMs * 0.001 * fs));
            loopAP2[i].allocate ((int) std::ceil (kApR2 * 1.5 * maxLineMs * 0.001 * fs));
        }
        for (int c = 0; c < 2; ++c)
        {
            build[c].allocate ((int) std::ceil (0.82 * fs));         // late delay 300 + spread 500
            for (auto& a : inDiff[c]) a.allocate ((int) std::ceil (0.04 * fs));
            color[c].prepare (fs);
        }
        reset();
        configure (Chamber);
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { lines[i].clear(); loopAP[i].clear(); loopAP2[i].clear(); shLow[i].clear(); shMid[i].clear(); shHigh[i].clear(); modZ[i] = 0; }
        mLP = sLP = 0; for (int c = 0; c < 2; ++c) { build[c].clear(); for (auto& a : inDiff[c]) a.clear(); color[c].clear(); }
        for (int i = 0; i < N; ++i) { modPhase[i] = (double) i / N; modTarget[i] = 0; }
    }

    void configure (int algorithm)
    {
        algo = std::clamp (algorithm, 0, (int) numAlgorithms - 1);
        // base line lengths (ms at size 1): log-spaced with golden-ratio jitter
        static constexpr float ranges[numAlgorithms][2] = { { 6.5f, 29.0f }, { 11.0f, 47.0f }, { 23.0f, 97.0f } };
        const float lo = ranges[algo][0], hi = ranges[algo][1];
        for (int i = 0; i < N; ++i)
        {
            const float u = (float) i / (float) (N - 1);
            const float jitter = (std::fmod ((float) i * 0.61803398875f, 1.0f) - 0.5f) * 0.09f;
            baseMs[i] = lo * std::pow (hi / lo, std::clamp (u + jitter, 0.0f, 1.0f));
        }
        // shuffle so neighbours in the matrix differ in length
        static constexpr int order[N] = { 0, 9, 4, 13, 2, 11, 6, 15, 1, 8, 5, 12, 3, 10, 7, 14 };
        std::array<float, N> tmp {};
        for (int i = 0; i < N; ++i) tmp[(size_t) i] = baseMs[order[i]];
        double sumMs = 0.0;
        for (int i = 0; i < N; ++i) { baseMs[i] = tmp[(size_t) i]; sumMs += baseMs[i] * (1.0 + kApR1 + kApR2); }
        minScale = kMinLoopSec * 1000.0 / sumMs;

        // per-line modulation speed / depth spread
        for (int i = 0; i < N; ++i)
        {
            modRateMul[i]  = 0.72f + 0.56f * std::fmod ((float) (i * 7 + 3) * 0.3819660f, 1.0f);
            modDepthMul[i] = 0.55f + 0.45f * std::fmod ((float) (i * 5 + 1) * 0.6180340f, 1.0f);
        }
    }

    int getAlgorithm() const { return algo; }
    float getLevelComp() const { return levelComp; }

    // roomScale: late size relative to the reference room; rt60: effective decay
    void update (const Params& p, double roomScale, double rt60, bool force)
    {
        const double rawScale = roomScale * (1.25 - 0.45 * std::clamp ((double) p.density, 0.0, 1.0));
        const double scale = std::clamp (rawScale, minScale, kMaxRoomScale * 1.25);
        // reverberant energy goes as 1 / loop length: keep the level the short lines would have had
        lineComp = (float) std::sqrt (scale / rawScale);
        targetScale = (float) scale;
        if (force) curScale = targetScale;

        apG = kApGMax * std::clamp (p.density, 0.0f, 1.0f);
        inDiffG = 0.75f * std::clamp (p.diffusion, 0.0f, 1.0f);
        inDiffScale = (float) std::sqrt (roomScale);
        diffuserDelayMs = (kInDiffMs[0] + kInDiffMs[1] + kInDiffMs[2] + kInDiffMs[3]) * inDiffScale;
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 4; ++a)
                inDiffLen[c][a] = (int) std::lround (kInDiffMs[a] * (c ? 1.07 : 1.0) * 0.001 * fs * inDiffScale);

        // RT60 per band -> loop gains (using the target lengths).
        // Realised as base gain (low-mid band) x low shelf x two high shelves, so each band's
        // decay time is set independently and the loop magnitude never exceeds the largest band gain.
        const double rt = std::clamp (rt60, 0.1, 40.0);
        const double mult[4] = { std::clamp ((double) p.lfMult, 0.1, 4.0), std::clamp ((double) p.lmMult, 0.1, 4.0),
                                 std::clamp ((double) p.hmMult, 0.1, 4.0), std::clamp ((double) p.hfMult, 0.1, 4.0) };
        const double f1 = std::clamp ((double) p.xLow, 40.0, 1000.0);
        const double f2 = std::clamp ((double) p.xMid, f1 * 1.5, 8000.0);
        const double f3 = std::clamp ((double) p.xHigh, f2 * 1.5, 18000.0);
        for (int i = 0; i < N; ++i)
        {
            const double loopSec = baseMs[i] * (1.0 + kApR1 + kApR2) * scale * 0.001;   // line + in-loop all-passes
            // decay rate per band (dB/s). 1.2: the DECAY EQ multipliers alone set the decay per band;
            // 1.1 added an extra air term and an input high shelf that reproduced the interpolation
            // losses of 1.0 (a darker tail than the HF DECAY setting said).
            double rate[4];                                                    // dB per second
            for (int b = 0; b < 4; ++b)
                rate[b] = 60.0 / (rt * mult[b]);
            // all-pass sections lose energy at the low-mid rate inside their own delay; the line
            // filter takes the rest, so that every band decays at its rate over the whole loop
            const double apSec1 = baseMs[i] * kApR1 * scale * 0.001, apSec2 = baseMs[i] * kApR2 * scale * 0.001;
            apA1[i] = (float) std::pow (10.0, -rate[1] * apSec1 / 20.0);
            apA2[i] = (float) std::pow (10.0, -rate[1] * apSec2 / 20.0);
            double g[4];
            for (int b = 0; b < 4; ++b)
                g[b] = std::pow (10.0, -(rate[b] * loopSec - rate[1] * (apSec1 + apSec2)) / 20.0);
            baseGain[i] = (float) g[1];
            shLow[i].set (f1, g[0] / g[1], fs, false);
            shMid[i].set (f2, g[2] / g[1], fs, true);
            shHigh[i].set (f3, g[3] / g[2], fs, true);
        }

        // build-up (Shape / Spread)
        const float spread = std::clamp (p.spreadMs, 0.0f, 500.0f);
        const float lateDelay = std::clamp (p.lateDelayMs, 0.0f, 300.0f);
        const float alpha = -4.5f + 8.0f * std::clamp (p.shape, 0.0f, 1.0f);   // <0 decaying, >0 rising
        float wsum = 0.0f;
        for (int k = 0; k < K; ++k)
        {
            const float u = (float) k / (float) (K - 1);
            const float jit = (k == 0 || k == K - 1) ? 0.0f : (std::fmod ((float) k * 0.7548777f, 1.0f) - 0.5f) * 0.6f / (float) (K - 1);
            // LATE DELAY is where the late field starts: subtract the delay of the input diffusers
            tapTime[k] = (float) ((std::max (0.0f, lateDelay - diffuserDelayMs) + spread * std::clamp (u + jit, 0.0f, 1.0f)) * 0.001 * fs) + 1.0f;
            tapW[k] = std::exp (alpha * u);
            wsum += tapW[k] * tapW[k];
        }
        const float wn = 1.0f / std::sqrt (wsum);
        for (int k = 0; k < K; ++k) tapW[k] *= wn;

        // modulation
        float rate = std::clamp (p.modRate, 0.01f, 5.0f), depthMs = 0.55f * std::clamp (p.modDepth, 0.0f, 1.0f);
        float rnd = std::clamp (p.modRandom, 0.0f, 1.0f), smooth = std::clamp (p.modSmooth, 0.0f, 1.0f);
        switch (p.modCharacter)
        {
            case ModRandom:  rnd = 0.5f + 0.5f * rnd; break;
            case ModVintage: rate *= 0.6f; depthMs *= 1.6f; smooth *= 0.5f; break;
            case ModModern:  rate *= 1.4f; depthMs *= 0.6f; rnd = 0.3f + 0.7f * rnd; break;
            default:         rnd *= 0.35f; break;
        }
        modRateHz = rate;
        modDepthSamples = (float) (depthMs * 0.001 * fs * std::max (1.0, std::sqrt (roomScale)));
        apMod = 0.08f + 0.3f * std::clamp (p.modDepth, 0.0f, 1.0f);
        modRnd = rnd;
        modSmoothCoef = (float) (1.0 - std::exp (-2.0 * kPi * (double) rate * (4.0 - 3.5 * smooth) / fs));

        // outputs
        color[0].set (p.lateColor); color[1].set (p.lateColor);
        decor = std::clamp (p.decorrelation, 0.0f, 1.0f);
        {
            const float rHi = 1.0f - decor, rLo = std::min (1.0f, rHi + 0.2f);
            aHi = std::sqrt (0.5f * (1.0f + rHi)); bHi = std::sqrt (0.5f * (1.0f - rHi));
            aLo = std::sqrt (0.5f * (1.0f + rLo)); bLo = std::sqrt (0.5f * (1.0f - rLo));
            lpA = (float) (1.0 - std::exp (-2.0 * kPi * 140.0 / fs));
            midNorm = 1.0f / std::sqrt (1.0f + kMidTapW * kMidTapW);
        }

        // gentle level compensation so long decays are louder, but not explosive
        static constexpr float algoTrim[numAlgorithms] = { 0.876f, 0.940f, 0.942f };   // late loudness as in 1.1
        levelComp = (float) (0.60 * std::pow (std::max (rt, 0.1) / 1.5, -0.32)) * lineComp * algoTrim[algo];


    }

    // inL/inR: pre-delayed input.  outL/outR: late reverb (overwrites)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const float sScale = (float) (1.0 - std::exp (-1.0 / (0.08 * fs)));
        const double phaseInc = modRateHz / fs;
        static constexpr float signL[N] = { 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1 };
        static constexpr float signR[N] = { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };
        const float outNorm = 1.0f / std::sqrt ((float) N);

        for (int s = 0; s < n; ++s)
        {
            curScale += sScale * (targetScale - curScale);

            // input diffusion (Diffusion)
            float x[2] = { inL[s], inR[s] };
            if (inDiffG > 0.001f)
            {
                for (int c = 0; c < 2; ++c)
                    for (int a = 0; a < 4; ++a)
                        x[c] = inDiff[c][a].process (x[c], inDiffLen[c][a], inDiffG);
            }
            build[0].push (x[0]);
            build[1].push (x[1]);

            // build-up taps -> injection vector (left taps feed even lines, right taps odd lines)
            float inj[N] {};
            for (int k = 0; k < K; ++k)
            {
                const float l = build[0].readLinear (tapTime[k]) * tapW[k];
                const float r = build[1].readLinear (tapTime[k] * 1.013f) * tapW[k];
                inj[(2 * k) % N]     += l;
                inj[(2 * k + 1) % N] += r;
                inj[(2 * k + 8) % N] -= 0.7f * l;
                inj[(2 * k + 9) % N] -= 0.7f * r;
            }

            // read lines (modulated), density all-pass, 4-band loop filter
            float y[N], z[N];
            for (int i = 0; i < N; ++i)
            {
                modPhase[i] += phaseInc * modRateMul[i];
                if (modPhase[i] >= 1.0) modPhase[i] -= 1.0;
                if (modRnd > 0.0f && rng.uni() < (float) (phaseInc * modRateMul[i] * 2.0)) modTarget[i] = rng.bi();
                modZ[i] += modSmoothCoef * (modTarget[i] - modZ[i]);
                const float lfo = (1.0f - modRnd) * fastSin2Pi (modPhase[i]) + modRnd * modZ[i];

                const float len = baseMs[i] * 0.001f * (float) fs * curScale;
                const float d = std::max (4.0f, len + modDepthSamples * modDepthMul[i] * (1.0f + lfo));
                float v = lines[i].readHermite (d);
                const float am = apMod * lfo * modDepthMul[i];
                v = loopAP[i].process (v, len * kApR1 * (1.0f + am), apG, apA1[i]);
                v = loopAP2[i].process (v, len * kApR2 * (1.0f - am), apG, apA2[i]);
                y[i] = v;

                z[i] = shHigh[i].process (shMid[i].process (shLow[i].process (v * baseGain[i])));
            }

            hadamard16 (z);
            for (int i = 0; i < N; ++i)
                lines[i].push (z[i] + inj[i]);

            // two orthogonal output sums (lines read at their end and 40 % along)
            float L0 = 0, R0 = 0;
            for (int i = 0; i < N; ++i)
            {
                const float lenS = baseMs[i] * 0.001f * (float) fs * curScale;
                const float yo = y[i] + kMidTapW * lines[i].read ((int) (lenS * kMidTapPos));
                L0 += signL[i] * yo;
                R0 += signR[i] * yo;
            }
            // DECORRELATION sets the interchannel correlation directly (0 % = mono, 100 % = uncorrelated),
            // a little higher below ~150 Hz as in a real diffuse field. 1.1 mixed the outputs instead, so
            // 40 % already meant +0.83: a late field sitting on top of a centred voice.
            const float M = (L0 + R0) * 0.70710678f, S = (L0 - R0) * 0.70710678f;
            mLP += lpA * (M - mLP);
            sLP += lpA * (S - sLP);
            const float Mo = aHi * M + (aLo - aHi) * mLP, So = bHi * S + (bLo - bHi) * sLP;
            const float g = outNorm * midNorm * levelComp;
            outL[s] = color[0].process ((Mo + So) * g);
            outR[s] = color[1].process ((Mo - So) * g);
        }
    }

private:
    static inline void hadamard16 (float* v) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b; v[j + h] = a - b;
                }
        for (int i = 0; i < N; ++i) v[i] *= 0.25f;
    }

    double fs = 48000.0;
    int algo = Chamber;
    DelayLine lines[N];
    Allpass loopAP[N], loopAP2[N];
    Shelf shLow[N], shMid[N], shHigh[N];
    float baseGain[N] {}, apA1[N] {}, apA2[N] {};
    float baseMs[N] {};
    DelayLine build[2];
    Allpass inDiff[2][4];
    // 1.2: short input diffusers. An all-pass moves the energy centre of what passes through it by its
    // delay; 1.1 used 4.7 + 3.6 + 12.7 + 9.3 = 30 ms, which (with the first pass through the lines)
    // made the late field peak ~100 ms after the sound and arrive as a second wave on the next syllable.
    static constexpr float kInDiffMs[4] = { 1.9f, 2.9f, 4.3f, 6.1f };
    // output also tapped inside each line, 40 % along it: the late field speaks earlier and denser
    static constexpr float kMidTapPos = 0.4f, kMidTapW = 0.8f;
    int inDiffLen[2][4] {};
    Tilt color[2];
    float tapTime[K] {}, tapW[K] {};
    float targetScale = 1.0f, curScale = 1.0f, apG = 0.4f, inDiffG = 0.5f, inDiffScale = 1.0f, diffuserDelayMs = 15.2f;
    double modPhase[N] {};
    float modRateMul[N] {}, modDepthMul[N] {}, modZ[N] {}, modTarget[N] {};
    double modRateHz = 0.6;
    float modDepthSamples = 0.0f, modRnd = 0.3f, modSmoothCoef = 0.001f, apMod = 0.2f;
    double minScale = 0.35;
    float aHi = 0.7f, bHi = 0.7f, aLo = 0.7f, bLo = 0.7f, lpA = 0.03f, mLP = 0, sLP = 0, midNorm = 1.0f;
    float lineComp = 1.0f;
    float decor = 0.85f, levelComp = 0.42f;
    Rng rng { 777u };
};

//==============================================================================
// Full processor: input conditioning, direct path (Insert), Spatial DNA + Late, locks, stereo, mix.
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : preDelay) d.allocate ((int) std::ceil (0.52 * fs));
        er.prepare (fs);
        late.prepare (fs);
        scratch.assign (8 * kChunk, 0.0f);
        params.algorithm = -1;
        applyParams (true);
        reset();
    }

    void reset()
    {
        for (auto& d : preDelay) d.clear();
        for (int c = 0; c < 2; ++c) { lowCut[c].clear(); highCut[c].clear(); directAir[c].clear(); toneShelf[c].clear(); }
        er.reset();
        late.reset();
        er.update (params);
    }

    void setParams (const Params& p) { pending = p; }

    void process (float* left, float* right, int numSamples)
    {
        for (int pos = 0; pos < numSamples; pos += kChunk)
        {
            const int n = std::min (kChunk, numSamples - pos);
            processChunk (left + pos, right + pos, n);
        }
    }

    double getTailSeconds() const
    {
        const double maxMult = std::max ({ (double) params.lfMult, (double) params.lmMult, (double) params.hmMult, (double) params.hfMult });
        return std::min (60.0, effectiveRT (params) * maxMult * 1.1 + (params.preDelayMs + params.lateDelayMs + params.spreadMs + params.erStopMs) * 0.001 + 0.2);
    }

    // late scale from the room volume
    static double lateScale (const Params& p)
    {
        const Geometry g = computeGeometry (p, p.depth);
        return std::clamp (std::cbrt ((double) g.W * g.D * g.H / kRefVolume), 0.35, kMaxRoomScale);
    }

    static double effectiveRT (const Params& p)
    {
        const double rt = std::clamp ((double) p.decay, 0.1, 30.0);
        return p.lockDecay ? rt : rt * (0.85 + 0.5 * std::clamp ((double) p.depth, 0.0, 1.0));
    }

    float getDirectGain() const { return targetDirect; }
    float getMakeupDb() const { return 20.0f * std::log10 (std::max (1e-6f, targetMakeup)); }

private:
    static constexpr int kChunk = 2048;
    // measured late output power (L+R, per unit input power per channel) at the reference room, by algorithm
    static float lateEnergy (int algorithm, double rt)
    {
        static constexpr float base[numAlgorithms] = { 0.13f, 0.095f, 0.06f };
        return base[std::clamp (algorithm, 0, (int) numAlgorithms - 1)] * (float) std::pow (std::max (rt, 0.1) / 1.8, 0.1);
    }

    void applyParams (bool force)
    {
        const bool algoChanged = pending.algorithm != params.algorithm;
        params = pending;

        if (algoChanged && force)
            late.configure (params.algorithm);
        else if (algoChanged)
        {
            algoSwitchPending = true;
            nextAlgorithm = params.algorithm;
        }

        const double rt = effectiveRT (params);
        const double scale = lateScale (params);
        Params forLate = params;
        forLate.algorithm = algoSwitchPending ? late.getAlgorithm() : params.algorithm;
        late.update (forLate, scale, rt, force);
        er.update (params);

        // input filters
        const double lc = std::clamp ((double) params.lowCutHz, 20.0, 2000.0);
        const double hc = std::clamp ((double) params.highCutHz, 1000.0, 20000.0);
        curLow  = force ? lc : curLow + 0.35 * (lc - curLow);
        curHigh = force ? hc : curHigh + 0.35 * (hc - curHigh);
        for (int c = 0; c < 2; ++c) { lowCut[c].setHighPass (curLow, fs); highCut[c].setLowPass (curHigh, fs); }

        // ---- Depth: direct path (Insert)
        const Geometry g = computeGeometry (params, params.depth);
        const bool insert = params.routing == Insert;
        targetDirect = insert ? directGain (g.direct) : 0.0f;
        const double airDb = params.lockTone ? 0.0 : -1.5 * kAirDbPerM * g.direct;
        for (int c = 0; c < 2; ++c) directAir[c].set (4000.0, std::pow (10.0, airDb / 20.0), fs, true);

        // ---- Width lock: keep the early pan spread of the reference depth
        targetErWidthComp = 1.0f;
        if (params.lockWidth && er.currentSpread() > 1e-4f)
            targetErWidthComp = std::clamp (std::pow (er.referenceSpread() / er.currentSpread(), 0.9f), 0.5f, 2.5f);

        // ---- Tone lock: keep the high / low energy balance of the reference depth
        targetTone = 1.0f;
        if (params.lockTone)
        {
            const float mixW = insert ? std::sin (std::clamp (params.mix, 0.0f, 1.0f) * 1.5707963f) : 1.0f;
            const float mixD = insert ? std::cos (std::clamp (params.mix, 0.0f, 1.0f) * 1.5707963f) : 0.0f;
            const float eE = params.erOn ? dbToGain (params.erLevelDb) : 0.0f;
            const float eL = params.lateOn ? dbToGain (params.lateLevelDb) : 0.0f;
            const float lateE = eL * eL * lateEnergy (params.algorithm, rt);
            const float lateHF = 0.5f * (params.hmMult + params.hfMult), lateLF = 0.5f * (params.lfMult + params.lmMult);
            const Geometry gRef = computeGeometry (params, kDepthRef);
            const float dRef = insert ? directGain (gRef.direct) : 0.0f;

            auto ratio = [&] (float dGain, float erLow, float erHigh)
            {
                const float lo = mixD * mixD * 2.0f * dGain * dGain + mixW * mixW * (eE * eE * erLow + lateE * lateLF);
                const float hi = mixD * mixD * 2.0f * dGain * dGain + mixW * mixW * (eE * eE * erHigh + lateE * lateHF);
                return hi / std::max (1e-9f, lo);
            };
            const float now = ratio (targetDirect, er.currentLow(), er.currentHigh());
            const float ref = ratio (dRef, er.referenceLow(), er.referenceHigh());
            if (now > 1e-9f)
                targetTone = std::clamp (std::sqrt (ref / now), 0.5f, 2.0f);
        }
        curTone += 0.25f * (targetTone - curTone);
        if (force) curTone = targetTone;
        for (int c = 0; c < 2; ++c) toneShelf[c].set (2500.0, curTone * curTone, fs, true);

        // ---- Level lock: keep the output energy of the reference depth
        targetMakeup = 1.0f;
        if (params.lockLevel)
        {
            const float mixW = insert ? std::sin (std::clamp (params.mix, 0.0f, 1.0f) * 1.5707963f) : 1.0f;
            const float mixD = insert ? std::cos (std::clamp (params.mix, 0.0f, 1.0f) * 1.5707963f) : 0.0f;
            const float eE = params.erOn ? dbToGain (params.erLevelDb) : 0.0f;
            const float eL = params.lateOn ? dbToGain (params.lateLevelDb) : 0.0f;
            const float lateE = eL * eL * lateEnergy (params.algorithm, rt);
            const Geometry gRef = computeGeometry (params, kDepthRef);
            const float dRef = insert ? directGain (gRef.direct) : 0.0f;

            const float eNow = mixD * mixD * 2.0f * targetDirect * targetDirect + mixW * mixW * (eE * eE * er.currentEnergy() + lateE);
            const float eRef = mixD * mixD * 2.0f * dRef * dRef + mixW * mixW * (eE * eE * er.referenceEnergy() + lateE);
            if (eNow > 1e-9f)
                targetMakeup = std::clamp (std::sqrt (eRef / eNow), 0.25f, 4.0f);
        }

        if (force)
        {
            curPre = std::clamp (params.preDelayMs, 0.0f, 500.0f) * 0.001f * (float) fs;
            curEr = params.erOn ? dbToGain (params.erLevelDb) : 0.0f;
            curLate = params.lateOn ? dbToGain (params.lateLevelDb) : 0.0f;
            curMix = std::clamp (params.mix, 0.0f, 1.0f);
            curOut = dbToGain (params.outputDb);
            curWidth = params.width; curErW = params.erWidth * targetErWidthComp; curLateW = params.lateWidth; curBal = params.balance;
            curDirect = targetDirect; curMakeup = targetMakeup;
            curRouteDry = insert ? 1.0f : 0.0f;
        }
    }

    void processChunk (float* left, float* right, int n)
    {
        applyParams (false);
        switchedThisChunk = false;

        float* inL = scratch.data();
        float* inR = inL + kChunk;
        float* erL = inR + kChunk;
        float* erR = erL + kChunk;
        float* ltL = erR + kChunk;
        float* ltR = ltL + kChunk;

        const float sSlow = (float) (1.0 - std::exp (-1.0 / (0.06 * fs)));
        const float sFast = (float) (1.0 - std::exp (-1.0 / (0.03 * fs)));
        const float tPre = std::clamp (params.preDelayMs, 0.0f, 500.0f) * 0.001f * (float) fs;
        const float cf = 0.5f * std::clamp (params.crossfeed, 0.0f, 1.0f);
        const bool highCutOn = curHigh < 19900.0;

        for (int i = 0; i < n; ++i)
        {
            curPre += sSlow * (tPre - curPre);
            const float l = left[i], r = right[i];
            float a = l * (1.0f - cf) + r * cf;
            float b = r * (1.0f - cf) + l * cf;
            a = lowCut[0].process (a); b = lowCut[1].process (b);
            if (highCutOn) { a = highCut[0].process (a); b = highCut[1].process (b); }
            const float pd = std::max (curPre, 1.0f);
            const float ya = preDelay[0].readLinear (pd), yb = preDelay[1].readLinear (pd);
            preDelay[0].push (a); preDelay[1].push (b);
            inL[i] = curPre < 1.0f ? a : ya;
            inR[i] = curPre < 1.0f ? b : yb;
        }

        er.process (inL, inR, erL, erR, n);
        late.process (inL, inR, ltL, ltR, n);

        const bool insert = params.routing == Insert;
        const float tEr = params.erOn ? dbToGain (params.erLevelDb) : 0.0f;
        const float tLate = params.lateOn ? dbToGain (params.lateLevelDb) : 0.0f;
        const float tMix = insert ? std::clamp (params.mix, 0.0f, 1.0f) : 1.0f;
        const float tOut = dbToGain (params.outputDb);
        const float tErW = params.erWidth * targetErWidthComp;
        const float tRouteDry = insert ? 1.0f : 0.0f;
        const float switchStep = (float) (1.0 / (0.015 * fs));

        for (int i = 0; i < n; ++i)
        {
            curEr      += sFast * (tEr - curEr);
            curLate    += sFast * (tLate - curLate);
            curMix     += sFast * (tMix - curMix);
            curOut     += sFast * (tOut - curOut);
            curWidth   += sFast * (params.width - curWidth);
            curErW     += sFast * (tErW - curErW);
            curLateW   += sFast * (params.lateWidth - curLateW);
            curBal     += sFast * (params.balance - curBal);
            curDirect  += sFast * (targetDirect - curDirect);
            curMakeup  += sSlow * (targetMakeup - curMakeup);
            curRouteDry += sFast * (tRouteDry - curRouteDry);

            if (algoSwitchPending)
            {
                switchedThisChunk = true;
                algoGain -= switchStep;
                if (algoGain <= 0.0f)
                {
                    algoGain = 0.0f;
                    late.configure (nextAlgorithm);
                    late.reset();
                    Params p = params; p.algorithm = nextAlgorithm;
                    late.update (p, lateScale (params), effectiveRT (params), true);
                    algoSwitchPending = false;
                }
            }
            else if (algoGain < 1.0f && ! switchedThisChunk)
                algoGain = std::min (1.0f, algoGain + switchStep);

            // wet: early + late with separate widths, then global width and balance
            const float eM = 0.5f * (erL[i] + erR[i]), eS = 0.5f * (erL[i] - erR[i]) * curErW;
            const float lM = 0.5f * (ltL[i] + ltR[i]), lS = 0.5f * (ltL[i] - ltR[i]) * curLateW;
            const float lg = curLate * algoGain;
            const float M = eM * curEr + lM * lg;
            const float S = (eS * curEr + lS * lg) * curWidth;
            float wl = M + S, wr = M - S;
            const float pb = std::clamp (curBal, -1.0f, 1.0f);
            wl *= std::sqrt (1.0f - std::max (0.0f, pb));
            wr *= std::sqrt (1.0f + std::min (0.0f, pb));

            // direct path (Insert): distance gain + air absorption, no delay
            const float dl = directAir[0].process (left[i]) * curDirect;
            const float dr = directAir[1].process (right[i]) * curDirect;

            const float wetAmt = std::sin (curMix * 1.5707963f);
            const float dryAmt = std::cos (curMix * 1.5707963f) * curRouteDry;
            const float g = curOut * curMakeup;
            float ol = (dl * dryAmt + wl * wetAmt) * g;
            float orr = (dr * dryAmt + wr * wetAmt) * g;
            if (params.lockTone || std::abs (curTone - 1.0f) > 1e-3f) { ol = toneShelf[0].process (ol); orr = toneShelf[1].process (orr); }
            left[i] = ol; right[i] = orr;
        }
    }

    double fs = 48000.0;
    Params pending, params;
    DelayLine preDelay[2];
    Biquad lowCut[2], highCut[2];
    Shelf directAir[2], toneShelf[2];
    SpatialEarly er;
    LateReverb late;
    std::vector<float> scratch;
    double curLow = 20.0, curHigh = 20000.0;
    float targetDirect = 1.0f, targetMakeup = 1.0f, targetErWidthComp = 1.0f, targetTone = 1.0f, curTone = 1.0f;
    float curPre = 0, curEr = 1, curLate = 1, curMix = 0.35f, curOut = 1, curWidth = 1, curErW = 1, curLateW = 1, curBal = 0;
    float curDirect = 1, curMakeup = 1, curRouteDry = 1;
    bool algoSwitchPending = false, switchedThisChunk = false;
    int nextAlgorithm = Chamber;
    float algoGain = 1.0f;
};

} // namespace mspace
