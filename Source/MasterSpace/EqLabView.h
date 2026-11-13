// MASTER SPACE 1.5 — the REVERB EQ page: curve and white points edited right on the display,
// SPECTRAL ANALYZER 2.0 (smoothed picture, resonance peak markers, PEAK TO EQ). MAS_EQ builds only.
// Four main bands are always there (LOW, LOW MID, HIGH MID, HIGH); the other four of the eight are free
// bands that PEAK TO EQ takes. No side panel: everything happens on the graph.

#pragma once

#include "SpectrumTools.h"
#ifdef MAS_STUDIO
 #include "StudioLook.h"
#endif

namespace eqlab
{
namespace col
{
    const juce::Colour gold { 0xffe9b85e }, goldHi { 0xfff4cd82 }, teal { 0xff33c6d8 }, tealHi { 0xff5adceb };
    const juce::Colour text { 0xffdfe5ee }, dim { 0xff8490a3 }, bgTop { 0xff0a1020 }, bgBot { 0xff070b15 }, panel { 0xff0d1424 };
    inline juce::Colour border() { return juce::Colour (0xffbecde6).withAlpha (0.13f); }
}
inline juce::Font font (float h, bool bold = false, float kern = 0.0f)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kern);
}
inline juce::String fmtFreq (float f)
{
    if (f >= 1000.0f) { auto s = juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2); while (s.containsChar ('.') && (s.endsWithChar ('0') || s.endsWithChar ('.'))) s = s.dropLastCharacters (1); return s + " kHz"; }
    return juce::String (juce::roundToInt (f)) + " Hz";
}
inline juce::String fmtGain (float g) { return (g > 0.04f ? "+" : g < -0.04f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (g), 1) + " dB"; }
inline const char* typeName (int t) { static const char* n[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }; return n[juce::jlimit (0, 4, t)]; }

//==============================================================================
// segmented selector (pill with items)
class Seg : public juce::Component
{
public:
    Seg (juce::StringArray names, juce::Colour accent) : items (std::move (names)), acc (accent) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void (int)> onChange;
    void setSelected (int i) { if (i != sel) { sel = i; repaint(); } }
    int getSelected() const { return sel; }
   #ifdef MAS_STUDIO
    // studio: a row of tactile keys, the selected one pressed and lit
    float itemW (int i) const { return juce::GlyphArrangement::getStringWidth (studio::inter (9.5f, true, 0.14f), items[i]) + 20.0f; }
    int idealWidth() const { float w = 0; for (int i = 0; i < items.size(); ++i) w += itemW (i) + (i ? 4.0f : 0.0f); return juce::roundToInt (w); }
    void paint (juce::Graphics& g) override
    {
        float x = 0;
        for (int i = 0; i < items.size(); ++i)
        {
            const juce::Rectangle<float> c (x, 1.0f, itemW (i), (float) getHeight() - 3.0f);
            studio::KeyState s; s.on = i == sel; s.hover = hoverItem == i;
            studio::key (g, c, studio::themeOf (*this), s);
            studio::keyText (g, c, items[i], studio::themeOf (*this), s, 9.5f);
            x += itemW (i) + 4.0f;
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = itemAt ((float) e.x); if (h != hoverItem) { hoverItem = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hoverItem = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = itemAt ((float) e.x);
        if (i >= 0 && i != sel) { sel = i; repaint(); if (onChange) onChange (i); }
    }
private:
    int itemAt (float px) const { float x = 0; for (int i = 0; i < items.size(); ++i) { if (px >= x && px < x + itemW (i) + 4.0f) return i; x += itemW (i) + 4.0f; } return -1; }
    int hoverItem = -1;
   #else
    int idealWidth() const
    {
        float w = 6.0f;
        for (auto& s : items) w += juce::GlyphArrangement::getStringWidth (font (10.5f, true, 0.1f), s) + 22.0f;
        return juce::roundToInt (w);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.02f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (col::border()); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setFont (font (10.5f, true, 0.1f));
        float x = 3.0f;
        for (int i = 0; i < items.size(); ++i)
        {
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), items[i]) + 22.0f;
            juce::Rectangle<float> c (x, 3.0f, w, (float) getHeight() - 6.0f);
            if (i == sel)
            {
                g.setColour (acc.withAlpha (0.13f)); g.fillRoundedRectangle (c, c.getHeight() * 0.5f);
                g.setColour (acc.withAlpha (0.5f)); g.drawRoundedRectangle (c, c.getHeight() * 0.5f, 1.0f);
            }
            g.setColour (i == sel ? acc : col::dim);
            g.drawText (items[i], c, juce::Justification::centred);
            x += w;
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        float x = 3.0f;
        for (int i = 0; i < items.size(); ++i)
        {
            const float w = juce::GlyphArrangement::getStringWidth (font (10.5f, true, 0.1f), items[i]) + 22.0f;
            if ((float) e.x >= x && (float) e.x < x + w) { if (i != sel) { sel = i; repaint(); if (onChange) onChange (i); } return; }
            x += w;
        }
    }
   #endif
private:
    juce::StringArray items; juce::Colour acc; int sel = 0;
};

// small button (toggle or momentary)
class Pill : public juce::Component
{
public:
    Pill (juce::String t, juce::Colour accent) : text (std::move (t)), acc (accent) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    void setOn (bool b) { if (b != on) { on = b; repaint(); } }
    bool isOn() const { return on; }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setDim (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
   #ifdef MAS_STUDIO
    int idealWidth() const { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (studio::inter (9.5f, true, 0.14f), text) + (text.startsWith ("UNDO") ? 24.0f : 36.0f)); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withTrimmedBottom (2.0f).withTrimmedTop (1.0f);
        studio::KeyState s; s.on = on; s.down = down; s.hover = isMouseOver(); s.enabled = ! dimmed; s.led = ! text.startsWith ("UNDO");
        studio::key (g, r, studio::themeOf (*this), s);
        studio::keyText (g, r, text, studio::themeOf (*this), s, 9.5f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
   #else
    int idealWidth() const { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (10.5f, true, 0.1f), text) + 24.0f); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setOpacity (dimmed ? 0.4f : 1.0f);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.09f : 0.03f)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? acc.withAlpha (0.6f) : col::border()); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (on ? acc : col::text); g.setFont (font (10.5f, true, 0.1f));
        g.drawText (text, r, juce::Justification::centred);
    }
   #endif
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { down = false; repaint(); if (contains (e.getPosition()) && onClick) onClick(); }
private:
    juce::String text; juce::Colour acc; bool on = false, down = false, dimmed = false;
};

