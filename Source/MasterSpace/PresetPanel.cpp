#include "PresetPanel.h"
#ifdef MAS_STUDIO
 #include "StudioLook.h"
#endif

namespace
{
    juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::String star  = juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85"));
    const juce::String arrow = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92"));

    void styleEditor (juce::TextEditor& e, const juce::String& hint)
    {
        e.setFont (uiFont (14.0f));
        e.setColour (juce::TextEditor::backgroundColourId, MASColours::track);
        e.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.08f));
        e.setColour (juce::TextEditor::focusedOutlineColourId, MASColours::accent);
        e.setColour (juce::TextEditor::textColourId, MASColours::text);
        e.setTextToShowWhenEmpty (hint, MASColours::textDim);
        e.setIndents (8, 6);
        e.setJustification (juce::Justification::centredLeft);
    }

    void styleSmallLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (11.5f, true).withExtraKerningFactor (0.1f));
        l.setColour (juce::Label::textColourId, MASColours::textDim);
        l.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
PresetDialog::PresetDialog (MASLookAndFeel& l) : lnf (l)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    styleSmallLabel (nameLabel, "NAME");
    styleSmallLabel (categoryLabel, "CATEGORY");
    styleSmallLabel (authorLabel, "AUTHOR");
    styleSmallLabel (descLabel, "DESCRIPTION");
    styleEditor (nameEdit, "Preset name");
    styleEditor (authorEdit, "Your name");
    styleEditor (descEdit, "What it is for, on what material it was checked");
    descEdit.setMultiLine (true, true);
    descEdit.setJustification (juce::Justification::topLeft);
    descEdit.setReturnKeyStartsNewLine (true);
    nameEdit.onReturnKey = [this] { submit(); };
    nameEdit.onEscapeKey = [this] { close(); };
    categoryBox.addItemList (mspreset::categories(), 1);
    categoryBox.addItem (mspreset::kViewUser, mspreset::categories().size() + 1);
    for (auto* c : { (juce::Component*) &nameLabel, (juce::Component*) &categoryLabel, (juce::Component*) &authorLabel,
                     (juce::Component*) &descLabel, (juce::Component*) &nameEdit, (juce::Component*) &authorEdit,
                     (juce::Component*) &descEdit, (juce::Component*) &categoryBox, (juce::Component*) &okButton,
                     (juce::Component*) &cancelButton })
        addChildComponent (c);
    okButton.setToggleState (true, juce::dontSendNotification);   // accent colour
    okButton.setClickingTogglesState (false);
    okButton.onClick = [this] { submit(); };
    cancelButton.onClick = [this] { close(); };
    setVisible (false);
}

void PresetDialog::open (Mode m)
{
    mode = m;
    error = {};
    const bool save = m == Mode::saveAs, ren = m == Mode::rename;
    nameLabel.setVisible (save || ren);
    nameEdit.setVisible (save || ren);
    for (auto* c : { (juce::Component*) &categoryLabel, (juce::Component*) &authorLabel, (juce::Component*) &descLabel,
                     (juce::Component*) &authorEdit, (juce::Component*) &descEdit, (juce::Component*) &categoryBox })
        c->setVisible (save);
    okButton.setVisible (true);
    cancelButton.setVisible (true);
    setVisible (true);
    toFront (true);
    resized();
    repaint();
    if (save || ren) { nameEdit.grabKeyboardFocus(); nameEdit.selectAll(); }
    else grabKeyboardFocus();
}

void PresetDialog::showSaveAs (const SaveAsData& d, std::function<void (const SaveAsData&)> onSave)
{
    title = "SAVE PRESET AS";
    message = "Saved as a user preset. Factory presets are never changed.";
    nameEdit.setText (d.name, false);
    authorEdit.setText (d.author, false);
    descEdit.setText (d.description, false);
    const int idx = mspreset::categories().indexOf (d.category);
    categoryBox.setSelectedId (idx >= 0 ? idx + 1 : mspreset::categories().size() + 1, juce::dontSendNotification);
    okButton.setButtonText ("SAVE");
    saveCallback = std::move (onSave);
    open (Mode::saveAs);
}

void PresetDialog::showRename (const juce::String& currentName, std::function<void (const juce::String&)> onRename)
{
    title = "RENAME PRESET";
    message = {};
    nameEdit.setText (currentName, false);
    okButton.setButtonText ("RENAME");
    renameCallback = std::move (onRename);
    open (Mode::rename);
}

void PresetDialog::showConfirm (const juce::String& t, const juce::String& m, const juce::String& okText, std::function<void()> onOk)
{
    title = t;
    message = m;
    okButton.setButtonText (okText);
    confirmCallback = std::move (onOk);
    open (Mode::confirm);
}

void PresetDialog::setError (const juce::String& m)
{
    error = m;
    repaint();
}

