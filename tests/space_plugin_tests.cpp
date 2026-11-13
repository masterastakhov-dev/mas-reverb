// Headless tests of the MASTER SPACE plugin processor (state, presets, A/B, transitions, CPU).
// Built with -DMAS_BUILD_TESTS=ON; run: MasterSpaceTests [--quick]
#include "../Source/MasterSpace/PluginProcessor.h"
#ifdef MAS_STUDIO
 #include "../Source/MasterSpace/StudioEditor.h"
#endif
#ifdef MAS_EQ
#include "../Source/MasterSpace/EqLabView.h"
#endif
#include "../Source/MasterSpace/PresetManager.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace
{
    int failures = 0, checks = 0;
    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) ++failures;
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
    }
    void section (const char* s) { std::printf ("\n== %s\n", s); }

    void pump (int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    std::unique_ptr<MasterSpaceAudioProcessor> makeProcessor (double fs, int block)
    {
        auto p = std::make_unique<MasterSpaceAudioProcessor>();
        p->setRateAndBufferSizeDetails (fs, block);
        p->prepareToPlay (fs, block);
        return p;
    }

    // runs the processor over a stereo buffer in host-sized blocks (in place)
    void run (MasterSpaceAudioProcessor& p, juce::AudioBuffer<float>& audio, int block)
    {
        juce::MidiBuffer midi;
        for (int pos = 0; pos < audio.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, audio.getNumSamples() - pos);
            juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, pos, n);
            p.processBlock (view, midi);
        }
    }

    juce::AudioBuffer<float> silence (double fs, double secs)
    {
        juce::AudioBuffer<float> b (2, (int) (fs * secs));
        b.clear();
        return b;
    }

    juce::AudioBuffer<float> pinkish (double fs, double secs, int seed = 1)
    {
        juce::AudioBuffer<float> b (2, (int) (fs * secs));
        juce::Random r (seed);
        float s = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            s = 0.97f * s + 0.03f * (r.nextFloat() * 2.0f - 1.0f);
            const float v = s * 2.0f + 0.05f * (r.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, v * 0.5f);
            b.setSample (1, i, v * 0.5f);
        }
        return b;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i))) return false;
        return true;
    }

    float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float m = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
                m = juce::jmax (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        return m;
    }

    float peak (const juce::AudioBuffer<float>& b) { return juce::jmax (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples())); }

    bool sameParams (const ParamMap& a, const ParamMap& b, juce::String* firstDiff = nullptr)
    {
        for (const auto& [id, v] : a)
        {
            const auto it = b.find (id);
            if (it == b.end() || std::abs (it->second - v) > 1.0e-3f * juce::jmax (1.0f, std::abs (v)))
            {
                if (firstDiff) *firstDiff = id + ": " + juce::String (v) + " vs " + (it == b.end() ? juce::String ("missing") : juce::String (it->second));
                return false;
            }
        }
        return true;
    }

    // impulse response after a clean restart of the audio state (reset), so two processors in the same
    // parameter state must produce the same output
    juce::AudioBuffer<float> impulseAfterReset (MasterSpaceAudioProcessor& p, double fs, int block, double secs)
    {
        p.reset();
        auto b = silence (fs, secs);
        b.setSample (0, 64, 0.5f);
        b.setSample (1, 64, 0.5f);
        run (p, b, block);
        return b;
    }

    double nowMs() { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

// --snapshot <dir>: renders the editor pages to PNG files (layout check without a display)
static int snapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto p = std::make_unique<MasterSpaceAudioProcessor>();
    p->setRateAndBufferSizeDetails (48000, 512);
    p->prepareToPlay (48000, 512);
    auto& pm = p->getPresetManager();
    pm.load (pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room 1.2 Open"));
   #ifdef MAS_RI
    {
        // modules on and some audio through them, so the 1.4 displays show real values
        auto set = [&] (const char* id, float plain) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        set (ParamIDs::duckOn, 1.0f); set (ParamIDs::rgOn, 1.0f); set (ParamIDs::rgMax, 6.0f); set (ParamIDs::rgTarget, 0.0f);
        set (ParamIDs::hcOn, 1.0f); set (ParamIDs::hcChar, 1.0f); set (ParamIDs::hcDrive, 55.0f);
        pump (100);
        juce::AudioBuffer<float> b (2, 48000 * 3);
        juce::Random rng (3);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / 48000.0;
            const bool phrase = std::fmod (t, 1.2) < 0.8;
            const float v = phrase ? (float) (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                              + 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * 1310.0 * t) + 0.03 * (rng.nextFloat() - 0.5f)) : 0.0f;
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        run (*p, b, 512);
    }
   #endif
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (1000, 680);
    pump (200);

    std::function<juce::Button* (juce::Component&, const juce::String&)> findButton = [&] (juce::Component& c, const juce::String& text) -> juce::Button*
    {
        for (auto* ch : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (ch)) if (b->getButtonText() == text) return b;
            if (auto* r = findButton (*ch, text)) return r;
        }
        return nullptr;
    };
    auto shot = [&] (const juce::String& name)
    {
        pump (300);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream fo (dir.getChildFile (name + ".png"));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("  wrote %s.png\n", name.toRawUTF8());
    };
   #ifdef MAS_EQ
    {
        // LIVING SPACE runs only while it is shown and fed by the audio thread: a vocal-like phrase, then its tail
        ed->setVisible (true);
        if (auto* r = p->apvts.getParameter (ParamIDs::algorithm)) r->setValueNotifyingHost (1.0f);       // HALL
        if (auto* r = p->apvts.getParameter (ParamIDs::decay)) r->setValueNotifyingHost (r->convertTo0to1 (2.8f));
        if (auto* r = p->apvts.getParameter (ParamIDs::mix)) r->setValueNotifyingHost (r->convertTo0to1 (40.0f));
        pump (300);
        for (int k = 0; k < 40; ++k)
        {
            auto sig = pinkish (48000, 0.05, 30 + k);
            sig.applyGain ((k % 8) < 5 ? 0.5f : 0.0f);
            run (*p, sig, 512); pump (50);
        }
        shot ("1_main");
        if (auto* r = p->apvts.getParameter (ParamIDs::tail)) r->setValueNotifyingHost (1.0f);            // ATMOSPHERIC TAIL
        if (auto* r = p->apvts.getParameter (ParamIDs::decay)) r->setValueNotifyingHost (r->convertTo0to1 (12.0f));
        pump (700);
        for (int k = 0; k < 50; ++k) { auto sig = pinkish (48000, 0.05, 80 + k); sig.applyGain (k < 30 ? 0.4f : 0.0f); run (*p, sig, 512); pump (50); }
        shot ("1_main_atmos");
        if (auto* r = p->apvts.getParameter (ParamIDs::tail)) r->setValueNotifyingHost (0.0f);
        pump (300);
    }
   #else
    shot ("1_main");
   #endif
   #ifdef MAS_RI
    for (const char* tab : { "ADVANCED", "DUCKER", "GUARD", "COLOR", "PRESETS" })
   #else
    for (const char* tab : { "ADVANCED", "PRESETS" })
   #endif
        if (auto* b = findButton (*ed, tab)) { b->triggerClick(); pump (200); shot (juce::String ("2_") + tab); }
   #ifdef MAS_EQ
    {
        auto set = [&] (const juce::String& id, float plain) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        auto band = [&] (int bank, int i, int type, float f, float g, float q, int slope = 1)
        {
            using namespace ParamIDs;
            set (eqId (bank, i, eqUsed), 1); set (eqId (bank, i, eqType), (float) type); set (eqId (bank, i, eqFreq), f);
            set (eqId (bank, i, eqGain), g); set (eqId (bank, i, eqQ), q); set (eqId (bank, i, eqSlope), (float) slope);
        };
        set (ParamIDs::eqOn, 1);
        band (0, 0, req::LowShelf, 100, 2, 0.71f); band (0, 1, req::Bell, 350, -3, 1.2f);
        band (0, 2, req::Bell, 2600, 3, 0.8f); band (0, 3, req::HighShelf, 9000, -2.5f, 0.71f);
        band (2, 0, req::LowCut, 120, 0, 0.71f, 1); band (2, 3, req::HighCut, 9000, 0, 0.71f, 2);
        if (auto* b = findButton (*ed, "REVERB EQ"))
        {
            ed->setVisible (true);              // off-screen, but the analyzer only runs while its page is shown
            b->triggerClick(); pump (300);
            req::BandParams rb[8];
            const float rf[3] = { 387.0f, 1200.0f, 4800.0f };
            for (int i = 0; i < 3; ++i) { rb[i].used = true; rb[i].freq = rf[i]; rb[i].gainDb = 18.0f; rb[i].q = 18.0f; }
            req::Bank res; res.prepare (48000.0); res.set (rb, true, true);
            for (int k = 0; k < 70; ++k)
            {
                auto sig = pinkish (48000, 1600.0 / 48000.0, 20 + k);
                res.process (sig.getWritePointer (0), sig.getWritePointer (1), sig.getNumSamples());
                run (*p, sig, 512); pump (34);
            }
            shot ("2_REVERB_EQ");
        }
    }
   #endif
   #ifdef MAS_RI
    if (auto* b = findButton (*ed, "COLOR"))
    {
        if (auto* r = p->apvts.getParameter (ParamIDs::hcChar)) r->setValueNotifyingHost (r->convertTo0to1 (2.0f));
        b->triggerClick(); pump (200); shot ("2_COLOR_TAPE");
    }
   #endif
    if (auto* b = findButton (*ed, "B")) { b->triggerClick(); pump (200); }
    if (auto* b = findButton (*ed, "SAVE AS")) { b->triggerClick(); pump (200); shot ("3_save_as"); }
    ed.reset();
    return 0;
}

#ifdef MAS_STUDIO
// --studio-snapshot <dir>: the studio editor at 100 % and at 2x (Retina): MAIN, MAIN + DEPTH VIEW, REVERB EQ, PRO pages, themes
static int studioSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto p = std::make_unique<MasterSpaceAudioProcessor>();
    p->setRateAndBufferSizeDetails (48000, 512);
    p->prepareToPlay (48000, 512);
    auto& pm = p->getPresetManager();
    pm.load (pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room 1.2 Open"));
    auto set = [&] (const juce::String& id, float plain) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
    set (ParamIDs::algorithm, 2.0f); set (ParamIDs::decay, 2.8f); set (ParamIDs::mix, 32.0f); set (ParamIDs::depth, 42.0f);
    set (ParamIDs::erLevel, -3.0f); set (ParamIDs::lateLevel, -1.5f); set (ParamIDs::preDelay, 18.0f); set (ParamIDs::rgOn, 1.0f);
    pump (100);
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor());
    auto* ed = dynamic_cast<studio::Editor*> (edp.get());
    if (ed == nullptr) { std::printf ("not a studio editor\n"); return 1; }
    auto& panel = ed->getPanel();
    ed->setVisible (true);
    panel.setPro (false); panel.setDrawer (false, false); panel.setPage (0);
    ed->setSize (1100, panel.logicalHeight());
    auto feed = [&] (int blocks)
    {
        for (int k = 0; k < blocks; ++k)
        {
            auto sig = pinkish (48000, 0.05, 30 + k);
            sig.applyGain ((k % 8) < 5 ? 0.5f : 0.0f);
            run (*p, sig, 512); pump (34);
        }
    };
    auto shot = [&] (const juce::String& name)
    {
        pump (200);
        for (float sc : { 1.0f, 2.0f })
        {
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, sc);
            juce::FileOutputStream fo (dir.getChildFile (name + (sc > 1.5f ? "@2x" : "") + ".png"));
            fo.setPosition (0); fo.truncate();
            juce::PNGImageFormat().writeImageToStream (img, fo);
        }
        std::printf ("  wrote %s (%dx%d)\n", name.toRawUTF8(), ed->getWidth(), ed->getHeight());
    };
    feed (20);
    shot ("01_MAIN");
#ifdef MAS_STUDIO_DYN
    {
        // brand emblem in place of the old wave: red disc above the wordmark, no cyan wave line left there
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        int red = 0, cyan = 0;
        for (int y = 24; y < 62; ++y)
            for (int x = 36; x < 196; ++x)
            {
                const auto c = img.getPixelAt (x, y);
                if (c.getRed() > 80 && c.getRed() > c.getGreen() + 35 && c.getRed() > c.getBlue() + 35) ++red;   // muted red disc
                if (c.getBlue() > 170 && c.getGreen() > 140 && c.getRed() < 120) ++cyan;
            }
       #ifdef MAS_STUDIO_18
        const bool ok = red == 0 && cyan == 0;        // 1.8: the corner carries only the slogan
       #else
        const bool ok = red > 40 && cyan == 0;
       #endif
        std::printf ("  [%s] header: brand emblem above the wordmark (red %d px), no wave (cyan %d px)\n", ok ? " ok " : "FAIL", red, cyan);
        if (! ok) return 1;
    }
    {
        // SIGNAL RAILS with real audio through the plugin: silence, a medium programme, peaks near 0 dBFS.
        // The rails are found in the 1x picture and checked: dark in silence, lit without red at a medium level,
        // red peak markers (>= -3 dBFS) right after the peaks.
        int railFails = 0;
        int seed = 500;
        float lastOutPeak = 0.0f;
        auto feedLevel = [&] (float rmsDb, float transientDb, double secs)
        {
            for (int k = 0; k < (int) (secs / 0.05); ++k)
            {
                auto sig = pinkish (48000, 0.05, ++seed);
                double e = 0.0;
                for (int i = 0; i < sig.getNumSamples(); ++i) e += (double) sig.getSample (0, i) * sig.getSample (0, i);
                const float rms = (float) std::sqrt (e / sig.getNumSamples());
                sig.applyGain (rmsDb <= -119.0f || rms <= 0.0f ? 0.0f : juce::Decibels::decibelsToGain (rmsDb) / rms);
                if (transientDb > -119.0f && k % 4 == 0)          // a 2 ms, 1 kHz burst: a real transient, not one sample
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 96; ++i)
                            sig.setSample (c, 777 + i, juce::Decibels::decibelsToGain (transientDb) * (float) std::sin (2.0 * 3.141592653589793 * (i + 12) / 48.0));
                run (*p, sig, 512); pump (34);
                if (k == 0) lastOutPeak = 0.0f;
                if (k >= (int) (secs / 0.05) - 14) lastOutPeak = juce::jmax (lastOutPeak, peak (sig));   // within the 0.8 s hold
            }
        };
        auto railStats = [&] (const juce::Image& img, int x0, bool gold, int& lit, int& red)
        {
            lit = red = 0;
            const int y0 = panel.getY() + 152 + 248 + 16;     // MAIN page y + rail y + slot y (1x)
            for (int y = y0 + 2; y < y0 + 15; ++y)
                for (int x = x0 + 9; x < x0 + 168; ++x)
                {
                    const auto c = img.getPixelAt (x, y);
                    const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
                    if (r > 200 && g < 120 && b < 110) ++red;
                    else if (gold ? (r > 150 && g > 80 && b < 120 && r > b + 70) : (b > 150 && b > r + 60)) ++lit;
                }
        };
        auto railShot = [&] (const juce::String& name, int wantLit, bool wantInRed)
        {
            // OUTPUT is red exactly when what leaves the plugin peaked at / above -3 dBFS within the hold
            const bool wantOutRed = juce::Decibels::gainToDecibels (lastOutPeak, -120.0f) >= mas::RailBallistics::kOverDb;
            shot (name);
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            int li, ri, lo, ro;
            railStats (img, 72, false, li, ri);
            railStats (img, 852, true, lo, ro);
            const bool ok = (wantLit == 0 ? li == 0 && lo == 0 : li > wantLit && lo > wantLit) && ((ri > 0) == wantInRed) && ((ro > 0) == wantOutRed);
            std::printf ("  [%s] %s: INPUT lit %d red %d, OUTPUT lit %d red %d (output peak %.1f dBFS)\n", ok ? " ok " : "FAIL", name.toRawUTF8(), li, ri, lo, ro,
                         juce::Decibels::gainToDecibels (lastOutPeak, -120.0f));
            if (! ok) ++railFails;
        };
        feedLevel (-120.0f, -120.0f, 4.0);  railShot ("R1_RAILS_SILENCE", 0, false);
        feedLevel (-22.0f, -120.0f, 2.0);   railShot ("R2_RAILS_MEDIUM", 200, false);
        feedLevel (-12.0f, -0.5f, 1.0);     railShot ("R3_RAILS_PEAKS", 400, true);
        // the plugin OUTPUT pushed +6 dB: what leaves the plugin reaches the red zone as well
        set (ParamIDs::output, 6.0f);
        feedLevel (-12.0f, -0.5f, 1.5);     railShot ("R4_RAILS_OUTPUT_OVER", 400, true);
        set (ParamIDs::output, 0.0f);
        feedLevel (-120.0f, -120.0f, 2.0);
        if (std::getenv ("RAILS_ONLY")) return railFails;
        if (railFails > 0) return railFails;
    }
#endif
#ifdef MAS_STUDIO_DYN
    // The lamp is UI-only: photograph identical grille geometry when off and when warmed up.
    set (ParamIDs::hcOn, 0.0f);
    feed (70);
    shot ("01a_ANALOG_GRILLES_LAMP_OFF");
    set (ParamIDs::hcDrive, 50.0f);
    set (ParamIDs::hcMix, 0.0f);
    set (ParamIDs::hcOn, 1.0f);
    feed (25);
    shot ("01b0_ANALOG_GRILLES_LAMP_ON_MIX0");
    set (ParamIDs::hcMix, 35.0f);
    feed (25);
    shot ("01b_ANALOG_GRILLES_LAMP_ON_MIX35");
    set (ParamIDs::hcMix, 100.0f);
    feed (25);
    shot ("01c_ANALOG_GRILLES_LAMP_ON_MIX100");
    set (ParamIDs::hcOn, 0.0f);
#endif
    panel.setDrawer (true, false);
    feed (30);
    shot ("02_MAIN_DEPTH_VIEW");
    // a few REVERB EQ bands and resonances for the detector
    {
        auto band = [&] (int bank, int i, int type, float f, float g, float q, int slope = 1)
        {
            using namespace ParamIDs;
            set (eqId (bank, i, eqUsed), 1); set (eqId (bank, i, eqType), (float) type); set (eqId (bank, i, eqFreq), f);
            set (eqId (bank, i, eqGain), g); set (eqId (bank, i, eqQ), q); set (eqId (bank, i, eqSlope), (float) slope);
        };
        set (ParamIDs::eqOn, 1);
        band (0, 0, req::LowShelf, 100, 2, 0.71f); band (0, 1, req::Bell, 350, -3, 1.2f);
        band (0, 2, req::Bell, 2600, 3, 0.8f); band (0, 3, req::HighShelf, 9000, -2.5f, 0.71f);
        panel.setPage (12);
       #ifdef MAS_STUDIO_DYN
        band (0, 4, req::Bell, 1200, 0, 3.0f);
        set (ParamIDs::eqId (0, 4, ParamIDs::eqDynThreshold), -60.0f); set (ParamIDs::eqId (0, 4, ParamIDs::eqDynMax), 4.0f);
        set (ParamIDs::eqId (0, 4, ParamIDs::eqDynOn), 1.0f);
       #endif
        req::BandParams rb[8];
        const float rf[3] = { 387.0f, 1200.0f, 4800.0f };
        for (int i = 0; i < 3; ++i) { rb[i].used = true; rb[i].freq = rf[i]; rb[i].gainDb = 18.0f; rb[i].q = 18.0f; }
        req::Bank res; res.prepare (48000.0); res.set (rb, true, true);
        for (int k = 0; k < 70; ++k)
        {
            auto sig = pinkish (48000, 1600.0 / 48000.0, 20 + k);
            res.process (sig.getWritePointer (0), sig.getWritePointer (1), sig.getNumSamples());
            run (*p, sig, 512); pump (34);
        }
        shot ("03_REVERB_EQ");
    }
    panel.setPro (true);
    panel.setPage (0);
    shot ("04_MAIN_PRO");
    const char* names[] = { "MAIN", "SPACE", "EARLY", "LATE", "DECAY_EQ", "MOD", "STEREO", "ADVANCED", "ATMOS", "DUCKER", "GUARD", "COLOR", "REVERB_EQ", "PRESETS" };
    for (int i = 1; i < 14; ++i) { panel.setPage (i); feed (3); shot (juce::String ("05_PRO_") + juce::String (i).paddedLeft ('0', 2) + "_" + names[i]); }
   #ifdef MAS_STUDIO_18
    {
        // 1.8: the compressor working (GR on both meters), COLOR in M/S, MAIN with M/S COMP on
        set (ParamIDs::cpOn, 1.0f); set (ParamIDs::cpMidThr, -52.0f); set (ParamIDs::cpSideThr, -56.0f);
        set (ParamIDs::cpMidRatio, 4.0f); set (ParamIDs::cpSideRatio, 3.0f);
        set (ParamIDs::hcOn, 1.0f); set (ParamIDs::hcMode, 1.0f); set (ParamIDs::hcMidDrive, 60.0f); set (ParamIDs::hcSideMix, 45.0f);
        panel.setPage (14); feed (12); shot ("05_PRO_14_COMP");
        panel.setPage (11); feed (3); shot ("05_PRO_11_COLOR_MS");
        panel.setPro (false); panel.setPage (0); feed (8); shot ("08_MAIN_COMP_ON");
        set (ParamIDs::cpOn, 0.0f); set (ParamIDs::hcOn, 0.0f); set (ParamIDs::hcMode, 0.0f);
        feed (4);
    }
   #endif
    panel.setPro (false); panel.setPage (0);
    for (int th = 1; th < studio::kNumThemes; ++th) { panel.setTheme (th); shot (juce::String ("06_THEME_") + studio::theme (th).name); }
    panel.setTheme (0);
    ed->setSize (770, juce::roundToInt (770.0f * (float) panel.logicalHeight() / 1100.0f));
    shot ("07_MAIN_DEPTH_VIEW_70pct");
    edp.reset();
    return 0;
}
#endif

#ifdef MAS_STUDIO_DYN
// --dyn-renders <dir>: listening material for Dynamic Cut. A reverb with a ringing 1.2 kHz resonance
// (a narrow +9 dB WET bell stands in for a room / instrument resonance), rendered with Dynamic Cut OFF and ON.
static int dynRenders (const juce::File& dir)
{
    dir.createDirectory();
    const double fs = 48000.0, pi = juce::MathConstants<double>::pi;
    const int n = (int) (fs * 12.0);
    // source: plucked notes (A minor arpeggio, harmonics) and short noise hits
    juce::AudioBuffer<float> src (2, n); src.clear();
    juce::Random rnd (3);
    const double notes[] = { 220.0, 261.63, 329.63, 440.0, 392.0, 329.63, 293.66, 246.94 };
    for (int k = 0; k < 24; ++k)
    {
        const int start = (int) (k * 0.45 * fs);
        const double f = notes[k % 8] * (k % 3 == 2 ? 2.0 : 1.0);
        for (int i = 0; i < (int) (fs * 1.2) && start + i < n; ++i)
        {
            const double tt = i / fs, env = std::exp (-tt * 4.0);
            double v = 0; for (int h = 1; h <= 8; ++h) v += std::sin (2 * pi * f * h * tt) / (h * h);
            for (int c = 0; c < 2; ++c) src.addSample (c, start + i, (float) (0.25 * env * v));
        }
        if (k % 4 == 0) for (int i = 0; i < 2400 && start + i < n; ++i)
            for (int c = 0; c < 2; ++c) src.addSample (c, start + i, (float) (0.3 * std::exp (-i / 400.0) * (rnd.nextFloat() * 2 - 1)));
    }
    auto renderWith = [&] (bool dyn, const char* name)
    {
        auto p = std::make_unique<MasterSpaceAudioProcessor>();
        p->setRateAndBufferSizeDetails (fs, 512); p->prepareToPlay (fs, 512);
        auto set = [&] (const juce::String& id, float v) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (v)); };
        using namespace ParamIDs;
        set (mix, 35.0f); set (decay, 2.6f); set (algorithm, 2.0f); set (eqOn, 1.0f);
        auto band = [&] (int i, float f, float g, float q, bool d)
        {
            set (eqId (0, i, eqUsed), 1); set (eqId (0, i, eqType), (float) req::Bell); set (eqId (0, i, eqFreq), f);
            set (eqId (0, i, eqGain), g); set (eqId (0, i, eqQ), q); set (eqId (0, i, eqDynOn), d ? 1.0f : 0.0f);
            set (eqId (0, i, eqDynThreshold), -36.0f); set (eqId (0, i, eqDynMax), 3.0f);
        };
        band (1, 1200.0f, 9.0f, 8.0f, false);             // the resonance
        if (dyn) band (4, 1200.0f, 0.0f, 3.0f, true);     // Dynamic Cut on it, default settings
        pump (60);
        auto buf = src;
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 512)
        {
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, juce::jmin (512, n - pos));
            p->processBlock (view, midi);
        }
        auto f = dir.getChildFile (name);
        f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (f), fs, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (buf, 0, n);
        std::printf ("  wrote %s\n", name);
        return buf;
    };
    auto off = renderWith (false, "1_dynamic_cut_OFF.wav");
    auto on = renderWith (true, "2_dynamic_cut_ON.wav");
    juce::AudioBuffer<float> diff (2, n);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) diff.setSample (c, i, off.getSample (c, i) - on.getSample (c, i));
    auto f = dir.getChildFile ("3_difference_what_was_removed.wav");
    f.deleteFile();
    std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (f), fs, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (diff, 0, n);
    return 0;
}
#endif

