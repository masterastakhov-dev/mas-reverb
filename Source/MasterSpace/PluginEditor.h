#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

#if defined (MAS_EQ)
// MASTER SPACE 1.5: MIDNIGHT BLUE. Deep blue surfaces, gold accents; the brand red stays in the logo and
// in the hot zone of the knobs (the arc turns red past 50 %).
namespace MASColours
{
    const juce::Colour bgTop    { 0xff0a1a38 };
    const juce::Colour bgBottom { 0xff03070f };
    const juce::Colour panel    { 0xff0b162e };
    const juce::Colour track    { 0xff1b2947 };
    const juce::Colour accent   { 0xffe2b866 };   // gold
    const juce::Colour accent2  { 0xfff3d79a };   // light gold
    const juce::Colour blue     { 0xff5ad6ff };   // early / cyan
    const juce::Colour text     { 0xffd6e0f0 };
    const juce::Colour textDim  { 0xff7f8fa8 };
    const juce::Colour hot      { 0xffe33f2b };   // brand red
}
#else
namespace MASColours
{
    const juce::Colour bgTop    { 0xff1c1e24 };
    const juce::Colour bgBottom { 0xff0e0f12 };
    const juce::Colour panel    { 0xff17191e };
    const juce::Colour track    { 0xff2c2f37 };
    const juce::Colour accent   { 0xffe33f2b };   // brand red
    const juce::Colour accent2  { 0xfff6b45c };   // brand orange
    const juce::Colour blue     { 0xff3a74f2 };   // brand blue
    const juce::Colour text     { 0xffe9e6df };
    const juce::Colour textDim  { 0xff8b8d93 };
}
#endif

class MASLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MASLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

//==============================================================================
// Top-down view of the virtual room: drag the source (lateral position + depth) and the listener.
class RoomView : public juce::Component
{
public:
    explicit RoomView (MasterSpaceAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refreshIfChanged();

private:
    struct Mapping { juce::Rectangle<float> room; float scale; mspace::Geometry g; };
    Mapping mapping (const mspace::Params&) const;
    void setParam (const char* id, float plainValue);

    MasterSpaceAudioProcessor& processor;
    enum class Drag { none, source, listener } drag = Drag::none;
    juce::String lastKey;
};

//==============================================================================
// One page of controls laid out in a grid, with an optional custom block on the left.
class ControlPage : public juce::Component
{
public:
    enum class Kind { knob, choice, toggle, empty };

    struct Spec
    {
        Kind kind;
        const char* paramID = nullptr;
        const char* title = nullptr;
        bool algoOnly = false;              // dimmed in IR mode
        const char* irParamID = nullptr;    // shown instead in IR mode
        const char* irTitle = nullptr;
        juce::uint32 accent = 0;            // optional label / knob accent (ARGB); 0 = the theme
    };

    ControlPage (juce::AudioProcessorValueTreeState&, MASLookAndFeel&, std::vector<std::vector<Spec>> rows,
                 int columns, std::unique_ptr<juce::Component> leftBlock = nullptr, int leftWidth = 0);
    ~ControlPage() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void setIRMode (bool irMode);
    juce::Component* getLeftBlock() { return leftBlock.get(); }
   #ifdef MAS_RI
    void setBottomBlock (std::unique_ptr<juce::Component> block, int height);
   #endif

private:
    struct Cell
    {
        Spec spec;
        juce::Label label, irLabel;
        std::unique_ptr<juce::Slider> slider, irSlider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::TextButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt, irAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
        int row = 0, col = 0;
    };

    juce::AudioProcessorValueTreeState& apvts;
    MASLookAndFeel& lnf;
    std::vector<std::unique_ptr<Cell>> cells;
    int numRows = 0, numCols = 6;
    std::unique_ptr<juce::Component> leftBlock;
    int leftWidth = 0;
   #ifdef MAS_RI
    std::unique_ptr<juce::Component> bottomBlock;
    int bottomHeight = 0;
   #endif
};

class PresetBar;
class PresetBrowser;
class PresetDialog;

//==============================================================================
class MASPanel : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int baseWidth  = 1000;
    static constexpr int baseHeight = 680;

    MASPanel (MasterSpaceAudioProcessor&, MASLookAndFeel&);
    ~MASPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void loadIRFile (const juce::File&);

   #ifdef MAS_STUDIO
    // studio build: this panel only hosts the classic pages inside the studio shell (no header, tabs or bar of its own)
    void setHosted (std::function<void (int)> onPageRequest);
    void showHostedPage (int index);
    void chooseIR() { chooseIRFile(); }
    PresetBar* getPresetBar() { return presetBar.get(); }
    PresetDialog* getPresetDialog() { return presetDialog.get(); }
   #endif

private:
   #ifdef MAS_STUDIO
    bool hosted = false, inHostedShow = false;
    std::function<void (int)> pageRequest;
   #endif
    void timerCallback() override;
    void setChoice (const char* paramID, int index);
    void updateState();
    void showPage (int index);
    void chooseIRFile();
    void applyRoomPreset (int index);

    MasterSpaceAudioProcessor& processor;
    MASLookAndFeel& lnf;
    juce::Image emblem, wordmark;

    juce::OwnedArray<juce::TextButton> algoButtons, tabButtons, routeButtons, modeButtons;
    juce::TextButton loadButton { "LOAD IR" };
    juce::Label infoLabel;
    juce::ComboBox roomPreset;
    juce::OwnedArray<ControlPage> pages;
    RoomView* roomView = nullptr;
    int currentPage = 0;
    std::unique_ptr<PresetDialog> presetDialog;
    std::unique_ptr<PresetBar> presetBar;
    std::unique_ptr<PresetBrowser> presetBrowser;

    std::unique_ptr<juce::FileChooser> chooser;
    bool shownIR = false;
    int shownAlgo = -1, shownRoute = -1;
    juce::String shownIRName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MASPanel)
};

class MasterSpaceEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget
{
public:
    explicit MasterSpaceEditor (MasterSpaceAudioProcessor&);
    ~MasterSpaceEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    MasterSpaceAudioProcessor& processor;
    MASLookAndFeel lnf;
    MASPanel panel;
    bool sizeRestored = false;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterSpaceEditor)
};