void PresetDialog::close()
{
    setVisible (false);
    saveCallback = nullptr;
    renameCallback = nullptr;
    confirmCallback = nullptr;
}

void PresetDialog::submit()
{
    if (mode == Mode::saveAs && saveCallback)
    {
        SaveAsData d;
        d.name = nameEdit.getText().trim();
        d.category = categoryBox.getText();
        d.author = authorEdit.getText().trim();
        d.description = descEdit.getText().trim();
        if (d.name.isEmpty()) { setError ("Enter a name"); return; }
        auto cb = saveCallback;      // the callback may close or re-open the dialog
        cb (d);
    }
    else if (mode == Mode::rename && renameCallback)
    {
        const auto n = nameEdit.getText().trim();
        if (n.isEmpty()) { setError ("Enter a name"); return; }
        auto cb = renameCallback;
        cb (n);
    }
    else if (mode == Mode::confirm && confirmCallback)
    {
        auto cb = confirmCallback;
        close();
        cb();
    }
}

bool PresetDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey && mode == Mode::confirm) { submit(); return true; }
    return true;   // modal: keys do not reach the controls behind
}

void PresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto b = box.toFloat();
    g.setColour (MASColours::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (MASColours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (b, 10.0f, 1.2f);

    g.setColour (MASColours::text);
    g.setFont (uiFont (16.0f, true).withExtraKerningFactor (0.12f));
    g.drawText (title, box.getX() + 24, box.getY() + 16, box.getWidth() - 48, 24, juce::Justification::centredLeft);

    const int msgY = mode == Mode::confirm ? box.getY() + 52 : box.getBottom() - 92;
    const int msgH = mode == Mode::confirm ? box.getHeight() - 110 : 22;
    g.setFont (uiFont (13.0f));
    g.setColour (error.isNotEmpty() ? MASColours::accent2 : MASColours::textDim);
    g.drawFittedText (error.isNotEmpty() ? error : message, box.getX() + 24, msgY, box.getWidth() - 48, msgH,
                      mode == Mode::confirm ? juce::Justification::topLeft : juce::Justification::centredLeft, 4);
}

void PresetDialog::resized()
{
    const int w = 470;
    const int h = mode == Mode::saveAs ? 372 : (mode == Mode::rename ? 190 : 190);
    box = juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
    auto r = box.reduced (24).withTrimmedTop (34);

    if (mode == Mode::saveAs || mode == Mode::rename)
    {
        nameLabel.setBounds (r.removeFromTop (18));
        nameEdit.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
    }
    if (mode == Mode::saveAs)
    {
        auto row = r.removeFromTop (18);
        categoryLabel.setBounds (row.removeFromLeft (r.getWidth() / 2));
        authorLabel.setBounds (row.withTrimmedLeft (8));
        row = r.removeFromTop (30);
        categoryBox.setBounds (row.removeFromLeft (r.getWidth() / 2));
        authorEdit.setBounds (row.withTrimmedLeft (8));
        r.removeFromTop (8);
        descLabel.setBounds (r.removeFromTop (18));
        descEdit.setBounds (r.removeFromTop (70));
    }
    auto buttons = box.reduced (24).removeFromBottom (32);
    okButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (100));
}

