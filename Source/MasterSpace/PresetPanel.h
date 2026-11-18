#pragma once

// Preset UI: the quick bar at the top of the window (preset selector, save, A/B, level match,
// DSP engine badge), the PRESETS page (browser) and the in-window dialog used by both.

#include "PluginEditor.h"
#include "PresetManager.h"

//==============================================================================
// Modal overlay inside the plugin window (no native dialogs: works the same in every host).
class PresetDialog : public juce::Component
{
public:
    explicit PresetDialog (MASLookAndFeel&);

    struct SaveAsData { juce::String name, category, author, description; };
    void showSaveAs (const SaveAsData& initial, std::function<void (const SaveAsData&)> onSave);
    void showRename (const juce::String& currentName, std::function<void (const juce::String&)> onRename);
    void showConfirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                      std::function<void()> onOk);
    void setError (const juce::String& message);
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Mode { saveAs, rename, confirm };
    void open (Mode);
    void submit();

    MASLookAndFeel& lnf;
    Mode mode = Mode::saveAs;
    juce::String title, message, error;
    juce::Label nameLabel, categoryLabel, authorLabel, descLabel;
    juce::TextEditor nameEdit, authorEdit, descEdit;
    juce::ComboBox categoryBox;
    juce::TextButton okButton { "SAVE" }, cancelButton { "CANCEL" };
    std::function<void (const SaveAsData&)> saveCallback;
    std::function<void (const juce::String&)> renameCallback;
    std::function<void()> confirmCallback;
    juce::Rectangle<int> box;
};

//==============================================================================
class PresetBar : public juce::Component,
                  private juce::Timer
{
public:
    PresetBar (MasterSpaceAudioProcessor&, MASLookAndFeel&, PresetDialog&, std::function<void()> openBrowser);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void saveAs();     // also used by the browser
    void flash (const juce::String& message, bool isError = false);
   #ifdef MAS_STUDIO_18
    // 1.8 layout: the preset field (< NAME >) stays in this bar; the preset tools (favourite, SAVE, SAVE AS,
    // A, B, A->B, UNDO, MATCH) are placed by the host: in the bottom strip on MAIN, next to the name elsewhere.
    // Moving them changes nothing in what they do.
    void placeTools (juce::Component& host, int x, int y, int h);
    int toolsWidth() const;
   #endif

private:
    void timerCallback() override;
    void refresh();
    void showPresetMenu();
    void showEngineMenu();

    MasterSpaceAudioProcessor& processor;
    mspreset::PresetManager& presets;
    MASLookAndFeel& lnf;
    PresetDialog& dialog;
    std::function<void()> openBrowser;

    juce::TextButton prevButton { "<" }, nextButton { ">" }, nameButton, favButton, saveButton { "SAVE" }, saveAsButton { "SAVE AS" };
    juce::TextButton slotA { "A" }, slotB { "B" }, copyButton, undoButton { "UNDO" }, matchButton { "MATCH" }, engineButton;
    juce::String shownName, shownCategory, flashText;
    bool shownModified = false, flashError = false;
    int flashTicks = 0, matchTicks = 0;
};

//==============================================================================
class PresetBrowser : public juce::Component,
                      private juce::Timer
{
public:
    PresetBrowser (MasterSpaceAudioProcessor&, MASLookAndFeel&, PresetDialog&, PresetBar&);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    struct CategoryModel : juce::ListBoxModel
    {
        PresetBrowser& owner;
        explicit CategoryModel (PresetBrowser& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int lastRow) override;
    };
    struct PresetModel : juce::ListBoxModel
    {
        PresetBrowser& owner;
        explicit PresetModel (PresetBrowser& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int lastRow) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
    };

    void timerCallback() override;
    void rebuild (bool keepSelection);
    void updateDetails();
    int selectedPreset() const;
    void loadSelected();
    void show (const mspreset::Result&);
    void importPresets();
    void exportSelected();

    MasterSpaceAudioProcessor& processor;
    mspreset::PresetManager& presets;
    MASLookAndFeel& lnf;
    PresetDialog& dialog;
    PresetBar& bar;

    juce::StringArray views;
    juce::String view { mspreset::kViewAll };
    std::vector<int> visible;
    CategoryModel categoryModel { *this };
    PresetModel presetModel { *this };
    juce::ListBox categoryList { "categories", &categoryModel }, presetList { "presets", &presetModel };
    juce::TextEditor searchBox;
    juce::Label detailName, detailMeta, detailDesc, detailNotes, status;
    juce::TextButton loadButton { "LOAD" }, saveAsButton { "SAVE AS" }, renameButton { "RENAME" }, duplicateButton { "DUPLICATE" },
                     deleteButton { "DELETE" }, favButton { "FAVORITE" }, importButton { "IMPORT" }, exportButton { "EXPORT" },
                     folderButton { "FOLDER" }, rescanButton { "RESCAN" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::uint32 shownChange = 0;
    juce::String shownCurrentKey;
};