#ifdef MAS_RI
// Prints a hash of the rendered output for fixed scenarios. Run on the 1.4 and on the 1.5 test binaries:
// with the new EQ off (any variant of "off") every 1.5 hash must equal the 1.4 hash of the same scenario.
static int refHashes (const juce::File& dumpDir)
{
    auto setP = [] (MasterSpaceAudioProcessor& q, const juce::String& id, float plain)
    {
        if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain));
    };
    struct Sc { const char* name; const char* preset; int duck, rg, hc, q, hcChar = 0, hcPos = 0; };
    const Sc scs[] = { { "default", nullptr, 0, 0, 0, 0 }, { "atmos-vocal-glow", "factory:Vocal Spaces/ATMOS Vocal Glow", 0, 0, 0, 0 },
                       { "hall-modules-live", "factory:Halls/Concert Hall", 1, 1, 1, 0 }, { "atmos-modules-hq", "factory:Vocal Spaces/ATMOS Vocal Glow", 1, 1, 1, 1 },
                       // HARMONIC COLOR STEREO in every place and character (1.8 must keep these bit-identical)
                       { "hc-pre-tube-live", "factory:Halls/Concert Hall", 0, 0, 1, 0, 1, 1 },
                       { "hc-post-tape-hq", "factory:Vocal Spaces/ATMOS Vocal Glow", 0, 0, 1, 1, 2, 0 } };
   #ifdef MAS_EQ
    const char* variants[] = { "eq-default", "eq-off-bands-set", "eq-on-no-bands", "eq-on-all-bypassed" };
   #else
    const char* variants[] = { "base" };
   #endif
    for (double fs : { 48000.0, 96000.0 })
        for (const auto& sc : scs)
            for (int v = 0; v < (int) std::size (variants); ++v)
            {
                auto p = makeProcessor (fs, 256);
                if (sc.preset != nullptr) p->getPresetManager().load (p->getPresetManager().indexOf (sc.preset));
                setP (*p, ParamIDs::duckOn, (float) sc.duck); setP (*p, ParamIDs::rgOn, (float) sc.rg);
                setP (*p, ParamIDs::hcOn, (float) sc.hc); setP (*p, ParamIDs::quality, (float) sc.q);
                setP (*p, ParamIDs::hcChar, (float) sc.hcChar); setP (*p, ParamIDs::hcPos, (float) sc.hcPos);
                if (sc.hcChar != 0 || sc.hcPos != 0) setP (*p, ParamIDs::hcDrive, 70.0f);   // the older scenarios stay as they were
               #ifdef MAS_EQ
                if (v >= 1)
                    for (int b = 0; b < 3; ++b)
                        for (int i = 0; i < 8; ++i)
                        {
                            setP (*p, ParamIDs::eqId (b, i, ParamIDs::eqUsed), v == 2 ? 0.0f : 1.0f);
                            setP (*p, ParamIDs::eqId (b, i, ParamIDs::eqGain), 9.0f - 2.0f * i);
                            setP (*p, ParamIDs::eqId (b, i, ParamIDs::eqFreq), 80.0f * (float) std::pow (2.2, i));
                            setP (*p, ParamIDs::eqId (b, i, ParamIDs::eqType), (float) (i % 5));
                            setP (*p, ParamIDs::eqId (b, i, ParamIDs::eqBypass), v == 3 ? 1.0f : 0.0f);
                        }
                setP (*p, ParamIDs::eqOn, v >= 2 ? 1.0f : 0.0f);
               #endif
                pump (300);
                auto sig = pinkish (fs, 3.0, 9);
                run (*p, sig, 256);
                juce::uint64 h = 1469598103934665603ull;
                for (int c = 0; c < 2; ++c)
                {
                    const auto* d = reinterpret_cast<const unsigned char*> (sig.getReadPointer (c));
                    for (size_t i = 0; i < sizeof (float) * (size_t) sig.getNumSamples(); ++i) { h ^= d[i]; h *= 1099511628211ull; }
                }
                std::printf ("REF %.0f %s %s %016llx\n", fs, sc.name, variants[v], (unsigned long long) h);
                if (dumpDir != juce::File())
                {
                    dumpDir.createDirectory();
                    juce::FileOutputStream fo (dumpDir.getChildFile (juce::String ((int) fs) + "_" + sc.name + "_" + variants[v] + ".f32"));
                    fo.setPosition (0); fo.truncate();
                    for (int c = 0; c < 2; ++c) fo.write (sig.getReadPointer (c), sizeof (float) * (size_t) sig.getNumSamples());
                }
            }
    return 0;
}
#endif

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && juce::String (argv[1]) == "--quick";
    juce::ScopedJuceInitialiser_GUI juceInit;
   #ifdef MAS_RI
    if (argc > 1 && juce::String (argv[1]) == "--refhash")
    {
        const auto root0 = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-ref-presets");
        setenv ("MASTER_SPACE_PRESET_ROOT", root0.getFullPathName().toRawUTF8(), 1);
        return refHashes (argc > 2 ? juce::File (juce::String (argv[2])) : juce::File());
    }
   #endif
   #ifdef MAS_STUDIO_DYN
    if (argc > 2 && juce::String (argv[1]) == "--dyn-renders") return dynRenders (juce::File (juce::String (argv[2])));
   #endif
   #ifdef MAS_STUDIO
    if (argc > 2 && juce::String (argv[1]) == "--studio-snapshot")
    {
        const auto root0 = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-snap-presets");
        setenv ("MASTER_SPACE_PRESET_ROOT", root0.getFullPathName().toRawUTF8(), 1);
        return studioSnapshots (juce::File (juce::String (argv[2])));
    }
   #endif
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        const auto root0 = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-snap-presets");
        setenv ("MASTER_SPACE_PRESET_ROOT", root0.getFullPathName().toRawUTF8(), 1);
        return snapshots (juce::File (juce::String (argv[2])));
    }

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-preset-test-" + juce::String (juce::Time::currentTimeMillis()));
    root.createDirectory();
    setenv ("MASTER_SPACE_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);

    //==========================================================================
    section ("Factory library");
    {
        auto p = makeProcessor (48000, 512);
        auto& pm = p->getPresetManager();
        const auto& all = pm.getPresets();
       #ifdef MAS_ATMOS
        constexpr size_t kFactory = 36;
       #else
        constexpr size_t kFactory = 26;
       #endif
        check (all.size() == kFactory, juce::String ((int) kFactory) + " factory presets (" + juce::String ((int) all.size()) + ")");
        bool cats = true, notes = true, params = true;
        for (const auto& pr : all)
        {
            cats &= mspreset::categories().contains (pr.category);
            notes &= pr.notes.isEmpty();
            params &= pr.values.size() == p->captureParamMap().size();
            if (! pr.notes.isEmpty()) std::printf ("    note in %s: %s\n", pr.name.toRawUTF8(), pr.notes.joinIntoString ("; ").toRawUTF8());
        }
        check (cats, "every preset in a known category");
        check (notes, "no migration notes (complete, valid files)");
        check (params, "every preset holds every parameter");
        check (pm.getProblems().isEmpty(), "no unreadable files");
        check (pm.find ("Plates", {}).empty(), "Plates empty (no plate algorithm)");
        check (pm.find ("Signature Collection", {}).empty(), "Signature Collection empty, ready for own presets");
        check (pm.find (mspreset::kViewAll, "leadvox").size() == 3, "search 'leadvox' finds 3");
        const int legacyIdx = pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room (1.1 Legacy)");
        check (legacyIdx >= 0 && pm.getPresets()[(size_t) legacyIdx].engineName() == "1.1 Legacy", "LeadVox 1.1 preset uses the 1.1 Legacy engine");
       #ifdef MAS_ATMOS
        {
            int atmosOk = 0, roomOk = 0, atmosN = 0;
            for (const auto& pr : all)
            {
                const bool isAtmos = pr.name.startsWith ("ATMOS");
                const float tail = pr.values.at (ParamIDs::tail);
                if (isAtmos) { ++atmosN; atmosOk += tail > 0.5f ? 1 : 0; } else roomOk += tail < 0.5f ? 1 : 0;
            }
            check (atmosN == 10 && atmosOk == 10, "10 ATMOS presets use the Atmospheric Tail");
            check (roomOk == 26, "the 26 room presets keep the room algorithm (no tail parameter in their files)");
        }
       #endif
    }

    //==========================================================================
    for (double fs : { 48000.0, 96000.0 })
    {
        const int block = 512;
        section (fs > 50000 ? "Full DSP state restore, 96 kHz" : "Full DSP state restore, 48 kHz");
        auto a = makeProcessor (fs, block);
        auto& pm = a->getPresetManager();
        const auto& all = pm.getPresets();
        int ok = 0, tested = 0;
        float worst = 0;
        juce::String worstName;
        const int stepN = quick ? 5 : 1;
        for (size_t i = 0; i < all.size(); i += (size_t) stepN)
        {
            const size_t from = (i + 7) % all.size();
            // A: comes from another preset, loads preset i through the click-free transition while running
            pm.load ((int) from);
            auto noise = pinkish (fs, 0.5);
            run (*a, noise, block);
            pm.load ((int) i);
            auto settle = pinkish (fs, 0.5, 3);
            run (*a, settle, block);
            auto quiet = silence (fs, 1.5);
            run (*a, quiet, block);
            pump();
            const auto irA = impulseAfterReset (*a, fs, block, 1.5);

            // B: a fresh instance restored from A's saved state
            juce::MemoryBlock state;
            a->getStateInformation (state);
            auto b = std::make_unique<MasterSpaceAudioProcessor>();
            b->setStateInformation (state.getData(), (int) state.getSize());
            b->setRateAndBufferSizeDetails (fs, block);
            b->prepareToPlay (fs, block);
            auto quietB = silence (fs, 2.0);
            run (*b, quietB, block);
            const auto irB = impulseAfterReset (*b, fs, block, 1.5);

            juce::String diff;
            const bool paramsOk = sameParams (all[i].values, a->captureParamMap(), &diff) && sameParams (a->captureParamMap(), b->captureParamMap());
            float d = maxAbsDiff (irA, irB) / juce::jmax (1.0e-6f, peak (irB));
            if (all[i].engineName() != "1.2")
            {
                // the frozen 1.1 engine keeps its random-modulation sequence across reset(): compare the
                // energy envelope instead (10 ms windows, first 400 ms) - same room, same decay
                float worstDb = 0;
                const int w = (int) (0.01 * fs);
                for (int s0 = 64; s0 + w < (int) (0.4 * fs); s0 += w)
                {
                    double ea = 0, eb = 0;
                    for (int c = 0; c < 2; ++c) for (int k = s0; k < s0 + w; ++k) { ea += irA.getSample (c, k) * irA.getSample (c, k); eb += irB.getSample (c, k) * irB.getSample (c, k); }
                    worstDb = juce::jmax (worstDb, (float) std::abs (10.0 * std::log10 ((ea + 1e-20) / (eb + 1e-20))));
                }
                std::printf ("    %s (1.1 Legacy engine): energy envelope within %.2f dB\n", all[i].name.toRawUTF8(), worstDb);
                d = worstDb < 1.0f ? 0.0f : 1.0f;
            }
            if (d > worst) { worst = d; worstName = all[i].name; }
            ++tested;
            if (paramsOk && d < 1.0e-3f && allFinite (irA)) ++ok;
            else std::printf ("    %s: params %s %s, IR diff %.2e\n", all[i].name.toRawUTF8(), paramsOk ? "ok" : "DIFFER", diff.toRawUTF8(), d);
        }
        check (ok == tested, juce::String (ok) + "/" + juce::String (tested) + " presets: after a live load the DSP output equals a fresh instance with that state (worst "
                             + juce::String (worst, 7) + " of peak, " + worstName + ")");
    }

    //==========================================================================
    section ("Click-free transitions (preset load and A/B while audio runs)");
    {
        const double fs = 48000;
        const int block = 256;
        auto p = makeProcessor (fs, block);
        auto& pm = p->getPresetManager();
        const auto& all = pm.getPresets();
        // a continuous tone + noise through the plugin; switch between very different presets
       #ifdef MAS_ATMOS
        const int pairs[][2] = { { pm.indexOf ("factory:Halls/Cathedral Stone"), pm.indexOf ("factory:Creative Spaces/ATMOS Endless 30 s") },
                                 { pm.indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"), pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room (1.2)") },
                                 { pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room (1.1 Legacy)"), pm.indexOf ("factory:Halls/ATMOS Floating Hall") },
                                 { pm.indexOf ("factory:Ambiences/ATMOS Soft Ambience"), pm.indexOf ("factory:Creative Spaces/ATMOS Cinematic 12 s") } };
       #else
        const int pairs[][2] = { { pm.indexOf ("factory:Halls/Cathedral Stone"), pm.indexOf ("factory:Ambiences/Ambience Wood") },
                                 { pm.indexOf ("factory:Early Reflections/ER Studio Depth"), pm.indexOf ("factory:Chambers/Stone Chamber") },
                                 { pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room (1.1 Legacy)"), pm.indexOf ("factory:Vocal Spaces/LeadVox Studio Room (1.2)") },
                                 { pm.indexOf ("factory:Studio Rooms/Small Tight Room"), pm.indexOf ("factory:Creative Spaces/Far Away") } };
       #endif
        for (const auto& pr : pairs)
        {
            pm.load (pr[0]);
            juce::AudioBuffer<float> sig (2, (int) (fs * 3.0));
            for (int i = 0; i < sig.getNumSamples(); ++i)
            {
                const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / (float) fs);
                sig.setSample (0, i, v); sig.setSample (1, i, v);
            }
            auto out = sig;
            const int switchAt = (int) (fs * 1.5);
            // process up to the switch, switch on the message thread, continue
            juce::AudioBuffer<float> first (out.getArrayOfWritePointers(), 2, 0, switchAt);
            run (*p, first, block);
            pm.load (pr[1]);
            juce::AudioBuffer<float> second (out.getArrayOfWritePointers(), 2, switchAt, out.getNumSamples() - switchAt);
            run (*p, second, block);

            if (auto dump = juce::SystemStats::getEnvironmentVariable ("MS_TEST_DUMP", {}); dump.isNotEmpty())
            {
                juce::FileOutputStream fo (juce::File (dump + juce::String (pr[0]) + ".f32"));
                fo.setPosition (0); fo.truncate();
                for (int i = 0; i < out.getNumSamples(); ++i) { fo.writeFloat (out.getSample (0, i)); fo.writeFloat (out.getSample (1, i)); }
            }
            // discontinuity: largest second difference around the switch vs. the steady state before it
            auto d2 = [&out] (int from, int to)
            {
                float m = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = juce::jmax (2, from); i < to; ++i)
                        m = juce::jmax (m, std::abs (out.getSample (c, i) - 2.0f * out.getSample (c, i - 1) + out.getSample (c, i - 2)));
                return m;
            };
            const float before = d2 (switchAt - (int) (0.5 * fs), switchAt);
            const float after  = d2 (switchAt + (int) (0.5 * fs), switchAt + (int) (1.0 * fs));
            const float around = d2 (switchAt, switchAt + (int) (0.2 * fs));
            // level: 10 ms RMS around the switch must stay within the range of the two states (+1.5 dB)
            auto rms = [&out] (int from, int len) { double s = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < from + len; ++i) s += out.getSample (c, i) * out.getSample (c, i); return std::sqrt (s / (2.0 * len)); };
            const int w = (int) (0.01 * fs);
            double maxAround = 0;
            for (int i = switchAt - w; i < switchAt + (int) (0.2 * fs); i += w / 2) maxAround = juce::jmax (maxAround, rms (i, w));
            double maxSteady = 0;
            for (int i = switchAt - (int) (0.5 * fs); i < switchAt - w; i += w / 2) maxSteady = juce::jmax (maxSteady, rms (i, w));
            for (int i = switchAt + (int) (0.5 * fs); i < out.getNumSamples() - w; i += w / 2) maxSteady = juce::jmax (maxSteady, rms (i, w));
            const double overDb = 20.0 * std::log10 (maxAround / juce::jmax (1.0e-9, maxSteady));
            const bool ok = allFinite (out) && around <= 1.5f * juce::jmax (before, after) && overDb < 1.5;
            check (ok, all[(size_t) pr[0]].name + " -> " + all[(size_t) pr[1]].name + ": 2nd-difference peak " + juce::String (around, 4)
                       + " (steady " + juce::String (juce::jmax (before, after), 4) + "), level overshoot " + juce::String (overDb, 2) + " dB");
        }

        // rapid A/B clicking while playing
        pm.load (pairs[0][0]);
        auto sig = pinkish (fs, 4.0, 7);
        int pos = 0, clicks = 0;
        while (pos < sig.getNumSamples())
        {
            const int n = juce::jmin ((int) (0.037 * fs), sig.getNumSamples() - pos);
            juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, n);
            run (*p, view, block);
            pos += n;
            p->selectSlot (clicks++ % 2);
            if (clicks == 3) pm.load (pairs[3][1]);   // B gets a different preset
        }
        check (allFinite (sig) && peak (sig) < 4.0f, "fast A/B switching every 37 ms for 4 s: finite, no runaway (peak " + juce::String (peak (sig), 3) + ")");
    }

    //==========================================================================
    section ("A/B compare");
    {
        auto p = makeProcessor (48000, 512);
        auto& pm = p->getPresetManager();
        pm.load (pm.indexOf ("factory:Chambers/Plaster Chamber"));
        const auto stateA = p->captureParamMap();
        p->selectSlot (1);
        check (sameParams (stateA, p->captureParamMap()), "first switch to B copies A (no audible change)");
        auto* decay = p->apvts.getParameter (ParamIDs::decay);
        decay->setValueNotifyingHost (decay->convertTo0to1 (3.3f));
        const auto stateB = p->captureParamMap();
        p->selectSlot (0);
        check (sameParams (stateA, p->captureParamMap()), "back to A restores A");
        p->selectSlot (1);
        check (std::abs (p->captureParamMap()[ParamIDs::decay] - 3.3f) < 0.01f, "B keeps its edit (decay 3.3 s)");
        p->undoCompare();
        check (p->getActiveSlot() == 0 && sameParams (stateA, p->captureParamMap()), "UNDO returns to the previous variant (A)");
        p->undoCompare();
        check (p->getActiveSlot() == 1 && sameParams (stateB, p->captureParamMap()), "UNDO again = redo (B)");
        p->copyActiveToOther();
        p->selectSlot (0);
        check (sameParams (stateB, p->captureParamMap()), "B->A copy");

        // A/B slots, active slot and preset names travel with the project
        pm.load (pm.indexOf ("factory:Halls/Small Hall"));
        p->selectSlot (1);
        pm.load (pm.indexOf ("factory:Studio Rooms/Studio A Natural"));
        juce::MemoryBlock st;
        p->getStateInformation (st);
        auto q = makeProcessor (48000, 512);
        q->setStateInformation (st.getData(), (int) st.getSize());
        check (q->getActiveSlot() == 1 && q->getPresetManager().getCurrentName() == "Studio A Natural", "project reload: slot B active, preset name restored");
        q->selectSlot (0);
        check (sameParams (pm.getPresets()[(size_t) pm.indexOf ("factory:Halls/Small Hall")].values, q->captureParamMap())
                   && q->getPresetManager().getCurrentName() == "Small Hall", "project reload: slot A content and name restored");

        // level match
        const auto hall = pm.getPresets()[(size_t) pm.indexOf ("factory:Halls/Small Hall")].values;
        auto louder = hall; louder[ParamIDs::output] += 4.0f;
        const double la = MasterSpaceAudioProcessor::measureLoudness (hall, 48000), lb = MasterSpaceAudioProcessor::measureLoudness (louder, 48000);
        check (std::abs ((lb - la) - 4.0) < 0.1, "loudness measurement: +4 dB output reads as " + juce::String (lb - la, 2) + " LU");
        p->setLevelMatch (true);
        for (int i = 0; i < 40 && ! p->getMatchStatus().startsWith ("B "); ++i) { p->refreshLevelMatch(); pump (100); }
        check (p->getMatchStatus().startsWith ("B "), "level match computed: " + p->getMatchStatus());
    }

    //==========================================================================
    section ("Preset files: save, load, rename, duplicate, delete, import, export, favorites");
    {
        auto p = makeProcessor (48000, 512);
        auto& pm = p->getPresetManager();
        pm.load (pm.indexOf ("factory:Vocal Spaces/Vocal Hall"));
        check (pm.saveCurrent().ok == false, "factory preset is protected (SAVE refuses)");
        auto* depth = p->apvts.getParameter (ParamIDs::depth);
        depth->setValueNotifyingHost (depth->convertTo0to1 (77.0f));
        check (pm.isCurrentModified(), "edit marks the preset as modified");
        auto r = pm.saveAs ("My Hall", "Signature Collection", "Test Author", "desc", false);
        check (r.ok, "SAVE AS into Signature Collection: " + r.message);
        check (pm.find ("Signature Collection", {}).size() == 1, "Signature Collection now lists it");
        check (! pm.isCurrentModified() && pm.getCurrentName() == "My Hall", "saved preset is current and clean");
        const int mine = pm.indexOf (pm.getCurrentKey());
        check (mine >= 0 && pm.getPresets()[(size_t) mine].values.at (ParamIDs::depth) > 76.9f, "saved file holds the edit");
        check (pm.saveAs ("My Hall", "Signature Collection", "x", "", false).ok == false, "SAVE AS does not overwrite without confirmation");
        bool noTemp = true;
        for (const auto& f : root.findChildFiles (juce::File::findFiles, true, "*"))
            if (f.getFileName().startsWithChar ('.') && f.getFileName().contains ("temp")) noTemp = false;
        check (noTemp, "atomic write leaves no temporary files");

        depth->setValueNotifyingHost (depth->convertTo0to1 (12.0f));
        check (pm.saveCurrent().ok && pm.getPresets()[(size_t) pm.indexOf (pm.getCurrentKey())].values.at (ParamIDs::depth) < 12.1f, "SAVE overwrites the user preset");
        check (pm.rename (pm.indexOf (pm.getCurrentKey()), "My Hall 2").ok && pm.getCurrentName() == "My Hall 2", "RENAME");
        pm.setFavorite (pm.getCurrentKey(), true);
        check (pm.find (mspreset::kViewFavorites, {}).size() == 1, "FAVORITE");
        check (pm.duplicate (pm.indexOf (pm.getCurrentKey())).ok && pm.find (mspreset::kViewUser, {}).size() == 2, "DUPLICATE");
        check (pm.duplicate (pm.indexOf ("factory:Halls/Concert Hall")).ok, "DUPLICATE of a factory preset gives an editable user copy");
        const auto exportFile = root.getChildFile ("export/My Hall 2.json");
        check (pm.exportPreset (pm.indexOf (pm.getCurrentKey()), exportFile).ok && exportFile.existsAsFile(), "EXPORT");
        const int before = (int) pm.find (mspreset::kViewUser, {}).size();
        check (pm.remove (pm.indexOf (pm.getCurrentKey())).ok && (int) pm.find (mspreset::kViewUser, {}).size() == before - 1, "DELETE");
        r = pm.importFiles ({ exportFile });
        check (r.ok && (int) pm.find (mspreset::kViewUser, {}).size() == before, "IMPORT: " + r.message);

        // damaged and foreign files
        const auto bad = root.getChildFile ("bad.json");
        bad.replaceWithText ("{ \"format\": \"MASTER SPACE Preset\", \"parameters\": { ");
        r = pm.importFiles ({ bad });
        check (! r.ok, "IMPORT rejects a damaged file: " + r.message);
        root.getChildFile ("User/Halls/broken.json").replaceWithText ("not json");
        pm.rescan();
        check (pm.getProblems().size() == 1, "a damaged file in the library is reported, not loaded: " + pm.getProblems().joinIntoString ("; "));
    }

    //==========================================================================
    section ("Versioning and migration");
    {
        auto p = makeProcessor (48000, 512);
        auto& pm = p->getPresetManager();
        mspreset::Preset pr;
        juce::String err;
        // a 1.1-era file: no engine parameter, engine at top level, choices as text
        const auto ref = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("presets/reference/LeadVox_StudioRoom_1.1.json");
        check (pm.fromJson (juce::JSON::parse (ref.loadFileAsString()), pr, err) && pr.engineName() == "1.1 Legacy"
                   && std::abs (pr.values[ParamIDs::decorrelation] - 40.0f) < 0.01f, "1.1 reference preset: loads with the 1.1 Legacy engine");
        // future version with unknown and missing parameters, wrong types, out of range values
        auto json = juce::JSON::parse (R"({ "format": "MASTER SPACE Preset", "schemaVersion": 99, "pluginVersion": "3.0.0",
            "name": "Future", "category": "Halls", "parameters": { "decay": 99.0, "algo": "Hall", "matfront": 4, "eron": "off",
            "depth": "abc", "newthing": 1 } })");
        check (pm.fromJson (json, pr, err), "newer format still loads");
        auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-3f; };
        check (near (pr.values[ParamIDs::decay], 30.0f) && near (pr.values[ParamIDs::algorithm], 2.0f) && near (pr.values[ParamIDs::matFront], 4.0f)
                   && near (pr.values[ParamIDs::erOn], 0.0f) && near (pr.values[ParamIDs::depth], 35.0f),
               "values clamped / converted / defaulted (decay 99 -> 30, depth 'abc' -> default)");
        check (pr.notes.size() == 4, "user is told what was changed: " + pr.notes.joinIntoString (" | "));
        json = juce::JSON::parse (R"({ "format": "Some Other Plugin", "parameters": {} })");
        check (! pm.fromJson (json, pr, err), "foreign preset refused: " + err);

        // project state from 1.1 (no engine parameter): must keep the 1.1 sound
        juce::MemoryBlock st;
        p->getStateInformation (st);
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == ParamIDs::engine) { xml->removeChildElement (e, true); break; }
        xml->removeChildElement (xml->getChildByName ("ABCompare"), true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto q = makeProcessor (48000, 512);
        q->setStateInformation (old.getData(), (int) old.getSize());
        check (q->isLegacyEngine(), "project saved by 1.1 opens with the 1.1 Legacy engine");
        juce::MemoryBlock junk;
        junk.append ("garbage", 7);
        const auto beforeJunk = q->captureParamMap();
        q->setStateInformation (junk.getData(), (int) junk.getSize());
        check (sameParams (beforeJunk, q->captureParamMap()), "unreadable project state is ignored (current settings kept)");
    }

   #ifdef MAS_RI
    //==========================================================================
    section ("1.4 SMART DUCKER / RESONANCE GUARD in the plugin");
    {
        auto setP = [] (MasterSpaceAudioProcessor& q, const char* id, float plain)
        {
            if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain));
        };
        auto getP = [] (MasterSpaceAudioProcessor& q, const char* id) { return q.apvts.getRawParameterValue (id)->load(); };
        auto argmaxAbs = [] (const juce::AudioBuffer<float>& b, int ch)
        {
            int k = 0; float m = 0;
            for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (ch, i)) > m) { m = std::abs (b.getSample (ch, i)); k = i; }
            return k;
        };
        auto rmsDb = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            double e = 0; int n = 0;
            for (int c = 0; c < 2; ++c) for (int i = juce::jmax (0, from); i < juce::jmin (to, b.getNumSamples()); ++i) { e += b.getSample (c, i) * b.getSample (c, i); ++n; }
            return 10.0 * std::log10 (e / juce::jmax (1, n) + 1e-30);
        };
        auto diffFrom = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int shiftB = 0)
        {
            float m = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = from; i < a.getNumSamples() && i + shiftB < b.getNumSamples(); ++i)
                    m = juce::jmax (m, std::abs (a.getSample (c, i) - b.getSample (c, i + shiftB)));
            return m;
        };
        // vocal-like phrases: tones with harmonics in 0.8 s phrases, 0.6 s gaps
        auto phrases = [] (double fs, double secs)
        {
            juce::AudioBuffer<float> b (2, (int) (fs * secs));
            juce::Random rng (11);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / fs, ph = std::fmod (t, 1.4);
                const double env = ph < 0.8 ? std::sin (juce::MathConstants<double>::pi * juce::jmin (1.0, ph / 0.02)) * 0.5 + 0.5 : 0.0;
                const double f0 = 196.0 * (1.0 + 0.12 * std::floor (t / 1.4));
                double v = 0;
                for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t) / h;
                const float x = (float) (env * 0.18 * v + (ph < 0.8 ? 0.01 * (rng.nextFloat() - 0.5f) : 0.0));
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            return b;
        };

        // ---- defaults, old projects
        {
            auto q = makeProcessor (48000, 256);
            check (getP (*q, ParamIDs::duckOn) < 0.5f && getP (*q, ParamIDs::rgOn) < 0.5f && getP (*q, ParamIDs::quality) < 0.5f
                   && q->getLatencySamples() == 0, "new instance: SMART DUCKER off, RESONANCE GUARD off, LIVE, no latency");

            // an ATMOS 1.3 project (no 1.4 parameters) loaded into an instance that had both modules on and STUDIO HQ
            juce::MemoryBlock st;
            q->getStateInformation (st);
            auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
            juce::Array<juce::XmlElement*> drop;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                for (const char* id : ParamIDs::riParams) if (e->getStringAttribute ("id") == id) drop.add (e);
            for (auto* e : drop) xml->removeChildElement (e, true);
            xml->removeChildElement (xml->getChildByName ("ABCompare"), true);
            juce::MemoryBlock old13;
            juce::AudioProcessor::copyXmlToBinary (*xml, old13);
            setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::rgOn, 1); setP (*q, ParamIDs::quality, 1); setP (*q, ParamIDs::duckAmount, 15);
            pump (250);
            const int latHQ = q->getLatencySamples();
            q->setStateInformation (old13.getData(), (int) old13.getSize());
            pump (250);
            check (latHQ == 5191 && latHQ == mspace::Engine::hqLatencyFor (48000.0),
                   "STUDIO HQ reports its latency to the host: " + juce::String (latHQ) + " samples at 48 kHz (guard FFT 4096 + hop 1024, colour 4x FIR 71)");
            check (getP (*q, ParamIDs::duckOn) < 0.5f && getP (*q, ParamIDs::rgOn) < 0.5f && getP (*q, ParamIDs::quality) < 0.5f
                   && getP (*q, ParamIDs::hcOn) < 0.5f
                   && std::abs (getP (*q, ParamIDs::duckAmount) - 6.0f) < 0.01f && q->getLatencySamples() == 0,
                   "project without 1.4 parameters (ATMOS 1.3 / 1.2): all three modules OFF, defaults, LIVE, latency back to 0");

            // 1.4 state round trip; DELTA LISTEN is not saved
            setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::duckMode, 2); setP (*q, ParamIDs::rgOn, 1); setP (*q, ParamIDs::rgMode, 2);
            setP (*q, ParamIDs::rgMax, 2.5f); setP (*q, ParamIDs::scSource, 1);
            q->setDeltaListen (true);
            juce::MemoryBlock st14;
            q->getStateInformation (st14);
            auto r = makeProcessor (48000, 256);
            r->setStateInformation (st14.getData(), (int) st14.getSize());
            juce::String diff;
            check (sameParams (q->captureParamMap(), r->captureParamMap(), &diff) && ! r->getDeltaListen(),
                   "1.4 project round trip keeps every 1.4 parameter; DELTA LISTEN is a monitor switch, not saved " + diff);
            q->setDeltaListen (false);
        }

        // ---- presets
        {
            auto q = makeProcessor (48000, 256);
            auto& pm = q->getPresetManager();
            setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::rgOn, 1);
            pump (50);
            pm.load (pm.indexOf ("factory:Halls/Concert Hall"));
            pump (50);
            check (getP (*q, ParamIDs::duckOn) < 0.5f && getP (*q, ParamIDs::rgOn) < 0.5f, "factory / older presets load with both modules OFF");
            setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::duckAmount, 9.5f); setP (*q, ParamIDs::rgOn, 1); setP (*q, ParamIDs::rgMode, 1);
            const auto res = pm.saveAs ("RI Test", "Signature Collection", "", "", false);
            const int idx = pm.indexOf (pm.getCurrentKey());
            bool ok = res.ok && idx >= 0;
            if (ok)
            {
                const auto& v = pm.getPresets()[(size_t) idx].values;
                ok = v.at (ParamIDs::duckOn) > 0.5f && std::abs (v.at (ParamIDs::duckAmount) - 9.5f) < 0.01f && v.at (ParamIDs::rgOn) > 0.5f
                     && std::abs (v.at (ParamIDs::rgMode) - 1.0f) < 0.01f;
            }
            pm.load (pm.indexOf ("factory:Halls/Concert Hall"));
            pump (50);
            pm.load (idx);
            pump (50);
            ok = ok && getP (*q, ParamIDs::duckOn) > 0.5f && std::abs (getP (*q, ParamIDs::duckAmount) - 9.5f) < 0.01f;
            check (ok, "a preset saved with SMART DUCKER / RESONANCE GUARD restores them");
            pm.remove (idx);
        }

        // ---- OFF is transparent; OFF after ON returns bit-exactly to the untouched output (the FDN is never touched)
        {
            auto ref = makeProcessor (48000, 256), any = makeProcessor (48000, 256), duck = makeProcessor (48000, 256), guard = makeProcessor (48000, 256);
            for (auto* q : { ref.get(), any.get(), duck.get(), guard.get() })
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"));
            // every 1.4 setting away from its default, modules still OFF
            for (const char* id : ParamIDs::riParams)
                if (juce::String (id) != ParamIDs::duckOn && juce::String (id) != ParamIDs::rgOn && juce::String (id) != ParamIDs::quality
                    && juce::String (id) != ParamIDs::hcOn)
                    if (auto* r = any->apvts.getParameter (id)) r->setValueNotifyingHost (r->getDefaultValue() < 0.5f ? 0.8f : 0.2f);
            setP (*duck, ParamIDs::duckOn, 1); setP (*duck, ParamIDs::duckAmount, 12);
            setP (*guard, ParamIDs::rgOn, 1); setP (*guard, ParamIDs::rgMode, 1); setP (*guard, ParamIDs::rgMax, 6); setP (*guard, ParamIDs::rgTarget, 0);
            pump (50);
            auto a = phrases (48000, 9.0), b = a, c = a, d = a;
            const int half = 48000 * 3;
            juce::AudioBuffer<float> a1 (a.getArrayOfWritePointers(), 2, 0, half), b1 (b.getArrayOfWritePointers(), 2, 0, half),
                                     c1 (c.getArrayOfWritePointers(), 2, 0, half), d1 (d.getArrayOfWritePointers(), 2, 0, half);
            run (*ref, a1, 256); run (*any, b1, 256); run (*duck, c1, 256); run (*guard, d1, 256);
            const float duckedDiff = diffFrom (a1, c1, 0), guardDiff = diffFrom (a1, d1, 0);
            setP (*duck, ParamIDs::duckOn, 0); setP (*guard, ParamIDs::rgOn, 0);
            pump (50);
            juce::AudioBuffer<float> a2 (a.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half), b2 (b.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half),
                                     c2 (c.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half), d2 (d.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half);
            run (*ref, a2, 256); run (*any, b2, 256); run (*duck, c2, 256); run (*guard, d2, 256);
            check (maxAbsDiff (a, b) == 0.0f, "all modules OFF (DUCKER, GUARD, COLOR): output identical whatever their settings (bit-exact)");
            const int settle = 48000 * 2;   // release (600 ms + hold) and the guard's release
            check (duckedDiff > 1.0e-3f && diffFrom (a2, c2, settle) == 0.0f,
                   "SMART DUCKER switched off: after its release the output is bit-identical to never ducked (tail structure untouched; ducked diff "
                   + juce::String (duckedDiff, 4) + ")");
            check (diffFrom (a2, d2, settle) < 1.0e-6f, "RESONANCE GUARD switched off: after its release the output equals the unguarded one (max diff "
                   + juce::String (diffFrom (a2, d2, settle), 8) + ", guarded diff " + juce::String (guardDiff, 4) + ")");
        }

        // ---- STUDIO HQ: every path delayed by the reported latency, exactly
        for (double fs : { 48000.0, 96000.0 })
        {
            const int expected = mspace::Engine::hqLatencyFor (fs);
            auto live = makeProcessor (fs, 256), hq = makeProcessor (fs, 256);
            for (auto* q : { live.get(), hq.get() })
            {
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Studio Rooms/Studio A Natural"));
                pump (50);
            }
            setP (*hq, ParamIDs::quality, 1);
            pump (250);
            for (auto* q : { live.get(), hq.get() }) { auto warm = silence (fs, 0.1); run (*q, warm, 256); }   // HQ: switched at a faded point
            check (hq->getLatencySamples() == expected, juce::String (fs / 1000.0, 0) + " kHz: STUDIO HQ latency " + juce::String (hq->getLatencySamples()) + " samples ("
                   + juce::String (1000.0 * expected / fs, 1) + " ms) reported");
            for (int variant = 0; variant < 3; ++variant)   // 0: 1.2 Insert direct only, 1: 1.2 Aux wet, 2: 1.1 Legacy engine
            {
                for (auto* q : { live.get(), hq.get() })
                {
                    setP (*q, ParamIDs::routing, variant == 1 ? 1.0f : 0.0f);
                    setP (*q, ParamIDs::mix, variant == 0 ? 0.0f : 40.0f);
                    setP (*q, ParamIDs::engine, variant == 2 ? 0.0f : 1.0f);
                    pump (50);
                    auto w = silence (fs, 1.5); run (*q, w, 256);     // parameter smoothing settled
                }
                const auto x = impulseAfterReset (*live, fs, 256, 1.0), y = impulseAfterReset (*hq, fs, 256, 1.0);
                const float d = diffFrom (x, y, 0, expected);
                const char* names[] = { "Insert direct sound", "Aux wet (RESONANCE GUARD off)", "1.1 Legacy engine" };
                check (d < 1.0e-6f && argmaxAbs (y, 0) == argmaxAbs (x, 0) + expected,
                       juce::String (fs / 1000.0, 0) + " kHz STUDIO HQ = LIVE delayed by exactly the reported latency: " + names[variant]
                       + " (max diff " + juce::String (d, 8) + ")");
            }
            // IR mode too
            {
                const auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms14-test-ir.wav");
                {
                    juce::AudioBuffer<float> ir (2, (int) (0.4 * fs));
                    juce::Random rng (5);
                    for (int i = 0; i < ir.getNumSamples(); ++i)
                        for (int c = 0; c < 2; ++c) ir.setSample (c, i, (rng.nextFloat() - 0.5f) * std::exp (-6.0f * (float) i / ir.getNumSamples()));
                    irFile.deleteFile();
                    std::unique_ptr<juce::OutputStream> os (irFile.createOutputStream());
                    std::unique_ptr<juce::AudioFormatWriter> wr (juce::WavAudioFormat().createWriterFor (os.get(), fs, 2, 24, {}, 0));
                    if (wr != nullptr) { os.release(); wr->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()); }
                }
                for (auto* q : { live.get(), hq.get() })
                {
                    setP (*q, ParamIDs::engine, 1.0f); setP (*q, ParamIDs::routing, 0.0f); setP (*q, ParamIDs::mix, 40.0f);
                    q->loadImpulseResponse (irFile);
                    setP (*q, ParamIDs::mode, 1.0f);
                    pump (300);
                    auto w = silence (fs, 0.5); run (*q, w, 256);
                    pump (100);
                    w = silence (fs, 0.5); run (*q, w, 256);
                }
                const auto x = impulseAfterReset (*live, fs, 256, 1.0), y = impulseAfterReset (*hq, fs, 256, 1.0);
                const float d = diffFrom (x, y, 0, expected);
                check (d < 1.0e-5f && peak (x) > 0.01f, juce::String (fs / 1000.0, 0) + " kHz STUDIO HQ = LIVE delayed: IR mode (max diff " + juce::String (d, 8) + ")");
                irFile.deleteFile();
            }
        }

        // ---- STUDIO HQ through a preset transition, a 1.2 -> 1.1 switch and ALGO -> IR -> ALGO: still exactly LIVE delayed
        //      (the cross-fade gains on the output side are delayed with the audio, so the direct sound stays constant)
        {
            const double fs = 48000.0;
            const int L = mspace::Engine::hqLatencyFor (fs);
            auto live = makeProcessor (fs, 256), hq = makeProcessor (fs, 256);
            const auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms14-test-ir2.wav");
            {
                juce::AudioBuffer<float> ir (2, (int) (0.3 * fs));
                juce::Random rng (6);
                for (int i = 0; i < ir.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c) ir.setSample (c, i, (rng.nextFloat() - 0.5f) * std::exp (-8.0f * (float) i / ir.getNumSamples()));
                irFile.deleteFile();
                std::unique_ptr<juce::OutputStream> os (irFile.createOutputStream());
                std::unique_ptr<juce::AudioFormatWriter> wr (juce::WavAudioFormat().createWriterFor (os.get(), fs, 2, 24, {}, 0));
                if (wr != nullptr) { os.release(); wr->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()); }
            }
            for (auto* q : { live.get(), hq.get() })
            {
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Studio Rooms/Studio A Natural"));
                setP (*q, ParamIDs::mix, 30);
                q->loadImpulseResponse (irFile);
            }
            setP (*hq, ParamIDs::quality, 1);
            pump (300);
            for (auto* q : { live.get(), hq.get() }) { auto w = silence (fs, 2.0); run (*q, w, 256); }
            pump (100);
            juce::AudioBuffer<float> x (2, (int) (fs * 6.0));
            juce::Random rng (8);
            for (int i = 0; i < x.getNumSamples(); ++i)
            {
                const float v = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / fs) + 0.02f * (rng.nextFloat() - 0.5f);
                x.setSample (0, i, v); x.setSample (1, i, v);
            }
            auto y = x;
            juce::MidiBuffer midi;
            for (int pos = 0; pos < x.getNumSamples(); pos += 256)
            {
                const int len = juce::jmin (256, x.getNumSamples() - pos);
                if (pos == 256 * 188)  for (auto* q : { live.get(), hq.get() }) { auto m = q->captureParamMap(); m[ParamIDs::decay] = 2.4f; q->applyParamMap (m); }
                if (pos == 256 * 375)  for (auto* q : { live.get(), hq.get() }) setP (*q, ParamIDs::engine, 0.0f);
                if (pos == 256 * 563)  for (auto* q : { live.get(), hq.get() }) setP (*q, ParamIDs::mode, 1.0f);
                if (pos == 256 * 750)  for (auto* q : { live.get(), hq.get() }) setP (*q, ParamIDs::mode, 0.0f);
                juce::AudioBuffer<float> a (x.getArrayOfWritePointers(), 2, pos, len), b (y.getArrayOfWritePointers(), 2, pos, len);
                live->processBlock (a, midi);
                hq->processBlock (b, midi);
            }
            // same sound, only later: envelope in 10 ms windows and click measure (2nd difference) of HQ vs LIVE.
            // (Not sample-exact here: a path that fades out runs L samples longer in HQ, so its modulation phase
            //  differs when it is restarted later.)
            double envDiff = 0;
            for (int w = (int) (0.6 * fs); w + L + 480 < y.getNumSamples(); w += 480)
            {
                double ex = 0, ey = 0;
                for (int c = 0; c < 2; ++c) for (int i = w; i < w + 480; ++i) { ex += x.getSample (c, i) * x.getSample (c, i); ey += y.getSample (c, i + L) * y.getSample (c, i + L); }
                const double dd = std::abs (10.0 * std::log10 ((ey + 1e-12) / (ex + 1e-12)));
                if (std::getenv ("MS_DEBUG_ENV") && dd > 0.1) std::printf ("    env diff at %.3f s: %.3f dB\n", w / fs, dd);
                envDiff = juce::jmax (envDiff, dd);
            }
            auto d2 = [] (const juce::AudioBuffer<float>& b, int from)
            {
                float w = 0;
                for (int c = 0; c < 2; ++c) for (int i = from + 2; i < b.getNumSamples(); ++i)
                    w = juce::jmax (w, std::abs (b.getSample (c, i) - 2.0f * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
                return w;
            };
            const float clickHQ = d2 (y, (int) (0.6 * fs) + L), clickLive = d2 (x, (int) (0.6 * fs));
            check (envDiff < 0.5 && clickHQ < clickLive * 1.25f + 1.0e-3f,
                   "STUDIO HQ through a preset transition, 1.2 -> 1.1, ALGO -> IR -> ALGO: same envelope as LIVE, delayed (max "
                   + juce::String (envDiff, 3) + " dB), no click (2nd-difference peak " + juce::String (clickHQ, 4) + " vs " + juce::String (clickLive, 4) + ")");
            irFile.deleteFile();
        }

        // ---- LIVE <-> STUDIO HQ while audio runs: faded, no click, no NaN (compared with the same run without switches)
        {
            auto q = makeProcessor (48000, 128), ref = makeProcessor (48000, 128);
            for (auto* x : { q.get(), ref.get() })
            {
                x->getPresetManager().load (x->getPresetManager().indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"));
                setP (*x, ParamIDs::rgOn, 1); setP (*x, ParamIDs::duckOn, 1);
            }
            pump (50);
            auto sig = phrases (48000, 4.0), refSig = sig;
            juce::MidiBuffer midi;
            int switches = 0;
            for (int pos = 0; pos < sig.getNumSamples(); pos += 128)
            {
                if (pos % (48000 / 2) == 0 && pos > 0) { setP (*q, ParamIDs::quality, (float) (++switches % 2)); }
                const int len = juce::jmin (128, sig.getNumSamples() - pos);
                juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, len), rv (refSig.getArrayOfWritePointers(), 2, pos, len);
                q->processBlock (view, midi);
                ref->processBlock (rv, midi);
            }
            auto d2 = [] (const juce::AudioBuffer<float>& b)
            {
                float w = 0;
                for (int c = 0; c < 2; ++c) for (int i = 2; i < b.getNumSamples(); ++i)
                    w = juce::jmax (w, std::abs (b.getSample (c, i) - 2.0f * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
                return w;
            };
            const float worst = d2 (sig), base = d2 (refSig);
            check (allFinite (sig) && worst < base * 1.25f + 1.0e-3f, juce::String (switches) + " quality switches while audio runs: finite, faded (2nd-difference peak "
                   + juce::String (worst, 4) + " vs " + juce::String (base, 4) + " without switching)");
        }

        // ---- DELTA LISTEN is the exact difference: processed + delta = unprocessed (OUTPUT MATCH off)
        for (int quality : { 0, 1 })
        {
            auto off = makeProcessor (48000, 256), on = makeProcessor (48000, 256), del = makeProcessor (48000, 256);
            for (auto* q : { off.get(), on.get(), del.get() })
            {
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
                setP (*q, ParamIDs::routing, 1); setP (*q, ParamIDs::quality, (float) quality); setP (*q, ParamIDs::rgMatch, 0);
                setP (*q, ParamIDs::rgMode, 1); setP (*q, ParamIDs::rgMax, 6); setP (*q, ParamIDs::rgTarget, 0); setP (*q, ParamIDs::rgSelect, 0);
            }
            setP (*on, ParamIDs::rgOn, 1); setP (*del, ParamIDs::rgOn, 1); del->setDeltaListen (true);
            pump (250);
            auto x = phrases (48000, 6.0), y = x, z = x;
            run (*off, x, 256); run (*on, y, 256); run (*del, z, 256);
            float m = 0, dl = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = 48000; i < x.getNumSamples(); ++i)
                {
                    m = juce::jmax (m, std::abs (y.getSample (c, i) + z.getSample (c, i) - x.getSample (c, i)));
                    dl = juce::jmax (dl, std::abs (z.getSample (c, i)));
                }
            check (m < 2.0e-5f, juce::String (quality ? "STUDIO HQ" : "LIVE") + ": DELTA LISTEN is the exact, aligned difference (|processed + delta - unprocessed| <= "
                   + juce::String (m, 7) + ", delta peak " + juce::String (dl, 4) + ")");
        }

        // ---- external sidechain: the host bus keys the ducker; without a bus the internal key is used
        for (bool monoMain : { false, true })
        {
            auto q = std::make_unique<MasterSpaceAudioProcessor>();
            auto layout = q->getBusesLayout();
            if (monoMain) layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            const bool accepted = q->setBusesLayout (layout);
            q->setRateAndBufferSizeDetails (48000, 256);
            q->prepareToPlay (48000, 256);
            q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            setP (*q, ParamIDs::routing, 1); setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::duckMode, 0); setP (*q, ParamIDs::duckAmount, 12);
            setP (*q, ParamIDs::duckRelease, 150); setP (*q, ParamIDs::duckHold, 20); setP (*q, ParamIDs::scSource, 1);
            pump (50);
            const int chans = juce::jmax (q->getTotalNumInputChannels(), q->getTotalNumOutputChannels());
            const int first = q->getChannelIndexInProcessBlockBuffer (true, 1, 0);
            juce::AudioBuffer<float> buf (chans, 48000 * 4);
            buf.clear();
            juce::Random rng (9);
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float main = 0.1f * (rng.nextFloat() - 0.5f);                                   // steady material
                const float key = std::fmod (i / 48000.0, 1.0) < 0.5 ? 0.3f * (rng.nextFloat() - 0.5f) : 0.0f;   // key on / off
                buf.setSample (0, i, main);
                if (! monoMain) buf.setSample (1, i, main);
                buf.setSample (first, i, key); buf.setSample (first + 1, i, key);
            }
            juce::MidiBuffer midi;
            for (int pos = 0; pos < buf.getNumSamples(); pos += 256)
            {
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), chans, pos, juce::jmin (256, buf.getNumSamples() - pos));
                q->processBlock (view, midi);
            }
            // wet level while the key plays vs while it is silent (last 2 s)
            double keyOn = 0, keyOff = 0;
            for (int s0 = 2 * 48000; s0 < 4 * 48000; s0 += 48000)
            {
                keyOn  += rmsDb (buf, s0 + 12000, s0 + 23000) * 0.5;
                keyOff += rmsDb (buf, s0 + 36000, s0 + 47000) * 0.5;
            }
            check (accepted && q->riMeters.scExternalLive.load() && keyOff - keyOn > 4.0,
                   juce::String (monoMain ? "mono" : "stereo") + " input + stereo sidechain bus: EXTERNAL key ducks the wet by "
                   + juce::String (keyOff - keyOn, 1) + " dB while the key plays, opens in its pauses");
        }
        {
            auto q = makeProcessor (48000, 256);     // no sidechain bus enabled by the host
            setP (*q, ParamIDs::duckOn, 1); setP (*q, ParamIDs::scSource, 1);
            pump (50);
            auto sig = phrases (48000, 1.0);
            run (*q, sig, 256);
            check (! q->riMeters.scExternalLive.load() && allFinite (sig), "EXTERNAL chosen without a host sidechain bus: falls back to the internal key");
        }

        // ---- every 1.4 parameter automated at random (plus quality), on the main and the transition paths
        {
            auto q = makeProcessor (48000, 64);
            juce::Random rng (77);
            auto sig = phrases (48000, quick ? 3.0 : 6.0);
            juce::MidiBuffer midi;
            int k = 0;
            for (int pos = 0; pos < sig.getNumSamples(); pos += 64, ++k)
            {
                const char* id = ParamIDs::riParams[rng.nextInt ((int) std::size (ParamIDs::riParams))];
                if (juce::String (id) != ParamIDs::quality || k % 200 == 0)
                    if (auto* r = q->apvts.getParameter (id)) r->setValueNotifyingHost (rng.nextFloat());
                if (k % 900 == 0) q->getPresetManager().load (q->getPresetManager().indexOf (k % 1800 ? "factory:Halls/Concert Hall" : "factory:Vocal Spaces/ATMOS Vocal Glow"));
                juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, juce::jmin (64, sig.getNumSamples() - pos));
                q->processBlock (view, midi);
            }
            check (allFinite (sig) && peak (sig) < 8.0f, "random automation of every 1.4 parameter every 64 samples, preset loads, quality switches: finite (peak "
                   + juce::String (peak (sig), 2) + ")");
        }

        //======================================================================
        // ---- HARMONIC COLOR
        auto setMany = [&] (MasterSpaceAudioProcessor& q, std::initializer_list<std::pair<const char*, float>> v) { for (auto& kv : v) setP (q, kv.first, kv.second); };
        auto loadPreset = [] (MasterSpaceAudioProcessor& q, const char* key) { q.getPresetManager().load (q.getPresetManager().indexOf (key)); };
        {
            auto q = makeProcessor (48000, 256);
            check (getP (*q, ParamIDs::hcOn) < 0.5f && getP (*q, ParamIDs::hcPos) < 0.5f && std::abs (getP (*q, ParamIDs::hcMix) - 100.0f) < 0.01f
                   && getP (*q, ParamIDs::hcComp) > 0.5f, "HARMONIC COLOR: new instance OFF, POST (wet), MIX 100 %, LEVEL COMP on");
            setMany (*q, { { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, 2 }, { ParamIDs::hcDrive, 70 }, { ParamIDs::hcColor, -40 }, { ParamIDs::hcMix, 60 },
                           { ParamIDs::hcComp, 0 }, { ParamIDs::hcPos, 1 } });
            pump (50);
            loadPreset (*q, "factory:Halls/Concert Hall");
            pump (50);
            check (getP (*q, ParamIDs::hcOn) < 0.5f && getP (*q, ParamIDs::hcPos) < 0.5f, "HARMONIC COLOR: factory / older presets load with the module OFF, POST");
            auto& pm = q->getPresetManager();
            setMany (*q, { { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, 1 }, { ParamIDs::hcDrive, 64 }, { ParamIDs::hcColor, 25 }, { ParamIDs::hcMix, 55 },
                           { ParamIDs::hcComp, 0 }, { ParamIDs::hcPos, 1 } });
            const auto res = pm.saveAs ("HC Test", "Signature Collection", "", "", false);
            const int idx = pm.indexOf (pm.getCurrentKey());
            loadPreset (*q, "factory:Halls/Concert Hall");
            pump (50);
            pm.load (idx);
            pump (50);
            const bool ok = res.ok && idx >= 0 && getP (*q, ParamIDs::hcOn) > 0.5f && std::abs (getP (*q, ParamIDs::hcChar) - 1.0f) < 0.01f
                            && std::abs (getP (*q, ParamIDs::hcDrive) - 64.0f) < 0.01f && std::abs (getP (*q, ParamIDs::hcColor) - 25.0f) < 0.01f
                            && std::abs (getP (*q, ParamIDs::hcMix) - 55.0f) < 0.01f && getP (*q, ParamIDs::hcComp) < 0.5f && getP (*q, ParamIDs::hcPos) > 0.5f;
            check (ok, "HARMONIC COLOR: a preset keeps all seven settings (ON, CHARACTER, DRIVE, COLOR, MIX, LEVEL COMP, POSITION)");
            pm.remove (idx);
            juce::MemoryBlock st;
            q->getStateInformation (st);
            auto r = makeProcessor (48000, 256);
            r->setStateInformation (st.getData(), (int) st.getSize());
            check (sameParams (q->captureParamMap(), r->captureParamMap()), "HARMONIC COLOR: project state round trip");
            int automatable = 0;
            for (const char* id : { ParamIDs::hcOn, ParamIDs::hcChar, ParamIDs::hcDrive, ParamIDs::hcColor, ParamIDs::hcMix, ParamIDs::hcComp })
                if (auto* prm = q->apvts.getParameter (id)) automatable += prm->isAutomatable() ? 1 : 0;
            check (automatable == 6, "HARMONIC COLOR: ON, CHARACTER, DRIVE, COLOR, MIX, LEVEL COMP are host-automatable (POSITION is a setup choice)");
        }

        // dry untouched in INSERT: 200 ms pre-delay, so the first 150 ms are the direct sound only
        for (int pos : { 0, 1 })
        {
            auto off = makeProcessor (48000, 256), on = makeProcessor (48000, 256);
            for (auto* q : { off.get(), on.get() })
            {
                loadPreset (*q, "factory:Vocal Spaces/ATMOS Vocal Glow");
                setMany (*q, { { ParamIDs::routing, 0 }, { ParamIDs::mix, 50 }, { ParamIDs::preDelay, 200 }, { ParamIDs::hcPos, (float) pos },
                               { ParamIDs::hcDrive, 100 }, { ParamIDs::hcChar, 1 } });
            }
            setP (*on, ParamIDs::hcOn, 1);
            pump (50);
            for (auto* q : { off.get(), on.get() }) { auto w = silence (48000, 0.5); run (*q, w, 256); }
            juce::AudioBuffer<float> a (2, 48000);
            a.clear();
            for (int i = 0; i < 2400; ++i) for (int c = 0; c < 2; ++c) a.setSample (c, i, 0.9f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
            auto b = a;
            run (*off, a, 256); run (*on, b, 256);
            float dryDiff = 0, dryPeak = 0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 7200; ++i) { dryDiff = juce::jmax (dryDiff, std::abs (a.getSample (c, i) - b.getSample (c, i))); dryPeak = juce::jmax (dryPeak, std::abs (b.getSample (c, i))); }
            const float wetDiff = diffFrom (a, b, 12000);
            // (the LIVE oversampler carries a 1e-20 anti-denormal offset: the silent wet under the direct sound is ~1e-20, not 0)
            check (dryDiff < 1.0e-15f && dryPeak > 0.1f && wetDiff > 1.0e-3f,
                   juce::String ("HARMONIC COLOR ") + (pos ? "PRE" : "POST") + ", INSERT, DRIVE 100 %: the direct sound is unchanged (diff "
                   + juce::String ((double) dryDiff) + ", peak " + juce::String (dryPeak, 2) + "), only the reverb changes (wet diff " + juce::String (wetDiff, 4) + ")");
        }

        // switched off after ON (POST): after its 20 ms fade the output is bit-identical to never coloured
        {
            auto ref = makeProcessor (48000, 256), col = makeProcessor (48000, 256);
            for (auto* q : { ref.get(), col.get() }) loadPreset (*q, "factory:Vocal Spaces/ATMOS Vocal Glow");
            setMany (*col, { { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, 2 }, { ParamIDs::hcDrive, 80 } });
            pump (50);
            auto a = phrases (48000, 5.0), b = a;
            const int half = 48000 * 2;
            juce::AudioBuffer<float> a1 (a.getArrayOfWritePointers(), 2, 0, half), b1 (b.getArrayOfWritePointers(), 2, 0, half);
            run (*ref, a1, 256); run (*col, b1, 256);
            const float colDiff = diffFrom (a1, b1, 0);
            setP (*col, ParamIDs::hcOn, 0);
            pump (50);
            juce::AudioBuffer<float> a2 (a.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half), b2 (b.getArrayOfWritePointers(), 2, half, a.getNumSamples() - half);
            run (*ref, a2, 256); run (*col, b2, 256);
            check (colDiff > 1.0e-3f && diffFrom (a2, b2, 4800) == 0.0f,
                   "HARMONIC COLOR POST switched off: after the fade the output is bit-identical to never coloured (coloured diff " + juce::String (colDiff, 4) + ")");
        }

        // LEVEL COMP: DRIVE 0 -> 100 % keeps the wet loudness; the three characters really differ
        {
            juce::String txt;
            bool ok = true;
            double lvl[3][2] = {};
            juce::AudioBuffer<float> outs[3];
            for (int chr = 0; chr < 3; ++chr)
                for (int d = 0; d < 2; ++d)
                {
                    auto q = makeProcessor (48000, 256);
                    loadPreset (*q, "factory:Vocal Spaces/ATMOS Vocal Glow");
                    setMany (*q, { { ParamIDs::routing, 1 }, { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, (float) chr }, { ParamIDs::hcDrive, d ? 100.0f : 0.0f } });
                    pump (50);
                    auto x = phrases (48000, 6.0);
                    run (*q, x, 256);
                    lvl[chr][d] = rmsDb (x, 48000, x.getNumSamples());
                    if (d) outs[chr] = x;
                    ok = ok && allFinite (x);
                }
            for (int chr = 0; chr < 3; ++chr) ok = ok && std::abs (lvl[chr][1] - lvl[chr][0]) < 1.0;
            for (int chr = 0; chr < 3; ++chr) txt << " " << (const char*[]) { "SOFT", "TUBE", "TAPE" }[chr] << " " << juce::String (lvl[chr][1] - lvl[chr][0], 2) << " dB";
            check (ok, "HARMONIC COLOR LEVEL COMP: wet loudness at DRIVE 100 % vs 0 %:" + txt);
            auto relDiff = [&] (int a, int b) { return rmsDb ([&] { auto d = outs[a]; d.addFrom (0, 0, outs[b], 0, 0, d.getNumSamples(), -1.0f); d.addFrom (1, 0, outs[b], 1, 0, d.getNumSamples(), -1.0f); return d; }(), 48000, outs[a].getNumSamples()) - rmsDb (outs[a], 48000, outs[a].getNumSamples()); };
            const double st = relDiff (0, 1), sp = relDiff (0, 2), tp = relDiff (1, 2);
            check (st > -40.0 && sp > -40.0 && tp > -40.0, "SOFT / TUBE / TAPE are different processes at DRIVE 100 % (differences "
                   + juce::String (st, 1) + " / " + juce::String (sp, 1) + " / " + juce::String (tp, 1) + " dB re wet)");
        }

        // STUDIO HQ with the module on: same colour as LIVE (same harmonics, aligned), only the anti-alias filters differ
        for (int pos : { 0, 1 })
        {
            auto live = makeProcessor (48000, 256), hq = makeProcessor (48000, 256);
            for (auto* q : { live.get(), hq.get() })
            {
                loadPreset (*q, "factory:Vocal Spaces/ATMOS Vocal Glow");
                setMany (*q, { { ParamIDs::routing, 1 }, { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, 1 }, { ParamIDs::hcDrive, 70 }, { ParamIDs::hcPos, (float) pos } });
            }
            setP (*hq, ParamIDs::quality, 1);
            pump (250);
            for (auto* q : { live.get(), hq.get() }) { auto w = silence (48000, 0.3); run (*q, w, 256); }
            const int L = hq->getLatencySamples();
            auto x = phrases (48000, 5.0), y = x;
            juce::AudioBuffer<float> pad (2, x.getNumSamples() + L);
            pad.clear();
            for (int c = 0; c < 2; ++c) pad.copyFrom (c, 0, y, c, 0, y.getNumSamples());
            run (*live, x, 256); run (*hq, pad, 256);
            juce::AudioBuffer<float> hqAligned (2, x.getNumSamples());
            for (int c = 0; c < 2; ++c) hqAligned.copyFrom (c, 0, pad, c, L, x.getNumSamples());
            // the same colour: band energies (LIVE's IIR oversampler is an all-pass on the coloured wet, so its waveform is not HQ's)
            auto bandDb = [] (const juce::AudioBuffer<float>& b, double lo, double hi)
            {
                juce::IIRFilter f[4];
                double e = 0;
                for (int c = 0; c < 2; ++c)
                {
                    f[0].setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, lo)); f[1].setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, lo));
                    f[2].setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, hi));  f[3].setCoefficients (juce::IIRCoefficients::makeLowPass (48000.0, hi));
                    for (auto& ff : f) ff.reset();
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        float v = b.getSample (c, i);
                        for (auto& ff : f) v = ff.processSingleSampleRaw (v);
                        if (i >= 48000) e += (double) v * v;
                    }
                }
                return 10.0 * std::log10 (e + 1e-30);
            };
            double worst = 0;
            juce::String bands;
            for (auto [lo, hi] : { std::pair { 80.0, 250.0 }, { 250.0, 800.0 }, { 800.0, 2500.0 }, { 2500.0, 7000.0 }, { 7000.0, 16000.0 } })
            {
                const double d = bandDb (hqAligned, lo, hi) - bandDb (x, lo, hi);
                worst = juce::jmax (worst, std::abs (d));
                bands << " " << juce::String (d, 2);
            }
            // alignment: LIVE's all-pass delays the coloured wet by a few samples (~0.1 ms), HQ's is compensated exactly
            int lag = 0; double best = -1e30;
            for (int k = -24; k <= 24; ++k)
            {
                double acc = 0;
                for (int c = 0; c < 2; ++c) for (int i = 48000; i < x.getNumSamples() - 48; ++i) acc += (double) hqAligned.getSample (c, i) * x.getSample (c, i + k);
                if (acc > best) { best = acc; lag = k; }
            }
            check (L == mspace::Engine::hqLatencyFor (48000.0) && worst < 0.5 && lag >= 0 && lag <= 12,
                   juce::String ("HARMONIC COLOR ") + (pos ? "PRE" : "POST") + " in STUDIO HQ: the LIVE colour (band differences 80 Hz..16 kHz:" + bands
                   + " dB), aligned by the reported " + juce::String (L) + " samples (LIVE all-pass lag " + juce::String (lag) + " samples)");
        }

        // position change while audio runs, module on: faded, finite
        {
            auto q = makeProcessor (48000, 128);
            loadPreset (*q, "factory:Vocal Spaces/ATMOS Vocal Glow");
            setMany (*q, { { ParamIDs::hcOn, 1 }, { ParamIDs::hcChar, 2 }, { ParamIDs::hcDrive, 60 } });
            pump (50);
            auto sig = phrases (48000, 3.0);
            juce::MidiBuffer midi;
            for (int pos = 0, k = 0; pos < sig.getNumSamples(); pos += 128, ++k)
            {
                if (k % 120 == 60) setP (*q, ParamIDs::hcPos, (float) ((k / 120) % 2 == 0));
                juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, juce::jmin (128, sig.getNumSamples() - pos));
                q->processBlock (view, midi);
            }
            check (allFinite (sig) && peak (sig) < 4.0f, "HARMONIC COLOR POST <-> PRE while audio runs: finite (peak " + juce::String (peak (sig), 2) + ")");
        }
    }
   #endif

   #ifdef MAS_EQ
