#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "MSEngine.h"
#include "legacy/MSEngine11.h"

namespace ParamIDs
{
    // main
    inline constexpr const char* algorithm   = "algo";
    inline constexpr const char* mode        = "mode";        // 0 = algorithmic, 1 = impulse response
    inline constexpr const char* routing     = "routing";     // 0 = insert, 1 = aux
    inline constexpr const char* depth       = "depth";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* size        = "size";
    inline constexpr const char* preDelay    = "predelay";
    inline constexpr const char* erLevel     = "erlevel";
    inline constexpr const char* lateLevel   = "latelevel";
    inline constexpr const char* diffusion   = "diffusion";
    inline constexpr const char* density     = "density";
    inline constexpr const char* shape       = "shape";
    inline constexpr const char* spread      = "spread";
    inline constexpr const char* width       = "width";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* output      = "output";
    // locks
    inline constexpr const char* lockLevel   = "locklevel";
    inline constexpr const char* lockTone    = "locktone";
    inline constexpr const char* lockDecay   = "lockdecay";
    inline constexpr const char* lockWidth   = "lockwidth";
    // Spatial DNA
    inline constexpr const char* roomW       = "roomw";
    inline constexpr const char* roomD       = "roomd";
    inline constexpr const char* roomH       = "roomh";
    inline constexpr const char* srcX        = "srcx";
    inline constexpr const char* lisX        = "lisx";
    inline constexpr const char* lisY        = "lisy";
    inline constexpr const char* matFront    = "matfront";
    inline constexpr const char* matBack     = "matback";
    inline constexpr const char* matLeft     = "matleft";
    inline constexpr const char* matRight    = "matright";
    inline constexpr const char* matFloor    = "matfloor";
    inline constexpr const char* matCeiling  = "matceiling";
    inline constexpr const char* erOn        = "eron";
    inline constexpr const char* erDensity   = "erdensity";
    inline constexpr const char* erDiffusion = "erdiffusion";
    inline constexpr const char* erColor     = "ercolor";
    inline constexpr const char* erWidth     = "erwidth";
    inline constexpr const char* erStart     = "erstart";
    inline constexpr const char* erStop      = "erstop";
    inline constexpr const char* erPreDelay  = "erpredelay";
    // late
    inline constexpr const char* lateOn      = "lateon";
    inline constexpr const char* lateDelay   = "latedelay";
    inline constexpr const char* lateWidth   = "latewidth";
    inline constexpr const char* lateColor   = "latecolor";
    // decay EQ / filters
    inline constexpr const char* lfMult      = "lfmult";
    inline constexpr const char* lmMult      = "lmmult";
    inline constexpr const char* hmMult      = "hmmult";
    inline constexpr const char* hfMult      = "hfmult";
    inline constexpr const char* xLow        = "xlow";
    inline constexpr const char* xMid        = "xmid";
    inline constexpr const char* xHigh       = "xhigh";
    inline constexpr const char* lowCut      = "lowcut";
    inline constexpr const char* highCut     = "highcut";
    // modulation
    inline constexpr const char* modChar     = "modchar";
    inline constexpr const char* modRate     = "modrate";
    inline constexpr const char* modDepth    = "moddepth";
    inline constexpr const char* modRandom   = "modrandom";
    inline constexpr const char* modSmooth   = "modsmooth";
    // stereo
    inline constexpr const char* balance     = "balance";
    inline constexpr const char* crossfeed   = "crossfeed";
    inline constexpr const char* decorrelation = "decorr";
    // impulse response
    inline constexpr const char* irLength    = "irlength";
    // DSP engine version (1.2): 0 = 1.1 Legacy, 1 = 1.2
    inline constexpr const char* engine      = "engine";

    inline constexpr const char* materials[6] = { matFront, matBack, matLeft, matRight, matFloor, matCeiling };
}

class MasterSpaceAudioProcessor : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    MasterSpaceAudioProcessor();
    ~MasterSpaceAudioProcessor() override;

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

    // snapshot of all parameters (for the room view)
    mspace::Params getParamsSnapshot() const { return readParams(); }

    enum EngineVersion { engineLegacy11 = 0, engine12 = 1 };
    bool isLegacyEngine() const { return get (ParamIDs::engine) < 0.5f; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct PendingIR
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        bool stereo = false;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    mspace::Params readParams() const;
    template <class P> void fillParams (P& p) const;

    void timerCallback() override;
    juce::String irShapeSignature() const;
    void sendShapedIRToAudioThread();
    void takePendingIR() noexcept;
    void processStereo (float* left, float* right, int numSamples);
    void processIRPath (int numSamples);
    void processEngines (float* left, float* right, int numSamples);
    void resetIRPath();

    // ---- algorithmic path: current engine and the frozen 1.1 engine (Legacy mode)
    mspace::Engine engine;
    mspace11::Engine legacyEngine;
    juce::AudioBuffer<float> engineScratch;
    float engineGain = 1.0f;                 // 0 = legacy 1.1, 1 = 1.2 (cross-faded on switch)
    bool newRunning = true, legacyRunning = false;

    // ---- impulse-response path
    juce::dsp::Convolution convolver { juce::dsp::Convolution::NonUniform { 512 } };
    mspace::DelayLine irPreDelay[2];
    mspace::Biquad irLowCut[2], irHighCut[2];
    juce::AudioBuffer<float> irDry, irWet, monoScratch;
    float irCurPre = 0.0f, irCurMix = 0.3f, irCurWidth = 1.0f, irCurOut = 1.0f;
    double irCurLowCut = 80.0, irCurHighCut = 20000.0;
    std::atomic<bool> irActiveOnAudio { false };

    // handoff message thread -> audio thread (wait-free on the audio side)
    juce::SpinLock irLock;
    std::unique_ptr<PendingIR> pendingIR, retiredIR;

    // message-thread IR data
    juce::AudioBuffer<float> irOriginal;
    double irOriginalRate = 44100.0;
    juce::String irDisplayName, appliedShape, lastSeenShape;
    juce::File lastIRFolder;
    juce::String requestedIRPath;   // set by setStateInformation, loaded by the timer
    juce::SpinLock requestLock;
    std::atomic<double> irSeconds { 0.0 };

    // ---- mode crossfade
    float modeGain = 0.0f;
    bool algoRunning = true, irRunning = false;
    double sampleRateHz = 48000.0;
    std::atomic<double> hostRate { 48000.0 };
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);
    int chunkSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterSpaceAudioProcessor)
};
