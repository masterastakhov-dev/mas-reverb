#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "MasterVerbEngine.h"

namespace ParamIDs
{
    inline constexpr const char* preDelay = "predelay";
    inline constexpr const char* size     = "size";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* hfDamp   = "hfdamp";
    inline constexpr const char* lowCut   = "lowcut";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* mode     = "mode";      // 0 = algorithmic, 1 = impulse response
    inline constexpr const char* irLength = "irlength";  // % of the loaded IR
    inline constexpr const char* irCut    = "ircut";     // high cut on the IR wet signal
}

class MasterVerbAudioProcessor : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    MasterVerbAudioProcessor();
    ~MasterVerbAudioProcessor() override;

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

    //==========================================================================
    // Impulse responses (call from the message thread)
    bool loadImpulseResponse (const juce::File& file);
    juce::String getIRDisplayName() const { return irDisplayName; }
    bool hasImpulseResponse() const { return irOriginal.getNumSamples() > 0; }
    juce::File getLastIRFolder() const { return lastIRFolder; }
    static bool isSupportedIRFile (const juce::String& path);

    juce::AudioProcessorValueTreeState apvts;

private:
    struct PendingIR
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        bool stereo = false;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    mas::ReverbParams readParams() const;

    void timerCallback() override;
    void sendShapedIRToAudioThread();
    void takePendingIR() noexcept;
    void processStereo (float* left, float* right, int numSamples);
    void processIRPath (int numSamples);
    void resetIRPath();

    // ---- algorithmic path
    mas::ReverbEngine engine;

    // ---- impulse-response path
    juce::dsp::Convolution convolver { juce::dsp::Convolution::NonUniform { 512 } };
    mas::DelayLine irPreDelay[2];
    mas::HighPass  irLowCut[2];
    mas::LowPass   irHighCut[2];
    juce::AudioBuffer<float> irDry, irWet, monoScratch;
    float irCurPre = 0.0f, irCurMix = 0.3f;
    double irCurLowCut = 90.0, irCurHighCut = 20000.0;
    std::atomic<bool> irActiveOnAudio { false };

    // handoff message thread -> audio thread (wait-free on the audio side)
    juce::SpinLock irLock;
    std::unique_ptr<PendingIR> pendingIR, retiredIR;

    // message-thread IR data
    juce::AudioBuffer<float> irOriginal;
    double irOriginalRate = 44100.0;
    juce::String irDisplayName;
    juce::File lastIRFolder;
    float appliedIRLength = -1.0f, lastSeenIRLength = -1.0f;
    juce::String requestedIRPath;   // set by setStateInformation, loaded by the timer
    juce::SpinLock requestLock;
    std::atomic<double> irSeconds { 0.0 };

    // ---- mode crossfade
    float modeGain = 0.0f;
    bool algoRunning = true, irRunning = false;
    double sampleRateHz = 48000.0;
    int chunkSize = 512;

    std::atomic<float>* pPre   = nullptr;
    std::atomic<float>* pSize  = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pDamp  = nullptr;
    std::atomic<float>* pLow   = nullptr;
    std::atomic<float>* pMod   = nullptr;
    std::atomic<float>* pMix   = nullptr;
    std::atomic<float>* pMode  = nullptr;
    std::atomic<float>* pIRLen = nullptr;
    std::atomic<float>* pIRCut = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterVerbAudioProcessor)
};