// ON / OFF switch
class Switch : public juce::Component
{
public:
    Switch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
   #ifdef MAS_STUDIO
    void setOn (bool b) { if (b != target) { target = b; repaint(); } }
   #else
    void setOn (bool b) { target = b; }
   #endif
    void advance() { const float t = target ? 1.0f : 0.0f; if (pos != t) { pos += juce::jlimit (-0.25f, 0.25f, t - pos); repaint(); } }
   #ifdef MAS_STUDIO
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withTrimmedBottom (2.0f).withTrimmedTop (1.0f);
        studio::KeyState s; s.on = target; s.led = true;
        studio::key (g, r, studio::themeOf (*this), s);
        studio::keyText (g, r, target ? "ON" : "OFF", studio::themeOf (*this), s, 9.5f);
        juce::ignoreUnused (pos);
    }
   #else
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f).interpolatedWith (col::gold.withAlpha (0.35f), pos)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float d = r.getHeight() - 6.0f, x = r.getX() + 3.0f + pos * (r.getWidth() - d - 6.0f);
        g.setColour (juce::Colour (0xffaaaabb).interpolatedWith (col::gold, pos)); g.fillEllipse (x, r.getY() + 3.0f, d, d);
    }
   #endif
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
private:
    bool target = false; float pos = 0.0f;
};

//==============================================================================
// access to the EQ parameters of the plugin + what the page shows
struct Model
{
    static constexpr int kMain = 4;
    explicit Model (MasterSpaceAudioProcessor& p) : proc (p)
    {
        for (int b = 0; b < 3; ++b) for (int i = 0; i < req::kBands; ++i) for (int f = 0; f < ParamIDs::eqNumFields; ++f)
            prm[b][i][f] = p.apvts.getParameter (ParamIDs::eqId (b, i, f));
        onPrm = p.apvts.getParameter (ParamIDs::eqOn);
    }
    static const char* roleName (int i) { static const char* n[] = { "LOW", "LOW MID", "HIGH MID", "HIGH", "BAND 5", "BAND 6", "BAND 7", "BAND 8" }; return n[juce::jlimit (0, 7, i)]; }
    static int roleType (int i) { return i == 0 ? req::LowShelf : i == 3 ? req::HighShelf : req::Bell; }
    static float roleFreq (int i) { static const float f[] = { 100.0f, 400.0f, 2500.0f, 8000.0f }; return i < kMain ? f[i] : 1000.0f; }
    static float typeQ (int type) { return type == req::Bell ? 1.0f : 0.71f; }

    void refresh() { s = proc.readEq(); }
    const req::BandParams& raw (int i) const { return s.band[bank][i]; }
    bool shown (int i) const { return i < kMain || raw (i).used; }
    // what the display shows: a main band that was never touched sits at its home position, flat
    req::BandParams view (int i) const
    {
        auto b = raw (i);
        if (! b.used && i < kMain) { b = req::BandParams(); b.type = roleType (i); b.freq = roleFreq (i); b.q = typeQ (b.type); }
        return b;
    }
    void setRaw (int i, int field, float plain) { auto* r = prm[bank][i][field]; r->setValueNotifyingHost (r->convertTo0to1 (plain)); }
    // one complete edit (host undo step / automation gesture)
    void set (int i, int field, float plain) { ensure (i); auto* r = prm[bank][i][field]; r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (plain)); r->endChangeGesture(); refresh(); }
    void drag (int i, int field, float plain) { setRaw (i, field, plain); refresh(); }
    void begin (int i, int field) { ensure (i); prm[bank][i][field]->beginChangeGesture(); }
    void end (int i, int field) { prm[bank][i][field]->endChangeGesture(); }
    void ensure (int i)
    {
        if (raw (i).used) return;
        if (i < kMain) { const int t = roleType (i); setRaw (i, ParamIDs::eqType, (float) t); setRaw (i, ParamIDs::eqFreq, roleFreq (i)); setRaw (i, ParamIDs::eqQ, typeQ (t)); }
        setRaw (i, ParamIDs::eqGain, 0.0f); setRaw (i, ParamIDs::eqBypass, 0.0f); setRaw (i, ParamIDs::eqSlope, 1.0f); setRaw (i, ParamIDs::eqUsed, 1.0f);
        refresh();
    }
    void setOn (bool on) { onPrm->beginChangeGesture(); onPrm->setValueNotifyingHost (on ? 1.0f : 0.0f); onPrm->endChangeGesture(); refresh(); }
    void setType (int i, int type)
    {
        ensure (i);
        setRaw (i, ParamIDs::eqType, (float) type); setRaw (i, ParamIDs::eqQ, typeQ (type));
        if (req::isCut (type)) setRaw (i, ParamIDs::eqGain, 0.0f);
        refresh();
    }
    void resetBand (int i) { setRaw (i, ParamIDs::eqUsed, 0.0f); setRaw (i, ParamIDs::eqBypass, 0.0f); refresh(); }   // main band: back home and out of the signal; free band: removed

    // ---- PEAK TO EQ: prepares a Bell at the peak, gain 0 dB. Never cuts anything by itself.
    int freeBandForPeak() const
    {
        for (int i = kMain; i < req::kBands; ++i) if (! raw (i).used) return i;
        for (int i : { 1, 2 }) if (! raw (i).used) return i;
        return -1;
    }
    void assignPeak (int i, float freq, float q, bool dynamic = false)
    {
#ifndef MAS_STUDIO
        juce::ignoreUnused (dynamic);
#endif
        undoBank = bank; undoBand = i; undoValid = true;
        for (int f = 0; f < ParamIDs::eqNumFields; ++f) undoVal[f] = prm[bank][i][f]->getValue();
        setRaw (i, ParamIDs::eqType, (float) req::Bell); setRaw (i, ParamIDs::eqFreq, juce::jlimit (20.0f, 20000.0f, freq));
        setRaw (i, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, q)); setRaw (i, ParamIDs::eqGain, 0.0f);
        setRaw (i, ParamIDs::eqBypass, 0.0f); setRaw (i, ParamIDs::eqSlope, 1.0f); setRaw (i, ParamIDs::eqUsed, 1.0f);
#ifdef MAS_STUDIO
        setRaw (i, ParamIDs::eqDynOn, dynamic ? 1.0f : 0.0f);
        setRaw (i, ParamIDs::eqDynThreshold, -36.0f);
        setRaw (i, ParamIDs::eqDynMax, 3.0f);
        if (dynamic && ! s.on) setOn (true);
#endif
        refresh(); sel = i;
    }
    void undoPeak()
    {
        if (! undoValid) return;
        for (int f = 0; f < ParamIDs::eqNumFields; ++f) prm[undoBank][undoBand][f]->setValueNotifyingHost (undoVal[f]);
        undoValid = false; refresh(); if (bank == undoBank && sel == undoBand) sel = -1;
    }

    MasterSpaceAudioProcessor& proc;
    req::Settings s;
    int bank = 0, sel = -1;
    // analyzer options (a view: not saved, never touch the sound)
    bool anOn = true, peak = false, freeze = false, zone = true, detect = true;
    int src = 0;                              // 0 AUTO (follows the bank), 1 INPUT, 2 EARLY, 3 LATE, 4 WET
    int pp = 1;                               // 0 PRE, 1 POST, 2 PRE + POST
    int speed = 1, smooth = 2;                // smoothing: 0 OFF, 1 = 1/12, 2 = 1/6, 3 = 1/3 octave
    float tilt = 3.0f;
    juce::RangedAudioParameter* prm[3][req::kBands][ParamIDs::eqNumFields] = {};
    juce::RangedAudioParameter* onPrm = nullptr;
    bool undoValid = false; int undoBank = 0, undoBand = 0; float undoVal[ParamIDs::eqNumFields] = {};
    int srcKey() const { return src == 0 ? (bank == 0 ? 4 : bank == 1 ? 2 : 3) : src; }      // 1 in, 2 early, 3 late, 4 wet
    double smoothOct() const { return smooth == 0 ? 0.0 : smooth == 1 ? 1.0 / 12.0 : smooth == 2 ? 1.0 / 6.0 : 1.0 / 3.0; }
    double avgSeconds() const { return speed == 0 ? 0.5 : speed == 1 ? 0.2 : 0.08; }
};