//==============================================================================
PresetBar::PresetBar (MasterSpaceAudioProcessor& p, MASLookAndFeel& l, PresetDialog& d, std::function<void()> browse)
    : processor (p), presets (p.getPresetManager()), lnf (l), dialog (d), openBrowser (std::move (browse))
{
    setLookAndFeel (&lnf);
    for (auto* b : { &prevButton, &nextButton, &nameButton, &favButton, &saveButton, &saveAsButton,
                     &slotA, &slotB, &copyButton, &undoButton, &matchButton, &engineButton })
        addAndMakeVisible (b);

    nameButton.getProperties().set ("presetName", true);
    nameButton.setButtonText ({});
    favButton.setButtonText (star);

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    favButton.setTooltip ("Add to / remove from Favorites");
    saveButton.setTooltip ("Overwrite the current user preset");
    saveAsButton.setTooltip ("Save as a new user preset (name, category, author, description)");
    slotA.setTooltip ("A/B: listen to slot A");
    slotB.setTooltip ("A/B: listen to slot B (first press copies the current settings)");
    copyButton.setTooltip ("Copy the slot you hear into the other slot");
    undoButton.setTooltip ("Back to the previous variant (preset load, A/B switch or copy). Press again to redo");
    matchButton.setTooltip ("Match the loudness of B to A (pink noise, K-weighted), for a fair comparison");
    engineButton.setTooltip ("DSP engine: 1.2, or 1.1 Legacy (the sound of projects saved with 1.1)");

    prevButton.onClick = [this] { presets.step (-1); refresh(); };
    nextButton.onClick = [this] { presets.step (+1); refresh(); };
    nameButton.onClick = [this] { showPresetMenu(); };
    favButton.onClick = [this]
    {
        const auto key = presets.getCurrentKey();
        if (presets.getCurrentIndex() < 0) { flash ("Save the preset first", true); return; }
        presets.setFavorite (key, ! presets.isFavorite (key));
        refresh();
    };
    saveButton.onClick = [this]
    {
        const int i = presets.getCurrentIndex();
        if (i < 0 || presets.getPresets()[(size_t) i].readOnly()) { saveAs(); return; }
        const auto r = presets.saveCurrent();
        flash (r.message, ! r.ok);
    };
    saveAsButton.onClick = [this] { saveAs(); };
    slotA.onClick = [this] { processor.selectSlot (0); refresh(); };
    slotB.onClick = [this] { processor.selectSlot (1); refresh(); };
    copyButton.onClick = [this]
    {
        processor.copyActiveToOther();
        flash (processor.getActiveSlot() == 0 ? "A copied to B" : "B copied to A");
        refresh();
    };
    undoButton.onClick = [this] { processor.undoCompare(); refresh(); };
    matchButton.onClick = [this] { processor.setLevelMatch (! processor.getLevelMatch()); refresh(); };
    engineButton.onClick = [this] { showEngineMenu(); };

   #ifdef MAS_STUDIO
    // studio strip: compact keys; the DSP engine choice stays on the ADVANCED page
    prevButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")));
    nextButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")));
    for (auto* b : { &slotA, &slotB, &matchButton }) b->getProperties().set ("gold", true);
    for (auto* b : { &prevButton, &nextButton }) b->getProperties().set ("fontH", 15.0f);
    engineButton.setVisible (false);
   #endif
    refresh();
    startTimerHz (5);
}

PresetBar::~PresetBar()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PresetBar::saveAs()
{
    const int i = presets.getCurrentIndex();
    PresetDialog::SaveAsData d;
    if (i >= 0)
    {
        const auto& pr = presets.getPresets()[(size_t) i];
        d.name = pr.readOnly() ? pr.name + " (my version)" : pr.name;
        d.category = pr.category;
        d.description = pr.description;
    }
    else
    {
        d.name = presets.getCurrentName().isNotEmpty() && presets.getCurrentName() != "Default" ? presets.getCurrentName() : juce::String();
        d.category = mspreset::kViewUser;
    }
    d.author = presets.getDefaultAuthor();

    dialog.showSaveAs (d, [this] (const PresetDialog::SaveAsData& data)
    {
        auto doSave = [this, data] (bool overwrite)
        {
            const auto r = presets.saveAs (data.name, data.category, data.author, data.description, overwrite);
            if (r.ok) { dialog.close(); flash (r.message); refresh(); }
            else dialog.setError (r.message);
        };
        if (presets.userPresetExists (data.name, data.category))
            dialog.showConfirm ("REPLACE PRESET?", "A user preset \"" + data.name + "\" already exists in " + data.category
                                                   + ". Replace it with the current settings?", "REPLACE",
                                [doSave] { doSave (true); });
        else
            doSave (false);
    });
}

void PresetBar::flash (const juce::String& m, bool isError)
{
    flashText = m;
    flashError = isError;
    flashTicks = 15;   // 3 s
    repaint();
}

void PresetBar::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& all = presets.getPresets();
    const auto current = presets.getCurrentKey();
    int id = 1;
    std::vector<int> ids;   // menu id -> preset index

    auto addItems = [&] (juce::PopupMenu& m, const std::vector<int>& list, bool showCategory)
    {
        for (int idx : list)
        {
            const auto& pr = all[(size_t) idx];
            juce::String text = pr.name;
            if (showCategory) text << "   (" << pr.category << ")";
            if (pr.engineName() != "1.2") text << "   [" << pr.engineName() << "]";
            m.addItem (id++, text, true, pr.key == current);
            ids.push_back (idx);
        }
    };

    const auto favs = presets.find (mspreset::kViewFavorites, {});
    if (! favs.empty())
    {
        juce::PopupMenu sub;
        addItems (sub, favs, true);
        menu.addSubMenu (star + " Favorites", sub);
        menu.addSeparator();
    }
    for (const auto& cat : mspreset::categories())
    {
        const auto list = presets.find (cat, {});
        juce::PopupMenu sub;
        addItems (sub, list, false);
        if (list.empty())
            sub.addItem (-1, cat == "Plates" ? "No plate algorithm yet" : "Empty: save your own here", false, false);
        menu.addSubMenu (cat + "  (" + juce::String ((int) list.size()) + ")", sub, true);
    }
    {
        const auto list = presets.find (mspreset::kViewUser, {});
        juce::PopupMenu sub;
        addItems (sub, list, true);
        if (list.empty()) sub.addItem (-1, "No user presets yet", false, false);
        menu.addSubMenu (juce::String (mspreset::kViewUser) + "  (" + juce::String ((int) list.size()) + ")", sub, true);
    }
    menu.addSeparator();
    const int defaultId = 100000, browseId = 100001;
    menu.addItem (defaultId, "Default settings");
    menu.addItem (browseId, "Open preset browser...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton).withMinimumWidth (nameButton.getWidth()),
                        [this, ids, defaultId, browseId] (int result)
                        {
                            if (result == defaultId) { presets.loadDefaults(); refresh(); }
                            else if (result == browseId) { if (openBrowser) openBrowser(); }
                            else if (result >= 1 && result <= (int) ids.size())
                            {
                                const auto r = presets.load (ids[(size_t) result - 1]);
                                if (! r.ok) flash (r.message, true);
                                refresh();
                            }
                        });
}

