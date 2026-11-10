// Offline harness for the MASTER SPACE engine.
// usage: render_space <in.f32> <out.f32> <sampleRate> <block> [key=value ...]
#include "../Source/MasterSpace/MSEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#if defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

static void setKV (mspace::Params& p, const std::string& k, float v)
{
#define F(name) if (k == #name) { p.name = v; return; }
#define I(name) if (k == #name) { p.name = (int) v; return; }
#define B(name) if (k == #name) { p.name = v > 0.5f; return; }
    I(algorithm) I(routing) F(depth) F(decay) F(size) F(preDelayMs) F(erLevelDb) F(lateLevelDb) B(erOn) B(lateOn)
    F(diffusion) F(density) F(shape) F(spreadMs) F(width) F(mix) F(outputDb)
    F(roomW) F(roomD) F(roomH) F(srcX) F(lisX) F(lisY) F(erDensity) F(erDiffusion) F(erColor) F(erWidth)
    F(erStartMs) F(erStopMs) F(erPreDelayMs) F(lateDelayMs) F(lateWidth) F(lateColor)
    F(lfMult) F(lmMult) F(hmMult) F(hfMult) F(xLow) F(xMid) F(xHigh) F(lowCutHz) F(highCutHz) F(modRate) F(modDepth)
    F(modRandom) F(modSmooth) I(modCharacter) F(balance) F(crossfeed) F(decorrelation)
    B(lockLevel) B(lockTone) B(lockDecay) B(lockWidth)
    if (k.size() == 4 && k.rfind ("mat", 0) == 0) { p.materials[(size_t) (k[3] - '0')] = (int) v; return; }
    std::fprintf (stderr, "unknown param %s\n", k.c_str()); std::exit (2);
}

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "bad args\n"); return 1; }
#if defined(__x86_64__)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> data; float buf[4096]; size_t r;
    while ((r = std::fread (buf, sizeof (float), 4096, f)) > 0) data.insert (data.end(), buf, buf + r);
    std::fclose (f);

    const double fs = std::atof (argv[3]);
    const int block = std::atoi (argv[4]);

    // params: key=value, or "@frame:key=value" to change at a frame (automation test)
    mspace::Params p;
    struct Change { size_t frame; std::string k; float v; };
    std::vector<Change> changes;
    for (int a = 5; a < argc; ++a)
    {
        std::string s = argv[a];
        size_t at = 0;
        if (s[0] == '@') { const auto c = s.find (':'); at = (size_t) std::atoll (s.substr (1, c - 1).c_str()); s = s.substr (c + 1); }
        const auto e = s.find ('=');
        const std::string k = s.substr (0, e); const float v = (float) std::atof (s.substr (e + 1).c_str());
        if (at == 0) setKV (p, k, v); else changes.push_back ({ at, k, v });
    }

    mspace::Engine eng;
    eng.setParams (p);
    eng.prepare (fs);

    const size_t frames = data.size() / 2;
    std::vector<float> L (frames), R (frames);
    for (size_t i = 0; i < frames; ++i) { L[i] = data[2 * i]; R[i] = data[2 * i + 1]; }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t pos = 0; pos < frames; pos += (size_t) block)
    {
        bool changed = false;
        for (auto& c : changes) if (c.frame >= pos && c.frame < pos + (size_t) block) { setKV (p, c.k, c.v); changed = true; }
        if (changed) eng.setParams (p);
        const int n = (int) std::min ((size_t) block, frames - pos);
        eng.process (L.data() + pos, R.data() + pos, n);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::fprintf (stderr, "rt %.1fx\n", (frames / fs) / std::chrono::duration<double> (t1 - t0).count());

    for (size_t i = 0; i < frames; ++i) { data[2 * i] = L[i]; data[2 * i + 1] = R[i]; }
    f = std::fopen (argv[2], "wb"); std::fwrite (data.data(), sizeof (float), data.size(), f); std::fclose (f);
    return 0;
}
