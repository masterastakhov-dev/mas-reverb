#include "PluginProcessor.h"
#include "PluginEditor.h"
#ifdef MAS_STUDIO
 #include "StudioEditor.h"
#endif
#include "PresetManager.h"

using APF  = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
    constexpr double kMaxIRSeconds = 30.0;
    constexpr float  kIRWetTrim    = 3.2f;    // JUCE normalises IRs to -18 dB energy; bring wet near the algorithmic level
    constexpr double kModeFadeSec  = 0.03;
    constexpr double kIRSplitSec   = 0.08;    // early / late split point for impulse responses
    constexpr int    kVersionHint  = 1;       // MASTER SPACE 1.0 parameter set
    constexpr int    kVersionHint12 = 2;      // parameters added in 1.2
    constexpr double kEngineFadeSec = 0.03;
    constexpr double kStateFadeSec  = 0.06;   // preset / A-B transition: old path out, new path in (linear: direct sound stays constant)
    constexpr double kStateHoldSec  = 0.10;   // longest wait for the message thread to finish writing a new state

    juce::NormalisableRange<float> range (float lo, float hi, float centre = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi);
        if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        return r;
    }

    // juce::String (v, 0) would print every digit (69.7697): round explicitly
    juce::String fixed (float v, int decimals) { return decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v)); }
    juce::String fmtMs (float v)   { return fixed (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String fmtSec (float v)  { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; }
    juce::String fmtPct (float v)  { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String fmtHz (float v)   { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                        : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; }
    juce::String fmtDb (float v)   { return v <= -59.9f ? juce::String ("Off") : (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String fmtMult (float v) { return juce::String (v, 2) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")); }

    float parseHz (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = s.getFloatValue();
        return s.contains ("k") ? v * 1000.0f : v;
    }
    float parseDb (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        return (s.startsWith ("off") || s.contains ("inf")) ? -60.0f : s.getFloatValue();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterSpaceAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    using namespace ParamIDs;

    auto addF = [&p] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                      std::function<juce::String (float)> fmt, std::function<float (const juce::String&)> parse = nullptr)
    {
        auto attr = Attr().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); });
        if (parse) attr = attr.withValueFromStringFunction ([parse] (const juce::String& s) { return parse (s); });
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, kVersionHint }, name, r, def, attr));
    };
    auto addChoice = [&p] (const char* id, const char* name, juce::StringArray items, int def)
    {
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, kVersionHint }, name, items, def));
    };
    auto addBool = [&p] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, kVersionHint }, name, def));
    };
    auto pct = [] (float v) { return fmtPct (v); };
    auto bipolarPos = [] (float v)   // 0..100 -> L 50 .. C .. R 50
    {
        const int i = juce::roundToInt (v - 50.0f);
        return i == 0 ? juce::String ("C") : (i < 0 ? "L " + juce::String (-i) : "R " + juce::String (i));
    };
    auto bipolar = [] (const char* neg, const char* pos)
    {
        return std::function<juce::String (float)> ([neg, pos] (float v)
        {
            const int i = juce::roundToInt (v);
            return i == 0 ? juce::String ("0") : (i < 0 ? juce::String (neg) + " " + juce::String (-i) : juce::String (pos) + " " + juce::String (i));
        });
    };

    // ---- main
    addChoice (algorithm, "Algorithm", { "Studio Room", "Chamber", "Hall" }, 1);
    addChoice (mode,      "Mode",      { "Algorithmic", "IR" }, 0);
    addChoice (routing,   "Routing",   { "Insert", "Aux" }, 0);
    addF (depth,     "Depth",       range (0.0f, 100.0f),       35.0f, pct);
    addF (decay,     "Decay",       range (0.1f, 30.0f, 2.0f),  1.8f,  fmtSec);
    addF (size,      "Size",        range (0.0f, 100.0f),       50.0f, pct);
    addF (preDelay,  "Pre-Delay",   range (0.0f, 500.0f, 60.0f), 0.0f, fmtMs);
    addF (erLevel,   "Early Level", range (-60.0f, 12.0f, -12.0f), 0.0f, fmtDb, parseDb);
    addF (lateLevel, "Late Level",  range (-60.0f, 12.0f, -12.0f), 0.0f, fmtDb, parseDb);
    addF (diffusion, "Diffusion",   range (0.0f, 100.0f),       70.0f, pct);
    addF (density,   "Density",     range (0.0f, 100.0f),       70.0f, pct);
    addF (shape,     "Shape",       range (0.0f, 100.0f),       35.0f, pct);
    addF (spread,    "Spread",      range (0.0f, 500.0f, 100.0f), 60.0f, fmtMs);
    addF (width,     "Width",       range (0.0f, 200.0f),       100.0f, pct);
    addF (mix,       "Mix",         range (0.0f, 100.0f),       35.0f, pct);
    addF (output,    "Output",      range (-24.0f, 12.0f),      0.0f,  fmtDb, parseDb);

    // ---- locks
    addBool (lockLevel, "Level Lock", true);
    addBool (lockTone,  "Tone Lock",  false);
    addBool (lockDecay, "Decay Lock", true);
    addBool (lockWidth, "Width Lock", false);

    // ---- Spatial DNA
    auto metres = [] (float v) { return fixed (v, v < 10.0f ? 1 : 0) + " m"; };
    addF (roomW, "Room Width",  range (2.0f, 40.0f, 10.0f), 7.5f,  metres);
    addF (roomD, "Room Depth",  range (2.0f, 60.0f, 12.0f), 10.0f, metres);
    addF (roomH, "Room Height", range (2.0f, 20.0f, 5.0f),  4.0f,  metres);
    addF (srcX,  "Source X",    range (0.0f, 100.0f), 50.0f, bipolarPos);
    addF (lisX,  "Listener X",  range (0.0f, 100.0f), 50.0f, bipolarPos);
    addF (lisY,  "Listener Y",  range (10.0f, 95.0f), 72.0f, pct);
    const juce::StringArray mats { "Absorber", "Curtain", "Wood", "Plaster", "Concrete", "Glass" };
    addChoice (matFront,   "Front Wall", mats, 3);
    addChoice (matBack,    "Back Wall",  mats, 2);
    addChoice (matLeft,    "Left Wall",  mats, 3);
    addChoice (matRight,   "Right Wall", mats, 3);
    addChoice (matFloor,   "Floor",      mats, 2);
    addChoice (matCeiling, "Ceiling",    mats, 3);
    addBool (erOn, "Early On", true);
    addF (erDensity,   "ER Density",   range (0.0f, 100.0f), 60.0f, pct);
    addF (erDiffusion, "ER Diffusion", range (0.0f, 100.0f), 40.0f, pct);
    addF (erColor,     "ER Color",     range (-100.0f, 100.0f), 0.0f, bipolar ("Dark", "Bright"));
    addF (erWidth,     "ER Width",     range (0.0f, 200.0f), 100.0f, pct);
    addF (erStart,     "ER Start",     range (0.0f, 100.0f, 20.0f),   0.0f,   fmtMs);
    addF (erStop,      "ER Stop",      range (10.0f, 300.0f, 100.0f), 150.0f, fmtMs);
    addF (erPreDelay,  "ER Pre-Delay", range (0.0f, 200.0f, 40.0f),   0.0f,   fmtMs);

    // ---- late
    addBool (lateOn, "Late On", true);
    addF (lateDelay, "Late Delay", range (0.0f, 300.0f, 60.0f), 20.0f, fmtMs);
    addF (lateWidth, "Late Width", range (0.0f, 200.0f),        100.0f, pct);
    addF (lateColor, "Late Color", range (-100.0f, 100.0f),     0.0f,  bipolar ("Dark", "Bright"));

    // ---- decay EQ / filters
   #ifdef MAS_ATMOS
    // experimental build: defaults for the Atmospheric Tail (nearly flat decay; brightness comes from AIR)
    constexpr float dLf = 0.9f, dHm = 1.0f, dHf = 1.0f, dXMid = 2000.0f, dXHigh = 7000.0f;
   #else
    constexpr float dLf = 1.2f, dHm = 0.85f, dHf = 0.55f, dXMid = 1500.0f, dXHigh = 6000.0f;
   #endif
    addF (lfMult, "LF Decay",       range (0.1f, 4.0f, 1.0f), dLf,   fmtMult);
    addF (lmMult, "Low-Mid Decay",  range (0.1f, 4.0f, 1.0f), 1.0f,  fmtMult);
    addF (hmMult, "High-Mid Decay", range (0.1f, 4.0f, 1.0f), dHm,   fmtMult);
    addF (hfMult, "HF Decay",       range (0.1f, 4.0f, 1.0f), dHf,   fmtMult);
    addF (xLow,  "Crossover Low",  range (40.0f, 1000.0f, 200.0f),    200.0f,  fmtHz, parseHz);
    addF (xMid,  "Crossover Mid",  range (300.0f, 8000.0f, 1500.0f),  dXMid,   fmtHz, parseHz);
    addF (xHigh, "Crossover High", range (1500.0f, 18000.0f, 6000.0f), dXHigh, fmtHz, parseHz);
    addF (lowCut,  "Low Cut",  range (20.0f, 2000.0f, 150.0f),   20.0f,    [] (float v) { return v <= 20.5f ? juce::String ("Off") : fmtHz (v); }, parseHz);
    addF (highCut, "High Cut", range (1000.0f, 20000.0f, 6000.0f), 20000.0f, [] (float v) { return v >= 19900.0f ? juce::String ("Off") : fmtHz (v); }, parseHz);

    // ---- modulation
    addChoice (modChar, "Mod Character", { "Smooth", "Random", "Vintage", "Modern" }, 0);
    addF (modRate,   "Mod Rate",   range (0.01f, 5.0f, 0.5f), 0.6f, [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; });
    addF (modDepth,  "Mod Depth",  range (0.0f, 100.0f), 35.0f, pct);
    addF (modRandom, "Mod Random", range (0.0f, 100.0f), 50.0f, pct);
    addF (modSmooth, "Mod Smooth", range (0.0f, 100.0f), 60.0f, pct);

    // ---- stereo
    addF (balance,       "Balance",       range (-100.0f, 100.0f), 0.0f,  bipolar ("L", "R"));
    addF (crossfeed,     "Crossfeed",     range (0.0f, 100.0f),    20.0f, pct);
    addF (decorrelation, "Decorrelation", range (0.0f, 100.0f),    85.0f, pct);

    // ---- impulse response
    addF (irLength, "IR Length", range (5.0f, 100.0f), 100.0f, pct);

    // ---- 1.2: DSP engine version. Projects saved before 1.2 have no value and load as "1.1 Legacy".
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::engine, kVersionHint12 }, "DSP Engine",
                                                               juce::StringArray { "1.1 Legacy", "1.2" }, 1));

   #ifdef MAS_ATMOS
    // ---- ATMOSPHERIC TAIL (experimental). States without "tail" load with the room algorithm.
    {
        constexpr int kHintAtmos = 3;
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::tail, kHintAtmos }, "Late Engine",
                                                                   juce::StringArray { "Room Algorithm", "Atmospheric Tail" }, 1));
        auto pctAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return fmtPct (v); });
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::bloom, kHintAtmos }, "Bloom", range (0.0f, 100.0f), 30.0f, pctAttr));
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::motion, kHintAtmos }, "Motion", range (0.0f, 100.0f), 50.0f, pctAttr));
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::air, kHintAtmos }, "Air", range (0.0f, 100.0f), 50.0f, pctAttr));
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::charLock, kHintAtmos }, "Character Lock", true));
    }
   #endif

   #ifdef MAS_RI
    // ---- 1.4 SMART DUCKER + RESONANCE GUARD. Both OFF by default; states / presets without them load OFF.
    {
        constexpr int kHintRI = 4;
        auto fl = [&p] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                        std::function<juce::String (float)> fmt, std::function<float (const juce::String&)> parse = nullptr)
        {
            auto attr = Attr().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); });
            if (parse) attr = attr.withValueFromStringFunction ([parse] (const juce::String& s) { return parse (s); });
            p.push_back (std::make_unique<APF> (juce::ParameterID { id, kHintRI }, name, r, def, attr));
        };
        auto ch = [&p] (const char* id, const char* name, juce::StringArray items, int def, bool automatable = true)
        {
            p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, kHintRI }, name, items, def,
                                                                       juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
        };
        auto bl = [&p] (const char* id, const char* name, bool def)
        {
            p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, kHintRI }, name, def));
        };
        auto dbAbs = [] (float v) { return juce::String (v, 1) + " dB"; };
        auto dbFs  = [] (float v) { return juce::String (v, 1) + " dBFS"; };
        auto ratio = [] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; };

        bl (duckOn, "Ducker On", false);
        ch (duckMode, "Ducker Mode", { "Classic", "Vocal Focus", "Spectral" }, 1);
        fl (duckAmount,   "Duck Amount",    range (0.0f, 18.0f),             6.0f,   dbAbs);
        fl (duckThresh,   "Duck Threshold", range (-60.0f, 0.0f),            -30.0f, dbFs);
        fl (duckAttack,   "Duck Attack",    range (1.0f, 100.0f, 15.0f),     10.0f,  fmtMs);
        fl (duckHold,     "Duck Hold",      range (0.0f, 500.0f, 120.0f),    120.0f, fmtMs);
        fl (duckRelease,  "Duck Release",   range (50.0f, 3000.0f, 600.0f),  600.0f, fmtMs);
        fl (duckRatio,    "Duck Ratio",     range (1.0f, 10.0f, 3.0f),       4.0f,   ratio);
        fl (duckSens,     "Duck Sensitivity", range (0.0f, 100.0f),          50.0f,  pct);
        fl (duckEarly,    "Early Duck",     range (0.0f, 100.0f),            35.0f,  pct);
        fl (duckLate,     "Late Duck",      range (0.0f, 100.0f),            100.0f, pct);
        fl (duckSpectral, "Spectral Amount", range (0.0f, 100.0f),           60.0f,  pct);
        fl (scLowCut,     "Sidechain Low Cut",  range (20.0f, 1000.0f, 150.0f),   100.0f,  fmtHz, parseHz);
        fl (scHighCut,    "Sidechain High Cut", range (1000.0f, 20000.0f, 6000.0f), 8000.0f, fmtHz, parseHz);
        ch (scSource, "Sidechain", { "Internal", "External" }, 0);

        bl (rgOn, "Resonance Guard On", false);
        ch (rgMode, "Guard Mode", { "Smooth", "Deep", "Tail Aware" }, 0);
        fl (rgSmooth,  "Guard Smooth",      range (0.0f, 100.0f),            50.0f,  pct);
        fl (rgDetail,  "Guard Detail",      range (0.0f, 100.0f),            50.0f,  pct);
        fl (rgSelect,  "Guard Selectivity", range (0.0f, 100.0f),            50.0f,  pct);
        fl (rgAttack,  "Guard Attack",      range (5.0f, 500.0f, 40.0f),     40.0f,  fmtMs);
        fl (rgRelease, "Guard Release",     range (20.0f, 3000.0f, 250.0f),  250.0f, fmtMs);
        fl (rgMax,     "Max Reduction",     range (0.0f, 12.0f, 4.0f),       3.0f,   dbAbs);
        fl (rgLow,     "Guard Low",         range (20.0f, 2000.0f, 200.0f),  150.0f, fmtHz, parseHz);
        fl (rgHigh,    "Guard High",        range (2000.0f, 20000.0f, 8000.0f), 12000.0f, fmtHz, parseHz);
        ch (rgTarget, "Guard Target", { "Early + Late", "Late", "Early" }, 1);
        fl (rgEarly,   "Guard Early Amount", range (0.0f, 100.0f),           50.0f,  pct);
        fl (rgLate,    "Guard Late Amount",  range (0.0f, 100.0f),           100.0f, pct);
        bl (rgMatch, "Guard Output Match", true);
        // changes the latency: not automatable, applied with a short fade of the output
        ch (quality, "Processing Quality", { "Live", "Studio HQ" }, 0, false);

        // HARMONIC COLOR: off by default; wet only (the dry / direct sound is never coloured)
        auto signedPct = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; };
        bl (hcOn, "Harmonic Color On", false);
        ch (hcChar, "Color Character", { "Soft", "Tube", "Tape" }, 0);
        fl (hcDrive, "Color Drive",  range (0.0f, 100.0f),   30.0f,  pct);
        fl (hcColor, "Color Tone",   range (-100.0f, 100.0f), 0.0f,  signedPct);
        fl (hcMix,   "Color Mix",    range (0.0f, 100.0f),   100.0f, pct);
        bl (hcComp, "Color Level Comp", true);
        // moves where the module sits (and, in STUDIO HQ, its latency): not automatable, applied with a fade
        ch (hcPos, "Color Position", { "Post (wet)", "Pre (reverb input)" }, 0, false);
    }
   #endif
   #ifdef MAS_EQ
    // ---- 1.5 EQ LAB: REVERB EQ. Off by default; states / presets without it load with the EQ off.
    {
        constexpr int kHintEQ = 5;
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { eqOn, kHintEQ }, "Reverb EQ On", false));
        const char* bankName[3] = { "WET", "EARLY", "LATE" };
        auto fHz = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
        auto pHz = [] (const juce::String& s) { const auto t = s.trim().toLowerCase(); return t.getFloatValue() * (t.containsChar ('k') ? 1000.0f : 1.0f); };
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < req::kBands; ++i)
            {
                const auto nm = juce::String ("EQ ") + bankName[b] + " " + juce::String (i + 1) + " ";
                auto pid = [b, i] (int field) { return juce::ParameterID { eqId (b, i, field), kHintEQ }; };
                p.push_back (std::make_unique<juce::AudioParameterBool> (pid (eqUsed), nm + "In Use", false,
                                                                         juce::AudioParameterBoolAttributes().withAutomatable (false)));
                p.push_back (std::make_unique<juce::AudioParameterBool> (pid (eqBypass), nm + "Bypass", false));
                p.push_back (std::make_unique<juce::AudioParameterChoice> (pid (eqType), nm + "Type",
                                 juce::StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }, 0,
                                 juce::AudioParameterChoiceAttributes().withAutomatable (false)));
                p.push_back (std::make_unique<APF> (pid (eqFreq), nm + "Freq", range (20.0f, 20000.0f, 1000.0f), 1000.0f,
                                 Attr().withStringFromValueFunction (fHz).withValueFromStringFunction (pHz)));
                p.push_back (std::make_unique<APF> (pid (eqGain), nm + "Gain", juce::NormalisableRange<float> (-18.0f, 18.0f), 0.0f,
                                 Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
                p.push_back (std::make_unique<APF> (pid (eqQ), nm + "Q", range (0.1f, 18.0f, 1.0f), 1.0f,
                                 Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));
                p.push_back (std::make_unique<juce::AudioParameterChoice> (pid (eqSlope), nm + "Slope",
                                 juce::StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1,
                                 juce::AudioParameterChoiceAttributes().withAutomatable (false)));
#ifdef MAS_STUDIO_DYN
                // Manual dynamic cuts are OFF by default; 1.5 and earlier builds never see these IDs.
                p.push_back (std::make_unique<juce::AudioParameterBool> (pid (eqDynOn), nm + "Dynamic Cut", false));
                p.push_back (std::make_unique<APF> (pid (eqDynThreshold), nm + "Dynamic Threshold",
                                 juce::NormalisableRange<float> (-72.0f, -6.0f), -36.0f,
                                 Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dBFS"; })));
                p.push_back (std::make_unique<APF> (pid (eqDynMax), nm + "Dynamic Max Cut",
                                 juce::NormalisableRange<float> (0.0f, 6.0f), 3.0f,
                                 Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
#endif
            }
    }
   #endif

    return { p.begin(), p.end() };
}

MasterSpaceAudioProcessor::MasterSpaceAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                         #ifdef MAS_RI
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)   // SMART DUCKER key (optional)
                         #endif
                          ),
      apvts (*this, nullptr, "MasterSpaceState", createLayout())
{
    lastIRFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #ifdef MAS_EQ
    eqOnRaw = apvts.getRawParameterValue (ParamIDs::eqOn);
    for (int b = 0; b < req::numBanks; ++b)
        for (int i = 0; i < req::kBands; ++i)
            for (int f = 0; f < ParamIDs::eqNumFields; ++f)
                eqRaw[b][i][f] = apvts.getRawParameterValue (ParamIDs::eqId (b, i, f));
   #endif
    startTimerHz (10);
}

MasterSpaceAudioProcessor::~MasterSpaceAudioProcessor()
{
    stopTimer();
    matchGeneration.fetch_add (1);
    matchPool.removeAllJobs (true, 10000);
    presetManager.reset();
}

mspreset::PresetManager& MasterSpaceAudioProcessor::getPresetManager()
{
    if (presetManager == nullptr)
        presetManager = std::make_unique<mspreset::PresetManager> (*this);
    return *presetManager;
}

mspace::Params MasterSpaceAudioProcessor::readParams() const
{
    mspace::Params p;
    fillParams (p);
   #ifdef MAS_EQ
    p.eq = readEq();
   #endif
    return p;
}

#ifdef MAS_EQ
template <class Get>
static req::Settings eqFrom (Get&& value, bool on)
{
    using namespace ParamIDs;
    req::Settings s;
    s.on = on;
    for (int b = 0; b < req::numBanks; ++b)
        for (int i = 0; i < req::kBands; ++i)
        {
            auto& d = s.band[b][i];
            d.used   = value (b, i, eqUsed) > 0.5f;
            d.bypass = value (b, i, eqBypass) > 0.5f;
            d.type   = juce::jlimit (0, (int) req::numTypes - 1, juce::roundToInt (value (b, i, eqType)));
            d.freq   = juce::jlimit (20.0f, 20000.0f, value (b, i, eqFreq));
            d.gainDb = juce::jlimit (-18.0f, 18.0f, value (b, i, eqGain));
            d.q      = juce::jlimit (0.1f, 18.0f, value (b, i, eqQ));
            d.slope  = juce::jlimit (0, 3, juce::roundToInt (value (b, i, eqSlope)));
#ifdef MAS_STUDIO_DYN
            d.dynamic = value (b, i, eqDynOn) > 0.5f;
            d.dynamicThresholdDb = juce::jlimit (-72.0f, -6.0f, value (b, i, eqDynThreshold));
            d.dynamicMaxDb = juce::jlimit (0.0f, 6.0f, value (b, i, eqDynMax));
#endif
        }
    return s;
}

req::Settings MasterSpaceAudioProcessor::readEq() const
{
    return eqFrom ([this] (int b, int i, int f) { return eqRaw[b][i][f]->load (std::memory_order_relaxed); },
                   eqOnRaw->load (std::memory_order_relaxed) > 0.5f);
}
#endif

template <class P>
void MasterSpaceAudioProcessor::fillParams (P& p) const
{
    fillParamsWith (p, [this] (const char* id) { return get (id); });
}

// Fills the parameter set of either engine (1.2 or the frozen 1.1) from plain parameter values;
// both engines share these fields.
template <class P, class Get>
void MasterSpaceAudioProcessor::fillParamsWith (P& p, Get&& get)
{
    using namespace ParamIDs;
    p.algorithm    = juce::roundToInt (get (algorithm));
    p.routing      = juce::roundToInt (get (routing));
    p.depth        = get (depth) * 0.01f;
    p.decay        = get (decay);
    p.size         = get (size) * 0.01f;
    p.preDelayMs   = get (preDelay);
    p.erLevelDb    = get (erLevel);
    p.lateLevelDb  = get (lateLevel);
    p.erOn         = get (erOn) > 0.5f;
    p.lateOn       = get (lateOn) > 0.5f;
    p.diffusion    = get (diffusion) * 0.01f;
    p.density      = get (density) * 0.01f;
    p.shape        = get (shape) * 0.01f;
    p.spreadMs     = get (spread);
    p.width        = get (width) * 0.01f;
    p.mix          = get (mix) * 0.01f;
    p.outputDb     = get (output);

    p.lockLevel = get (lockLevel) > 0.5f;
    p.lockTone  = get (lockTone) > 0.5f;
    p.lockDecay = get (lockDecay) > 0.5f;
    p.lockWidth = get (lockWidth) > 0.5f;

    p.roomW = get (roomW); p.roomD = get (roomD); p.roomH = get (roomH);
    p.srcX = get (srcX) * 0.01f;
    p.lisX = get (lisX) * 0.01f;
    p.lisY = get (lisY) * 0.01f;
    for (int i = 0; i < 6; ++i)
        p.materials[(size_t) i] = juce::roundToInt (get (materials[i]));
    p.erDensity    = get (erDensity) * 0.01f;
    p.erDiffusion  = get (erDiffusion) * 0.01f;
    p.erColor      = get (erColor) * 0.01f;
    p.erWidth      = get (erWidth) * 0.01f;
    p.erStartMs    = get (erStart);
    p.erStopMs     = get (erStop);
    p.erPreDelayMs = get (erPreDelay);

    p.lateDelayMs  = get (lateDelay);
    p.lateWidth    = get (lateWidth) * 0.01f;
    p.lateColor    = get (lateColor) * 0.01f;

    p.lfMult = get (lfMult); p.lmMult = get (lmMult); p.hmMult = get (hmMult); p.hfMult = get (hfMult);
    p.xLow = get (xLow); p.xMid = get (xMid); p.xHigh = get (xHigh);
    p.lowCutHz = get (lowCut); p.highCutHz = get (highCut);

    p.modCharacter = juce::roundToInt (get (modChar));
    p.modRate      = get (modRate);
    p.modDepth     = get (modDepth) * 0.01f;
    p.modRandom    = get (modRandom) * 0.01f;
    p.modSmooth    = get (modSmooth) * 0.01f;

    p.balance       = get (balance) * 0.01f;
    p.crossfeed     = get (crossfeed) * 0.01f;
    p.decorrelation = get (decorrelation) * 0.01f;
   #ifdef MAS_ATMOS
    if constexpr (std::is_same_v<P, mspace::Params>)
    {
        p.tailMode = get (tail) > 0.5f ? 1 : 0;
        p.bloom    = get (bloom) * 0.01f;
        p.motion   = get (motion) * 0.01f;
        p.air      = get (air) * 0.01f;
        p.charLock = get (charLock) > 0.5f;
    }
   #endif
   #ifdef MAS_RI
    if constexpr (std::is_same_v<P, mspace::Params>)
    {
        p.duckOn        = get (duckOn) > 0.5f;
        p.duckMode      = juce::jlimit (0, 2, juce::roundToInt (get (duckMode)));
        p.duckAmountDb  = get (duckAmount);
        p.duckThreshDb  = get (duckThresh);
        p.duckAttackMs  = get (duckAttack);
        p.duckHoldMs    = get (duckHold);
        p.duckReleaseMs = get (duckRelease);
        p.duckRatio     = get (duckRatio);
        p.duckSens      = get (duckSens) * 0.01f;
        p.duckEarly     = get (duckEarly) * 0.01f;
        p.duckLate      = get (duckLate) * 0.01f;
        p.duckSpectral  = get (duckSpectral) * 0.01f;
        p.scLowHz       = get (scLowCut);
        p.scHighHz      = get (scHighCut);
        p.scExternal    = get (scSource) > 0.5f;
        p.rgOn          = get (rgOn) > 0.5f;
        p.rgMode        = juce::jlimit (0, 2, juce::roundToInt (get (rgMode)));
        p.rgSmooth      = get (rgSmooth) * 0.01f;
        p.rgDetail      = get (rgDetail) * 0.01f;
        p.rgSelect      = get (rgSelect) * 0.01f;
        p.rgAttackMs    = get (rgAttack);
        p.rgReleaseMs   = get (rgRelease);
        p.rgMaxDb       = get (rgMax);
        p.rgLoHz        = get (rgLow);
        p.rgHiHz        = get (rgHigh);
        p.rgTarget      = juce::jlimit (0, 2, juce::roundToInt (get (rgTarget)));
        p.rgEarly       = get (rgEarly) * 0.01f;
        p.rgLate        = get (rgLate) * 0.01f;
        p.rgMatch       = get (rgMatch) > 0.5f;
        p.rgDelta       = false;     // DELTA LISTEN is a monitoring switch of the plugin (set per block)
        p.quality       = juce::roundToInt (get (quality)) > 0 ? 1 : 0;
        p.hcOn          = get (hcOn) > 0.5f;
        p.hcChar        = juce::jlimit (0, 2, juce::roundToInt (get (hcChar)));
        p.hcDrive       = get (hcDrive) * 0.01f;
        p.hcColor       = get (hcColor) * 0.01f;
        p.hcMix         = get (hcMix) * 0.01f;
        p.hcLevelComp   = get (hcComp) > 0.5f;
        p.hcPos         = juce::roundToInt (get (hcPos)) > 0 ? 1 : 0;
    }
   #endif
}

//==============================================================================
void MasterSpaceAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    if (std::abs (hostRate.load() - sampleRate) > 1.0)
    {
        hostRate = sampleRate;
        if (irOriginal.getNumSamples() > 0)
            juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveFlag)]
            {
                if (! alive.expired()) sendShapedIRToAudioThread();
            });
    }
    chunkSize = juce::jmax (64, samplesPerBlock);
   #ifdef MAS_EQ
    analyzer.sampleRate.store (sampleRate);
   #endif

    {
        curP = readParams();
        fillParams (curLP);
        curLegacy = isLegacyEngine();
        haveCur = true;
        for (auto& path : paths)
            path.prepare (sampleRate, chunkSize, curP, curLP, curLegacy);
        txScratch.setSize (3, chunkSize);
        activePath = 0;
        txState = Tx::idle;
        txGain = 1.0f;
        transitionSeen = transitionRequest.load();
    }

   #ifdef MAS_RI
    {
        // STUDIO HQ latency: every path (1.2, 1.1 Legacy, IR) is delayed by the same amount
        const int hq = mspace::Engine::hqLatencyFor (sampleRate);
        scScratch.setSize (2, chunkSize);
        for (auto& d : irAlign) d.allocate (hq + chunkSize + 16);
        txAlign.allocate (hq + chunkSize + 16);
        modeAlign.allocate (hq + chunkSize + 16);
        alignQ = curP.quality;
        qSwitchPending = false;
        qFade = 1.0f;
        qHold = 0;
        for (auto& path : paths) path.engine.switchQualityNow (alignQ);
        setAlignment (latencyForQuality (alignQ));
        reportedLatency = alignL;
        setLatencySamples (alignL);
        riMeters.latency = alignL;
        riMeters.quality = alignQ;
        meterCountdown = 0;
    }
   #endif

    convolver.prepare ({ sampleRate, (juce::uint32) chunkSize, 2 });
    for (auto& d : irPreDelay) d.allocate ((int) std::ceil (0.52 * sampleRate) + 8);
    irDry.setSize (2, chunkSize);
    irWet.setSize (2, chunkSize);
    monoScratch.setSize (1, chunkSize);

    irCurLowCut  = get (ParamIDs::lowCut);
    irCurHighCut = get (ParamIDs::highCut);
    for (auto& f : irLowCut)  f.setHighPass (irCurLowCut, sampleRate);
    for (auto& f : irHighCut) f.setLowPass (irCurHighCut, sampleRate);
    irCurPre   = get (ParamIDs::preDelay) * 0.001f * (float) sampleRate;
    irCurMix   = get (ParamIDs::routing) > 0.5f ? 1.0f : get (ParamIDs::mix) * 0.01f;
    irCurWidth = get (ParamIDs::width) * 0.01f;
    irCurOut   = mspace::dbToGain (get (ParamIDs::output));

    modeGain = get (ParamIDs::mode) > 0.5f ? 1.0f : 0.0f;
   #ifdef MAS_RI
    modeGainOut = modeGain;
    modeRingSteady = false;
   #endif
    algoRunning = modeGain < 1.0f;
    irRunning = modeGain > 0.0f;
    resetIRPath();
}

