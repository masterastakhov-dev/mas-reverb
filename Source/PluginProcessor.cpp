#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
    constexpr double kMaxIRSeconds = 30.0;
    constexpr float  kIRWetTrim    = 3.2f;    // JUCE normalises IRs to -18 dB energy; bring wet near the algorithmic level
    constexpr double kModeFadeSec  = 0.03;

    juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterVerbAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto msText = [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto secText = [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; };
    auto pctText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto hzText = [] (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                         : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; };
    auto lowCutText = [hzText] (float v, int n) { return v <= 20.5f ? juce::String ("Off") : hzText (v, n); };
    auto highCutText = [hzText] (float v, int n) { return v >= 19900.0f ? juce::String ("Off") : hzText (v, n); };

    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::preDelay, 1 }, "Pre-Delay",
                                        skewedRange (0.0f, 250.0f, 40.0f), 20.0f,
                                        Attr().withLabel ("ms").withStringFromValueFunction (msText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::size, 1 }, "Size",
                                        juce::NormalisableRange<float> (0.0f, 100.0f), 55.0f,
                                        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::decay, 1 }, "Decay",
                                        skewedRange (0.2f, 20.0f, 2.5f), 2.4f,
                                        Attr().withLabel ("s").withStringFromValueFunction (secText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::hfDamp, 1 }, "HF Damping",
                                        juce::NormalisableRange<float> (0.0f, 100.0f), 35.0f,
                                        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::lowCut, 1 }, "Low Cut",
                                        skewedRange (20.0f, 1000.0f, 150.0f), 90.0f,
                                        Attr().withLabel ("Hz").withStringFromValueFunction (lowCutText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::mod, 1 }, "Mod",
                                        juce::NormalisableRange<float> (0.0f, 100.0f), 40.0f,
                                        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::mix, 1 }, "Mix",
                                        juce::NormalisableRange<float> (0.0f, 100.0f), 30.0f,
                                        Attr().withLabel ("%").withStringFromValueFunction (pctText)));

    // version hint 2: added in 1.3.0
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::mode, 2 }, "Mode",
                                                               juce::StringArray { "Hall", "IR" }, 0));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::irLength, 2 }, "IR Length",
                                        juce::NormalisableRange<float> (5.0f, 100.0f), 100.0f,
                                        Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ParamIDs::irCut, 2 }, "IR High Cut",
                                        skewedRange (1000.0f, 20000.0f, 6000.0f), 20000.0f,
                                        Attr().withLabel ("Hz").withStringFromValueFunction (highCutText)));

    return { p.begin(), p.end() };
}

MasterVerbAudioProcessor::MasterVerbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MasterVerbState", createLayout())
{
    pPre   = apvts.getRawParameterValue (ParamIDs::preDelay);
    pSize  = apvts.getRawParameterValue (ParamIDs::size);
    pDecay = apvts.getRawParameterValue (ParamIDs::decay);
    pDamp  = apvts.getRawParameterValue (ParamIDs::hfDamp);
    pLow   = apvts.getRawParameterValue (ParamIDs::lowCut);
    pMod   = apvts.getRawParameterValue (ParamIDs::mod);
    pMix   = apvts.getRawParameterValue (ParamIDs::mix);
    pMode  = apvts.getRawParameterValue (ParamIDs::mode);
    pIRLen = apvts.getRawParameterValue (ParamIDs::irLength);
    pIRCut = apvts.getRawParameterValue (ParamIDs::irCut);

    lastIRFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    startTimerHz (10);
}

MasterVerbAudioProcessor::~MasterVerbAudioProcessor()
{
    stopTimer();
}

mas::ReverbParams MasterVerbAudioProcessor::readParams() const
{
    mas::ReverbParams p;
    p.preDelayMs = pPre->load();
    p.size       = pSize->load() * 0.01f;
    p.decaySec   = pDecay->load();
    p.hfDamp     = pDamp->load() * 0.01f;
    p.lowCutHz   = pLow->load();
    p.mod        = pMod->load() * 0.01f;
    p.mix        = pMix->load() * 0.01f;
    return p;
}

//==============================================================================
void MasterVerbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    chunkSize = juce::jmax (64, samplesPerBlock);

    engine.setParams (readParams());
    engine.prepare (sampleRate);

    convolver.prepare ({ sampleRate, (juce::uint32) chunkSize, 2 });
    for (auto& d : irPreDelay) d.allocate ((int) std::ceil (0.26 * sampleRate) + 8);
    irDry.setSize (2, chunkSize);
    irWet.setSize (2, chunkSize);
    monoScratch.setSize (1, chunkSize);

    irCurLowCut = pLow->load();
    irCurHighCut = pIRCut->load();
    for (auto& f : irLowCut)  f.set (irCurLowCut, sampleRate);
    for (auto& f : irHighCut) f.set (irCurHighCut, sampleRate);
    irCurPre = pPre->load() * 0.001f * (float) sampleRate;
    irCurMix = pMix->load() * 0.01f;

    modeGain = pMode->load() > 0.5f ? 1.0f : 0.0f;
    algoRunning = modeGain < 1.0f;
    irRunning = modeGain > 0.0f;
    resetIRPath();
}

void MasterVerbAudioProcessor::reset()
{
    engine.reset();
    resetIRPath();
}

void MasterVerbAudioProcessor::resetIRPath()
{
    convolver.reset();
    for (int c = 0; c < 2; ++c)
    {
        irPreDelay[c].clear();
        irLowCut[c].clear();
        irHighCut[c].clear();
    }
}

