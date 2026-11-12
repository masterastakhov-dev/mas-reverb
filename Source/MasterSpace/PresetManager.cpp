#include "PresetManager.h"

#if __has_include ("MasterSpacePresets.h")
 #include "MasterSpacePresets.h"
 #define MAS_HAS_FACTORY_PRESETS 1
#else
 #define MAS_HAS_FACTORY_PRESETS 0
#endif

namespace mspreset
{
namespace
{
    // "1.1.0" -> 10100
    int versionNumber (const juce::String& v)
    {
        const auto parts = juce::StringArray::fromTokens (v.trim(), ".", {});
        int n = 0;
        for (int i = 0; i < 3; ++i)
            n = n * 100 + (i < parts.size() ? juce::jlimit (0, 99, parts[i].getIntValue()) : 0);
        return n;
    }

    // Parameters renamed between versions: old id -> new id. Empty in 1.2; kept as the migration hook.
    const std::map<juce::String, juce::String>& renamedParameters()
    {
        static const std::map<juce::String, juce::String> m {};
        return m;
    }

    juce::String cleanName (const juce::String& s)
    {
        return s.replaceCharacters ("\r\n\t", "   ").trim().substring (0, 80);
    }
}

const juce::StringArray& categories()
{
    static const juce::StringArray c { "Studio Rooms", "Chambers", "Halls", "Plates", "Vocal Spaces", "Drum Spaces",
                                       "Ambiences", "Early Reflections", "Creative Spaces", "Signature Collection" };
    return c;
}

juce::String Preset::engineName() const
{
    const auto it = values.find (ParamIDs::engine);
    return it != values.end() && it->second < 0.5f ? "1.1 Legacy" : "1.2";
}

//==============================================================================
PresetManager::PresetManager (MasterSpaceAudioProcessor& p) : processor (p)
{
    for (auto* prm : processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        ParamInfo pi;
        pi.id = r->getParameterID();
        pi.range = r->getNormalisableRange();
        pi.def = r->convertFrom0to1 (r->getDefaultValue());
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r)) { pi.kind = ParamInfo::Kind::choice; pi.choices = c->choices; }
        else if (dynamic_cast<juce::AudioParameterBool*> (r) != nullptr) pi.kind = ParamInfo::Kind::toggle;
        params.push_back (pi);
    }
    loadSettings();
    rescan();
}

juce::File PresetManager::getRootFolder() const
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("MASTER_SPACE_PRESET_ROOT", {});
    if (env.isNotEmpty())
        return juce::File (env);
   #if defined (MAS_EQ)
    const char* product = "MASTER SPACE 1.5 EQ LAB";   // 1.5 build: its own library, older builds untouched
   #elif defined (MAS_RI)
    const char* product = "MASTER SPACE 1.4";      // 1.4 build: its own library, 1.2 / ATMOS presets untouched
   #elif defined (MAS_ATMOS)
    const char* product = "MASTER SPACE ATMOS";    // experimental build: its own library, 1.2 presets untouched
   #else
    const char* product = "MASTER SPACE";
   #endif
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library/Audio/Presets/masterastakhovsound").getChildFile (product);
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("masterastakhovsound").getChildFile (product).getChildFile ("Presets");
   #endif
}

