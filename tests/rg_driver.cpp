// RESONANCE GUARD on its own (MAS_RI): reads 6 interleaved f32 channels (dry L/R, early L/R, late L/R),
// writes 8 (early L/R, late L/R processed, then the four deltas) and the cut map (bands per hop).
// usage: rg_driver <in.f32> <out.f32> <map.f32> <fs> key=value ...   (keys: on mode smooth detail select attack release
//        max lo hi target early late match quality rt0 rt1 rt2 rt3 x1 x2 x3 block)
#define MAS_ATMOS 1
#define MAS_RI 1
#include "../Source/MasterSpace/MSEngine.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> d; float buf[4096]; size_t r;
    while ((r = std::fread (buf, sizeof (float), 4096, f)) > 0) d.insert (d.end(), buf, buf + r);
    std::fclose (f);
    const double fs = std::atof (argv[4]);
    mspace::ri::ResonanceGuard::Settings s; s.on = true;
    double rt[4] = { 2, 2, 2, 2 }, x[3] = { 250, 2000, 7000 };
    int block = 256;
    for (int a = 5; a < argc; ++a)
    {
        std::string kv = argv[a]; const auto e = kv.find ('='); const std::string k = kv.substr (0, e); const double v = std::atof (kv.substr (e + 1).c_str());
        if (k == "on") s.on = v > 0.5; else if (k == "mode") s.mode = (int) v; else if (k == "smooth") s.smooth = (float) v;
        else if (k == "detail") s.detail = (float) v; else if (k == "select") s.selectivity = (float) v; else if (k == "attack") s.attackMs = (float) v;
        else if (k == "release") s.releaseMs = (float) v; else if (k == "max") s.maxRedDb = (float) v; else if (k == "lo") s.loHz = (float) v;
        else if (k == "hi") s.hiHz = (float) v; else if (k == "target") s.target = (int) v; else if (k == "early") s.earlyAmt = (float) v;
        else if (k == "late") s.lateAmt = (float) v; else if (k == "match") s.outputMatch = v > 0.5; else if (k == "quality") s.quality = (int) v;
        else if (k.size() == 3 && k[0] == 'r' && k[1] == 't') rt[k[2] - '0'] = v;
        else if (k.size() == 2 && k[0] == 'x') x[k[1] - '1'] = v;
        else if (k == "block") block = (int) v;
        else { std::fprintf (stderr, "unknown %s\n", k.c_str()); return 2; }
    }
    auto* g = new mspace::ri::ResonanceGuard();
    g->prepare (fs);
    g->set (s, true);
    g->setExpectedRT (rt, x[0], x[1], x[2]);
    const size_t n = d.size() / 6;
    std::vector<float> ch[6], dl[4];
    for (int c = 0; c < 6; ++c) { ch[c].resize (n); for (size_t i = 0; i < n; ++i) ch[c][i] = d[6 * i + (size_t) c]; }
    for (auto& v : dl) v.assign (n, 0.0f);
    std::vector<float> map;
    double worst = 0.0;
    std::vector<double> bt;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t p = 0; p < n; p += (size_t) block)
    {
        const int m = (int) std::min ((size_t) block, n - p);
        auto b0 = std::chrono::high_resolution_clock::now();
        g->process (ch[0].data() + p, ch[1].data() + p, ch[2].data() + p, ch[3].data() + p, ch[4].data() + p, ch[5].data() + p, m, 1.0f, 1.0f,
                    dl[0].data() + p, dl[1].data() + p, dl[2].data() + p, dl[3].data() + p);
        auto b1 = std::chrono::high_resolution_clock::now();
        const double ratio = std::chrono::duration<double> (b1 - b0).count() / (m / fs);
        worst = std::max (worst, ratio); bt.push_back (ratio);
        for (int b = 0; b < g->numBands(); ++b) map.push_back (g->vizCutDb (b));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::sort (bt.begin(), bt.end());
    std::fprintf (stderr, "bands %d latency %d cpu %.2f%% block p99.9 %.1f%% worst %.1f%%\n", g->numBands(), g->latencySamples(),
                  100.0 * std::chrono::duration<double> (t1 - t0).count() / (n / fs), 100.0 * bt[(size_t) (0.999 * (bt.size() - 1))], 100.0 * worst);
    std::vector<float> out (8 * n);
    for (size_t i = 0; i < n; ++i)
    {
        for (int c = 0; c < 4; ++c) out[8 * i + (size_t) c] = ch[2 + c][i];
        for (int c = 0; c < 4; ++c) out[8 * i + 4 + (size_t) c] = dl[c][i];
    }
    f = std::fopen (argv[2], "wb"); std::fwrite (out.data(), sizeof (float), out.size(), f); std::fclose (f);
    f = std::fopen (argv[3], "wb"); std::fwrite (map.data(), sizeof (float), map.size(), f); std::fclose (f);
    std::FILE* fb = std::fopen ((std::string (argv[3]) + ".hz").c_str(), "wb");
    for (int b = 0; b < g->numBands(); ++b) { const float hz = (float) g->bandHz (b); std::fwrite (&hz, sizeof (float), 1, fb); }
    std::fclose (fb);
    delete g;
    return 0;
}
