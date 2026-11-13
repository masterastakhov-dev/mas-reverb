// MASTER SPACE STUDIO (MAS_STUDIO builds only): the studio editor.
// MAIN and the DEPTH VIEW drawer are new; the other pages (REVERB EQ, SPACE ... PRESETS) are the existing pages,
// hosted inside the studio shell and drawn with the studio look. Every control is attached to the existing
// parameters; the editor itself keeps only UI state (theme, MIX/PRO, drawer) in the plugin state properties.

#pragma once

#include "StudioLook.h"
#include "PresetPanel.h"

namespace studio
{
class MainPage;
class DepthRoom;

class Panel : public juce::Component,
              private juce::Timer
{
public:
    static constexpr int kW = 1100;
    static constexpr int kMainPage = 0;
   #if defined (MAS_EQ)
    static constexpr int kEqPage = 12, kPresetsPage = 13;
   #endif

    Panel (MasterSpaceAudioProcessor&, LookAndFeel&);
    ~Panel() override;

    int logicalHeight() const;
    std::function<void()> onHeightChange;

    void setPage (int index);                 // 0 = studio MAIN, 1..13 = the classic pages (12 REVERB EQ, 13 PRESETS)
    int getPage() const { return page; }
    void setPro (bool);
    bool isPro() const { return pro; }
    void setDrawer (bool open, bool animate = true);
    bool isDrawerOpen() const { return drawerOpen; }
    float getDrawerPosition() const { return drawerT; }
    void setTheme (int);
    void loadIRFile (const juce::File&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // geometry (logical units)
    int contentY() const { return 152 + (pro ? 36 : 0); }
   #ifdef MAS_STUDIO_DYN
    static constexpr int kContentHeight = 396;      // 1.7d: room for the SATURATION mix fader under HARMONIC COLOR
   #else
    static constexpr int kContentHeight = 380;
   #endif
    int barY() const { return contentY() + kContentHeight + 8; }

    struct Cache { juce::Image img; juce::String key; };
    static void drawCached (juce::Graphics&, Cache&, const juce::String& key, int w, int h, const std::function<void (juce::Graphics&)>& render);
    static void drawFrame (juce::Graphics&, float w, float h, bool top, bool bottom, const Theme&);

private:
    friend class MainPage;
    void timerCallback() override;
    void animateDrawer();
    void layoutDrawer();
    void renderTop (juce::Graphics&, int h);
    void updateKeys();
    void showThemeMenu();
    void saveUiState();

    MasterSpaceAudioProcessor& proc;
    LookAndFeel& lnf;
    juce::Image wordmark;
   #ifdef MAS_STUDIO_18
   public:
    static int sloganVariant;        // top-left slogan: 0 = one line (A), 1 = two lines (B) - owner's choice pending
   private:
   #endif
   #ifdef MAS_STUDIO_DYN
    juce::Image emblem;                                    // Resources/emblem.png, the original (never modified)
   #endif

    int page = 0;
    bool pro = false, drawerOpen = false;
    float drawerT = 0.0f, animFrom = 0.0f, animTo = 0.0f;
    double animStart = 0.0;
    std::unique_ptr<juce::TimedCallback> anim;

    std::unique_ptr<MASPanel> classic;                     // the existing pages, hosted
    std::unique_ptr<MainPage> main;
    std::unique_ptr<juce::Component> bar, bodyClip, body, bezel;
    DepthRoom* room = nullptr;

    juce::TextButton themeKey { "THEME" }, mixKey { "MIX" }, proKey { "PRO" }, navMain { "MAIN" }, navEq { "REVERB EQ" };
    juce::OwnedArray<juce::TextButton> tabs;
    Cache topCache;
    int themeIndex = 0;
#ifdef MAS_STUDIO_DYN
    float lampHeat = 0.12f, lampLoad = 0.0f, lampColour = 0.0f;
    double lampLastMs = 0.0;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

class Editor : public juce::AudioProcessorEditor,
               public juce::FileDragAndDropTarget
{
public:
    explicit Editor (MasterSpaceAudioProcessor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    Panel& getPanel() { return *panel; }

    static constexpr int kMinWidth = 770, kMaxWidth = 2200;

private:
    void updateSize();

    juce::SharedResourcePointer<FontProvider> fonts;
    MasterSpaceAudioProcessor& proc;
    LookAndFeel lnf;
    std::unique_ptr<Panel> panel;
    juce::TooltipWindow tips { this, 700 };
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
} // namespace studio
