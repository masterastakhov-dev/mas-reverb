// MASTER SPACE STUDIO (MAS_STUDIO builds only): the look of the studio redesign.
// Photoreal metal knobs (static layers rendered once per size, scale and theme, only the arc and the pointer are
// drawn live), tactile keys, brushed graphite plates, five themes. Pure GUI: nothing here touches the DSP.

#pragma once

#include "PluginEditor.h"

namespace studio
{
//==============================================================================
struct Theme
{
    const char* name;
    juce::Colour tint, ind, gold, rim, screen, grid, lamp;
    bool goldSphere;
};

constexpr int kNumThemes = 5;
const Theme& theme (int index);

inline juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }

juce::Font inter (float height, bool semiBold = false, float kerning = 0.0f);
juce::Font cinzel (float height, float kerning = 0.0f);

//==============================================================================
// drawing helpers (logical coordinates; they render at whatever scale the Graphics context has)
void brushFill (juce::Graphics&, const juce::Path& area, juce::Colour base, float amount = 0.55f);
void plate (juce::Graphics&, juce::Rectangle<float>, float radius, const Theme&, float light = 0.035f, bool screws = true);
void recess (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour fill);
void screw (juce::Graphics&, juce::Point<float>, float r, float rotation, const Theme&);
void slit (juce::Graphics&, juce::Rectangle<float>, const Theme&);
void grille (juce::Graphics&, juce::Rectangle<float>, const Theme&);
#ifdef MAS_STUDIO_DYN
void glowingGrille (juce::Graphics&, juce::Rectangle<float>, const Theme&, float heat);
#endif
void goldLine (juce::Graphics&, float x, float y0, float y1, const Theme&);
void glowStroke (juce::Graphics&, const juce::Path&, juce::Colour, float width, float glow, float alpha = 1.0f);
void engraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Colour,
               juce::Justification = juce::Justification::centred);

// a tactile key: physical depth, pressed state, ON state lit from inside (blue, or gold for "gold" keys)
struct KeyState { bool on = false, hover = false, down = false, gold = false, led = false, enabled = true; };
void key (juce::Graphics&, juce::Rectangle<float>, const Theme&, const KeyState&);
void keyText (juce::Graphics&, juce::Rectangle<float>, const juce::String&, const Theme&, const KeyState&, float fontHeight = 10.0f);

//==============================================================================
// Rotary knob with fine control: Shift = 1/7 speed, without the jump JUCE would make when the speed changes mid-drag.
class Knob : public juce::Slider
{
public:
    Knob();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    juce::Point<float> last, virt;
};

//==============================================================================
class LookAndFeel : public MASLookAndFeel
{
public:
    LookAndFeel();
    ~LookAndFeel() override;

    void setThemeIndex (int);
    int getThemeIndex() const { return themeIndex; }
    const Theme& th() const { return theme (themeIndex); }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    // knob geometry, shared with the material sheet / tests
    enum class KnobKind { big, medium, small };
    static KnobKind kindOf (const juce::Slider&, float diameter);

private:
    const juce::Image& knobBase (KnobKind, float diameter, float scale);
    void applyColours();

    int themeIndex = 0;
    std::map<juce::String, juce::Image> knobCache;
};

// Inter for every default-sans font of this plugin while a studio editor is open (JUCE resolves default fonts
// through the default LookAndFeel, so a shared, reference-counted provider installs itself as the default).
struct FontProvider : public juce::LookAndFeel_V4
{
    FontProvider();
    ~FontProvider() override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};

// the studio look of the component's tree (falls back to the default theme)
const Theme& themeOf (const juce::Component&);
} // namespace studio