void MasterSpaceAudioProcessor::reset()
{
    for (auto& path : paths) path.reset();
   #ifdef MAS_RI
    setAlignment (alignL);    // clears the alignment delays
   #endif
    if (txState == Tx::fading) activePath = 1 - activePath;
    txState = Tx::idle;
    txGain = 1.0f;
    resetIRPath();
}

void MasterSpaceAudioProcessor::resetIRPath()
{
    convolver.reset();
    for (int c = 0; c < 2; ++c)
    {
        irPreDelay[c].clear();
        irLowCut[c].clear();
        irHighCut[c].clear();
       #ifdef MAS_RI
        irAlign[c].clear();
       #endif
    }
}

#ifdef MAS_RI
int MasterSpaceAudioProcessor::latencyForQuality (int q) const
{
    return q == 1 ? mspace::Engine::hqLatencyFor (sampleRateHz) : 0;
}

bool MasterSpaceAudioProcessor::hasSidechainBus() const
{
    if (getBusCount (true) < 2) return false;
    const auto* bus = getBus (true, 1);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

// Audio thread (or prepare). Sets the delay that keeps the 1.1 engine, the IR path and the cross-fade
// gains aligned with the 1.2 engine, whose STUDIO HQ latency is internal. Clears the delays.
void MasterSpaceAudioProcessor::setAlignment (int latency)
{
    alignL = juce::jmax (0, latency);
    for (auto& path : paths) path.setAlignment (alignL);
    for (auto& d : irAlign) d.clear();
    txAlign.clear();
    modeGainOut = modeGain;
    modeRingSteady = false;
}

void AlgoPath::setAlignment (int latency)
{
    alignLatency = latency;
    for (auto& d : legacyAlign) d.clear();
    // gains already on the output side of a running cross-fade are lost: restart from the steady state
    for (int i = 0; i <= latency + 1 && i < gainAlign.capacity(); ++i) gainAlign.push (engineGain);
    engineGainOut = engineGain;
}

// Values for the displays, about 30 times per second (cheap, lock-free; skipped when the displays are off)
void MasterSpaceAudioProcessor::publishMeters (int n)
{
    meterCountdown -= n;
    if (meterCountdown > 0) return;
    meterCountdown = (int) (sampleRateHz / 30.0);

    auto& m = riMeters;
    const auto& e = paths[activePath].engine;
    const bool engineOn = algoRunning && ! curLegacy && get (ParamIDs::mode) < 0.5f;
    m.engineAvailable.store (! curLegacy && get (ParamIDs::mode) < 0.5f, std::memory_order_relaxed);
    m.duckEarlyDb.store (engineOn ? e.duckReductionDb (true) : 0.0f, std::memory_order_relaxed);
    m.duckLateDb.store (engineOn ? e.duckReductionDb (false) : 0.0f, std::memory_order_relaxed);
    m.duckActive.store (engineOn && e.duckActive(), std::memory_order_relaxed);
    m.hcResidueDb.store (engineOn ? e.colorMeters().meterResidue.load (std::memory_order_relaxed) : -120.0f, std::memory_order_relaxed);
    m.hcCompDb.store (engineOn ? e.colorMeters().meterComp.load (std::memory_order_relaxed) : 0.0f, std::memory_order_relaxed);
    m.latency.store (alignL, std::memory_order_relaxed);
    m.quality.store (alignQ, std::memory_order_relaxed);

    if (! vizOn.load (std::memory_order_relaxed))
        return;
    const auto& g = e.guard();
    const int nb = juce::jmin (RIMeters::maxBands, g.numBands());
    for (int b = 0; b < nb; ++b)
    {
        m.bandHz[b].store ((float) g.bandHz (b), std::memory_order_relaxed);
        m.levelDb[b].store (engineOn ? g.vizLevelDb (b) : -120.0f, std::memory_order_relaxed);
        m.cutDb[b].store (engineOn ? g.vizCutDb (b) : 0.0f, std::memory_order_relaxed);
    }
    m.numBands.store (nb, std::memory_order_relaxed);
    m.rgMaxCutDb.store (engineOn ? g.maxCutDb() : 0.0f, std::memory_order_relaxed);
    m.rgMakeupDb.store (engineOn ? g.makeupDb() : 0.0f, std::memory_order_relaxed);
    m.rgProcessing.store (engineOn && g.isProcessing(), std::memory_order_relaxed);
    m.generation.fetch_add (1, std::memory_order_release);
}
#endif

bool MasterSpaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

   #ifdef MAS_RI
    bool mainOk = false;
    if (out == juce::AudioChannelSet::stereo())
        mainOk = in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    else if (out == juce::AudioChannelSet::mono())
        mainOk = in == juce::AudioChannelSet::mono();
    if (! mainOk) return false;

    // optional sidechain (SMART DUCKER key): off, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
    }
    return true;
   #else
    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();

    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();

    return false;
   #endif
}

