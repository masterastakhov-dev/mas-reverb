// MASTER SPACE 1.5 — SPECTRAL ANALYZER 2.0 (editor side; nothing here touches the audio)
//   * SpectrumEngine: FFT of the newest block of a DSP tap, time-averaged power spectrum,
//     fractional-octave smoothing for the picture only.
//   * PeakDetector: works on the UNSMOOTHED averaged spectrum. Finds narrow peaks, measures their level,
//     prominence over the neighbourhood and width, and keeps only those that stay at the same frequency:
//     lone peaks for >= 1 s, members of a harmonic series (musical notes and their overtones) for >= 3 s.
//     It shows candidates; it never changes the EQ or the sound.

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace spec
{
struct Peak
{
    int id = 0;
    float freq = 0, levelDb = -200, promDb = 0, bwHz = 0, q = 0, age = 0;
    bool harmonic = false;
#ifdef MAS_STUDIO_DYN
    bool fundamental = false;   // root of a detected harmonic family (overtones are marked separately)
#endif
};

class PeakDetector
{
public:
    int maxMarkers = 6;
    float minPromDb = 9.0f, floorDb = -84.0f, lonePeakSeconds = 1.0f, harmonicSeconds = 3.0f;
#ifdef MAS_STUDIO_DYN
    float markerHoldSeconds = 3.0f; // keep a CONFIRMED marker actionable after the last matching peak
#endif

    void reset() { tracks.clear(); shown.clear(); }

    // power[i]: averaged power of bin i (a full-scale sine reads 1.0), nBins = N/2, hz = bin width
    void process (const float* power, int nBins, double hz, double fs, double dt)
    {
        const int lo = std::max (4, (int) std::ceil (60.0 / hz)), hi = std::min (nBins - 4, (int) (std::min (16000.0, 0.45 * fs) / hz));
        if ((int) cum.size() != nBins + 1) cum.assign ((size_t) nBins + 1, 0.0);
        for (int i = 0; i < nBins; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + power[i];
        auto mean = [&] (double f0, double f1)
        {
            const int a = std::clamp ((int) std::floor (f0 / hz), 1, nBins - 1), b = std::clamp ((int) std::ceil (f1 / hz), a + 1, nBins);
            return (cum[(size_t) b] - cum[(size_t) a]) / (double) (b - a);
        };

        cand.clear();
        for (int i = lo; i < hi; ++i)
        {
            const float p = power[i];
            if (! (p > power[i - 1] && p >= power[i + 1] && p > power[i - 2] && p > power[i + 2])) continue;
            const float lev = 10.0f * std::log10 (p + 1e-20f);
            if (lev < floorDb) continue;
            const double f = i * hz;
            const double base = 0.5 * (mean (f * 0.7937, f * 0.9576) + mean (f * 1.0443, f * 1.2599));   // 1/3 oct around, 1/16 oct excluded
            const float prom = lev - (float) (10.0 * std::log10 (base + 1e-20));
            if (prom < minPromDb) continue;
            // refine the frequency (parabola through the dB values) and measure the -3 dB width
            const double dl = 10.0 * std::log10 (power[i - 1] + 1e-20), dc = lev, dr = 10.0 * std::log10 (power[i + 1] + 1e-20);
            const double den = dl - 2.0 * dc + dr, off = std::abs (den) > 1e-9 ? std::clamp (0.5 * (dl - dr) / den, -0.5, 0.5) : 0.0;
            const float half = 0.5f * p;
            double l = i, r = i;
            for (int k = i; k > std::max (1, i - 400); --k) if (power[k - 1] < half) { l = k - (power[k] - half) / std::max (1e-20f, power[k] - power[k - 1]); break; } else l = k - 1;
            for (int k = i; k < std::min (nBins - 2, i + 400); ++k) if (power[k + 1] < half) { r = k + (power[k] - half) / std::max (1e-20f, power[k] - power[k + 1]); break; } else r = k + 1;
            const double bw = (r - l) * hz, win = 1.44 * hz;                       // Hann main lobe at -3 dB
            const double bwEff = std::sqrt (std::max (bw * bw - win * win, 0.09 * hz * hz));
            Peak pk; pk.freq = (float) ((i + off) * hz); pk.levelDb = lev; pk.promDb = prom; pk.bwHz = (float) bwEff; pk.q = (float) (pk.freq / bwEff);
            cand.push_back (pk);
        }
        std::sort (cand.begin(), cand.end(), [] (const Peak& a, const Peak& b) { return a.promDb > b.promDb; });
        // one peak can come out as two neighbouring maxima: keep the stronger one
        for (size_t i = 0; i < cand.size(); ++i)
            for (size_t j = i + 1; j < cand.size();)
                if (std::abs (std::log2 (cand[j].freq / cand[i].freq)) < 0.045f) cand.erase (cand.begin() + (long) j); else ++j;
        if (cand.size() > 32) cand.resize (32);

        for (auto& t : tracks) t.hit = false;
        for (const auto& c : cand)
        {
            Track* best = nullptr; float bd = 0.045f;
            for (auto& t : tracks) { const float d = std::abs (std::log2 (c.freq / t.p.freq)); if (! t.hit && d < bd) { bd = d; best = &t; } }
            if (best == nullptr) { Track t; t.p = c; t.p.id = ++nextId; t.p.age = 0; t.hit = true; t.miss = 0; tracks.push_back (t); continue; }
            auto& p = best->p;
            p.freq += 0.3f * (c.freq - p.freq); p.levelDb += 0.4f * (c.levelDb - p.levelDb); p.promDb += 0.3f * (c.promDb - p.promDb);
            p.bwHz += 0.3f * (c.bwHz - p.bwHz); p.q = p.freq / std::max (1e-3f, p.bwHz);
            p.age += (float) dt; best->hit = true; best->miss = 0;
        }
#ifdef MAS_STUDIO_DYN
        // Confirmed peaks remain visible and clickable for 3 seconds after their last match.
        for (auto& t : tracks) if (! t.hit)
        {
            t.miss += (float) dt;
            t.p.age = std::max (0.0f, t.p.age - 2.0f * (float) dt);
            t.linger = std::max (0.0f, t.linger - (float) dt);
        }
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [] (const Track& t)
        { return t.miss > 0.25f && t.linger <= 0.0f; }), tracks.end());
#else
        for (auto& t : tracks) if (! t.hit) { t.miss += (float) dt; t.p.age = std::max (0.0f, t.p.age - 2.0f * (float) dt); }
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [] (const Track& t) { return t.miss > 0.25f; }), tracks.end());
#endif

        // harmonic series: notes and their overtones have to hold much longer before they are shown
        // (a series = a root with at least two whole-number multiples among the peaks present right now; two
        // resonances that just happen to sit an octave or two apart are not a series)