void PresetBar::showEngineMenu()
{
    const bool legacy = processor.isLegacyEngine();
    juce::PopupMenu menu;
    menu.addSectionHeader ("DSP ENGINE");
    menu.addItem (2, "1.2  (current)", true, ! legacy);
    menu.addItem (1, "1.1 Legacy  (sound of projects saved with 1.1)", true, legacy);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&engineButton), [this] (int r)
    {
        if (r <= 0) return;
        if (auto* prm = processor.apvts.getParameter (ParamIDs::engine))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (r - 1)));
            prm->endChangeGesture();
        }
        refresh();
    });
}

void PresetBar::refresh()
{
    const auto name = presets.getCurrentName();
    const int idx = presets.getCurrentIndex();
    shownName = name.isNotEmpty() ? name : juce::String ("No preset");
    shownCategory = idx >= 0 ? presets.getPresets()[(size_t) idx].category.toUpperCase() : juce::String ("PRESET");
    shownModified = presets.isCurrentModified();
   #ifdef MAS_STUDIO_18
    {
        juce::String tip = shownName + "\n" + shownCategory;
        if (idx >= 0 && presets.getPresets()[(size_t) idx].description.isNotEmpty()) tip << "\n" << presets.getPresets()[(size_t) idx].description;
        nameButton.setTooltip (tip + "\nClick: all presets");
    }
   #else
    nameButton.setTooltip (idx >= 0 ? presets.getPresets()[(size_t) idx].description : juce::String());
   #endif

    const bool fav = idx >= 0 && presets.isFavorite (presets.getCurrentKey());
    favButton.setToggleState (fav, juce::dontSendNotification);
    const bool readOnly = idx < 0 || presets.getPresets()[(size_t) idx].readOnly();
    saveButton.setEnabled (! readOnly);

    const int slot = processor.getActiveSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "A" + arrow + "B" : "B" + arrow + "A");
    undoButton.setEnabled (processor.canUndoCompare());
    matchButton.setToggleState (processor.getLevelMatch(), juce::dontSendNotification);
    matchButton.setButtonText (processor.getLevelMatch() && processor.getMatchStatus().startsWith ("B ")
                                   ? processor.getMatchStatus() : juce::String ("MATCH"));
    matchButton.setTooltip (processor.getLevelMatch() ? processor.getMatchStatus()
                                                      : "Match the loudness of B to A (pink noise, K-weighted), for a fair comparison");

    const bool legacy = processor.isLegacyEngine();
    engineButton.setButtonText (legacy ? "1.1 LEGACY" : "ENGINE 1.2");
    engineButton.setToggleState (legacy, juce::dontSendNotification);
    repaint();
}

void PresetBar::timerCallback()
{
    if (flashTicks > 0 && --flashTicks == 0) repaint();
    if (processor.getLevelMatch() && ++matchTicks >= 2) { matchTicks = 0; processor.refreshLevelMatch(); }
    refresh();
}