//==============================================================================
void MasterSpaceAudioProcessor::takePendingIR() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (irLock);
    if (! lock.isLocked() || pendingIR == nullptr || retiredIR != nullptr)
        return;

    if (pendingIR->buffer.getNumSamples() > 0)
    {
        convolver.loadImpulseResponse (std::move (pendingIR->buffer), pendingIR->sampleRate,
                                       pendingIR->stereo ? juce::dsp::Convolution::Stereo::yes
                                                         : juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::no,
                                       juce::dsp::Convolution::Normalise::no);
        irActiveOnAudio = true;
    }
    else
    {
        irActiveOnAudio = false;
    }

    retiredIR = std::move (pendingIR);   // freed later on the message thread
}

void MasterSpaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

   #ifdef MAS_RI
    const int numIn  = getMainBusNumInputChannels();    // the sidechain bus is not part of the main input
   #else
    const int numIn  = getTotalNumInputChannels();
   #endif
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    takePendingIR();

    // Parameters of this block. They are read BEFORE the transition counters: the message thread
    // raises the request counter before it writes a new state, so a block that sees any new value
    // also sees the request and keeps the running path on the previous block's (old) state.
   #ifdef MAS_RI
    mspace::Params p = readParams();
    // quality (latency) changes are applied by this processor at a faded point of the output, for every path
    const int qWanted = p.quality;
    qSwitchPending = qWanted != alignQ;
    p.quality = alignQ;
    p.rgDelta = deltaListen.load (std::memory_order_relaxed);
   #else
    const mspace::Params p = readParams();
   #endif
    mspace11::Params lp; fillParams (lp);
    const bool legacyNow = isLegacyEngine();
    const auto req = transitionRequest.load();
    const auto ready = transitionReady.load();

    if (req != transitionSeen)
    {
        transitionSeen = req;
        if (txState == Tx::idle)
        {
            frozenP = haveCur ? curP : p;
            frozenLP = haveCur ? curLP : lp;
            frozenLegacy = haveCur ? curLegacy : legacyNow;
            txState = Tx::holding;
            txHold = 0;
        }
        // a new state while fading in: the incoming path just follows it (smoothed like automation)
    }
    else if (txState == Tx::holding)
    {
        // start one block after the new state was seen complete, so all of it is in p
        txHold += n;
        if (ready == req || txHold > (int) (kStateHoldSec * sampleRateHz))
        {
            paths[1 - activePath].snap (p, lp, legacyNow);
            txGain = 0.0f;
            txState = Tx::fading;
           #ifdef MAS_RI
            for (int i = 0; i <= alignL + 1; ++i) txAlign.push (0.0f);   // output-side gains start from 0 as well
           #endif
        }
    }

    if (txState == Tx::idle)
        paths[activePath].setParams (p, lp);
    else
    {
        paths[activePath].setParams (frozenP, frozenLP);
        if (txState == Tx::fading)
            paths[1 - activePath].setParams (p, lp);
    }
    curP = p; curLP = lp; curLegacy = legacyNow; haveCur = true;

   #ifdef MAS_RI
    // external sidechain (SMART DUCKER key): only when chosen and the host provides the bus; otherwise internal
    const float* scSrc[2] = { nullptr, nullptr };
    if (p.scExternal && hasSidechainBus())
    {
        const int scCh = getBus (true, 1)->getNumberOfChannels();
        const int first = getChannelIndexInProcessBlockBuffer (true, 1, 0);
        if (first >= 0 && first + scCh <= buffer.getNumChannels())
        {
            scSrc[0] = buffer.getReadPointer (first);
            scSrc[1] = scCh > 1 ? buffer.getReadPointer (first + 1) : scSrc[0];
        }
    }
    riMeters.scExternalLive.store (scSrc[0] != nullptr, std::memory_order_relaxed);

    const float qStep = (float) (1.0 / (0.012 * sampleRateHz));
    for (int pos = 0; pos < n; pos += chunkSize)
    {
        const int len = juce::jmin (chunkSize, n - pos);
        float* l = buffer.getWritePointer (0, pos);

        // copy the key first: with a mono main input, output channel 1 shares memory with the sidechain
        const float* scL = nullptr;
        const float* scR = nullptr;
        if (scSrc[0] != nullptr)
        {
            scScratch.copyFrom (0, 0, scSrc[0] + pos, len);
            scScratch.copyFrom (1, 0, scSrc[1] + pos, len);
            scL = scScratch.getReadPointer (0);
            scR = scScratch.getReadPointer (1);
        }
        if (numOut >= 2 && numIn == 1)
            buffer.copyFrom (1, pos, buffer, 0, pos, len);   // mono in -> stereo out

        float* r = numOut >= 2 ? buffer.getWritePointer (1, pos) : monoScratch.getWritePointer (0);
        if (numOut < 2) juce::FloatVectorOperations::copy (r, l, len);
        processStereo (l, r, len, scL, scR);

        // LIVE <-> STUDIO HQ: fade out, switch all paths (latency changes together), fade in
        // (after a switch to STUDIO HQ the output stays muted until the delayed signal arrives)
        if (qSwitchPending || qFade < 1.0f || qHold > 0)
        {
            for (int i = 0; i < len; ++i)
            {
                if (qSwitchPending)  qFade = juce::jmax (0.0f, qFade - qStep);
                else if (qHold > 0) { --qHold; qFade = 0.0f; }
                else                 qFade = juce::jmin (1.0f, qFade + qStep);
                l[i] *= qFade;
                r[i] *= qFade;
            }
            if (qSwitchPending && qFade <= 0.0f)
            {
                alignQ = qWanted;
                for (auto& path : paths) path.engine.switchQualityNow (alignQ);
                curP.quality = frozenP.quality = alignQ;
                setAlignment (latencyForQuality (alignQ));
                qHold = alignL;
                qSwitchPending = false;
            }
        }
    }
    publishMeters (n);
   #ifdef MAS_EQ
    {
        const bool want = spaceLevelsWanted.load (std::memory_order_relaxed);
        auto& e = paths[activePath].engine;
        paths[1 - activePath].engine.setLevelsOn (false);
        e.setLevelsOn (want);
        const bool on = want && algoRunning && ! curLegacy && get (ParamIDs::mode) < 0.5f;
        for (int k = 0; k < 5; ++k) spaceLevel[k].store (on ? e.levelDb (k) : -140.0f, std::memory_order_relaxed);
       #ifdef MAS_STUDIO_DYN
        for (int b = 0; b < req::numBanks; ++b)       // Dynamic Cut: the live reduction of each band, for the EQ graph
            for (int i = 0; i < req::kBands; ++i)
                dynReduction[b][i].store (e.dynReductionDb (b, i), std::memory_order_relaxed);
       #endif
    }
   #endif
   #else
    if (numOut >= 2 && numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in -> stereo out

    for (int pos = 0; pos < n; pos += chunkSize)
    {
        const int len = juce::jmin (chunkSize, n - pos);
        float* l = buffer.getWritePointer (0, pos);

        if (numOut >= 2)
        {
            processStereo (l, buffer.getWritePointer (1, pos), len);
        }
        else
        {
            float* r = monoScratch.getWritePointer (0);
            juce::FloatVectorOperations::copy (r, l, len);
            processStereo (l, r, len);
        }
    }
   #endif

    for (int ch = juce::jmax (2, numIn); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // A/B level match: trim on the B slot (smoothed, ~30 ms)
    const float tTrim = (levelMatchOn.load() && audioSlot.load() == 1) ? juce::Decibels::decibelsToGain (matchTrimDb.load()) : 1.0f;
    if (std::abs (tTrim - curTrim) > 1.0e-6f || std::abs (curTrim - 1.0f) > 1.0e-6f)
    {
        const float a = (float) (1.0 - std::exp (-1.0 / (0.03 * sampleRateHz)));
        const int chans = juce::jmin (2, numOut);
        float g = curTrim;
        for (int i = 0; i < n; ++i)
        {
            g += a * (tTrim - g);
            for (int ch = 0; ch < chans; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
        curTrim = std::abs (g - tTrim) < 1.0e-5f ? tTrim : g;
    }
}

#ifdef MAS_RI
// As below, with the host sidechain; with a latency (STUDIO HQ) the ALGO / IR cross-fade on the output uses the
// mode gain delayed by that latency, and each path keeps running until its delayed gain has reached 0.
void MasterSpaceAudioProcessor::processStereo (float* left, float* right, int n, const float* scL, const float* scR)
{
    const float target = get (ParamIDs::mode) > 0.5f ? 1.0f : 0.0f;
    const bool needIR   = target > 0.0f || modeGain > 0.0f || modeGainOut > 0.0f;
    const bool needAlgo = target < 1.0f || modeGain < 1.0f || modeGainOut < 1.0f;

    if (needIR)
    {
        irDry.copyFrom (0, 0, left, n);
        irDry.copyFrom (1, 0, right, n);
    }

    if (needAlgo)
    {
        if (! algoRunning)
        {
            if (txState == Tx::fading) activePath = 1 - activePath;
            txState = Tx::idle;
            paths[activePath].snap (curP, curLP, curLegacy);
            algoRunning = true;
        }
        processAlgo (left, right, n, scL, scR);
    }
    else
    {
        algoRunning = false;
    }

    if (! needIR)
    {
        irRunning = false;
        if (! modeRingSteady) { for (int i = 0; i <= alignL + 1; ++i) modeAlign.push (modeGain); modeRingSteady = true; }
        return;
    }

    if (! irRunning) { resetIRPath(); irRunning = true; }
    processIRPath (n);
    if (alignL > 0)   // STUDIO HQ: the IR path is delayed like the algorithmic paths
        for (int c = 0; c < 2; ++c)
        {
            float* d = irDry.getWritePointer (c);
            auto& dl = irAlign[c];
            for (int i = 0; i < n; ++i) { const float y = dl.read (alignL); dl.push (d[i]); d[i] = y; }
        }

    const float step = (float) (1.0 / (kModeFadeSec * sampleRateHz));
    const float* irL = irDry.getReadPointer (0);
    const float* irR = irDry.getReadPointer (1);

    if (! needAlgo && ! modeRingSteady) { for (int i = 0; i <= alignL + 1; ++i) modeAlign.push (modeGain); modeRingSteady = true; }
    const bool fading = needAlgo;
    if (fading) modeRingSteady = false;
    for (int i = 0; i < n; ++i)
    {
        modeGain = target > modeGain ? juce::jmin (target, modeGain + step)
                                     : juce::jmax (target, modeGain - step);
        float gOut = modeGain;
        if (alignL > 0 && fading) { gOut = modeAlign.read (alignL); modeAlign.push (modeGain); }
        modeGainOut = gOut;
        left[i]  = left[i]  * (1.0f - gOut) + irL[i] * gOut;
        right[i] = right[i] * (1.0f - gOut) + irR[i] * gOut;
    }
}
#else
void MasterSpaceAudioProcessor::processStereo (float* left, float* right, int n)
{
    const float target = get (ParamIDs::mode) > 0.5f ? 1.0f : 0.0f;
    const bool needIR   = target > 0.0f || modeGain > 0.0f;
    const bool needAlgo = target < 1.0f || modeGain < 1.0f;

    if (needIR)
    {
        irDry.copyFrom (0, 0, left, n);
        irDry.copyFrom (1, 0, right, n);
    }

    if (needAlgo)
    {
        if (! algoRunning)
        {
            if (txState == Tx::fading) activePath = 1 - activePath;
            txState = Tx::idle;
            paths[activePath].snap (curP, curLP, curLegacy);
            algoRunning = true;
        }
        processAlgo (left, right, n);
    }
    else
    {
        algoRunning = false;
    }

    if (! needIR)
    {
        irRunning = false;
        return;
    }

    if (! irRunning) { resetIRPath(); irRunning = true; }
    processIRPath (n);

    const float step = (float) (1.0 / (kModeFadeSec * sampleRateHz));
    const float* irL = irDry.getReadPointer (0);
    const float* irR = irDry.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        modeGain = target > modeGain ? juce::jmin (target, modeGain + step)
                                     : juce::jmax (target, modeGain - step);
        left[i]  = left[i]  * (1.0f - modeGain) + irL[i] * modeGain;
        right[i] = right[i] * (1.0f - modeGain) + irR[i] * modeGain;
    }
}
#endif

//==============================================================================
void AlgoPath::prepare (double fs, int maxBlock, const mspace::Params& p, const mspace11::Params& lp, bool legacyActive)
{
    engine.setParams (p);
    engine.prepare (fs);
    legacy.setParams (lp);
    legacy.prepare (fs);
   #ifdef MAS_RI
    scratch.setSize (6, maxBlock);
    {
        const int hq = mspace::Engine::hqLatencyFor (fs);
        for (auto& d : legacyAlign) d.allocate (hq + maxBlock + 16);
        gainAlign.allocate (hq + maxBlock + 16);
    }
   #else
    scratch.setSize (5, maxBlock);
   #endif
    engineGain = legacyActive ? 0.0f : 1.0f;
    newRunning = ! legacyActive;
    legacyRunning = legacyActive;
   #ifdef MAS_RI
    setAlignment (alignLatency);
   #endif
}

void AlgoPath::snap (const mspace::Params& p, const mspace11::Params& lp, bool legacyActive)
{
    engine.snap (p);
    legacy.snap (lp);
    engineGain = legacyActive ? 0.0f : 1.0f;
    newRunning = ! legacyActive;
    legacyRunning = legacyActive;
   #ifdef MAS_RI
    setAlignment (alignLatency);
   #endif
}

double AlgoPath::getTailSeconds (bool legacyActive) const
{
    return legacyActive ? legacy.getTailSeconds() : engine.getTailSeconds();
}

#ifdef MAS_RI
// As below, with the host sidechain and the STUDIO HQ alignment: the 1.2 engine delays its output by its own
// latency, so the 1.1 engine's output is delayed by the same amount, and the output-side cross-fade gains are
// the input-side gains delayed too (the direct sound in Insert then stays at constant level during the fade).
void AlgoPath::process (float* left, float* right, int n, bool legacyTarget, double fs, const float* scL, const float* scR)
{
    const int L = alignLatency;
    auto alignLegacy = [this, L] (float* l, float* r, int len)
    {
        if (L <= 0) return;
        for (int i = 0; i < len; ++i)
        {
            const float yl = legacyAlign[0].read (L), yr = legacyAlign[1].read (L);
            legacyAlign[0].push (l[i]); legacyAlign[1].push (r[i]);
            l[i] = yl; r[i] = yr;
        }
    };

    const float target = legacyTarget ? 0.0f : 1.0f;
    const bool needNew = target > 0.0f || engineGain > 0.0f || engineGainOut > 0.0f;
    const bool needOld = target < 1.0f || engineGain < 1.0f || engineGainOut < 1.0f;

    if (needNew && ! needOld)
    {
        if (! newRunning) { engine.reset(); newRunning = true; }
        legacyRunning = false;
        engine.process (left, right, n, scL, scR);
        return;
    }
    if (needOld && ! needNew)
    {
        if (! legacyRunning) { legacy.reset(); for (auto& d : legacyAlign) d.clear(); legacyRunning = true; }
        newRunning = false;
        legacy.process (left, right, n);
        alignLegacy (left, right, n);
        return;
    }

    if (! newRunning)    { engine.reset(); newRunning = true; }
    if (! legacyRunning) { legacy.reset(); for (auto& d : legacyAlign) d.clear(); legacyRunning = true; }

    float* gains = scratch.getWritePointer (0);
    float* oL = scratch.getWritePointer (1);
    float* oR = scratch.getWritePointer (2);
    float* nL = scratch.getWritePointer (3);
    float* nR = scratch.getWritePointer (4);
    float* gOut = scratch.getWritePointer (5);
    const float step = (float) (1.0 / (kEngineFadeSec * fs));
    for (int i = 0; i < n; ++i)
    {
        engineGain = target > engineGain ? juce::jmin (target, engineGain + step) : juce::jmax (target, engineGain - step);
        gains[i] = engineGain;
        if (L > 0) { gOut[i] = gainAlign.read (L); gainAlign.push (engineGain); }
        else gOut[i] = engineGain;
    }
    engineGainOut = gOut[n - 1];
    const bool newComesIn = target > 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float gNew = gains[i], gOld = 1.0f - gains[i];
        nL[i] = left[i] * (newComesIn ? gNew : 1.0f);  nR[i] = right[i] * (newComesIn ? gNew : 1.0f);
        oL[i] = left[i] * (newComesIn ? 1.0f : gOld);  oR[i] = right[i] * (newComesIn ? 1.0f : gOld);
    }
    engine.process (nL, nR, n, scL, scR);
    legacy.process (oL, oR, n);
    alignLegacy (oL, oR, n);
    for (int i = 0; i < n; ++i)
    {
        const float gNew = gOut[i], gOld = 1.0f - gOut[i];
        left[i]  = nL[i] * (newComesIn ? 1.0f : gNew) + oL[i] * (newComesIn ? gOld : 1.0f);
        right[i] = nR[i] * (newComesIn ? 1.0f : gNew) + oR[i] * (newComesIn ? gOld : 1.0f);
    }
}
#else
// Runs the 1.2 engine, the frozen 1.1 engine, or both while cross-fading after a switch.
// The engine that comes in starts from silence and gets a faded-in INPUT (its reflections then build
// up smoothly, no onset clicks on sustained notes); the engine that goes out gets a faded-out OUTPUT.
// Both ramps are the same linear ramp, so the direct sound (Insert) is cross-faded at constant level.
void AlgoPath::process (float* left, float* right, int n, bool legacyTarget, double fs)
{
    const float target = legacyTarget ? 0.0f : 1.0f;
    const bool needNew = target > 0.0f || engineGain > 0.0f;
    const bool needOld = target < 1.0f || engineGain < 1.0f;

    if (needNew && ! needOld)
    {
        if (! newRunning) { engine.reset(); newRunning = true; }
        legacyRunning = false;
        engine.process (left, right, n);
        return;
    }
    if (needOld && ! needNew)
    {
        if (! legacyRunning) { legacy.reset(); legacyRunning = true; }
        newRunning = false;
        legacy.process (left, right, n);
        return;
    }

    if (! newRunning)    { engine.reset(); newRunning = true; }
    if (! legacyRunning) { legacy.reset(); legacyRunning = true; }

    // per-sample gains of the new engine (g) along the ramp
    float* gains = scratch.getWritePointer (0);
    float* oL = scratch.getWritePointer (1);
    float* oR = scratch.getWritePointer (2);
    float* nL = scratch.getWritePointer (3);
    float* nR = scratch.getWritePointer (4);
    const float step = (float) (1.0 / (kEngineFadeSec * fs));
    for (int i = 0; i < n; ++i)
    {
        engineGain = target > engineGain ? juce::jmin (target, engineGain + step) : juce::jmax (target, engineGain - step);
        gains[i] = engineGain;
    }
    const bool newComesIn = target > 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float gNew = gains[i], gOld = 1.0f - gains[i];
        nL[i] = left[i] * (newComesIn ? gNew : 1.0f);  nR[i] = right[i] * (newComesIn ? gNew : 1.0f);
        oL[i] = left[i] * (newComesIn ? 1.0f : gOld);  oR[i] = right[i] * (newComesIn ? 1.0f : gOld);
    }
    engine.process (nL, nR, n);
    legacy.process (oL, oR, n);
    for (int i = 0; i < n; ++i)
    {
        const float gNew = gains[i], gOld = 1.0f - gains[i];
        left[i]  = nL[i] * (newComesIn ? 1.0f : gNew) + oL[i] * (newComesIn ? gOld : 1.0f);
        right[i] = nR[i] * (newComesIn ? 1.0f : gNew) + oR[i] * (newComesIn ? gOld : 1.0f);
    }
}
#endif

