#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

static juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

juce::AudioProcessorValueTreeState::ParameterLayout MASReverbAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto msText = [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto secText = [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; };
    auto pctText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto hzText = [] (float v, int) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                         : juce::String (v / 1000.0f, 2) + " kHz"; };
    auto lowCutText = [hzText] (float v, int n) { return v <= 20.5f ? juce::String ("Off") : hzText (v, n); };

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

    return { p.begin(), p.end() };
}

MASReverbAudioProcessor::MASReverbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MASReverbState", createLayout())
{
    pPre   = apvts.getRawParameterValue (ParamIDs::preDelay);
    pSize  = apvts.getRawParameterValue (ParamIDs::size);
    pDecay = apvts.getRawParameterValue (ParamIDs::decay);
    pDamp  = apvts.getRawParameterValue (ParamIDs::hfDamp);
    pLow   = apvts.getRawParameterValue (ParamIDs::lowCut);
    pMod   = apvts.getRawParameterValue (ParamIDs::mod);
    pMix   = apvts.getRawParameterValue (ParamIDs::mix);
}

mas::ReverbParams MASReverbAudioProcessor::readParams() const
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

void MASReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock));
}

void MASReverbAudioProcessor::reset()
{
    engine.reset();
}

bool MASReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();

    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();

    return false;
}

void MASReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    engine.setParams (readParams());

    if (numOut >= 2)
    {
        if (numIn == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in -> stereo out

        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    }
    else
    {
        // mono -> mono: run the stereo engine, keep the left output
        if (monoScratch.getNumSamples() < n)
            monoScratch.setSize (1, n, false, false, true);

        monoScratch.copyFrom (0, 0, buffer, 0, 0, n);
        engine.process (buffer.getWritePointer (0), monoScratch.getWritePointer (0), n);
    }

    for (int ch = juce::jmax (2, numIn); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

double MASReverbAudioProcessor::getTailLengthSeconds() const
{
    return juce::jmin (30.0, (double) pDecay->load() * 1.2 + 0.3);
}

void MASReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MASReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MASReverbAudioProcessor::createEditor()
{
    return new MASReverbAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MASReverbAudioProcessor();
}