//==============================================================================
class Graph : public juce::Component
{
public:
    explicit Graph (Model& m) : md (m)
    {
        addChildComponent (entry);
        entry.setFont (font (13.0f)); entry.setJustification (juce::Justification::centred);
        entry.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xf00c1017)); entry.setColour (juce::TextEditor::textColourId, col::text);
        entry.setColour (juce::TextEditor::outlineColourId, col::gold.withAlpha (0.6f)); entry.setColour (juce::TextEditor::focusedOutlineColourId, col::gold);
        entry.onReturnKey = [this] { commitEntry(); };
        entry.onEscapeKey = [this] { entry.setVisible (false); };
        entry.onFocusLost = [this] { entry.setVisible (false); };
    }
    std::function<void()> onEdit;

    // ---- geometry
    float padL = 34.0f, padR = 10.0f, padT = 12.0f, padB = 22.0f;
    float pw() const { return (float) getWidth() - padL - padR; }
    float ph() const { return (float) getHeight() - padT - padB; }
    float xOf (float f) const { return padL + pw() * std::log (f / 20.0f) / std::log (1000.0f); }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - padL) / pw())); }
    float yOf (float gDb) const { return padT + ph() * 0.5f - gDb / 18.0f * ph() * 0.5f; }
    float gOf (float y) const { return juce::jlimit (-18.0f, 18.0f, (padT + ph() * 0.5f - y) / (ph() * 0.5f) * 18.0f); }
    static constexpr float specTop = -10.0f, specBot = -100.0f;
    float ySpec (float db) const { return padT + ph() * juce::jlimit (0.0f, 1.0f, (specTop - db) / (specTop - specBot)); }
    juce::Point<float> pointOf (const req::BandParams& b) const { return { xOf (b.freq), yOf (req::isCut (b.type) ? 0.0f : b.gainDb) }; }
    static float bwOct (float q) { return 2.0f * std::asinh (1.0f / (2.0f * q)) / 0.6931472f; }
    static float qOfBw (float oct) { return 1.0f / (2.0f * std::sinh (0.3465736f * oct)); }

    bool shownInTree() const { for (const juce::Component* c = this; c != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return false; return true; }
    void release() { md.proc.analyzer.request[0] = -1; md.proc.analyzer.request[1] = -1; }
    void clearSpectrum() { engA.clear(); engB.clear(); std::fill (peakA.begin(), peakA.end(), -200.0f); std::fill (dispA.begin(), dispA.end(), -200.0f); std::fill (dispB.begin(), dispB.end(), -200.0f); }
    void clearPeak() { std::fill (peakA.begin(), peakA.end(), -200.0f); }
    void poke() { tipUntil = juce::Time::getMillisecondCounter() + 2200; }
    const std::vector<spec::Peak>& markers() const { return engA.detector.markers(); }

    // ---- analyzer: FFT of the real signal from the DSP taps (smoothing is for the picture only)
    void tick (double dt)
    {
        auto& an = md.proc.analyzer;
        const int srcKey = md.srcKey();
        const bool both = md.pp == 2 && srcKey != 1;
        auto tapOf = [srcKey] (bool post) -> int
        {
            if (srcKey == 1) return req::Analyzer::Input;
            if (srcKey == 2) return post ? req::Analyzer::EarlyPost : req::Analyzer::EarlyPre;
            if (srcKey == 3) return post ? req::Analyzer::LatePost : req::Analyzer::LatePre;
            return post ? req::Analyzer::WetPost : req::Analyzer::WetPre;
        };
        const bool run = md.anOn && shownInTree();
        const int tA = run ? tapOf (md.pp != 0) : -1, tB = run && both ? tapOf (false) : -1;
        const double fs = an.sampleRate.load();
        const int order = fs > 60000.0 ? 14 : 13;
        engA.setOrder (order); engB.setOrder (order);
        if (tA != shownA || tB != shownB) { if (! md.freeze) clearSpectrum(); shownA = tA; shownB = tB; settle = 3; }
        an.request[0].store (tA, std::memory_order_relaxed);
        an.request[1].store (tB, std::memory_order_relaxed);
        if (! run || md.freeze) return;
        if (settle > 0) { --settle; return; }
        const int n = juce::roundToInt (pw());
        if (n < 8) return;
        if ((int) dispA.size() != n) { dispA.assign ((size_t) n, -200.0f); dispB.assign ((size_t) n, -200.0f); peakA.assign ((size_t) n, -200.0f); }
        an.read (0, engA.input(), engA.size());
        engA.analyse (fs, dt, md.avgSeconds(), md.detect);
        if (tB >= 0) { an.read (1, engB.input(), engB.size()); engB.analyse (fs, dt, md.avgSeconds(), false); }
        const double oct = md.smoothOct();
        for (int x = 0; x < n; ++x)
        {
            const float f0 = fOf (padL + (float) x), f1 = fOf (padL + (float) x + 1.0f), tl = md.tilt * std::log2 (std::sqrt (f0 * f1) / 1000.0f);
            dispA[(size_t) x] = engA.displayDb (f0, f1, oct) + tl;
            if (tB >= 0) dispB[(size_t) x] = engB.displayDb (f0, f1, oct) + tl;
            peakA[(size_t) x] = juce::jmax (peakA[(size_t) x] - 5.0f * (float) dt, dispA[(size_t) x]);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
       #ifdef MAS_STUDIO
        {
            const auto& th = studio::themeOf (*this);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff05080e), 0, 0, studio::mix (juce::Colour (0xff060a12), th.screen, 0.5f), 0, b.getBottom(), false));
            g.fillRoundedRectangle (b, 14.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0, 0, juce::Colours::transparentBlack, 0, 14.0f, false));
            g.fillRoundedRectangle (b.withHeight (16.0f), 14.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0, 0, juce::Colours::white.withAlpha (0.12f), 0, b.getBottom(), false));
            g.drawRoundedRectangle (b.reduced (0.5f), 14.0f, 1.2f);
        }
       #else
        g.setGradientFill (juce::ColourGradient (col::bgTop, 0, 0, col::bgBot, 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::border()); g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);
       #endif
        const float x0 = padL, x1 = (float) getWidth() - padR, y0 = padT, y1 = padT + ph();
        const bool avail = md.proc.eqAvailable();
        const auto now = juce::Time::getMillisecondCounter();
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

        if (md.zone) { g.setColour (col::gold.withAlpha (0.05f)); g.fillRect (xOf (200.0f), y0, xOf (500.0f) - xOf (200.0f), y1 - y0); }
        for (int d = 10; d <= 10000; d *= 10)
            for (int m = 1; m < 10; ++m)
            {
                const float f = (float) (d * m);
                if (f < 20.0f || f > 20000.0f) continue;
                g.setColour (juce::Colour (0xffc8d7f0).withAlpha (m == 1 ? 0.09f : 0.04f));
                g.fillRect (std::round (xOf (f)), y0, 1.0f, y1 - y0);
            }
        g.setFont (font (10.0f)); g.setColour (col::dim.withAlpha (0.9f));
        const std::pair<float, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (auto& p : fl) g.drawText (p.second, juce::Rectangle<float> (juce::jlimit (x0 - 6.0f, x1 - 24.0f, xOf (p.first) - 15.0f), y1 + 4.0f, 30.0f, 14.0f), juce::Justification::centred);
        for (int gd = -18; gd <= 18; gd += 6)
        {
            const float y = std::round (yOf ((float) gd));
            g.setColour (juce::Colour (0xffc8d7f0).withAlpha (gd == 0 ? 0.15f : 0.045f)); g.fillRect (x0, y, x1 - x0, 1.0f);
            if (std::abs (gd) < 18) { g.setColour (col::dim.withAlpha (0.9f)); g.drawText ((gd > 0 ? "+" : "") + juce::String (gd), juce::Rectangle<float> (0.0f, y - 7.0f, x0 - 6.0f, 14.0f), juce::Justification::centredRight); }
        }

        // ---- spectrum (calm, smoothed) and the peak markers
        if (md.anOn && ! dispA.empty())
        {
            g.saveState(); g.reduceClipRegion (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).toNearestInt());
            juce::Path fill, line; specPath (dispA, fill, true); specPath (dispA, line, false);
            g.setGradientFill (juce::ColourGradient (col::teal.withAlpha (0.26f), 0, y0, col::teal.withAlpha (0.03f), 0, y1, false)); g.fillPath (fill);
            g.setColour (col::tealHi.withAlpha (0.8f)); g.strokePath (line, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (shownB >= 0) { juce::Path pre; specPath (dispB, pre, false); juce::Path dashed; const float dl[] = { 3.0f, 3.0f }; juce::PathStrokeType (1.0f).createDashedStroke (dashed, pre, dl, 2); g.setColour (col::text.withAlpha (0.5f)); g.fillPath (dashed); }
            if (md.peak) { juce::Path pk; specPath (peakA, pk, false); g.setColour (juce::Colour (0xff96ebf5).withAlpha (0.45f)); g.strokePath (pk, juce::PathStrokeType (0.8f)); }
            if (md.detect)
                for (const auto& p : markers())
                {
                    const auto c = markerPos (p);
                    const bool hov = p.id == hoverMarker;
                    juce::Path dm; dm.addQuadrilateral (c.x, c.y - 4.5f, c.x + 3.6f, c.y, c.x, c.y + 4.5f, c.x - 3.6f, c.y);
                    g.setColour (juce::Colour (0xff0b0f15)); g.fillPath (dm);
                    g.setColour ((p.fundamental ? col::gold : juce::Colours::white).withAlpha (hov ? 1.0f : 0.85f));
                    g.strokePath (dm, juce::PathStrokeType (hov ? 1.6f : 1.1f));
                    g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillRect (c.x - 0.5f, c.y + 6.0f, 1.0f, juce::jmax (0.0f, y1 - c.y - 6.0f));
                }
            g.restoreState();
            const int srcKey = md.srcKey();
            const char* nm[] = { "", "INPUT", "EARLY", "LATE", "WET" };
            juce::String t ("ANALYZER: "); t << nm[srcKey];
            if (srcKey != 1) t << dot << (md.pp == 0 ? "PRE EQ" : md.pp == 1 ? "POST EQ" : "POST  (dashed: PRE)");
            if (md.freeze) t << dot << "FREEZE";
            g.setFont (font (9.5f, true, 0.06f)); g.setColour (col::teal.withAlpha (0.9f));
            g.drawText (t, juce::Rectangle<float> (x1 - 300.0f, y0 + 2.0f, 296.0f, 12.0f), juce::Justification::centredRight);
        }
        {
            const char* bn[] = { "WET", "EARLY", "LATE" };
            g.setFont (font (9.5f, true, 0.06f)); g.setColour (col::gold.withAlpha (0.9f));
            g.drawText (juce::String ("EQ TARGET: ") + bn[md.bank], juce::Rectangle<float> (x0 + 6.0f, y0 + 2.0f, 200.0f, 12.0f), juce::Justification::centredLeft);
        }

        // ---- EQ curve (the response of the filters as set) and the white points
        const float a = (md.s.on && avail) ? 1.0f : 0.35f;
        const double fs = juce::jmax (8000.0, md.proc.getSampleRate());
        const int n = juce::jmax (8, juce::roundToInt (pw()));
        const float tip = tipAlpha (now);
        juce::Path curve, selFill;
        const bool haveSel = md.sel >= 0 && md.shown (md.sel);
        const auto sb = haveSel ? md.view (md.sel) : req::BandParams();
        selFill.startNewSubPath (x0, yOf (0.0f));
        for (int x = 0; x <= n; ++x)
        {
            const double f = juce::jmin ((double) fOf (x0 + (float) x), 0.499 * fs);
            double tot = 0.0;
            for (int i = 0; i < req::kBands; ++i) tot += req::bandMagnitudeDb (md.raw (i), f, fs);
            const float y = yOf ((float) juce::jlimit (-40.0, 40.0, tot));
            if (x == 0) curve.startNewSubPath (x0, y); else curve.lineTo (x0 + (float) x, y);
            if (haveSel) { auto vb = sb; vb.used = true; selFill.lineTo (x0 + (float) x, yOf ((float) juce::jlimit (-40.0, 40.0, req::bandMagnitudeDb (vb, f, fs)))); }
        }
        selFill.lineTo (x1, yOf (0.0f)); selFill.closeSubPath();
        g.saveState(); g.reduceClipRegion (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).toNearestInt());
        if (haveSel && ! sb.bypass && tip > 0.0f) { g.setColour (col::gold.withAlpha (0.09f * a * tip)); g.fillPath (selFill); }
        g.setColour (col::gold.withAlpha (a)); g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
        for (int i = 0; i < req::kBands; ++i)
        {
            if (! md.shown (i)) continue;
            const auto bd = md.view (i);
            const auto p = pointOf (bd);
            const bool sel = i == md.sel;
            if (sel && ! req::isCut (bd.type))
            {
                const float h = bwOct (bd.q) * 0.5f, xa = xOf (juce::jlimit (20.0f, 20000.0f, bd.freq * std::pow (2.0f, -h))), xb = xOf (juce::jlimit (20.0f, 20000.0f, bd.freq * std::pow (2.0f, h)));
                g.setColour (juce::Colours::white.withAlpha (0.35f * a));
                g.fillRect (xa, p.y - 0.5f, xb - xa, 1.0f); g.fillRect (xa - 0.5f, p.y - 5.0f, 1.0f, 10.0f); g.fillRect (xb - 0.5f, p.y - 5.0f, 1.0f, 10.0f);
            }
            if (sel) { g.setColour (juce::Colours::white.withAlpha (0.06f * a)); g.fillEllipse (p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f); g.setColour (juce::Colours::white.withAlpha (0.45f * a)); g.drawEllipse (p.x - 9.0f, p.y - 9.0f, 18.0f, 18.0f, 1.0f); }
            if (! bd.bypass) { g.setColour (juce::Colours::white.withAlpha (0.95f * a)); g.fillEllipse (p.x - 4.2f, p.y - 4.2f, 8.4f, 8.4f); }
            else { g.setColour (juce::Colour (0xff0b0f15)); g.fillEllipse (p.x - 4.2f, p.y - 4.2f, 8.4f, 8.4f); g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawEllipse (p.x - 4.2f, p.y - 4.2f, 8.4f, 8.4f, 1.2f); }
        }

        // ---- short hint next to the selected point / the hovered marker
        juce::String t1, t2; juce::Point<float> at;
        float ta = 0.0f;
        if (hoverMarker >= 0 && drag < 0)
            for (const auto& p : markers())
                if (p.id == hoverMarker)
                {
                    const char* bn[] = { "WET", "EARLY", "LATE" };
                    t1 << fmtFreq (p.freq) << dot << juce::String (p.levelDb, 1) << " dBFS" << dot << "+" << juce::String (p.promDb, 1) << " dB over neighbours" << dot << "width " << (p.bwHz < 10.0f ? juce::String (p.bwHz, 1) : juce::String (juce::roundToInt (p.bwHz))) << " Hz";
#ifdef MAS_STUDIO
                    t2 << "click: DYNAMIC CUT or static " << bn[md.bank] << " EQ (no change until selected)";
#else
                    t2 << "click: prepare a " << bn[md.bank] << " EQ band here (gain stays 0 dB)";
#endif
                    at = markerPos (p); ta = 1.0f;
                }
        if (ta == 0.0f && haveSel && tip > 0.0f)
        {
            t1 << Model::roleName (md.sel) << dot;
            if (req::isCut (sb.type)) t1 << typeName (sb.type) << dot << fmtFreq (sb.freq) << dot << req::slopeDb (sb.slope) << " dB/oct";
            else t1 << fmtFreq (sb.freq) << dot << fmtGain (sb.gainDb) << dot << "Q " << juce::String (sb.q, 2);
#ifdef MAS_STUDIO
            if (sb.dynamic) t1 << dot << "DYNAMIC CUT (max " << juce::String (sb.dynamicMaxDb, 1) << " dB)";
#endif
            if (sb.bypass) t1 << dot << "bypass";
            at = pointOf (sb); ta = tip;
        }
        if (ta > 0.0f)
        {
            g.setFont (font (11.5f));
            const float w = juce::jmax (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t1), t2.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font (10.0f), t2)) + 20.0f, h = t2.isEmpty() ? 24.0f : 40.0f;
            float bx = juce::jlimit (x0 + 2.0f, x1 - w - 2.0f, at.x - w * 0.5f), by = at.y - h - 16.0f;
            if (by < y0 + 16.0f) by = at.y + 16.0f;
            g.setOpacity (ta);
            g.setColour (juce::Colour (0xff0c1017).withAlpha (0.94f * ta)); g.fillRoundedRectangle (bx, by, w, h, 7.0f);
            g.setColour (juce::Colours::white.withAlpha (0.14f * ta)); g.drawRoundedRectangle (bx, by, w, h, 7.0f, 1.0f);
            g.setColour (col::text.withAlpha (0.97f * ta)); g.drawText (t1, juce::Rectangle<float> (bx, by + 4.0f, w, 16.0f), juce::Justification::centred);
            if (t2.isNotEmpty()) { g.setFont (font (10.0f)); g.setColour (col::dim.withAlpha (ta)); g.drawText (t2, juce::Rectangle<float> (bx, by + 21.0f, w, 14.0f), juce::Justification::centred); }
        }
        g.setFont (font (10.5f, true, 0.1f)); g.setColour (col::dim.withAlpha (0.95f));
        if (! avail) g.drawText ("REVERB EQ: ALGO MODE, ENGINE 1.2", juce::Rectangle<float> (x0, y1 - 20.0f, x1 - x0, 14.0f), juce::Justification::centred);
        else if (! md.s.on) g.drawText ("REVERB EQ OFF", juce::Rectangle<float> (x0, y1 - 20.0f, x1 - x0, 14.0f), juce::Justification::centred);
        if (flashUntil > now) { g.setFont (font (11.0f, true)); g.setColour (col::text); g.drawText (flash, juce::Rectangle<float> (x0, y1 - 38.0f, x1 - x0, 14.0f), juce::Justification::centred); }
    }

    // ---- handling
    int hit (juce::Point<float> p, float rad) const
    {
        int best = -1; float bd = rad;
        for (int i = 0; i < req::kBands; ++i)
        {
            if (! md.shown (i)) continue;
            const float d = p.getDistanceFrom (pointOf (md.view (i)));
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
    int hitWidth (juce::Point<float> p, float rad) const
    {
        if (md.sel < 0 || ! md.shown (md.sel)) return 0;
        const auto b = md.view (md.sel);
        if (req::isCut (b.type)) return 0;
        const float h = bwOct (b.q) * 0.5f, y = yOf (b.gainDb);
        if (p.getDistanceFrom ({ xOf (juce::jlimit (20.0f, 20000.0f, b.freq * std::pow (2.0f, -h))), y }) < rad) return -1;
        if (p.getDistanceFrom ({ xOf (juce::jlimit (20.0f, 20000.0f, b.freq * std::pow (2.0f, h))), y }) < rad) return 1;
        return 0;
    }
    juce::Point<float> markerPos (const spec::Peak& p) const
    {
        const float x = xOf (p.freq);
        const int ix = juce::jlimit (0, juce::jmax (0, (int) dispA.size() - 1), juce::roundToInt (x - padL));
        const float y = dispA.empty() ? padT + ph() : ySpec (dispA[(size_t) ix]);
        return { x, juce::jlimit (padT + 8.0f, padT + ph() - 6.0f, y - 9.0f) };
    }
    int hitMarker (juce::Point<float> p) const
    {
        if (! md.detect || ! md.anOn) return -1;
        for (const auto& m : markers()) if (p.getDistanceFrom (markerPos (m)) < 9.0f) return m.id;
        return -1;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position, 14.0f), hm = h < 0 ? hitMarker (e.position) : -1;
        if (hm != hoverMarker) { hoverMarker = hm; repaint(); }
        if (h >= 0 && h == md.sel) poke();
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : hm >= 0 ? juce::MouseCursor::PointingHandCursor
                               : hitWidth (e.position, 10.0f) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverMarker != -1) { hoverMarker = -1; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        entry.setVisible (false);
        const int i = hit (e.position, 14.0f);
        if (e.mods.isPopupMenu()) { if (i >= 0) { select (i); menu (i); } return; }
        if (i < 0)
        {
            const int w = hitWidth (e.position, 10.0f);
            if (w != 0) { drag = md.sel; dragMode = 2; md.begin (drag, ParamIDs::eqQ); poke(); return; }
            const int hm = hitMarker (e.position);
            if (hm >= 0) { peakToEq (hm); return; }
            if (md.sel != -1) { md.sel = -1; edited(); }
            return;
        }
        select (i);
        const auto b = md.view (i);
        drag = i; dragF = b.freq; dragG = b.gainDb; dragQ = b.q; dragSlope = b.slope; dragCut = req::isCut (b.type); dragMoved = false;
        dragMode = e.mods.isAltDown() && ! dragCut ? 3 : 1;          // Alt + drag up / down = Q
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        if (dragMode == 2)
        {
            const float oct = std::abs (std::log2 (fOf (e.position.x) / md.view (drag).freq)) * 2.0f;
            md.drag (drag, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, qOfBw (juce::jlimit (0.08f, 7.0f, oct)))); poke(); edited(); return;
        }
        const float dx = (float) e.getDistanceFromDragStartX(), dy = (float) e.getDistanceFromDragStartY();
        if (! dragMoved)
        {
            if (std::abs (dx) + std::abs (dy) < 3.0f) return;
            dragMoved = true;
            if (dragMode == 3) md.begin (drag, ParamIDs::eqQ);
            else { md.begin (drag, ParamIDs::eqFreq); md.begin (drag, dragCut ? ParamIDs::eqSlope : ParamIDs::eqGain); }
        }
        const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
        if (dragMode == 3) md.drag (drag, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, dragQ * std::exp (-dy * 0.012f * fine)));
        else
        {
            md.drag (drag, ParamIDs::eqFreq, juce::jlimit (20.0f, 20000.0f, dragF * std::pow (1000.0f, dx * fine / pw())));
            if (dragCut) md.drag (drag, ParamIDs::eqSlope, (float) juce::jlimit (0, 3, dragSlope + juce::roundToInt (dy / 30.0f)));
            else md.drag (drag, ParamIDs::eqGain, juce::jlimit (-18.0f, 18.0f, dragG - dy * fine / (ph() * 0.5f) * 18.0f));
        }
        poke(); edited();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0)
        {
            if (dragMode == 2) md.end (drag, ParamIDs::eqQ);
            else if (dragMoved) { if (dragMode == 3) md.end (drag, ParamIDs::eqQ); else { md.end (drag, ParamIDs::eqFreq); md.end (drag, dragCut ? ParamIDs::eqSlope : ParamIDs::eqGain); } }
        }
        drag = -1; poke();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const int i = hit (e.position, 14.0f);
        if (i >= 0) { select (i); menu (i); }          // a double click never creates a band
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int i = hit (e.position, 30.0f);
        if (i < 0) i = md.sel;
        if (i < 0 || ! md.shown (i)) return;
        if (i != md.sel) select (i);
        const auto b = md.view (i);
        if (req::isCut (b.type))
        {
            wheelAcc += w.deltaY;
            if (std::abs (wheelAcc) > 0.12f) { md.set (i, ParamIDs::eqSlope, (float) juce::jlimit (0, 3, b.slope + (wheelAcc > 0 ? 1 : -1))); wheelAcc = 0.0f; }
        }
        else md.set (i, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, b.q * std::exp (w.deltaY * 1.6f)));
        poke(); edited();
    }
    void mouseMagnify (const juce::MouseEvent&, float scale) override      // trackpad pinch = band width
    {
        const int i = md.sel;
        if (i < 0 || ! md.shown (i) || req::isCut (md.view (i).type)) return;
        md.set (i, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, md.view (i).q / juce::jlimit (0.5f, 2.0f, scale)));
        poke(); edited();
    }

    void menu (int i)
    {
        juce::PopupMenu m;
        const auto b = md.view (i);
        const bool edge = i == 0 || i == 3;
        if (edge)
        {
            const int shelf = i == 0 ? req::LowShelf : req::HighShelf, cut = i == 0 ? req::LowCut : req::HighCut;
            m.addItem (1, typeName (shelf), true, b.type == shelf);
            m.addItem (2, i == 0 ? "High Pass (Low Cut)" : "Low Pass (High Cut)", true, b.type == cut);
            if (req::isCut (b.type)) { m.addSeparator(); for (int s = 0; s < 4; ++s) m.addItem (10 + s, juce::String (req::slopeDb (s)) + " dB/oct", true, b.slope == s); }
            m.addSeparator();
        }
        m.addItem (20, "Frequency...");
        if (! req::isCut (b.type)) { m.addItem (21, "Gain..."); m.addItem (22, "Q..."); }
        m.addSeparator();
#ifdef MAS_STUDIO
        if (! req::isCut (b.type))
        {
            m.addItem (40, b.dynamic ? "Dynamic Cut: ON (click for static)" : "Convert to Dynamic Cut");
            if (b.dynamic)
            {
                m.addSeparator();
                m.addSectionHeader ("DYNAMIC CUT / THRESHOLD");
                for (int j = 0; j < 5; ++j) m.addItem (50 + j, juce::String (-42 + 6 * j) + " dBFS", true,
                                                        std::abs (b.dynamicThresholdDb - (float) (-42 + 6 * j)) < 0.6f);
                m.addSectionHeader ("MAX REDUCTION");
                for (int j = 0; j < 4; ++j) m.addItem (60 + j, juce::String (2 + j) + " dB", true,
                                                        std::abs (b.dynamicMaxDb - (float) (2 + j)) < 0.2f);
            }
        }
#endif
        m.addItem (30, b.bypass ? "Enable band" : "Bypass");
        m.addItem (31, i < Model::kMain ? "Reset band" : "Remove band");
        juce::Component::SafePointer<Graph> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, i, edge] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& md2 = safe->md;
            if (r == 1) md2.setType (i, i == 0 ? req::LowShelf : req::HighShelf);
            else if (r == 2) md2.setType (i, i == 0 ? req::LowCut : req::HighCut);
            else if (r >= 10 && r < 14) md2.set (i, ParamIDs::eqSlope, (float) (r - 10));
            else if (r >= 20 && r <= 22) { safe->openEntry (i, r == 20 ? ParamIDs::eqFreq : r == 21 ? ParamIDs::eqGain : ParamIDs::eqQ); return; }
