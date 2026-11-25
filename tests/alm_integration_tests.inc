// Compiled with the real processor and the independent gated BS.1770 meter.
static int testInsertMatch()
{
    int failures = 0;
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
        std::printf ("INSERT %.0f Hz block %d gain %.3f dB error %+.3f LU latency %d samples\\n",
                     fs, block, gain, delta, proc->getLatencySamples());
        failures += !(std::isfinite (delta) && std::abs (delta) <= 0.5 && gain > 1);
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
    return failures ? 1 : 0;
}
