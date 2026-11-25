// Compiled with the real processor and the independent gated BS.1770 meter.
static int printDefaultInsertCalibration()
{
    auto processor = std::make_unique<MasterSpaceAudioProcessor> (false);
    auto state = processor->defaultParamMap();
    for (double fs : {44100., 48000., 96000.})
        for (bool mono : { false, true })
        {
            std::array<double, 3> ratio {};
            constexpr float positions[] = {0, 100, 50};
            for (unsigned i = 0; i < ratio.size(); ++i)
            {
                state[ParamIDs::mix] = positions[i];
                double input = 0;
                auto output = MasterSpaceAudioProcessor::measureLoudness (state, fs, &input, nullptr, mono);
                ratio[i] = std::pow (10.0, (output - input) * 0.1);
            }
            std::printf ("%.0f %d %.12g %.12g %.12g\n", fs, int (mono), ratio[0], ratio[1], ratio[2] - 0.5 * (ratio[0] + ratio[1]));
        }
    return 0;
}

static bool settleInsert (MasterSpaceAudioProcessor& proc, double fs, int block)
{
    auto buffer = silence (fs, 0.08);
    const double deadline = juce::Time::getMillisecondCounterHiRes() + 60000;
    do
    {
        pump (30);
        if (proc.hasInsertReference())
        {
            buffer.clear();
            run (proc, buffer, block);
            // Route switching may consume the first block's reference.
            run (proc, buffer, block);
            return proc.getInsertStatus() >= 3;
        }
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return false;
}

static int testInsertMatch()
{
    int failures = 0;
    const auto root = testTempDirectory().getNonexistentChildFile ("ms-match-tests", "", false);
    setenv ("MASTER_SPACE_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    for (double fs : {44100., 48000., 96000.})
    {
        const int block = 32;
        auto proc = std::make_unique<MasterSpaceAudioProcessor>();
        if (proc->apvts.getParameter ("alm") != nullptr || proc->captureCompareSlot()["alm"] != 1.0f
            || proc->captureParamMap()[ParamIDs::mix] != 100.0f) return 1;
        proc->setRateAndBufferSizeDetails (fs, block);
        proc->prepareToPlay (fs, block);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        do
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            juce::AudioBuffer<float> silence (2, block);
            silence.clear();
            run (*proc, silence, block);
        }
        while (proc->insertGainDb.load() < 0.005f && juce::Time::getMillisecondCounterHiRes() - start < 20000);
        auto input = lvl::material (0, fs, 8.);
        auto output = input;
        run (*proc, output, block);
        const auto delta = lvl::lufs (output, fs, int (fs * 3)) - lvl::lufs (input, fs, int (fs * 3));
        const auto gain = proc->insertGainDb.load();
        std::printf ("INSERT %.0f Hz block %d gain %.3f dB error %+.3f LU latency %d samples\n",
                     fs, block, gain, delta, proc->getLatencySamples());
        failures += !(std::isfinite (delta) && std::abs (delta) <= 0.5 && gain > 1);
        // MIX changes use the existing covariance immediately, even without a message loop.
        const auto mixParam = proc->apvts.getParameter (ParamIDs::mix);
        for (float mixValue : {0.0f, 50.0f, 100.0f})
        {
            mixParam->setValueNotifyingHost (mixParam->convertTo0to1 (mixValue));
            auto rendered = input;
            run (*proc, rendered, block);
            const auto error = lvl::lufs (rendered, fs, int (fs * 3)) - lvl::lufs (input, fs, int (fs * 3));
            std::printf ("MIX %.0f Hz %.0f%% error %+.3f LU gain %.3f dB\n", fs, mixValue, error, proc->insertGainDb.load());
            failures += !std::isfinite (error) || std::abs (error) > 0.5;
        }
        auto recalledState = proc->captureParamMap();
        recalledState["alm"] = 0;
        proc->applyParamMap (recalledState);
        run (*proc, output, block);
        failures += proc->insertGainDb.load() != 0;
        recalledState["alm"] = 1;
        proc->applyParamMap (recalledState);
        proc->apvts.getParameter (ParamIDs::routing)->setValueNotifyingHost (1);
        run (*proc, output, block);
        failures += proc->insertGainDb.load() != 0;
        auto tree = proc->apvts.copyState();
        tree.removeProperty ("automaticReference", nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldState);
        proc->setStateInformation (oldState.getData(), (int)oldState.getSize());
        failures += proc->captureCompareSlot()["alm"] != 0;
    }
    // Factory LeadVox keeps its authored -3.6 dB OUTPUT and 100% wet balance.
    auto voice = makeProcessor (96000, 64);
    auto& library = voice->getPresetManager();
    const auto& presets = library.getPresets();
    const auto lead = std::find_if (presets.begin(), presets.end(), [] (const auto& p) {
        return p.name == "LeadVox Studio Room 1.2 Open";
    });
    if (lead == presets.end() || !library.load (int (lead - presets.begin())).ok) return 1;
    if (!settleInsert (*voice, 96000, 64)) return 1;
    const auto trim = voice->apvts.getRawParameterValue (ParamIDs::output)->load();
    const auto mix = voice->apvts.getRawParameterValue (ParamIDs::mix)->load();
    failures += std::abs (trim + 3.6f) > .01f || mix != 100;
    for (int kind = 0; kind < 4; ++kind)
    {
        auto input = lvl::material (kind, 96000, 10.0);
        auto output = input;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        run (*voice, output, 64);
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - start) * 0.01;
        const auto error = lvl::lufs (output, 96000, 384000) - lvl::lufs (input, 96000, 384000) - trim;
        std::printf ("LeadVox 96 kHz/64 material %d trim %.1f gain %.3f error %+.3f LU peak %.2f dBFS CPU %.1f%%\n",
                     kind, trim, voice->insertGainDb.load(), error, juce::Decibels::gainToDecibels (lvl::peak (output, 384000)), cpu);
        failures += !std::isfinite (error) || std::abs (error) > (kind == 0 ? 0.5 : 1.5);
    }
    juce::MemoryBlock saved;
    voice->getStateInformation (saved);
    auto recalled = std::make_unique<MasterSpaceAudioProcessor>();
    recalled->setStateInformation (saved.getData(), int (saved.getSize()));
    recalled->prepareToPlay (96000, 64);
    auto callbackOnly = silence (96000, 0.12);
    run (*recalled, callbackOnly, 64);
    failures += std::abs (voice->insertGainDb.load() - recalled->insertGainDb.load()) > .001f;
    root.deleteRecursively();
    return failures ? 1 : 0;
}

