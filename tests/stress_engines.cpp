// Random automation of every parameter on both engines (1.2 and 1.1 Legacy): no runaway allowed.
// usage: stress_engines [param index or -1] [seeds]
#include "../Source/MasterSpace/MSEngine.h"
#include "../Source/MasterSpace/legacy/MSEngine11.h"
#include <cstdio>
#include <random>
#include <string>
#include <xmmintrin.h>
#include <pmmintrin.h>

template <class P> void randomise (P& p, std::mt19937& g, int which, std::string& name)
{
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    auto lg = [&] (float lo, float hi) { return lo * std::pow (hi / lo, u (g)); };
    switch (which)
    {
        case 0: p.algorithm = (int) (u (g) * 2.999f); name = "algorithm"; break;
        case 1: p.routing = u (g) > 0.5f; name = "routing"; break;
        case 2: p.depth = u (g); name = "depth"; break;
        case 3: p.decay = lg (0.1f, 30.0f); name = "decay"; break;
        case 4: p.size = u (g); name = "size"; break;
        case 5: p.preDelayMs = u (g) * 500; name = "predelay"; break;
        case 6: p.erLevelDb = -60 + 72 * u (g); name = "erlevel"; break;
        case 7: p.lateLevelDb = -60 + 72 * u (g); name = "latelevel"; break;
        case 8: p.diffusion = u (g); name = "diffusion"; break;
        case 9: p.density = u (g); name = "density"; break;
        case 10: p.shape = u (g); name = "shape"; break;
        case 11: p.spreadMs = 500 * u (g); name = "spread"; break;
        case 12: p.width = 2 * u (g); name = "width"; break;
        case 13: p.mix = u (g); name = "mix"; break;
        case 14: p.outputDb = -24 + 36 * u (g); name = "output"; break;
        case 15: p.lockLevel = u (g) > 0.5f; name = "locklevel"; break;
        case 16: p.lockTone = u (g) > 0.5f; name = "locktone"; break;
        case 17: p.lockDecay = u (g) > 0.5f; name = "lockdecay"; break;
        case 18: p.lockWidth = u (g) > 0.5f; name = "lockwidth"; break;
        case 19: p.roomW = lg (2, 40); name = "roomw"; break;
        case 20: p.roomD = lg (2, 60); name = "roomd"; break;
        case 21: p.roomH = lg (2, 20); name = "roomh"; break;
        case 22: p.srcX = u (g); name = "srcx"; break;
        case 23: p.lisX = u (g); name = "lisx"; break;
        case 24: p.lisY = 0.1f + 0.85f * u (g); name = "lisy"; break;
        case 25: p.materials[(size_t) (u (g) * 5.99f)] = (int) (u (g) * 5.99f); name = "material"; break;
        case 26: p.erOn = u (g) > 0.3f; name = "eron"; break;
        case 27: p.erDensity = u (g); name = "erdensity"; break;
        case 28: p.erDiffusion = u (g); name = "erdiffusion"; break;
        case 29: p.erColor = 2 * u (g) - 1; name = "ercolor"; break;
        case 30: p.erWidth = 2 * u (g); name = "erwidth"; break;
        case 31: p.erStartMs = 100 * u (g); name = "erstart"; break;
        case 32: p.erStopMs = 10 + 290 * u (g); name = "erstop"; break;
        case 33: p.erPreDelayMs = 200 * u (g); name = "erpredelay"; break;
        case 34: p.lateOn = u (g) > 0.3f; name = "lateon"; break;
        case 35: p.lateDelayMs = 300 * u (g); name = "latedelay"; break;
        case 36: p.lateWidth = 2 * u (g); name = "latewidth"; break;
        case 37: p.lateColor = 2 * u (g) - 1; name = "latecolor"; break;
        case 38: p.lfMult = lg (0.1f, 4); name = "lfmult"; break;
        case 39: p.lmMult = lg (0.1f, 4); name = "lmmult"; break;
        case 40: p.hmMult = lg (0.1f, 4); name = "hmmult"; break;
        case 41: p.hfMult = lg (0.1f, 4); name = "hfmult"; break;
        case 42: p.xLow = lg (40, 1000); name = "xlow"; break;
        case 43: p.xMid = lg (300, 8000); name = "xmid"; break;
        case 44: p.xHigh = lg (1500, 18000); name = "xhigh"; break;
        case 45: p.lowCutHz = lg (20, 2000); name = "lowcut"; break;
        case 46: p.highCutHz = lg (1000, 20000); name = "highcut"; break;
        case 47: p.modCharacter = (int) (u (g) * 3.99f); name = "modchar"; break;
        case 48: p.modRate = lg (0.01f, 5); name = "modrate"; break;
        case 49: p.modDepth = u (g); name = "moddepth"; break;
        case 50: p.modRandom = u (g); name = "modrandom"; break;
        case 51: p.modSmooth = u (g); name = "modsmooth"; break;
        case 52: p.balance = 2 * u (g) - 1; name = "balance"; break;
        case 53: p.crossfeed = u (g); name = "crossfeed"; break;
        case 54: p.decorrelation = u (g); name = "decorr"; break;
    }
}

template <class E, class P> int stress (const char* tag, unsigned seed, double fs, int only = -1)
{
    E e; P p; e.setParams (p); e.prepare (fs);
    std::mt19937 g (seed);
    std::normal_distribution<float> nd (0, 0.1f);
    std::vector<float> l (64), r (64);
    std::string last[8];
    for (int blk = 0; blk < (int) (fs * 8 / 64); ++blk)
    {
        std::string nm;
        const int w = only >= 0 ? only : (int) (g() % 55);
        randomise (p, g, w, nm);
        for (int k = 7; k > 0; --k) last[k] = last[k - 1];
        last[0] = nm;
        e.setParams (p);
        for (int i = 0; i < 64; ++i) { l[(size_t) i] = nd (g); r[(size_t) i] = nd (g); }
        e.process (l.data(), r.data(), 64);
        float m = 0; for (int i = 0; i < 64; ++i) m = std::max ({ m, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        if (! std::isfinite (m) || m > 1.0e4f)
        {
            std::printf ("%s seed %u: blow-up %.3g at %.3f s; last params:", tag, seed, m, blk * 64 / fs);
            for (auto& s : last) std::printf (" %s", s.c_str());
            std::printf ("\n   decay %.2f size %.2f density %.2f diffusion %.2f mults %.2f %.2f %.2f %.2f x %.0f %.0f %.0f algo %d moddepth %.2f rate %.2f room %.1f %.1f %.1f\n",
                         p.decay, p.size, p.density, p.diffusion, p.lfMult, p.lmMult, p.hmMult, p.hfMult, p.xLow, p.xMid, p.xHigh, p.algorithm,
                         p.modDepth, p.modRate, p.roomW, p.roomD, p.roomH);
            return 1;
        }
    }
    return 0;
}

int main (int argc, char** argv)
{
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
    const int only = argc > 1 ? std::atoi (argv[1]) : -1;
    int n12 = 0, n11 = 0;
    for (unsigned s = 1; s <= (unsigned) (argc > 2 ? std::atoi (argv[2]) : 12); ++s)
    {
        n12 += stress<mspace::Engine, mspace::Params> ("1.2", s, s % 2 ? 48000 : 96000, only);
        n11 += stress<mspace11::Engine, mspace11::Params> ("1.1", s, s % 2 ? 48000 : 96000, only);
    }
    std::printf ("runaways: 1.2 %d, 1.1 Legacy %d\n", n12, n11);
    return n12 + n11 == 0 ? 0 : 1;
}
