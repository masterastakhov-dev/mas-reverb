#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr int kHeader  = 104;
    constexpr int kMinWidth = 602;    // 70 %
    constexpr int kMaxWidth = 1720;   // 200 %

    juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
MASLookAndFeel::MASLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, MASColours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, MASColours::accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, MASColours::text);
    setColour (juce::Label::textWhenEditingColourId, MASColours::text);
    setColour (juce::TextEditor::textColourId, MASColours::text);
    setColour (juce::TextEditor::backgroundColourId, MASColours::panel);
    setColour (juce::TextEditor::highlightColourId, MASColours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, MASColours::accent);
}

void MASLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float arcR = radius - 3.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool hover = slider.isMouseOverOrDragging();

    // outer track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (MASColours::track);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path value;
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (MASColours::accent.withAlpha (hover ? 1.0f : 0.9f));
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float bodyR = arcR - 9.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d46), c.x, c.y - bodyR,
                                             juce::Colour (0xff1d1f25), c.x, c.y + bodyR, false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (body, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (body.reduced (1.5f), 1.0f);

    // pointer
    const float p1 = bodyR * 0.35f, p2 = bodyR * 0.88f;
    const juce::Point<float> a (c.x + p1 * std::sin (angle), c.y - p1 * std::cos (angle));
    const juce::Point<float> b (c.x + p2 * std::sin (angle), c.y - p2 * std::cos (angle));
    g.setColour (MASColours::text);
    g.drawLine ({ a, b }, 2.5f);
}

juce::Label* MASLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (uiFont (14.0f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

//==============================================================================
MASPanel::MASPanel (MasterVerbAudioProcessor& p, MASLookAndFeel& lf)
    : processor (p), lnf (lf)
{
    setLookAndFeel (&lnf);

    emblem   = juce::ImageCache::getFromMemory (BinaryData::emblem_png,   BinaryData::emblem_pngSize);
    wordmark = juce::ImageCache::getFromMemory (BinaryData::wordmark_png, BinaryData::wordmark_pngSize);

    addKnob (preDelay, ParamIDs::preDelay, "PRE-DELAY");
    addKnob (size,     ParamIDs::size,     "SIZE");
    addKnob (decay,    ParamIDs::decay,    "DECAY");
    addKnob (hfDamp,   ParamIDs::hfDamp,   "HF DAMPING");
    addKnob (lowCut,   ParamIDs::lowCut,   "LOW CUT");
    addKnob (mod,      ParamIDs::mod,      "MOD");
    addKnob (mix,      ParamIDs::mix,      "MIX");

    setSize (baseWidth, baseHeight);
}

MASPanel::~MASPanel()
{
    for (auto* k : { &preDelay, &size, &decay, &hfDamp, &lowCut, &mod, &mix })
        k->slider.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MASPanel::addKnob (Knob& k, const char* paramID, const juce::String& title)
{
    k.slider.setLookAndFeel (&lnf);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, MASColours::text);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 22);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setMouseDragSensitivity (220);
    k.slider.setVelocityBasedMode (false);
    addAndMakeVisible (k.slider);

    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (uiFont (12.5f, true).withExtraKerningFactor (0.12f));
    k.label.setColour (juce::Label::textColourId, MASColours::textDim);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramID, k.slider);

    if (auto* param = processor.apvts.getParameter (paramID))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void MASPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (MASColours::bgTop, 0.0f, 0.0f,
                                             MASColours::bgBottom, 0.0f, area.getHeight(), false));
    g.fillAll();

    // header: brand emblem, title, wordmark
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const float emH = 78.0f;
    const float emW = emblem.isValid() ? emH * (float) emblem.getWidth() / (float) emblem.getHeight() : 0.0f;
    if (emblem.isValid())
        g.drawImage (emblem, juce::Rectangle<float> (26.0f, 12.0f, emW, emH), juce::RectanglePlacement::centred);

    const int tx = (int) (26.0f + emW + 18.0f);
    g.setColour (MASColours::text);
    g.setFont (uiFont (31.0f, true).withExtraKerningFactor (0.16f));
    g.drawText (JucePlugin_Name, tx, 20, 460, 38, juce::Justification::centredLeft);

    if (wordmark.isValid())
    {
        const float wmH = 13.0f;
        const float wmW = wmH * (float) wordmark.getWidth() / (float) wordmark.getHeight();
        g.drawImage (wordmark, juce::Rectangle<float> ((float) tx + 2.0f, 62.0f, wmW, wmH), juce::RectanglePlacement::centred);
    }

    g.setColour (MASColours::textDim);
    g.setFont (uiFont (12.0f).withExtraKerningFactor (0.06f));
    g.drawText ("ALGORITHMIC HALL  /  v" JucePlugin_VersionString, getWidth() - 332, 34, 300, 18,
                juce::Justification::centredRight);

    // divider
    g.setColour (MASColours::accent.withAlpha (0.45f));
    g.fillRect (32.0f, (float) kHeader - 6.0f, area.getWidth() - 64.0f, 1.0f);

    // knob panel
    const auto panel = juce::Rectangle<float> (20.0f, (float) kHeader + 6.0f,
                                               area.getWidth() - 40.0f, area.getHeight() - kHeader - 26.0f);
    g.setColour (MASColours::panel.withAlpha (0.7f));
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (panel, 10.0f, 1.0f);
}

void MASPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (kHeader + 14).withTrimmedBottom (26).reduced (28, 0);
    Knob* knobs[] = { &preDelay, &size, &decay, &hfDamp, &lowCut, &mod, &mix };
    const int colW = area.getWidth() / 7;

    for (auto* k : knobs)
    {
        auto col = area.removeFromLeft (colW);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col.reduced (6, 2));
    }
}

//==============================================================================
MasterVerbAudioProcessorEditor::MasterVerbAudioProcessorEditor (MasterVerbAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p, lnf)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);   // before setResizable, so the corner grip stays on top

    // read the remembered size first: setResizeLimits() below triggers resized()
    const int savedWidth = (int) processor.apvts.state.getProperty ("uiWidth", MASPanel::baseWidth);

    const double ratio = (double) MASPanel::baseWidth / (double) MASPanel::baseHeight;
    setResizable (true, true);
    setResizeLimits (kMinWidth, juce::roundToInt (kMinWidth / ratio),
                     kMaxWidth, juce::roundToInt (kMaxWidth / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);

    const int w = juce::jlimit (kMinWidth, kMaxWidth, savedWidth);
    setSize (w, juce::roundToInt (w / ratio));
    sizeRestored = true;
}

MasterVerbAudioProcessorEditor::~MasterVerbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MasterVerbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (MASColours::bgBottom);
}

void MasterVerbAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MASPanel::baseWidth;
    panel.setBounds (0, 0, MASPanel::baseWidth, MASPanel::baseHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));

    if (sizeRestored)
        processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);   // remembered with the project
}