#ifdef MAS_STUDIO_DYN
    //==========================================================================
    section ("1.7d SIGNAL RAILS: INPUT / OUTPUT meters (measurement only)");
    {
        constexpr double kPi = 3.141592653589793;
        auto sineBuf = [&] (double fs, double secs, float db, float rightDb, double hz = 1000.0)
        {
            juce::AudioBuffer<float> b (2, (int) (fs * secs));
            const float a = juce::Decibels::decibelsToGain (db, -200.0f), ar = juce::Decibels::decibelsToGain (rightDb, -200.0f);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = (float) std::sin (2.0 * kPi * hz * i / fs);
                b.setSample (0, i, a * v); b.setSample (1, i, ar * v);
            }
            return b;
        };
        auto setP = [] (MasterSpaceAudioProcessor& pp, const juce::String& id, float plain)
        { if (auto* r = pp.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        auto rms = [] (MasterSpaceAudioProcessor& pp, int io, int c) { return pp.ioMeters.rmsDb[io][c].load(); };
        auto pk = [] (MasterSpaceAudioProcessor& pp, int io, int c)
        { return juce::Decibels::gainToDecibels (pp.ioMeters.takePeak ((mas::IOMeters::Io) io, c), -200.0f); };
        // the independent reference: the same 300 ms averaging done offline on the real samples
        auto refRmsDb = [] (const juce::AudioBuffer<float>& b, int c, double fs, int block)
        {
            double m = 0.0;
            for (int pos = 0; pos < b.getNumSamples(); pos += block)
            {
                const int n = juce::jmin (block, b.getNumSamples() - pos);
                double e = 0.0;
                for (int i = 0; i < n; ++i) e += (double) b.getSample (c, pos + i) * b.getSample (c, pos + i);
                m += (1.0 - std::exp (-(double) n / (0.3 * fs))) * (e / n - m);
            }
            return m > 1.0e-14 ? 10.0 * std::log10 (m) : -120.0;
        };

        // ---- -18 dBFS sine at 44.1 / 48 / 96 kHz: RMS -21.0 dBFS, sample peak -18.0 dBFS, both channels, both sides
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            auto pr = makeProcessor (fs, 512);
            setP (*pr, ParamIDs::mix, 0.0f);
            pr->ioMeters.wanted = true;
            auto b = sineBuf (fs, 3.0, -18.0f, -18.0f, 997.0);
            run (*pr, b, 512);
            const float ri = rms (*pr, 0, 0), ri2 = rms (*pr, 0, 1), pi = pk (*pr, 0, 0);
            check (std::abs (ri + 21.03f) < 0.1f && std::abs (ri2 - ri) < 0.01f && std::abs (pi + 18.0f) < 0.05f,
                   juce::String (fs / 1000.0, 1) + " kHz: -18 dBFS sine -> INPUT RMS " + juce::String (ri, 2) + " / " + juce::String (ri2, 2)
                   + " dBFS, sample peak " + juce::String (pi, 2) + " dBFS");
            const double refL = refRmsDb (b, 0, fs, 512), refR = refRmsDb (b, 1, fs, 512);
            const float ro = rms (*pr, 1, 0), ro2 = rms (*pr, 1, 1);
            check (std::abs (ro - (float) refL) < 0.05f && std::abs (ro2 - (float) refR) < 0.05f && std::abs (pk (*pr, 1, 0) - juce::Decibels::gainToDecibels (b.getMagnitude (0, 0, b.getNumSamples()))) < 0.05f,
                   "  OUTPUT reads what really leaves the plugin: RMS " + juce::String (ro, 2) + " (offline " + juce::String (refL, 2) + "), peak matches");
        }

        // ---- the result does not depend on the host block size; mono / stereo; channels are independent
        {
            float r64 = 0, r4096 = 0;
            for (int blk : { 64, 4096 })
            {
                auto pr = makeProcessor (48000.0, blk);
                pr->ioMeters.wanted = true;
                auto b = sineBuf (48000.0, 3.0, -18.0f, -30.0f);
                run (*pr, b, blk);
                (blk == 64 ? r64 : r4096) = rms (*pr, 0, 0);
                if (blk == 64)
                    check (std::abs (rms (*pr, 0, 1) + 33.03f) < 0.1f && std::abs (pk (*pr, 0, 1) + 30.0f) < 0.05f && std::abs (pk (*pr, 0, 0) + 18.0f) < 0.05f,
                           "stereo: L -18 / R -30 dBFS sine read separately (R RMS " + juce::String (rms (*pr, 0, 1), 2) + ")");
            }
            check (std::abs (r64 - r4096) < 0.1f, "same reading with 64- and 4096-sample blocks (" + juce::String (r64, 2) + " / " + juce::String (r4096, 2) + ")");

            auto pr = std::make_unique<MasterSpaceAudioProcessor>();
            auto mono = pr->getBusesLayout();
            mono.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            const bool monoOk = pr->setBusesLayout (mono) && pr->getMainBusNumInputChannels() == 1;
            pr->setRateAndBufferSizeDetails (48000.0, 512);
            pr->prepareToPlay (48000.0, 512);
            pr->ioMeters.wanted = true;
            auto b = sineBuf (48000.0, 3.0, -18.0f, -18.0f);
            b.clear (1, 0, b.getNumSamples());          // channel 1 is output only with a mono input
            run (*pr, b, 512);
            check (monoOk && std::abs (rms (*pr, 0, 0) + 21.03f) < 0.1f && std::abs (rms (*pr, 0, 1) - rms (*pr, 0, 0)) < 0.001f,
                   "mono input: INPUT shows the one channel on L and R (" + juce::String (rms (*pr, 0, 1), 2) + " dBFS)");
            check (std::abs (rms (*pr, 1, 1) - (float) refRmsDb (b, 1, 48000.0, 512)) < 0.05f && rms (*pr, 1, 1) > -60.0f,
                   "mono input: OUTPUT R is the real right output (" + juce::String (rms (*pr, 1, 1), 2) + " dBFS)");
        }

        // ---- the host changes sample rate and block size on a running instance: still exact
        {
            auto pr = makeProcessor (44100.0, 256);
            pr->ioMeters.wanted = true;
            auto a = sineBuf (44100.0, 2.0, -12.0f, -12.0f);
            run (*pr, a, 256);
            pr->releaseResources();
            pr->setRateAndBufferSizeDetails (96000.0, 1024);
            pr->prepareToPlay (96000.0, 1024);
            auto b = sineBuf (96000.0, 3.0, -18.0f, -18.0f);
            pk (*pr, 0, 0);
            run (*pr, b, 1024);
            check (std::abs (rms (*pr, 0, 0) + 21.03f) < 0.1f && std::abs (pk (*pr, 0, 0) + 18.0f) < 0.05f,
                   "44.1 kHz / 256 -> 96 kHz / 1024 on the same instance: " + juce::String (rms (*pr, 0, 0), 2) + " dBFS RMS");
        }

        // ---- transients: one sample at -6 dBFS is caught exactly, wherever it sits in the block
        {
            auto pr = makeProcessor (48000.0, 512);
            pr->ioMeters.wanted = true;
            bool all = true;
            for (int pos : { 0, 1, 255, 511, 3001 })
            {
                auto b = silence (48000.0, 0.2);
                b.setSample (0, pos, -0.5f);
                b.setSample (1, pos, 0.25f);
                pk (*pr, 0, 0); pk (*pr, 0, 1);
                run (*pr, b, 512);
                all = all && std::abs (pk (*pr, 0, 0) + 6.02f) < 0.01f && std::abs (pk (*pr, 0, 1) + 12.04f) < 0.01f;
            }
            check (all, "a single-sample transient (-6 / -12 dBFS) is caught exactly at any position in the block");
            check (rms (*pr, 0, 0) < -40.0f, "  and barely moves the RMS body (" + juce::String (rms (*pr, 0, 0), 1) + " dBFS)");
        }

        // ---- silence: exact floor, no denormals; a full-scale sine reads 0 dBFS peak (overload)
        {
            auto pr = makeProcessor (48000.0, 512);
            pr->ioMeters.wanted = true;
            auto loud = sineBuf (48000.0, 1.0, 0.0f, 0.0f);
            run (*pr, loud, 512);
            check (std::abs (pk (*pr, 0, 0)) < 0.01f && rms (*pr, 0, 0) > -3.2f, "full-scale sine: INPUT peak 0.00 dBFS, RMS -3.0 dBFS");
            auto sil = silence (48000.0, 12.0);
            run (*pr, sil, 512);
            check (rms (*pr, 0, 0) <= -119.9f && pk (*pr, 0, 0) < -199.0f, "after silence: INPUT exactly at the floor, no peak");
            check (rms (*pr, 1, 0) < -60.0f, "after silence: OUTPUT below the scale (reverb tail decayed: " + juce::String (rms (*pr, 1, 0), 1) + " dBFS)");
        }

        // ---- HARMONIC COLOR on / off, MIX 100 %: OUTPUT always matches the real output, INPUT never changes
        {
            for (int hc = 0; hc < 2; ++hc)
            {
                auto pr = makeProcessor (48000.0, 512);
                setP (*pr, ParamIDs::mix, 100.0f);
                setP (*pr, ParamIDs::hcOn, (float) hc);
                setP (*pr, ParamIDs::hcMix, 100.0f);
                setP (*pr, ParamIDs::hcDrive, 80.0f);
                pr->ioMeters.wanted = true;
                auto b = pinkish (48000.0, 4.0, 77);
                auto in = b;
                run (*pr, b, 512);
                check (std::abs (rms (*pr, 1, 0) - (float) refRmsDb (b, 0, 48000.0, 512)) < 0.05f
                       && std::abs (rms (*pr, 0, 0) - (float) refRmsDb (in, 0, 48000.0, 512)) < 0.05f
                       && std::abs (pk (*pr, 1, 0) - juce::Decibels::gainToDecibels (b.getMagnitude (0, 0, b.getNumSamples()))) < 0.05f,
                       juce::String ("HARMONIC COLOR ") + (hc ? "ON" : "OFF") + ": OUTPUT " + juce::String (rms (*pr, 1, 0), 2)
                       + " dBFS = real output, INPUT " + juce::String (rms (*pr, 0, 0), 2) + " dBFS = real input");
            }
        }

        // ---- host bypass: the audio is JUCE's default bypass; INPUT and OUTPUT both read the passing signal
        {
            auto pr = makeProcessor (48000.0, 512);
            pr->ioMeters.wanted = true;
            auto b = sineBuf (48000.0, 2.0, -18.0f, -24.0f);
            const auto orig = b;
            juce::MidiBuffer midi;
            for (int pos = 0; pos < b.getNumSamples(); pos += 512)
            {
                juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos));
                pr->processBlockBypassed (view, midi);
            }
            check (maxAbsDiff (b, orig) == 0.0f, "bypass: audio passes untouched");
            check (std::abs (rms (*pr, 0, 0) - rms (*pr, 1, 0)) < 0.001f && std::abs (rms (*pr, 0, 1) - rms (*pr, 1, 1)) < 0.001f && std::abs (rms (*pr, 1, 1) + 27.03f) < 0.1f,
                   "bypass: INPUT = OUTPUT (L " + juce::String (rms (*pr, 1, 0), 2) + ", R " + juce::String (rms (*pr, 1, 1), 2) + " dBFS)");
        }

        // ---- measurement never touches the audio; nothing is measured while no display asks
        {
            auto a = makeProcessor (48000.0, 512), b2 = makeProcessor (48000.0, 512);
            setP (*a, ParamIDs::hcOn, 1.0f); setP (*b2, ParamIDs::hcOn, 1.0f);
            a->ioMeters.wanted = true;
            auto x = pinkish (48000.0, 3.0, 5), y = x;
            run (*a, x, 512); run (*b2, y, 512);
            check (maxAbsDiff (x, y) == 0.0f, "meters on / off: output bit-identical");
            check (b2->ioMeters.blocks.load() == 0 && b2->ioMeters.rmsDb[0][0].load() <= -119.9f, "meters not wanted: nothing measured");
            auto nanBuf = silence (48000.0, 0.1);
            nanBuf.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
            juce::MidiBuffer midi;
            a->processBlockBypassed (nanBuf, midi);
            auto quiet = silence (48000.0, 12.0);
            run (*a, quiet, 512);
            check (std::isfinite (a->ioMeters.rmsDb[0][0].load()) && a->ioMeters.rmsDb[0][0].load() <= -119.9f, "a NaN sample cannot freeze the meter (recovers to the floor)");
        }

        // ---- no stale peak: MAIN shown -> peak near 0 dBFS -> MAIN hidden -> silence -> MAIN shown again
        {
            auto pr = makeProcessor (48000.0, 512);
            pr->ioMeters.wanted = true;
            auto hot = sineBuf (48000.0, 0.5, -0.3f, -0.3f);
            run (*pr, hot, 512);                          // a red peak is waiting in the atomics, nobody takes it
            pr->ioMeters.wanted = false;
            auto quiet = silence (48000.0, 1.0);
            run (*pr, quiet, 512);
            pr->ioMeters.wanted = true;
            auto quiet2 = silence (48000.0, 0.05);
            run (*pr, quiet2, 512);
            const float pin = pk (*pr, 0, 0), pout = pk (*pr, 1, 0);
            check (pin < -199.0f && pout < -12.0f && rms (*pr, 0, 0) <= -119.9f,
                   "MAIN hidden during a -0.3 dBFS peak, shown again in silence: INPUT no stale peak; OUTPUT only the real tail ("
                   + juce::String (pout, 1) + " dBFS)");
        }

        // ---- cost: measurement overhead per 512-sample block
        {
            auto on = makeProcessor (48000.0, 512), off = makeProcessor (48000.0, 512);
            on->ioMeters.wanted = true;
            auto xo = pinkish (48000.0, 20.0, 9), xf = xo;
            double t0 = nowMs(); run (*off, xf, 512); const double tOff = nowMs() - t0;
            t0 = nowMs(); run (*on, xo, 512); const double tOn = nowMs() - t0;
            mas::IOMeters m; m.wanted = true;
            auto z = pinkish (48000.0, 20.0, 10);
            t0 = nowMs();
            for (int pos = 0; pos + 512 <= z.getNumSamples(); pos += 512)
            {
                m.beginBlock();
                m.measure (mas::IOMeters::input, z.getReadPointer (0, pos), z.getReadPointer (1, pos), 512, 48000.0);
                m.measure (mas::IOMeters::output, z.getReadPointer (0, pos), z.getReadPointer (1, pos), 512, 48000.0);
                m.endBlock();
            }
            const double tM = nowMs() - t0;
            std::printf ("     20 s of audio: plugin %.1f ms (meters on %.1f ms); the meters alone %.2f ms = %.4f %% of real time\n", tOff, tOn, tM, tM / 200.0);
            check (tM < 0.01 * 20000.0, "meter cost < 1 % of real time even in this unoptimised check");
        }

        // ---- UI ballistics: instant attack, 0.8 s hold, smooth return, overload from -3 dBFS
        {
            mas::RailBallistics r;
            const float dt = 1.0f / 60.0f;
            r.tick (-20.0f, juce::Decibels::decibelsToGain (-1.0f), dt);
            const bool attack = std::abs (r.peakDb + 1.0f) < 1.0e-4f && r.over();
            float t = dt, heldUntil = 0.0f;
            while (t < 3.0f) { r.tick (-20.0f, juce::Decibels::decibelsToGain (-12.0f), dt); t += dt; if (r.peakDb > -1.001f) heldUntil = t; }
            check (attack, "peak marker: jumps to a new peak at once, red at -1 dBFS");
            check (heldUntil > 0.78f && heldUntil < 0.84f, "peak marker: held " + juce::String (heldUntil, 3) + " s (0.8 s)");
            check (std::abs (r.peakDb + 12.0f) < 0.01f && ! r.over(), "peak marker: returns smoothly to the running peak (-12 dBFS), not red");
            mas::RailBallistics s;
            s.tick (-20.0f, juce::Decibels::decibelsToGain (-3.0f), dt);
            mas::RailBallistics u;
            u.tick (-20.0f, juce::Decibels::decibelsToGain (-3.1f), dt);
            check (s.over() && ! u.over(), "overload: red from -3.0 dBFS, not at -3.1 dBFS");
            mas::RailBallistics f;
            f.tick (-6.0f, 0.5f, dt);
            float maxStep = 0.0f, prev = f.peakDb;
            for (int i = 0; i < 300; ++i) { f.tick (-120.0f, 0.0f, dt); maxStep = juce::jmax (maxStep, prev - f.peakDb); prev = f.peakDb; }
            check (maxStep < 0.5f && f.peakDb <= -69.9f && f.rmsDb < -69.0f, "return to silence is smooth (largest step " + juce::String (maxStep, 2) + " dB per frame at 60 Hz)");
        }
    }

   #ifdef MAS_STUDIO_18
    //==========================================================================
    section ("1.8 M/S HARMONIC COLOR: STEREO untouched, independent MID / SIDE");
    {
        constexpr double kPi = 3.141592653589793;
        auto setQ = [] (MasterSpaceAudioProcessor& q, const juce::String& id, float plain)
        { if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        // a stereo test signal: different material left / right (a real wide wet)
        auto wide = [] (double fs, double secs, int seed)
        {
            auto b = pinkish (fs, secs, seed);
            juce::Random r (seed + 99);
            float t = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                t = 0.96f * t + 0.04f * (r.nextFloat() * 2.0f - 1.0f);
                b.setSample (1, i, 0.6f * b.getSample (1, i) + 0.9f * t);
            }
            return b;
        };
        auto mDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int ch, bool side)
        {   // largest difference of MID (ch 0) or SIDE of two stereo buffers
            float m = 0.0f;
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const float x = side ? 0.5f * (a.getSample (0, i) - a.getSample (1, i)) : 0.5f * (a.getSample (0, i) + a.getSample (1, i));
                const float y = side ? 0.5f * (b.getSample (0, i) - b.getSample (1, i)) : 0.5f * (b.getSample (0, i) + b.getSample (1, i));
                m = juce::jmax (m, std::abs (x - y));
            }
            juce::ignoreUnused (ch);
            return m;
        };

        // ---- the slot on its own (LIVE and HQ, 44.1 / 48 / 96 kHz)
        for (int q : { 0, 1 })
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                const juce::String tag = juce::String (q ? "HQ   " : "LIVE ") + juce::String (fs / 1000.0, 1) + " kHz: ";
                auto run1 = [&] (const juce::AudioBuffer<float>& in, bool ms, hc::HarmonicColor::Settings h, hc::ColorSlot::MS m)
                {
                    hc::ColorSlot slot;
                    h.quality = q;
                    slot.prepare (fs);
                    slot.set (h, ms, m, true);
                    auto b = in;
                    for (int pos = 0; pos < b.getNumSamples(); pos += 256)
                    {
                        const int n = juce::jmin (256, b.getNumSamples() - pos);
                        slot.set (h, ms, m, false);
                        slot.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), n, 1.0f);
                    }
                    return b;
                };
                hc::HarmonicColor::Settings h; h.on = true; h.character = hc::HarmonicColor::Tube; h.drive = 0.8f; h.mix = 1.0f; h.levelComp = true;
                const auto in = wide (fs, 2.0, 7);

                // neutral M/S: MIX 0 gives what STEREO gives at MIX 0 (no level, width or phase change)
                {
                    // without LEVEL COMP the two paths are the same linear system: equal to rounding. With it, M/S follows
                    // MID and SIDE energies separately (as it should): only its tiny micro-corrections differ.
                    auto h0 = h; h0.mix = 0.0f; h0.levelComp = false;
                    const auto a = run1 (in, false, h0, {}), b = run1 (in, true, h0, { 0.8f, 0.8f, 1.0f, 1.0f });
                    h0.levelComp = true;
                    const auto c = run1 (in, false, h0, {}), d = run1 (in, true, h0, { 0.8f, 0.8f, 1.0f, 1.0f });
                    double e = 0.0, ref = 0.0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < c.getNumSamples(); ++i)
                        { const double x = c.getSample (ch, i), y = d.getSample (ch, i); e += (x - y) * (x - y); ref += x * x; }
                    const double relDb = 10.0 * std::log10 ((e + 1.0e-30) / (ref + 1.0e-30));
                    check (maxAbsDiff (a, b) < 2.0e-6f && relDb < -60.0, tag + "M/S at MIX 0 = STEREO at MIX 0: exact without LEVEL COMP (max diff "
                           + juce::String (maxAbsDiff (a, b), 8) + "), with it the difference is " + juce::String (relDb, 1) + " dB");
                }
                // mono in -> no SIDE is made; anti-phase in -> no MID is made
                {
                    auto mono = in; mono.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());
                    const auto o = run1 (mono, true, h, { 0.9f, 0.9f, 1.0f, 1.0f });
                    float side = 0.0f, mid = 0.0f;
                    for (int i = 0; i < o.getNumSamples(); ++i) { side = juce::jmax (side, std::abs (o.getSample (0, i) - o.getSample (1, i))); mid = juce::jmax (mid, std::abs (o.getSample (0, i))); }
                    check (side < 1.0e-9f && mid > 0.05f, tag + "mono in: L = R out (|L - R| " + juce::String (side, 12) + "), MID saturated");
                    auto anti = in; for (int i = 0; i < anti.getNumSamples(); ++i) anti.setSample (1, i, -anti.getSample (0, i));
                    const auto o2 = run1 (anti, true, h, { 0.9f, 0.9f, 1.0f, 1.0f });
                    float sum = 0.0f;
                    for (int i = 0; i < o2.getNumSamples(); ++i) sum = juce::jmax (sum, std::abs (o2.getSample (0, i) + o2.getSample (1, i)));
                    check (sum < 1.0e-9f, tag + "anti-phase in (L = -R): stays anti-phase (|L + R| " + juce::String (sum, 12) + ")");
                }
                // independence: SIDE settings never touch the MID result and vice versa (LEVEL COMP included)
                {
                    const auto a = run1 (in, true, h, { 0.8f, 0.1f, 1.0f, 0.2f }), b = run1 (in, true, h, { 0.8f, 1.0f, 1.0f, 1.0f });
                    const auto c = run1 (in, true, h, { 0.1f, 0.6f, 0.3f, 1.0f }), d = run1 (in, true, h, { 1.0f, 0.6f, 1.0f, 1.0f });
                    check (mDiff (a, b, 0, false) < 1.0e-6f && mDiff (a, b, 0, true) > 1.0e-3f, tag + "SIDE DRIVE / MIX change only the SIDE (MID diff " + juce::String (mDiff (a, b, 0, false), 9) + ")");
                    check (mDiff (c, d, 0, true) < 1.0e-6f && mDiff (c, d, 0, false) > 1.0e-3f, tag + "MID DRIVE / MIX change only the MID (SIDE diff " + juce::String (mDiff (c, d, 0, true), 9) + ")");
                }
                // the main SATURATION MIX scales both: at 0 % M/S is neutral whatever MID / SIDE MIX say
                {
                    auto h0 = h; h0.mix = 0.0f;
                    const auto a = run1 (in, true, h0, { 1.0f, 1.0f, 1.0f, 1.0f }), b = run1 (in, true, h, { 1.0f, 1.0f, 0.0f, 0.0f });
                    check (maxAbsDiff (a, b) < 2.0e-6f, tag + "SATURATION MIX 0 % = MID / SIDE MIX 0 %: no saturation anywhere");
                }
                // level: LEVEL COMP holds the energy of each part
                {
                    auto off = in;
                    if (q == 1) { hc::HarmonicColor::Settings ho = h; ho.on = false; off = run1 (in, false, ho, {}); }
                    const auto o = run1 (in, true, h, { 0.9f, 0.9f, 1.0f, 1.0f });
                    const int s0 = (int) fs / 2, len = o.getNumSamples() - s0;
                    auto rmsOf = [&] (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (std::sqrt (0.5f * (b.getRMSLevel (0, s0, len) * b.getRMSLevel (0, s0, len) + b.getRMSLevel (1, s0, len) * b.getRMSLevel (1, s0, len)))); };
                    const float dl = rmsOf (o) - rmsOf (off);
                    check (std::abs (dl) < 1.0f, tag + "M/S at DRIVE 90 %: level held by LEVEL COMP (" + juce::String (dl, 2) + " dB)");
                }
                // switching STEREO <-> M/S on a steady tone: no click (no step larger than the tone itself makes)
                {
                    juce::AudioBuffer<float> tone (2, (int) (fs * 1.2));
                    for (int i = 0; i < tone.getNumSamples(); ++i)
                    {
                        const float v = 0.3f * (float) std::sin (2.0 * kPi * 220.0 * i / fs);
                        tone.setSample (0, i, v); tone.setSample (1, i, 0.7f * v + 0.1f * (float) std::sin (2.0 * kPi * 330.0 * i / fs));
                    }
                    hc::ColorSlot slot; auto hq = h; hq.quality = q; hq.drive = 0.6f;
                    slot.prepare (fs); slot.set (hq, false, {}, true);
                    const hc::ColorSlot::MS ms { 0.9f, 0.9f, 1.0f, 1.0f };
                    float stepSteady = 0.0f, stepSwitch = 0.0f, prevL = 0.0f;
                    const int third = tone.getNumSamples() / 3;
                    for (int pos = 0; pos < tone.getNumSamples(); pos += 128)
                    {
                        const int nb = juce::jmin (128, tone.getNumSamples() - pos);
                        const bool msNow = pos >= third && pos < 2 * third;
                        slot.set (hq, msNow, ms, false);
                        slot.process (tone.getWritePointer (0, pos), tone.getWritePointer (1, pos), nb, 1.0f);
                        for (int i = 0; i < nb; ++i)
                        {
                            const int k = pos + i;
                            const float st = std::abs (tone.getSample (0, k) - prevL); prevL = tone.getSample (0, k);
                            const bool nearSwitch = std::abs (k - third) < (int) (0.08 * fs) || std::abs (k - 2 * third) < (int) (0.08 * fs);
                            if (k > (int) (0.2 * fs)) (nearSwitch ? stepSwitch : stepSteady) = juce::jmax (nearSwitch ? stepSwitch : stepSteady, st);
                        }
                    }
                    check (allFinite (tone) && stepSwitch < 1.25f * stepSteady, tag + "STEREO -> M/S -> STEREO while a tone plays: no click (largest step "
                           + juce::String (stepSwitch, 4) + " vs " + juce::String (stepSteady, 4) + " steady)");
                }
            }

        // ---- in the plugin: M/S selected but HARMONIC COLOR OFF = bit-identical to 1.7d; latency unchanged
        for (int q : { 0, 1 })
        {
            auto a = makeProcessor (48000.0, 256), b = makeProcessor (48000.0, 256);
            for (auto* pr : { a.get(), b.get() }) { pr->getPresetManager().load (pr->getPresetManager().indexOf ("factory:Halls/Concert Hall")); setQ (*pr, ParamIDs::quality, (float) q); }
            setQ (*b, ParamIDs::hcMode, 1.0f); setQ (*b, ParamIDs::hcMidDrive, 90.0f); setQ (*b, ParamIDs::hcSideMix, 20.0f);
            pump (100);
            auto x = pinkish (48000.0, 3.0, 4), y = x;
            run (*a, x, 256); run (*b, y, 256);
            check (maxAbsDiff (x, y) == 0.0f && a->getLatencySamples() == b->getLatencySamples(),
                   juce::String (q ? "STUDIO HQ" : "LIVE") + ": M/S selected, HARMONIC COLOR OFF -> bit-identical, latency " + juce::String (b->getLatencySamples()));
        }
        // ---- in the plugin: M/S ON, POST and PRE, finite, latency the same as STEREO; preset loads STEREO
        for (int pos : { 0, 1 })
        {
            auto a = makeProcessor (48000.0, 256);
            a->getPresetManager().load (a->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            setQ (*a, ParamIDs::quality, 1.0f); setQ (*a, ParamIDs::hcOn, 1.0f); setQ (*a, ParamIDs::hcPos, (float) pos);
            setQ (*a, ParamIDs::hcMode, 1.0f); setQ (*a, ParamIDs::hcMidDrive, 80.0f); setQ (*a, ParamIDs::hcSideDrive, 20.0f);
            pump (100);
            const int lat = a->getLatencySamples();
            auto x = wide (48000.0, 3.0, 12);
            run (*a, x, 256);
            check (allFinite (x) && peak (x) > 0.01f && lat == a->getLatencySamples(),
                   juce::String (pos ? "PRE" : "POST") + ": M/S ON in STUDIO HQ runs clean, latency unchanged (" + juce::String (lat) + ")");
        }
        {
            auto a = makeProcessor (48000.0, 256);
            setQ (*a, ParamIDs::hcMode, 1.0f); setQ (*a, ParamIDs::hcMidMix, 40.0f);
            a->getPresetManager().load (a->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            pump (50);
            const bool presetOk = a->apvts.getRawParameterValue (ParamIDs::hcMode)->load() < 0.5f
                                  && std::abs (a->apvts.getRawParameterValue (ParamIDs::hcMidMix)->load() - 100.0f) < 0.01f;
            juce::MemoryBlock mb;
            auto fresh = makeProcessor (48000.0, 256);
            fresh->getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            for (auto* e : xml->getChildIterator()) if (e->getStringAttribute ("id").startsWith ("hcm") || e->getStringAttribute ("id").startsWith ("hcs")) e->setAttribute ("id", "zz");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
            setQ (*a, ParamIDs::hcMode, 1.0f);
            a->setStateInformation (old.getData(), (int) old.getSize());
            pump (50);
            check (presetOk && a->apvts.getRawParameterValue (ParamIDs::hcMode)->load() < 0.5f,
                   "an older preset / project (no 1.8 IDs) loads STEREO with every M/S value at its default");
        }
        // ---- CPU: M/S costs about what STEREO costs
        {
            hc::HarmonicColor::Settings h; h.on = true; h.drive = 0.6f; h.quality = 1;
            double t[2] {};
            for (int ms = 0; ms < 2; ++ms)
            {
                hc::ColorSlot slot; slot.prepare (48000.0); slot.set (h, ms == 1, {}, true);
                auto b = pinkish (48000.0, 10.0, 3);
                const double t0 = nowMs();
                for (int pos = 0; pos + 256 <= b.getNumSamples(); pos += 256) slot.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), 256, 1.0f);
                t[ms] = nowMs() - t0;
            }
            std::printf ("     HARMONIC COLOR HQ, 10 s stereo at 48 kHz: STEREO %.1f ms, M/S %.1f ms\n", t[0], t[1]);
            check (t[1] < 1.35 * t[0] + 5.0, "M/S costs about what STEREO costs (" + juce::String (t[1] / juce::jmax (1.0, t[0]), 2) + "x)");
        }
    }

    //==========================================================================
    section ("1.8 M/S OUTPUT COMPRESSOR: the module");
    {
        constexpr double kPi = 3.141592653589793;
        auto sine = [] (double fs, double secs, float dbL, float dbR, double hz = 1000.0)
        {
            juce::AudioBuffer<float> b (2, (int) (fs * secs));
            const float a = juce::Decibels::decibelsToGain (dbL, -300.0f), c = juce::Decibels::decibelsToGain (dbR, -300.0f);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = (float) std::sin (2.0 * kPi * hz * i / fs);
                b.setSample (0, i, a * v); b.setSample (1, i, c * v);
            }
            return b;
        };
        auto runComp = [] (msc::MSCompressor& c, juce::AudioBuffer<float>& b, int block = 256)
        {
            for (int pos = 0; pos < b.getNumSamples(); pos += block)
            {
                const int n = juce::jmin (block, b.getNumSamples() - pos);
                c.process (b.getReadPointer (0, pos), b.getReadPointer (1, pos), b.getWritePointer (0, pos), b.getWritePointer (1, pos), n, c.routing());
            }
        };
        auto peakDb = [] (const juce::AudioBuffer<float>& b, int ch, int from)
        { return juce::Decibels::gainToDecibels (b.getMagnitude (ch, from, b.getNumSamples() - from), -200.0f); };
        auto msOf = [] (const juce::AudioBuffer<float>& b, bool side, int from)
        {
            float m = 0.0f;
            for (int i = from; i < b.getNumSamples(); ++i) m = juce::jmax (m, std::abs (0.5f * (b.getSample (0, i) + (side ? -1.0f : 1.0f) * b.getSample (1, i))));
            return juce::Decibels::gainToDecibels (m, -200.0f);
        };
        auto base = []
        {
            msc::Settings s; s.on = true; s.hpfOn = false; s.mix = 1.0f;
            s.mid  = { -24.0f, 4.0f, 1.0f, 50.0f, 0.0f, 0.0f };
            s.side = { -24.0f, 4.0f, 1.0f, 50.0f, 0.0f, 0.0f };
            return s;
        };
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const juce::String tag = juce::String (fs / 1000.0, 1) + " kHz: ";
            const int settle = (int) (0.6 * fs);
            // static curve, MID only (L = R): -12 dBFS sine, threshold -24, 4:1, hard knee -> -21 dBFS, SIDE stays 0
            {
                msc::MSCompressor c; c.prepare (fs); c.set (base(), true);
                auto b = sine (fs, 1.0, -12.0f, -12.0f);
                runComp (c, b);
                float lr = 0.0f; for (int i = 0; i < b.getNumSamples(); ++i) lr = juce::jmax (lr, std::abs (b.getSample (0, i) - b.getSample (1, i)));
                check (std::abs (msOf (b, false, settle) + 21.0f) < 0.3f && lr == 0.0f && std::abs (c.grMid.load() - 9.0f) < 0.3f && c.grSide.load() == 0.0f,
                       tag + "MID: -12 dBFS sine, -24 dBFS / 4:1 -> " + juce::String (msOf (b, false, settle), 2) + " dBFS (expected -21), GR "
                       + juce::String (c.grMid.load(), 2) + " dB; L = R kept exactly; SIDE GR 0");
            }
            // SIDE only (L = -R): its own curve, no MID created
            {
                auto st = base(); st.side.ratio = 2.0f;
                msc::MSCompressor c; c.prepare (fs); c.set (st, true);
                auto b = sine (fs, 1.0, -12.0f, -12.0f);
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (1, i, -b.getSample (0, i));
                runComp (c, b);
                float sum = 0.0f; for (int i = 0; i < b.getNumSamples(); ++i) sum = juce::jmax (sum, std::abs (b.getSample (0, i) + b.getSample (1, i)));
                check (std::abs (msOf (b, true, settle) + 18.0f) < 0.3f && sum == 0.0f && c.grMid.load() == 0.0f,
                       tag + "SIDE (L = -R): -12 dBFS, -24 dBFS / 2:1 -> " + juce::String (msOf (b, true, settle), 2) + " dBFS (expected -18); no MID made");
            }
            // independence: a loud MID and a quiet SIDE; the SIDE passes bit-exact whatever MID does
            {
                juce::AudioBuffer<float> a (2, (int) fs);
                juce::Random r (3);
                for (int i = 0; i < a.getNumSamples(); ++i)
                {
                    const float m = 0.5f * (float) std::sin (2.0 * kPi * 200.0 * i / fs), sd = 0.003f * (r.nextFloat() - 0.5f);
                    a.setSample (0, i, m + sd); a.setSample (1, i, m - sd);
                }
                auto b1 = a, b2 = a;
                auto s1 = base(); auto s2 = base(); s2.mid = { -40.0f, 10.0f, 0.1f, 30.0f, 12.0f, 6.0f };
                msc::MSCompressor c1, c2; c1.prepare (fs); c1.set (s1, true); c2.prepare (fs); c2.set (s2, true);
                runComp (c1, b1); runComp (c2, b2);
                float dS = 0.0f;
                for (int i = 0; i < a.getNumSamples(); ++i)
                    dS = juce::jmax (dS, std::abs ((b1.getSample (0, i) - b1.getSample (1, i)) - (b2.getSample (0, i) - b2.getSample (1, i))));
                check (dS < 1.0e-7f && msOf (b1, false, 0) > msOf (b2, false, 0) + 3.0f,
                       tag + "MID and SIDE independent: heavy MID settings leave the quiet SIDE untouched (diff " + juce::String (dS, 9) + ")");
            }
            // 1:1, PARALLEL MIX 0 %: the output is the input, bit for bit
            {
                auto s1 = base(); s1.mid.ratio = s1.side.ratio = 1.0f;
                auto s2 = base(); s2.mix = 0.0f;
                msc::MSCompressor c1, c2; c1.prepare (fs); c1.set (s1, true); c2.prepare (fs); c2.set (s2, true);
                const auto in = pinkish (fs, 1.0, 8);
                auto b1 = in, b2 = in;
                runComp (c1, b1); runComp (c2, b2);
                check (maxAbsDiff (b1, in) == 0.0f && maxAbsDiff (b2, in) == 0.0f, tag + "RATIO 1:1 and PARALLEL MIX 0 %: bit-identical to the input");
            }
            // PARALLEL MIX 50 %: half of the change
            {
                auto s1 = base(); auto s2 = base(); s2.mix = 0.5f;
                msc::MSCompressor c1, c2; c1.prepare (fs); c1.set (s1, true); c2.prepare (fs); c2.set (s2, true);
                const auto in = sine (fs, 1.0, -6.0f, -10.0f);
                auto b1 = in, b2 = in;
                runComp (c1, b1); runComp (c2, b2);
                float e = 0.0f;
                for (int i = settle; i < in.getNumSamples(); ++i) e = juce::jmax (e, std::abs ((b2.getSample (0, i) - in.getSample (0, i)) - 0.5f * (b1.getSample (0, i) - in.getSample (0, i))));
                check (e < 1.0e-5f, tag + "PARALLEL MIX 50 % = half of the change (error " + juce::String (e, 8) + ")");
            }
            // attack / release in time: 63 % of the final GR after about ATTACK (+ the 5 ms detector), RELEASE likewise
            {
                auto st = base(); st.mid = { -30.0f, 10.0f, 10.0f, 200.0f, 0.0f, 0.0f };
                msc::MSCompressor c; c.prepare (fs); c.set (st, true);
                auto b = sine (fs, 2.0, -40.0f, -40.0f);
                for (int i = (int) (0.5 * fs); i < (int) (1.5 * fs); ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, b.getSample (ch, i) * juce::Decibels::decibelsToGain (30.0f));
                std::vector<float> gr;
                for (int pos = 0; pos < b.getNumSamples(); pos += 16)
                {
                    c.process (b.getReadPointer (0, pos), b.getReadPointer (1, pos), b.getWritePointer (0, pos), b.getWritePointer (1, pos), 16, 0);
                    gr.push_back (c.grMid.load());
                }
                auto at = [&] (double t) { return gr[(size_t) juce::jlimit (0, (int) gr.size() - 1, (int) (t * fs / 16))]; };
                const float fin = at (1.45);
                const float a1 = at (0.5 + 0.015) / fin, r1 = at (1.5 + 0.2) / fin;
                check (fin > 15.0f && a1 > 0.45f && a1 < 0.85f && r1 > 0.25f && r1 < 0.5f,
                       tag + "ATTACK 10 ms: " + juce::String (a1 * 100.0f, 0) + " % of the GR after 15 ms; RELEASE 200 ms: "
                       + juce::String (r1 * 100.0f, 0) + " % left after 200 ms (GR " + juce::String (fin, 1) + " dB)");
            }
            // side-chain high-pass: a 50 Hz bass drives the detector far less (the bass itself is not filtered)
            {
                auto s1 = base(); auto s2 = base(); s2.hpfOn = true; s2.hpfHz = 150.0f;
                msc::MSCompressor c1, c2; c1.prepare (fs); c1.set (s1, true); c2.prepare (fs); c2.set (s2, true);
                auto b1 = sine (fs, 1.0, -10.0f, -10.0f, 50.0), b2 = b1;
                runComp (c1, b1); runComp (c2, b2);
                check (c1.grMid.load() > c2.grMid.load() + 6.0f, tag + "SC HPF 150 Hz: GR on a 50 Hz bass " + juce::String (c2.grMid.load(), 1)
                       + " dB instead of " + juce::String (c1.grMid.load(), 1) + " dB");
            }
        }
        // ON / OFF and WET <-> FULL while a loud tone is compressed by 10 dB: smooth, no click
        {
            const double fs = 48000.0;
            auto st = base(); st.mid = { -30.0f, 4.0f, 5.0f, 100.0f, 0.0f, 0.0f };
            msc::MSCompressor c; c.prepare (fs); c.set (st, true);
            auto b = sine (fs, 2.0, -6.0f, -9.0f, 220.0);
            float stepSwitch = 0.0f, stepSteady = 0.0f, prev = 0.0f;
            for (int pos = 0; pos < b.getNumSamples(); pos += 128)
            {
                const double t = pos / fs;
                st.on = ! (t > 0.5 && t < 1.0);
                st.routing = t > 1.4 ? 1 : 0;
                c.set (st);
                c.process (b.getReadPointer (0, pos), b.getReadPointer (1, pos), b.getWritePointer (0, pos), b.getWritePointer (1, pos), 128, c.routing());
                for (int i = 0; i < 128; ++i)
                {
                    const double ti = (pos + i) / fs;
                    const float d = std::abs (b.getSample (0, pos + i) - prev); prev = b.getSample (0, pos + i);
                    const bool near = std::abs (ti - 0.5) < 0.05 || std::abs (ti - 1.0) < 0.05 || std::abs (ti - 1.4) < 0.08;
                    if (ti > 0.3) (near ? stepSwitch : stepSteady) = juce::jmax (near ? stepSwitch : stepSteady, d);
                }
            }
            check (stepSwitch < 1.05f * juce::Decibels::decibelsToGain (-6.0f) * (float) (2.0 * kPi * 220.0 / fs) && allFinite (b),
                   "ON / OFF and WET ONLY <-> FULL OUTPUT while compressing 10 dB: no click (largest step " + juce::String (stepSwitch, 4)
                   + ", steady " + juce::String (stepSteady, 4) + ")");
        }
        // NaN / inf: never stuck
        {
            msc::MSCompressor c; c.prepare (48000.0); c.set (base(), true);
            auto b = pinkish (48000.0, 0.2, 2);
            b.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
            b.setSample (1, 200, std::numeric_limits<float>::infinity());
            runComp (c, b);
            auto ok = sine (48000.0, 1.0, -40.0f, -40.0f), ref = ok;
            runComp (c, ok);
            float tail = 0.0f;
            for (int ch = 0; ch < 2; ++ch) for (int i = 36000; i < ok.getNumSamples(); ++i) tail = juce::jmax (tail, std::abs (ok.getSample (ch, i) - ref.getSample (ch, i)));
            check (std::isfinite (c.grMid.load()) && allFinite (ok) && tail < 1.0e-6f,
                   "a NaN / inf sample does not leave the compressor stuck: finite, back to unity below the threshold after the release (diff "
                   + juce::String (tail, 9) + ")");
        }
    }

    //==========================================================================
    section ("1.8 M/S OUTPUT COMPRESSOR: in the plugin");
    {
        auto setQ = [] (MasterSpaceAudioProcessor& q, const juce::String& id, float plain)
        { if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        auto heavy = [&] (MasterSpaceAudioProcessor& q, int routing)
        {
            setQ (q, ParamIDs::cpOn, 1.0f); setQ (q, ParamIDs::cpRouting, (float) routing);
            setQ (q, ParamIDs::cpMidThr, -40.0f); setQ (q, ParamIDs::cpMidRatio, 8.0f); setQ (q, ParamIDs::cpMidAtk, 2.0f);
            setQ (q, ParamIDs::cpSideThr, -40.0f); setQ (q, ParamIDs::cpSideRatio, 8.0f); setQ (q, ParamIDs::cpSideAtk, 2.0f);
        };
        const auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms18-comp-ir.wav");
        {
            juce::AudioBuffer<float> ir (2, 19200);
            juce::Random rng (11);
            for (int i = 0; i < ir.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) ir.setSample (c, i, (rng.nextFloat() - 0.5f) * std::exp (-5.0f * (float) i / ir.getNumSamples()));
            irFile.deleteFile();
            std::unique_ptr<juce::OutputStream> os (irFile.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> wr (juce::WavAudioFormat().createWriterFor (os.get(), 48000.0, 2, 24, {}, 0));
            if (wr != nullptr) { os.release(); wr->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()); }
        }
        // kind: 0 ALGO 1.2, 1 ALGO 1.1 legacy, 2 IR
        auto make = [&] (int kind, int routing, int quality)
        {
            auto q = makeProcessor (48000.0, 256);
            q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            setQ (*q, ParamIDs::routing, (float) routing); setQ (*q, ParamIDs::quality, (float) quality);
            if (kind == 1) setQ (*q, ParamIDs::engine, 0.0f);
            if (kind == 2) { q->loadImpulseResponse (irFile); setQ (*q, ParamIDs::mode, 1.0f); }
            pump (300);
            // the IR loads in the background: wait until the audio thread really has it (not a fixed time)
            for (int k = 0; k < 400 && kind == 2 && ! q->isIRActiveOnAudio(); ++k) { auto w0 = silence (48000.0, 0.01); run (*q, w0, 256); pump (20); }
            // juce::dsp::Convolution swaps the IR on its own thread and cross-fades to it: let that finish too
            for (int k = 0; k < 60 && kind == 2; ++k) { auto w1 = silence (48000.0, 0.01); run (*q, w1, 256); pump (15); }
            auto w = silence (48000.0, 0.3); run (*q, w, 256); pump (100);
            return q;
        };
        const char* kinds[3] = { "ALGO 1.2", "ALGO 1.1", "IR" };

        // OFF by default; ON with 1:1 = bit-identical, in every path
        for (int kind = 0; kind < 3; ++kind)
            for (int routing : { 0, 1 })
            {
                auto a = make (kind, routing, 0), b = make (kind, routing, 0);
                const bool defOff = a->apvts.getRawParameterValue (ParamIDs::cpOn)->load() < 0.5f;
                setQ (*b, ParamIDs::cpOn, 1.0f); setQ (*b, ParamIDs::cpMidRatio, 1.0f); setQ (*b, ParamIDs::cpSideRatio, 1.0f);
                pump (50);
                auto x = pinkish (48000.0, 2.0, 5), y = x;
                run (*a, x, 256); run (*b, y, 256);
                // IR: JUCE's convolution is reproducible between two instances only to the last float bit (-144 dB,
                // with or without the compressor), so IR is checked to -120 dB; the algorithmic paths bit for bit
                const float tolOff = kind == 2 ? 1.0e-6f : 0.0f;
                check (defOff && maxAbsDiff (x, y) <= tolOff, juce::String (kinds[kind]) + (routing ? " AUX" : " INSERT")
                       + ": COMP OFF by default; ON at 1:1 (WET ONLY) = " + (kind == 2 ? "OFF to -120 dB" : "bit-identical to OFF")
                       + " (diff " + juce::String (maxAbsDiff (x, y), 9) + ")");
            }

        // WET ONLY never touches the dry: INSERT with the reverb MIX at 0 % -> identical to OFF; FULL OUTPUT does compress it
        {
            auto a = make (0, 0, 0), b = make (0, 0, 0), c = make (0, 0, 0);
            for (auto* q : { a.get(), b.get(), c.get() }) setQ (*q, ParamIDs::mix, 0.0f);
            heavy (*b, 0); heavy (*c, 1);
            pump (50);
            auto x = pinkish (48000.0, 2.0, 6), y = x, z = x;
            run (*a, x, 256); run (*b, y, 256); run (*c, z, 256);
            check (maxAbsDiff (x, y) == 0.0f && maxAbsDiff (x, z) > 0.01f, "INSERT, reverb MIX 0 %: WET ONLY leaves the dry bit-identical; FULL OUTPUT compresses it (diff "
                   + juce::String (maxAbsDiff (x, z), 3) + ")");
        }
        // the wet tap is the real wet: in AUX (output = wet) WET ONLY and FULL OUTPUT give the same result, in every path, LIVE and HQ
        for (int kind = 0; kind < 3; ++kind)
            for (int quality : { 0, 1 })
            {
                auto b = make (kind, 1, quality), c = make (kind, 1, quality), off = make (kind, 1, quality);
                heavy (*b, 0); heavy (*c, 1);
                pump (50);
                auto x = pinkish (48000.0, 2.0, 7), y = x, z = x;
                run (*b, x, 256);
                run (*c, y, 256);
                run (*off, z, 256);
                const float d = maxAbsDiff (x, y), comp = maxAbsDiff (x, z);
                // IR in AUX keeps a -86 dB dry leak of its own (its float MIX smoothing settles at 0.99997): FULL OUTPUT also
                // compresses that, WET ONLY rightly does not. The algorithmic paths must match exactly.
                const float tol = kind == 2 ? 1.0e-4f : 1.0e-6f;
                check (d < tol && comp > 1.0e-3f && b->getLatencySamples() == off->getLatencySamples(),
                       juce::String (kinds[kind]) + (quality ? " HQ  " : " LIVE") + ", AUX: WET ONLY = FULL OUTPUT (diff " + juce::String (d, 7)
                       + ", compression " + juce::String (comp, 3) + "), latency unchanged (" + juce::String (b->getLatencySamples()) + ")");
            }
        // transitions while compressing: preset change, 1.2 <-> 1.1, ALGO <-> IR, LIVE <-> HQ: finite and without clicks
        {
            auto q = make (0, 0, 0);
            heavy (*q, 0);
            q->loadImpulseResponse (irFile);
            pump (300);
            juce::AudioBuffer<float> x (2, 48000 * 6);
            for (int i = 0; i < x.getNumSamples(); ++i)
            {
                const float v = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 180.0 * i / 48000.0);
                x.setSample (0, i, v); x.setSample (1, i, 0.8f * v);
            }
            float big = 0.0f, prev = 0.0f;
            juce::MidiBuffer midi;
            for (int pos = 0, k = 0; pos < x.getNumSamples(); pos += 256, ++k)
            {
                if (k == 60)  q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"));
                if (k == 300) setQ (*q, ParamIDs::engine, 0.0f);
                if (k == 480) setQ (*q, ParamIDs::mode, 1.0f);
                if (k == 660) setQ (*q, ParamIDs::mode, 0.0f);
                if (k == 840) setQ (*q, ParamIDs::quality, 1.0f);
                if (k % 20 == 0) pump (5);
                juce::AudioBuffer<float> view (x.getArrayOfWritePointers(), 2, pos, 256);
                q->processBlock (view, midi);
                for (int i = 0; i < 256; ++i) { big = juce::jmax (big, std::abs (x.getSample (0, pos + i) - prev)); prev = x.getSample (0, pos + i); }
            }
            heavy (*q, 1);   // and to FULL OUTPUT
            check (allFinite (x) && big < 0.12f, "while compressing: preset change, 1.2 -> 1.1, ALGO -> IR -> ALGO, LIVE -> HQ: finite, largest step "
                   + juce::String (big, 3));
        }
        // mono input, stereo output
        {
            auto q = std::make_unique<MasterSpaceAudioProcessor>();
            auto lay = q->getBusesLayout(); lay.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            q->setBusesLayout (lay); q->setRateAndBufferSizeDetails (48000.0, 256); q->prepareToPlay (48000.0, 256);
            heavy (*q, 0);
            pump (50);
            auto x = pinkish (48000.0, 2.0, 9); x.clear (1, 0, x.getNumSamples());
            run (*q, x, 256);
            check (allFinite (x) && peak (x) > 0.001f, "mono input: WET ONLY runs clean");
        }
        // an older project (no 1.8 IDs) loads with the compressor OFF
        {
            auto fresh = makeProcessor (48000.0, 256);
            juce::MemoryBlock mb; fresh->getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            for (auto* e : xml->getChildIterator()) if (e->getStringAttribute ("id").startsWith ("cp")) e->setAttribute ("id", "zz");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
            auto q = makeProcessor (48000.0, 256);
            heavy (*q, 1);
            q->setStateInformation (old.getData(), (int) old.getSize());
            pump (50);
            check (q->apvts.getRawParameterValue (ParamIDs::cpOn)->load() < 0.5f && q->apvts.getRawParameterValue (ParamIDs::cpRouting)->load() < 0.5f
                   && std::abs (q->apvts.getRawParameterValue (ParamIDs::cpMidThr)->load() + 24.0f) < 0.01f,
                   "an older project loads with the compressor OFF, WET ONLY, defaults");
        }
        // CPU of the compressor on its own and in the plugin
        {
            auto off = make (0, 0, 0), on = make (0, 0, 0);
            heavy (*on, 0);
            pump (50);
            auto x = pinkish (48000.0, 20.0, 3), y = x;
            double t0 = nowMs(); run (*off, x, 256); const double tOff = nowMs() - t0;
            t0 = nowMs(); run (*on, y, 256); const double tOn = nowMs() - t0;
            msc::MSCompressor c; msc::Settings st; st.on = true; c.prepare (48000.0); c.set (st, true);
            auto z = pinkish (48000.0, 20.0, 4);
            t0 = nowMs();
            for (int pos = 0; pos + 256 <= z.getNumSamples(); pos += 256)
                c.process (z.getReadPointer (0, pos), z.getReadPointer (1, pos), z.getWritePointer (0, pos), z.getWritePointer (1, pos), 256, 0);
            const double tC = nowMs() - t0;
            std::printf ("     20 s at 48 kHz: plugin %.0f ms, with WET ONLY compressor %.0f ms; the compressor alone %.1f ms = %.2f %% of real time\n",
                         tOff, tOn, tC, tC / 200.0);
            check (tC < 0.02 * 20000.0, "compressor cost < 2 % of one core in real time (" + juce::String (tC / 200.0, 2) + " %)");
        }
    }
   #endif

    //==========================================================================
    section ("1.7d DYNAMIC CUT: opt-in attenuation and transparent bypass");
    {
        req::ManualDynamicBank dyn;
        constexpr double fs = 48000.0;
        constexpr int count = 256;
        dyn.prepare (fs);
        float l[count] {}, r[count] {};
        for (int i = 0; i < count; ++i) l[i] = r[i] = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / fs);
        auto original = l[0];
        dyn.process (l, r, count);
        check (l[0] == original && ! dyn.isActive(), "manual dynamic EQ disabled: bit-exact and inactive");

        req::BandParams bands[req::kBands] {};
        bands[0].used = true;
        bands[0].dynamic = true;
        bands[0].freq = 1000.0f;
        bands[0].q = 3.0f;
        bands[0].dynamicThresholdDb = -54.0f;
        bands[0].dynamicMaxDb = 3.0f;
        dyn.set (bands, true, true);
        auto testLevel = [&] (double amplitude, int blocks)
        {
            double inEnergy = 0.0, outEnergy = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < count; ++i)
                    l[i] = r[i] = (float) (amplitude * std::sin (2.0 * 3.141592653589793 * 1000.0 * (b * count + i) / fs));
                dyn.process (l, r, count);
                if (b > blocks - 7)
                    for (int i = 0; i < count; ++i)
                    {
                        const double x = amplitude * std::sin (2.0 * 3.141592653589793 * 1000.0 * (b * count + i) / fs);
                        inEnergy += x * x;
                        outEnergy += (double) l[i] * l[i];
                    }
            }
            return 10.0 * std::log10 ((outEnergy + 1e-30) / (inEnergy + 1e-30));
        };
        const double cutDb = testLevel (0.25, 150);
        check (cutDb < -1.0 && cutDb > -4.0, "approved 1 kHz resonance: dynamic cut is active and <= 3 dB (measured " + juce::String (cutDb, 2) + " dB)");
        const double restoredDb = testLevel (0.00001, 750);
        check (std::abs (restoredDb) < 0.5, "quiet resonance: gain returns to unity after release (" + juce::String (restoredDb, 2) + " dB)");
        bands[0].dynamic = false;
        dyn.set (bands, true);
        const double bypassDb = testLevel (0.25, 200);
        check (std::abs (bypassDb) < 0.0001, "disarmed manual dynamic band bypasses without affecting audio");
    }

    //==========================================================================
    section ("1.7d DYNAMIC CUT in the plugin: cut, clicks, sample rates, blocks, mono, state, NaN, CPU");
    {
        using namespace ParamIDs;
        const double pi = juce::MathConstants<double>::pi;
        auto setP = [] (MasterSpaceAudioProcessor& q, const juce::String& id, float plain)
        { if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        auto getP = [] (MasterSpaceAudioProcessor& q, const juce::String& id) { return q.apvts.getRawParameterValue (id)->load(); };
        // material: pink noise bursts + a sustained 1.2 kHz tone that rings in the reverb; 100 % wet
        auto material = [pi] (double fs, double secs, int seed)
        {
            auto b = pinkish (fs, secs, seed);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double tt = i / fs;
                const float gate = std::fmod (tt, 0.5) < 0.12 ? 0.5f : 0.02f;
                const float tone = (float) (0.18 * std::sin (2.0 * pi * 1200.0 * tt));
                for (int c = 0; c < 2; ++c) b.setSample (c, i, b.getSample (c, i) * gate + tone);
            }
            return b;
        };
        auto setup = [&] (MasterSpaceAudioProcessor& q, int bank, bool dyn, float thr = -48.0f, float mx = 3.0f)
        {
            setP (q, mix, 100.0f); setP (q, decay, 3.0f); setP (q, eqOn, 1.0f);
            setP (q, erOn, bank == req::Late ? 0.0f : 1.0f);      // EARLY / LATE banks: listen to that bus alone
            setP (q, lateOn, bank == req::Early ? 0.0f : 1.0f);
            const int i = 4;
            setP (q, eqId (bank, i, eqUsed), 1); setP (q, eqId (bank, i, eqBypass), 0); setP (q, eqId (bank, i, eqType), (float) req::Bell);
            setP (q, eqId (bank, i, eqFreq), 1200.0f); setP (q, eqId (bank, i, eqGain), 0.0f); setP (q, eqId (bank, i, eqQ), 3.0f);
            setP (q, eqId (bank, i, eqDynThreshold), thr); setP (q, eqId (bank, i, eqDynMax), mx);
            setP (q, eqId (bank, i, eqDynOn), dyn ? 1.0f : 0.0f);
        };
        auto goertzelDb = [pi] (const juce::AudioBuffer<float>& b, double fs, double f, int from)
        {
            double e = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const double w = 2.0 * pi * f / fs, k = 2.0 * std::cos (w);
                double s1 = 0, s2 = 0;
                for (int i = from; i < b.getNumSamples(); ++i) { const double s0 = b.getSample (c, i) + k * s1 - s2; s2 = s1; s1 = s0; }
                e += s1 * s1 + s2 * s2 - k * s1 * s2;
            }
            return 10.0 * std::log10 (e + 1e-30);
        };
        auto bandDb = [] (const juce::AudioBuffer<float>& b, double fs, double f0, double f1, int from)
        {   // energy of a band by a 2nd-order band-pass, both channels
            const double fc = std::sqrt (f0 * f1), q = fc / (f1 - f0), w = juce::MathConstants<double>::twoPi * fc / fs, al = std::sin (w) / (2 * q), a0 = 1 + al;
            const double b0 = al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
            double e = 0;
            for (int c = 0; c < 2; ++c)
            {
                double z[4][2] = {};
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    double y = b.getSample (c, i);
                    for (auto& s : z) { const double x = y; y = b0 * x + s[0]; s[0] = -a1 * y + s[1]; s[1] = -b0 * x - a2 * y; }   // 8th order: the test tone cannot leak in
                    if (i >= from) e += y * y;
                }
            }
            return 10.0 * std::log10 (e + 1e-30);
        };
        auto finite = [] (const juce::AudioBuffer<float>& b)
        { for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false; return true; };
        auto maxD2 = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {   // the largest second difference: a click shows up here long before it is audible elsewhere
            float m = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = juce::jmax (2, from); i < juce::jmin (to, b.getNumSamples()); ++i)
                    m = juce::jmax (m, std::abs (b.getSample (c, i) - 2 * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
            return m;
        };
        auto render = [&] (double fs, int block, int bank, bool dyn, double secs, std::function<void (MasterSpaceAudioProcessor&, int)> perBlock = nullptr, float thr = -48.0f)
        {
            auto q = makeProcessor (fs, block);
            setup (*q, bank, dyn, thr);
            pump (60);
            auto buf = material (fs, secs, 7);
            juce::MidiBuffer midi;
            int k = 0;
            for (int pos = 0; pos < buf.getNumSamples(); pos += block, ++k)
            {
                if (perBlock) perBlock (*q, k);
                const int n = juce::jmin (block, buf.getNumSamples() - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, n);
                q->processBlock (view, midi);
            }
            return buf;
        };

        // 1. the cut: at the resonance, within the max, and nothing else moves
        for (int bank : { (int) req::Wet, (int) req::Early, (int) req::Late })
        {
            const char* bn[] = { "WET", "EARLY", "LATE" };
            const double fs = 48000.0;
            const int from = (int) (fs * 3.0);
            auto off = render (fs, 512, bank, false, 6.0), on = render (fs, 512, bank, true, 6.0);
            if (std::getenv ("DYN_DEBUG"))
            {
                auto off2 = render (fs, 512, bank, false, 6.0);
                auto onHi = render (fs, 512, bank, true, 6.0, nullptr, -6.0f);
                std::printf ("    DEBUG off2-off lo %.2f  onHi-off lo %.2f  tone %.2f\n", bandDb (off2, fs, 200, 400, from) - bandDb (off, fs, 200, 400, from),
                             bandDb (onHi, fs, 200, 400, from) - bandDb (off, fs, 200, 400, from), goertzelDb (onHi, fs, 1200, from) - goertzelDb (off, fs, 1200, from));
            }
            const double cut = goertzelDb (on, fs, 1200.0, from) - goertzelDb (off, fs, 1200.0, from);
            const double lo = bandDb (on, fs, 200, 400, from) - bandDb (off, fs, 200, 400, from);
            const double hi = bandDb (on, fs, 6000, 9000, from) - bandDb (off, fs, 6000, 9000, from);
            check (finite (on) && cut < -0.8 && cut > -3.3, juce::String (bn[bank]) + " bank: the 1.2 kHz resonance is cut by " + juce::String (cut, 2) + " dB (max 3 dB)");
            check (std::abs (lo) < 0.25 && std::abs (hi) < 0.25, juce::String (bn[bank]) + " bank: 200-400 Hz and 6-9 kHz stay put (" + juce::String (lo, 2) + " / " + juce::String (hi, 2) + " dB)");
        }

        // 2. no clicks: switching Dynamic Cut on / off and sweeping the threshold while the reverb rings
        {
            const double fs = 48000.0; const int block = 256, n = (int) (fs * 6.0);
            auto steady = render (fs, block, req::Wet, true, 6.0);
            auto toggled = render (fs, block, req::Wet, true, 6.0, [&] (MasterSpaceAudioProcessor& q, int k)
            { if (k % 90 == 45) setP (q, eqId (req::Wet, 4, eqDynOn), (k / 90) % 2 == 0 ? 0.0f : 1.0f); });
            auto swept = render (fs, block, req::Wet, true, 6.0, [&] (MasterSpaceAudioProcessor& q, int k)
            { setP (q, eqId (req::Wet, 4, eqDynThreshold), -60.0f + 40.0f * (float) (0.5 + 0.5 * std::sin (k * 0.05))); });
            const float base = maxD2 (steady, (int) fs, n);
            check (maxD2 (toggled, (int) fs, n) <= base * 1.2f + 1e-4f, "Dynamic Cut ON / OFF while the tail rings: no click (2nd difference " + juce::String (maxD2 (toggled, (int) fs, n), 4) + " vs " + juce::String (base, 4) + ")");
            check (maxD2 (swept, (int) fs, n) <= base * 1.2f + 1e-4f, "threshold automated across -60..-20 dBFS: no zipper / click");
            check (finite (toggled) && finite (swept), "toggled / swept renders are finite");
        }

        // 3. sample rates and block sizes: the same cut everywhere, no NaN
        {
            double cuts[3] {}; int si = 0;
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                const int from = (int) (fs * 3.0);
                auto off = render (fs, 512, req::Wet, false, 5.0), on = render (fs, 512, req::Wet, true, 5.0);
                cuts[si++] = goertzelDb (on, fs, 1200.0, from) - goertzelDb (off, fs, 1200.0, from);
            }
            check (std::abs (cuts[0] - cuts[1]) < 0.6 && std::abs (cuts[2] - cuts[1]) < 0.6,
                   "44.1 / 48 / 96 kHz: same cut (" + juce::String (cuts[0], 2) + " / " + juce::String (cuts[1], 2) + " / " + juce::String (cuts[2], 2) + " dB)");
            const double fs = 48000.0; const int from = (int) (fs * 3.0);
            auto off = render (fs, 512, req::Wet, false, 5.0);
            double worst = 0;
            for (int block : { 32, 64, 1024, 4096 })
            {
                auto on = render (fs, block, req::Wet, true, 5.0);
                const double c = goertzelDb (on, fs, 1200.0, from) - goertzelDb (off, fs, 1200.0, from);
                worst = juce::jmax (worst, std::abs (c - cuts[1]));
                if (! finite (on)) worst = 99;
            }
            check (worst < 0.4, "block sizes 32 / 64 / 1024 / 4096: same cut (worst deviation " + juce::String (worst, 2) + " dB)");
            // the host changes the rate and the block size while the band is armed
            auto q = makeProcessor (48000.0, 512);
            setup (*q, req::Wet, true);
            auto a = material (48000.0, 1.0, 3); run (*q, a, 512);
            q->releaseResources(); q->setRateAndBufferSizeDetails (96000.0, 128); q->prepareToPlay (96000.0, 128);
            auto b = material (96000.0, 2.0, 4); run (*q, b, 128);
            check (finite (b), "rate / block change 48 kHz 512 -> 96 kHz 128 with Dynamic Cut armed: finite and running");
        }

        // 4. mono in, stereo out
        {
            auto q = std::make_unique<MasterSpaceAudioProcessor>();
            q->setPlayConfigDetails (1, 2, 48000.0, 512);
            q->prepareToPlay (48000.0, 512);
            setup (*q, req::Wet, true);
            auto buf = material (48000.0, 3.0, 5);
            run (*q, buf, 512);
            check (finite (buf), "mono in -> stereo out with Dynamic Cut: finite");
        }

        // 5. bypass: the band bypassed = Dynamic Cut off (after the 10 ms fade)
        {
            auto off = render (48000.0, 512, req::Wet, false, 3.0);
            auto byp = render (48000.0, 512, req::Wet, true, 3.0, [&] (MasterSpaceAudioProcessor& q, int k) { if (k == 0) setP (q, eqId (req::Wet, 4, eqBypass), 1.0f); });
            float m = 0;
            for (int c = 0; c < 2; ++c) for (int i = 4800; i < off.getNumSamples(); ++i) m = juce::jmax (m, std::abs (off.getSample (c, i) - byp.getSample (c, i)));
            check (m < 1e-4f, "band BYPASS with Dynamic Cut armed sounds like Dynamic Cut off (max diff " + juce::String (m, 7) + ")");
        }

        // 6. state / presets: the per-band settings come back
        {
            auto q = makeProcessor (48000.0, 512);
            setup (*q, req::Late, true, -40.0f, 4.5f);
            juce::MemoryBlock mb; q->getStateInformation (mb);
            auto r2 = makeProcessor (48000.0, 512);
            r2->setStateInformation (mb.getData(), (int) mb.getSize());
            pump (60);
            check (getP (*r2, eqId (req::Late, 4, eqDynOn)) > 0.5f && std::abs (getP (*r2, eqId (req::Late, 4, eqDynThreshold)) + 40.0f) < 0.01f
                   && std::abs (getP (*r2, eqId (req::Late, 4, eqDynMax)) - 4.5f) < 0.01f, "Dynamic Cut ON, threshold and max reduction restore from the project state");
            auto fresh = makeProcessor (48000.0, 512);
            bool allOff = true;
            for (int b = 0; b < 3; ++b) for (int i = 0; i < 8; ++i) allOff = allOff && getP (*fresh, eqId (b, i, eqDynOn)) < 0.5f;
            check (allOff, "a new instance: Dynamic Cut off on every band");
        }

        // 7. NaN from upstream: the detector and the cut filters recover
        {
            req::ManualDynamicBank dyn; dyn.prepare (48000.0);
            req::BandParams bands[req::kBands] {};
            bands[0].used = true; bands[0].dynamic = true; bands[0].freq = 1000.0f; bands[0].q = 3.0f;
            bands[0].dynamicThresholdDb = -54.0f; bands[0].dynamicMaxDb = 3.0f;
            dyn.set (bands, true, true);
            float l[256], r[256];
            for (int i = 0; i < 256; ++i) l[i] = r[i] = std::numeric_limits<float>::quiet_NaN();
            dyn.process (l, r, 256);
            bool ok = true;
            for (int k = 0; k < 40; ++k)
            {
                for (int i = 0; i < 256; ++i) l[i] = r[i] = (float) (0.25 * std::sin (2.0 * pi * 1000.0 * (k * 256 + i) / 48000.0));
                dyn.process (l, r, 256);
                if (k > 10) for (int i = 0; i < 256; ++i) ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
            }
            check (ok && std::isfinite (dyn.reductionDb (0)), "a NaN block: Dynamic Cut recovers within ~60 ms (finite output, finite reduction)");
            // silence: no denormals stuck, reduction back to 0
            for (int k = 0; k < 800; ++k) { std::fill (l, l + 256, 0.0f); std::fill (r, r + 256, 0.0f); dyn.process (l, r, 256); }
            check (dyn.reductionDb (0) < 0.01f, "silence: the reduction returns to 0 dB (" + juce::String (dyn.reductionDb (0), 4) + " dB)");
        }

        // 8. CPU: 24 armed bands (8 per bank) against none
        {
            auto time = [&] (bool all)
            {
                auto q = makeProcessor (48000.0, 512);
                setP (*q, mix, 100.0f); setP (*q, eqOn, 1.0f);
                if (all) for (int b = 0; b < 3; ++b) for (int i = 0; i < 8; ++i)
                {
                    setP (*q, eqId (b, i, eqUsed), 1); setP (*q, eqId (b, i, eqType), (float) req::Bell);
                    setP (*q, eqId (b, i, eqFreq), 200.0f * (float) (i + 1)); setP (*q, eqId (b, i, eqQ), 3.0f); setP (*q, eqId (b, i, eqDynOn), 1.0f);
                }
                pump (60);
                auto buf = material (48000.0, 10.0, 9);
                const auto t0 = std::chrono::steady_clock::now();
                run (*q, buf, 512);
                return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            };
            const double t0 = time (false), t1 = time (true);
            std::printf ("  CPU: 10 s of audio, no dynamic bands %.3f s, 24 armed bands %.3f s (+%.1f %% of real time)\n", t0, t1, (t1 - t0) / 10.0 * 100.0);
            check (t1 - t0 < 0.5, "24 armed Dynamic Cut bands cost < 5 % of real time on this machine");
        }
    }