//==============================================================================
juce::var PresetManager::toJson (const juce::String& name, const juce::String& category, const juce::String& author,
                                 const juce::String& description, const ParamMap& values, const juce::String& irFile) const
{
    auto* parameters = new juce::DynamicObject();
    juce::String engine = "1.2";
    for (const auto& pi : params)
    {
        const auto it = values.find (pi.id);
        const float v = it != values.end() ? it->second : pi.def;
        switch (pi.kind)
        {
            case ParamInfo::Kind::choice:
            {
                const int idx = juce::jlimit (0, pi.choices.size() - 1, juce::roundToInt (v));
                parameters->setProperty (pi.id, pi.choices[idx]);
                if (pi.id == ParamIDs::engine) engine = pi.choices[idx];
                break;
            }
            case ParamInfo::Kind::toggle: parameters->setProperty (pi.id, v > 0.5f); break;
            case ParamInfo::Kind::number: parameters->setProperty (pi.id, std::round ((double) v * 1.0e4) / 1.0e4); break;
        }
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("format", kFormat);
    o->setProperty ("schemaVersion", kSchemaVersion);
    o->setProperty ("pluginVersion", JucePlugin_VersionString);
    o->setProperty ("name", cleanName (name));
    o->setProperty ("category", category);
    o->setProperty ("author", author.trim());
    o->setProperty ("description", description.trim());
    o->setProperty ("engine", engine);
   #ifdef MAS_ATMOS
    {
        // DSP version and the algorithm actually heard, stated explicitly
        auto choiceText = [&] (const char* id) -> juce::String
        {
            for (const auto& pi : params)
                if (pi.id == id) { const auto it = values.find (pi.id); const int idx = juce::roundToInt (it != values.end() ? it->second : pi.def);
                                   return pi.choices[juce::jlimit (0, pi.choices.size() - 1, idx)]; }
            return {};
        };
        const bool atmos = choiceText (ParamIDs::tail) == "Atmospheric Tail" && engine == "1.2";
       #ifdef MAS_RI
        auto onOff = [&] (const char* id) { for (const auto& pi : params) if (pi.id == id) { const auto it = values.find (pi.id);
                                                return (it != values.end() ? it->second : pi.def) > 0.5f; } return false; };
        o->setProperty ("dspVersion", juce::String (JucePlugin_VersionString) + " (engine " + engine + (atmos ? ", ATMOSPHERIC TAIL" : "")
                                          + (onOff (ParamIDs::duckOn) ? ", SMART DUCKER " + choiceText (ParamIDs::duckMode) : juce::String())
                                          + (onOff (ParamIDs::rgOn) ? ", RESONANCE GUARD " + choiceText (ParamIDs::rgMode) : juce::String())
                                          + (onOff (ParamIDs::hcOn) ? ", HARMONIC COLOR " + choiceText (ParamIDs::hcChar) : juce::String())
                                          + ", " + choiceText (ParamIDs::quality) + ")");
       #else
        o->setProperty ("dspVersion", juce::String (JucePlugin_VersionString) + " (engine " + engine + (atmos ? ", ATMOSPHERIC TAIL prototype" : "") + ")");
       #endif
        o->setProperty ("algorithm", atmos ? juce::String ("Atmospheric Tail") : choiceText (ParamIDs::algorithm));
    }
   #endif
    o->setProperty ("parameters", juce::var (parameters));
    if (irFile.isNotEmpty())
        o->setProperty ("irFile", irFile);
    return juce::var (o);
}

bool PresetManager::fromJson (const juce::var& json, Preset& out, juce::String& error) const
{
    auto* o = json.getDynamicObject();
    if (o == nullptr) { error = "not a JSON object"; return false; }

    const auto format = o->getProperty ("format").toString();
    if (format.isNotEmpty() && ! format.equalsIgnoreCase (kFormat))
    {
        error = "not a MASTER SPACE preset (" + format + ")";
        return false;
    }
    auto* pv = o->getProperty ("parameters").getDynamicObject();
    if (pv == nullptr) { error = "no \"parameters\" block"; return false; }

    out.schemaVersion = o->hasProperty ("schemaVersion") ? (int) o->getProperty ("schemaVersion") : 0;
    out.name = cleanName (o->getProperty ("name").toString());
    if (out.name.isEmpty()) out.name = "Untitled";
    out.category = o->getProperty ("category").toString().trim();
    if (out.category.isEmpty()) out.category = kViewUser;
    out.author = o->getProperty ("author").toString().trim();
    out.description = o->getProperty ("description").toString().trim();
    out.pluginVersion = o->getProperty ("pluginVersion").toString().trim();
    out.irFile = o->getProperty ("irFile").toString().trim();
    out.notes.clear();
    out.values.clear();

    if (out.schemaVersion > kSchemaVersion)
        out.notes.add ("Saved by a newer MASTER SPACE (format " + juce::String (out.schemaVersion) + "): settings it added are ignored.");

    // migration: read renamed parameters under their current id
    juce::NamedValueSet in = pv->getProperties();
    for (const auto& [oldId, newId] : renamedParameters())
        if (in.contains (oldId) && ! in.contains (newId))
            in.set (newId, in[oldId]);

    juce::StringArray missing, invalid;
    for (const auto& pi : params)
    {
        const juce::var v = in[juce::Identifier (pi.id)];
        float value = pi.def;

        if (v.isVoid())
        {
            if (pi.id == ParamIDs::engine)
            {
                // presets written before the engine parameter existed: 1.1 sound unless stated otherwise
                const auto topEngine = o->getProperty ("engine").toString();
                const bool legacy = topEngine.isNotEmpty() ? topEngine.containsIgnoreCase ("legacy") || topEngine.startsWith ("1.1")
                                                           : (out.pluginVersion.isNotEmpty() && versionNumber (out.pluginVersion) < 10200);
                value = legacy ? (float) MasterSpaceAudioProcessor::engineLegacy11 : (float) MasterSpaceAudioProcessor::engine12;
            }
           #ifdef MAS_ATMOS
            else if (pi.id == ParamIDs::tail)
            {
                value = 0.0f;   // presets made before the Atmospheric Tail use the room algorithm
            }
           #endif
            else
            {
               #ifdef MAS_ATMOS
                // parameters of the Atmospheric Tail: older presets simply do not use it
                bool addedLater = pi.id == ParamIDs::bloom || pi.id == ParamIDs::motion || pi.id == ParamIDs::air || pi.id == ParamIDs::charLock;
               #ifdef MAS_RI
                // SMART DUCKER / RESONANCE GUARD: older presets load with both modules off
                for (const char* id : ParamIDs::riParams) addedLater = addedLater || pi.id == id;
               #endif
               #ifdef MAS_EQ
                addedLater = addedLater || ParamIDs::isEqParam (pi.id);   // REVERB EQ: older presets load with it off
               #endif
               #else
                const bool addedLater = false;
               #endif
                if (! addedLater) missing.add (pi.id);
            }
        }
        else if (pi.kind == ParamInfo::Kind::choice)
        {
            int idx = -1;
            if (v.isString())
            {
                for (int i = 0; i < pi.choices.size(); ++i)
                    if (pi.choices[i].equalsIgnoreCase (v.toString().trim())) { idx = i; break; }
            }
            else if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
            {
                idx = juce::roundToInt ((double) v);
            }
            if (idx < 0 || idx >= pi.choices.size()) { invalid.add (pi.id); idx = juce::roundToInt (pi.def); }
            value = (float) idx;
        }
        else if (pi.kind == ParamInfo::Kind::toggle)
        {
            if (v.isBool() || v.isInt() || v.isInt64() || v.isDouble()) value = (double) v > 0.5 ? 1.0f : 0.0f;
            else if (v.isString()) value = v.toString().trim().equalsIgnoreCase ("true") || v.toString().trim().equalsIgnoreCase ("on") ? 1.0f : 0.0f;
            else invalid.add (pi.id);
        }
        else
        {
            double d = 0.0;
            bool ok = true;
            if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()) d = (double) v;
            else if (v.isString() && v.toString().trim().containsOnly ("0123456789.-+eE")) d = v.toString().getDoubleValue();
            else ok = false;
            if (! ok || ! std::isfinite (d)) { invalid.add (pi.id); d = pi.def; }
            const double lo = pi.range.start, hi = pi.range.end;
            if (d < lo || d > hi) invalid.add (pi.id);
            value = (float) juce::jlimit (lo, hi, d);
        }
        out.values[pi.id] = value;
    }

    juce::StringArray unknown;
    for (const auto& nv : in)
    {
        const auto id = nv.name.toString();
        if (renamedParameters().count (id) > 0) continue;
        if (std::none_of (params.begin(), params.end(), [&id] (const ParamInfo& pi) { return pi.id == id; }))
            unknown.add (id);
    }

    if (! missing.isEmpty() && out.schemaVersion >= 1)
        out.notes.add (juce::String (missing.size()) + " setting(s) not in the file, defaults used: " + missing.joinIntoString (", "));
    if (! invalid.isEmpty())
        out.notes.add ("Out of range or unreadable, corrected: " + invalid.joinIntoString (", "));
    if (! unknown.isEmpty())
        out.notes.add ("Unknown settings ignored: " + unknown.joinIntoString (", "));
    return true;
}

