#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace MASColours
{
    const juce::Colour bgTop    { 0xff1c1e24 };
    const juce::Colour bgBottom { 0xff0e0f12 };
    const juce::Colour panel    { 0xff17191e };
    const juce::Colour track    { 0xff2c2f37 };
    const juce::Colour accent   { 0xffe33f2b };   // brand red
    const juce::Colour accent2  { 0xfff6b45c };   // brand orange
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
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

// All controls, drawn at a fixed base size; the editor scales it to the window.
class MASPanel : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int baseWidth  = 860;
    static constexpr int baseHeight = 312;

    MASPanel (MasterVerbAudioProcessor&, MASLookAndFeel&);
    ~MASPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void loadIRFile (const juce::File&);

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const char* paramID, const juce::String& title);
    void timerCallback() override;
    void setMode (bool irMode);
    void updateModeUI();
    void chooseIRFile();

    MasterVerbAudioProcessor& processor;
    MASLookAndFeel& lnf;
    juce::Image emblem, wordmark;
    Knob preDelay, size, decay, hfDamp, lowCut, mod, mix, irLength, irCut;

    juce::TextButton hallButton { "HALL" }, irButton { "IR" }, loadButton { "LOAD IR" };
    juce::Label irName;
    std::unique_ptr<juce::FileChooser> chooser;
    bool shownIRMode = false;
    juce::String shownIRName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MASPanel)
};

class MasterVerbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::FileDragAndDropTarget
{
public:
    explicit MasterVerbAudioProcessorEditor (MasterVerbAudioProcessor&);
    ~MasterVerbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    MasterVerbAudioProcessor& processor;
    MASLookAndFeel lnf;
    MASPanel panel;
    bool sizeRestored = false;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterVerbAudioProcessorEditor)
};
