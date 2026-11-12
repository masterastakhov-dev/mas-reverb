#include "PluginEditor.h"
#include "PresetPanel.h"
#include "BinaryData.h"

namespace
{
    constexpr int kHeader   = 100;
    constexpr int kBarY     = 106;    // preset bar
    constexpr int kBarH     = 30;
    constexpr int kTabsY    = 146;
    constexpr int kTabsH    = 30;
    constexpr int kPageY    = 184;
   #if defined (MAS_RI)
    constexpr int kAtmosPage = 8;
    constexpr int kDuckerPage = 9;
    constexpr int kGuardPage = 10;
    constexpr int kColorPage = 11;
   #if defined (MAS_EQ)
    constexpr int kEqPage = 12;
    constexpr int kPresetsPage = 13;
   #else
    constexpr int kPresetsPage = 12;
   #endif
   #elif defined (MAS_ATMOS)
    constexpr int kAtmosPage = 8;
    constexpr int kPresetsPage = 9;
   #else
    constexpr int kPresetsPage = 8;
   #endif
    constexpr int kMinWidth = 700;    // 70 %
    constexpr int kMaxWidth = 2000;   // 200 %

    juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Colour materialColour (int m)
    {
        static const juce::Colour c[] = { juce::Colour (0xff5a5f6b), juce::Colour (0xff9466d6), juce::Colour (0xffc98a4b),
                                          juce::Colour (0xffd9d4ca), juce::Colour (0xff9aa0a8), juce::Colour (0xff62c9e8) };
        return c[juce::jlimit (0, 5, m)];
    }

    void setParamPlain (juce::AudioProcessorValueTreeState& apvts, const char* id, float plain)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
            prm->endChangeGesture();
        }
    }

    void styleKnob (juce::Slider& s, MASLookAndFeel& lnf, int textBoxW, int textBoxH)
    {
        s.setLookAndFeel (&lnf);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, MASColours::text);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);   // editable: click to type a value
        s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        s.setMouseDragSensitivity (220);
    }

    void setDefaultOnDoubleClick (juce::Slider& s, juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        if (auto* prm = apvts.getParameter (id))
            s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }

    void styleLabel (juce::Label& l, const juce::String& text, float size = 12.0f)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (uiFont (size, true).withExtraKerningFactor (0.12f));
        l.setColour (juce::Label::textColourId, MASColours::textDim);
        l.setInterceptsMouseClicks (false, false);
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
    setColour (juce::TextEditor::focusedOutlineColourId, MASColours::accent);
    setColour (juce::CaretComponent::caretColourId, MASColours::accent);
    setColour (juce::TextButton::textColourOffId, MASColours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, MASColours::track);
    setColour (juce::ComboBox::textColourId, MASColours::text);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.08f));
    setColour (juce::ComboBox::arrowColourId, MASColours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff202329));
    setColour (juce::PopupMenu::textColourId, MASColours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, MASColours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ScrollBar::thumbColourId, MASColours::textDim.withAlpha (0.5f));
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::backgroundColourId, MASColours::panel);
}

void MASLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float stroke = juce::jlimit (3.0f, 7.0f, radius * 0.09f);
    const float arcR = radius - stroke * 0.5f - 1.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool hover = slider.isMouseOverOrDragging();
    const float dim = slider.isEnabled() ? 1.0f : 0.28f;
    const bool big = slider.getProperties().getWithDefault ("big", false);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (MASColours::track.withMultipliedAlpha (dim));
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    if (big)
        g.setGradientFill (juce::ColourGradient (MASColours::accent2.withMultipliedAlpha (dim), c.x - arcR, c.y,
                                                 MASColours::accent.withMultipliedAlpha (dim), c.x + arcR, c.y, false));
    else
        g.setColour (MASColours::accent.withAlpha ((hover ? 1.0f : 0.9f) * dim));
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float bodyR = arcR - stroke - 4.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d46).withMultipliedAlpha (dim), c.x, c.y - bodyR,
                                             juce::Colour (0xff1d1f25).withMultipliedAlpha (dim), c.x, c.y + bodyR, false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.6f * dim));
    g.drawEllipse (body, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f * dim));
    g.drawEllipse (body.reduced (1.5f), 1.0f);

    const float p1 = bodyR * 0.35f, p2 = bodyR * 0.88f;
    const juce::Point<float> a (c.x + p1 * std::sin (angle), c.y - p1 * std::cos (angle));
    const juce::Point<float> b (c.x + p2 * std::sin (angle), c.y - p2 * std::cos (angle));
    g.setColour (MASColours::text.withMultipliedAlpha (dim));
    g.drawLine ({ a, b }, juce::jlimit (2.0f, 4.0f, bodyR * 0.07f));
}

juce::Label* MASLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    const bool big = slider.getProperties().getWithDefault ("big", false);
    l->setFont (uiFont (big ? 17.0f : 13.0f, big));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void MASLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                           bool highlighted, bool down)
{
    if (b.getProperties().getWithDefault ("presetName", false))
        return;   // drawn by the preset bar

    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float dim = b.isEnabled() ? 1.0f : 0.35f;

    if (b.getProperties().getWithDefault ("tab", false))
    {
        if (highlighted && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        if (on)
        {
            g.setColour (MASColours::accent);
            g.fillRect (r.withTop (r.getBottom() - 3.0f).reduced (8.0f, 0.0f));
        }
        return;
    }

    auto fill = on ? MASColours::accent : MASColours::track;
    if (highlighted && ! on) fill = fill.brighter (0.15f);
    if (down) fill = fill.darker (0.2f);
    g.setColour (fill.withMultipliedAlpha (dim));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha ((on ? 0.0f : 0.07f) * dim));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void MASLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    if (b.getProperties().getWithDefault ("presetName", false))
        return;
    const bool tab = b.getProperties().getWithDefault ("tab", false);
    const bool on = b.getToggleState();
    const float dim = b.isEnabled() ? 1.0f : 0.35f;
    const juce::String text = b.getProperties().getWithDefault ("onoff", false) ? juce::String (on ? "ON" : "OFF") : b.getButtonText();
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour ((tab ? (on ? MASColours::text : MASColours::textDim) : (on ? juce::Colours::white : MASColours::text)).withMultipliedAlpha (dim));
    g.drawFittedText (text, b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

juce::Font MASLookAndFeel::getTextButtonFont (juce::TextButton& b, int)
{
    const bool tab = b.getProperties().getWithDefault ("tab", false);
    return uiFont (tab ? 12.5f : 11.5f, true).withExtraKerningFactor (0.1f);
}

void MASLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float dim = box.isEnabled() ? 1.0f : 0.35f;
    g.setColour (MASColours::track.withMultipliedAlpha (dim));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f * dim));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (MASColours::textDim.withMultipliedAlpha (dim));
    g.fillPath (arrow);
}

juce::Font MASLookAndFeel::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f); }
juce::Font MASLookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

//==============================================================================
RoomView::RoomView (MasterSpaceAudioProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
}

RoomView::Mapping RoomView::mapping (const mspace::Params& p) const
{
    Mapping m;
    m.g = mspace::computeGeometry (p, p.depth);
    // top strip: room-type menu + FRONT label, bottom strip: dimensions
    const auto area = getLocalBounds().toFloat().withTrimmedTop (64.0f).withTrimmedBottom (30.0f).reduced (24.0f, 0.0f);
    m.scale = juce::jmin (area.getWidth() / m.g.W, area.getHeight() / m.g.D);
    m.room = juce::Rectangle<float> (m.g.W * m.scale, m.g.D * m.scale).withCentre (area.getCentre());
    return m;
}

void RoomView::refreshIfChanged()
{
    const auto p = processor.getParamsSnapshot();
    juce::String key;
    for (float v : { p.roomW, p.roomD, p.roomH, p.size, p.srcX, p.lisX, p.lisY, p.depth })
        key << juce::String (v, 3) << ",";
    for (int mat : p.materials) key << mat;
    key << (processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f ? "ir" : "al");
    if (key != lastKey) { lastKey = key; repaint(); }
}

