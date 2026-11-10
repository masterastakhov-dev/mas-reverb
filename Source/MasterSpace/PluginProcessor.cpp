#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF  = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
    constexpr double kMaxIRSeconds = 30.0;
    constexpr float  kIRWetTrim    = 3.2f;    // JUCE normalises IRs to -18 dB energy; bring wet near the algorithmic level
    constexpr double kModeFadeSec  = 0.03;
    constexpr double kIRSplitSec   = 0.08;    // early / late split point for impulse responses
    constexpr int    kVersionHint  = 1;       // MASTER SPACE 1.0 parameter set

    juce::NormalisableRange<float> range (float lo, float hi, float centre = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi);
        if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        return r;
    }

    juce::String fmtMs (float v)   { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String fmtSec (float v)  { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; }
    juce::String fmtPct (float v)  { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String fmtHz (float v)   { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                        : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; }
    juce::String fmtDb (float v)   { return v <= -59.9f ? juce::String ("Off") : (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String fmtMult (float v) { return juce::String (v, 2) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")); }

    float parseHz (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = s.getFloatValue();
        return s.contains ("k") ? v * 1000.0f : v;
    }
    float parseDb (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        return (s.startsWith ("off") || s.contains ("inf")) ? -60.0f : s.getFloatValue();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterSpaceAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    using namespace ParamIDs;

    auto addF = [&p] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                      std::function<juce::String (float)> fmt, std::function<float (const juce::String&)> parse = nullptr)
    {
        auto attr = Attr().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); });
        if (parse) attr = attr.withValueFromStringFunction ([parse] (const juce::String& s) { return parse (s); });
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, kVersionHint }, name, r, def, attr));
    };
    auto addChoice = [&p] (const char* id, const char* name, juce::StringArray items, int def)
    {
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, kVersionHint }, name, items, def));
    };
    auto addBool = [&p] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, kVersionHint }, name, def));
    };
    auto pct = [] (float v) { return fmtPct (v); };
    auto bipolarPos = [] (float v)   // 0..100 -> L 50 .. C .. R 50
    {
        const int i = juce::roundToInt (v - 50.0f);
        return i == 0 ? juce::String ("C") : (i < 0 ? "L " + juce::String (-i) : "R " + juce::String (i));
    };
    auto bipolar = [] (const char* neg, const char* pos)
    {
        return std::function<juce::String (float)> ([neg, pos] (float v)
        {
            const int i = juce::roundToInt (v);
            return i == 0 ? juce::String ("0") : (i < 0 ? juce::String (neg) + " " + juce::String (-i) : juce::String (pos) + " " + juce::String (i));
        });
    };

    // ---- main
    addChoice (algorithm, "Algorithm", { "Studio Room", "Chamber", "Hall" }, 1);
    addChoice (mode,      "Mode",      { "Algorithmic", "IR" }, 0);
    addChoice (routing,   "Routing",   { "Insert", "Aux" }, 0);
    addF (depth,     "Depth",       range (0.0f, 100.0f),       35.0f, pct);
    addF (decay,     "Decay",       range (0.1f, 30.0f, 2.0f),  1.8f,  fmtSec);
    addF (size,      "Size",        range (0.0f, 100.0f),       50.0f, pct);
    addF (preDelay,  "Pre-Delay",   range (0.0f, 500.0f, 60.0f), 0.0f, fmtMs);
    addF (erLevel,   "Early Level", range (-60.0f, 12.0f, -12.0f), 0.0f, fmtDb, parseDb);
    addF (lateLevel, "Late Level",  range (-60.0f, 12.0f, -12.0f), 0.0f, fmtDb, parseDb);
    addF (diffusion, "Diffusion",   range (0.0f, 100.0f),       70.0f, pct);
    addF (density,   "Density",     range (0.0f, 100.0f),       70.0f, pct);
    addF (shape,     "Shape",       range (0.0f, 100.0f),       35.0f, pct);
    addF (spread,    "Spread",      range (0.0f, 500.0f, 100.0f), 60.0f, fmtMs);
    addF (width,     "Width",       range (0.0f, 200.0f),       100.0f, pct);
    addF (mix,       "Mix",         range (0.0f, 100.0f),       35.0f, pct);
    addF (output,    "Output",      range (-24.0f, 12.0f),      0.0f,  fmtDb, parseDb);

    // ---- locks
    addBool (lockLevel, "Level Lock", true);
    addBool (lockTone,  "Tone Lock",  false);
    addBool (lockDecay, "Decay Lock", true);
    addBool (lockWidth, "Width Lock", false);

    // ---- Spatial DNA
    auto metres = [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " m"; };
    addF (roomW, "Room Width",  range (2.0f, 40.0f, 10.0f), 7.5f,  metres);
    addF (roomD, "Room Depth",  range (2.0f, 60.0f, 12.0f), 10.0f, metres);
    addF (roomH, "Room Height", range (2.0f, 20.0f, 5.0f),  4.0f,  metres);
    addF (srcX,  "Source X",    range (0.0f, 100.0f), 50.0f, bipolarPos);
    addF (lisX,  "Listener X",  range (0.0f, 100.0f), 50.0f, bipolarPos);
    addF (lisY,  "Listener Y",  range (10.0f, 95.0f), 72.0f, pct);
    const juce::StringArray mats { "Absorber", "Curtain", "Wood", "Plaster", "Concrete", "Glass" };
    addChoice (matFront,   "Front Wall", mats, 3);
    addChoice (matBack,    "Back Wall",  mats, 2);
    addChoice (matLeft,    "Left Wall",  mats, 3);
    addChoice (matRight,   "Right Wall", mats, 3);
    addChoice (matFloor,   "Floor",      mats, 2);
    addChoice (matCeiling, "Ceiling",    mats, 3);
    addBool (erOn, "Early On", true);
    addF (erDensity,   "ER Density",   range (0.0f, 100.0f), 60.0f, pct);
    addF (erDiffusion, "ER Diffusion", range (0.0f, 100.0f), 40.0f, pct);
    addF (erColor,     "ER Color",     range (-100.0f, 100.0f), 0.0f, bipolar ("Dark", "Bright"));
    addF (erWidth,     "ER Width",     range (0.0f, 200.0f), 100.0f, pct);
    addF (erStart,     "ER Start",     range (0.0f, 100.0f, 20.0f),   0.0f,   fmtMs);
    addF (erStop,      "ER Stop",      range (10.0f, 300.0f, 100.0f), 150.0f, fmtMs);
    addF (erPreDelay,  "ER Pre-Delay", range (0.0f, 200.0f, 40.0f),   0.0f,   fmtMs);

    // ---- late
    addBool (lateOn, "Late On", true);
    addF (lateDelay, "Late Delay", range (0.0f, 300.0f, 60.0f), 20.0f, fmtMs);
    addF (lateWidth, "Late Width", range (0.0f, 200.0f),        100.0f, pct);
    addF (lateColor, "Late Color", range (-100.0f, 100.0f),     0.0f,  bipolar ("Dark", "Bright"));

    // ---- decay EQ / filters
    addF (lfMult, "LF Decay",       range (0.1f, 4.0f, 1.0f), 1.2f,  fmtMult);
    addF (lmMult, "Low-Mid Decay",  range (0.1f, 4.0f, 1.0f), 1.0f,  fmtMult);
    addF (hmMult, "High-Mid Decay", range (0.1f, 4.0f, 1.0f), 0.85f, fmtMult);
    addF (hfMult, "HF Decay",       range (0.1f, 4.0f, 1.0f), 0.55f, fmtMult);
    addF (xLow,  "Crossover Low",  range (40.0f, 1000.0f, 200.0f),    200.0f,  fmtHz, parseHz);
    addF (xMid,  "Crossover Mid",  range (300.0f, 8000.0f, 1500.0f),  1500.0f, fmtHz, parseHz);
    addF (xHigh, "Crossover High", range (1500.0f, 18000.0f, 6000.0f), 6000.0f, fmtHz, parseHz);
    addF (lowCut,  "Low Cut",  range (20.0f, 2000.0f, 150.0f),   20.0f,    [] (float v) { return v <= 20.5f ? juce::String ("Off") : fmtHz (v); }, parseHz);
    addF (highCut, "High Cut", range (1000.0f, 20000.0f, 6000.0f), 20000.0f, [] (float v) { return v >= 19900.0f ? juce::String ("Off") : fmtHz (v); }, parseHz);

    // ---- modulation
    addChoice (modChar, "Mod Character", { "Smooth", "Random", "Vintage", "Modern" }, 0);
    addF (modRate,   "Mod Rate",   range (0.01f, 5.0f, 0.5f), 0.6f, [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; });
    addF (modDepth,  "Mod Depth",  range (0.0f, 100.0f), 35.0f, pct);
    addF (modRandom, "Mod Random", range (0.0f, 100.0f), 50.0f, pct);
    addF (modSmooth, "Mod Smooth", range (0.0f, 100.0f), 60.0f, pct);

    // ---- stereo
    addF (balance,       "Balance",       range (-100.0f, 100.0f), 0.0f,  bipolar ("L", "R"));
    addF (crossfeed,     "Crossfeed",     range (0.0f, 100.0f),    20.0f, pct);
    addF (decorrelation, "Decorrelation", range (0.0f, 100.0f),    85.0f, pct);

    // ---- impulse response
    addF (irLength, "IR Length", range (5.0f, 100.0f), 100.0f, pct);

    return { p.begin(), p.end() };
}

