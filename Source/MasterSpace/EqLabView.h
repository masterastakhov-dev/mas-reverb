// MASTER SPACE 1.5 EQ LAB — the REVERB EQ page: interactive curve, points, spectral analyzer.
// Included by PluginEditor.cpp (MAS_EQ builds only). The look and the handling follow the approved prototype:
// graphite background, thin grid, gold curve and points, teal spectrum.

#pragma once

namespace eqlab
{
namespace col
{
    const juce::Colour gold { 0xffe9b85e }, goldHi { 0xfff4cd82 }, teal { 0xff33c6d8 }, tealHi { 0xff5adceb };
    const juce::Colour text { 0xffdfe5ee }, dim { 0xff8490a3 }, bgTop { 0xff0a0e14 }, bgBot { 0xff080b10 }, panel { 0xff10151d };
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
    void setOn (bool b) { target = b; }
    void advance() { const float t = target ? 1.0f : 0.0f; if (pos != t) { pos += juce::jlimit (-0.25f, 0.25f, t - pos); repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f).interpolatedWith (col::gold.withAlpha (0.35f), pos)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float d = r.getHeight() - 6.0f, x = r.getX() + 3.0f + pos * (r.getWidth() - d - 6.0f);
        g.setColour (juce::Colour (0xffaaaabb).interpolatedWith (col::gold, pos)); g.fillEllipse (x, r.getY() + 3.0f, d, d);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
private:
    bool target = false; float pos = 0.0f;
};

// thin horizontal slider, value 0..1
class Bar : public juce::Component
{
public:
    Bar() { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
    std::function<void (float)> onDrag; std::function<void()> onStart, onEnd;
    void setValue (float v) { v = juce::jlimit (0.0f, 1.0f, v); if (std::abs (v - value) > 1e-4f) { value = v; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        const float y = (float) getHeight() * 0.5f, x0 = 8.0f, x1 = (float) getWidth() - 8.0f, x = x0 + value * (x1 - x0);
        g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (x0, y - 1.5f, x1 - x0, 3.0f, 1.5f);
        g.setColour (col::gold); g.fillRoundedRectangle (x0, y - 1.5f, x - x0, 3.0f, 1.5f);
        g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (onStart) onStart(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setValue (((float) e.x - 8.0f) / ((float) getWidth() - 16.0f)); if (onDrag) onDrag (value); }
    void mouseUp (const juce::MouseEvent&) override { if (onEnd) onEnd(); }
private:
    float value = 0.5f;
};

//==============================================================================
// access to the EQ parameters of the plugin
struct Model
{
    explicit Model (MasterSpaceAudioProcessor& p) : proc (p)
    {
        for (int b = 0; b < 3; ++b) for (int i = 0; i < req::kBands; ++i) for (int f = 0; f < ParamIDs::eqNumFields; ++f)
            prm[b][i][f] = p.apvts.getParameter (ParamIDs::eqId (b, i, f));
        onPrm = p.apvts.getParameter (ParamIDs::eqOn);
    }
    void refresh() { s = proc.readEq(); }
    const req::BandParams& band (int i) const { return s.band[bank][i]; }
    void set (int i, int field, float plain) { auto* r = prm[bank][i][field]; r->setValueNotifyingHost (r->convertTo0to1 (plain)); refresh(); }
    void begin (int i, int field) { prm[bank][i][field]->beginChangeGesture(); }
    void end (int i, int field) { prm[bank][i][field]->endChangeGesture(); }
    void setOn (bool on) { onPrm->beginChangeGesture(); onPrm->setValueNotifyingHost (on ? 1.0f : 0.0f); onPrm->endChangeGesture(); refresh(); }
    void setType (int i, int type)
    {
        const bool was = req::isCut (band (i).type), now = req::isCut (type);
        set (i, ParamIDs::eqType, (float) type);
        if (now && ! was) { set (i, ParamIDs::eqQ, 0.71f); set (i, ParamIDs::eqGain, 0.0f); }
        if (! now && was) set (i, ParamIDs::eqQ, 1.0f);
        set (i, ParamIDs::eqUsed, 1.0f);
    }
    int firstFree() const { for (int i = 0; i < req::kBands; ++i) if (! band (i).used) return i; return -1; }
    void selectFirstUsed() { sel = 0; for (int i = 0; i < req::kBands; ++i) if (band (i).used) { sel = i; break; } }

    MasterSpaceAudioProcessor& proc;
    req::Settings s;
    int bank = 0, sel = 0;
    bool eqMode = true;                       // false = SPECTRAL (analyzer only)
    // analyzer options (display only: not saved with the project)
    bool anOn = true, peak = false, freeze = false, zone = true;
    int src = 0;                              // 0 AUTO (follows the bank), 1 INPUT, 2 EARLY, 3 LATE, 4 WET
    int pp = 1;                               // 0 PRE, 1 POST, 2 PRE + POST
    int speed = 1, res = 2;
    float tilt = 3.0f;
    juce::RangedAudioParameter* prm[3][req::kBands][ParamIDs::eqNumFields] = {};
    juce::RangedAudioParameter* onPrm = nullptr;
};

//==============================================================================
class Graph : public juce::Component
{
public:
    explicit Graph (Model& m) : md (m) { setOpaque (false); }
    std::function<void()> onEdit;       // selection or a value changed

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

    // ---- analyzer: FFT of the real signal taken from the DSP taps
    void tick (double dt)
    {
        auto& an = md.proc.analyzer;
        const int srcKey = md.src == 0 ? (md.bank == 0 ? 4 : md.bank == 1 ? 2 : 3) : md.src;     // 1 in, 2 early, 3 late, 4 wet
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
        if (tA != shownA || tB != shownB) { clearSpectrum(); shownA = tA; shownB = tB; settle = 3; }
        an.request[0].store (tA, std::memory_order_relaxed);
        an.request[1].store (tB, std::memory_order_relaxed);
        if (! run || md.freeze) return;
        if (settle > 0) { --settle; return; }          // let the ring fill with the newly chosen tap
        const int n = juce::roundToInt (pw());
        if (n < 8) return;
        if ((int) dispA.size() != n) { dispA.assign ((size_t) n, -200.0f); dispB.assign ((size_t) n, -200.0f); peakA.assign ((size_t) n, -200.0f); }
        spectrum (0, dispA, dt, &peakA);
        if (tB >= 0) spectrum (1, dispB, dt, nullptr);
    }
    bool shownInTree() const { for (const juce::Component* c = this; c != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return false; return true; }
    void clearSpectrum() { std::fill (dispA.begin(), dispA.end(), -200.0f); std::fill (dispB.begin(), dispB.end(), -200.0f); std::fill (peakA.begin(), peakA.end(), -200.0f); }
    void clearPeak() { std::fill (peakA.begin(), peakA.end(), -200.0f); }
    void release() { md.proc.analyzer.request[0] = -1; md.proc.analyzer.request[1] = -1; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (col::bgTop, 0, 0, col::bgBot, 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::border()); g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);
        const float x0 = padL, x1 = (float) getWidth() - padR, y0 = padT, y1 = padT + ph();
        const bool avail = md.proc.eqAvailable();

        if (md.zone)
        {
            g.setColour (col::gold.withAlpha (0.07f)); g.fillRect (xOf (200.0f), y0, xOf (500.0f) - xOf (200.0f), y1 - y0);
            g.setColour (col::gold.withAlpha (0.5f)); g.setFont (font (9.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("200\xe2\x80\x93" "500")), juce::Rectangle<float> (xOf (200.0f), y0, xOf (500.0f) - xOf (200.0f), 12.0f), juce::Justification::centred);
        }
        // grid
        for (int d = 10; d <= 10000; d *= 10)
            for (int m = 1; m < 10; ++m)
            {
                const float f = (float) (d * m);
                if (f < 20.0f || f > 20000.0f) continue;
                g.setColour (juce::Colour (0xffc8d7f0).withAlpha (m == 1 ? 0.10f : 0.045f));
                g.fillRect (std::round (xOf (f)), y0, 1.0f, y1 - y0);
            }
        g.setFont (font (10.0f)); g.setColour (col::dim.withAlpha (0.9f));
        const std::pair<float, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (auto& p : fl) g.drawText (p.second, juce::Rectangle<float> (juce::jlimit (x0 - 6.0f, x1 - 24.0f, xOf (p.first) - 15.0f), y1 + 4.0f, 30.0f, 14.0f), juce::Justification::centred);
        for (int gd = -18; gd <= 18; gd += 6)
        {
            const float y = std::round (yOf ((float) gd));
            g.setColour (juce::Colour (0xffc8d7f0).withAlpha (gd == 0 ? 0.16f : 0.05f)); g.fillRect (x0, y, x1 - x0, 1.0f);
            if (std::abs (gd) < 18) { g.setColour (col::dim.withAlpha (0.9f)); g.drawText ((gd > 0 ? "+" : "") + juce::String (gd), juce::Rectangle<float> (0.0f, y - 7.0f, x0 - 6.0f, 14.0f), juce::Justification::centredRight); }
        }

        // analyzer
        if (md.anOn && ! dispA.empty())
        {
            g.saveState(); g.reduceClipRegion (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).toNearestInt());
            juce::Path fill, line; specPath (dispA, fill, true); specPath (dispA, line, false);
            g.setGradientFill (juce::ColourGradient (col::teal.withAlpha (0.55f), 0, y0, col::teal.withAlpha (0.08f), 0, y1, false)); g.fillPath (fill);
            g.setColour (col::tealHi.withAlpha (0.75f)); g.strokePath (line, juce::PathStrokeType (1.0f));
            if (shownB >= 0) { juce::Path pre; specPath (dispB, pre, false); juce::Path dashed; const float dl[] = { 3.0f, 3.0f }; juce::PathStrokeType (1.0f).createDashedStroke (dashed, pre, dl, 2); g.setColour (col::text.withAlpha (0.55f)); g.fillPath (dashed); }
            if (md.peak) { juce::Path pk; specPath (peakA, pk, false); g.setColour (juce::Colour (0xff96ebf5).withAlpha (0.5f)); g.strokePath (pk, juce::PathStrokeType (0.8f)); }
            g.restoreState();
            const int srcKey = md.src == 0 ? (md.bank == 0 ? 4 : md.bank == 1 ? 2 : 3) : md.src;
            const char* nm[] = { "", "INPUT", "EARLY", "LATE", "WET" };
            juce::String t (nm[srcKey]);
            if (srcKey != 1) t << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << (md.pp == 0 ? "PRE EQ" : md.pp == 1 ? "POST EQ" : "POST  (dashed: PRE)");
            if (md.freeze) t << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << "FREEZE";
            g.setFont (font (9.0f, true, 0.06f)); g.setColour (col::teal.withAlpha (0.9f));
            g.drawText (t, juce::Rectangle<float> (x1 - 260.0f, y0 + 2.0f, 256.0f, 12.0f), juce::Justification::centredRight);
        }

        // EQ curve and points: the response of the filters as they are set
        if (md.eqMode)
        {
            const float a = (md.s.on && avail) ? 1.0f : 0.35f;
            const double fs = juce::jmax (8000.0, md.proc.getSampleRate());
            const int n = juce::jmax (8, juce::roundToInt (pw()));
            const auto& sb = md.band (md.sel);
            juce::Path curve, selFill;
            selFill.startNewSubPath (x0, yOf (0.0f));
            for (int x = 0; x <= n; ++x)
            {
                const double f = juce::jmin ((double) fOf (x0 + (float) x), 0.499 * fs);
                double tot = 0.0;
                for (int i = 0; i < req::kBands; ++i) tot += req::bandMagnitudeDb (md.band (i), f, fs);
                const float y = yOf ((float) juce::jlimit (-40.0, 40.0, tot));
                if (x == 0) curve.startNewSubPath (x0, y); else curve.lineTo (x0 + (float) x, y);
                selFill.lineTo (x0 + (float) x, yOf ((float) juce::jlimit (-40.0, 40.0, req::bandMagnitudeDb (sb, f, fs))));
            }
            selFill.lineTo (x1, yOf (0.0f)); selFill.closeSubPath();
            g.saveState(); g.reduceClipRegion (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).toNearestInt());
            if (sb.used && ! sb.bypass) { g.setColour (col::gold.withAlpha (0.10f * a)); g.fillPath (selFill); }
            g.setColour (col::gold.withAlpha (a)); g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.restoreState();
            for (int i = 0; i < req::kBands; ++i)
            {
                const auto& bd = md.band (i);
                if (! bd.used) continue;
                const float x = xOf (bd.freq), y = yOf (req::isCut (bd.type) ? 0.0f : bd.gainDb), rr = i == md.sel ? 6.5f : 4.8f;
                const bool sel = i == md.sel, live = ! bd.bypass;
                g.setColour (live && ! sel ? col::gold.withAlpha (a) : juce::Colour (0xff0b0f15)); g.fillEllipse (x - rr, y - rr, 2 * rr, 2 * rr);
                g.setColour (live ? col::goldHi.withAlpha (a) : col::dim.withAlpha (0.8f)); g.drawEllipse (x - rr, y - rr, 2 * rr, 2 * rr, sel ? 2.2f : 1.4f);
                if (sel || i == hover) { g.setFont (font (9.5f, true)); g.setColour (live ? col::goldHi : col::dim); g.drawText (juce::String (i + 1), juce::Rectangle<float> (x - 10.0f, y - 22.0f, 20.0f, 12.0f), juce::Justification::centred); }
            }
            if (sb.used)
            {
                juce::String t; const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
                t << (md.sel + 1) << dot << typeName (sb.type) << dot << fmtFreq (sb.freq) << dot
                  << (req::isCut (sb.type) ? juce::String (req::slopeDb (sb.slope)) + " dB/oct" : fmtGain (sb.gainDb)) << dot << "Q " << juce::String (sb.q, 2) << (sb.bypass ? dot + "bypass" : juce::String());
                g.setFont (font (11.0f)); g.setColour (col::text.withAlpha (0.92f));
                g.drawText (t, juce::Rectangle<float> (x0 + 6.0f, y0 + 1.0f, 330.0f, 14.0f), juce::Justification::centredLeft);
            }
            g.setFont (font (10.5f, true, 0.1f)); g.setColour (col::dim.withAlpha (0.95f));
            if (! avail) g.drawText ("REVERB EQ: ALGO MODE, ENGINE 1.2", juce::Rectangle<float> (x0, y1 - 20.0f, x1 - x0, 14.0f), juce::Justification::centred);
            else if (! md.s.on) g.drawText ("REVERB EQ OFF", juce::Rectangle<float> (x0, y1 - 20.0f, x1 - x0, 14.0f), juce::Justification::centred);
        }
        if (flashUntil > juce::Time::getMillisecondCounter())
        {
            g.setFont (font (11.0f, true)); g.setColour (col::goldHi);
            g.drawText (flash, juce::Rectangle<float> (x0, y1 - 38.0f, x1 - x0, 14.0f), juce::Justification::centred);
        }
    }

    // ---- handling (as in the approved prototype)
    int hit (juce::Point<float> p, float rad) const
    {
        int best = -1; float bd = rad;
        for (int i = 0; i < req::kBands; ++i)
        {
            const auto& b = md.band (i);
            if (! b.used) continue;
            const float d = p.getDistanceFrom ({ xOf (b.freq), yOf (req::isCut (b.type) ? 0.0f : b.gainDb) });
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = md.eqMode ? hit (e.position, 14.0f) : -1;
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hover != -1) { hover = -1; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! md.eqMode) return;
        const int i = hit (e.position, 14.0f);
        if (e.mods.isPopupMenu()) { if (i >= 0) { select (i); menu (i); } return; }
        if (i < 0) return;
        select (i);
        const auto& b = md.band (i);
        drag = i; dragF = b.freq; dragG = b.gainDb; dragSlope = b.slope; dragCut = req::isCut (b.type);
        md.begin (i, ParamIDs::eqFreq); md.begin (i, dragCut ? ParamIDs::eqSlope : ParamIDs::eqGain);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
        const float dx = (float) e.getDistanceFromDragStartX(), dy = (float) e.getDistanceFromDragStartY();
        md.set (drag, ParamIDs::eqFreq, juce::jlimit (20.0f, 20000.0f, dragF * std::pow (1000.0f, dx * fine / pw())));
        if (dragCut) md.set (drag, ParamIDs::eqSlope, (float) juce::jlimit (0, 3, dragSlope + juce::roundToInt (dy / 30.0f)));
        else md.set (drag, ParamIDs::eqGain, juce::jlimit (-18.0f, 18.0f, dragG - dy * fine / (ph() * 0.5f) * 18.0f));
        edited();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0) { md.end (drag, ParamIDs::eqFreq); md.end (drag, dragCut ? ParamIDs::eqSlope : ParamIDs::eqGain); }
        drag = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! md.eqMode || e.mods.isPopupMenu()) return;
        const int i = hit (e.position, 14.0f);
        if (i >= 0)
        {
            md.set (i, ParamIDs::eqGain, 0.0f); md.set (i, ParamIDs::eqQ, req::isCut (md.band (i).type) ? 0.71f : 1.0f); md.set (i, ParamIDs::eqBypass, 0.0f);
            select (i);
        }
        else
        {
            const int k = md.firstFree();
            if (k < 0) { flash = "All 8 bands are in use"; flashUntil = juce::Time::getMillisecondCounter() + 1600; repaint(); return; }
            const float f = fOf (e.position.x);
            const int type = f < 35.0f ? req::LowCut : f > 15000.0f ? req::HighCut : req::Bell;
            md.set (k, ParamIDs::eqType, (float) type); md.set (k, ParamIDs::eqFreq, f);
            md.set (k, ParamIDs::eqGain, type == req::Bell ? std::round (gOf (e.position.y) * 2.0f) * 0.5f : 0.0f);
            md.set (k, ParamIDs::eqQ, type == req::Bell ? 1.0f : 0.71f); md.set (k, ParamIDs::eqSlope, 1.0f);
            md.set (k, ParamIDs::eqBypass, 0.0f); md.set (k, ParamIDs::eqUsed, 1.0f);
            select (k);
        }
        edited();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! md.eqMode) return;
        int i = hit (e.position, 30.0f);
        if (i < 0) i = md.sel;
        if (! md.band (i).used) return;
        select (i);
        md.begin (i, ParamIDs::eqQ);
        md.set (i, ParamIDs::eqQ, juce::jlimit (0.1f, 18.0f, md.band (i).q * std::exp (w.deltaY * 1.6f)));
        md.end (i, ParamIDs::eqQ);
        edited();
    }
    void menu (int i)
    {
        juce::PopupMenu m;
        const auto& b = md.band (i);
        for (int t = 0; t < req::numTypes; ++t) m.addItem (1 + t, typeName (t), true, b.type == t);
        m.addSeparator();
        m.addItem (20, b.bypass ? "Enable band" : "Bypass");
        m.addItem (21, "Remove band");
        juce::Component::SafePointer<Graph> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, i] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& md2 = safe->md;
            if (r >= 1 && r <= req::numTypes) md2.setType (i, r - 1);
            else if (r == 20) md2.set (i, ParamIDs::eqBypass, md2.band (i).bypass ? 0.0f : 1.0f);
            else if (r == 21) md2.set (i, ParamIDs::eqUsed, 0.0f);
            safe->edited();
        });
    }

