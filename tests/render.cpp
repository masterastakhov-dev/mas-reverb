// Offline test harness for the MAS Reverb engine.
// usage: render <in.f32> <out.f32> <sampleRate> <preMs> <size> <decay> <damp> <lowcut> <mod> <mix> [block]
// Files are raw float32, interleaved stereo.
#include "../Source/MASReverbEngine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#if defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

int main (int argc, char** argv)
{
    if (argc < 11) { std::fprintf (stderr, "bad args\n"); return 1; }
#if defined(__x86_64__)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> data;
    float buf[4096];
    size_t r;
    while ((r = std::fread (buf, sizeof (float), 4096, f)) > 0) data.insert (data.end(), buf, buf + r);
    std::fclose (f);

    const double fs = std::atof (argv[3]);
    mas::ReverbParams p;
    p.preDelayMs = (float) std::atof (argv[4]);
    p.size       = (float) std::atof (argv[5]);
    p.decaySec   = (float) std::atof (argv[6]);
    p.hfDamp     = (float) std::atof (argv[7]);
    p.lowCutHz   = (float) std::atof (argv[8]);
    p.mod        = (float) std::atof (argv[9]);
    p.mix        = (float) std::atof (argv[10]);
    const int block = argc > 11 ? std::atoi (argv[11]) : 256;

    mas::ReverbEngine eng;
    eng.setParams (p);
    eng.prepare (fs);

    const size_t frames = data.size() / 2;
    std::vector<float> L (frames), R (frames);
    for (size_t i = 0; i < frames; ++i) { L[i] = data[2 * i]; R[i] = data[2 * i + 1]; }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t pos = 0; pos < frames; pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, frames - pos);
        eng.process (L.data() + pos, R.data() + pos, n);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    const double secs = std::chrono::duration<double> (t1 - t0).count();
    std::fprintf (stderr, "realtime factor: %.1fx (%.3f s for %.2f s audio)\n",
                  (frames / fs) / secs, secs, frames / fs);

    for (size_t i = 0; i < frames; ++i) { data[2 * i] = L[i]; data[2 * i + 1] = R[i]; }
    f = std::fopen (argv[2], "wb");
    std::fwrite (data.data(), sizeof (float), data.size(), f);
    std::fclose (f);
    return 0;
}
