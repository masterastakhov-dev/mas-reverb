#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "MSEngine.h"
#include "legacy/MSEngine11.h"
#include "IOMeters.h"
#ifdef MAS_STUDIO_18
 #include "MSCompressor.h"
#endif
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
   #ifdef MAS_ATMOS
    // ATMOSPHERIC TAIL (experimental build)
    inline constexpr const char* tail        = "tail";        // 0 room algorithm, 1 Atmospheric Tail
    inline constexpr const char* bloom       = "bloom";
    inline constexpr const char* motion      = "motion";
    inline constexpr const char* air         = "air";
    inline constexpr const char* charLock    = "charlock";
   #endif
   #ifdef MAS_RI
    // SMART DUCKER (1.4)
    inline constexpr const char* duckOn      = "duckon";
    inline constexpr const char* duckMode    = "duckmode";     // 0 Classic, 1 Vocal Focus, 2 Spectral
    inline constexpr const char* duckAmount  = "duckamount";
    inline constexpr const char* duckThresh  = "duckthresh";
    inline constexpr const char* duckAttack  = "duckattack";
    inline constexpr const char* duckHold    = "duckhold";
    inline constexpr const char* duckRelease = "duckrelease";
    inline constexpr const char* duckRatio   = "duckratio";
    inline constexpr const char* duckSens    = "ducksens";
    inline constexpr const char* duckEarly   = "duckearly";
    inline constexpr const char* duckLate    = "ducklate";
    inline constexpr const char* duckSpectral = "duckspectral";
    inline constexpr const char* scLowCut    = "sclowcut";
    inline constexpr const char* scHighCut   = "schighcut";
    inline constexpr const char* scSource    = "scsource";     // 0 internal, 1 external (host sidechain bus)
    // RESONANCE GUARD (1.4)
    inline constexpr const char* rgOn        = "rgon";
    inline constexpr const char* rgMode      = "rgmode";       // 0 Smooth, 1 Deep, 2 Tail Aware
    inline constexpr const char* rgSmooth    = "rgsmooth";
    inline constexpr const char* rgDetail    = "rgdetail";
    inline constexpr const char* rgSelect    = "rgselect";
    inline constexpr const char* rgAttack    = "rgattack";
    inline constexpr const char* rgRelease   = "rgrelease";
    inline constexpr const char* rgMax       = "rgmax";
    inline constexpr const char* rgLow       = "rglow";
    inline constexpr const char* rgHigh      = "rghigh";
    inline constexpr const char* rgTarget    = "rgtarget";     // 0 Early + Late, 1 Late, 2 Early
    inline constexpr const char* rgEarly     = "rgearly";
    inline constexpr const char* rgLate      = "rglate";
    inline constexpr const char* rgMatch     = "rgmatch";
    inline constexpr const char* quality     = "quality";      // 0 LIVE (no latency), 1 STUDIO HQ (latency reported)
    // HARMONIC COLOR (1.4)
    inline constexpr const char* hcOn        = "hcon";
    inline constexpr const char* hcChar      = "hcchar";       // 0 Soft, 1 Tube, 2 Tape
    inline constexpr const char* hcDrive     = "hcdrive";
    inline constexpr const char* hcColor     = "hccolor";
    inline constexpr const char* hcMix       = "hcmix";
    inline constexpr const char* hcComp      = "hccomp";
    inline constexpr const char* hcPos       = "hcpos";        // 0 POST (wet), 1 PRE (reverb input)

    // every 1.4 parameter: states and presets without them load with the defaults (both modules OFF, LIVE)
    inline constexpr const char* riParams[] = { duckOn, duckMode, duckAmount, duckThresh, duckAttack, duckHold, duckRelease,
                                                duckRatio, duckSens, duckEarly, duckLate, duckSpectral, scLowCut, scHighCut,
                                                scSource, rgOn, rgMode, rgSmooth, rgDetail, rgSelect, rgAttack, rgRelease,
                                                rgMax, rgLow, rgHigh, rgTarget, rgEarly, rgLate, rgMatch, quality,
                                                hcOn, hcChar, hcDrive, hcColor, hcMix, hcComp, hcPos };
   #endif
   #ifdef MAS_STUDIO_18
    // 1.8 STUDIO: M/S HARMONIC COLOR (STEREO by default)
    inline constexpr const char* hcMode      = "hcmode";       // 0 STEREO, 1 M/S
    inline constexpr const char* hcMidDrive  = "hcmdrive";
    inline constexpr const char* hcSideDrive = "hcsdrive";
    inline constexpr const char* hcMidMix    = "hcmmix";
    inline constexpr const char* hcSideMix   = "hcsmix";
    // 1.8 STUDIO: M/S OUTPUT COMPRESSOR (OFF by default)
    inline constexpr const char* cpOn        = "cpon";
    inline constexpr const char* cpRouting   = "cprouting";    // 0 WET ONLY, 1 FULL OUTPUT
    inline constexpr const char* cpMix       = "cpmix";        // PARALLEL MIX
    inline constexpr const char* cpHpfOn     = "cphpfon";
    inline constexpr const char* cpHpf       = "cphpf";        // side-chain high-pass
    inline constexpr const char* cpMidThr    = "cpmthr";
    inline constexpr const char* cpMidRatio  = "cpmratio";
    inline constexpr const char* cpMidAtk    = "cpmatk";
    inline constexpr const char* cpMidRel    = "cpmrel";
    inline constexpr const char* cpMidKnee   = "cpmknee";
    inline constexpr const char* cpMidGain   = "cpmgain";
    inline constexpr const char* cpSideThr   = "cpsthr";
    inline constexpr const char* cpSideRatio = "cpsratio";
    inline constexpr const char* cpSideAtk   = "cpsatk";
    inline constexpr const char* cpSideRel   = "cpsrel";
    inline constexpr const char* cpSideKnee  = "cpsknee";
    inline constexpr const char* cpSideGain  = "cpsgain";
    // every 1.8 parameter: states and presets without them load with the defaults (STEREO, compressor OFF)
    inline constexpr const char* studio18Params[] = { hcMode, hcMidDrive, hcSideDrive, hcMidMix, hcSideMix,
                                                      cpOn, cpRouting, cpMix, cpHpfOn, cpHpf,
                                                      cpMidThr, cpMidRatio, cpMidAtk, cpMidRel, cpMidKnee, cpMidGain,
                                                      cpSideThr, cpSideRatio, cpSideAtk, cpSideRel, cpSideKnee, cpSideGain };
   #endif
   #ifdef MAS_EQ
    // REVERB EQ (1.5 EQ LAB): "eqon" and, per bank (w = WET, e = EARLY, l = LATE) and band (1..8):
    // u = used, b = bypass, t = type, f = frequency, g = gain, q = Q, s = slope  ->  e.g. "eqw3f".
    // States and presets without them load with the EQ off and no band in use.
    inline constexpr const char* eqOn = "eqon";
    inline constexpr char eqBankChar[3] = { 'w', 'e', 'l' };
    inline constexpr char eqFieldChar[] = { 'u', 'b', 't', 'f', 'g', 'q', 's'
#ifdef MAS_STUDIO_DYN
                                             , 'd', 'h', 'm'
#endif
                                           };
    enum EqField { eqUsed = 0, eqBypass, eqType, eqFreq, eqGain, eqQ, eqSlope,
#ifdef MAS_STUDIO_DYN
                   eqDynOn, eqDynThreshold, eqDynMax,
#endif
                   eqNumFields };
    inline juce::String eqId (int bank, int band, int field)
    {
        return juce::String ("eq") + juce::String::charToString ((juce::juce_wchar) eqBankChar[bank]) + juce::String (band + 1)
               + juce::String::charToString ((juce::juce_wchar) eqFieldChar[field]);
    }
    inline bool isEqParam (const juce::String& id) { return id == eqOn || (id.length() == 5 && id.startsWith ("eq") && id != "eqon"); }
   #endif

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
   #ifdef MAS_RI
    // scL / scR: host sidechain (nullptr = none). With a latency (STUDIO HQ) the 1.1 engine's output is
    // delayed like the 1.2 engine's, and the cross-fade gains on the output side follow with the same delay.
    void process (float* left, float* right, int n, bool legacyTarget, double fs, const float* scL, const float* scR);
    void setAlignment (int latency);
    int alignLatency = 0;
    mspace::DelayLine legacyAlign[2], gainAlign;
    float engineGainOut = 1.0f;
   #ifdef MAS_STUDIO_18
    // M/S OUTPUT COMPRESSOR, WET ONLY: while set, process() also writes the wet part of its output here,
    // through the same cross-fade and alignment as the output itself
    void setWetTap (float* l, float* r) { tapL = l; tapR = r; }
    float* tapL = nullptr; float* tapR = nullptr;
    mspace::DelayLine legacyTapAlign[2];
   #endif
   #else
    void process (float* left, float* right, int n, bool legacyTarget, double fs);
   #endif
    double getTailSeconds (bool legacyActive) const;
};

