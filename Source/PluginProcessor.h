#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MASReverbEngine.h"

namespace ParamIDs
{
    inline constexpr const char* preDelay = "predelay";
    inline constexpr const char* size     = "size";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* hfDamp   = "hfdamp";
    inline constexpr const char* lowCut   = "lowcut";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* mix      = "mix";
}

class MASReverbAudioProcessor : public juce::AudioProcessor
{
public:
    MASReverbAudioProcessor();
    ~MASReverbAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    mas::ReverbParams readParams() const;

    mas::ReverbEngine engine;
    juce::AudioBuffer<float> monoScratch;

    std::atomic<float>* pPre   = nullptr;
    std::atomic<float>* pSize  = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pDamp  = nullptr;
    std::atomic<float>* pLow   = nullptr;
    std::atomic<float>* pMod   = nullptr;
    std::atomic<float>* pMix   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MASReverbAudioProcessor)
};
