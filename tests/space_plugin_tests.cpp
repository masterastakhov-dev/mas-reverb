// Headless tests of the MASTER SPACE plugin processor (state, presets, A/B, transitions, CPU).
// Built with -DMAS_BUILD_TESTS=ON; run: MasterSpaceTests [--quick]
#include "../Source/MasterSpace/PluginProcessor.h"
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
    shot ("1_main");
    for (const char* tab : { "ADVANCED", "PRESETS" })
        if (auto* b = findButton (*ed, tab)) { b->triggerClick(); pump (200); shot (juce::String ("2_") + tab); }
    if (auto* b = findButton (*ed, "B")) { b->triggerClick(); pump (200); }
    if (auto* b = findButton (*ed, "SAVE AS")) { b->triggerClick(); pump (200); shot ("3_save_as"); }
    ed.reset();
    return 0;
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && juce::String (argv[1]) == "--quick";
    juce::ScopedJuceInitialiser_GUI juceInit;
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