bool MasterVerbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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
void MasterVerbAudioProcessor::takePendingIR() noexcept
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
                                       juce::dsp::Convolution::Normalise::yes);
        irActiveOnAudio = true;
    }
    else
    {
        irActiveOnAudio = false;
    }

    retiredIR = std::move (pendingIR);   // freed later on the message thread
}

void MasterVerbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

void MasterVerbAudioProcessor::processStereo (float* left, float* right, int n)
{
    const float target = pMode->load() > 0.5f ? 1.0f : 0.0f;
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
void MasterVerbAudioProcessor::processIRPath (int n)
{
    const double fs = sampleRateHz;

    // filters: smooth cutoffs across chunks
    const double lc = juce::jlimit (20.0, 1000.0, (double) pLow->load());
    const double hc = juce::jlimit (1000.0, 20000.0, (double) pIRCut->load());
    irCurLowCut  += 0.3 * (lc - irCurLowCut);
    irCurHighCut += 0.3 * (hc - irCurHighCut);
    for (auto& f : irLowCut)  f.set (irCurLowCut, fs);
    const bool highCutOn = irCurHighCut < 19900.0;
    if (highCutOn)
        for (auto& f : irHighCut) f.set (irCurHighCut, fs);

    const float sSlow = (float) (1.0 - std::exp (-1.0 / (0.060 * fs)));
    const float sFast = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
    const float targetPre = juce::jlimit (0.0f, 250.0f, pPre->load()) * 0.001f * (float) fs;
    const float targetMix = juce::jlimit (0.0f, 1.0f, pMix->load() * 0.01f);
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

    // high cut + equal-power mix
    float mix = irCurMix;
    float* outL = irDry.getWritePointer (0);
    float* outR = irDry.getWritePointer (1);
    float* wetL = irWet.getWritePointer (0);
    float* wetR = irWet.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        mix += sFast * (targetMix - mix);

        float wl = haveIR ? wetL[i] * kIRWetTrim : 0.0f;
        float wr = haveIR ? wetR[i] * kIRWetTrim : 0.0f;
        if (highCutOn) { wl = irHighCut[0].process (wl); wr = irHighCut[1].process (wr); }

        const float wetAmt = std::sin (mix * 1.5707963f);
        const float dryAmt = std::cos (mix * 1.5707963f);
        outL[i] = outL[i] * dryAmt + wl * wetAmt;
        outR[i] = outR[i] * dryAmt + wr * wetAmt;
    }
    irCurMix = mix;
}

double MasterVerbAudioProcessor::getTailLengthSeconds() const
{
    if (pMode->load() > 0.5f)
        return juce::jmin (kMaxIRSeconds, irSeconds.load() + 0.3);

    return juce::jmin (30.0, (double) pDecay->load() * 1.2 + 0.3);
}

//==============================================================================
bool MasterVerbAudioProcessor::isSupportedIRFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff") || path.endsWithIgnoreCase (".flac");
}

bool MasterVerbAudioProcessor::loadImpulseResponse (const juce::File& file)
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

    appliedIRLength = -1.0f;      // force a fresh send
    sendShapedIRToAudioThread();
    return true;
}

// Apply the Length control (fade + truncate) and hand the result to the audio thread.
void MasterVerbAudioProcessor::sendShapedIRToAudioThread()
{
    if (irOriginal.getNumSamples() == 0)
        return;

    const float lengthPct = juce::jlimit (5.0f, 100.0f, pIRLen->load());
    const int total = irOriginal.getNumSamples();
    const int keep = juce::jmax (64, (int) std::round (total * lengthPct * 0.01f));

    auto pending = std::make_unique<PendingIR>();
    pending->buffer.setSize (irOriginal.getNumChannels(), keep);
    for (int c = 0; c < irOriginal.getNumChannels(); ++c)
        pending->buffer.copyFrom (c, 0, irOriginal, c, 0, keep);

    if (keep < total)
    {
        // raised-cosine fade over the last 30 % of the kept part
        const int fadeLen = juce::jmax (32, (int) (keep * 0.3));
        const int fadeStart = keep - fadeLen;
        for (int c = 0; c < pending->buffer.getNumChannels(); ++c)
        {
            float* d = pending->buffer.getWritePointer (c);
            for (int i = 0; i < fadeLen; ++i)
                d[fadeStart + i] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeLen));
        }
    }

    pending->sampleRate = irOriginalRate;
    pending->stereo = pending->buffer.getNumChannels() > 1;
    irSeconds = (double) keep / irOriginalRate;
    appliedIRLength = lengthPct;

    std::unique_ptr<PendingIR> toFree;
    {
        const juce::SpinLock::ScopedLockType lock (irLock);
        toFree = std::move (pendingIR);       // replace a not-yet-taken IR
        pendingIR = std::move (pending);
    }
    // toFree is released here, on the message thread
}

void MasterVerbAudioProcessor::timerCallback()
{
    // free IRs already consumed by the audio thread
    {
        std::unique_ptr<PendingIR> toFree;
        {
            const juce::SpinLock::ScopedLockType lock (irLock);
            toFree = std::move (retiredIR);
        }
    }

    // IR requested by a restored project
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

    // Length knob: re-shape once it has settled
    const float len = pIRLen->load();
    if (irOriginal.getNumSamples() > 0 && ! juce::exactlyEqual (len, appliedIRLength))
    {
        if (juce::exactlyEqual (len, lastSeenIRLength))
            sendShapedIRToAudioThread();
        lastSeenIRLength = len;
    }
}

//==============================================================================
void MasterVerbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterVerbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* MasterVerbAudioProcessor::createEditor()
{
    return new MasterVerbAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterVerbAudioProcessor();
}