#ifdef MAS_STUDIO
            else if (r == 40)
            {
                md2.ensure (i);
                md2.set (i, ParamIDs::eqDynOn, md2.view (i).dynamic ? 0.0f : 1.0f);
                if (md2.view (i).dynamic)
                {
                    md2.set (i, ParamIDs::eqType, (float) req::Bell);
                    md2.set (i, ParamIDs::eqGain, 0.0f);
                }
            }
            else if (r >= 50 && r < 55) md2.set (i, ParamIDs::eqDynThreshold, (float) (-42 + 6 * (r - 50)));
            else if (r >= 60 && r < 64) md2.set (i, ParamIDs::eqDynMax, (float) (2 + r - 60));
#endif
            else if (r == 30) md2.set (i, ParamIDs::eqBypass, md2.view (i).bypass ? 0.0f : 1.0f);
            else if (r == 31) { md2.resetBand (i); if (i >= Model::kMain) md2.sel = -1; }
            juce::ignoreUnused (edge);
            safe->poke(); safe->edited();
        });
    }

    // PEAK TO EQ: one click on a marker prepares a band (Bell, measured frequency, proposed Q, gain 0 dB)
    void peakToEq (int markerId)
    {
        for (const auto& p : markers())
            if (p.id == markerId)
            {
                const float f = p.freq, q = spec::suggestQ (p);
#ifdef MAS_STUDIO
                // Only a confirmed click can arm dynamic processing. Default untouched.
                juce::PopupMenu choice;
                choice.addSectionHeader ("Detected " + fmtFreq (f));
                choice.addItem (1, "DYNAMIC CUT   Q 3 / max 3 dB");
                choice.addItem (2, "STATIC EQ   0 dB / adjust by hand");
                juce::Component::SafePointer<Graph> safe (this);
                choice.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                    [safe, f, q] (int answer)
                    {
                        if (safe == nullptr || (answer != 1 && answer != 2)) return;
                        const bool dyn = answer == 1;
                        const float chosenQ = dyn ? 3.0f : q;
                        auto assign = [safe, f, chosenQ, dyn] (int band)
                        {
                            if (safe == nullptr) return;
                            safe->md.assignPeak (band, f, chosenQ, dyn);
                            safe->hoverMarker = -1;
                            safe->poke(); safe->edited();
                        };
                        const int unused = safe->md.freeBandForPeak();
                        if (unused >= 0) { assign (unused); return; }
                        juce::PopupMenu replace;
                        replace.addSectionHeader ("All bands are used. Replace one:");
                        for (int i : { 1, 2, 4, 5, 6, 7 })
                        {
                            const auto b = safe->md.view (i);
                            replace.addItem (100 + i, juce::String (Model::roleName (i)) + "  (" + fmtFreq (b.freq) + ")");
                        }
                        replace.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (safe.getComponent()).withMousePosition(),
                            [assign] (int r) { if (r >= 100) assign (r - 100); });
                    });
                return;