void RoomView::paint (juce::Graphics& g)
{
    const auto p = processor.getParamsSnapshot();
    const auto m = mapping (p);
    const auto& geo = m.g;
    const bool irMode = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    auto toPx = [&] (float x, float y) { return juce::Point<float> (m.room.getX() + x * m.scale, m.room.getY() + y * m.scale); };

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // floor + 1 m grid (5 m in big rooms)
    g.setColour (juce::Colour (0xff1f2229));
    g.fillRect (m.room);
    const float step = geo.W > 25.0f || geo.D > 25.0f ? 5.0f : 1.0f;
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (float x = step; x < geo.W; x += step) g.drawVerticalLine (juce::roundToInt (m.room.getX() + x * m.scale), m.room.getY(), m.room.getBottom());
    for (float y = step; y < geo.D; y += step) g.drawHorizontalLine (juce::roundToInt (m.room.getY() + y * m.scale), m.room.getX(), m.room.getRight());

    // walls in material colours (front, back, left, right)
    const float t = 4.0f;
    const auto r = m.room;
    g.setColour (materialColour (p.materials[0])); g.fillRect (r.getX() - t, r.getY() - t, r.getWidth() + 2 * t, t);
    g.setColour (materialColour (p.materials[1])); g.fillRect (r.getX() - t, r.getBottom(), r.getWidth() + 2 * t, t);
    g.setColour (materialColour (p.materials[2])); g.fillRect (r.getX() - t, r.getY(), t, r.getHeight());
    g.setColour (materialColour (p.materials[3])); g.fillRect (r.getRight(), r.getY(), t, r.getHeight());

    const auto src = toPx (geo.sx, geo.sy), lis = toPx (geo.lx, geo.ly);

    // first-order reflection paths (walls only; floor / ceiling are listed below)
    if (! irMode)
    {
        struct Img { float x, y; int mat; };
        const Img imgs[4] = { { geo.sx, -geo.sy, p.materials[0] }, { geo.sx, 2.0f * geo.D - geo.sy, p.materials[1] },
                              { -geo.sx, geo.sy, p.materials[2] }, { 2.0f * geo.W - geo.sx, geo.sy, p.materials[3] } };
        for (int i = 0; i < 4; ++i)
        {
            const auto& im = imgs[i];
            float tt;
            if (i < 2) tt = ((i == 0 ? 0.0f : geo.D) - geo.ly) / (im.y - geo.ly);
            else       tt = ((i == 2 ? 0.0f : geo.W) - geo.lx) / (im.x - geo.lx);
            const auto hit = toPx (geo.lx + tt * (im.x - geo.lx), geo.ly + tt * (im.y - geo.ly));
            const auto md = mspace::materialData (im.mat);
            const float a = 0.25f + 0.5f * md.rLow;
            juce::Path path;
            path.startNewSubPath (src);
            path.lineTo (hit);
            path.lineTo (lis);
            g.setColour (materialColour (im.mat).withAlpha (a));
            g.strokePath (path, juce::PathStrokeType (1.4f));
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (hit));
        }
    }

    // direct path + distance
    g.setColour (MASColours::text.withAlpha (0.55f));
    const float dashes[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ src, lis }, dashes, 2, 1.2f);
    g.setFont (uiFont (12.0f, true));
    g.setColour (MASColours::text);
    const auto mid = (src + lis) * 0.5f;
    g.drawText (juce::String (geo.direct, 1) + " m", juce::Rectangle<float> (70.0f, 18.0f).withCentre (mid.translated (34.0f, 0.0f)),
                juce::Justification::centred);

    // listener: head facing the front wall
    g.setColour (MASColours::blue);
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (lis));
    juce::Path nose;
    nose.addTriangle (lis.x - 5.0f, lis.y - 7.0f, lis.x + 5.0f, lis.y - 7.0f, lis.x, lis.y - 15.0f);
    g.fillPath (nose);

    // source
    g.setColour (MASColours::accent.withAlpha (0.25f));
    g.fillEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (src));
    g.setColour (MASColours::accent);
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (src));

    // labels
    g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.12f));
    g.setColour (MASColours::textDim);
    g.drawText ("FRONT", juce::Rectangle<float> (r.getX(), r.getY() - 22.0f, r.getWidth(), 14.0f), juce::Justification::centred);
    g.drawText (juce::String (geo.W, 1) + " x " + juce::String (geo.D, 1) + " x " + juce::String (geo.H, 1) + " m",
                juce::Rectangle<float> (r.getX(), r.getBottom() + 8.0f, r.getWidth(), 16.0f), juce::Justification::centred);
    g.setColour (MASColours::accent);
    g.drawText ("SOURCE", juce::Rectangle<float> (80.0f, 14.0f).withCentre (src.translated (0.0f, -22.0f)), juce::Justification::centred);
    g.setColour (MASColours::blue.brighter (0.3f));
    g.drawText ("LISTENER", juce::Rectangle<float> (80.0f, 14.0f).withCentre (lis.translated (0.0f, 22.0f)), juce::Justification::centred);

    if (irMode)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (MASColours::textDim);
        g.setFont (uiFont (14.0f, true));
        g.drawText ("ROOM IS NOT USED IN IR MODE", getLocalBounds(), juce::Justification::centred);
    }
}

void RoomView::setParam (const char* id, float plainValue)
{
    if (auto* prm = processor.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

void RoomView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = processor.getParamsSnapshot();
    const auto m = mapping (p);
    const auto src = juce::Point<float> (m.room.getX() + m.g.sx * m.scale, m.room.getY() + m.g.sy * m.scale);
    const auto lis = juce::Point<float> (m.room.getX() + m.g.lx * m.scale, m.room.getY() + m.g.ly * m.scale);
    const auto pos = e.position;

    drag = Drag::none;
    if (pos.getDistanceFrom (src) < 22.0f)      drag = Drag::source;
    else if (pos.getDistanceFrom (lis) < 22.0f) drag = Drag::listener;

    const char* ids[] = { ParamIDs::srcX, ParamIDs::depth, ParamIDs::lisX, ParamIDs::lisY };
    const int first = drag == Drag::source ? 0 : 2;
    if (drag != Drag::none)
        for (int i = first; i < first + 2; ++i)
            if (auto* prm = processor.apvts.getParameter (ids[i])) prm->beginChangeGesture();
}

void RoomView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none) return;
    const auto p = processor.getParamsSnapshot();
    const auto m = mapping (p);
    const float xm = (e.position.x - m.room.getX()) / m.scale;
    const float ym = (e.position.y - m.room.getY()) / m.scale;

    if (drag == Drag::source)
    {
        setParam (ParamIDs::srcX, juce::jlimit (0.0f, 100.0f, xm / m.g.W * 100.0f));
        // invert the depth -> distance mapping of mspace::computeGeometry
        const float avail = juce::jmax (0.3f, m.g.ly - 0.4f);
        const float dmin = juce::jmin (0.6f, avail);
        const float dist = juce::jlimit (dmin, avail, m.g.ly - ym);
        const float u = avail > dmin ? (dist - dmin) / (avail - dmin) : 0.0f;
        setParam (ParamIDs::depth, std::pow (u, 1.0f / 1.15f) * 100.0f);
    }
    else
    {
        setParam (ParamIDs::lisX, juce::jlimit (0.0f, 100.0f, xm / m.g.W * 100.0f));
        setParam (ParamIDs::lisY, juce::jlimit (10.0f, 95.0f, ym / m.g.D * 100.0f));
    }
    refreshIfChanged();
}

void RoomView::mouseUp (const juce::MouseEvent&)
{
    const char* ids[] = { ParamIDs::srcX, ParamIDs::depth, ParamIDs::lisX, ParamIDs::lisY };
    const int first = drag == Drag::source ? 0 : 2;
    if (drag != Drag::none)
        for (int i = first; i < first + 2; ++i)
            if (auto* prm = processor.apvts.getParameter (ids[i])) prm->endChangeGesture();
    drag = Drag::none;
}

//==============================================================================
// MAIN page, left block: the big DEPTH control and the locks.
class DepthBlock : public juce::Component
{
public:
    DepthBlock (juce::AudioProcessorValueTreeState& s, MASLookAndFeel& l) : apvts (s), lnf (l)
    {
        depth.getProperties().set ("big", true);
        styleKnob (depth, lnf, 120, 26);
        addAndMakeVisible (depth);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ParamIDs::depth, depth);
        setDefaultOnDoubleClick (depth, apvts, ParamIDs::depth);

        styleLabel (title, "DEPTH", 15.0f);
        title.setColour (juce::Label::textColourId, MASColours::text);
        addAndMakeVisible (title);
        styleLabel (nearFar, "NEAR                    FAR", 10.5f);
        addAndMakeVisible (nearFar);
        styleLabel (lockTitle, "LOCK WHILE MOVING", 10.5f);
        addAndMakeVisible (lockTitle);

        const char* ids[4] = { ParamIDs::lockLevel, ParamIDs::lockTone, ParamIDs::lockDecay, ParamIDs::lockWidth };
        const char* names[4] = { "LEVEL", "TONE", "DECAY", "WIDTH" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = locks.add (new juce::TextButton (names[i]));
            b->setClickingTogglesState (true);
            b->setLookAndFeel (&lnf);
            addAndMakeVisible (b);
            lockAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, ids[i], *b));
        }
    }

    ~DepthBlock() override
    {
        depth.setLookAndFeel (nullptr);
        for (auto* b : locks) b->setLookAndFeel (nullptr);
    }

    void setIRMode (bool ir)
    {
        depth.setEnabled (! ir);
        for (auto* b : locks) b->setEnabled (! ir);
        title.setAlpha (ir ? 0.35f : 1.0f);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect ((float) getWidth() - 1.0f, 10.0f, 1.0f, (float) getHeight() - 20.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 6);
        title.setBounds (r.removeFromTop (24));
        auto knob = r.removeFromTop (juce::jmin (r.getHeight() - 110, r.getWidth() + 20));
        depth.setBounds (knob);
        nearFar.setBounds (knob.getX(), knob.getBottom() - 46, knob.getWidth(), 14);
        r.removeFromTop (8);
        lockTitle.setBounds (r.removeFromTop (18));
        r.removeFromTop (4);
        auto row1 = r.removeFromTop (30), row2 = (r.removeFromTop (6), r.removeFromTop (30));
        const int w = (row1.getWidth() - 8) / 2;
        locks[0]->setBounds (row1.removeFromLeft (w)); row1.removeFromLeft (8); locks[1]->setBounds (row1);
        locks[2]->setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (8); locks[3]->setBounds (row2);
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    MASLookAndFeel& lnf;
    juce::Slider depth;
    juce::Label title, nearFar, lockTitle;
    juce::OwnedArray<juce::TextButton> locks;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAtt;
};