MasterSpaceAudioProcessor::MasterSpaceAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MasterSpaceState", createLayout())
{
    lastIRFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    startTimerHz (10);
}

MasterSpaceAudioProcessor::~MasterSpaceAudioProcessor()
{
    stopTimer();
}

mspace::Params MasterSpaceAudioProcessor::readParams() const
{
    using namespace ParamIDs;
    mspace::Params p;
    p.algorithm    = juce::roundToInt (get (algorithm));
    p.routing      = juce::roundToInt (get (routing));
    p.depth        = get (depth) * 0.01f;
    p.decay        = get (decay);
    p.size         = get (size) * 0.01f;
    p.preDelayMs   = get (preDelay);
    p.erLevelDb    = get (erLevel);
    p.lateLevelDb  = get (lateLevel);
    p.erOn         = get (erOn) > 0.5f;
    p.lateOn       = get (lateOn) > 0.5f;
    p.diffusion    = get (diffusion) * 0.01f;
    p.density      = get (density) * 0.01f;
    p.shape        = get (shape) * 0.01f;
    p.spreadMs     = get (spread);
    p.width        = get (width) * 0.01f;
    p.mix          = get (mix) * 0.01f;
    p.outputDb     = get (output);

    p.lockLevel = get (lockLevel) > 0.5f;
    p.lockTone  = get (lockTone) > 0.5f;
    p.lockDecay = get (lockDecay) > 0.5f;
    p.lockWidth = get (lockWidth) > 0.5f;

    p.roomW = get (roomW); p.roomD = get (roomD); p.roomH = get (roomH);
    p.srcX = get (srcX) * 0.01f;
    p.lisX = get (lisX) * 0.01f;
    p.lisY = get (lisY) * 0.01f;
    for (int i = 0; i < 6; ++i)
        p.materials[(size_t) i] = juce::roundToInt (get (materials[i]));
    p.erDensity    = get (erDensity) * 0.01f;
    p.erDiffusion  = get (erDiffusion) * 0.01f;
    p.erColor      = get (erColor) * 0.01f;
    p.erWidth      = get (erWidth) * 0.01f;
    p.erStartMs    = get (erStart);
    p.erStopMs     = get (erStop);
    p.erPreDelayMs = get (erPreDelay);

    p.lateDelayMs  = get (lateDelay);
    p.lateWidth    = get (lateWidth) * 0.01f;
    p.lateColor    = get (lateColor) * 0.01f;

    p.lfMult = get (lfMult); p.lmMult = get (lmMult); p.hmMult = get (hmMult); p.hfMult = get (hfMult);
    p.xLow = get (xLow); p.xMid = get (xMid); p.xHigh = get (xHigh);
    p.lowCutHz = get (lowCut); p.highCutHz = get (highCut);

    p.modCharacter = juce::roundToInt (get (modChar));
    p.modRate      = get (modRate);
    p.modDepth     = get (modDepth) * 0.01f;
    p.modRandom    = get (modRandom) * 0.01f;
    p.modSmooth    = get (modSmooth) * 0.01f;

    p.balance       = get (balance) * 0.01f;
    p.crossfeed     = get (crossfeed) * 0.01f;
    p.decorrelation = get (decorrelation) * 0.01f;
    return p;
}

