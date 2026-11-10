#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "MSEngine.h"
#include "legacy/MSEngine11.h"
#include <map>

namespace mspreset { class PresetManager; }

// Complete sound state: plain (denormalised) value of every parameter, by parameter ID.
using ParamMap = std::map<juce::String, float>;

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

//==============================================================================
// One algorithmic signal path: the 1.2 engine and the frozen 1.1 engine (Legacy DSP mode),
// cross-faded when the DSP Engine parameter changes.
struct AlgoPath
{
    mspace::Engine engine;
    mspace11::Engine legacy;
    juce::AudioBuffer<float> scratch;
    float engineGain = 1.0f;                 // 0 = legacy 1.1, 1 = 1.2
    bool newRunning = true, legacyRunning = false;

    void prepare (double fs, int maxBlock, const mspace::Params&, const mspace11::Params&, bool legacyActive);
    void setParams (const mspace::Params& p, const mspace11::Params& lp) { engine.setParams (p); legacy.setParams (lp); }
    void reset() { engine.reset(); legacy.reset(); }
    // clears all audio state and jumps (no smoothing) to the given parameters; audio-thread safe, no allocation
    void snap (const mspace::Params&, const mspace11::Params&, bool legacyActive);
    void process (float* left, float* right, int n, bool legacyTarget, double fs);
    double getTailSeconds (bool legacyActive) const;
};

//==============================================================================
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

    //==========================================================================
    // Complete state (message thread). applyParamMap() sets every parameter (missing ones -> default),
    // notifies the host and moves the DSP to the new state through a click-free transition:
    // the running path keeps the old state and fades out, a second path starts from a clean,
    // fully re-initialised engine with the new state and fades in.
    ParamMap captureParamMap() const;
    void applyParamMap (const ParamMap&, const juce::String& irPath = {});
    ParamMap defaultParamMap() const;
    juce::String getIRPath() const { return apvts.state.getProperty ("irPath").toString(); }
    void beginStateTransition() noexcept { transitionRequest.fetch_add (1); }
    void endStateTransition() noexcept   { transitionReady.store (transitionRequest.load()); }

    mspreset::PresetManager& getPresetManager();

    //==========================================================================
    // A/B compare (message thread). The live parameters always belong to the active slot.
    int  getActiveSlot() const;
    bool slotHasState (int slot) const;
    void selectSlot (int slot);                 // stores the live state in the active slot, recalls the other one
    void copyActiveToOther();                    // A -> B (or B -> A)
    bool canUndoCompare() const;
    void undoCompare();                          // back to the previous variant (toggles: undo / redo)
    void pushCompareHistory();                   // called before preset loads
    void setLevelMatch (bool on);
    bool getLevelMatch() const { return levelMatchOn.load(); }
    float getMatchTrimDb() const { return matchTrimDb.load(); }
    juce::String getMatchStatus() const;
    void refreshLevelMatch();                    // re-measures when the slots changed (call from a UI timer)

    // loudness (LUFS-like, K-weighted) of a state for pink noise, rendered offline (any thread but audio)
    static double measureLoudness (const ParamMap&, double sampleRate);

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
public:
    template <class P, class Get> static void fillParamsWith (P& p, Get&& get);
private:

    void timerCallback() override;
    juce::String irShapeSignature() const;
    void sendShapedIRToAudioThread();
    void takePendingIR() noexcept;
    void processStereo (float* left, float* right, int numSamples);
    void processIRPath (int numSamples);
    void processAlgo (float* left, float* right, int numSamples);
    void resetIRPath();

    // ---- algorithmic path: two complete paths for click-free state transitions (preset load, A/B)
    AlgoPath paths[2];
    int activePath = 0;
    mspace::Params curP, frozenP;
    mspace11::Params curLP, frozenLP;
    bool curLegacy = false, frozenLegacy = false, haveCur = false;
    enum class Tx { idle, holding, fading } txState = Tx::idle;
    int txHold = 0;
    float txGain = 1.0f;
    juce::AudioBuffer<float> txScratch;
    std::atomic<juce::uint32> transitionRequest { 0 }, transitionReady { 0 };
    juce::uint32 transitionSeen = 0;

    // ---- A/B compare
    struct CompareContext
    {
        int active = 0;
        ParamMap slot[2];
        bool has[2] { false, false };
        juce::String irPath[2], presetKey[2], presetName[2];
    };
    void captureLiveInto (CompareContext&) const;    // live state -> active slot of the context
    void setLivePresetInfo (const juce::String& key, const juce::String& name);
    CompareContext compare, previous;
    bool hasPrevious = false;
    mutable juce::CriticalSection compareLock;
    std::atomic<bool> levelMatchOn { false };
    std::atomic<float> matchTrimDb { 0.0f };
    std::atomic<int> audioSlot { 0 };            // active A/B slot, for the audio thread
    float curTrim = 1.0f;
    juce::String matchStatus;
    juce::int64 matchedHash = 0, pendingMatchHash = 0;
    std::atomic<int> matchGeneration { 0 };
    juce::ThreadPool matchPool { 1 };
    void storeCompareState (juce::ValueTree& state) const;
    void restoreCompareState (const juce::XmlElement& xml);
    static juce::int64 hashOf (const ParamMap&);

    std::unique_ptr<mspreset::PresetManager> presetManager;

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