//==============================================================================
void PresetManager::addFromJsonText (const juce::String& text, Source source, const juce::File& file, const juce::String& keyPath)
{
    const juce::String where = file != juce::File() ? file.getFileName() : juce::String ("factory preset");
    juce::var json;
    const auto parsed = juce::JSON::parse (text, json);
    if (parsed.failed())
    {
        problems.add (where + ": damaged file (" + parsed.getErrorMessage() + ")");
        return;
    }
    Preset pr;
    juce::String error;
    if (! fromJson (json, pr, error))
    {
        problems.add (where + ": " + error);
        return;
    }
    pr.source = source;
    pr.file = file;
    pr.key = source == Source::factory    ? "factory:" + pr.category + "/" + pr.name
           : source == Source::collection ? "collection:" + keyPath
                                          : "user:" + keyPath;
    if (indexOf (pr.key) >= 0)
    {
        problems.add (where + ": duplicate of \"" + pr.name + "\", skipped");
        return;
    }
    presets.push_back (std::move (pr));
}

void PresetManager::sortPresets()
{
    auto catIndex = [] (const juce::String& c)
    {
        const int i = categories().indexOf (c);
        return i >= 0 ? i : categories().size();
    };
    std::stable_sort (presets.begin(), presets.end(), [&] (const Preset& a, const Preset& b)
    {
        const int ca = catIndex (a.category), cb = catIndex (b.category);
        if (ca != cb) return ca < cb;
        if (a.source != b.source) return (int) a.source < (int) b.source;
        return a.name.compareNatural (b.name) < 0;
    });
}