//==============================================================================
void MasterSpaceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    if (std::abs (hostRate.load() - sampleRate) > 1.0)
    {
        hostRate = sampleRate;
        if (irOriginal.getNumSamples() > 0)
            juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
            {
                if (! alive.expired()) sendShapedIRToAudioThread();
            });
    }
    chunkSize = juce::jmax (64, samplesPerBlock);

    engine.setParams (readParams());
    engine.prepare (sampleRate);

    convolver.prepare ({ sampleRate, (juce::uint32) chunkSize, 2 });
    for (auto& d : irPreDelay) d.allocate ((int) std::ceil (0.52 * sampleRate) + 8);
    irDry.setSize (2, chunkSize);
    irWet.setSize (2, chunkSize);
    monoScratch.setSize (1, chunkSize);

    irCurLowCut  = get (ParamIDs::lowCut);
    irCurHighCut = get (ParamIDs::highCut);
    for (auto& f : irLowCut)  f.setHighPass (irCurLowCut, sampleRate);
    for (auto& f : irHighCut) f.setLowPass (irCurHighCut, sampleRate);
    irCurPre   = get (ParamIDs::preDelay) * 0.001f * (float) sampleRate;
    irCurMix   = get (ParamIDs::routing) > 0.5f ? 1.0f : get (ParamIDs::mix) * 0.01f;
    irCurWidth = get (ParamIDs::width) * 0.01f;
    irCurOut   = mspace::dbToGain (get (ParamIDs::output));

    modeGain = get (ParamIDs::mode) > 0.5f ? 1.0f : 0.0f;
    algoRunning = modeGain < 1.0f;
    irRunning = modeGain > 0.0f;
    resetIRPath();
}