void PresetBar::paint (juce::Graphics& g)
{
   #ifdef MAS_STUDIO
    {
        const auto& th = studio::themeOf (*this);
        const auto r = nameButton.getBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff07090d), 0, r.getY(), juce::Colour (0xff0d1117), 0, r.getBottom(), false);
        g.setGradientFill (bg); g.fillRoundedRectangle (r, 6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0, r.getY(), juce::Colours::white.withAlpha (nameButton.isMouseOver() ? 0.16f : 0.07f), 0, r.getBottom(), false));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        auto t = nameButton.getBounds().reduced (12, 0);
        if (flashTicks > 0)
        {
            g.setColour (flashError ? juce::Colour (0xffff7a6a) : juce::Colour (0xffe8eef6));
            g.setFont (studio::inter (12.5f, true));
            g.drawFittedText (flashText, t, juce::Justification::centredLeft, 1);
        }
        else
        {
           #ifdef MAS_STUDIO_18
            // the preset NAME is what matters: large and readable; the category lives in the tooltip and the browser
            g.setColour (juce::Colour (0xffeef2f8));
            g.setFont (studio::inter (13.5f, true, 0.02f));
            g.drawFittedText (shownName + (shownModified ? " *" : ""), t.withTrimmedRight (18), juce::Justification::centredLeft, 1, 0.92f);
           #else
            const auto cf = studio::inter (9.0f, true, 0.16f);
            g.setFont (cf);
            g.setColour (th.gold.withAlpha (0.85f));
            const int cw = juce::jmin (110, juce::GlyphArrangement::getStringWidthInt (cf, shownCategory.toUpperCase()) + 10);
            g.drawText (shownCategory.toUpperCase(), t.removeFromLeft (cw), juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffe8eef6));
            g.setFont (studio::inter (12.5f));
            g.drawFittedText (shownName + (shownModified ? " *" : ""), t.withTrimmedRight (16), juce::Justification::centredLeft, 1);
           #endif
        }
        juce::Path chev;
        const float ax = r.getRight() - 13.0f, ay = r.getCentreY();
        chev.startNewSubPath (ax - 4.0f, ay - 2.0f); chev.lineTo (ax, ay + 2.0f); chev.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (juce::Colour (0xff8a95a6));
        g.strokePath (chev, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
   #endif
    // preset name field (drawn over the transparent name button)
    const auto r = nameButton.getBounds().toFloat();
    g.setColour (MASColours::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (nameButton.isMouseOver() ? 0.16f : 0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    auto t = nameButton.getBounds().reduced (10, 0);
    const bool showFlash = flashTicks > 0;
    if (showFlash)
    {
        g.setColour (flashError ? MASColours::accent2 : MASColours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawFittedText (flashText, t, juce::Justification::centredLeft, 1);
    }
    else
    {
        g.setColour (MASColours::textDim);
        g.setFont (uiFont (10.5f, true).withExtraKerningFactor (0.12f));
        const int cw = juce::jmin (150, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), shownCategory) + 14);
        g.drawText (shownCategory, t.removeFromLeft (cw), juce::Justification::centredLeft);
        g.setColour (MASColours::text);
        g.setFont (uiFont (15.0f, true));
        g.drawFittedText (shownName + (shownModified ? " *" : ""), t.withTrimmedRight (18), juce::Justification::centredLeft, 1);
    }
    juce::Path arrowPath;
    const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
    arrowPath.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (MASColours::textDim);
    g.fillPath (arrowPath);

    // separators
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (auto* b : { &slotA, &engineButton })
        g.fillRect ((float) b->getX() - 9.0f, 6.0f, 1.0f, (float) getHeight() - 12.0f);
}

#ifdef MAS_STUDIO_18
static constexpr int kNameW18 = 290;
static const int kToolW18[]   = { 26, 48, 62, 26, 26, 40, 50, 54 };
static const int kToolGap18[] = { 4, 4, 12, 4, 4, 4, 4, 0 };

int PresetBar::toolsWidth() const
{
    int w = 0;
    for (int i = 0; i < 8; ++i) w += kToolW18[i] + kToolGap18[i];
    return w;
}

void PresetBar::placeTools (juce::Component& host, int x, int y, int h)
{
    juce::TextButton* tools[] = { &favButton, &saveButton, &saveAsButton, &slotA, &slotB, &copyButton, &undoButton, &matchButton };
    for (int i = 0; i < 8; ++i)
    {
        if (tools[i]->getParentComponent() != &host) host.addAndMakeVisible (*tools[i]);
        tools[i]->setBounds (x, y, kToolW18[i], h);
        x += kToolW18[i] + kToolGap18[i];
    }
}
#endif

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = r.getHeight();
    auto take = [&r, h] (int w, int gap = 6) { auto b = r.removeFromLeft (w).withHeight (h); r.removeFromLeft (gap); return b; };
   #ifdef MAS_STUDIO_18
    prevButton.setBounds (take (28, 4)); nameButton.setBounds (take (kNameW18, 4)); nextButton.setBounds (take (28, 0));
    return;
   #endif
   #ifdef MAS_STUDIO
    prevButton.setBounds (take (28, 4)); nameButton.setBounds (take (200, 4)); nextButton.setBounds (take (28, 8));
    favButton.setBounds (take (28)); saveButton.setBounds (take (54)); saveAsButton.setBounds (take (66, 14));
    slotA.setBounds (take (32, 4)); slotB.setBounds (take (32)); copyButton.setBounds (take (48));
    undoButton.setBounds (take (54)); matchButton.setBounds (take (62, 0));
    return;
   #endif

    prevButton.setBounds (take (28, 4));
    nameButton.setBounds (take (300, 4));
    nextButton.setBounds (take (28, 8));
    favButton.setBounds (take (30));
    saveButton.setBounds (take (58));
    saveAsButton.setBounds (take (72, 18));

    slotA.setBounds (take (34, 4));
    slotB.setBounds (take (34));
    copyButton.setBounds (take (54));
    undoButton.setBounds (take (54));
    matchButton.setBounds (take (82, 18));

    engineButton.setBounds (r.removeFromRight (96));
}