//==============================================================================
ControlPage::ControlPage (juce::AudioProcessorValueTreeState& s, MASLookAndFeel& l, std::vector<std::vector<Spec>> rows,
                          int columns, std::unique_ptr<juce::Component> left, int leftW)
    : apvts (s), lnf (l), numCols (columns), leftBlock (std::move (left)), leftWidth (leftW)
{
    if (leftBlock != nullptr) addAndMakeVisible (*leftBlock);
    numRows = (int) rows.size();

    for (int r = 0; r < (int) rows.size(); ++r)
        for (int c = 0; c < (int) rows[(size_t) r].size(); ++c)
        {
            const auto& spec = rows[(size_t) r][(size_t) c];
            auto cell = std::make_unique<Cell>();
            cell->spec = spec;
            cell->row = r;
            cell->col = c;

            if (spec.kind == Kind::empty) { cells.push_back (std::move (cell)); continue; }

            styleLabel (cell->label, spec.title);
            addAndMakeVisible (cell->label);

            switch (spec.kind)
            {
                case Kind::knob:
                    cell->slider = std::make_unique<juce::Slider>();
                    styleKnob (*cell->slider, lnf, 96, 20);
                    addAndMakeVisible (*cell->slider);
                    cell->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.paramID, *cell->slider);
                    setDefaultOnDoubleClick (*cell->slider, apvts, spec.paramID);
                    if (spec.irParamID != nullptr)
                    {
                        cell->irSlider = std::make_unique<juce::Slider>();
                        styleKnob (*cell->irSlider, lnf, 96, 20);
                        addChildComponent (*cell->irSlider);
                        cell->irAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.irParamID, *cell->irSlider);
                        setDefaultOnDoubleClick (*cell->irSlider, apvts, spec.irParamID);
                        styleLabel (cell->irLabel, spec.irTitle);
                        addChildComponent (cell->irLabel);
                    }
                    break;

                case Kind::choice:
                    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (spec.paramID)))
                    {
                        cell->combo = std::make_unique<juce::ComboBox>();
                        cell->combo->setLookAndFeel (&lnf);
                        cell->combo->addItemList (prm->choices, 1);
                        cell->combo->setJustificationType (juce::Justification::centred);
                        addAndMakeVisible (*cell->combo);
                        cell->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, spec.paramID, *cell->combo);
                    }
                    break;

                case Kind::toggle:
                    cell->toggle = std::make_unique<juce::TextButton> (spec.title);
                    cell->toggle->getProperties().set ("onoff", true);
                    cell->toggle->setClickingTogglesState (true);
                    cell->toggle->setLookAndFeel (&lnf);
                    addAndMakeVisible (*cell->toggle);
                    cell->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, spec.paramID, *cell->toggle);
                    break;

                case Kind::empty: break;
            }
            cells.push_back (std::move (cell));
        }
}

ControlPage::~ControlPage()
{
    for (auto& c : cells)
    {
        if (c->slider) c->slider->setLookAndFeel (nullptr);
        if (c->irSlider) c->irSlider->setLookAndFeel (nullptr);
        if (c->combo) c->combo->setLookAndFeel (nullptr);
        if (c->toggle) c->toggle->setLookAndFeel (nullptr);
    }
}

void ControlPage::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (MASColours::panel.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

#ifdef MAS_RI
void ControlPage::setBottomBlock (std::unique_ptr<juce::Component> block, int height)
{
    bottomBlock = std::move (block);
    bottomHeight = height;
    if (bottomBlock != nullptr) addAndMakeVisible (*bottomBlock);
    resized();
}
#endif

void ControlPage::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
   #ifdef MAS_RI
    if (bottomBlock != nullptr)
        bottomBlock->setBounds (area.removeFromBottom (bottomHeight));
   #endif
    if (leftBlock != nullptr)
        leftBlock->setBounds (area.removeFromLeft (leftWidth));

    const int colW = area.getWidth() / juce::jmax (1, numCols);
    const int rowH = area.getHeight() / juce::jmax (1, numRows);

    for (auto& c : cells)
    {
        const auto cellArea = juce::Rectangle<int> (area.getX() + c->col * colW, area.getY() + c->row * rowH, colW, rowH).reduced (4, 2);
        constexpr int labelH = 20, textH = 22;

        // label + control as one block, centred vertically in the cell
        int controlH = 30;
        if (c->slider)     controlH = juce::jmin (cellArea.getWidth() - 14, cellArea.getHeight() - labelH - textH - 4, 120) + textH;
        else if (c->combo) controlH = 28;
        auto block = cellArea.withSizeKeepingCentre (cellArea.getWidth(), labelH + 6 + controlH);
        const auto labelArea = block.removeFromTop (labelH);
        block.removeFromTop (6);
        c->label.setBounds (labelArea);
        c->irLabel.setBounds (labelArea);

        if (c->slider)
        {
            const auto knob = block.withSizeKeepingCentre (juce::jmin (block.getWidth(), controlH - textH + 30), controlH);
            c->slider->setBounds (knob);
            if (c->irSlider) c->irSlider->setBounds (knob);
        }
        if (c->combo)  c->combo->setBounds (block.withSizeKeepingCentre (juce::jmin (block.getWidth() - 8, 150), 28));
        if (c->toggle) c->toggle->setBounds (block.withSizeKeepingCentre (juce::jmin (block.getWidth() - 16, 96), 30));
    }
}

void ControlPage::setIRMode (bool ir)
{
    for (auto& c : cells)
    {
        if (c->spec.irParamID != nullptr && c->slider)
        {
            c->slider->setVisible (! ir);
            c->label.setVisible (! ir);
            c->irSlider->setVisible (ir);
            c->irLabel.setVisible (ir);
            continue;
        }
        if (! c->spec.algoOnly) continue;
        if (c->slider) c->slider->setEnabled (! ir);
        if (c->combo) c->combo->setEnabled (! ir);
        if (c->toggle) c->toggle->setEnabled (! ir);
        c->label.setAlpha (ir ? 0.35f : 1.0f);
    }
    if (auto* d = dynamic_cast<DepthBlock*> (leftBlock.get()))
        d->setIRMode (ir);
}


#ifdef MAS_RI
//==============================================================================
// 1.4 displays. They only read the values the audio thread publishes (RIMeters); nothing here touches the DSP.
namespace
{
    float rawParam (juce::AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }

    void drawLed (juce::Graphics& g, juce::Rectangle<float> r, float amount, juce::Colour c)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (r);
        if (amount > 0.01f)
        {
            g.setColour (c.withAlpha (0.25f * amount));
            g.fillEllipse (r.expanded (3.0f));
            g.setColour (c.withAlpha (0.35f + 0.65f * amount));
            g.fillEllipse (r.reduced (1.0f));
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (r, 1.0f);
    }

    juce::String qualityText (int q, int latency, double fs)
    {
        if (q == 0) return "LIVE  0 ms";
        return "STUDIO HQ  " + juce::String (1000.0 * latency / juce::jmax (1.0, fs), 1) + " ms";
    }
}

// MAIN page, bottom strip: SMART DUCKER on / amount / activity, RESONANCE GUARD on / activity, quality.
class RIStrip : public juce::Component, private juce::Timer
{
public:
    RIStrip (MasterSpaceAudioProcessor& p, MASLookAndFeel& l) : processor (p), lnf (l)
    {
        for (auto* b : { &duckOn, &guardOn })
        {
            b->getProperties().set ("onoff", true);
            b->setClickingTogglesState (true);
            b->setLookAndFeel (&lnf);
            addAndMakeVisible (b);
        }
        duckAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamIDs::duckOn, duckOn);
        guardAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamIDs::rgOn, guardOn);

        amount.setLookAndFeel (&lnf);
        amount.setSliderStyle (juce::Slider::LinearBar);
        amount.setColour (juce::Slider::trackColourId, MASColours::accent.withAlpha (0.55f));
        amount.setColour (juce::Slider::backgroundColourId, MASColours::track);
        amount.setColour (juce::Slider::textBoxTextColourId, MASColours::text);
        amount.setMouseDragSensitivity (220);
        addAndMakeVisible (amount);
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamIDs::duckAmount, amount);
        setDefaultOnDoubleClick (amount, p.apvts, ParamIDs::duckAmount);
        startTimerHz (30);
    }
    ~RIStrip() override
    {
        stopTimer();
        duckOn.setLookAndFeel (nullptr); guardOn.setLookAndFeel (nullptr); amount.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.12f));
        g.setColour (MASColours::textDim);
        g.drawText ("SMART DUCKER", duckTitle, juce::Justification::centredLeft);
        g.drawText ("RESONANCE GUARD", guardTitle, juce::Justification::centredLeft);
        g.drawText ("AMOUNT", amountTitle, juce::Justification::centredRight);
        g.setColour (MASColours::track);
        g.fillRect (amount.getBounds().toFloat());

        auto& m = processor.riMeters;
        const bool avail = m.engineAvailable.load();
        const float gr = juce::jmax (m.duckEarlyDb.load(), m.duckLateDb.load());
        const float cut = m.rgMaxCutDb.load();
        drawLed (g, duckLed, avail && duckOn.getToggleState() ? gr / 6.0f : 0.0f, MASColours::accent2);
        drawLed (g, guardLed, avail && guardOn.getToggleState() ? cut / 3.0f : 0.0f, MASColours::accent);
        g.setFont (uiFont (12.0f, true));
        g.setColour (MASColours::text);
        g.drawText (avail ? (duckOn.getToggleState() ? (gr < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (gr, 1) + " dB") : juce::String ("OFF")) : juce::String ("n/a"),
                    duckValue, juce::Justification::centredLeft);
        g.drawText (avail ? (guardOn.getToggleState() ? (cut < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (cut, 1) + " dB") : juce::String ("OFF")) : juce::String ("n/a"),
                    guardValue, juce::Justification::centredLeft);
        g.setColour (m.quality.load() == 1 ? MASColours::accent2 : MASColours::textDim);
        g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.08f));
        g.drawText (qualityText (m.quality.load(), m.latency.load(), processor.getSampleRate()), qualityArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0);
        const int h = getHeight();
        auto take = [&r] (int w) { auto a = r.removeFromLeft (w); return a; };
        duckTitle = take (98).toFloat();
        duckOn.setBounds (take (56).withSizeKeepingCentre (56, 26)); take (14);
        amountTitle = take (62).toFloat(); take (8);
        amount.setBounds (take (130).withSizeKeepingCentre (130, 24)); take (12);
        duckLed = take (14).toFloat().withSizeKeepingCentre (12, 12); take (6);
        duckValue = take (70).toFloat();
        take (18);
        guardTitle = take (136).toFloat();
        guardOn.setBounds (take (56).withSizeKeepingCentre (56, 26)); take (12);
        guardLed = take (14).toFloat().withSizeKeepingCentre (12, 12); take (6);
        guardValue = take (70).toFloat();
        qualityArea = r.toFloat();
        juce::ignoreUnused (h);
    }