#ifdef MAS_RI
// Values the audio thread publishes for the 1.4 displays (lock-free, read by the editor).
struct RIMeters
{
    static constexpr int maxBands = 256;
    std::atomic<float> duckEarlyDb { 0.0f }, duckLateDb { 0.0f }, rgMaxCutDb { 0.0f }, rgMakeupDb { 0.0f };
    std::atomic<float> hcResidueDb { -120.0f }, hcCompDb { 0.0f };
    std::atomic<bool> duckActive { false }, rgProcessing { false }, scExternalLive { false }, engineAvailable { true };
    std::atomic<int> numBands { 0 }, latency { 0 }, quality { 0 };
    std::atomic<float> bandHz[maxBands], levelDb[maxBands], cutDb[maxBands];
    std::atomic<juce::uint32> generation { 0 };
    RIMeters() { for (int b = 0; b < maxBands; ++b) { bandHz[b] = 1000.0f; levelDb[b] = -120.0f; cutDb[b] = 0.0f; } }
};
#endif

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
   #ifdef MAS_STUDIO_DYN
    // host bypass: the audio is JUCE's default bypass, unchanged; only the SIGNAL RAILS see it
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;
   #endif

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
    bool isIRActiveOnAudio() const { return irActiveOnAudio.load(); }      // the IR has reached the audio thread
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

   #ifdef MAS_RI
    // ---- 1.4: SMART DUCKER / RESONANCE GUARD
    RIMeters riMeters;
    void setDeltaListen (bool on) { deltaListen.store (on); }    // monitoring only: not saved, not in presets
    bool getDeltaListen() const { return deltaListen.load(); }
    void setVisualisation (bool on) { vizOn.store (on); }        // displays only: never changes the audio
    bool getVisualisation() const { return vizOn.load(); }
    bool hasSidechainBus() const;
    int latencyForQuality (int q) const;
   #endif
   #ifdef MAS_EQ
    // ---- 1.5 EQ LAB: REVERB EQ + analyzer
    req::Analyzer analyzer;                                      // taps the editor reads (lock-free)
    // LIVING SPACE display: measured levels in dBFS (input, early L/R, late L/R), written by the audio thread
    // only while the display asks for them
    std::atomic<float> spaceLevel[5] { { -140.0f }, { -140.0f }, { -140.0f }, { -140.0f }, { -140.0f } };
    std::atomic<bool> spaceLevelsWanted { false };
   #ifdef MAS_STUDIO_DYN
    std::atomic<float> dynReduction[3][8] {};     // Dynamic Cut: current reduction per bank / band, dB (>= 0)
    mas::IOMeters ioMeters;                       // SIGNAL RAILS: INPUT (before processing) / OUTPUT (after everything)
   #endif
    std::atomic<int> spaceVisual { 0 };                          // 0 FULL, 1 ECO, 2 OFF (a view setting, not a parameter)
    req::Settings readEq() const;                                // current EQ settings (any thread)
    bool eqAvailable() const { return riMeters.engineAvailable.load(); }   // algorithmic 1.2 engine only
   #endif

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
   #ifndef MAS_RI
    void processStereo (float* left, float* right, int numSamples);
   #endif
    void processIRPath (int numSamples);
   #ifdef MAS_RI
    void processAlgo (float* left, float* right, int numSamples, const float* scL, const float* scR);
    void processStereo (float* left, float* right, int numSamples, const float* scL, const float* scR);
    void publishMeters (int numSamples);
    void setAlignment (int latency);
    std::atomic<bool> deltaListen { false }, vizOn { true };
    juce::AudioBuffer<float> scScratch;
    int alignQ = 0, alignL = 0, reportedLatency = -1, meterCountdown = 0, qHold = 0;
    bool qSwitchPending = false, modeRingSteady = false;
    float qFade = 1.0f, modeGainOut = 0.0f;
    mspace::DelayLine irAlign[2], txAlign, modeAlign;
   #ifdef MAS_EQ
    std::atomic<float>* eqRaw[req::numBanks][req::kBands][ParamIDs::eqNumFields] = {};
    std::atomic<float>* eqOnRaw = nullptr;
   #endif
   #else
    void processAlgo (float* left, float* right, int numSamples);
   #endif
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
   #ifdef MAS_STUDIO_18
    // M/S OUTPUT COMPRESSOR: one instance. WET ONLY uses a tap of the wet part of the output (wetTap), carried
    // through every cross-fade with the same gains as the output (txTap: outgoing path of a state transition,
    // irTap: the IR path, delayed like it in STUDIO HQ).
    msc::MSCompressor comp;
    juce::AudioBuffer<float> wetTap, txTap, irTap;
    mspace::DelayLine irTapAlign[2];
    float* curTapL = nullptr; float* curTapR = nullptr;       // set for the chunk being processed (nullptr = no tap)
    msc::Settings readComp() const;
   public:
    float compGrDb (bool side) const { return (side ? comp.grSide : comp.grMid).load (std::memory_order_relaxed); }
    bool compActive() const { return comp.isActive(); }
   private:
   #endif
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