//==============================================================================
PresetBrowser::PresetBrowser (MasterSpaceAudioProcessor& p, MASLookAndFeel& l, PresetDialog& d, PresetBar& b)
    : processor (p), presets (p.getPresetManager()), lnf (l), dialog (d), bar (b)
{
    setLookAndFeel (&lnf);
    views.add (mspreset::kViewAll);
    views.add (mspreset::kViewFavorites);
    views.addArray (mspreset::categories());
    views.add (mspreset::kViewUser);

    for (auto* lb : { &categoryList, &presetList })
    {
        lb->setColour (juce::ListBox::backgroundColourId, MASColours::panel);
        lb->setColour (juce::ListBox::outlineColourId, juce::Colours::white.withAlpha (0.06f));
        lb->setOutlineThickness (1);
        lb->setRowHeight (28);
        addAndMakeVisible (lb);
    }
    presetList.setRowHeight (30);

    styleEditor (searchBox, "Search name, category, author, description");
    searchBox.onTextChange = [this] { rebuild (true); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); rebuild (true); };
    addAndMakeVisible (searchBox);

    for (auto* lab : { &detailName, &detailMeta, &detailDesc, &detailNotes, &status })
    {
        lab->setJustificationType (juce::Justification::topLeft);
        lab->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (lab);
    }
    detailName.setFont (uiFont (18.0f, true));
    detailName.setColour (juce::Label::textColourId, MASColours::text);
    detailMeta.setFont (uiFont (12.0f, true).withExtraKerningFactor (0.06f));
    detailMeta.setColour (juce::Label::textColourId, MASColours::textDim);
    detailDesc.setFont (uiFont (13.5f));
    detailDesc.setColour (juce::Label::textColourId, MASColours::text);
    detailNotes.setFont (uiFont (12.0f));
    detailNotes.setColour (juce::Label::textColourId, MASColours::accent2);
    status.setFont (uiFont (12.5f));
    status.setColour (juce::Label::textColourId, MASColours::textDim);
    status.setJustificationType (juce::Justification::centredLeft);

    for (auto* btn : { &loadButton, &saveAsButton, &renameButton, &duplicateButton, &deleteButton, &favButton,
                       &importButton, &exportButton, &folderButton, &rescanButton })
        addAndMakeVisible (btn);
    loadButton.setToggleState (true, juce::dontSendNotification);

    loadButton.onClick = [this] { loadSelected(); };
    saveAsButton.onClick = [this] { bar.saveAs(); };
    renameButton.onClick = [this]
    {
        const int i = selectedPreset();
        if (i < 0) return;
        const auto key = presets.getPresets()[(size_t) i].key;
        dialog.showRename (presets.getPresets()[(size_t) i].name, [this, key] (const juce::String& n)
        {
            const auto r = presets.rename (presets.indexOf (key), n);
            if (r.ok) { dialog.close(); show (r); } else dialog.setError (r.message);
        });
    };
    duplicateButton.onClick = [this] { show (presets.duplicate (selectedPreset())); };
    deleteButton.onClick = [this]
    {
        const int i = selectedPreset();
        if (i < 0) return;
        const auto key = presets.getPresets()[(size_t) i].key;
        dialog.showConfirm ("DELETE PRESET?", "\"" + presets.getPresets()[(size_t) i].name
                                                + "\" will be moved to the Trash.", "DELETE",
                            [this, key] { show (presets.remove (presets.indexOf (key))); });
    };
    favButton.onClick = [this]
    {
        const int i = selectedPreset();
        if (i < 0) return;
        const auto key = presets.getPresets()[(size_t) i].key;
        presets.setFavorite (key, ! presets.isFavorite (key));
        rebuild (true);
    };
    importButton.onClick = [this] { importPresets(); };
    exportButton.onClick = [this] { exportSelected(); };
    folderButton.onClick = [this]
    {
        const auto f = presets.getUserFolder();
        f.createDirectory();
        f.revealToUser();
    };
    rescanButton.onClick = [this] { presets.rescan(); show (mspreset::Result::success ("Library re-read")); };

    rebuild (false);
    startTimerHz (4);
}