private:
    void timerCallback() override
    {
        const bool avail = processor.riMeters.engineAvailable.load();
        duckOn.setEnabled (avail); guardOn.setEnabled (avail); amount.setEnabled (avail);
        repaint();
    }

    MasterSpaceAudioProcessor& processor;
    MASLookAndFeel& lnf;
    juce::TextButton duckOn { "DUCK" }, guardOn { "GUARD" };
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> duckAtt, guardAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt;
    juce::Rectangle<float> duckTitle, guardTitle, amountTitle, duckLed, guardLed, duckValue, guardValue, qualityArea;
};

// DUCKER page, left block: gain reduction over time (early / late), current reduction, sidechain status.
class DuckMeterView : public juce::Component, private juce::Timer
{
public:
    explicit DuckMeterView (MasterSpaceAudioProcessor& p) : processor (p)
    {
        std::fill (std::begin (histE), std::end (histE), 0.0f);
        std::fill (std::begin (histL), std::end (histL), 0.0f);
        startTimerHz (30);
    }
    ~DuckMeterView() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 8.0f);
        r.reduce (12.0f, 10.0f);

        g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.12f));
        g.setColour (MASColours::textDim);
        g.drawText ("GAIN REDUCTION", r.removeFromTop (16.0f), juce::Justification::centredLeft);
        r.removeFromTop (6.0f);

        auto& m = processor.riMeters;
        const float scale = juce::jmax (6.0f, rawParam (processor.apvts, ParamIDs::duckAmount) + 1.0f);
        auto bars = r.removeFromRight (56.0f);
        auto status = r.removeFromBottom (58.0f);
        auto graph = r.reduced (0.0f, 2.0f);

        // grid: 0 dB at the top, down to the amount (+1 dB)
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (float db = 0.0f; db <= scale; db += scale > 12.0f ? 6.0f : 3.0f)
        {
            const float y = graph.getY() + graph.getHeight() * db / scale;
            g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
            g.setColour (MASColours::textDim.withAlpha (0.7f));
            g.setFont (uiFont (9.5f));
            g.drawText (juce::String (-db, 0), juce::Rectangle<float> (graph.getX(), y - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
        }

        auto curve = [&] (const float* h, bool fill)
        {
            juce::Path p;
            const float dx = graph.getWidth() / (float) (kHist - 1);
            for (int i = 0; i < kHist; ++i)
            {
                const float v = h[(head + i) % kHist];
                const float x = graph.getX() + dx * (float) i;
                const float y = graph.getY() + graph.getHeight() * juce::jlimit (0.0f, 1.0f, v / scale);
                if (i == 0) p.startNewSubPath (x, fill ? graph.getY() : y);
                if (fill || i > 0) p.lineTo (x, y);
            }
            if (fill) { p.lineTo (graph.getRight(), graph.getY()); p.closeSubPath(); }
            return p;
        };
        g.setColour (MASColours::accent.withAlpha (0.35f));
        g.fillPath (curve (histL, true));
        g.setColour (MASColours::accent);
        g.strokePath (curve (histL, false), juce::PathStrokeType (1.6f));
        g.setColour (MASColours::blue.brighter (0.3f));
        g.strokePath (curve (histE, false), juce::PathStrokeType (1.4f));

        // bars: early / late
        const float e = m.duckEarlyDb.load(), l = m.duckLateDb.load();
        auto bar = [&] (juce::Rectangle<float> a, float v, juce::Colour c, const char* name)
        {
            auto label = a.removeFromBottom (16.0f);
            g.setColour (MASColours::track);
            g.fillRoundedRectangle (a, 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (a.withHeight (a.getHeight() * juce::jlimit (0.0f, 1.0f, v / scale)), 3.0f);
            g.setColour (MASColours::textDim);
            g.setFont (uiFont (10.0f, true));
            g.drawText (name, label, juce::Justification::centred);
        };
        auto b1 = bars.removeFromLeft (24.0f), b2 = (bars.removeFromLeft (8.0f), bars.removeFromLeft (24.0f));
        bar (b1.withTrimmedLeft (4.0f), e, MASColours::blue, "E");
        bar (b2, l, MASColours::accent, "L");

        // status
        const bool on = rawParam (processor.apvts, ParamIDs::duckOn) > 0.5f;
        const bool extWanted = rawParam (processor.apvts, ParamIDs::scSource) > 0.5f;
        const bool extLive = m.scExternalLive.load();
        g.setFont (uiFont (12.0f, true));
        g.setColour (MASColours::text);
        g.drawText (on ? "EARLY -" + juce::String (e, 1) + " dB    LATE -" + juce::String (l, 1) + " dB" : juce::String ("SMART DUCKER OFF"),
                    status.removeFromTop (20.0f), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f));
        g.setColour (extWanted && ! extLive ? MASColours::accent2 : MASColours::textDim);
        const juce::String sc = ! extWanted ? "Sidechain: INTERNAL (plugin input)"
                              : extLive ? "Sidechain: EXTERNAL (host sidechain bus)"
                                        : "Sidechain: EXTERNAL chosen, no bus from the host - INTERNAL used";
        g.drawText (sc, status.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setColour (MASColours::textDim);
        g.drawText (m.duckActive.load() ? "ducking" : "open", status.removeFromTop (18.0f), juce::Justification::centredLeft);

        if (! m.engineAvailable.load())
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f, 6.0f), 8.0f);
            g.setColour (MASColours::textDim);
            g.setFont (uiFont (13.0f, true));
            g.drawFittedText ("SMART DUCKER works with the 1.2 engine in ALGO mode", getLocalBounds().reduced (30), juce::Justification::centred, 3);
        }
    }

private:
    void timerCallback() override
    {
        auto& m = processor.riMeters;
        histE[head] = m.duckEarlyDb.load();
        histL[head] = m.duckLateDb.load();
        head = (head + 1) % kHist;
        repaint();
    }

    static constexpr int kHist = 180;      // 6 s at 30 Hz
    MasterSpaceAudioProcessor& processor;
    float histE[kHist], histL[kHist];
    int head = 0;
};

// GUARD page, left block: wet spectrum, regions being reduced and by how much, processing range, DELTA / VIEW.
class GuardView : public juce::Component, private juce::Timer
{
public:
    GuardView (MasterSpaceAudioProcessor& p, MASLookAndFeel& l) : processor (p), lnf (l)
    {
        delta.setClickingTogglesState (true);
        delta.setToggleState (p.getDeltaListen(), juce::dontSendNotification);
        delta.onClick = [this] { processor.setDeltaListen (delta.getToggleState()); };
        delta.setTooltip ("DELTA LISTEN: hear only what RESONANCE GUARD removes (exact, aligned difference). Not saved.");
        view.setClickingTogglesState (true);
        view.setToggleState (p.getVisualisation(), juce::dontSendNotification);
        view.onClick = [this] { processor.setVisualisation (view.getToggleState()); repaint(); };
        view.setTooltip ("Spectrum display on / off (the display never affects the sound)");
        for (auto* b : { &delta, &view }) { b->setLookAndFeel (&lnf); addAndMakeVisible (b); }
        startTimerHz (30);
    }
    ~GuardView() override { stopTimer(); delta.setLookAndFeel (nullptr); view.setLookAndFeel (nullptr); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        auto top = r.removeFromTop (26);
        view.setBounds (top.removeFromRight (64));
        top.removeFromRight (8);
        delta.setBounds (top.removeFromRight (74));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 8.0f);
        r.reduce (12.0f, 10.0f);

