#pragma once

// MASTER SPACE preset library (message thread only; no file access from the audio thread).
//
// Preset file format (JSON, documented in docs/PRESET_FORMAT.md):
//   {
//     "format": "MASTER SPACE Preset",
//     "schemaVersion": 1,
//     "pluginVersion": "1.2.0",
//     "name": "...", "category": "...", "author": "...", "description": "...",
//     "engine": "1.2",                         // informational copy of parameters.engine
//     "parameters": { "<parameter id>": value, ... },
//     "irFile": "/path/to/impulse.wav"          // optional, IR mode only
//   }
// Values are plain (as shown in the plugin): numbers for continuous parameters, text for choices
// ("Studio Room", "Plaster", "1.2"), true / false for switches. Missing parameters take their
// default value, unknown ones are ignored, out-of-range values are clamped.
//
// Sources: factory presets (compiled into the plugin, read-only), collections (read-only JSON files
// in <root>/Collections, e.g. a Signature Collection library), user presets (<root>/User).

#include "PluginProcessor.h"

namespace mspreset
{
    inline constexpr int kSchemaVersion = 1;
    inline constexpr const char* kFormat = "MASTER SPACE Preset";

    enum class Source { factory, collection, user };

    struct Preset
    {
        juce::String key;                 // "factory:Category/Name", "collection:<path>", "user:<path>"
        juce::String name, category, author, description, pluginVersion;
        int schemaVersion = kSchemaVersion;
        Source source = Source::user;
        juce::File file;                  // collection / user presets
        ParamMap values;                  // complete and validated
        juce::String irFile;
        juce::StringArray notes;          // what migration had to do (shown in the browser)

        bool readOnly() const   { return source != Source::user; }
        juce::String engineName() const;
    };

    struct Result
    {
        bool ok = true;
        juce::String message;
        static Result success (const juce::String& m = {}) { return { true, m }; }
        static Result fail (const juce::String& m)         { return { false, m }; }
    };

    // the content categories, in display order (views add "All", "Favorites", "User Presets")
    const juce::StringArray& categories();
    inline const char* kViewAll = "All";
    inline const char* kViewFavorites = "Favorites";
    inline const char* kViewUser = "User Presets";

    class PresetManager
    {
    public:
        explicit PresetManager (MasterSpaceAudioProcessor&);

        void rescan();
        const std::vector<Preset>& getPresets() const { return presets; }
        std::vector<int> find (const juce::String& view, const juce::String& search) const;
        int indexOf (const juce::String& key) const;
        int countIn (const juce::String& view) const { return (int) find (view, {}).size(); }

        Result load (int index);
        Result loadDefaults();
        Result saveAs (const juce::String& name, const juce::String& category,
                       const juce::String& author, const juce::String& description, bool overwrite);
        Result saveCurrent();                        // overwrite the current user preset
        Result rename (int index, const juce::String& newName);
        Result duplicate (int index);
        Result remove (int index);
        Result importFiles (const juce::Array<juce::File>&);
        Result exportPreset (int index, const juce::File& destination) const;
        Result exportCurrent (const juce::File& destination) const;
        int step (int delta);                        // previous / next preset (library order); returns index

        bool userPresetExists (const juce::String& name, const juce::String& category) const;
        bool isFavorite (const juce::String& key) const { return favorites.contains (key); }
        void setFavorite (const juce::String& key, bool on);

        // current preset (stored in the plugin state, so it survives project reload)
        juce::String getCurrentKey() const;
        juce::String getCurrentName() const;
        int getCurrentIndex() const { return indexOf (getCurrentKey()); }
        bool isCurrentModified();
        void stateReplaced() { needsResync = true; }  // any thread

        juce::String getDefaultAuthor() const { return defaultAuthor; }
        juce::File getRootFolder() const;
        juce::File getUserFolder() const        { return getRootFolder().getChildFile ("User"); }
        juce::File getCollectionsFolder() const { return getRootFolder().getChildFile ("Collections"); }
        const juce::StringArray& getProblems() const { return problems; }
        juce::uint32 getChangeCount() const { return changeCount; }

        // JSON (also used by the tests)
        juce::var toJson (const juce::String& name, const juce::String& category, const juce::String& author,
                          const juce::String& description, const ParamMap& values, const juce::String& irFile) const;
        bool fromJson (const juce::var& json, Preset& out, juce::String& error) const;

    private:
        struct ParamInfo
        {
            juce::String id;
            enum class Kind { number, choice, toggle } kind = Kind::number;
            juce::StringArray choices;
            juce::NormalisableRange<float> range;
            float def = 0.0f;
        };

        void addFromJsonText (const juce::String& text, Source, const juce::File&, const juce::String& keyPath);
        void sortPresets();
        Result writePresetFile (const juce::File&, const juce::var& json) const;
        juce::File userFileFor (const juce::String& name, const juce::String& category) const;
        juce::String userKeyFor (const juce::File&) const;
        void setCurrent (const juce::String& key, const juce::String& name, const ParamMap& baseline);
        void resyncIfNeeded();
        void loadSettings();
        void saveSettings() const;
        void bump() { ++changeCount; }

        MasterSpaceAudioProcessor& processor;
        std::vector<ParamInfo> params;
        std::vector<Preset> presets;
        juce::StringArray favorites, problems;
        juce::String defaultAuthor;
        ParamMap baseline;
        juce::String baselineKey;
        std::atomic<bool> needsResync { true };
        juce::uint32 changeCount = 1;
    };
}