#ifdef MAS_RI
// As below; with a latency (STUDIO HQ) the outgoing path's output fades with the delayed gain, and the
// transition ends when that delayed gain has reached 1.
void MasterSpaceAudioProcessor::processAlgo (float* left, float* right, int n, const float* scL, const float* scR)
{
    if (txState != Tx::fading)
    {
       #ifdef MAS_EQ
        paths[1 - activePath].engine.setAnalyzer (nullptr);
        paths[activePath].engine.setAnalyzer (&analyzer);
       #endif
        paths[activePath].process (left, right, n, txState == Tx::holding ? frozenLegacy : curLegacy, sampleRateHz, scL, scR);
        return;
    }

    float* oL = txScratch.getWritePointer (0);
    float* oR = txScratch.getWritePointer (1);
    float* g  = txScratch.getWritePointer (2);
    const float step = (float) (1.0 / (kStateFadeSec * sampleRateHz));
    for (int i = 0; i < n; ++i)
    {
        txGain = juce::jmin (1.0f, txGain + step);
        if (alignL > 0) { g[i] = txAlign.read (alignL); txAlign.push (txGain); }
        else g[i] = txGain;
        oL[i] = left[i];
        oR[i] = right[i];
        left[i]  *= txGain;
        right[i] *= txGain;
    }
   #ifdef MAS_EQ
    paths[activePath].engine.setAnalyzer (nullptr);            // during a transition the incoming path feeds the analyzer
    paths[1 - activePath].engine.setAnalyzer (&analyzer);
   #endif
    paths[activePath].process (oL, oR, n, frozenLegacy, sampleRateHz, scL, scR);      // outgoing
    paths[1 - activePath].process (left, right, n, curLegacy, sampleRateHz, scL, scR);   // incoming (faded-in input)
    for (int i = 0; i < n; ++i)
    {
        left[i]  += oL[i] * (1.0f - g[i]);
        right[i] += oR[i] * (1.0f - g[i]);
    }
    if (txGain >= 1.0f && g[n - 1] >= 1.0f)
    {
        activePath = 1 - activePath;
        txState = Tx::idle;
    }
}
#else
// Algorithmic path with state transitions. While fading, the old path (old state) keeps running and its
// output fades out; the new path (clean engine, new state) gets the same input with a fade-in, so its
// reflections and tail build up smoothly from the moment of the switch. The two linear ramps add up to
// one, so the direct sound stays at constant level.
void MasterSpaceAudioProcessor::processAlgo (float* left, float* right, int n)
{
    if (txState != Tx::fading)
    {
        paths[activePath].process (left, right, n, txState == Tx::holding ? frozenLegacy : curLegacy, sampleRateHz);
        return;
    }

    float* oL = txScratch.getWritePointer (0);
    float* oR = txScratch.getWritePointer (1);
    float* g  = txScratch.getWritePointer (2);
    const float step = (float) (1.0 / (kStateFadeSec * sampleRateHz));
    for (int i = 0; i < n; ++i)
    {
        txGain = juce::jmin (1.0f, txGain + step);
        g[i] = txGain;
        oL[i] = left[i];
        oR[i] = right[i];
        left[i]  *= txGain;
        right[i] *= txGain;
    }
    paths[activePath].process (oL, oR, n, frozenLegacy, sampleRateHz);      // outgoing
    paths[1 - activePath].process (left, right, n, curLegacy, sampleRateHz);   // incoming (faded-in input)
    for (int i = 0; i < n; ++i)
    {
        left[i]  += oL[i] * (1.0f - g[i]);
        right[i] += oR[i] * (1.0f - g[i]);
    }
    if (txGain >= 1.0f)
    {
        activePath = 1 - activePath;
        txState = Tx::idle;
    }
}
#endif

