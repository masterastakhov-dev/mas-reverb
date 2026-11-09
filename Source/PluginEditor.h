#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace MASColours
{
    const juce::Colour bgTop    { 0xff1c1e24 };
    const juce::Colour bgBottom { 0xff0e0f12 };
    const juce::Colour panel    { 0xff17191e };
    const juce::Colour track    { 0xff2c2f37 };
    const juce::Colour accent   { 0xffe2a04a };
    const juce::Colour text     { 0xffe9e6df };
    const juce::Colour textDim  { 0xff8b8d93 };
}

class MASLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MASLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class MASReverbAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MASReverbAudioProcessorEditor (MASReverbAudioProcessor&);
    ~MASReverbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const char* paramID, const juce::String& title);

    MASReverbAudioProcessor& processor;
    MASLookAndFeel lnf;
    Knob preDelay, size, decay, hfDamp, lowCut, mod, mix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MASReverbAudioProcessorEditor)
};