void PresetManager::rescan()
{
    presets.clear();
    problems.clear();

   #if MAS_HAS_FACTORY_PRESETS
    for (int i = 0; i < MasterSpacePresets::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = MasterSpacePresets::getNamedResource (MasterSpacePresets::namedResourceList[i], size))
            addFromJsonText (juce::String::fromUTF8 (data, size), Source::factory, {}, {});
    }
   #endif

    for (auto [folder, source] : { std::pair<juce::File, Source> { getCollectionsFolder(), Source::collection },
                                   std::pair<juce::File, Source> { getUserFolder(), Source::user } })
    {
        if (! folder.isDirectory()) continue;
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.json"))
        {
            if (f.getFileName().startsWithChar ('.')) continue;   // temporary files of interrupted saves
            if (f.getSize() > 1024 * 1024) { problems.add (f.getFileName() + ": too large, skipped"); continue; }
            addFromJsonText (f.loadFileAsString(), source, f, f.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
        }
    }
    sortPresets();

    favorites.clear();
    juce::var fav;
    const auto favFile = getRootFolder().getChildFile ("favorites.json");
    if (favFile.existsAsFile() && juce::JSON::parse (favFile.loadFileAsString(), fav).wasOk())
        if (auto* arr = fav.getProperty ("favorites", {}).getArray())
            for (const auto& k : *arr) favorites.addIfNotAlreadyThere (k.toString());

    needsResync = true;
    bump();
}

std::vector<int> PresetManager::find (const juce::String& view, const juce::String& search) const
{
    const auto words = juce::StringArray::fromTokens (search.toLowerCase(), " ", "\"");
    std::vector<int> out;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        const bool inView = view.isEmpty() || view == kViewAll
                         || (view == kViewFavorites && favorites.contains (p.key))
                         || (view == kViewUser && p.source == Source::user)
                         || p.category == view;
        if (! inView) continue;
        const auto hay = (p.name + " " + p.category + " " + p.author + " " + p.description).toLowerCase();
        bool all = true;
        for (const auto& w : words)
            if (w.isNotEmpty() && ! hay.contains (w)) { all = false; break; }
        if (all) out.push_back (i);
    }
    return out;
}

int PresetManager::indexOf (const juce::String& key) const
{
    if (key.isEmpty()) return -1;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].key == key) return i;
    return -1;
}

//==============================================================================
void PresetManager::setCurrent (const juce::String& key, const juce::String& name, const ParamMap& values)
{
    processor.apvts.state.setProperty ("presetKey", key, nullptr);
    processor.apvts.state.setProperty ("presetName", name, nullptr);
    baseline = values;
    baselineKey = key;
    needsResync = false;
    bump();
}

