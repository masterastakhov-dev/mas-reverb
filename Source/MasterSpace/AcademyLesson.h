#pragma once

// A separate model window, owned by the editor. Its controls never bind to the APVTS.
class AcademyLesson final : public juce::Component
{
public:
    explicit AcademyLesson (AcademyPlayer& transport) : player (transport)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "MASTER SPACE 2.0 ACADEMY";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        prefs = std::make_unique<juce::PropertiesFile> (options);
        russian = prefs->getBoolValue ("russian", true);
        complete = prefs->getBoolValue ("predelayComplete", false);
        for (auto* button : { &language, &play, &stop, &dry, &shortTime, &longTime, &quiz })
            addAndMakeVisible (*button);
        addAndMakeVisible (slider);
        slider.setRange (0, 80, 1);
        slider.setTextValueSuffix (" ms");
        slider.setValue (player.delayMs.load(), juce::dontSendNotification);
        slider.onValueChange = [this] { player.delayMs = int (slider.getValue()); repaint(); };
        language.onClick = [this] { russian = !russian; prefs->setValue ("russian", russian); refresh(); };
        play.onClick = [this] { player.play(); };
        stop.onClick = [this] { player.stop(); };
        dry.onClick = [this] { player.dryOnly = !player.dryOnly.load(); refresh(); };
        shortTime.onClick = [this] { choose (5); };
        longTime.onClick = [this] { choose (45); };
        quiz.onClick = [this]
        {
            player.stop();
            blind = !blind; feedback = 0; player.dryOnly = false;
            const int value = blind && juce::Random::getSystemRandom().nextBool() ? 45 : 5;
            slider.setValue (value, juce::sendNotificationSync);
            refresh();
        };
        setSize (560, 332);
        refresh();
    }

    void resized() override
    {
        language.setBounds (456, 16, 82, 28);
        slider.setBounds (23, 158, 512, 32);
        shortTime.setBounds (24, 202, 94, 32); longTime.setBounds (124, 202, 94, 32);
        dry.setBounds (226, 202, 134, 32); quiz.setBounds (368, 202, 169, 32);
        play.setBounds (24, 272, 255, 38); stop.setBounds (288, 272, 248, 38);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a2029));
        g.setColour (juce::Colours::white); g.setFont (22.f);
        g.drawText (tr ("Малый барабан", "Snare drum") + "  /  PRE-DELAY", 24, 13, 420, 36, juce::Justification::centredLeft);
        g.setFont (16.f);
        auto guide = blind ? tr ("Прослушайте пример и выберите задержку на слух.", "Listen, then identify the pre-delay by ear.")
                          : tr ("Слушайте паузу между атакой малого и началом отражений.", "Listen for the gap between the snare attack and the reflections.");
        g.drawFittedText (guide, 24, 52, 510, 44, juce::Justification::centredLeft, 2);
        if (!blind)
        {
            const int x = 62 + 5 * player.delayMs.load();
            g.setColour (juce::Colour (0xff80c1ed)); g.fillRect (48, 104, 3, 28);
            g.setColour (juce::Colour (0xffe6b669));
            for (int i = 0; i < 7; ++i) g.fillRect (x + i * 6, 106+i*2, 2, 24-i*3);
            g.setFont (13.f); g.drawText (tr ("Атака → отражения", "Attack → reflections"), 26, 132, 500, 22, juce::Justification::centredLeft);
        }
        auto status = feedback == 1 ? tr ("Верно. Урок пройден.", "Correct. Lesson complete.")
                    : feedback == 2 ? tr ("Попробуйте ещё раз.", "Listen again and retry.")
                    : complete ? tr ("Урок пройден", "Lesson complete") : juce::String();
        g.drawText (status, 24, 239, 510, 26, juce::Justification::centredLeft);
    }
private:
    AcademyPlayer& player;
    std::unique_ptr<juce::PropertiesFile> prefs;
    juce::TextButton language, play, stop, dry, shortTime { "5 ms" }, longTime { "45 ms" }, quiz;
    juce::Slider slider;
    bool russian = true, complete = false, blind = false;
    int feedback = 0;
    juce::String tr (const char* ru, const char* en) const { return juce::String::fromUTF8 (russian ? ru : en); }
    void choose (int value)
    {
        if (!blind) { player.dryOnly = false; slider.setValue (value, juce::sendNotificationSync); }
        else if (player.delayMs == value) { feedback = 1; complete = true; prefs->setValue ("predelayComplete", true); }
        else feedback = 2;
        refresh();
    }
    void refresh()
    {
        language.setButtonText (russian ? "English" : juce::String::fromUTF8 ("Русский"));
        play.setButtonText (tr ("Слушать через выход AU", "Play through AU output"));
        stop.setButtonText (tr ("Стоп", "Stop"));
        dry.setButtonText (player.dryOnly ? tr ("Сухой звук", "Dry only") : tr ("С отражениями", "With reflections"));
        quiz.setButtonText (blind ? tr ("Вернуться к уроку", "Back to lesson") : tr ("Проверить на слух", "Ear test"));
        slider.setVisible (!blind); dry.setEnabled (!blind); repaint();
    }
};

class AcademyWindow final : public juce::DocumentWindow
{
public:
    AcademyWindow (AcademyPlayer& t, juce::Component& launcher)
        : DocumentWindow ("SPACE ACADEMY", juce::Colour (0xff1a2029), closeButton), player (t)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new AcademyLesson (player), true);
        const auto anchor = launcher.getScreenBounds();
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (anchor))
            setBounds (getLocalBounds().withCentre (anchor.getCentre()).constrainedWithin (display->userArea));
    }
    void closeButtonPressed() override { player.stop(); setVisible (false); }
private:
    AcademyPlayer& player;
};