        auto& m = processor.riMeters;
        auto& s = processor.apvts;
        const int mode = juce::roundToInt (rawParam (s, ParamIDs::rgMode));
        const bool on = rawParam (s, ParamIDs::rgOn) > 0.5f;
        g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.12f));
        g.setColour (MASColours::textDim);
        const char* modes[] = { "SMOOTH", "DEEP", "TAIL AWARE (EXPERIMENTAL)" };
        g.drawText (juce::String ("RESONANCE GUARD  ") + modes[juce::jlimit (0, 2, mode)], r.removeFromTop (16.0f), juce::Justification::centredLeft);
        r.removeFromTop (18.0f);

        auto footer = r.removeFromBottom (40.0f);
        auto plot = r;
        auto grStrip = plot.removeFromBottom (plot.getHeight() * 0.28f);
        plot.removeFromBottom (6.0f);

        constexpr float fLo = 20.0f, fHi = 20000.0f, dbTop = 0.0f, dbBot = -84.0f;
        auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / fLo) / std::log (fHi / fLo); };
        auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (dbTop - juce::jlimit (dbBot, dbTop, db)) / (dbTop - dbBot); };

        // range shading (outside LOW..HIGH is not processed)
        const float lo = rawParam (s, ParamIDs::rgLow), hi = rawParam (s, ParamIDs::rgHigh);
        g.setColour (juce::Colour (0xff1f2229));
        g.fillRect (plot);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (plot.withRight (xOf (lo)));
        g.fillRect (plot.withLeft (xOf (hi)));
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.drawVerticalLine (juce::roundToInt (xOf (f)), plot.getY(), grStrip.getBottom());
        }
        g.setFont (uiFont (9.5f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (MASColours::textDim.withAlpha (0.7f));
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (xOf (f) + 3.0f, plot.getBottom() - 13.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        }

        const int nb = m.numBands.load();
        if (! processor.getVisualisation())
        {
            g.setColour (MASColours::textDim);
            g.setFont (uiFont (13.0f, true));
            g.drawText ("DISPLAY OFF", plot, juce::Justification::centred);
        }
        else if (nb > 1)
        {
            // wet spectrum (smoothed per band for the display), and the part removed
            float peakDb = -200.0f;
            for (int b = 0; b < nb; ++b) peakDb = juce::jmax (peakDb, shownLevel[b]);
            const float norm = peakDb > -150.0f ? -6.0f - peakDb : 0.0f;    // the display follows the level (relative view)
            juce::Path before, after, removed;
            for (int b = 0; b < nb; ++b)
            {
                const float x = xOf (m.bandHz[b].load());
                const float y0 = yOf (shownLevel[b] + norm), y1 = yOf (shownLevel[b] + norm - shownCut[b]);
                if (b == 0) { before.startNewSubPath (x, y0); after.startNewSubPath (x, y1); }
                else { before.lineTo (x, y0); after.lineTo (x, y1); }
            }
            removed = before;
            for (int b = nb - 1; b >= 0; --b) removed.lineTo (xOf (m.bandHz[b].load()), yOf (shownLevel[b] + norm - shownCut[b]));
            removed.closeSubPath();
            g.setColour (MASColours::accent.withAlpha (0.55f));
            g.fillPath (removed);
            g.setColour (MASColours::text.withAlpha (0.55f));
            g.strokePath (before, juce::PathStrokeType (1.2f));
            g.setColour (MASColours::accent2);
            g.strokePath (after, juce::PathStrokeType (1.4f));

            // reduction per band (GR), scaled to MAX REDUCTION
            const float maxDb = juce::jmax (1.0f, rawParam (s, ParamIDs::rgMax));
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (grStrip);
            for (int b = 0; b < nb; ++b)
            {
                const float c = shownCut[b];
                if (c < 0.02f) continue;
                const float x0 = xOf (m.bandHz[juce::jmax (0, b - 1)].load()), x1 = xOf (m.bandHz[juce::jmin (nb - 1, b + 1)].load());
                const float w = juce::jmax (1.5f, (x1 - x0) * 0.5f);
                const float h = grStrip.getHeight() * juce::jlimit (0.0f, 1.0f, c / maxDb);
                g.setColour (c > 6.0f ? MASColours::accent2 : MASColours::accent);
                g.fillRect (xOf (m.bandHz[b].load()) - w * 0.5f, grStrip.getY(), w, h);
            }
            g.setColour (MASColours::textDim.withAlpha (0.8f));
            g.setFont (uiFont (9.5f));
            g.drawText ("GR  0 .. -" + juce::String (maxDb, 1) + " dB", grStrip.reduced (4.0f, 2.0f), juce::Justification::bottomLeft);
        }
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRect (plot.getUnion (grStrip));

        // footer: state, reduction, quality / latency
        g.setFont (uiFont (12.0f, true));
        g.setColour (MASColours::text);
        const float maxCut = m.rgMaxCutDb.load();
        const juce::String state = ! m.engineAvailable.load() ? juce::String ("needs the 1.2 engine in ALGO mode")
                                 : ! on ? juce::String ("OFF (bypassed, bit-exact)")
                                 : m.rgProcessing.load() ? "reducing  max -" + juce::String (maxCut, 1) + " dB" : juce::String ("listening (nothing to reduce)");
        g.drawText (state, footer.removeFromTop (20.0f), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f));
        g.setColour (MASColours::textDim);
        juce::String info = qualityText (m.quality.load(), m.latency.load(), processor.getSampleRate());
        if (on && rawParam (s, ParamIDs::rgMatch) > 0.5f) info << "    output match +" << juce::String (m.rgMakeupDb.load(), 1) << " dB";
        if (delta.getToggleState()) info << "    DELTA LISTEN";
        g.drawText (info, footer, juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        auto& m = processor.riMeters;
        const int nb = m.numBands.load();
        for (int b = 0; b < nb && b < RIMeters::maxBands; ++b)
        {
            shownLevel[b] += 0.35f * (m.levelDb[b].load() - shownLevel[b]);
            shownCut[b] += 0.5f * (m.cutDb[b].load() - shownCut[b]);
        }
        if (delta.getToggleState() != processor.getDeltaListen()) delta.setToggleState (processor.getDeltaListen(), juce::dontSendNotification);
        repaint();
    }

    MasterSpaceAudioProcessor& processor;
    MASLookAndFeel& lnf;
    juce::TextButton delta { "DELTA" }, view { "VIEW" };
    float shownLevel[RIMeters::maxBands] {}, shownCut[RIMeters::maxBands] {};
};

// COLOR page, left block: the transfer curve of the current character at the current DRIVE (TAPE: the
// hysteresis loop of one sine cycle), and what the module adds / how LEVEL COMP holds the level.
class ColorView : public juce::Component, private juce::Timer
{
public:
    explicit ColorView (MasterSpaceAudioProcessor& p) : processor (p) { startTimerHz (20); }
    ~ColorView() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 8.0f);
        r.reduce (14.0f, 10.0f);
        auto& s = processor.apvts;
        const int chr = juce::jlimit (0, 2, juce::roundToInt (rawParam (s, ParamIDs::hcChar)));
        const bool on = rawParam (s, ParamIDs::hcOn) > 0.5f;
        const float drive = rawParam (s, ParamIDs::hcDrive) * 0.01f;
        const char* names[] = { "SOFT", "TUBE", "TAPE" };
        g.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.12f));
        g.setColour (MASColours::textDim);
        g.drawText (juce::String ("HARMONIC COLOR  ") + names[chr] + (rawParam (s, ParamIDs::hcPos) > 0.5f ? "  \xc2\xb7  PRE" : "  \xc2\xb7  POST"),
                    r.removeFromTop (16.0f), juce::Justification::centredLeft);
        r.removeFromTop (10.0f);
        auto footer = r.removeFromBottom (46.0f);
        auto plot = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));

        g.setColour (juce::Colour (0xff1f2229));
        g.fillRect (plot);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY());
        g.drawLine (plot.getCentreX(), plot.getY(), plot.getCentreX(), plot.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);    // straight line = clean

        // the curve: input (heard level, peak 1) -> output, the same functions as the DSP
        const float gain = std::pow (10.0f, (-12.0f + 36.0f * drive) / 20.0f);
        auto toX = [&] (float v) { return plot.getCentreX() + v * plot.getWidth() * 0.5f; };
        auto toY = [&] (float v) { return plot.getCentreY() - juce::jlimit (-1.2f, 1.2f, v) * plot.getHeight() * 0.5f; };
        juce::Path curve;
        if (chr == 2)
        {
            // one sine cycle (after one cycle to settle) through the hysteresis, as in the DSP (a full-scale low note)
            float play = 0.0f;
            const int N = 256;
            const float rr = 0.35f * gain * hc::fastTanh (gain);
            for (int k = 0; k < 2 * N; ++k)
            {
                const float x = std::sin (juce::MathConstants<float>::twoPi * (float) k / N);
                const float v = x * gain;
                play = juce::jlimit (v - rr, v + rr, play);
                const float e = (play - v) / rr, e2 = e * e;
                const float dev = rr * e * (15.0f - 10.0f * e2 + 3.0f * e2 * e2) * 0.125f;
                const float y = hc::fastTanh (v + 0.35f * dev) / gain;
                if (k == N) curve.startNewSubPath (toX (x), toY (y));
                else if (k > N) curve.lineTo (toX (x), toY (y));
            }
        }
        else
        {
            for (int k = 0; k <= 200; ++k)
            {
                const float x = -1.0f + 2.0f * (float) k / 200.0f, v = x * gain;
                float y;
                if (chr == 1)
                {
                    const float b = 0.45f / (1.0f + 1.5f * std::abs (v) * 0.64f), tb = hc::fastTanh (b);
                    y = (hc::fastTanh (v + b) - tb) / (1.0f - tb * tb) / gain;
                }
                else y = v / std::sqrt (1.0f + v * v) / gain;
                if (k == 0) curve.startNewSubPath (toX (x), toY (y)); else curve.lineTo (toX (x), toY (y));
            }
        }
        g.setColour ((on ? MASColours::accent : MASColours::textDim).withAlpha (on ? 1.0f : 0.5f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRect (plot);

        // footer: what is added, level compensation
        auto& m = processor.riMeters;
        const float res = m.hcResidueDb.load(), comp = m.hcCompDb.load();
        g.setFont (uiFont (12.0f, true));
        g.setColour (MASColours::text);
        const juce::String state = ! m.engineAvailable.load() ? juce::String ("needs the 1.2 engine in ALGO mode")
                                 : ! on ? juce::String ("OFF (bypassed, bit-exact)")
                                 : "harmonics " + (res < -100.0f ? juce::String ("none") : juce::String (res, 1) + " dB re wet");
        g.drawText (state, footer.removeFromTop (22.0f), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f));
        g.setColour (MASColours::textDim);
        g.drawText (on && rawParam (s, ParamIDs::hcComp) > 0.5f ? "level comp " + juce::String (comp, 1) + " dB  \xc2\xb7  200-500 Hz and air held"
                                                               : juce::String ("level comp off"),
                    footer, juce::Justification::centredLeft);
    }