#else
                const int k = md.freeBandForPeak();
                if (k >= 0) { md.assignPeak (k, f, q); hoverMarker = -1; poke(); edited(); return; }
                juce::PopupMenu m;                                   // no free band: the user chooses, nothing is replaced silently
                m.addSectionHeader ("All bands are in use. Put " + fmtFreq (f) + " on:");
                for (int i : { 1, 2, 4, 5, 6, 7 })
                {
                    const auto b = md.view (i);
                    m.addItem (100 + i, juce::String (Model::roleName (i)) + "   (" + fmtFreq (b.freq) + ", " + fmtGain (b.gainDb) + ")");
                }
                juce::Component::SafePointer<Graph> safe (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, f, q] (int r)
                { if (safe != nullptr && r >= 100) { safe->md.assignPeak (r - 100, f, q); safe->poke(); safe->edited(); } });
                return;
#endif
            }
    }

    void openEntry (int i, int field)
    {
        entryBand = i; entryField = field;
        const auto b = md.view (i);
        entry.setText (field == ParamIDs::eqFreq ? juce::String (juce::roundToInt (b.freq)) : field == ParamIDs::eqGain ? juce::String (b.gainDb, 1) : juce::String (b.q, 2), false);
        const auto p = pointOf (b);
        entry.setBounds (juce::Rectangle<int> (92, 24).withCentre ({ juce::roundToInt (p.x), juce::roundToInt (p.y) - 30 }).constrainedWithin (getLocalBounds().reduced (4)));
        entry.setVisible (true); entry.grabKeyboardFocus(); entry.selectAll();
    }
    void commitEntry()
    {
        auto t = entry.getText().trim().toLowerCase().replaceCharacter (',', '.').replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
        entry.setVisible (false);
        if (entryBand < 0 || ! t.containsAnyOf ("0123456789")) return;
        float v = t.getFloatValue();
        if (entryField == ParamIDs::eqFreq) md.set (entryBand, entryField, juce::jlimit (20.0f, 20000.0f, v * (t.containsChar ('k') ? 1000.0f : 1.0f)));
        else if (entryField == ParamIDs::eqGain) md.set (entryBand, entryField, juce::jlimit (-18.0f, 18.0f, v));
        else md.set (entryBand, entryField, juce::jlimit (0.1f, 18.0f, v));
        poke(); edited();
    }