PresetBrowser::~PresetBrowser()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        rebuild (true);
        const int cur = presets.getCurrentIndex();
        const auto it = std::find (visible.begin(), visible.end(), cur);
        if (it != visible.end())
        {
            presetList.selectRow ((int) (it - visible.begin()));
            presetList.scrollToEnsureRowIsOnscreen ((int) (it - visible.begin()));
        }
    }
}

int PresetBrowser::selectedPreset() const
{
    const int row = presetList.getSelectedRow();
    return row >= 0 && row < (int) visible.size() ? visible[(size_t) row] : -1;
}

void PresetBrowser::rebuild (bool keepSelection)
{
    const auto selKey = keepSelection && selectedPreset() >= 0 ? presets.getPresets()[(size_t) selectedPreset()].key : juce::String();
    visible = presets.find (view, searchBox.getText());
    categoryList.updateContent();
    categoryList.selectRow (views.indexOf (view), true, true);
    presetList.updateContent();
    int row = -1;
    if (selKey.isNotEmpty())
        for (int i = 0; i < (int) visible.size(); ++i)
            if (presets.getPresets()[(size_t) visible[(size_t) i]].key == selKey) row = i;
    if (row >= 0) presetList.selectRow (row, true, true);
    else presetList.deselectAllRows();
    shownChange = presets.getChangeCount();
    shownCurrentKey = presets.getCurrentKey();
    presetList.repaint();
    updateDetails();
}

void PresetBrowser::updateDetails()
{
    const int i = selectedPreset();
    const bool has = i >= 0;
    const mspreset::Preset* pr = has ? &presets.getPresets()[(size_t) i] : nullptr;

    detailName.setText (has ? pr->name : juce::String ("Select a preset"), juce::dontSendNotification);
    juce::String meta;
    if (has)
    {
        meta << pr->category.toUpperCase() << "   " << "ENGINE " << pr->engineName().toUpperCase() << "\n"
             << (pr->author.isNotEmpty() ? "by " + pr->author : juce::String ("no author")) << "   "
             << (pr->source == mspreset::Source::factory ? "FACTORY (protected)"
                 : pr->source == mspreset::Source::collection ? "COLLECTION (protected)" : "USER");
    }
    detailMeta.setText (meta, juce::dontSendNotification);
    detailDesc.setText (has ? pr->description : juce::String ("Double-click a preset to load it. Factory presets are protected: "
                                                              "SAVE AS or DUPLICATE makes your own version."),
                        juce::dontSendNotification);
    detailNotes.setText (has ? pr->notes.joinIntoString ("\n") : juce::String(), juce::dontSendNotification);

    const bool user = has && ! pr->readOnly();
    loadButton.setEnabled (has);
    renameButton.setEnabled (user);
    deleteButton.setEnabled (user);
    duplicateButton.setEnabled (has);
    exportButton.setEnabled (true);
    favButton.setEnabled (has);
    favButton.setToggleState (has && presets.isFavorite (pr->key), juce::dontSendNotification);
    favButton.setButtonText (has && presets.isFavorite (pr->key) ? star + " FAVORITE" : "FAVORITE");
}

void PresetBrowser::loadSelected()
{
    const int i = selectedPreset();
    if (i < 0) return;
    show (presets.load (i));
}

void PresetBrowser::show (const mspreset::Result& r)
{
    status.setText (r.message, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, r.ok ? MASColours::textDim : MASColours::accent2);
    rebuild (true);
}

void PresetBrowser::importPresets()
{
    chooser = std::make_unique<juce::FileChooser> ("Import MASTER SPACE presets",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResults().isEmpty()) return;
                              show (presets.importFiles (fc.getResults()));
                          });
}

void PresetBrowser::exportSelected()
{
    const int i = selectedPreset();
    const auto name = i >= 0 ? presets.getPresets()[(size_t) i].name
                             : (presets.getCurrentName().isNotEmpty() ? presets.getCurrentName() : juce::String ("MASTER SPACE preset"));
    chooser = std::make_unique<juce::FileChooser> ("Export preset",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile (juce::File::createLegalFileName (name) + ".json"),
                                                   "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, i] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("json")) f = f.withFileExtension ("json");
                              show (i >= 0 ? presets.exportPreset (i, f) : presets.exportCurrent (f));
                          });
}