private:
    void timerCallback() override { repaint(); }
    MasterSpaceAudioProcessor& processor;
};
#endif

//==============================================================================
#ifdef MAS_EQ
#include "EqLabView.h"
#endif

MASPanel::MASPanel (MasterSpaceAudioProcessor& p, MASLookAndFeel& lf)
    : processor (p), lnf (lf)
{
    setLookAndFeel (&lnf);
    emblem   = juce::ImageCache::getFromMemory (BinaryData::emblem_png,   BinaryData::emblem_pngSize);
    wordmark = juce::ImageCache::getFromMemory (BinaryData::wordmark_png, BinaryData::wordmark_pngSize);

    auto makeGroup = [this] (juce::OwnedArray<juce::TextButton>& group, juce::StringArray names, const char* paramID)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = group.add (new juce::TextButton (names[i]));
            b->onClick = [this, paramID, i] { setChoice (paramID, i); };
            addAndMakeVisible (b);
        }
    };
   #ifdef MAS_ATMOS
    makeGroup (algoButtons,  { "STUDIO ROOM", "CHAMBER", "HALL", "ATMOS" }, ParamIDs::algorithm);
    for (int i = 0; i < 3; ++i)
        algoButtons[i]->onClick = [this, i] { setParamPlain (processor.apvts, ParamIDs::tail, 0.0f); setChoice (ParamIDs::algorithm, i); };
    algoButtons[3]->onClick = [this] { setChoice (ParamIDs::tail, 1); showPage (kAtmosPage); };
    algoButtons[3]->setTooltip ("ATMOSPHERIC TAIL (experimental late engine)");
   #else
    makeGroup (algoButtons,  { "STUDIO ROOM", "CHAMBER", "HALL" }, ParamIDs::algorithm);
   #endif
    makeGroup (routeButtons, { "INSERT", "AUX" }, ParamIDs::routing);
    makeGroup (modeButtons,  { "ALGO", "IR" }, ParamIDs::mode);
    routeButtons[0]->setTooltip ("Direct sound is moved into the space too");
    routeButtons[1]->setTooltip ("Space only, the direct sound stays untouched (100 % wet)");

    loadButton.onClick = [this] { chooseIRFile(); };
    addChildComponent (loadButton);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setFont (uiFont (12.0f));
    infoLabel.setColour (juce::Label::textColourId, MASColours::textDim);
    infoLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (infoLabel);

    // ---- pages
    using K = ControlPage::Kind;
    using namespace ParamIDs;
    std::vector<std::vector<ControlPage::Spec>> mainRows {
        { { K::knob, decay, "DECAY", true, irLength, "IR LENGTH" }, { K::knob, size, "SIZE", true }, { K::knob, preDelay, "PRE-DELAY" },
          { K::knob, erLevel, "EARLY" }, { K::knob, lateLevel, "LATE" }, { K::knob, mix, "MIX" } },
        { { K::knob, diffusion, "DIFFUSION", true }, { K::knob, density, "DENSITY", true }, { K::knob, shape, "SHAPE", true },
          { K::knob, spread, "SPREAD", true }, { K::knob, width, "WIDTH" }, { K::knob, output, "OUTPUT" } } };
    pages.add (new ControlPage (processor.apvts, lnf, mainRows, 6, std::make_unique<DepthBlock> (processor.apvts, lnf), 236));
   #ifdef MAS_RI
    pages.getLast()->setBottomBlock (std::make_unique<RIStrip> (processor, lnf), 44);
   #endif

    std::vector<std::vector<ControlPage::Spec>> spaceRows {
        { { K::knob, roomW, "ROOM W", true }, { K::knob, roomD, "ROOM L", true }, { K::knob, roomH, "ROOM H", true } },
        { { K::knob, size, "SIZE", true }, { K::knob, depth, "DEPTH", true }, { K::knob, srcX, "SOURCE X", true } },
        { { K::choice, matFront, "FRONT", true }, { K::choice, matBack, "BACK", true }, { K::choice, matLeft, "LEFT", true } },
        { { K::choice, matRight, "RIGHT", true }, { K::choice, matFloor, "FLOOR", true }, { K::choice, matCeiling, "CEILING", true } } };
    auto rv = std::make_unique<RoomView> (processor);
    roomView = rv.get();
    pages.add (new ControlPage (processor.apvts, lnf, spaceRows, 3, std::move (rv), 500));

    std::vector<std::vector<ControlPage::Spec>> earlyRows {
        { { K::knob, erLevel, "EARLY LEVEL" }, { K::knob, erDensity, "DENSITY", true }, { K::knob, erDiffusion, "DIFFUSION", true },
          { K::knob, erColor, "COLOR", true }, { K::knob, erWidth, "WIDTH", true }, { K::toggle, erOn, "EARLY" } },
        { { K::knob, erStart, "START", true }, { K::knob, erStop, "STOP", true }, { K::knob, erPreDelay, "PRE-DELAY", true },
          { K::knob, lisX, "LISTENER X", true }, { K::knob, lisY, "LISTENER Y", true }, { K::toggle, lateOn, "LATE" } } };
    pages.add (new ControlPage (processor.apvts, lnf, earlyRows, 6));

    std::vector<std::vector<ControlPage::Spec>> lateRows {
        { { K::knob, lateLevel, "LATE LEVEL" }, { K::knob, lateDelay, "LATE DELAY", true }, { K::knob, density, "DENSITY", true },
          { K::knob, diffusion, "DIFFUSION", true }, { K::knob, shape, "SHAPE", true }, { K::knob, spread, "SPREAD", true } },
        { { K::knob, decay, "DECAY", true }, { K::knob, lateWidth, "WIDTH", true }, { K::knob, lateColor, "COLOR", true },
          { K::knob, modDepth, "MODULATION", true }, { K::choice, algorithm, "ALGORITHM", true }, { K::toggle, lateOn, "LATE" } } };
    pages.add (new ControlPage (processor.apvts, lnf, lateRows, 6));

    std::vector<std::vector<ControlPage::Spec>> eqRows {
        { { K::knob, lfMult, "LF DECAY", true }, { K::knob, lmMult, "LOW-MID DECAY", true }, { K::knob, hmMult, "HIGH-MID DECAY", true },
          { K::knob, hfMult, "HF DECAY", true }, { K::knob, decay, "DECAY", true }, { K::toggle, lockDecay, "DECAY LOCK", true } },
        { { K::knob, xLow, "X-OVER LOW", true }, { K::knob, xMid, "X-OVER MID", true }, { K::knob, xHigh, "X-OVER HIGH", true },
          { K::knob, lowCut, "LOW CUT" }, { K::knob, highCut, "HIGH CUT" }, { K::empty } } };
    pages.add (new ControlPage (processor.apvts, lnf, eqRows, 6));

    std::vector<std::vector<ControlPage::Spec>> modRows {
        { { K::choice, modChar, "CHARACTER", true }, { K::knob, modRate, "RATE", true }, { K::knob, modDepth, "DEPTH", true },
          { K::knob, modRandom, "RANDOM", true }, { K::knob, modSmooth, "SMOOTH", true } } };
    pages.add (new ControlPage (processor.apvts, lnf, modRows, 5));

    std::vector<std::vector<ControlPage::Spec>> stereoRows {
        { { K::knob, width, "WIDTH" }, { K::knob, erWidth, "EARLY WIDTH", true }, { K::knob, lateWidth, "LATE WIDTH", true },
          { K::knob, balance, "BALANCE", true }, { K::knob, crossfeed, "CROSSFEED", true }, { K::knob, decorrelation, "DECORRELATION", true } },
        { { K::toggle, lockWidth, "WIDTH LOCK", true }, { K::empty } } };
    pages.add (new ControlPage (processor.apvts, lnf, stereoRows, 6));

    std::vector<std::vector<ControlPage::Spec>> advRows {
        { { K::knob, output, "OUTPUT" }, { K::knob, mix, "MIX" }, { K::choice, routing, "ROUTING" },
          { K::knob, irLength, "IR LENGTH" }, { K::knob, lowCut, "LOW CUT" }, { K::knob, highCut, "HIGH CUT" } },
        { { K::toggle, lockLevel, "LEVEL LOCK", true }, { K::toggle, lockTone, "TONE LOCK", true }, { K::toggle, lockDecay, "DECAY LOCK", true },
          { K::toggle, lockWidth, "WIDTH LOCK", true }, { K::choice, mode, "MODE" }, { K::choice, algorithm, "ALGORITHM", true } },
        { { K::choice, ParamIDs::engine, "DSP ENGINE" }, { K::empty }, { K::empty }, { K::empty }, { K::empty }, { K::empty } } };
   #ifdef MAS_RI
    advRows[2][1] = { K::choice, ParamIDs::hcPos, "COLOR POSITION", true };
   #endif
    pages.add (new ControlPage (processor.apvts, lnf, advRows, 6));

   #ifdef MAS_ATMOS
    // ATMOS: the five main controls, then the shaping controls, then routing of the late engine
    const std::vector<std::vector<ControlPage::Spec>> atmosRows {
        { { K::knob, decay, "DECAY", true }, { K::knob, ParamIDs::bloom, "BLOOM", true }, { K::knob, ParamIDs::motion, "MOTION", true },
          { K::knob, ParamIDs::air, "AIR", true }, { K::knob, density, "DENSITY", true }, { K::knob, lateLevel, "LATE LEVEL" } },
        { { K::knob, shape, "SHAPE", true }, { K::knob, spread, "SPREAD", true }, { K::knob, diffusion, "DIFFUSION", true },
          { K::knob, size, "SIZE", true }, { K::knob, lateDelay, "LATE DELAY", true }, { K::toggle, ParamIDs::charLock, "CHARACTER LOCK", true } },
        { { K::choice, ParamIDs::tail, "LATE ENGINE", true }, { K::toggle, erOn, "EARLY REFLECTIONS" }, { K::knob, modRate, "MOTION RATE", true },
          { K::knob, decorrelation, "DECORRELATION" }, { K::knob, mix, "MIX" }, { K::knob, output, "OUTPUT" } } };
    pages.add (new ControlPage (processor.apvts, lnf, atmosRows, 6));
   #endif

   #ifdef MAS_RI
    // SMART DUCKER: level detector and timing, early / late depth, spectral amount, sidechain
    const std::vector<std::vector<ControlPage::Spec>> duckRows {
        { { K::toggle, duckOn, "SMART DUCKER", true }, { K::choice, duckMode, "MODE", true }, { K::knob, duckAmount, "DUCK AMOUNT", true },
          { K::knob, duckThresh, "THRESHOLD", true }, { K::knob, duckRatio, "RATIO", true } },
        { { K::knob, duckAttack, "ATTACK", true }, { K::knob, duckHold, "HOLD", true }, { K::knob, duckRelease, "RELEASE", true },
          { K::knob, duckSens, "SENSITIVITY", true }, { K::knob, duckSpectral, "SPECTRAL AMOUNT", true } },
        { { K::knob, duckEarly, "EARLY DUCK", true }, { K::knob, duckLate, "LATE DUCK", true }, { K::choice, scSource, "SIDECHAIN", true },
          { K::knob, scLowCut, "SC LOW CUT", true }, { K::knob, scHighCut, "SC HIGH CUT", true } } };
    pages.add (new ControlPage (processor.apvts, lnf, duckRows, 5, std::make_unique<DuckMeterView> (processor), 330));

    // RESONANCE GUARD: detection, ballistics, range, early / late targets, quality
    const std::vector<std::vector<ControlPage::Spec>> guardRows {
        { { K::toggle, rgOn, "RESONANCE GUARD", true }, { K::choice, rgMode, "MODE", true }, { K::choice, quality, "QUALITY", true },
          { K::knob, rgMax, "MAX REDUCTION", true } },
        { { K::knob, rgSmooth, "SMOOTH", true }, { K::knob, rgDetail, "DETAIL", true }, { K::knob, rgSelect, "SELECTIVITY", true },
          { K::toggle, rgMatch, "OUTPUT MATCH", true } },
        { { K::knob, rgAttack, "ATTACK", true }, { K::knob, rgRelease, "RELEASE", true }, { K::knob, rgLow, "LOW", true },
          { K::knob, rgHigh, "HIGH", true } },
        { { K::choice, rgTarget, "TARGET", true }, { K::knob, rgEarly, "EARLY AMOUNT", true }, { K::knob, rgLate, "LATE AMOUNT", true },
          { K::empty } } };
    pages.add (new ControlPage (processor.apvts, lnf, guardRows, 4, std::make_unique<GuardView> (processor, lnf), 470));

    // HARMONIC COLOR: character, drive, colour, mix, level compensation (position: ADVANCED)
    const std::vector<std::vector<ControlPage::Spec>> colorRows {
        { { K::toggle, hcOn, "HARMONIC COLOR", true }, { K::choice, hcChar, "CHARACTER", true }, { K::toggle, hcComp, "LEVEL COMP", true } },
        { { K::knob, hcDrive, "DRIVE", true }, { K::knob, hcColor, "COLOR", true }, { K::knob, hcMix, "MIX", true } } };
    pages.add (new ControlPage (processor.apvts, lnf, colorRows, 3, std::make_unique<ColorView> (processor), 420));
   #endif
   #ifdef MAS_EQ
    // REVERB EQ + spectral analyzer (1.5 EQ LAB): one custom block over the whole page
    pages.add (new ControlPage (processor.apvts, lnf, {}, 1, std::make_unique<eqlab::Page> (processor), baseWidth - 60));
    jassert (pages.size() - 1 == kEqPage);
   #endif

   #if defined (MAS_EQ)
    const juce::StringArray tabNames { "MAIN", "SPACE", "EARLY", "LATE", "DECAY EQ", "MOD", "STEREO", "ADVANCED", "ATMOS",
                                       "DUCKER", "GUARD", "COLOR", "REVERB EQ", "PRESETS" };
   #elif defined (MAS_RI)
    const juce::StringArray tabNames { "MAIN", "SPACE", "EARLY", "LATE", "DECAY EQ", "MODULATION", "STEREO", "ADVANCED", "ATMOS",
                                       "DUCKER", "GUARD", "COLOR", "PRESETS" };
   #elif defined (MAS_ATMOS)
    const juce::StringArray tabNames { "MAIN", "SPACE", "EARLY", "LATE", "DECAY EQ", "MODULATION", "STEREO", "ADVANCED", "ATMOS", "PRESETS" };
   #else
    const juce::StringArray tabNames { "MAIN", "SPACE", "EARLY", "LATE", "DECAY EQ", "MODULATION", "STEREO", "ADVANCED", "PRESETS" };
   #endif
    for (int i = 0; i < tabNames.size(); ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (tabNames[i]));
        b->getProperties().set ("tab", true);
        b->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
    }
    for (auto* pg : pages) addChildComponent (pg);

    // presets: quick bar, browser page, in-window dialog (on top of everything)
    presetDialog = std::make_unique<PresetDialog> (lnf);
    presetBar = std::make_unique<PresetBar> (processor, lnf, *presetDialog, [this] { showPage (kPresetsPage); });
    presetBrowser = std::make_unique<PresetBrowser> (processor, lnf, *presetDialog, *presetBar);
    addAndMakeVisible (*presetBar);
    addChildComponent (*presetBrowser);

    // room presets (set geometry + materials)
    roomPreset.setLookAndFeel (&lnf);
    roomPreset.setTextWhenNothingSelected ("Room type...");
    roomPreset.addItemList ({ "Vocal Booth", "Studio Room", "Live Room", "Chamber", "Stone Chamber", "Concert Hall", "Church", "Glass Room" }, 1);
    roomPreset.onChange = [this] { applyRoomPreset (roomPreset.getSelectedItemIndex()); };
    addChildComponent (roomPreset);

    addChildComponent (*presetDialog);

    setSize (baseWidth, baseHeight);
    showPage (0);
    updateState();
    startTimerHz (15);
}