// Reads the dry signal from irDry and writes the mixed IR-mode output back into irDry.
void MasterSpaceAudioProcessor::processIRPath (int n)
{
    using namespace ParamIDs;
    const double fs = sampleRateHz;

    const double lc = juce::jlimit (20.0, 2000.0, (double) get (lowCut));
    const double hc = juce::jlimit (1000.0, 20000.0, (double) get (highCut));
    irCurLowCut  += 0.3 * (lc - irCurLowCut);
    irCurHighCut += 0.3 * (hc - irCurHighCut);
    for (auto& f : irLowCut) f.setHighPass (irCurLowCut, fs);
    const bool highCutOn = irCurHighCut < 19900.0;
    if (highCutOn)
        for (auto& f : irHighCut) f.setLowPass (irCurHighCut, fs);

    const float sSlow = (float) (1.0 - std::exp (-1.0 / (0.060 * fs)));
    const float sFast = (float) (1.0 - std::exp (-1.0 / (0.020 * fs)));
    const float targetPre   = juce::jlimit (0.0f, 500.0f, get (preDelay)) * 0.001f * (float) fs;
    const float targetMix   = get (routing) > 0.5f ? 1.0f : juce::jlimit (0.0f, 1.0f, get (mix) * 0.01f);
    const float targetWidth = get (width) * 0.01f;
    const float targetOut   = mspace::dbToGain (get (output));
    const bool haveIR = irActiveOnAudio.load();

    // wet input: low cut + pre-delay
    for (int c = 0; c < 2; ++c)
    {
        const float* dry = irDry.getReadPointer (c);
        float* wet = irWet.getWritePointer (c);
        float pre = irCurPre;

        for (int i = 0; i < n; ++i)
        {
            pre += sSlow * (targetPre - pre);
            const float x = irLowCut[c].process (dry[i]);
            const float y = irPreDelay[c].readLinear (juce::jmax (pre, 1.0f));
            irPreDelay[c].push (x);
            wet[i] = pre < 1.0f ? x : y;
        }

        if (c == 1) irCurPre = pre;
    }

    if (haveIR)
    {
        juce::dsp::AudioBlock<float> block (irWet);
        auto sub = block.getSubBlock (0, (size_t) n);
        convolver.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

    float* outL = irDry.getWritePointer (0);
    float* outR = irDry.getWritePointer (1);
    const float* wetL = irWet.getReadPointer (0);
    const float* wetR = irWet.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        irCurMix   += sFast * (targetMix - irCurMix);
        irCurWidth += sFast * (targetWidth - irCurWidth);
        irCurOut   += sFast * (targetOut - irCurOut);

        float wl = haveIR ? wetL[i] * kIRWetTrim : 0.0f;
        float wr = haveIR ? wetR[i] * kIRWetTrim : 0.0f;
        if (highCutOn) { wl = irHighCut[0].process (wl); wr = irHighCut[1].process (wr); }

        const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * irCurWidth;
        wl = m + s; wr = m - s;

        const float wetAmt = std::sin (irCurMix * 1.5707963f);
        const float dryAmt = std::cos (irCurMix * 1.5707963f);
        outL[i] = (outL[i] * dryAmt + wl * wetAmt) * irCurOut;
        outR[i] = (outR[i] * dryAmt + wr * wetAmt) * irCurOut;
    }
}