private:
    void select (int i) { md.sel = i; edited(); }
    void edited() { repaint(); if (onEdit) onEdit(); }

    void spectrum (int slot, std::vector<float>& disp, double dt, std::vector<float>* peak)
    {
        const int order = 11 + juce::jlimit (0, 2, md.res), N = 1 << order;
        if (fftOrder != order) { fft = std::make_unique<juce::dsp::FFT> (order); fftOrder = order; buf.assign ((size_t) (2 * N), 0.0f); win.resize ((size_t) N);
                                 for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1)); }
        md.proc.analyzer.read (slot, buf.data(), N);
        for (int i = 0; i < N; ++i) buf[(size_t) i] *= win[(size_t) i];
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (buf.data());
        const float fs = (float) md.proc.analyzer.sampleRate.load(), hz = fs / (float) N, norm = 4.0f / (float) N;   // full-scale sine = 0 dB
        const float rel = (md.speed == 0 ? 14.0f : md.speed == 1 ? 34.0f : 90.0f) * (float) dt, att = md.speed == 0 ? 0.35f : md.speed == 1 ? 0.6f : 0.9f;
        const int n = (int) disp.size(), half = N / 2;
        for (int x = 0; x < n; ++x)
        {
            const float f0 = fOf (padL + (float) x), f1 = fOf (padL + (float) x + 1.0f), p0 = f0 / hz, p1 = f1 / hz;
            float mag = 0.0f;
            if (p1 - p0 < 1.0f) { const float pc = 0.5f * (p0 + p1); const int i = juce::jlimit (0, half - 2, (int) pc); const float fr = pc - (float) i; mag = buf[(size_t) i] + fr * (buf[(size_t) i + 1] - buf[(size_t) i]); }
            else for (int i = (int) std::ceil (p0); i <= (int) p1 && i < half; ++i) mag = juce::jmax (mag, buf[(size_t) i]);
            float v = 20.0f * std::log10 (mag * norm + 1e-10f) + md.tilt * std::log2 (std::sqrt (f0 * f1) / 1000.0f);
            if (f0 > 0.5f * fs) v = -200.0f;
            disp[(size_t) x] = v > disp[(size_t) x] ? disp[(size_t) x] + (v - disp[(size_t) x]) * att : juce::jmax (v, disp[(size_t) x] - rel);
            if (peak != nullptr) (*peak)[(size_t) x] = juce::jmax ((*peak)[(size_t) x] - 5.0f * (float) dt, disp[(size_t) x]);
        }
    }
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
    std::vector<float> dispA, dispB, peakA, buf, win;
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrder = 0, shownA = -2, shownB = -2, settle = 0, hover = -1, drag = -1, dragSlope = 1;
    float dragF = 1000.0f, dragG = 0.0f;
    bool dragCut = false;
    juce::String flash; juce::uint32 flashUntil = 0;
};