MASPanel::~MASPanel()
{
    stopTimer();
    presetBrowser.reset();
    presetBar.reset();
    presetDialog.reset();
    roomPreset.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MASPanel::applyRoomPreset (int index)
{
    struct Preset { float w, d, h; int m[6]; };
    // materials: 0 absorber, 1 curtain, 2 wood, 3 plaster, 4 concrete, 5 glass  (front, back, left, right, floor, ceiling)
    static const Preset presets[] = {
        { 2.4f, 3.0f, 2.5f,  { 1, 0, 1, 1, 2, 0 } },
        { 6.0f, 8.0f, 3.2f,  { 2, 0, 2, 3, 2, 0 } },
        { 9.0f, 12.0f, 4.5f, { 2, 2, 3, 3, 2, 2 } },
        { 7.5f, 10.0f, 4.0f, { 3, 2, 3, 3, 2, 3 } },
        { 6.0f, 8.0f, 5.0f,  { 4, 4, 4, 4, 4, 4 } },
        { 22.0f, 34.0f, 14.0f, { 2, 3, 3, 3, 2, 3 } },
        { 18.0f, 40.0f, 18.0f, { 4, 4, 4, 5, 4, 4 } },
        { 8.0f, 10.0f, 3.5f, { 5, 5, 5, 5, 4, 3 } },
    };
    if (index < 0 || index >= (int) std::size (presets)) return;
    const auto& pr = presets[index];
    auto& s = processor.apvts;
    setParamPlain (s, ParamIDs::roomW, pr.w);
    setParamPlain (s, ParamIDs::roomD, pr.d);
    setParamPlain (s, ParamIDs::roomH, pr.h);
    setParamPlain (s, ParamIDs::size, 50.0f);
    for (int i = 0; i < 6; ++i) setParamPlain (s, ParamIDs::materials[i], (float) pr.m[i]);
}

void MASPanel::setChoice (const char* paramID, int index)
{
    setParamPlain (processor.apvts, paramID, (float) index);
    updateState();
}

void MASPanel::showPage (int index)
{
    currentPage = index;
    for (int i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (i == index);
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setToggleState (i == index, juce::dontSendNotification);
    if (presetBrowser != nullptr) presetBrowser->setVisible (index == kPresetsPage);
    roomPreset.setVisible (index == 1);
    roomPreset.toFront (false);
    if (presetDialog != nullptr && presetDialog->isVisible()) presetDialog->toFront (true);
}

void MASPanel::updateState()
{
    const bool ir = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    const int algo = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::algorithm)->load());
    const int route = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::routing)->load());
    shownIR = ir; shownAlgo = algo; shownRoute = route;
   #ifdef MAS_ATMOS
    if (processor.apvts.getRawParameterValue (ParamIDs::tail)->load() > 0.5f) shownAlgo += 4;
   #endif
    shownIRName = processor.getIRDisplayName();

   #ifdef MAS_ATMOS
    const bool atmos = processor.apvts.getRawParameterValue (ParamIDs::tail)->load() > 0.5f;
    for (int i = 0; i < algoButtons.size(); ++i)
    {
        algoButtons[i]->setToggleState (i == 3 ? atmos : (! atmos && i == algo), juce::dontSendNotification);
        algoButtons[i]->setEnabled (! ir);
    }
   #else
    for (int i = 0; i < algoButtons.size(); ++i)  { algoButtons[i]->setToggleState (i == algo, juce::dontSendNotification); algoButtons[i]->setEnabled (! ir); }
   #endif
    for (int i = 0; i < routeButtons.size(); ++i) routeButtons[i]->setToggleState (i == route, juce::dontSendNotification);
    for (int i = 0; i < modeButtons.size(); ++i)  modeButtons[i]->setToggleState (i == (ir ? 1 : 0), juce::dontSendNotification);

    for (auto* pg : pages) pg->setIRMode (ir);
    roomPreset.setEnabled (! ir);
    loadButton.setVisible (ir);

    if (! ir)
       #ifdef MAS_EQ
        infoLabel.setText ("EQ LAB  v" JucePlugin_VersionString "   DON'T ADD REVERB. CREATE SPACE.", juce::dontSendNotification);
       #else
        infoLabel.setText ("v" JucePlugin_VersionString "   DON'T ADD REVERB. CREATE SPACE.", juce::dontSendNotification);
       #endif
    else if (shownIRName.isEmpty())
        infoLabel.setText ("Drop a WAV / AIFF / FLAC here", juce::dontSendNotification);
    else
        infoLabel.setText ("IR: " + shownIRName, juce::dontSendNotification);
    infoLabel.setColour (juce::Label::textColourId, ir && shownIRName.isNotEmpty() ? MASColours::text : MASColours::textDim);

    if (roomView != nullptr) roomView->repaint();
    resized();
}