double MasterSpaceAudioProcessor::getTailLengthSeconds() const
{
    if (get (ParamIDs::mode) > 0.5f)
        return juce::jmin (kMaxIRSeconds, irSeconds.load() + get (ParamIDs::preDelay) * 0.001 + 0.3);

    return paths[activePath].getTailSeconds (isLegacyEngine());
}

//==============================================================================
bool MasterSpaceAudioProcessor::isSupportedIRFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff") || path.endsWithIgnoreCase (".flac");
}

bool MasterSpaceAudioProcessor::loadImpulseResponse (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        irDisplayName = "Can't read: " + file.getFileName();
        return false;
    }

    const int channels = (int) juce::jmin ((juce::uint32) 2, reader->numChannels);
    const int length = (int) juce::jmin ((juce::int64) (kMaxIRSeconds * reader->sampleRate), reader->lengthInSamples);

    juce::AudioBuffer<float> buf (channels, length);
    reader->read (&buf, 0, length, 0, true, channels > 1);

    irOriginal = std::move (buf);
    irOriginalRate = reader->sampleRate;
    irDisplayName = file.getFileNameWithoutExtension();
    lastIRFolder = file.getParentDirectory();
    apvts.state.setProperty ("irPath", file.getFullPathName(), nullptr);

    sendShapedIRToAudioThread();
    return true;
}