//==============================================================================
class Page : public juce::Component, private juce::Timer
{
public:
    explicit Page (MasterSpaceAudioProcessor& p)
        : md (p), graph (md),
          modeSeg ({ "REVERB EQ", "SPECTRAL" }, col::gold), bankSeg ({ "WET", "EARLY", "LATE" }, col::gold),
          srcSeg ({ "AUTO", "INPUT", "EARLY", "LATE", "WET" }, col::teal), ppSeg ({ "PRE", "POST", "PRE+POST" }, col::teal),
          spdSeg ({ "SLOW", "MED", "FAST" }, col::teal), resSeg ({ "LOW", "MED", "HIGH" }, col::teal), tiltSeg ({ "0", "3 dB/oct", "4.5" }, col::teal),
          peakBtn ("PEAK HOLD", col::teal), freezeBtn ("FREEZE", col::teal), zoneBtn (juce::String (juce::CharPointer_UTF8 ("200\xe2\x80\x93" "500")), col::teal), anBtn ("ANALYZER ON", col::teal),
          typeBtn ("Bell", col::gold), slopeBtn ("24 dB/oct", col::gold), bandOn ("BAND ON", col::gold), bandDel ("REMOVE", col::gold)
    {
        md.refresh(); md.selectFirstUsed();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &graph, &modeSeg, &bankSeg, &srcSeg, &ppSeg, &spdSeg, &resSeg, &tiltSeg, &peakBtn, &freezeBtn, &zoneBtn, &anBtn,
                                                                           &typeBtn, &slopeBtn, &bandOn, &bandDel, &eqSwitch, &qBar, &fFreq, &fGain, &fQ })
            addAndMakeVisible (c);
        for (int i = 0; i < req::kBands; ++i) { auto* c = chips.add (new Pill (juce::String (i + 1), col::gold)); addAndMakeVisible (c); c->onClick = [this, i] { md.sel = i; sync(); graph.repaint(); }; }
        ppSeg.setSelected (1); spdSeg.setSelected (1); resSeg.setSelected (2); tiltSeg.setSelected (1); zoneBtn.setOn (true); anBtn.setOn (true);

        graph.onEdit = [this] { sync(); };
        modeSeg.onChange = [this] (int i) { md.eqMode = i == 0; graph.repaint(); };
        bankSeg.onChange = [this] (int i) { md.bank = i; md.selectFirstUsed(); sync(); graph.repaint(); };
        srcSeg.onChange = [this] (int i) { md.src = i; };
        ppSeg.onChange = [this] (int i) { md.pp = i; };
        spdSeg.onChange = [this] (int i) { md.speed = i; };
        resSeg.onChange = [this] (int i) { md.res = i; };
        tiltSeg.onChange = [this] (int i) { md.tilt = i == 0 ? 0.0f : i == 1 ? 3.0f : 4.5f; graph.clearSpectrum(); };
        peakBtn.onClick = [this] { md.peak = ! md.peak; peakBtn.setOn (md.peak); graph.clearPeak(); };
        freezeBtn.onClick = [this] { md.freeze = ! md.freeze; freezeBtn.setOn (md.freeze); graph.repaint(); };
        zoneBtn.onClick = [this] { md.zone = ! md.zone; zoneBtn.setOn (md.zone); graph.repaint(); };
        anBtn.onClick = [this] { md.anOn = ! md.anOn; anBtn.setOn (md.anOn); anBtn.setText (md.anOn ? "ANALYZER ON" : "ANALYZER OFF"); graph.repaint(); };
        eqSwitch.onClick = [this] { md.setOn (! md.s.on); sync(); graph.repaint(); };

        typeBtn.onClick = [this]
        {
            juce::PopupMenu m;
            for (int t = 0; t < req::numTypes; ++t) m.addItem (1 + t, typeName (t), true, md.band (md.sel).type == t);
            juce::Component::SafePointer<Page> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typeBtn), [safe] (int r) { if (safe != nullptr && r > 0) { safe->md.setType (safe->md.sel, r - 1); safe->sync(); safe->graph.repaint(); } });
        };
        slopeBtn.onClick = [this]
        {
            juce::PopupMenu m;
            for (int s = 0; s < 4; ++s) m.addItem (1 + s, juce::String (req::slopeDb (s)) + " dB/oct", true, md.band (md.sel).slope == s);
            juce::Component::SafePointer<Page> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slopeBtn), [safe] (int r) { if (safe != nullptr && r > 0) { safe->ensure(); safe->md.set (safe->md.sel, ParamIDs::eqSlope, (float) (r - 1)); safe->sync(); safe->graph.repaint(); } });
        };
        bandOn.onClick = [this]
        {
            const auto& b = md.band (md.sel);
            if (! b.used) md.set (md.sel, ParamIDs::eqUsed, 1.0f); else md.set (md.sel, ParamIDs::eqBypass, b.bypass ? 0.0f : 1.0f);
            sync(); graph.repaint();
        };
        bandDel.onClick = [this] { md.set (md.sel, ParamIDs::eqUsed, 0.0f); sync(); graph.repaint(); };
        qBar.onStart = [this] { ensure(); md.begin (md.sel, ParamIDs::eqQ); };
        qBar.onDrag = [this] (float v) { md.set (md.sel, ParamIDs::eqQ, 0.1f * std::pow (180.0f, v)); sync(); graph.repaint(); };
        qBar.onEnd = [this] { md.end (md.sel, ParamIDs::eqQ); };

        auto field = [this] (juce::Label& l, int fieldId, float lo, float hi)
        {
            l.setEditable (true, true, false);
            l.setJustificationType (juce::Justification::centredRight);
            l.setFont (font (13.5f));
            l.setColour (juce::Label::textColourId, col::text);
            l.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
            l.setColour (juce::Label::outlineColourId, col::border());
            l.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::black.withAlpha (0.6f));
            l.setColour (juce::Label::textWhenEditingColourId, col::text);
            l.setColour (juce::Label::outlineWhenEditingColourId, col::gold);
            l.onTextChange = [this, &l, fieldId, lo, hi]
            {
                auto t = l.getText().trim().toLowerCase().replaceCharacter (',', '.').replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
                const bool k = fieldId == ParamIDs::eqFreq && t.containsChar ('k');
                if (t.containsAnyOf ("0123456789")) { ensure(); md.set (md.sel, fieldId, juce::jlimit (lo, hi, t.getFloatValue() * (k ? 1000.0f : 1.0f))); }
                sync (true); graph.repaint();
            };
        };
        field (fFreq, ParamIDs::eqFreq, 20.0f, 20000.0f); field (fGain, ParamIDs::eqGain, -18.0f, 18.0f); field (fQ, ParamIDs::eqQ, 0.1f, 18.0f);
        sync();
        startTimerHz (30);
    }
    ~Page() override { stopTimer(); graph.release(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
        g.setColour (col::panel.withAlpha (0.85f)); g.fillRoundedRectangle (r, 14.0f);
        g.setColour (col::border()); g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        g.setFont (font (10.0f, true, 0.16f)); g.setColour (col::dim);
        for (auto& l : captions) g.drawText (l.first, l.second, juce::Justification::centredLeft);
        g.setFont (font (12.5f, true, 0.14f)); g.setColour (col::gold);
        g.drawText ("REVERB EQ", titleArea, juce::Justification::centredLeft);
        g.setFont (font (12.0f)); g.setColour (col::dim);
        for (auto& l : fieldNames) g.drawText (l.first, l.second, juce::Justification::centredLeft);
    }

    void resized() override
    {
        captions.clear(); fieldNames.clear();
        const int W = getWidth(), H = getHeight(), m = 12, sideW = 222, rowH = 26;
        int y = 10;
        modeSeg.setBounds (m, y, modeSeg.idealWidth(), rowH);
        const int gx1 = W - m - sideW - 12;
        bankSeg.setBounds (gx1 - bankSeg.idealWidth(), y, bankSeg.idealWidth(), rowH);
        captions.push_back ({ "APPLY TO", juce::Rectangle<int> (bankSeg.getX() - 74, y, 70, rowH) });
        const int sx = W - m - sideW;
        titleArea = { sx, y, 120, rowH };
        eqSwitch.setBounds (W - m - 42, y + 2, 42, 22);

        const int gy = y + rowH + 8, bottomH = 2 * rowH + 8 + 10;
        graph.setBounds (m, gy, gx1 - m, H - gy - bottomH - 8);

        // side panel
        int sy = gy;
        const int cw = (sideW - 7 * 4) / 8;
        for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds (sx + i * (cw + 4), sy, cw, 28);
        sy += 38;
        auto row = [&] (const char* name, juce::Component& c, int h = 30) { fieldNames.push_back ({ name, juce::Rectangle<int> (sx, sy, 50, h) }); c.setBounds (sx + 54, sy, sideW - 54, h); sy += h + 8; };
        row ("Type", typeBtn); row ("Freq", fFreq);
        gainRowY = sy; row ("Gain", fGain); slopeBtn.setBounds (fGain.getBounds());
        row ("Q", fQ);
        qBar.setBounds (sx + 54, sy - 2, sideW - 54, 20); sy += 26;
        bandOn.setBounds (sx, sy, (sideW - 6) / 2, 30); bandDel.setBounds (sx + (sideW - 6) / 2 + 6, sy, (sideW - 6) / 2, 30);

        // analyzer rows
        int by = H - bottomH, x = m;
        auto put = [&] (juce::Component& c, int w) { c.setBounds (x, by, w, rowH); x += w + 7; };
        auto cap = [&] (const char* t, int w) { captions.push_back ({ t, juce::Rectangle<int> (x, by, w, rowH) }); x += w; };
        cap ("ANALYZER", 74); put (srcSeg, srcSeg.idealWidth()); put (ppSeg, ppSeg.idealWidth());
        put (peakBtn, peakBtn.idealWidth()); put (freezeBtn, freezeBtn.idealWidth()); put (zoneBtn, zoneBtn.idealWidth()); put (anBtn, 112);
        by += rowH + 8; x = m;
        cap ("SPEED", 50); put (spdSeg, spdSeg.idealWidth()); x += 6;
        cap ("RESOLUTION", 86); put (resSeg, resSeg.idealWidth()); x += 6;
        cap ("SLOPE", 48); put (tiltSeg, tiltSeg.idealWidth());
    }

private:
    void ensure() { if (! md.band (md.sel).used) md.set (md.sel, ParamIDs::eqUsed, 1.0f); }
    void sync (bool force = false)
    {
        md.refresh();
        const auto& b = md.band (md.sel);
        for (int i = 0; i < chips.size(); ++i) { chips[i]->setOn (i == md.sel); chips[i]->setDim (! md.band (i).used && i != md.sel); chips[i]->setText (md.band (i).used && md.band (i).bypass ? juce::String (i + 1) + "*" : juce::String (i + 1)); }
        typeBtn.setText (typeName (b.type));
        const bool cut = req::isCut (b.type);
        fGain.setVisible (! cut); slopeBtn.setVisible (cut);
        slopeBtn.setText (juce::String (req::slopeDb (b.slope)) + " dB/oct");
        if (! fieldNames.empty()) { auto& gn = fieldNames[2]; const char* want = cut ? "Slope" : "Gain"; if (gn.first != juce::String (want)) { gn.first = want; repaint(); } }
        auto put = [force] (juce::Label& l, const juce::String& t) { if ((force || ! l.isBeingEdited()) && l.getText() != t) l.setText (t, juce::dontSendNotification); };
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        put (fFreq, b.used ? fmtFreq (b.freq) : dash); put (fGain, b.used ? fmtGain (b.gainDb) : dash); put (fQ, b.used ? juce::String (b.q, 2) : dash);
        qBar.setValue (std::log (b.q / 0.1f) / std::log (180.0f));
        bandOn.setText (b.used ? (b.bypass ? "BYPASSED" : "BAND ON") : "ADD BAND"); bandOn.setOn (b.used && ! b.bypass);
        bandDel.setDim (! b.used);
        eqSwitch.setOn (md.s.on);
        bankSeg.setSelected (md.bank);
    }
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.001, 0.1, (now - last) * 0.001); last = now;
        const auto before = md.s;
        sync();                                           // follows automation, presets, A/B, undo
        eqSwitch.advance();
        graph.tick (dt);
        if (! graph.shownInTree()) return;
        if (md.anOn || std::memcmp (&before, &md.s, sizeof (before)) != 0) graph.repaint();
    }

    Model md;
    Graph graph;
    Seg modeSeg, bankSeg, srcSeg, ppSeg, spdSeg, resSeg, tiltSeg;
    Pill peakBtn, freezeBtn, zoneBtn, anBtn, typeBtn, slopeBtn, bandOn, bandDel;
    Switch eqSwitch;
    Bar qBar;
    juce::Label fFreq, fGain, fQ;
    juce::OwnedArray<Pill> chips;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions, fieldNames;
    juce::Rectangle<int> titleArea;
    int gainRowY = 0;
    double last = 0.0;
};
} // namespace eqlab