juce::String PresetManager::getCurrentKey() const  { return processor.apvts.state.getProperty ("presetKey").toString(); }
juce::String PresetManager::getCurrentName() const { return processor.apvts.state.getProperty ("presetName").toString(); }

void PresetManager::resyncIfNeeded()
{
    if (! needsResync.exchange (false)) return;
    const auto key = getCurrentKey();
    const int i = indexOf (key);
    baselineKey = key;
    baseline = i >= 0 ? presets[(size_t) i].values : ParamMap {};
    bump();
}

bool PresetManager::isCurrentModified()
{
    resyncIfNeeded();
    if (baseline.empty()) return false;
    const auto now = processor.captureParamMap();
    for (const auto& pi : params)
    {
        const auto a = now.find (pi.id);
        const auto b = baseline.find (pi.id);
        if (a == now.end() || b == baseline.end()) continue;
        const float tol = pi.kind == ParamInfo::Kind::number ? 1.0e-4f * (pi.range.end - pi.range.start) : 0.5f;
        if (std::abs (a->second - b->second) > tol) return true;
    }
    return false;
}

Result PresetManager::load (int index)
{
    if (index < 0 || index >= (int) presets.size()) return Result::fail ("No such preset");
    const auto pr = presets[(size_t) index];   // copy: the list may be rescanned by callbacks
    processor.pushCompareHistory();
    processor.applyParamMap (pr.values, pr.irFile);
    setCurrent (pr.key, pr.name, pr.values);
    return Result::success (pr.notes.isEmpty() ? "Loaded " + pr.name : "Loaded " + pr.name + " (" + pr.notes.joinIntoString ("; ") + ")");
}

Result PresetManager::loadDefaults()
{
    processor.pushCompareHistory();
    processor.applyParamMap (processor.defaultParamMap());
    setCurrent ({}, "Default", processor.defaultParamMap());
    return Result::success ("Default settings");
}

int PresetManager::step (int delta)
{
    if (presets.empty()) return -1;
    int i = getCurrentIndex();
    const int n = (int) presets.size();
    i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    load (i);
    return i;
}

//==============================================================================
Result PresetManager::writePresetFile (const juce::File& target, const juce::var& json) const
{
    const auto text = juce::JSON::toString (json, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine)
                                                                         .withMaxDecimalPlaces (4));
    // check that what we write reads back as a valid preset before touching the destination
    {
        juce::var back;
        Preset tmp;
        juce::String err;
        if (juce::JSON::parse (text, back).failed() || ! fromJson (back, tmp, err))
            return Result::fail ("Internal error: preset did not verify (" + err + ")");
    }
    if (! target.getParentDirectory().createDirectory())
        return Result::fail ("Cannot create folder " + target.getParentDirectory().getFullPathName());

    // atomic: write a hidden temporary file next to the target, then rename it over the target
    juce::TemporaryFile temp (target, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk()) return Result::fail ("Cannot write " + target.getFullPathName());
        out.writeText (text, false, false, "\n");
        out.flush();
        if (out.getStatus().failed()) return Result::fail ("Write failed: " + out.getStatus().getErrorMessage());
    }
    if (! temp.overwriteTargetFileWithTemporary())
        return Result::fail ("Cannot replace " + target.getFullPathName());
    return Result::success();
}

juce::File PresetManager::userFileFor (const juce::String& name, const juce::String& category) const
{
    const auto folder = category.isEmpty() || category == kViewUser ? juce::String ("Uncategorised")
                                                                     : juce::File::createLegalFileName (category);
    return getUserFolder().getChildFile (folder).getChildFile (juce::File::createLegalFileName (cleanName (name)) + ".json");
}

juce::String PresetManager::userKeyFor (const juce::File& f) const
{
    return "user:" + f.getRelativePathFrom (getUserFolder()).replaceCharacter ('\\', '/');
}

bool PresetManager::userPresetExists (const juce::String& name, const juce::String& category) const
{
    return userFileFor (name, category).existsAsFile();
}