private:
    void select (int i) { md.sel = i; poke(); edited(); }
    void edited() { repaint(); if (onEdit) onEdit(); }
    float tipAlpha (juce::uint32 now) const { return drag >= 0 ? 1.0f : juce::jlimit (0.0f, 1.0f, ((float) tipUntil - (float) now) / 350.0f); }
    void specPath (const std::vector<float>& d, juce::Path& p, bool closed) const
    {
        const float base = padT + ph();
        if (closed) p.startNewSubPath (padL, base);
        for (size_t x = 0; x < d.size(); ++x)
        {
            const float y = ySpec (d[x]);
            if (x == 0 && ! closed) p.startNewSubPath (padL, y); else p.lineTo (padL + (float) x + 0.5f, y);
        }
        if (closed) { p.lineTo (padL + (float) d.size(), base); p.closeSubPath(); }
    }

    Model& md;
    spec::SpectrumEngine engA, engB;
    std::vector<float> dispA, dispB, peakA;
    juce::TextEditor entry;
    int shownA = -2, shownB = -2, settle = 0, hoverMarker = -1, drag = -1, dragMode = 1, dragSlope = 1, entryBand = -1, entryField = 0;
    float dragF = 1000.0f, dragG = 0.0f, dragQ = 1.0f, wheelAcc = 0.0f;
    bool dragCut = false, dragMoved = false;
    juce::uint32 tipUntil = 0, flashUntil = 0;
    juce::String flash;
};