void PresetBrowser::timerCallback()
{
    if (presets.getChangeCount() != shownChange || presets.getCurrentKey() != shownCurrentKey)
        rebuild (true);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (MASColours::panel.withAlpha (0.6f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    const auto& probs = presets.getProblems();
    if (! probs.isEmpty())
    {
        g.setColour (MASColours::accent2);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (juce::String (probs.size()) + " file(s) could not be read: " + probs.joinIntoString ("; "),
                          getLocalBounds().reduced (14, 6).removeFromBottom (16), juce::Justification::centredLeft, 1);
    }
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    r.removeFromBottom (18);   // problems line
    auto left = r.removeFromLeft (196);
    r.removeFromLeft (12);
    auto right = r.removeFromRight (300);
    r.removeFromRight (12);

    categoryList.setBounds (left);
    searchBox.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    status.setBounds (r.removeFromBottom (22));
    presetList.setBounds (r);

    auto d = right.reduced (4, 0);
    detailName.setBounds (d.removeFromTop (28));
    detailMeta.setBounds (d.removeFromTop (36));
    d.removeFromTop (4);
    auto buttons = d.removeFromBottom (5 * 30 + 4 * 6);
    detailNotes.setBounds (d.removeFromBottom (48));
    detailDesc.setBounds (d);

    const int bw = (buttons.getWidth() - 6) / 2;
    juce::TextButton* grid[5][2] = { { &loadButton, &saveAsButton }, { &renameButton, &duplicateButton },
                                     { &favButton, &deleteButton }, { &importButton, &exportButton },
                                     { &folderButton, &rescanButton } };
    for (int row = 0; row < 5; ++row)
    {
        auto line = buttons.removeFromTop (30);
        buttons.removeFromTop (6);
        grid[row][0]->setBounds (line.removeFromLeft (bw));
        grid[row][1]->setBounds (line.removeFromRight (bw));
    }
}

//==============================================================================
int PresetBrowser::CategoryModel::getNumRows() { return owner.views.size(); }

void PresetBrowser::CategoryModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= owner.views.size()) return;
    const auto& v = owner.views[row];
    if (selected)
    {
        g.setColour (MASColours::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 2.0f, (float) w - 6.0f, (float) h - 4.0f), 4.0f);
    }
    const int n = owner.presets.countIn (v);
    const bool separatorAbove = v == mspreset::categories()[0] || v == mspreset::kViewUser;
    if (separatorAbove)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (8, 0, w - 16, 1);
    }
    g.setColour (selected ? juce::Colours::white : (n > 0 ? MASColours::text : MASColours::textDim));
    g.setFont (uiFont (13.0f, selected));
    const juce::String label = v == mspreset::kViewFavorites ? star + " " + v : v;
    g.drawText (label, 12, 0, w - 50, h, juce::Justification::centredLeft);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : MASColours::textDim);
    g.setFont (uiFont (11.5f));
    g.drawText (juce::String (n), w - 40, 0, 30, h, juce::Justification::centredRight);
}

void PresetBrowser::CategoryModel::selectedRowsChanged (int lastRow)
{
    if (lastRow < 0 || lastRow >= owner.views.size() || owner.views[lastRow] == owner.view) return;
    owner.view = owner.views[lastRow];
    owner.rebuild (true);
}

int PresetBrowser::PresetModel::getNumRows() { return (int) owner.visible.size(); }

void PresetBrowser::PresetModel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) owner.visible.size()) return;
    const auto& pr = owner.presets.getPresets()[(size_t) owner.visible[(size_t) row]];
    const bool current = pr.key == owner.presets.getCurrentKey();
    if (selected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 2.0f, (float) w - 6.0f, (float) h - 4.0f), 4.0f);
    }
    if (current)
    {
        g.setColour (MASColours::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 7.0f, 3.0f, (float) h - 14.0f), 1.5f);
    }
    const bool fav = owner.presets.isFavorite (pr.key);
    g.setFont (uiFont (13.0f));
    g.setColour (fav ? MASColours::accent2 : MASColours::textDim.withAlpha (0.35f));
    g.drawText (star, 12, 0, 18, h, juce::Justification::centred);

    g.setColour (current ? juce::Colours::white : MASColours::text);
    g.setFont (uiFont (14.0f, current));
    const bool showCat = owner.view == mspreset::kViewAll || owner.view == mspreset::kViewFavorites || owner.view == mspreset::kViewUser;
    const int tagW = 150;
    g.drawFittedText (pr.name, 36, 0, w - 36 - tagW, h, juce::Justification::centredLeft, 1);

    juce::String tag = showCat ? pr.category : juce::String();
    if (pr.engineName() != "1.2") tag = (tag.isNotEmpty() ? tag + "  " : juce::String()) + "1.1 LEGACY";
    if (pr.source == mspreset::Source::user) tag = (tag.isNotEmpty() ? tag + "  " : juce::String()) + "USER";
    g.setColour (MASColours::textDim);
    g.setFont (uiFont (10.5f, true));
    g.drawFittedText (tag.toUpperCase(), w - tagW, 0, tagW - 10, h, juce::Justification::centredRight, 1);
}

void PresetBrowser::PresetModel::selectedRowsChanged (int) { owner.updateDetails(); }
void PresetBrowser::PresetModel::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { owner.loadSelected(); }
void PresetBrowser::PresetModel::returnKeyPressed (int) { owner.loadSelected(); }