Result PresetManager::saveAs (const juce::String& nameIn, const juce::String& category, const juce::String& author,
                              const juce::String& description, bool overwrite)
{
    const auto name = cleanName (nameIn);
    if (name.isEmpty()) return Result::fail ("Enter a name");
    const auto file = userFileFor (name, category);
    if (file.existsAsFile() && ! overwrite) return Result::fail ("A user preset with this name exists");

    const auto values = processor.captureParamMap();
    const auto ir = values.count (ParamIDs::mode) && values.at (ParamIDs::mode) > 0.5f ? processor.getIRPath() : juce::String();
    const auto r = writePresetFile (file, toJson (name, category, author, description, values, ir));
    if (! r.ok) return r;

    defaultAuthor = author.trim();
    saveSettings();
    rescan();
    const auto key = userKeyFor (file);
    setCurrent (key, name, values);
    return Result::success ("Saved " + name);
}

Result PresetManager::saveCurrent()
{
    const int i = getCurrentIndex();
    if (i < 0) return Result::fail ("No user preset loaded: use SAVE AS");
    const auto pr = presets[(size_t) i];
    if (pr.readOnly()) return Result::fail ("Factory presets are protected: use SAVE AS to make your own version");

    const auto values = processor.captureParamMap();
    const auto ir = values.count (ParamIDs::mode) && values.at (ParamIDs::mode) > 0.5f ? processor.getIRPath() : juce::String();
    const auto r = writePresetFile (pr.file, toJson (pr.name, pr.category, pr.author, pr.description, values, ir));
    if (! r.ok) return r;
    rescan();
    setCurrent (pr.key, pr.name, values);
    return Result::success ("Saved " + pr.name);
}

Result PresetManager::rename (int index, const juce::String& newNameIn)
{
    if (index < 0 || index >= (int) presets.size()) return Result::fail ("No such preset");
    const auto pr = presets[(size_t) index];
    if (pr.readOnly()) return Result::fail ("Factory presets cannot be renamed: duplicate it first");
    const auto newName = cleanName (newNameIn);
    if (newName.isEmpty()) return Result::fail ("Enter a name");
    const auto target = userFileFor (newName, pr.category);
    if (target.existsAsFile() && target != pr.file) return Result::fail ("A user preset with this name exists");

    const auto r = writePresetFile (target, toJson (newName, pr.category, pr.author, pr.description, pr.values, pr.irFile));
    if (! r.ok) return r;
    if (target != pr.file) pr.file.deleteFile();

    const auto newKey = userKeyFor (target);
    if (favorites.contains (pr.key) && newKey != pr.key) { setFavorite (pr.key, false); setFavorite (newKey, true); }
    const bool wasCurrent = getCurrentKey() == pr.key;
    rescan();
    if (wasCurrent) { setCurrent (newKey, newName, baseline); }
    return Result::success ("Renamed to " + newName);
}

Result PresetManager::duplicate (int index)
{
    if (index < 0 || index >= (int) presets.size()) return Result::fail ("No such preset");
    const auto pr = presets[(size_t) index];
    juce::String name = pr.name + " copy";
    for (int n = 2; userPresetExists (name, pr.category) && n < 1000; ++n)
        name = pr.name + " copy " + juce::String (n);
    const auto author = defaultAuthor.isNotEmpty() ? defaultAuthor : pr.author;
    const auto file = userFileFor (name, pr.category);
    const auto r = writePresetFile (file, toJson (name, pr.category, author, pr.description, pr.values, pr.irFile));
    if (! r.ok) return r;
    rescan();
    return Result::success ("Created " + name);
}

Result PresetManager::remove (int index)
{
    if (index < 0 || index >= (int) presets.size()) return Result::fail ("No such preset");
    const auto pr = presets[(size_t) index];
    if (pr.readOnly()) return Result::fail ("Factory presets are protected");
    if (! pr.file.moveToTrash() && ! pr.file.deleteFile())
        return Result::fail ("Cannot delete " + pr.file.getFullPathName());
    if (favorites.contains (pr.key)) setFavorite (pr.key, false);
    rescan();
    return Result::success ("Deleted " + pr.name + " (moved to the Trash)");
}