void MasterSpaceAudioProcessor::reset()
{
    engine.reset();
    resetIRPath();
}

void MasterSpaceAudioProcessor::resetIRPath()
{
    convolver.reset();
    for (int c = 0; c < 2; ++c)
    {
        irPreDelay[c].clear();
        irLowCut[c].clear();
        irHighCut[c].clear();
    }
}

bool MasterSpaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();

    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();

    return false;
}

//==============================================================================
void MasterSpaceAudioProcessor::takePendingIR() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (irLock);
    if (! lock.isLocked() || pendingIR == nullptr || retiredIR != nullptr)
        return;

    if (pendingIR->buffer.getNumSamples() > 0)
    {
        convolver.loadImpulseResponse (std::move (pendingIR->buffer), pendingIR->sampleRate,
                                       pendingIR->stereo ? juce::dsp::Convolution::Stereo::yes
                                                         : juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::no,
                                       juce::dsp::Convolution::Normalise::no);
        irActiveOnAudio = true;
    }
    else
    {
        irActiveOnAudio = false;
    }

    retiredIR = std::move (pendingIR);   // freed later on the message thread
}

void MasterSpaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    takePendingIR();
    engine.setParams (readParams());

    if (numOut >= 2 && numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in -> stereo out

    for (int pos = 0; pos < n; pos += chunkSize)
    {
        const int len = juce::jmin (chunkSize, n - pos);
        float* l = buffer.getWritePointer (0, pos);

        if (numOut >= 2)
        {
            processStereo (l, buffer.getWritePointer (1, pos), len);
        }
        else
        {
            float* r = monoScratch.getWritePointer (0);
            juce::FloatVectorOperations::copy (r, l, len);
            processStereo (l, r, len);
        }
    }

    for (int ch = juce::jmax (2, numIn); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

void MasterSpaceAudioProcessor::processStereo (float* left, float* right, int n)
{
    const float target = get (ParamIDs::mode) > 0.5f ? 1.0f : 0.0f;
    const bool needIR   = target > 0.0f || modeGain > 0.0f;
    const bool needAlgo = target < 1.0f || modeGain < 1.0f;

    if (needIR)
    {
        irDry.copyFrom (0, 0, left, n);
        irDry.copyFrom (1, 0, right, n);
    }

    if (needAlgo)
    {
        if (! algoRunning) { engine.reset(); algoRunning = true; }
        engine.process (left, right, n);
    }
    else
    {
        algoRunning = false;
    }

    if (! needIR)
    {
        irRunning = false;
        return;
    }

    if (! irRunning) { resetIRPath(); irRunning = true; }
    processIRPath (n);

    const float step = (float) (1.0 / (kModeFadeSec * sampleRateHz));
    const float* irL = irDry.getReadPointer (0);
    const float* irR = irDry.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        modeGain = target > modeGain ? juce::jmin (target, modeGain + step)
                                     : juce::jmax (target, modeGain - step);
        left[i]  = left[i]  * (1.0f - modeGain) + irL[i] * modeGain;
        right[i] = right[i] * (1.0f - modeGain) + irR[i] * modeGain;
    }
}

// Reads the dry signal from irDry and writes the mixed IR-mode output back into irDry.
void MasterSpaceAudioProcessor::processIRPath (int n)
{
    using namespace ParamIDs;
    const double fs = sampleRateHz;

    const double lc = juce::jlimit (20.0, 2000.0, (double) get (lowCut));
    const double hc = juce::jlimit (1000.0, 20000.0, (double) get (highCut));
    irCurLowCut  += 0.3 * (lc - irCurLowCut);
    irCurHighCut += 0.3 * (hc - irCurHighCut);
    for (auto& f : irLowCut) f.setHighPass (irCurLowCut, fs);
    const bool highCutOn = irCurHighCut < 19900.0;
    if (highCutOn)
        for (auto& f : irHighCut) f.setLowPass (irCurHighCut, fs);

    const float sSlow = (float) (1.0 - std::exp (-1.0 / (0.060 * fs)));
    const float sFast = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
    const float targetPre   = juce::jlimit (0.0f, 500.0f, get (preDelay)) * 0.001f * (float) fs;
    const float targetMix   = get (routing) > 0.5f ? 1.0f : juce::jlimit (0.0f, 1.0f, get (mix) * 0.01f);
    const float targetWidth = get (width) * 0.01f;
    const float targetOut   = mspace::dbToGain (get (output));
    const bool haveIR = irActiveOnAudio.load();

    // wet input: low cut + pre-delay
    for (int c = 0; c < 2; ++c)
    {
        const float* dry = irDry.getReadPointer (c);
        float* wet = irWet.getWritePointer (c);
        float pre = irCurPre;

        for (int i = 0; i < n; ++i)
        {
            pre += sSlow * (targetPre - pre);
            const float x = irLowCut[c].process (dry[i]);
            const float y = irPreDelay[c].readLinear (juce::jmax (pre, 1.0f));
            irPreDelay[c].push (x);
            wet[i] = pre < 1.0f ? x : y;
        }

        if (c == 1) irCurPre = pre;
    }

    if (haveIR)
    {
        juce::dsp::AudioBlock<float> block (irWet);
        auto sub = block.getSubBlock (0, (size_t) n);
        convolver.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

    float* outL = irDry.getWritePointer (0);
    float* outR = irDry.getWritePointer (1);
    const float* wetL = irWet.getReadPointer (0);
    const float* wetR = irWet.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        irCurMix   += sFast * (targetMix - irCurMix);
        irCurWidth += sFast * (targetWidth - irCurWidth);
        irCurOut   += sFast * (targetOut - irCurOut);

        float wl = haveIR ? wetL[i] * kIRWetTrim : 0.0f;
        float wr = haveIR ? wetR[i] * kIRWetTrim : 0.0f;
        if (highCutOn) { wl = irHighCut[0].process (wl); wr = irHighCut[1].process (wr); }

        const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * irCurWidth;
        wl = m + s; wr = m - s;

        const float wetAmt = std::sin (irCurMix * 1.5707963f);
        const float dryAmt = std::cos (irCurMix * 1.5707963f);
        outL[i] = (outL[i] * dryAmt + wl * wetAmt) * irCurOut;
        outR[i] = (outR[i] * dryAmt + wr * wetAmt) * irCurOut;
    }
}

double MasterSpaceAudioProcessor::getTailLengthSeconds() const
{
    if (get (ParamIDs::mode) > 0.5f)
        return juce::jmin (kMaxIRSeconds, irSeconds.load() + get (ParamIDs::preDelay) * 0.001 + 0.3);

    return engine.getTailSeconds();
}

//==============================================================================
bool MasterSpaceAudioProcessor::isSupportedIRFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff") || path.endsWithIgnoreCase (".flac");
}