juce::String MasterSpaceAudioProcessor::irShapeSignature() const
{
    using namespace ParamIDs;
    return juce::String (get (irLength), 2) + "|" + juce::String (get (erLevel), 2) + "|" + juce::String (get (lateLevel), 2)
         + "|" + juce::String (get (erOn)) + "|" + juce::String (get (lateOn));
}

// Applies Length and the Early / Late levels to the loaded IR and hands the result to the audio thread.
void MasterSpaceAudioProcessor::sendShapedIRToAudioThread()
{
    if (irOriginal.getNumSamples() == 0)
        return;

    using namespace ParamIDs;
    const float lengthPct = juce::jlimit (5.0f, 100.0f, get (irLength));
    const float gEarly = get (erOn) > 0.5f ? mspace::dbToGain (get (erLevel)) : 0.0f;
    const float gLate  = get (lateOn) > 0.5f ? mspace::dbToGain (get (lateLevel)) : 0.0f;

    const int total = irOriginal.getNumSamples();
    const int keep = juce::jmax (64, (int) std::round (total * lengthPct * 0.01f));
    const int numCh = irOriginal.getNumChannels();

    auto pending = std::make_unique<PendingIR>();
    pending->buffer.setSize (numCh, keep);
    for (int c = 0; c < numCh; ++c)
        pending->buffer.copyFrom (c, 0, irOriginal, c, 0, keep);

    // early / late gains with a 10 ms crossfade around the split point
    const int split = (int) (kIRSplitSec * irOriginalRate);
    const int xf = (int) (0.010 * irOriginalRate);
    for (int c = 0; c < numCh; ++c)
    {
        float* d = pending->buffer.getWritePointer (c);
        for (int i = 0; i < keep; ++i)
        {
            float g;
            if (i < split - xf)      g = gEarly;
            else if (i > split + xf) g = gLate;
            else
            {
                const float u = (float) (i - (split - xf)) / (float) (2 * xf);
                g = gEarly + (gLate - gEarly) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u));
            }
            d[i] *= g;
        }
    }

    if (keep < total)
    {
        // raised-cosine fade over the last 30 % of the kept part
        const int fadeLen = juce::jmax (32, (int) (keep * 0.3));
        const int fadeStart = keep - fadeLen;
        for (int c = 0; c < numCh; ++c)
        {
            float* d = pending->buffer.getWritePointer (c);
            for (int i = 0; i < fadeLen; ++i)
                d[fadeStart + i] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeLen));
        }
    }

    // Fixed normalisation from the *original* IR (same formula JUCE uses), so Length / Early / Late
    // change the real level instead of being re-normalised away. Also compensate for resampling to
    // the host rate (more samples of the same amplitude = more energy).
    double maxE = 0.0;
    for (int c = 0; c < numCh; ++c)
    {
        const float* o = irOriginal.getReadPointer (c);
        double e = 0.0;
        for (int i = 0; i < total; ++i) e += (double) o[i] * o[i];
        maxE = juce::jmax (maxE, e);
    }
    const double host = juce::jmax (8000.0, hostRate.load());
    const float norm = maxE > 1e-12 ? (float) (0.125 / std::sqrt (maxE) * std::sqrt (irOriginalRate / host)) : 0.0f;
    pending->buffer.applyGain (norm);

    pending->sampleRate = irOriginalRate;
    pending->stereo = numCh > 1;
    irSeconds = (double) keep / irOriginalRate;
    appliedShape = irShapeSignature();

    std::unique_ptr<PendingIR> toFree;
    {
        const juce::SpinLock::ScopedLockType lock (irLock);
        toFree = std::move (pendingIR);       // replace a not-yet-taken IR
        pendingIR = std::move (pending);
    }
}

void MasterSpaceAudioProcessor::timerCallback()
{
   #ifdef MAS_RI
    // STUDIO HQ latency: reported from the message thread when the quality changes
    {
        const int lat = latencyForQuality (juce::roundToInt (get (ParamIDs::quality)) > 0 ? 1 : 0);
        if (lat != reportedLatency)
        {
            reportedLatency = lat;
            setLatencySamples (lat);
        }
    }
   #endif

    {
        std::unique_ptr<PendingIR> toFree;
        {
            const juce::SpinLock::ScopedLockType lock (irLock);
            toFree = std::move (retiredIR);
        }
    }

    juce::String path;
    {
        const juce::SpinLock::ScopedLockType lock (requestLock);
        path.swapWith (requestedIRPath);
    }
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        if (f.existsAsFile())
            loadImpulseResponse (f);
        else
            irDisplayName = "Not found: " + f.getFileName();
    }

    // re-shape once Length / Early / Late have settled
    const auto sig = irShapeSignature();
    if (irOriginal.getNumSamples() > 0 && sig != appliedShape)
    {
        if (sig == lastSeenShape)
            sendShapedIRToAudioThread();
        lastSeenShape = sig;
    }
}

//==============================================================================
ParamMap MasterSpaceAudioProcessor::captureParamMap() const
{
    ParamMap m;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            m[r->getParameterID()] = r->convertFrom0to1 (r->getValue());
    return m;
}

ParamMap MasterSpaceAudioProcessor::defaultParamMap() const
{
    ParamMap m;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            m[r->getParameterID()] = r->convertFrom0to1 (r->getDefaultValue());
    return m;
}

void MasterSpaceAudioProcessor::applyParamMap (const ParamMap& values, const juce::String& irPath)
{
    jassert (juce::MessageManager::getInstanceWithoutCreating() == nullptr
             || juce::MessageManager::getInstance()->isThisTheMessageThread());

    beginStateTransition();
    for (auto* prm : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        const auto it = values.find (r->getParameterID());
        const float norm = it != values.end() ? r->convertTo0to1 (r->getNormalisableRange().snapToLegalValue (it->second))
                                              : r->getDefaultValue();
        if (std::abs (norm - r->getValue()) > 1.0e-7f)
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (norm);
            r->endChangeGesture();
        }
    }
    endStateTransition();

    if (irPath.isNotEmpty() && irPath != getIRPath())
    {
        const juce::File f (irPath);
        if (f.existsAsFile())
            loadImpulseResponse (f);
        else
            irDisplayName = "Not found: " + f.getFileName();
    }
}

//==============================================================================
// A/B compare
juce::int64 MasterSpaceAudioProcessor::hashOf (const ParamMap& m)
{
    juce::String s;
    for (const auto& [id, v] : m) s << id << '=' << juce::String (v, 5) << ';';
    return s.hashCode64();
}

int MasterSpaceAudioProcessor::getActiveSlot() const
{
    const juce::ScopedLock sl (compareLock);
    return compare.active;
}

bool MasterSpaceAudioProcessor::slotHasState (int slot) const
{
    const juce::ScopedLock sl (compareLock);
    return compare.has[slot & 1] || (slot & 1) == compare.active;
}

void MasterSpaceAudioProcessor::captureLiveInto (CompareContext& c) const
{
    const int a = c.active;
    c.slot[a] = captureParamMap();
    c.has[a] = true;
    c.irPath[a] = getIRPath();
    c.presetKey[a] = apvts.state.getProperty ("presetKey").toString();
    c.presetName[a] = apvts.state.getProperty ("presetName").toString();
}

void MasterSpaceAudioProcessor::setLivePresetInfo (const juce::String& key, const juce::String& name)
{
    apvts.state.setProperty ("presetKey", key, nullptr);
    apvts.state.setProperty ("presetName", name, nullptr);
}

void MasterSpaceAudioProcessor::selectSlot (int slot)
{
    slot &= 1;
    CompareContext target;
    {
        const juce::ScopedLock sl (compareLock);
        if (slot == compare.active) return;
        captureLiveInto (compare);
        previous = compare; hasPrevious = true;
        if (! compare.has[slot])
        {
            // first visit: the other slot starts as a copy of the current one (no audible change)
            const int a = compare.active;
            compare.slot[slot] = compare.slot[a];
            compare.irPath[slot] = compare.irPath[a];
            compare.presetKey[slot] = compare.presetKey[a];
            compare.presetName[slot] = compare.presetName[a];
            compare.has[slot] = true;
        }
        compare.active = slot;
        target = compare;
    }
    applyParamMap (target.slot[slot], target.irPath[slot]);
    setLivePresetInfo (target.presetKey[slot], target.presetName[slot]);
    audioSlot = slot;
    if (presetManager != nullptr) presetManager->stateReplaced();
}

void MasterSpaceAudioProcessor::copyActiveToOther()
{
    const juce::ScopedLock sl (compareLock);
    captureLiveInto (compare);
    previous = compare; hasPrevious = true;
    const int a = compare.active, o = 1 - a;
    compare.slot[o] = compare.slot[a];
    compare.irPath[o] = compare.irPath[a];
    compare.presetKey[o] = compare.presetKey[a];
    compare.presetName[o] = compare.presetName[a];
    compare.has[o] = true;
}

void MasterSpaceAudioProcessor::pushCompareHistory()
{
    const juce::ScopedLock sl (compareLock);
    captureLiveInto (compare);
    previous = compare; hasPrevious = true;
}

bool MasterSpaceAudioProcessor::canUndoCompare() const
{
    const juce::ScopedLock sl (compareLock);
    return hasPrevious;
}