Result PresetManager::importFiles (const juce::Array<juce::File>& files)
{
    int imported = 0;
    juce::StringArray errors;
    for (const auto& f : files)
    {
        juce::var json;
        Preset pr;
        juce::String error;
        if (! f.existsAsFile() || f.getSize() > 1024 * 1024) { errors.add (f.getFileName() + ": not readable"); continue; }
        if (juce::JSON::parse (f.loadFileAsString(), json).failed()) { errors.add (f.getFileName() + ": not JSON"); continue; }
        if (! fromJson (json, pr, error)) { errors.add (f.getFileName() + ": " + error); continue; }

        juce::String name = pr.name;
        for (int n = 2; userPresetExists (name, pr.category) && n < 1000; ++n)
            name = pr.name + " (" + juce::String (n) + ")";
        const auto r = writePresetFile (userFileFor (name, pr.category),
                                        toJson (name, pr.category, pr.author, pr.description, pr.values, pr.irFile));
        if (r.ok) ++imported; else errors.add (f.getFileName() + ": " + r.message);
    }
    rescan();
    juce::String msg = "Imported " + juce::String (imported) + " preset" + (imported == 1 ? "" : "s");
    if (! errors.isEmpty()) msg << ". Not imported: " << errors.joinIntoString ("; ");
    return { imported > 0 || errors.isEmpty(), msg };
}

Result PresetManager::exportPreset (int index, const juce::File& destination) const
{
    if (index < 0 || index >= (int) presets.size()) return Result::fail ("No such preset");
    const auto& pr = presets[(size_t) index];
    const auto r = writePresetFile (destination, toJson (pr.name, pr.category, pr.author, pr.description, pr.values, pr.irFile));
    return r.ok ? Result::success ("Exported to " + destination.getFullPathName()) : r;
}

Result PresetManager::exportCurrent (const juce::File& destination) const
{
    const auto values = processor.captureParamMap();
    const auto ir = values.count (ParamIDs::mode) && values.at (ParamIDs::mode) > 0.5f ? processor.getIRPath() : juce::String();
    const int i = getCurrentIndex();
    const auto name = getCurrentName().isNotEmpty() ? getCurrentName() : destination.getFileNameWithoutExtension();
    const auto category = i >= 0 ? presets[(size_t) i].category : juce::String (kViewUser);
    const auto r = writePresetFile (destination, toJson (name, category, defaultAuthor, {}, values, ir));
    return r.ok ? Result::success ("Exported to " + destination.getFullPathName()) : r;
}

//==============================================================================
void PresetManager::setFavorite (const juce::String& key, bool on)
{
    // merge with the file first: other plugin instances may have changed it
    juce::var fav;
    const auto favFile = getRootFolder().getChildFile ("favorites.json");
    juce::StringArray onDisk;
    if (favFile.existsAsFile() && juce::JSON::parse (favFile.loadFileAsString(), fav).wasOk())
        if (auto* arr = fav.getProperty ("favorites", {}).getArray())
            for (const auto& k : *arr) onDisk.addIfNotAlreadyThere (k.toString());
    for (const auto& k : favorites) onDisk.addIfNotAlreadyThere (k);
    if (on) onDisk.addIfNotAlreadyThere (key); else onDisk.removeString (key);
    favorites = onDisk;

    juce::Array<juce::var> arr;
    for (const auto& k : favorites) arr.add (k);
    auto* o = new juce::DynamicObject();
    o->setProperty ("favorites", arr);
    getRootFolder().createDirectory();
    juce::TemporaryFile temp (favFile, juce::TemporaryFile::useHiddenFile);
    if (temp.getFile().replaceWithText (juce::JSON::toString (juce::var (o))))
        temp.overwriteTargetFileWithTemporary();
    bump();
}

void PresetManager::loadSettings()
{
    juce::var s;
    const auto f = getRootFolder().getChildFile ("settings.json");
    if (f.existsAsFile() && juce::JSON::parse (f.loadFileAsString(), s).wasOk())
        defaultAuthor = s.getProperty ("author", {}).toString();
}

void PresetManager::saveSettings() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("author", defaultAuthor);
    const auto f = getRootFolder().getChildFile ("settings.json");
    getRootFolder().createDirectory();
    juce::TemporaryFile temp (f, juce::TemporaryFile::useHiddenFile);
    if (temp.getFile().replaceWithText (juce::JSON::toString (juce::var (o))))
        temp.overwriteTargetFileWithTemporary();
}
}
