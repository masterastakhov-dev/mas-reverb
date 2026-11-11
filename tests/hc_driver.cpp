// HARMONIC COLOR alone: stereo float32 in -> out, for spectra, aliasing, latency and level measurements.
//   g++ -O2 -std=c++17 -o hc_driver tests/hc_driver.cpp
//   hc_driver in.f32 out.f32 fs on=1 char=0 drive=0.5 color=0 mix=1 comp=1 quality=0 level=1 block=64
#include "../Source/MasterSpace/HarmonicColor.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: hc_driver in.f32 out.f32 fs [key=value ...]\n"); return 1; }
    const double fs = std::atof (argv[3]);
    hc::HarmonicColor::Settings s; s.on = true;
    float level = 1.0f; int block = 64;
    for (int i = 4; i < argc; ++i)
    {
        std::string a = argv[i]; const auto eq = a.find ('='); if (eq == std::string::npos) continue;
        const std::string k = a.substr (0, eq); const double v = std::atof (a.c_str() + eq + 1);
        if (k == "on") s.on = v > 0.5; else if (k == "char") s.character = (int) v; else if (k == "drive") s.drive = (float) v;
        else if (k == "color") s.color = (float) v; else if (k == "mix") s.mix = (float) v; else if (k == "comp") s.levelComp = v > 0.5;
        else if (k == "quality") s.quality = (int) v; else if (k == "level") level = (float) v; else if (k == "block") block = (int) v;
    }
    FILE* f = std::fopen (argv[1], "rb"); if (! f) return 1;
    std::vector<float> in; float buf[4096]; size_t r;
    while ((r = std::fread (buf, sizeof (float), 4096, f)) > 0) in.insert (in.end(), buf, buf + r);
    std::fclose (f);
    const size_t frames = in.size() / 2;
    std::vector<float> L (frames), R (frames);
    for (size_t i = 0; i < frames; ++i) { L[i] = in[2 * i]; R[i] = in[2 * i + 1]; }
    hc::HarmonicColor m; m.prepare (fs); m.set (s, true); m.reset();
    std::vector<float> blockUs;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t pos = 0; pos < frames; pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, frames - pos);
        const auto b0 = std::chrono::high_resolution_clock::now();
        m.process (L.data() + pos, R.data() + pos, n, level);
        blockUs.push_back ((float) std::chrono::duration<double, std::micro> (std::chrono::high_resolution_clock::now() - b0).count());
    }
    const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    std::vector<float> out (frames * 2);
    for (size_t i = 0; i < frames; ++i) { out[2 * i] = L[i]; out[2 * i + 1] = R[i]; }
    FILE* o = std::fopen (argv[2], "wb"); std::fwrite (out.data(), sizeof (float), out.size(), o); std::fclose (o);
    std::sort (blockUs.begin(), blockUs.end());
    std::fprintf (stderr, "cpu %.3f %% latency %d residue %.1f dB comp %.2f dB guard %.2f dB p99.9 block %.1f %%\n", 100.0 * sec / (frames / fs), m.latencySamples(),
                  m.meterResidue.load(), m.meterComp.load(), m.meterGuard.load(), 100.0 * blockUs[(size_t) (0.999 * (blockUs.size() - 1))] / (1.0e6 * block / fs));
    return 0;
}
