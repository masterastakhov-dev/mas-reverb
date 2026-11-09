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
    setColour (juce::TextButton::textColourOffId, MASColours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
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
    const float dim = slider.isEnabled() ? 1.0f : 0.28f;

    // outer track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (MASColours::track.withMultipliedAlpha (dim));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    juce::Path value;
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (MASColours::accent.withAlpha ((hover ? 1.0f : 0.9f) * dim));
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float bodyR = arcR - 9.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d46).withMultipliedAlpha (dim), c.x, c.y - bodyR,
                                             juce::Colour (0xff1d1f25).withMultipliedAlpha (dim), c.x, c.y + bodyR, false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (body, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (body.reduced (1.5f), 1.0f);

    // pointer
    const float p1 = bodyR * 0.35f, p2 = bodyR * 0.88f;
    const juce::Point<float> a (c.x + p1 * std::sin (angle), c.y - p1 * std::cos (angle));
    const juce::Point<float> b (c.x + p2 * std::sin (angle), c.y - p2 * std::cos (angle));
    g.setColour (MASColours::text.withMultipliedAlpha (dim));
    g.drawLine ({ a, b }, 2.5f);
}

void MASLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                           bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? MASColours::accent : MASColours::track;
    if (highlighted && ! on) fill = fill.brighter (0.15f);
    if (down) fill = fill.darker (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.0f : 0.07f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font MASLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return uiFont (12.0f, true).withExtraKerningFactor (0.1f);
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
    addKnob (irLength, ParamIDs::irLength, "LENGTH");
    addKnob (irCut,    ParamIDs::irCut,    "HIGH CUT");

    for (auto* b : { &hallButton, &irButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    hallButton.onClick = [this] { setMode (false); };
    irButton.onClick   = [this] { setMode (true); };
    hallButton.setTooltip ("Algorithmic hall");
    irButton.setTooltip ("Convolution with your own impulse response");

    loadButton.onClick = [this] { chooseIRFile(); };
    addAndMakeVisible (loadButton);

    irName.setJustificationType (juce::Justification::centredRight);
    irName.setFont (uiFont (12.5f));
    irName.setColour (juce::Label::textColourId, MASColours::textDim);
    irName.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (irName);

    setSize (baseWidth, baseHeight);
    updateModeUI();
    startTimerHz (15);
}

MASPanel::~MASPanel()
{
    stopTimer();
    for (auto* k : { &preDelay, &size, &decay, &hfDamp, &lowCut, &mod, &mix, &irLength, &irCut })
        k->slider.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MASPanel::setMode (bool irMode)
{
    if (auto* param = processor.apvts.getParameter (ParamIDs::mode))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (irMode ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
    updateModeUI();
}

void MASPanel::updateModeUI()
{
    const bool ir = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    shownIRMode = ir;

    hallButton.setToggleState (! ir, juce::dontSendNotification);
    irButton.setToggleState (ir, juce::dontSendNotification);

    // the IR controls share columns with the algorithmic ones
    decay.slider.setVisible (! ir);   decay.label.setVisible (! ir);
    hfDamp.slider.setVisible (! ir);  hfDamp.label.setVisible (! ir);
    irLength.slider.setVisible (ir);  irLength.label.setVisible (ir);
    irCut.slider.setVisible (ir);     irCut.label.setVisible (ir);

    for (auto* k : { &size, &mod })
    {
        k->slider.setEnabled (! ir);
        k->label.setAlpha (ir ? 0.35f : 1.0f);
    }

    loadButton.setVisible (ir);

    shownIRName = processor.getIRDisplayName();
    if (! ir)
        irName.setText ("ALGORITHMIC HALL  /  v" JucePlugin_VersionString, juce::dontSendNotification);
    else if (shownIRName.isEmpty())
        irName.setText ("Drop a WAV / AIFF / FLAC here", juce::dontSendNotification);
    else
        irName.setText ("IR: " + shownIRName, juce::dontSendNotification);

    irName.setColour (juce::Label::textColourId, ir && shownIRName.isNotEmpty() ? MASColours::text : MASColours::textDim);
    resized();
}

void MASPanel::timerCallback()
{
    const bool ir = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    if (ir != shownIRMode || processor.getIRDisplayName() != shownIRName)
        updateModeUI();
}

void MASPanel::chooseIRFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an impulse response", processor.getLastIRFolder(),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  loadIRFile (f);
                          });
}

void MASPanel::loadIRFile (const juce::File& f)
{
    if (processor.loadImpulseResponse (f))
        setMode (true);
    updateModeUI();
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
    Knob* columns[7][2] = { { &preDelay, nullptr }, { &size, nullptr }, { &decay, &irLength },
                            { &hfDamp, &irCut }, { &lowCut, nullptr }, { &mod, nullptr }, { &mix, nullptr } };
    const int colW = area.getWidth() / 7;

    for (auto& column : columns)
    {
        auto col = area.removeFromLeft (colW);
        const auto labelArea = col.removeFromTop (22);
        const auto knobArea = col.reduced (6, 2);
        for (auto* k : column)
            if (k != nullptr)
            {
                k->label.setBounds (labelArea);
                k->slider.setBounds (knobArea);
            }
    }

    // header, right side: mode switch, IR loader / info line
    const int right = baseWidth - 32;
    irButton.setBounds (right - 64, 20, 64, 26);
    hallButton.setBounds (right - 64 - 70, 20, 66, 26);
    loadButton.setBounds (right - 92, 56, 92, 24);
    irName.setBounds (shownIRMode ? juce::Rectangle<int> (right - 92 - 330, 56, 322, 24)
                                  : juce::Rectangle<int> (right - 330, 56, 330, 24));
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

void MasterVerbAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;

    const auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (MASColours::accent.withAlpha (0.12f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (MASColours::accent);
    g.drawRoundedRectangle (r, 12.0f, 2.0f);
    g.setColour (MASColours::text);
    g.setFont (juce::Font (juce::FontOptions ((float) getHeight() * 0.07f, juce::Font::bold)));
    g.drawText ("DROP IMPULSE RESPONSE", getLocalBounds(), juce::Justification::centred);
}

bool MasterVerbAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (MasterVerbAudioProcessor::isSupportedIRFile (f))
            return true;
    return false;
}

void MasterVerbAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (const auto& f : files)
        if (MasterVerbAudioProcessor::isSupportedIRFile (f))
        {
            panel.loadIRFile (juce::File (f));
            break;
        }
}

void MasterVerbAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MASPanel::baseWidth;
    panel.setBounds (0, 0, MASPanel::baseWidth, MASPanel::baseHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));

    if (sizeRestored)
        processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);   // remembered with the project
}
