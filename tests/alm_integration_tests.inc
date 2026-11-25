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
        buffer.clear();
        run (proc, buffer, block);
        if (proc.getInsertStatus() >= 3) return true;
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return false;
}

static int testInsertMatch()
{
    int failures = 0;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ms-match-tests", "", false);
    setenv ("MASTER_SPACE_PRESET_ROOT", root.getFullPathName().toRawUTF8(), 1);
    for (double fs : {44100., 48000., 96000.})
    {
        const int block = 32;
        auto proc = std::make_unique<MasterSpaceAudioProcessor>();
        auto* enable = proc->apvts.getParameter ("alm");
        if (!enable || enable->getValue() < 0.5f) return 1;
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
        enable->setValueNotifyingHost (0);
        run (*proc, output, block);
        failures += proc->insertGainDb.load() != 0;
        enable->setValueNotifyingHost (1);
        proc->apvts.getParameter (ParamIDs::routing)->setValueNotifyingHost (1);
        run (*proc, output, block);
        failures += proc->insertGainDb.load() != 0;
        auto tree = proc->apvts.copyState();
        tree.removeChild (tree.getChildWithProperty ("id", "alm"), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldState);
        proc->setStateInformation (oldState.getData(), (int)oldState.getSize());
        failures += enable->getValue() != 0;
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
    root.deleteRecursively();
    return failures ? 1 : 0;
}