#ifdef MAS_STUDIO_DYN
        for (auto& t : tracks) if (t.hit) { t.p.harmonic = false; t.p.fundamental = false; }
#else
        for (auto& t : tracks) t.p.harmonic = false;
#endif
        for (auto& u : tracks)
        {
            if (! u.hit) continue;
            int multiples = 0;
            for (const auto& t : tracks)
            {
                if (&u == &t || ! t.hit || t.p.freq < u.p.freq * 1.5f) continue;
                const float ratio = t.p.freq / u.p.freq, n = std::round (ratio);
                if (n >= 2.0f && n <= 16.0f && std::abs (ratio - n) < 0.02f * n) ++multiples;
            }
            if (multiples < 2) continue;
            u.p.harmonic = true;
#ifdef MAS_STUDIO_DYN
            u.p.fundamental = true;
#endif
            for (auto& t : tracks)
            {
                const float ratio = t.p.freq / u.p.freq, n = std::round (ratio);
                if (n >= 2.0f && n <= 16.0f && std::abs (ratio - n) < 0.02f * n) t.p.harmonic = true;
            }
        }
        shown.clear();
#ifdef MAS_STUDIO_DYN
        for (auto& t : tracks)
        {
            if (t.hit && t.p.age >= (t.p.harmonic ? harmonicSeconds : lonePeakSeconds))
                t.linger = markerHoldSeconds;
            if (t.linger > 0.0f) shown.push_back (t.p);
        }
        std::sort (shown.begin(), shown.end(), [] (const Peak& a, const Peak& b)
        { return a.fundamental != b.fundamental ? a.fundamental : a.promDb > b.promDb; });
#else
        for (const auto& t : tracks)
            if (t.hit && t.p.age >= (t.p.harmonic ? harmonicSeconds : lonePeakSeconds)) shown.push_back (t.p);
        std::sort (shown.begin(), shown.end(), [] (const Peak& a, const Peak& b) { return a.promDb > b.promDb; });