bool MasterSpaceAudioProcessor::loadImpulseResponse (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        irDisplayName = "Can't read: " + file.getFileName();
        return false;
    }

    const int channels = (int) juce::jmin ((juce::uint32) 2, reader->numChannels);
    const int length = (int) juce::jmin ((juce::int64) (kMaxIRSeconds * reader->sampleRate), reader->lengthInSamples);

    juce::AudioBuffer<float> buf (channels, length);
    reader->read (&buf, 0, length, 0, true, channels > 1);

    irOriginal = std::move (buf);
    irOriginalRate = reader->sampleRate;
    irDisplayName = file.getFileNameWithoutExtension();
    lastIRFolder = file.getParentDirectory();
    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);

    sendShapedIRToAudioThread();
    return true;
}

juce::String MasterSpaceAudioProcessor::irShapeSignature() const
{
    using namespace ParamIDs;
    return juce::String (get (irLength), 2) + "|" + juce::String (get (erLevel), 2) + "|" + juce::String (get (lateLevel), 2)
         + "|" + juce::String (get (erOn)) + "|" + juce::String (get (lateOn));
}

// Applies Length and the Early / Late levels to the loaded IR and hands the result to the audio thread.
void MasterSpaceAudioProcessor::sendShapedIRToAudioThread()
{
    if (irOriginal.getNumSamples() == 0)
        return;

    using namespace ParamIDs;
    const float lengthPct = juce::jlimit (5.0f, 100.0f, get (irLength));
    const float gEarly = get (erOn) > 0.5f ? mspace::dbToGain (get (erLevel)) : 0.0f;
    const float gLate  = get (lateOn) > 0.5f ? mspace::dbToGain (get (lateLevel)) : 0.0f;

    const int total = irOriginal.getNumSamples();
    const int keep = juce::jmax (64, (int) std::round (total * lengthPct * 0.01f));
    const int numCh = irOriginal.getNumChannels();

    auto pending = std::make_unique<PendingIR>();
    pending->buffer.setSize (numCh, keep);
    for (int c = 0; c < numCh; ++c)
        pending->buffer.copyFrom (c, 0, irOriginal, c, 0, keep);

    // early / late gains with a 10 ms crossfade around the split point
    const int split = (int) (kIRSplitSec * irOriginalRate);
    const int xf = (int) (0.010 * irOriginalRate);
    for (int c = 0; c < numCh; ++c)
    {
        float* d = pending->buffer.getWritePointer (c);
        for (int i = 0; i < keep; ++i)
        {
            float g;
            if (i < split - xf)      g = gEarly;
            else if (i > split + xf) g = gLate;
            else
            {
                const float u = (float) (i - (split - xf)) / (float) (2 * xf);
                g = gEarly + (gLate - gEarly) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u));
            }
            d[i] *= g;
        }
    }

    if (keep < total)
    {
        // raised-cosine fade over the last 30 % of the kept part
        const int fadeLen = juce::jmax (32, (int) (keep * 0.3));
        const int fadeStart = keep - fadeLen;
        for (int c = 0; c < numCh; ++c)
        {
            float* d = pending->buffer.getWritePointer (c);
            for (int i = 0; i < fadeLen; ++i)
                d[fadeStart + i] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeLen));
        }
    }

    // Fixed normalisation from the *original* IR (same formula JUCE uses), so Length / Early / Late
    // change the real level instead of being re-normalised away. Also compensate for resampling to
    // the host rate (more samples of the same amplitude = more energy).
    double maxE = 0.0;
    for (int c = 0; c < numCh; ++c)
    {
        const float* o = irOriginal.getReadPointer (c);
        double e = 0.0;
        for (int i = 0; i < total; ++i) e += (double) o[i] * o[i];
        maxE = juce::jmax (maxE, e);
    }
    const double host = juce::jmax (8000.0, hostRate.load());
    const float norm = maxE > 1e-12 ? (float) (0.125 / std::sqrt (maxE) * std::sqrt (irOriginalRate / host)) : 0.0f;
    pending->buffer.applyGain (norm);

    pending->sampleRate = irOriginalRate;
    pending->stereo = numCh > 1;
    irSeconds = (double) keep / irOriginalRate;
    appliedShape = irShapeSignature();

    std::unique_ptr<PendingIR> toFree;
    {
        const juce::SpinLock::ScopedLockType lock (irLock);
        toFree = std::move (pendingIR);       // replace a not-yet-taken IR
        pendingIR = std::move (pending);
    }
}

void MasterSpaceAudioProcessor::timerCallback()
{
    {
        std::unique_ptr<PendingIR> toFree;
        {
            const juce::SpinLock::ScopedLockType lock (irLock);
            toFree = std::move (retiredIR);
        }
    }

    juce::String path;
    {
        const juce::SpinLock::ScopedLockType lock (requestLock);
        path.swapWith (requestedIRPath);
    }
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        if (f.existsAsFile())
            loadImpulseResponse (f);
        else
            irDisplayName = "Not found: " + f.getFileName();
    }

    // re-shape once Length / Early / Late have settled
    const auto sig = irShapeSignature();
    if (irOriginal.getNumSamples() > 0 && sig != appliedShape)
    {
        if (sig == lastSeenShape)
            sendShapedIRToAudioThread();
        lastSeenShape = sig;
    }
}

//==============================================================================
void MasterSpaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterSpaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto path = apvts.state.getProperty ("irPath").toString();
            if (path.isNotEmpty())
            {
                const juce::SpinLock::ScopedLockType lock (requestLock);
                requestedIRPath = path;
            }
        }
    }
}

juce::AudioProcessorEditor* MasterSpaceAudioProcessor::createEditor()
{
    return new MasterSpaceEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterSpaceAudioProcessor();
}