// Independent reference material and full processor renders for factory states.
static int surveyFactoryInsert (double fs)
{
    auto probe = std::make_unique<MasterSpaceAudioProcessor> (false);
    const auto root = testTempDirectory().getNonexistentChildFile ("ms-factory-matches", "", false);
    setenv ("MASTER_SPACE_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    int count = 0, invalid = 0;
    for (const auto& preset : probe->getPresetManager().getPresets())
    {
        if (preset.source != mspreset::Source::factory) continue;
        auto proc = std::make_unique<MasterSpaceAudioProcessor>();
        auto state = preset.values;
        state[ParamIDs::routing] = state[ParamIDs::output] = 0;
        state[ParamIDs::depth] = 35;
        state[ParamIDs::duckOn] = state[ParamIDs::rgOn] = state[ParamIDs::cpOn] = 0;
        for (int b = 0; b < req::numBanks; ++b)
            for (int i = 0; i < req::kBands; ++i) state[ParamIDs::eqId (b, i, ParamIDs::eqDynOn)] = 0;
        state["alm"] = 1;
        proc->applyParamMap (state);
        proc->setRateAndBufferSizeDetails (fs, 128);
        proc->prepareToPlay (fs, 128);
        if (!settleInsert (*proc, fs, 128)) { ++invalid; continue; }
        const bool limited = proc->getInsertStatus() == 3;
        const auto warmup = std::max (4., double (state[ParamIDs::decay]));
        std::printf ("FACTORY %.0f %s gain %.3f%s", fs, preset.name.toRawUTF8(), proc->insertGainDb.load(), limited ? " limited" : "");
        for (int kind = 0; kind < 4; ++kind)
        {
            proc->reset();
            auto input = lvl::material (kind, fs, warmup + 6.);
            auto out = input;
            run (*proc, out, 128);
            const int skip = int (warmup * fs);
            const auto delta = lvl::lufs (out, fs, skip) - lvl::lufs (input, fs, skip);
            std::printf (" | %+.3f LU %.2f dBFS", delta, juce::Decibels::gainToDecibels (lvl::peak (out, skip)));
            if (!std::isfinite (delta) || !allFinite (out) || (!limited && kind == 0 && std::abs (delta) > 0.5)) ++invalid;
        }
        ++count;
        std::puts ("");
        std::fflush (stdout);
    }
    std::printf ("%d factory presets, %d failed reference checks\n", count, invalid);
    root.deleteRecursively();
    return count == 36 && invalid == 0 ? 0 : 1;
}

static int testRecordedMaterial (const juce::File& assets)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    int errors = 0;
    for (auto file : {"singing.ogg", "cello.wav"})
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (assets.getChildFile (file)));
        if (!reader || reader->lengthInSamples <= 0) return 1;
        juce::AudioBuffer<float> clip (2, (int)reader->lengthInSamples);
        reader->read (&clip, 0, clip.getNumSamples(), 0, true, true);
        const double fs = reader->sampleRate;
        for (int routing : {0, 1})
        {
            auto matched = makeProcessor (fs, 64), baseline = makeProcessor (fs, 64);
            for (auto* p : {matched.get(), baseline.get()})
            {
                auto state = p->defaultParamMap();
                state[ParamIDs::cpOn] = state[ParamIDs::hcOn] = 1;
                state[ParamIDs::cpRouting] = float (routing);
                state[ParamIDs::cpMidThr] = state[ParamIDs::cpSideThr] = -42;
                state[ParamIDs::cpMidRatio] = state[ParamIDs::cpSideRatio] = 4;
                state["alm"] = float (p == matched.get());
                p->applyParamMap (state);
                p->prepareToPlay (fs, 64);
            }
            if (!settleInsert (*matched, fs, 64)) return 1;
            matched->reset();
            baseline->reset();
            auto input = silence (fs, 8);
            for (int i = 0; i < int (fs * 4); ++i)
                for (int c = 0; c < 2; ++c)
                    input.setSample (c, i, clip.getSample (c, i % clip.getNumSamples()));
            auto output = input, original = input;
            run (*matched, output, 64);
            run (*baseline, original, 64);
            const auto g = juce::Decibels::decibelsToGain (matched->insertGainDb.load());
            double err = 0, signal = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = int (fs * .5); i < output.getNumSamples(); ++i)
                {
                    const double x = original.getSample (c, i);
                    const double diff = x - output.getSample (c, i) / g;
                    err += diff * diff; signal += x * x;
                }
            const double residual = 10 * std::log10 (std::max (1e-30, err / std::max (signal, 1e-30)));
            std::printf ("RECORDING %s %s gain %.3f dB normalized residual %.1f dB\n",
                         file, routing ? "FULL" : "WET", matched->insertGainDb.load(), residual);
            errors += !(signal > 0 && residual < -90);
        }
    }
    return errors ? 1 : 0;
}