void MasterSpaceAudioProcessor::undoCompare()
{
    CompareContext target;
    {
        const juce::ScopedLock sl (compareLock);
        if (! hasPrevious) return;
        CompareContext now = compare;
        captureLiveInto (now);
        compare = previous;
        previous = now;                          // a second undo goes back again (redo)
        target = compare;
        audioSlot = compare.active;
    }
    const int a = target.active;
    applyParamMap (target.slot[a], target.irPath[a]);
    setLivePresetInfo (target.presetKey[a], target.presetName[a]);
    if (presetManager != nullptr) presetManager->stateReplaced();
}

void MasterSpaceAudioProcessor::setLevelMatch (bool on)
{
    levelMatchOn = on;
    matchedHash = 0;
    if (! on) { matchTrimDb = 0.0f; matchStatus = {}; }
    refreshLevelMatch();
}

juce::String MasterSpaceAudioProcessor::getMatchStatus() const
{
    return matchStatus;
}

// Level match: the B slot gets a trim so its loudness for pink noise equals that of A. Measured
// offline on a background thread with separate engine instances; nothing runs on the audio thread
// except one smoothed gain.
void MasterSpaceAudioProcessor::refreshLevelMatch()
{
    if (! levelMatchOn) return;

    ParamMap a, b;
    {
        const juce::ScopedLock sl (compareLock);
        if (! compare.has[1 - compare.active]) { matchStatus = "MATCH: set up B first"; matchTrimDb = 0.0f; return; }
        a = compare.active == 0 ? captureParamMap() : compare.slot[0];
        b = compare.active == 1 ? captureParamMap() : compare.slot[1];
    }
    const auto h = hashOf (a) ^ (hashOf (b) * 31);
    if (h == matchedHash) return;
    if (h != pendingMatchHash) { pendingMatchHash = h; matchStatus = "MATCH: waiting..."; return; }   // settle first
    matchedHash = h;

    const auto isIR = [] (const ParamMap& m) { const auto it = m.find (ParamIDs::mode); return it != m.end() && it->second > 0.5f; };
    if (isIR (a) || isIR (b)) { matchStatus = "MATCH: n/a in IR mode"; matchTrimDb = 0.0f; return; }

    matchStatus = "MATCH: measuring...";
    const int gen = matchGeneration.fetch_add (1) + 1;
    const double fs = sampleRateHz > 0 ? sampleRateHz : 48000.0;
    matchPool.addJob ([this, a, b, gen, fs, alive = std::weak_ptr<bool> (aliveFlag)]
    {
        const double la = measureLoudness (a, fs);
        if (matchGeneration.load() != gen) return;
        const double lb = measureLoudness (b, fs);
        if (matchGeneration.load() != gen) return;
        const float trim = (float) juce::jlimit (-12.0, 12.0, la - lb);
        juce::MessageManager::callAsync ([this, gen, trim, alive]
        {
            if (alive.expired() || matchGeneration.load() != gen) return;
            matchTrimDb = trim;
            matchStatus = "B " + juce::String (trim >= 0 ? "+" : "") + juce::String (trim, 1) + " dB";
        });
    });
}

double MasterSpaceAudioProcessor::measureLoudness (const ParamMap& m, double fs)
{
    auto getter = [&m] (const char* id) { const auto it = m.find (id); return it != m.end() ? it->second : 0.0f; };
    mspace::Params p; fillParamsWith (p, getter);
   #ifdef MAS_EQ
    {
        // a band that is missing from the map counts as not in use (its other fields then do not matter)
        auto field = [&m] (int b, int i, int f)
        {
            const auto it = m.find (ParamIDs::eqId (b, i, f));
            if (it != m.end()) return it->second;
#ifdef MAS_STUDIO_DYN
            if (f == ParamIDs::eqDynThreshold) return -36.0f;
            if (f == ParamIDs::eqDynMax) return 3.0f;
#endif
            return f == ParamIDs::eqFreq ? 1000.0f : f == ParamIDs::eqQ ? 1.0f : f == ParamIDs::eqSlope ? 1.0f : 0.0f;
        };
        p.eq = eqFrom (field, getter (ParamIDs::eqOn) > 0.5f);
    }
   #endif
    mspace11::Params lp; fillParamsWith (lp, getter);
    const bool legacy = getter (ParamIDs::engine) < 0.5f;

    constexpr int block = 512;
    auto path = std::make_unique<AlgoPath>();
    path->prepare (fs, block, p, lp, legacy);

    // K-weighting (ITU-R BS.1770), coefficients for any sample rate
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
                double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    auto kw = [fs]
    {
        std::array<BQ, 2> f {};
        { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
          const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
          const double a0 = 1.0 + K / Q + K * K;
          f[0] = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 }; }
        { const double f0 = 38.13547087602444, Q = 0.5003270373238773;
          const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
          f[1] = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 }; }
        return f;
    };
    auto kL = kw(), kR = kw();

    // pink noise (Paul Kellet), same signal on both channels like a centred source, -18 dBFS RMS
    juce::Random rng (0x5eed1234);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    juce::AudioBuffer<float> buf (2, block);
    const int total = (int) (3.0 * fs), skip = (int) (1.0 * fs);
    double sum = 0.0; long long count = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        for (int i = 0; i < n; ++i)
        {
            const double w = rng.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11; b6 = w * 0.115926;
            buf.setSample (0, i, (float) (pink * 0.5));
            buf.setSample (1, i, (float) (pink * 0.5));
        }
       #ifdef MAS_RI
        path->process (buf.getWritePointer (0), buf.getWritePointer (1), n, legacy, fs, nullptr, nullptr);
       #else
        path->process (buf.getWritePointer (0), buf.getWritePointer (1), n, legacy, fs);
       #endif
        for (int i = 0; i < n; ++i)
        {
            double l = buf.getSample (0, i), r = buf.getSample (1, i);
            for (auto& f : kL) l = f.run (l);
            for (auto& f : kR) r = f.run (r);
            if (pos + i >= skip) { sum += l * l + r * r; ++count; }
        }
    }
    return -0.691 + 10.0 * std::log10 (std::max (1.0e-12, sum / (double) std::max (1LL, count)));
}

void MasterSpaceAudioProcessor::storeCompareState (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (compareLock);
    juce::ValueTree ab ("ABCompare");
    ab.setProperty ("active", compare.active, nullptr);
    ab.setProperty ("levelMatch", levelMatchOn.load(), nullptr);
    ab.setProperty ("matchTrim", matchTrimDb.load(), nullptr);
    for (int i = 0; i < 2; ++i)
    {
        if (! compare.has[i] || i == compare.active) continue;   // the active slot is the live state
        juce::ValueTree slot ("Slot");
        slot.setProperty ("index", i, nullptr);
        slot.setProperty ("irPath", compare.irPath[i], nullptr);
        slot.setProperty ("presetKey", compare.presetKey[i], nullptr);
        slot.setProperty ("presetName", compare.presetName[i], nullptr);
        for (const auto& [id, v] : compare.slot[i])
            slot.setProperty (juce::Identifier (id), v, nullptr);
        ab.appendChild (slot, nullptr);
    }
    state.appendChild (ab, nullptr);
}

void MasterSpaceAudioProcessor::restoreCompareState (const juce::XmlElement& xml)
{
    const juce::ScopedLock sl (compareLock);
    compare = {};
    hasPrevious = false;
    if (auto* ab = xml.getChildByName ("ABCompare"))
    {
        compare.active = ab->getIntAttribute ("active", 0) & 1;
        levelMatchOn = ab->getBoolAttribute ("levelMatch", false);
        matchTrimDb = levelMatchOn ? (float) juce::jlimit (-12.0, 12.0, ab->getDoubleAttribute ("matchTrim", 0.0)) : 0.0f;
        const auto defaults = defaultParamMap();
        for (auto* slot : ab->getChildWithTagNameIterator ("Slot"))
        {
            const int i = slot->getIntAttribute ("index", 0) & 1;
            ParamMap m = defaults;
            for (auto& [id, v] : m)
                if (slot->hasAttribute (id)) v = (float) slot->getDoubleAttribute (id, v);
            compare.slot[i] = m;
            compare.irPath[i] = slot->getStringAttribute ("irPath");
            compare.presetKey[i] = slot->getStringAttribute ("presetKey");
            compare.presetName[i] = slot->getStringAttribute ("presetName");
            compare.has[i] = true;
        }
    }
    else
    {
        levelMatchOn = false;
        matchTrimDb = 0.0f;
    }
    audioSlot = compare.active;
    matchedHash = pendingMatchHash = 0;
}

//==============================================================================
void MasterSpaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 2, nullptr);       // 2 = MASTER SPACE 1.2 (engine parameter, A/B, preset info)
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    storeCompareState (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterSpaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;   // unreadable state: keep the current one rather than resetting the sound

    // states written before 1.2 carry no engine version: keep their sound with the 1.1 engine
    bool hasEngine = false, hasTail = false;
   #ifdef MAS_RI
    juce::StringArray presentIDs;
   #endif
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        if (e->getStringAttribute ("id") == ParamIDs::engine) hasEngine = true;
       #ifdef MAS_ATMOS
        if (e->getStringAttribute ("id") == ParamIDs::tail) hasTail = true;
       #endif
       #ifdef MAS_RI
        presentIDs.add (e->getStringAttribute ("id"));
       #endif
    }
    juce::ignoreUnused (hasTail);

    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithName ("ABCompare"), nullptr);

    beginStateTransition();
    apvts.replaceState (tree);
    if (! hasEngine)
        if (auto* prm = apvts.getParameter (ParamIDs::engine))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) engineLegacy11));
   #ifdef MAS_ATMOS
    if (! hasTail)
        if (auto* prm = apvts.getParameter (ParamIDs::tail))
            prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f));
   #endif
   #ifdef MAS_RI
    // states written before 1.4: SMART DUCKER and RESONANCE GUARD off, every new parameter at its default
    for (const char* id : ParamIDs::riParams)
        if (! presentIDs.contains (id))
            if (auto* prm = apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->getDefaultValue());
   #endif
   #ifdef MAS_EQ
    // states written before 1.5: REVERB EQ off, no band in use
    for (auto* prm : getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
            if (ParamIDs::isEqParam (wid->paramID) && ! presentIDs.contains (wid->paramID))
                prm->setValueNotifyingHost (prm->getDefaultValue());
   #endif
    endStateTransition();

    restoreCompareState (*xml);
    if (presetManager != nullptr) presetManager->stateReplaced();

    const auto path = apvts.state.getProperty ("irPath").toString();
    if (path.isNotEmpty())
    {
        const juce::SpinLock::ScopedLockType lock (requestLock);
        requestedIRPath = path;
    }
}

juce::AudioProcessorEditor* MasterSpaceAudioProcessor::createEditor()
{
   #ifdef MAS_STUDIO
    return new studio::Editor (*this);
   #else
    return new MasterSpaceEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterSpaceAudioProcessor();
}