#endif

    //==========================================================================
    section ("1.5 REVERB EQ and analyzer");
    {
        using namespace ParamIDs;
        const double pi = juce::MathConstants<double>::pi;
        auto setP = [] (MasterSpaceAudioProcessor& q, const juce::String& id, float plain)
        {
            if (auto* r = q.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain));
        };
        auto getP = [] (MasterSpaceAudioProcessor& q, const juce::String& id) { return q.apvts.getRawParameterValue (id)->load(); };
        auto band = [&] (MasterSpaceAudioProcessor& q, int bank, int i, int type, float f, float g, float qq, int slope = 1)
        {
            setP (q, eqId (bank, i, eqUsed), 1); setP (q, eqId (bank, i, eqBypass), 0); setP (q, eqId (bank, i, eqType), (float) type);
            setP (q, eqId (bank, i, eqFreq), f); setP (q, eqId (bank, i, eqGain), g); setP (q, eqId (bank, i, eqQ), qq);
            setP (q, eqId (bank, i, eqSlope), (float) slope);
        };
        auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
        {
            float m = 0;
            for (int c = 0; c < 2; ++c) for (int i = from; i < a.getNumSamples(); ++i) m = juce::jmax (m, std::abs (a.getSample (c, i) - b.getSample (c, i)));
            return m;
        };
        auto goertzelDb = [pi] (const float* x, int n, double f, double fs)
        {
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i) { re += x[i] * std::cos (2 * pi * f * i / fs); im -= x[i] * std::sin (2 * pi * f * i / fs); }
            return 10.0 * std::log10 (re * re + im * im + 1e-300);
        };

        // ---- defaults and old projects
        {
            auto q = makeProcessor (48000, 256);
            bool anyUsed = false;
            for (int b = 0; b < 3; ++b) for (int i = 0; i < 8; ++i) anyUsed = anyUsed || getP (*q, eqId (b, i, eqUsed)) > 0.5f;
            check (getP (*q, eqOn) < 0.5f && ! anyUsed && q->getLatencySamples() == 0, "new instance: REVERB EQ off, no band in use, no latency");

            juce::MemoryBlock st;
            q->getStateInformation (st);
            auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
            juce::Array<juce::XmlElement*> drop;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM")) if (isEqParam (e->getStringAttribute ("id"))) drop.add (e);
            for (auto* e : drop) xml->removeChildElement (e, true);
            xml->removeChildElement (xml->getChildByName ("ABCompare"), true);
            juce::MemoryBlock old14;
            juce::AudioProcessor::copyXmlToBinary (*xml, old14);
            setP (*q, eqOn, 1); band (*q, 0, 0, req::Bell, 500, 12, 2); band (*q, 2, 3, req::LowCut, 200, 0, 0.71f, 3);
            pump (200);
            q->setStateInformation (old14.getData(), (int) old14.getSize());
            pump (200);
            anyUsed = false;
            for (int b = 0; b < 3; ++b) for (int i = 0; i < 8; ++i) anyUsed = anyUsed || getP (*q, eqId (b, i, eqUsed)) > 0.5f;
            check (drop.size() == 1 + 3 * req::kBands * ParamIDs::eqNumFields && getP (*q, eqOn) < 0.5f && ! anyUsed, "a 1.4 project (no EQ parameters) loads with REVERB EQ off and no band in use");

            // factory presets carry no EQ: loading one turns the EQ off
            setP (*q, eqOn, 1); band (*q, 0, 0, req::Bell, 500, 12, 2);
            pump (100);
            q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            pump (200);
            check (getP (*q, eqOn) < 0.5f && getP (*q, eqId (0, 0, eqUsed)) < 0.5f, "an older preset loads with REVERB EQ off");
        }

        // ---- the filters themselves: response of every type against the design, landmarks, both rates
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const int n = 1 << 16;
            double worst = 0; bool marks = true;
            struct T { int type; float f, g, q; int slope; };
            for (const T t : { T { req::Bell, 350, -6, 1.2f, 1 }, T { req::Bell, 3000, 12, 4, 1 }, T { req::Bell, 9000, 18, 0.5f, 1 }, T { req::Bell, 60, -18, 18, 1 },
                               T { req::LowShelf, 200, 9, 0.71f, 1 }, T { req::HighShelf, 6000, -12, 0.71f, 1 }, T { req::HighShelf, 12000, 6, 1.5f, 1 },
                               T { req::LowCut, 120, 0, 0.71f, 0 }, T { req::LowCut, 120, 0, 0.71f, 1 }, T { req::LowCut, 120, 0, 0.71f, 2 }, T { req::LowCut, 120, 0, 0.71f, 3 },
                               T { req::HighCut, 5000, 0, 0.71f, 0 }, T { req::HighCut, 5000, 0, 0.71f, 1 }, T { req::HighCut, 5000, 0, 0.71f, 2 }, T { req::HighCut, 5000, 0, 0.71f, 3 } })
            {
                req::BandParams bp[8];
                bp[2].used = true; bp[2].type = t.type; bp[2].freq = t.f; bp[2].gainDb = t.g; bp[2].q = t.q; bp[2].slope = t.slope;
                req::Bank bank; bank.prepare (fs); bank.set (bp, true, true);
                std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
                l[0] = r[0] = 1.0f;
                bank.process (l.data(), r.data(), n);
                for (double f : { 30.0, 60.0, 120.0, 240.0, 350.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0 })
                {
                    const double want = req::bandMagnitudeDb (bp[2], f, fs);
                    if (want > -90.0) worst = juce::jmax (worst, std::abs (goertzelDb (l.data(), n, f, fs) - want));
                }
                const double atF = req::bandMagnitudeDb (bp[2], t.f, fs);
                if (t.type == req::Bell) marks = marks && std::abs (atF - t.g) < 0.01;
                if (t.type == req::LowShelf) marks = marks && std::abs (req::bandMagnitudeDb (bp[2], 20.0, fs) - t.g) < 0.3 && std::abs (req::bandMagnitudeDb (bp[2], 15000.0, fs)) < 0.1;
                if (t.type == req::HighShelf) marks = marks && std::abs (req::bandMagnitudeDb (bp[2], 30.0, fs)) < 0.05;
                if (req::isCut (t.type))
                {
                    const double oct = t.type == req::LowCut ? req::bandMagnitudeDb (bp[2], t.f / 4, fs) - req::bandMagnitudeDb (bp[2], t.f / 8, fs)
                                                             : req::bandMagnitudeDb (bp[2], t.f * 2, fs) - req::bandMagnitudeDb (bp[2], t.f * 1.5, fs) ;
                    marks = marks && std::abs (atF + 3.01) < 0.05;
                    if (t.type == req::LowCut) marks = marks && std::abs (oct - 12.0 * (t.slope + 1)) < 0.6;
                    else marks = marks && oct < 0.0;
                }
            }
            check (worst < 0.02 && marks, juce::String (fs / 1000.0, 1) + " kHz: measured response of every filter type equals the displayed curve (worst "
                                          + juce::String (worst, 4) + " dB); centre gain, -3 dB points and slopes as set");
        }

        // ---- in the plugin: the WET bank is exactly the designed filter on the wet signal (Aux = wet only)
        for (double fs : { 48000.0, 96000.0 })
        {
            auto render = [&] (bool eq)
            {
                auto q = makeProcessor (fs, 256);
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
                setP (*q, routing, 1);
                if (eq) { setP (*q, eqOn, 1); band (*q, 0, 1, req::Bell, 350, -6, 1.2f); band (*q, 0, 4, req::HighShelf, 5000, 4, 0.71f); band (*q, 0, 6, req::LowCut, 90, 0, 0.71f, 1); }
                pump (250);
                auto sig = pinkish (fs, 3.0, 4);
                run (*q, sig, 256);
                return sig;
            };
            auto off = render (false), on = render (true);
            // reference: the same sections applied offline to the EQ-off output (direct form I, double)
            req::BandParams bp[3];
            bp[0].used = true; bp[0].type = req::Bell; bp[0].freq = 350; bp[0].gainDb = -6; bp[0].q = 1.2f;
            bp[1].used = true; bp[1].type = req::HighShelf; bp[1].freq = 5000; bp[1].gainDb = 4; bp[1].q = 0.71f;
            bp[2].used = true; bp[2].type = req::LowCut; bp[2].freq = 90; bp[2].q = 0.71f; bp[2].slope = 1;
            auto ref = off;
            for (const auto& b : bp)
            {
                req::Coefs c[req::kMaxStages];
                const int ns = req::design (b.type, b.freq, b.gainDb, b.q, b.slope, fs, c);
                for (int k = 0; k < ns; ++k)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                        auto* d = ref.getWritePointer (ch);
                        for (int i = 0; i < ref.getNumSamples(); ++i)
                        {
                            const double x = d[i], y = c[k].b0 * x + c[k].b1 * x1 + c[k].b2 * x2 - c[k].a1 * y1 - c[k].a2 * y2;
                            x2 = x1; x1 = x; y2 = y1; y1 = y; d[i] = (float) y;
                        }
                    }
            }
            const float d = maxDiff (on, ref, (int) (0.3 * fs)), pk = peak (off);
            check (d < 2e-4f * pk && maxDiff (on, off) > 0.01f * pk, juce::String (fs / 1000.0, 0) + " kHz: WET EQ in the plugin = the designed filters on the wet signal (difference "
                                         + juce::String (20.0 * std::log10 (d / pk + 1e-12), 1) + " dB re peak)");
        }

        // ---- routing: EARLY bank touches only the early part, LATE bank only the late part
        {
            auto render = [&] (bool erOnV, bool lateOnV, int eqBank)
            {
                auto q = makeProcessor (48000, 256);
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"));
                setP (*q, routing, 1); setP (*q, erOn, erOnV ? 1.0f : 0.0f); setP (*q, lateOn, lateOnV ? 1.0f : 0.0f);
                if (eqBank >= 0) { setP (*q, eqOn, 1); band (*q, eqBank, 0, req::Bell, 1000, 12, 1); band (*q, eqBank, 1, req::LowCut, 300, 0, 0.71f, 2); }
                pump (250);
                auto sig = pinkish (48000, 2.5, 6);
                run (*q, sig, 256);
                return sig;
            };
            const auto lateOnly = render (false, true, -1), earlyOnly = render (true, false, -1);
            const float a = maxDiff (render (false, true, req::Early), lateOnly), b = maxDiff (render (true, false, req::Late), earlyOnly);
            const float c = maxDiff (render (true, false, req::Early), earlyOnly), d = maxDiff (render (false, true, req::Late), lateOnly);
            check (a == 0.0f && b == 0.0f, "EARLY EQ leaves the late reverb (ATMOSPHERIC TAIL) bit-exact, LATE EQ leaves the early reflections bit-exact");
            check (c > 0.01f * peak (earlyOnly) && d > 0.01f * peak (lateOnly), "EARLY EQ shapes the early reflections, LATE EQ shapes the late reverb");

            // room algorithm too
            auto render2 = [&] (int eqBank, bool erOnV, bool lateOnV)
            {
                auto q = makeProcessor (48000, 256);
                q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
                setP (*q, routing, 1); setP (*q, erOn, erOnV ? 1.0f : 0.0f); setP (*q, lateOn, lateOnV ? 1.0f : 0.0f);
                if (eqBank >= 0) { setP (*q, eqOn, 1); band (*q, eqBank, 0, req::HighShelf, 2000, -12, 0.71f); }
                pump (250);
                auto sig = pinkish (48000, 2.0, 6);
                run (*q, sig, 256);
                return sig;
            };
            check (maxDiff (render2 (req::Early, false, true), render2 (-1, false, true)) == 0.0f
                   && maxDiff (render2 (req::Late, true, false), render2 (-1, true, false)) == 0.0f, "the same with the room algorithm (Hall)");
        }

        // ---- the direct sound (Insert) is never equalised; flat bands change nothing audible; no latency
        {
            auto render = [&] (int mode)      // 0 off, 1 heavy EQ on all banks, 2 bands in use but flat
            {
                auto q = makeProcessor (48000, 256);
                setP (*q, routing, 0); setP (*q, mix, 50);
                if (mode == 1) { setP (*q, eqOn, 1); for (int b = 0; b < 3; ++b) { band (*q, b, 0, req::LowCut, 2000, 0, 0.71f, 3); band (*q, b, 1, req::HighCut, 2100, 0, 0.71f, 3); band (*q, b, 2, req::Bell, 2050, -18, 0.3f); band (*q, b, 3, req::Bell, 2050, -18, 0.3f); } }
                if (mode == 2) { setP (*q, eqOn, 1); for (int b = 0; b < 3; ++b) for (int i = 0; i < 8; ++i) band (*q, b, i, i % 3 == 0 ? req::Bell : i % 3 == 1 ? req::LowShelf : req::HighShelf, 100.0f * (i + 1) * (i + 1), 0, 1); }
                pump (250);
                juce::AudioBuffer<float> sig (2, 48000);
                sig.clear();
                sig.setSample (0, 100, 1.0f); sig.setSample (1, 100, 1.0f);
                const int lat = q->getLatencySamples();
                run (*q, sig, 256);
                return std::make_pair (sig, lat);
            };
            const auto off = render (0), heavy = render (1), flat = render (2);
            check (heavy.second == 0 && flat.second == 0, "REVERB EQ adds no latency in LIVE (reported latency 0 samples)");
            check (heavy.first.getSample (0, 100) == off.first.getSample (0, 100) && off.first.getSample (0, 100) != 0.0f,
                   "Insert: the direct sound is identical with the EQ on (first sample " + juce::String (off.first.getSample (0, 100), 4) + ")");
            float tail = 0, tailOff = 0;
            for (int i = 4000; i < 48000; ++i) { tail = juce::jmax (tail, std::abs (heavy.first.getSample (0, i))); tailOff = juce::jmax (tailOff, std::abs (off.first.getSample (0, i))); }
            check (tail < 0.05f * tailOff, "with the wet removed by the EQ only the direct sound is left (tail " + juce::String (20.0 * std::log10 (tail / tailOff + 1e-9), 1) + " dB)");
            check (maxDiff (flat.first, off.first) < 2e-5f * peak (off.first), "24 bands in use at 0 dB: output equals EQ off within float rounding");
        }

        // ---- click-free: switching and automation on a steady low tone (energy above 5 kHz stays at the floor)
        for (double fs : { 48000.0, 96000.0 })
        {
            const int n = (int) (fs * 3.0), blk = 64;
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.5 * std::sin (2 * pi * 110.0 * i / fs));
            req::Bank bank; bank.prepare (fs);
            req::BandParams bp[8];
            juce::Random rng (3);
            int ev = 0;
            for (int pos = 0; pos < n; pos += blk)
            {
                bool on = true;
                if (pos % (blk * 40) == 0)
                {
                    ++ev;
                    auto& b = bp[ev % 4];
                    switch (ev % 7)
                    {
                        case 0: b.used = ! b.used; break;
                        case 1: b.bypass = ! b.bypass; b.used = true; break;
                        case 2: b.type = rng.nextInt (5); b.used = true; break;
                        case 3: b.slope = rng.nextInt (4); b.type = req::LowCut; b.freq = 60; b.used = true; break;
                        case 4: b.gainDb = b.gainDb > 0 ? -18.0f : 18.0f; b.type = req::Bell; b.freq = 110; b.used = true; break;
                        case 5: b.freq = 40.0f + rng.nextFloat() * 400.0f; b.q = 0.1f + rng.nextFloat() * 17.0f; b.used = true; break;
                        default: break;
                    }
                }
                on = (pos / (blk * 280)) % 2 == 0;                       // EQ ON / OFF every ~0.37 s
                // continuous automation of one band between the switches
                bp[5].used = true; bp[5].type = req::Bell; bp[5].freq = 110.0f * (float) std::pow (2.0, std::sin (pos / fs * 9.0));
                bp[5].gainDb = 12.0f * (float) std::sin (pos / fs * 13.0); bp[5].q = 2.0f;
                bank.set (bp, on);
                bank.process (l.data() + pos, r.data() + pos, juce::jmin (blk, n - pos));
            }
            // 4th-order high-pass at 5 kHz
            req::Coefs hp[4];
            const int ns = req::design (req::LowCut, 5000, 0, 0.71, 1, fs, hp);
            double pkHp = 0, pkAll = 0;
            double z[4][2] = {};
            for (int i = 0; i < n; ++i)
            {
                double y = l[(size_t) i];
                pkAll = juce::jmax (pkAll, std::abs (y));
                for (int k = 0; k < ns; ++k) { const double x = y; y = hp[k].b0 * x + z[k][0]; z[k][0] = hp[k].b1 * x - hp[k].a1 * y + z[k][1]; z[k][1] = hp[k].b2 * x - hp[k].a2 * y; }
                if (i > 2000) pkHp = juce::jmax (pkHp, std::abs (y));
            }
            bool finite = true;
            for (float v : l) finite = finite && std::isfinite (v);
            const double clickDb = 20.0 * std::log10 (pkHp / 0.5 + 1e-12);
            check (finite && clickDb < -55.0 && pkAll < 64.0, juce::String (fs / 1000.0, 0) + " kHz: " + juce::String (ev) + " switches (band on/off, bypass, type, slope, jumps) + EQ ON/OFF + automation: content above 5 kHz "
                                               + juce::String (clickDb, 1) + " dB re the tone (no clicks)");
        }

        // ---- state, presets, A/B
        {
            auto q = makeProcessor (48000, 256);
            setP (*q, eqOn, 1); band (*q, 0, 2, req::Bell, 350, -3, 1.2f); band (*q, 1, 0, req::LowCut, 180, 0, 0.9f, 2); band (*q, 2, 7, req::HighShelf, 7000, -4.5f, 0.71f);
            setP (*q, eqId (2, 7, eqBypass), 1);
            pump (100);
            const auto want = q->captureParamMap();
            juce::MemoryBlock st;
            q->getStateInformation (st);
            auto q2 = makeProcessor (48000, 256);
            q2->setStateInformation (st.getData(), (int) st.getSize());
            pump (200);
            bool same = true;
            const auto got = q2->captureParamMap();
            for (const auto& kv : want) if (isEqParam (kv.first)) same = same && std::abs (got.at (kv.first) - kv.second) < 1e-4f * juce::jmax (1.0f, std::abs (kv.second));
            check (same && getP (*q2, eqId (1, 0, eqSlope)) == 2.0f && getP (*q2, eqId (0, 2, eqFreq)) > 349.0f, "project state: all three banks restored (169 EQ parameters)");

            auto& pm = q->getPresetManager();
            const bool saved = pm.saveAs ("EQ Lab Test", "Signature Collection", "", "", false).ok;
            const auto savedKey = pm.getCurrentKey();
            setP (*q, eqOn, 0); setP (*q, eqId (0, 2, eqGain), 9); setP (*q, eqId (1, 0, eqUsed), 0);
            pump (100);
            const int idx = pm.indexOf (savedKey);
            if (idx >= 0) pm.load (idx);
            pump (250);
            check (saved && idx >= 0 && getP (*q, eqOn) > 0.5f && std::abs (getP (*q, eqId (0, 2, eqGain)) + 3.0f) < 1e-3f && getP (*q, eqId (1, 0, eqUsed)) > 0.5f
                   && getP (*q, eqId (2, 7, eqBypass)) > 0.5f, "user preset: EQ saved and restored");

            // A/B: A keeps the EQ, B gets a different one
            q->selectSlot (1);
            pump (250);
            setP (*q, eqId (0, 2, eqGain), 6); setP (*q, eqId (2, 7, eqBypass), 0);
            pump (100);
            q->selectSlot (0);
            pump (250);
            const float gA = getP (*q, eqId (0, 2, eqGain));
            q->selectSlot (1);
            pump (250);
            const float gB = getP (*q, eqId (0, 2, eqGain));
            check (std::abs (gA + 3.0f) < 1e-3f && std::abs (gB - 6.0f) < 1e-3f, "A/B: each slot keeps its own EQ (A " + juce::String (gA, 1) + " dB, B " + juce::String (gB, 1) + " dB)");
        }

        // ---- analyzer taps carry the real signals
        {
            auto q = makeProcessor (48000, 256);
            q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            setP (*q, eqOn, 1); band (*q, 0, 0, req::Bell, 1000, -18, 1.0f);
            pump (250);
            q->analyzer.request[0] = req::Analyzer::WetPre; q->analyzer.request[1] = req::Analyzer::WetPost;
            auto sig = pinkish (48000, 2.0, 8);
            run (*q, sig, 256);
            const int n = 8192;
            std::vector<float> a ((size_t) n), b ((size_t) n);
            q->analyzer.read (0, a.data(), n); q->analyzer.read (1, b.data(), n);
            double ea = 0, eb = 0, la = 0, lb = 0;
            for (double f = 900; f <= 1100; f += 12.5) { ea += std::pow (10.0, goertzelDb (a.data(), n, f, 48000) / 10); eb += std::pow (10.0, goertzelDb (b.data(), n, f, 48000) / 10); }
            for (double f = 100; f <= 200; f += 12.5) { la += std::pow (10.0, goertzelDb (a.data(), n, f, 48000) / 10); lb += std::pow (10.0, goertzelDb (b.data(), n, f, 48000) / 10); }
            const double dMid = 10 * std::log10 (eb / ea), dLow = 10 * std::log10 (lb / la);
            req::BandParams bp; bp.used = true; bp.freq = 1000; bp.gainDb = -18; bp.q = 1.0f;
            const double wantLow = req::bandMagnitudeDb (bp, 150.0, 48000.0);
            check (std::abs (dMid + 17.0) < 2.0 && std::abs (dLow - wantLow) < 0.3, "analyzer WET PRE / POST taps: -18 dB bell at 1 kHz reads " + juce::String (dMid, 1) + " dB around 1 kHz, "
                                                    + juce::String (dLow, 2) + " dB at 150 Hz (curve " + juce::String (wantLow, 2) + " dB)");

            q->analyzer.request[0] = req::Analyzer::Input; q->analyzer.request[1] = -1;
            auto in = pinkish (48000, 0.5, 12);
            const auto copy = in;
            run (*q, in, 256);
            q->analyzer.read (0, a.data(), n);
            float d = 0;
            for (int i = 0; i < n; ++i) d = juce::jmax (d, std::abs (a[(size_t) i] - 0.5f * (copy.getSample (0, copy.getNumSamples() - n + i) + copy.getSample (1, copy.getNumSamples() - n + i))));
            check (d == 0.0f, "analyzer INPUT tap is the plugin input, sample for sample");
        }
    }

    //==========================================================================
    section ("1.5 SPECTRAL ANALYZER 2.0: smoothing, peak detector, PEAK TO EQ");
    {
        using namespace ParamIDs;
        const double pi = juce::MathConstants<double>::pi;
        // runs the editor's analysis on a mono signal exactly as the page does: one FFT frame every 1/30 s
        auto analyse = [] (spec::SpectrumEngine& eng, const std::vector<float>& x, double fs, double from, double to, const std::function<void (double)>& each = {})
        {
            eng.setOrder (fs > 60000.0 ? 14 : 13);
            const int N = eng.size();
            for (double tm = from; tm <= to; tm += 1.0 / 30.0)
            {
                const int end = (int) (tm * fs);
                float* in = eng.input();
                for (int i = 0; i < N; ++i) { const int k = end - N + i; in[i] = k >= 0 && k < (int) x.size() ? x[(size_t) k] : 0.0f; }
                eng.analyse (fs, 1.0 / 30.0, 0.2, true);
                if (each) each (tm);
            }
        };
        auto hasNear = [] (const std::vector<spec::Peak>& m, double f) { for (const auto& p : m) if (std::abs (p.freq / f - 1.0) < 0.015) return true; return false; };
        auto noiseWithPeaks = [&] (double fs, double seconds)
        {
            auto buf = pinkish (fs, seconds, 21);
            req::BandParams bp[8];
            const float fr[3] = { 387.0f, 1200.0f, 4800.0f };
            for (int i = 0; i < 3; ++i) { bp[i].used = true; bp[i].type = req::Bell; bp[i].freq = fr[i]; bp[i].gainDb = 18.0f; bp[i].q = 18.0f; }
            req::Bank bank; bank.prepare (fs); bank.set (bp, true, true);
            bank.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples());
            std::vector<float> m ((size_t) buf.getNumSamples());
            for (int i = 0; i < buf.getNumSamples(); ++i) m[(size_t) i] = 0.25f * (buf.getSample (0, i) + buf.getSample (1, i));
            return m;
        };

       #ifdef MAS_STUDIO_DYN
        {   // 1.7d: a confirmed marker is held >= 5 s after the resonance stops, marked as held, then released
            const double fs = 48000.0;
            auto x = noiseWithPeaks (fs, 4.0);
            auto plain = pinkish (fs, 9.0, 21);
            for (int i = 0; i < plain.getNumSamples(); ++i) x.push_back (0.25f * (plain.getSample (0, i) + plain.getSample (1, i)));
            spec::SpectrumEngine eng;
            bool at25 = false, heldFlag = false, at45 = true, liveBefore = false;
            analyse (eng, x, fs, 0.3, 12.6, [&] (double tm)
            {
                const auto& mk = eng.detector.markers();
                for (const auto& p : mk) if (std::abs (p.freq / 1200.0 - 1.0) < 0.015)
                {
                    if (tm > 3.5 && tm < 3.9 && ! p.held) liveBefore = true;
                    if (tm > 8.4 && tm < 8.6) { at25 = true; heldFlag = heldFlag || p.held; }
                }
                if (tm > 10.6 && tm < 10.8) at45 = hasNear (mk, 1200.0);
            });
            check (liveBefore && at25 && heldFlag, "PEAK DETECT: the 1.2 kHz marker is still there (held) ~4.5 s after the resonance stopped");
            check (! at45, "PEAK DETECT: and is released afterwards (gone ~6.7 s after the resonance stopped)");
        }
        {   // 1.7d: a fast-changing spectrum: six strong resonances, then three weaker new ones while the six are held.
            // The live ones must get places among the six markers; the held ones keep the remaining places.
            const double fs = 48000.0;
            auto withPeaks = [&] (std::initializer_list<float> fr, float gain, double seconds, int seed)
            {
                auto buf = pinkish (fs, seconds, seed);
                req::BandParams bp[8]; int k = 0;
                for (float f : fr) { bp[k].used = true; bp[k].type = req::Bell; bp[k].freq = f; bp[k].gainDb = gain; bp[k].q = 18.0f; ++k; }
                req::Bank bank; bank.prepare (fs); bank.set (bp, true, true);
                bank.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples());
                std::vector<float> m ((size_t) buf.getNumSamples());
                for (int i = 0; i < buf.getNumSamples(); ++i) m[(size_t) i] = 0.25f * (buf.getSample (0, i) + buf.getSample (1, i));
                return m;
            };
            auto x = withPeaks ({ 250.0f, 450.0f, 800.0f, 1500.0f, 3000.0f, 6000.0f }, 18.0f, 3.0, 31);
            const auto b = withPeaks ({ 600.0f, 2200.0f, 4300.0f }, 13.0f, 4.0, 32);
            x.insert (x.end(), b.begin(), b.end());
            spec::SpectrumEngine eng;
            int liveShown = 0, heldShown = 0, total = 0;
            analyse (eng, x, fs, 0.3, 5.8, [&] (double tm)
            {
                if (tm < 5.6) return;
                const auto& mk = eng.detector.markers();
                liveShown = 0; heldShown = 0; total = (int) mk.size();
                for (const auto& p : mk) if (p.held) ++heldShown;
                liveShown = (hasNear (mk, 600.0) ? 1 : 0) + (hasNear (mk, 2200.0) ? 1 : 0) + (hasNear (mk, 4300.0) ? 1 : 0);
            });
            check (liveShown == 3, "PEAK DETECT, fast-changing spectrum: all 3 new (weaker) resonances are shown (" + juce::String (liveShown) + " / 3)");
            check (total <= 6 && heldShown >= 1, "  and the held markers of the previous six still fill the free places (" + juce::String (heldShown) + " held, " + juce::String (total) + " shown)");
        }
       #endif
        for (double fs : { 48000.0, 96000.0 })
        {
            const auto x = noiseWithPeaks (fs, 5.0);
            spec::SpectrumEngine eng;
            analyse (eng, x, fs, 0.3, 5.0);
            const auto mk = eng.detector.markers();
            int extra = 0;
            for (const auto& p : mk) if (std::abs (p.freq / 387.0 - 1) > 0.015 && std::abs (p.freq / 1200.0 - 1) > 0.015 && std::abs (p.freq / 4800.0 - 1) > 0.015) ++extra;
            juce::String found;
            for (const auto& p : mk) found << juce::String (p.freq, 0) << " Hz (+" << juce::String (p.promDb, 1) << " dB, Q " << juce::String (p.q, 0) << ")  ";
            check (hasNear (mk, 387) && hasNear (mk, 1200) && hasNear (mk, 4800) && extra == 0,
                   juce::String (fs / 1000.0, 0) + " kHz, pink noise + narrow peaks at 387 Hz / 1.2 kHz / 4.8 kHz: detector (unsmoothed data) marks all three, false markers " + juce::String (extra) + "   [" + found.trim() + "]");

            // the picture: 1/6 octave is calm, yet the detector above still saw the narrow peaks
            double roughRaw = 0, roughSm = 0; float pkRaw = -200, pkSm = -200;
            const int n = 600; std::vector<float> a ((size_t) n), b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double f0 = 40.0 * std::pow (400.0, (double) i / n), f1 = 40.0 * std::pow (400.0, (double) (i + 1) / n);
                a[(size_t) i] = eng.displayDb (f0, f1, 0.0); b[(size_t) i] = eng.displayDb (f0, f1, 1.0 / 6.0);
                if (std::abs (f0 / 387.0 - 1.0) < 0.03) { pkRaw = juce::jmax (pkRaw, a[(size_t) i]); pkSm = juce::jmax (pkSm, b[(size_t) i]); }
            }
            for (int i = 1; i < n - 1; ++i) { roughRaw += std::abs (a[(size_t) i - 1] - 2 * a[(size_t) i] + a[(size_t) i + 1]); roughSm += std::abs (b[(size_t) i - 1] - 2 * b[(size_t) i] + b[(size_t) i + 1]); }
            check (roughSm < 0.2 * roughRaw && pkSm < pkRaw - 3.0f, juce::String (fs / 1000.0, 0) + " kHz: 1/6-octave picture is smooth (roughness " + juce::String (100.0 * roughSm / roughRaw, 0)
                                                                     + " % of the raw spectrum; the 387 Hz peak reads " + juce::String (pkRaw - pkSm, 1) + " dB lower in the picture, not in the detector)");
        }

        // music: a melody of harmonic notes (overtones everywhere, nothing stays) must not light up markers
        {
            const double fs = 48000.0; const int n = (int) (fs * 8.0);
            std::vector<float> x ((size_t) n);
            const double notes[] = { 220, 247, 262, 294, 330, 294, 262, 247, 196, 220, 262, 330 };
            double ph = 0; juce::Random rng (5);
            for (int i = 0; i < n; ++i)
            {
                const double tm = i / fs, f = notes[(int) (tm / 0.35) % 12] * (1.0 + 0.006 * std::sin (2 * pi * 5.5 * tm));
                ph += f / fs; if (ph >= 1.0) ph -= 1.0;
                double s = 0; for (int h = 1; h <= 12; ++h) s += std::sin (2 * pi * ph * h) / h;
                x[(size_t) i] = (float) (0.12 * s) + 0.01f * (rng.nextFloat() - 0.5f);
            }
            spec::SpectrumEngine eng; int worst = 0;
            analyse (eng, x, fs, 0.3, 8.0, [&] (double) { worst = juce::jmax (worst, (int) eng.detector.markers().size()); });
            check (worst == 0, "a melody with vibrato (12 harmonics per note, 8 s): false markers at any moment: " + juce::String (worst));

            // a note held for 6 s is a steady harmonic series: it has to hold 3 s before it may show
            for (int i = 0; i < n; ++i) { double s = 0; for (int h = 1; h <= 8; ++h) s += std::sin (2 * pi * 220.0 * h * i / fs) / h; x[(size_t) i] = (float) (0.12 * s) + 0.01f * (rng.nextFloat() - 0.5f); }
            spec::SpectrumEngine e2; int at2 = -1, atEnd = 0;
            analyse (e2, x, fs, 0.3, 6.0, [&] (double tm) { if (tm < 2.5) at2 = juce::jmax (at2, (int) e2.detector.markers().size()); atEnd = (int) e2.detector.markers().size(); });
            check (at2 == 0, "a held note (harmonic series): no markers during the first 2.5 s (after 6 s: " + juce::String (atEnd) + ", by design: a tone that never moves is worth a look)");
        }

        // a long reverb tail with a resonance: the marker has to hold while the tail decays
        {
            auto q = makeProcessor (48000, 256);
            q->getPresetManager().load (q->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
            auto setP = [&] (const juce::String& id, float plain) { if (auto* r = q->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
            setP (decay, 6.0f); setP (routing, 1); setP (eqOn, 1);
            setP (eqId (2, 4, eqUsed), 1); setP (eqId (2, 4, eqFreq), 387); setP (eqId (2, 4, eqGain), 15); setP (eqId (2, 4, eqQ), 18);   // stands in for a resonant room
            pump (250);
            q->analyzer.request[0] = req::Analyzer::LatePost; q->analyzer.request[1] = -1;
            spec::SpectrumEngine eng; eng.setOrder (13);
            int seenTail = 0, framesTail = 0, extraMax = 0;
            for (int f = 0; f < 150; ++f)                       // 5 s at 30 frames per second
            {
                auto sig = pinkish (48000, 1600.0 / 48000.0, 200 + f);
                if (f >= 30) sig.clear();                       // 1 s of noise, then only the tail
                run (*q, sig, 256);
                q->analyzer.read (0, eng.input(), eng.size());
                eng.analyse (48000.0, 1.0 / 30.0, 0.2, true);
                if (f >= 75)
                {
                    ++framesTail; const auto& mk = eng.detector.markers();
                    if (hasNear (mk, 387)) ++seenTail;
                    extraMax = juce::jmax (extraMax, (int) mk.size() - (hasNear (mk, 387) ? 1 : 0));
                }
            }
            check (seenTail >= framesTail * 9 / 10 && extraMax <= 1, "reverb tail (decay 6 s, 2.5-5 s after the sound stops): the 387 Hz resonance stays marked in "
                                     + juce::String (seenTail) + " of " + juce::String (framesTail) + " frames, other markers at most " + juce::String (extraMax));
        }

        // PEAK TO EQ: one click prepares a band; nothing is cut
        {
            auto q = makeProcessor (48000, 256), ref = makeProcessor (48000, 256);
            auto getP = [&] (const juce::String& id) { return q->apvts.getRawParameterValue (id)->load(); };
            auto render = [&] (MasterSpaceAudioProcessor& pr) { auto sig = pinkish (48000, 1.0, 3); run (pr, sig, 256); return sig; };
            for (auto* pr : { q.get(), ref.get() }) if (auto* r = pr->apvts.getParameter (eqOn)) r->setValueNotifyingHost (1.0f);
            pump (200);
            render (*q); render (*ref);
            eqlab::Model m (*q); m.refresh(); m.bank = 2;                         // target: LATE
            spec::Peak pk; pk.freq = 387.0f; pk.q = 31.0f; pk.bwHz = 12.5f;
            const int k = m.freeBandForPeak();
            m.assignPeak (k, pk.freq, spec::suggestQ (pk));
            pump (200);
            const auto after = render (*q), before = render (*ref);
            float d = 0; for (int i = 0; i < after.getNumSamples(); ++i) d = juce::jmax (d, std::abs (after.getSample (0, i) - before.getSample (0, i)));
            check (k == 4 && getP (eqId (2, 4, eqUsed)) > 0.5f && std::abs (getP (eqId (2, 4, eqFreq)) - 387.0f) < 0.5f && getP (eqId (2, 4, eqGain)) == 0.0f
                   && std::abs (getP (eqId (2, 4, eqQ)) - 12.0f) < 0.01f && juce::roundToInt (getP (eqId (2, 4, eqType))) == req::Bell && m.sel == 4
                   && getP (eqId (0, 4, eqUsed)) < 0.5f && getP (eqId (1, 4, eqUsed)) < 0.5f,
                   "PEAK TO EQ on a 387 Hz marker: free band 5 of the LATE bank becomes Bell, 387 Hz, Q 12 (measured 31, limited), gain 0 dB, selected; WET and EARLY untouched");
            check (d < 2e-5f * peak (before), "PEAK TO EQ does not change the sound by itself (difference " + juce::String (20.0 * std::log10 (d / peak (before) + 1e-12), 1) + " dB re peak)");
            m.undoPeak(); pump (100);
            const bool undone = getP (eqId (2, 4, eqUsed)) < 0.5f;
            int order[7]; for (int i = 0; i < 7; ++i) { order[i] = m.freeBandForPeak(); if (order[i] >= 0) m.assignPeak (order[i], 500.0f + 100.0f * i, 4.0f); }
            check (undone && order[0] == 4 && order[1] == 5 && order[2] == 6 && order[3] == 7 && order[4] == 1 && order[5] == 2 && order[6] == -1,
                   "undo restores the band; free bands are taken in the order 5, 6, 7, 8, LOW MID, HIGH MID; with none left nothing is replaced (the user is asked)");
        }
    }
   #endif

    //==========================================================================
    section ("Instances, automation, stability");
    {
        auto a = makeProcessor (48000, 128), b = makeProcessor (48000, 128);
        a->getPresetManager().load (a->getPresetManager().indexOf ("factory:Halls/Concert Hall"));
        b->getPresetManager().load (b->getPresetManager().indexOf ("factory:Ambiences/Ambience Wood"));
        check (std::abs (a->captureParamMap()[ParamIDs::decay] - b->captureParamMap()[ParamIDs::decay]) > 1.0f, "two instances keep independent states");

        // automation of every parameter, random values, every 64 samples, both engines
        juce::Random rng (42);
        auto sig = pinkish (48000, quick ? 3.0 : 8.0, 5);
        const auto& prms = a->getParameters();
        juce::MidiBuffer midi;
        for (int pos = 0; pos < sig.getNumSamples(); pos += 64)
        {
            auto* prm = prms[rng.nextInt (prms.size())];
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (r->getParameterID() != ParamIDs::mode)
                    r->setValueNotifyingHost (rng.nextFloat());
            juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, juce::jmin (64, sig.getNumSamples() - pos));
            a->processBlock (view, midi);
        }
        check (allFinite (sig) && peak (sig) < 64.0f, "random automation of all parameters: finite output (peak " + juce::String (peak (sig), 2) + ")");
    }

    //==========================================================================
    section ("CPU (one instance, this machine)");
    for (double fs : { 48000.0, 96000.0 })
    {
        auto p = makeProcessor (fs, 256);
        auto& pm = p->getPresetManager();
       #ifdef MAS_ATMOS
        for (const char* key : { "factory:Studio Rooms/Studio A Natural", "factory:Halls/Cathedral Stone",
                                 "factory:Vocal Spaces/LeadVox Studio Room (1.1 Legacy)", "factory:Vocal Spaces/ATMOS Vocal Glow",
                                 "factory:Creative Spaces/ATMOS Endless 30 s" })
       #else
        for (const char* key : { "factory:Studio Rooms/Studio A Natural", "factory:Halls/Cathedral Stone",
                                 "factory:Vocal Spaces/LeadVox Studio Room (1.1 Legacy)" })
       #endif
        {
            pm.load (pm.indexOf (key));
            auto sig = pinkish (fs, 1.0);
            run (*p, sig, 256);
            sig = pinkish (fs, quick ? 2.0 : 5.0);
            const double t0 = nowMs();
            run (*p, sig, 256);
            const double ms = nowMs() - t0;
            const double secs = sig.getNumSamples() / fs;
            std::printf ("  %5.0f kHz  %-40s %6.2f %% of one core (%.0fx real time)\n", fs / 1000.0,
                         juce::String (key).fromFirstOccurrenceOf ("/", false, false).toRawUTF8(), 100.0 * ms / (secs * 1000.0), secs * 1000.0 / ms);
        }
       #ifdef MAS_RI
        {
            pm.load (pm.indexOf ("factory:Vocal Spaces/ATMOS Vocal Glow"));
            auto setP = [&] (const char* id, float plain) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
            struct Cfg { const char* name; int duck, rg, q, hc = 0; };
            for (const Cfg c : { Cfg { "ATMOS, modules off", 0, 0, 0 }, Cfg { "ATMOS + SMART DUCKER (Vocal Focus)", 1, 0, 0 },
                                 Cfg { "ATMOS + HARMONIC COLOR SOFT LIVE", 0, 0, 0, 1 }, Cfg { "ATMOS + HARMONIC COLOR TUBE LIVE", 0, 0, 0, 2 },
                                 Cfg { "ATMOS + HARMONIC COLOR TAPE LIVE", 0, 0, 0, 3 }, Cfg { "ATMOS + HARMONIC COLOR TAPE STUDIO HQ", 0, 0, 1, 3 },
                                 Cfg { "ATMOS + all three modules STUDIO HQ", 1, 1, 1, 2 },
                                 Cfg { "ATMOS + SMART DUCKER (Spectral)", 2, 0, 0 }, Cfg { "ATMOS + RESONANCE GUARD LIVE", 0, 1, 0 },
                                 Cfg { "ATMOS + both LIVE", 1, 1, 0 }, Cfg { "ATMOS + RESONANCE GUARD STUDIO HQ", 0, 1, 1 },
                                 Cfg { "ATMOS + both STUDIO HQ", 1, 1, 1 } })
            {
                setP (ParamIDs::duckOn, c.duck > 0 ? 1.0f : 0.0f); setP (ParamIDs::duckMode, c.duck == 2 ? 2.0f : 1.0f);
                setP (ParamIDs::rgOn, (float) c.rg); setP (ParamIDs::quality, (float) c.q); setP (ParamIDs::rgTarget, 0.0f);
                setP (ParamIDs::hcOn, c.hc > 0 ? 1.0f : 0.0f); setP (ParamIDs::hcChar, (float) juce::jmax (0, c.hc - 1)); setP (ParamIDs::hcDrive, 60.0f);
                pump (250);
                auto warm = pinkish (fs, 1.0);
                run (*p, warm, 256);
                auto sg = pinkish (fs, quick ? 2.0 : 5.0);
                const double t0 = nowMs();
                run (*p, sg, 256);
                const double ms = nowMs() - t0;
                std::printf ("  %5.0f kHz  %-40s %6.2f %% of one core\n", fs / 1000.0, c.name, 100.0 * ms / (sg.getNumSamples() / fs * 1000.0));
            }
            setP (ParamIDs::duckOn, 0); setP (ParamIDs::rgOn, 0); setP (ParamIDs::quality, 0); setP (ParamIDs::hcOn, 0);
            pump (250);
           #ifdef MAS_EQ
            for (int mode = 0; mode < 4; ++mode)      // EQ off / 5 WET bands / 8 bands in every bank / the same + analyzer
            {
                auto sp = [&] (const juce::String& id, float plain) { if (auto* r = p->apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
                sp (ParamIDs::eqOn, mode > 0 ? 1.0f : 0.0f);
                for (int b = 0; b < 3; ++b)
                    for (int i = 0; i < 8; ++i)
                    {
                        sp (ParamIDs::eqId (b, i, ParamIDs::eqUsed), (mode >= 2 || (mode == 1 && b == 0 && i < 5)) ? 1.0f : 0.0f);
                        sp (ParamIDs::eqId (b, i, ParamIDs::eqGain), 3.0f - i); sp (ParamIDs::eqId (b, i, ParamIDs::eqFreq), 60.0f * (float) std::pow (2.1, i));
                    }
                p->analyzer.request[0] = mode == 3 ? (int) req::Analyzer::WetPre : -1; p->analyzer.request[1] = mode == 3 ? (int) req::Analyzer::WetPost : -1;
                pump (250);
                auto warm = pinkish (fs, 1.0);
                run (*p, warm, 256);
                auto sg = pinkish (fs, quick ? 2.0 : 5.0);
                const double t0 = nowMs();
                run (*p, sg, 256);
                const double ms = nowMs() - t0;
                const char* nm[] = { "ATMOS, REVERB EQ off", "ATMOS + REVERB EQ, 5 WET bands", "ATMOS + REVERB EQ, 3 x 8 bands", "ATMOS + REVERB EQ 3 x 8 + analyzer taps" };
                std::printf ("  %5.0f kHz  %-40s %6.2f %% of one core\n", fs / 1000.0, nm[mode], 100.0 * ms / (sg.getNumSamples() / fs * 1000.0));
            }
            { auto* r = p->apvts.getParameter (ParamIDs::eqOn); r->setValueNotifyingHost (0.0f); p->analyzer.request[0] = -1; p->analyzer.request[1] = -1; pump (100); }
           #endif
        }
       #endif
        // during a transition both paths run
        auto sig = pinkish (fs, 2.0);
        const double t0 = nowMs();
        for (int k = 0; k < 20; ++k)
        {
            juce::AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, k * (int) (0.1 * fs), (int) (0.1 * fs));
            pm.load (pm.indexOf (k % 2 ? "factory:Halls/Cathedral Stone" : "factory:Halls/Concert Hall"));
            run (*p, view, 256);
        }
        const double ms = nowMs() - t0;
        std::printf ("  %5.0f kHz  %-40s %6.2f %% of one core\n", fs / 1000.0, "preset change every 100 ms (worst case)", 100.0 * ms / 2000.0);
    }

    root.deleteRecursively();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