//==============================================================================
class Page : public juce::Component, private juce::Timer
{
public:
    explicit Page (MasterSpaceAudioProcessor& p)
        : md (p), graph (md),
          bankSeg ({ "WET", "EARLY", "LATE" }, col::gold),
          srcSeg ({ "AUTO", "INPUT", "EARLY", "LATE", "WET" }, col::teal), ppSeg ({ "PRE", "POST", "PRE+POST" }, col::teal),
          smoothSeg ({ "OFF", "1/12", "1/6", "1/3" }, col::teal), spdSeg ({ "SLOW", "MED", "FAST" }, col::teal), tiltSeg ({ "0", "3 dB/oct", "4.5" }, col::teal),
          peakBtn ("PEAK HOLD", col::teal), freezeBtn ("FREEZE", col::teal), zoneBtn (juce::String (juce::CharPointer_UTF8 ("200\xe2\x80\x93" "500")), col::teal),
          anBtn ("ANALYZER ON", col::teal), detectBtn ("PEAK DETECT", col::teal), undoBtn ("UNDO PEAK TO EQ", col::gold)
    {
        md.refresh();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &graph, &bankSeg, &srcSeg, &ppSeg, &smoothSeg, &spdSeg, &tiltSeg, &peakBtn, &freezeBtn, &zoneBtn, &anBtn, &detectBtn, &eqSwitch })
            addAndMakeVisible (c);
        addChildComponent (undoBtn);
        ppSeg.setSelected (1); smoothSeg.setSelected (2); spdSeg.setSelected (1); tiltSeg.setSelected (1); zoneBtn.setOn (true); anBtn.setOn (true); detectBtn.setOn (true);

        graph.onEdit = [this] { sync(); };
        bankSeg.onChange = [this] (int i) { md.bank = i; md.sel = -1; sync(); graph.repaint(); };
        srcSeg.onChange = [this] (int i) { md.src = i; };
        ppSeg.onChange = [this] (int i) { md.pp = i; };
        smoothSeg.onChange = [this] (int i) { md.smooth = i; graph.clearPeak(); };
        spdSeg.onChange = [this] (int i) { md.speed = i; };
        tiltSeg.onChange = [this] (int i) { md.tilt = i == 0 ? 0.0f : i == 1 ? 3.0f : 4.5f; graph.clearPeak(); };
        peakBtn.onClick = [this] { md.peak = ! md.peak; peakBtn.setOn (md.peak); graph.clearPeak(); };
        freezeBtn.onClick = [this] { md.freeze = ! md.freeze; freezeBtn.setOn (md.freeze); graph.repaint(); };
        zoneBtn.onClick = [this] { md.zone = ! md.zone; zoneBtn.setOn (md.zone); graph.repaint(); };
        detectBtn.onClick = [this] { md.detect = ! md.detect; detectBtn.setOn (md.detect); graph.repaint(); };
        anBtn.onClick = [this] { md.anOn = ! md.anOn; anBtn.setOn (md.anOn); anBtn.setText (md.anOn ? "ANALYZER ON" : "ANALYZER OFF"); graph.repaint(); };
        eqSwitch.onClick = [this] { md.setOn (! md.s.on); sync(); graph.repaint(); };
        undoBtn.onClick = [this] { md.undoPeak(); sync(); graph.repaint(); };
        sync();
        startTimerHz (30);
    }
    ~Page() override { stopTimer(); graph.release(); }

    void paint (juce::Graphics& g) override
    {
       #ifdef MAS_STUDIO
        {
            const auto& th = studio::themeOf (*this);
            for (auto& l : captions)
                studio::engraved (g, l.first, l.second.toFloat(), studio::inter (10.0f, true, 0.2f), juce::Colour (0xff9aa6b8), juce::Justification::centredLeft);
            studio::engraved (g, "REVERB EQ", titleArea.toFloat(), studio::inter (12.0f, true, 0.22f), th.gold, juce::Justification::centredLeft);
            return;
        }
       #endif
        auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
        g.setColour (col::panel.withAlpha (0.9f)); g.fillRoundedRectangle (r, 14.0f);
        g.setColour (col::border()); g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        g.setFont (font (10.0f, true, 0.16f)); g.setColour (col::dim);
        for (auto& l : captions) g.drawText (l.first, l.second, juce::Justification::centredLeft);
        g.setFont (font (12.5f, true, 0.14f)); g.setColour (col::gold);
        g.drawText ("REVERB EQ", titleArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        captions.clear();
       #ifdef MAS_STUDIO
        const int W = getWidth(), H = getHeight(), m = 12, rowH = 30;
        int y = 6, x = m;
        titleArea = { x, y, 110, rowH }; x += 110;
        eqSwitch.setBounds (x, y, 64, rowH); x += 84;
       #else
        const int W = getWidth(), H = getHeight(), m = 12, rowH = 26;
        int y = 10, x = m;
        titleArea = { x, y, 92, rowH }; x += 92;
        eqSwitch.setBounds (x, y + 2, 42, 22); x += 60;
       #endif
        captions.push_back ({ "APPLY TO", juce::Rectangle<int> (x, y, 70, rowH) }); x += 72;
        bankSeg.setBounds (x, y, bankSeg.idealWidth(), rowH);
       #ifdef MAS_STUDIO
        undoBtn.setBounds (W - m - undoBtn.idealWidth(), y, undoBtn.idealWidth(), rowH);
       #else
        undoBtn.setBounds (W - m - 150, y, 150, rowH);
       #endif
        const int gy = y + rowH + 8, bottomH = 2 * rowH + 8 + 10;
        graph.setBounds (m, gy, W - 2 * m, H - gy - bottomH - 8);

        int by = H - bottomH; x = m;
        auto put = [&] (juce::Component& c, int w) { c.setBounds (x, by, w, rowH); x += w + 7; };
        auto cap = [&] (const char* t, int w) { captions.push_back ({ t, juce::Rectangle<int> (x, by, w, rowH) }); x += w; };
        cap ("ANALYZER", 74); put (srcSeg, srcSeg.idealWidth()); put (ppSeg, ppSeg.idealWidth()); put (freezeBtn, freezeBtn.idealWidth());
        x += 6; cap ("SMOOTH", 60); put (smoothSeg, smoothSeg.idealWidth());
        by += rowH + 8; x = m;
        put (anBtn, 112); put (detectBtn, detectBtn.idealWidth()); put (peakBtn, peakBtn.idealWidth()); put (zoneBtn, zoneBtn.idealWidth()); x += 6;
        cap ("AVERAGE", 66); put (spdSeg, spdSeg.idealWidth()); x += 6;
        cap ("SLOPE", 48); put (tiltSeg, tiltSeg.idealWidth());
    }

    Model& model() { return md; }
    Graph& view() { return graph; }

private:
    void sync()
    {
        md.refresh();
        eqSwitch.setOn (md.s.on);
        bankSeg.setSelected (md.bank);
        undoBtn.setVisible (md.undoValid);
    }
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.001, 0.1, (now - last) * 0.001); last = now;
        sync();                                           // follows automation, presets, A/B, undo
        eqSwitch.advance();
        graph.tick (dt);
        if (graph.shownInTree()) graph.repaint();
    }

    Model md;
    Graph graph;
    Seg bankSeg, srcSeg, ppSeg, smoothSeg, spdSeg, tiltSeg;
    Pill peakBtn, freezeBtn, zoneBtn, anBtn, detectBtn, undoBtn;
    Switch eqSwitch;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    juce::Rectangle<int> titleArea;
    double last = 0.0;
};
} // namespace eqlab