void MASPanel::timerCallback()
{
    const bool ir = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    int algo = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::algorithm)->load());
   #ifdef MAS_ATMOS
    if (processor.apvts.getRawParameterValue (ParamIDs::tail)->load() > 0.5f) algo += 4;
   #endif
    const int route = juce::roundToInt (processor.apvts.getRawParameterValue (ParamIDs::routing)->load());
    if (ir != shownIR || algo != shownAlgo || route != shownRoute || processor.getIRDisplayName() != shownIRName)
        updateState();
    if (roomView != nullptr && currentPage == 1)
        roomView->refreshIfChanged();
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
        setChoice (ParamIDs::mode, 1);
    updateState();
}

void MASPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (MASColours::bgTop, 0.0f, 0.0f, MASColours::bgBottom, 0.0f, area.getHeight(), false));
    g.fillAll();

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const float emH = 76.0f;
    const float emW = emblem.isValid() ? emH * (float) emblem.getWidth() / (float) emblem.getHeight() : 0.0f;
    if (emblem.isValid())
        g.drawImage (emblem, juce::Rectangle<float> (26.0f, 12.0f, emW, emH), juce::RectanglePlacement::centred);

    const int tx = (int) (26.0f + emW + 18.0f);
    g.setColour (MASColours::text);
    g.setFont (uiFont (31.0f, true).withExtraKerningFactor (0.16f));
   #ifdef MAS_EQ
    g.drawText ("MASTER SPACE 1.5", tx, 18, 440, 38, juce::Justification::centredLeft);   // "EQ LAB" is in the version line
   #else
    g.drawText (JucePlugin_Name, tx, 18, 440, 38, juce::Justification::centredLeft);
   #endif

    if (wordmark.isValid())
    {
        const float wmH = 13.0f;
        const float wmW = wmH * (float) wordmark.getWidth() / (float) wordmark.getHeight();
        g.drawImage (wordmark, juce::Rectangle<float> ((float) tx + 2.0f, 60.0f, wmW, wmH), juce::RectanglePlacement::centred);
    }

    g.setColour (MASColours::accent.withAlpha (0.45f));
    g.fillRect (32.0f, (float) kHeader - 2.0f, area.getWidth() - 64.0f, 1.0f);
}

void MASPanel::resized()
{
    const int right = baseWidth - 32;

    // row 1: algorithms
    {
        const int n = algoButtons.size(), gap = 6, w = n > 3 ? 98 : 112;
        int x = right - (n * w + (n - 1) * gap);
        for (auto* b : algoButtons) { b->setBounds (x, 16, w, 28); x += w + gap; }
    }
    // row 2: routing + mode, IR loader / info
    {
        const int w = 68, gap = 6;
        int x = right - (4 * w + 2 * gap + 12);
        for (auto* b : routeButtons) { b->setBounds (x, 54, w, 26); x += w + gap; }
        x += 12 - gap;
        for (auto* b : modeButtons) { b->setBounds (x, 54, w, 26); x += w + gap; }
        const int left = right - (4 * w + 2 * gap + 12);
        loadButton.setBounds (left - 100, 54, 92, 26);
        infoLabel.setBounds (shownIR ? juce::Rectangle<int> (left - 100 - 330, 54, 322, 26)
                                     : juce::Rectangle<int> (left - 420, 54, 412, 26));
    }

    // tabs
    {
        const int x0 = 26, w = (baseWidth - 52) / tabButtons.size();
        for (int i = 0; i < tabButtons.size(); ++i)
            tabButtons[i]->setBounds (x0 + i * w, kTabsY, w, kTabsH);
    }

    const auto pageArea = juce::Rectangle<int> (20, kPageY, baseWidth - 40, baseHeight - kPageY - 18);
    for (auto* pg : pages) pg->setBounds (pageArea);
    if (presetBrowser != nullptr) presetBrowser->setBounds (pageArea);
    if (presetBar != nullptr) presetBar->setBounds (26, kBarY, baseWidth - 52, kBarH);
    if (presetDialog != nullptr) presetDialog->setBounds (getLocalBounds());
    roomPreset.setBounds (pageArea.getX() + 22, pageArea.getY() + 14, 160, 26);
}

//==============================================================================
MasterSpaceEditor::MasterSpaceEditor (MasterSpaceAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p, lnf)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);

    const int savedWidth = (int) processor.apvts.state.getProperty ("uiWidth", MASPanel::baseWidth);
    const double ratio = (double) MASPanel::baseWidth / (double) MASPanel::baseHeight;
    setResizable (true, true);
    setResizeLimits (kMinWidth, juce::roundToInt (kMinWidth / ratio), kMaxWidth, juce::roundToInt (kMaxWidth / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);

    const int w = juce::jlimit (kMinWidth, kMaxWidth, savedWidth);
    setSize (w, juce::roundToInt (w / ratio));
    sizeRestored = true;
}

MasterSpaceEditor::~MasterSpaceEditor()
{
    setLookAndFeel (nullptr);
}

void MasterSpaceEditor::paint (juce::Graphics& g)
{
    g.fillAll (MASColours::bgBottom);
}

void MasterSpaceEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    const auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (MASColours::accent.withAlpha (0.12f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (MASColours::accent);
    g.drawRoundedRectangle (r, 12.0f, 2.0f);
    g.setColour (MASColours::text);
    g.setFont (juce::Font (juce::FontOptions ((float) getHeight() * 0.06f, juce::Font::bold)));
    g.drawText ("DROP IMPULSE RESPONSE", getLocalBounds(), juce::Justification::centred);
}

bool MasterSpaceEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (MasterSpaceAudioProcessor::isSupportedIRFile (f))
            return true;
    return false;
}

void MasterSpaceEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (const auto& f : files)
        if (MasterSpaceAudioProcessor::isSupportedIRFile (f))
        {
            panel.loadIRFile (juce::File (f));
            break;
        }
}

void MasterSpaceEditor::resized()
{
    const float scale = (float) getWidth() / (float) MASPanel::baseWidth;
    panel.setBounds (0, 0, MASPanel::baseWidth, MASPanel::baseHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));

    if (sizeRestored)
        processor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}