#endif
        if ((int) shown.size() > maxMarkers) shown.resize ((size_t) maxMarkers);
    }

    const std::vector<Peak>& markers() const { return shown; }
    int numTracks() const { return (int) tracks.size(); }

private:
#ifdef MAS_STUDIO_DYN
    struct Track { Peak p; bool hit = false; float miss = 0, linger = 0; };
#else
    struct Track { Peak p; bool hit = false; float miss = 0; };
#endif
    std::vector<Track> tracks;
    std::vector<Peak> cand, shown;
    std::vector<double> cum;
    int nextId = 0;
};

// Q proposed for an EQ band from a measured peak (kept in a safe musical range)
inline float suggestQ (const Peak& p) { return std::clamp (p.q, 2.0f, 12.0f); }

//==============================================================================
class SpectrumEngine
{
public:
    void setOrder (int order)
    {
        if (order == fftOrder) return;
        fftOrder = order; N = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        buf.assign ((size_t) (2 * N), 0.0f); win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.283185307f * (float) i / (float) (N - 1));
        clear();
    }
    void clear() { avg.assign ((size_t) (N / 2), 0.0f); slow.assign ((size_t) (N / 2), 0.0f); cum.assign ((size_t) (N / 2) + 1, 0.0); primed = false; detector.reset(); }
    int size() const { return N; }
    float* input() { return buf.data(); }                    // write N newest samples here, then call analyse

    // avgSeconds: time averaging of the picture; the detector uses its own, slower average of the raw bins
    void analyse (double fs, double dt, double avgSeconds, bool detect)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] *= win[(size_t) i];
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (buf.data());
        const float norm = 4.0f / (float) N, n2 = norm * norm;                    // full-scale sine -> power 1
        const float a = primed ? (float) (1.0 - std::exp (-dt / std::max (0.01, avgSeconds))) : 1.0f;
        const float b = primed ? (float) (1.0 - std::exp (-dt / 0.25)) : 1.0f;
        const int nb = N / 2;
        for (int i = 0; i < nb; ++i)
        {
            const float p = buf[(size_t) i] * buf[(size_t) i] * n2;
            avg[(size_t) i] += a * (p - avg[(size_t) i]);
            slow[(size_t) i] += b * (p - slow[(size_t) i]);
        }
        primed = true;
        for (int i = 0; i < nb; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + avg[(size_t) i];
        sampleRate = fs;
        if (detect) detector.process (slow.data(), nb, fs / N, fs, dt);
    }

    // level for the picture at a frequency: fracOct = 0 -> the raw bins (peak of the bins the pixel covers),
    // otherwise the mean power in a band fracOct octaves wide. f0..f1 = the frequencies the pixel covers.
    float displayDb (double f0, double f1, double fracOct) const
    {
        const double hz = sampleRate / N; const int nb = N / 2;
        if (f0 >= 0.5 * sampleRate) return -200.0f;
        if (fracOct <= 0.0)
        {
            const double p0 = f0 / hz, p1 = f1 / hz;
            float p = 0.0f;
            if (p1 - p0 < 1.0) { const double pc = 0.5 * (p0 + p1); const int i = std::clamp ((int) pc, 0, nb - 2); const float fr = (float) (pc - i); p = avg[(size_t) i] + fr * (avg[(size_t) i + 1] - avg[(size_t) i]); }
            else for (int i = (int) std::ceil (p0); i <= (int) p1 && i < nb; ++i) p = std::max (p, avg[(size_t) i]);
            return 10.0f * std::log10 (p + 1e-20f);
        }
        const double fc = std::sqrt (f0 * f1), k = std::pow (2.0, 0.5 * fracOct);
        double a = fc / k / hz, b = fc * k / hz;
        if (b - a < 1.0) { const double c = 0.5 * (a + b); a = c - 0.5; b = c + 0.5; }
        a = std::clamp (a, 0.0, (double) nb - 1.0); b = std::clamp (b, a + 1e-6, (double) nb);
        auto at = [&] (double x) { const int i = std::min ((int) x, nb - 1); return cum[(size_t) i] + (x - i) * avg[(size_t) i]; };
        return (float) (10.0 * std::log10 ((at (b) - at (a)) / (b - a) + 1e-20));
    }

    PeakDetector detector;

private:
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> buf, win, avg, slow;
    std::vector<double> cum;
    int fftOrder = 0, N = 0;
    bool primed = false;
    double sampleRate = 48000.0;
};
} // namespace spec
