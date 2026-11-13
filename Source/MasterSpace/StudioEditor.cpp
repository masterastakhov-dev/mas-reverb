#include "StudioEditor.h"
#include "BinaryData.h"

namespace studio
{
namespace
{
    constexpr float kTau = juce::MathConstants<float>::twoPi;
    constexpr int kBarH = 52, kBodyH = 300, kBezelH = 24, kContentH = 380, kPageH = 440;

    juce::Colour rgb (int r, int g, int b) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b); }
    float easeInOut (float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f; }

    void setPlain (juce::AudioProcessorValueTreeState& s, const char* id, float plain)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    }
    float plainOf (juce::AudioProcessorValueTreeState& s, const char* id)
    {
        auto* v = s.getRawParameterValue (id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::TextButton* makeKey (juce::Component& parent, juce::TextButton& b, bool gold = false, bool led = false)
    {
        b.getProperties().set ("gold", gold);
        b.getProperties().set ("led", led);
        b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        parent.addAndMakeVisible (b);
        return &b;
    }

    //==========================================================================
    class EngravedLabel : public juce::Component
    {
    public:
        EngravedLabel (juce::String t, float h = 10.5f) : text (std::move (t)), height (h) { setInterceptsMouseClicks (false, false); }
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        void setColour (juce::Colour c) { col = c; repaint(); }
        void paint (juce::Graphics& g) override { engraved (g, text, getLocalBounds().toFloat(), inter (height, true, 0.2f), col); }
    private:
        juce::String text; float height; juce::Colour col { 0xff9aa6b8 };
    };
}

#ifdef MAS_STUDIO_DYN
//==============================================================================
// 1.7d: warm valve light behind a side grille. Three cached layers (recess, light at full heat, metal mesh)
// rendered once per size / scale / theme; a heat change only redraws this small component with a different
// light opacity, so warming up and cooling down cost almost nothing. UI only: the audio thread is not involved.
class LampGrille : public juce::Component
{
public:
    static constexpr int kPad = 22;     // room around the grille for the light spilling onto the plate
    LampGrille() { setInterceptsMouseClicks (false, false); }

    void setHeat (float h)
    {
        h = juce::jlimit (0.0f, 1.0f, h);
        if (std::abs (h - heat) < 0.002f && ! (h == 0.0f && heat != 0.0f)) return;
        heat = h;
        repaint();
    }
    float getHeat() const { return heat; }

    void paint (juce::Graphics& g) override
    {
        const auto& t = themeOf (*this);
        const float W = (float) getWidth(), H = (float) getHeight();
        const juce::Rectangle<float> r ((float) kPad, (float) kPad, W - 2.0f * kPad, H - 2.0f * kPad);
        // warm light reflected on the brushed plate around the grille (subtle, only when lit)
        if (heat > 0.001f)
        {
            juce::ColourGradient spill (juce::Colour (0xffffa04c).withAlpha (0.16f * heat), r.getCentreX(), r.getCentreY(),
                                        juce::Colour (0xffffa04c).withAlpha (0.0f), r.getCentreX() + (float) kPad + r.getWidth() * 0.5f, r.getCentreY(), true);
            g.setGradientFill (spill);
            g.fillEllipse (r.expanded ((float) kPad - 2.0f, (float) kPad * 0.6f));
        }
        Panel::drawCached (g, base, t.name, getWidth(), getHeight(), [&] (juce::Graphics& gg)
        {
            recess (gg, r.expanded (3.0f), 7.0f, rgb (3, 4, 6));
            juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
            gg.reduceClipRegion (clip);
            gg.setColour (rgb (5, 5, 7)); gg.fillRect (r);
        });
        if (heat > 0.001f)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.setOpacity (heat);
            Panel::drawCached (g, light, t.name, getWidth(), getHeight(), [&] (juce::Graphics& gg)
            {
                juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
                gg.reduceClipRegion (clip);
                const juce::Colour amber (0xffffa04c), core (0xffffce83), ember (0xff76280d);
                juce::ColourGradient bg (ember.withAlpha (0.45f), r.getCentreX(), r.getY(), ember.withAlpha (0.4f), r.getCentreX(), r.getBottom(), false);
                bg.addColour (0.5, amber.withAlpha (0.7f));
                gg.setGradientFill (bg); gg.fillRect (r);
                for (int n = 0; n < 3; ++n)   // three hidden valves, slightly uneven
                {
                    const float cy = r.getY() + r.getHeight() * (0.2f + 0.3f * (float) n), cx = r.getCentreX() + (n == 1 ? -3.0f : 2.0f);
                    juce::ColourGradient glow (core.withAlpha (1.0f), cx, cy, amber.withAlpha (0.0f), cx + 30.0f, cy + 44.0f, true);
                    glow.addColour (0.35, juce::Colour (0xffffb766).withAlpha (0.8f));
                    gg.setGradientFill (glow);
                    gg.fillEllipse (cx - 30.0f, cy - 46.0f, 60.0f, 92.0f);
                }
            });
        }
        Panel::drawCached (g, mesh, t.name, getWidth(), getHeight(), [&] (juce::Graphics& gg)
        {
            juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
            gg.reduceClipRegion (clip);
            juce::Path web; web.addRectangle (r);
            const float cr = 3.1f, dx = cr * 1.8f, dy = cr * 1.56f, ir = cr * 0.78f;
            int j = 0;
            for (float y = r.getY() - 2.0f; y < r.getBottom() + 4.0f; y += dy, ++j)
                for (float x = r.getX() - 2.0f + ((j & 1) ? dx * 0.5f : 0.0f); x < r.getRight() + 4.0f; x += dx)
                {
                    juce::Path h;
                    for (int k = 0; k < 6; ++k)
                    {
                        const float a = juce::MathConstants<float>::pi * (1.0f / 6.0f + (float) k / 3.0f);
                        if (k == 0) h.startNewSubPath (x + std::cos (a) * ir, y + std::sin (a) * ir); else h.lineTo (x + std::cos (a) * ir, y + std::sin (a) * ir);
                    }
                    h.closeSubPath(); web.addPath (h);
                }
            web.setUsingNonZeroWinding (false);
            gg.setColour (rgb (21, 23, 27)); gg.fillPath (web);
            // the inner edges of the cells catch a little light: drawn once, faded with the light below
        });
        if (heat > 0.001f)
        {
            // light scattered in front of the mesh (the cells glow a little, like a real lamp behind a grille)
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
            g.reduceClipRegion (clip);
            for (int n = 0; n < 3; ++n)
            {
                const float cy = r.getY() + r.getHeight() * (0.2f + 0.3f * (float) n), cx = r.getCentreX() + (n == 1 ? -3.0f : 2.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc275).withAlpha (0.28f * heat), cx, cy,
                                                         juce::Colour (0xffffa04c).withAlpha (0.0f), cx + 22.0f, cy + 34.0f, true));
                g.fillEllipse (cx - 22.0f, cy - 34.0f, 44.0f, 68.0f);
            }
            g.setColour (juce::Colour (0xffffb35f).withAlpha (0.22f * heat));      // warm rim on the inner metal
            g.drawRoundedRectangle (r.reduced (0.9f), 4.2f, 0.75f);
        }
    }

private:
    float heat = 0.0f;
    Panel::Cache base, light, mesh;
};
#endif

//==============================================================================
// MAIN: DEPTH in the centre, DECAY and MIX on the sides, EARLY / LATE / PRE-DELAY / BLOOM, the module rail
class MainPage : public juce::Component
{
public:
    MainPage (Panel& p, MasterSpaceAudioProcessor& pr) : panel (p), proc (pr)
    {
        using namespace ParamIDs;
        addKnob (decay, "DECAY", "medium", 148, 160);
        addKnob (irLength, "IR LENGTH", "medium", 148, 160);
        addKnob (ParamIDs::mix, "MIX", "medium", 952, 160);
        addKnob (erLevel, "EARLY", "small", 386, 94);
        addKnob (lateLevel, "LATE", "small", 386, 234);
        addKnob (preDelay, "PRE-DELAY", "small", 714, 94);
        addKnob (bloom, "BLOOM", "small", 714, 234);
        addKnob (depth, "DEPTH", "big", 550, 146);

        addAndMakeVisible (bloomNote);
        bloomNote.setColour (th().ind.withAlpha (0.8f));
        bloomNote.setBounds (654, 300, 120, 12);

        makeKey (*this, depthView, false, true);
        depthView.setBounds (488, 296, 124, 26);
        depthView.onClick = [this] { panel.setDrawer (! panel.isDrawerOpen()); };
        depthView.setTooltip ("DEPTH VIEW: open / close the SPATIAL POSITION panel");

        const char* algoNames[] = { "STUDIO ROOM", "CHAMBER", "HALL", "ATMOS TAIL" };
        const int ax[] = { 44, 156, 250, 326 }, aw[] = { 106, 88, 70, 104 };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = algo.add (new juce::TextButton (algoNames[i]));
            makeKey (*this, *b, false, true);
            b->setBounds (ax[i], 340, aw[i], 32);
            b->onClick = [this, i]
            {
                if (i == 3) setPlain (proc.apvts, ParamIDs::tail, 1.0f);
                else { setPlain (proc.apvts, ParamIDs::tail, 0.0f); setPlain (proc.apvts, ParamIDs::algorithm, (float) i); }
                sync();
            };
        }
        algo[3]->setTooltip ("ATMOSPHERIC TAIL (late engine); its own controls are on the ATMOS page in PRO");
        const char* routeNames[] = { "INSERT", "AUX" };
        for (int i = 0; i < 2; ++i)
        {
            auto* b = route.add (new juce::TextButton (routeNames[i]));
            makeKey (*this, *b, true, false);
            b->setBounds (440 + i * 66, 340, 62, 32);
            b->onClick = [this, i] { setPlain (proc.apvts, ParamIDs::routing, (float) i); sync(); };
        }
        makeKey (*this, irKey, false, true);
        irKey.setBounds (578, 340, 52, 32);
        irKey.setTooltip ("Impulse-response mode. Click: switch ALGO / IR (asks for a file if none is loaded); drop a WAV / AIFF / FLAC on the window to load one");
        irKey.onClick = [this]
        {
            const bool ir = plainOf (proc.apvts, ParamIDs::mode) > 0.5f;
            if (ir) setPlain (proc.apvts, ParamIDs::mode, 0.0f);
            else if (proc.getIRDisplayName().isNotEmpty()) setPlain (proc.apvts, ParamIDs::mode, 1.0f);
            else if (panel.classic != nullptr) panel.classic->chooseIR();
            sync();
        };
        const char* modNames[] = { "SMART DUCKER", "RESONANCE GUARD", "HARMONIC COLOR" };
        const char* modIds[] = { ParamIDs::duckOn, ParamIDs::rgOn, ParamIDs::hcOn };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = mods.add (new juce::TextButton (modNames[i]));
            makeKey (*this, *b, false, true);
            b->setClickingTogglesState (true);
            b->setBounds (640 + i * 142, 340, 136, 32);
            modAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, modIds[i], *b));
        }
       #ifdef MAS_STUDIO_DYN
        for (auto* l : { &lampL, &lampR }) addAndMakeVisible (*l);
        lampL.setBounds (36 - LampGrille::kPad, 52 - LampGrille::kPad, 22 + 2 * LampGrille::kPad, 212 + 2 * LampGrille::kPad);
        lampR.setBounds (1042 - LampGrille::kPad, 52 - LampGrille::kPad, 22 + 2 * LampGrille::kPad, 212 + 2 * LampGrille::kPad);
        for (auto* l : { &lampL, &lampR }) l->toBack();
       #endif
        sync();
    }

   #ifdef MAS_STUDIO_DYN
    void setLampHeat (float h) { lampL.setHeat (h); lampR.setHeat (h); }
    float lampHeat() const { return lampL.getHeat(); }
   #endif

    const Theme& th() const { return themeOf (panel); }

    void sync()
    {
        using namespace ParamIDs;
        const bool ir = plainOf (proc.apvts, mode) > 0.5f;
        const bool atmos = plainOf (proc.apvts, tail) > 0.5f;
        const int a = juce::roundToInt (plainOf (proc.apvts, algorithm));
        const int r = juce::roundToInt (plainOf (proc.apvts, routing));
        for (int i = 0; i < 4; ++i)
        {
            algo[i]->setToggleState (i == 3 ? atmos : (! atmos && i == a), juce::dontSendNotification);
            algo[i]->setEnabled (! ir);
        }
        for (int i = 0; i < 2; ++i) route[i]->setToggleState (i == r, juce::dontSendNotification);
        irKey.setToggleState (ir, juce::dontSendNotification);
        depthView.setToggleState (panel.isDrawerOpen(), juce::dontSendNotification);

        knob (decay)->setVisible (! ir);   label (decay)->setVisible (! ir);
        knob (irLength)->setVisible (ir);  label (irLength)->setVisible (ir);
        for (auto* id : { depth, bloom }) knob (id)->setEnabled (! ir);
        auto* bl = knob (bloom);
        const bool dimBloom = ! atmos;
        if ((bool) bl->getProperties()["dim"] != dimBloom) { bl->getProperties().set ("dim", dimBloom); bl->repaint(); }
        bloomNote.setVisible (dimBloom && ! ir);
    }

    void themeChanged()
    {
        bloomNote.setColour (th().ind.withAlpha (0.8f));
        for (auto* c : getChildren()) c->repaint();
    }

private:
    struct Item { const char* id; std::unique_ptr<Knob> knob; std::unique_ptr<EngravedLabel> label;
                  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };

    void addKnob (const char* id, const char* name, const char* kind, int cx, int cy)
    {
        auto it = std::make_unique<Item>();
        it->id = id;
        it->knob = std::make_unique<Knob>();
        auto& k = *it->knob;
        k.getProperties().set ("studioKnob", kind);
        const bool big = juce::String (kind) == "big";
        const int S = big ? 252 : juce::String (kind) == "medium" ? 144 : 112;
        k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 120 : 100, big ? 22 : 18);
        k.setBounds (cx - S / 2, cy - S / 2, S, big ? S + 22 : S);
        addAndMakeVisible (k);
        it->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k);
        if (auto* p = proc.apvts.getParameter (id))
            k.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
        it->label = std::make_unique<EngravedLabel> (name, big ? 12.0f : 10.5f);
        const float arc = .445f * (float) S;
        it->label->setBounds (cx - 90, big ? 2 : juce::roundToInt ((float) cy - arc - 28.0f), 180, 14);
        addAndMakeVisible (*it->label);
        items.push_back (std::move (it));
    }
    Knob* knob (const char* id) { for (auto& i : items) if (juce::String (i->id) == id) return i->knob.get(); return nullptr; }
    EngravedLabel* label (const char* id) { for (auto& i : items) if (juce::String (i->id) == id) return i->label.get(); return nullptr; }

    Panel& panel;
    MasterSpaceAudioProcessor& proc;
    std::vector<std::unique_ptr<Item>> items;
    EngravedLabel bloomNote { "ATMOS TAIL", 8.5f };
    juce::TextButton depthView { "DEPTH VIEW" }, irKey { "IR" };
    juce::OwnedArray<juce::TextButton> algo, route, mods;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> modAtt;
   #ifdef MAS_STUDIO_DYN
    LampGrille lampL, lampR;
   #endif
};

//==============================================================================
// SPATIAL POSITION: a quiet perspective room; SOURCE is dragged in depth and IS the DEPTH parameter
class DepthRoom : public juce::Component
{
public:
    static constexpr int RW = 796, RH = 268;

    DepthRoom (MasterSpaceAudioProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        depthParam = proc.apvts.getParameter (ParamIDs::depth);
    }

    void tick()
    {
        const float d = plainOf (proc.apvts, ParamIDs::depth), w = plainOf (proc.apvts, ParamIDs::width), s = plainOf (proc.apvts, ParamIDs::size);
        bool dirty = d != shownDepth || w != shownWidth || s != shownSize;
        // early / late balance measured by the DSP (MSEngine levels: 1,2 early L/R, 3,4 late L/R, dBFS)
        auto pw = [this] (int i) { const float db = proc.spaceLevel[i].load (std::memory_order_relaxed); return db < -120.0f ? 0.0f : std::pow (10.0f, db / 10.0f); };
        const float e = pw (1) + pw (2), l = pw (3) + pw (4);
        const bool sig = e + l > 1e-9f;
        const float frac = sig ? e / (e + l) : earlyFrac;
        if (sig != hasSignal || std::abs (frac - earlyFrac) > 0.002f) dirty = true;
        earlyFrac += 0.25f * (frac - earlyFrac);
        hasSignal = sig;
        const bool en = plainOf (proc.apvts, ParamIDs::mode) < 0.5f;
        if (en != enabledAlgo) { enabledAlgo = en; dirty = true; }
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = themeOf (*this);
        shownDepth = plainOf (proc.apvts, ParamIDs::depth);
        shownWidth = plainOf (proc.apvts, ParamIDs::width);
        shownSize = plainOf (proc.apvts, ParamIDs::size);
        const auto G = geom();
        Panel::drawCached (g, cache, juce::String (juce::roundToInt (shownSize)) + "|" + t.name, RW, RH, [this, &G] (juce::Graphics& gg) { renderStatic (gg, G); });

        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
        g.reduceClipRegion (clip);
        const float alpha = enabledAlgo ? 1.0f : 0.35f;

        // SOURCE: width rings on the floor, shadow, sphere
        const float zs = srcZ (G);
        const auto s0 = proj (G, 0, 0, zs), s1 = proj (G, 0, 0.85f, zs);
        const float R = juce::jmax (9.0f, s1.s * 0.5f);
        const float wid = shownWidth / 100.0f;
        for (int k = 1; k <= 3; ++k)
        {
            const float rr = (0.55f + 0.55f * wid) * (float) k * 0.75f;
            juce::Path ring;
            for (int i = 0; i <= 64; ++i)
            {
                const float a = (float) i / 64.0f * kTau;
                const auto p = proj (G, std::cos (a) * rr, 0, zs + std::sin (a) * rr * 0.9f);
                if (i == 0) ring.startNewSubPath (p.x, p.y); else ring.lineTo (p.x, p.y);
            }
            glowStroke (g, ring, t.gold, 1.3f, 2.0f, 0.75f / (float) k * alpha);
        }
        {
            juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), s0.x, s0.y, juce::Colours::transparentBlack, s0.x + R * 1.6f, s0.y, true);
            g.setGradientFill (sh);
            g.fillEllipse (s0.x - R * 1.6f, s0.y - R * 0.5f, R * 3.2f, R * 1.0f);
        }
        const auto c = juce::Point<float> (s1.x, s1.y);
        const auto glow = t.goldSphere ? t.gold : t.grid;
        for (int k = 5; k >= 1; --k)
        {
            g.setColour (glow.withAlpha ((hot ? 0.07f : 0.045f) * alpha));
            g.fillEllipse (c.x - R - (float) k * 3.5f, c.y - R - (float) k * 3.5f, 2 * (R + (float) k * 3.5f), 2 * (R + (float) k * 3.5f));
        }
        const auto base = t.goldSphere ? t.gold : rgb (150, 165, 185);
        juce::ColourGradient sg (juce::Colours::white, c.x - R * 0.35f, c.y - R * 0.45f, mix (t.grid, juce::Colours::black, 0.3f), c.x + R * 0.65f, c.y + R * 0.55f, true);
        sg.addColour (0.12, mix (base, juce::Colours::white, 0.5f));
        sg.addColour (0.45, mix (base, juce::Colours::black, 0.35f));
        sg.addColour (0.8, mix (base, juce::Colours::black, 0.7f));
        g.setGradientFill (sg);
        g.setOpacity (alpha);
        g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
        {
            juce::Path rim; rim.addCentredArc (c.x, c.y, R * 0.92f, R * 0.92f, 0, kTau * 0.3f, kTau * 0.45f, true);   // cold bounce light
            g.setColour (t.grid.withAlpha (0.55f * alpha));
            g.strokePath (rim, juce::PathStrokeType (R * 0.12f));
        }
        if (hover || hot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawEllipse (c.x - R - 7, c.y - R - 7, 2 * R + 14, 2 * R + 14, 1.0f);
        }
        srcPix = c; srcR = R;
        g.setFont (inter (9.0f, true, 0.12f));
        g.setColour (rgb (205, 215, 230).withAlpha (0.85f * alpha));
        g.drawText ("SOURCE", juce::Rectangle<float> (c.x - 50, c.y - R - 22, 100, 12), juce::Justification::centred);

        // DEPTH chip (the real parameter value)
        {
            const juce::Rectangle<float> chip (c.x + R + 16, c.y - 10, 84, 20);
            g.setColour (rgb (6, 8, 12).withAlpha (0.78f)); g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (t.gold.withAlpha (0.5f)); g.drawRoundedRectangle (chip, 6.0f, 1.0f);
            g.setColour (t.gold); g.setFont (inter (10.0f, true, 0.06f));
            g.drawText ("DEPTH " + juce::String (juce::roundToInt (shownDepth)) + " %", chip.reduced (8, 0), juce::Justification::centredLeft);
        }
        // EARLY / LATE energy, measured in the engine
        {
            const juce::Rectangle<float> m ((float) RW - 206, 14, 192, 40);
            g.setColour (rgb (6, 8, 12).withAlpha (0.72f)); g.fillRoundedRectangle (m, 8.0f);
            g.setFont (inter (8.5f, true, 0.14f)); g.setColour (rgb (205, 215, 230).withAlpha (0.7f));
            g.drawText ("EARLY", m.getX() + 10, m.getY() + 6, 80, 12, juce::Justification::centredLeft);
            g.drawText ("LATE", m.getRight() - 90, m.getY() + 6, 80, 12, juce::Justification::centredRight);
            const juce::Rectangle<float> bar (m.getX() + 10, m.getY() + 23, 172, 6);
            g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (bar, 3.0f);
            if (hasSignal)
            {
                g.setColour (mix (t.gold, juce::Colours::white, 0.2f).withAlpha (0.9f)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * earlyFrac), 3.0f);
                g.setColour (t.ind.withAlpha (0.9f)); g.fillRoundedRectangle (bar.withTrimmedLeft (bar.getWidth() * earlyFrac), 3.0f);
            }
            else
            {
                g.setColour (rgb (205, 215, 230).withAlpha (0.45f)); g.setFont (inter (8.5f, true, 0.14f));
                g.drawText ("NO SIGNAL", bar.translated (0, 8).withHeight (10), juce::Justification::centred);
            }
        }
        if (! enabledAlgo)
        {
            g.setColour (rgb (205, 215, 230).withAlpha (0.8f)); g.setFont (inter (10.0f, true, 0.16f));
            g.drawText ("IR MODE: DEPTH BELONGS TO THE ALGORITHM", getLocalBounds().toFloat().removeFromBottom (30), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool h = enabledAlgo && e.position.getDistanceFrom (srcPix) < juce::jmax (26.0f, srcR + 10.0f);
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hover) { hover = false; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! enabledAlgo || e.position.getDistanceFrom (srcPix) >= juce::jmax (26.0f, srcR + 10.0f) || depthParam == nullptr) return;
        dragOff = e.position.y - srcPix.y;
        hot = true;
        depthParam->beginChangeGesture();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! hot) return;
        depthParam->setValueNotifyingHost (depthParam->convertTo0to1 (depthFromY (e.position.y - dragOff)));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! hot) return;
        hot = false;
        depthParam->endChangeGesture();
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (enabledAlgo && depthParam != nullptr && e.position.getDistanceFrom (srcPix) < juce::jmax (26.0f, srcR + 10.0f))
        {
            depthParam->beginChangeGesture(); depthParam->setValueNotifyingHost (depthParam->getDefaultValue()); depthParam->endChangeGesture();
        }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! enabledAlgo || depthParam == nullptr) return;
        const float step = (e.mods.isShiftDown() ? 0.3f : 2.0f) * (w.deltaY > 0 ? 1.0f : -1.0f);
        depthParam->beginChangeGesture();
        depthParam->setValueNotifyingHost (depthParam->convertTo0to1 (juce::jlimit (0.0f, 100.0f, plainOf (proc.apvts, ParamIDs::depth) + step)));
        depthParam->endChangeGesture();
    }

private:
    struct Geom { float W, D, camY, camZ, F, CY, zNear; };
    struct P2 { float x, y, s; };
    Geom geom() const { const float s = shownSize / 100.0f; return { 12 + 8 * s, 12 + 12 * s, 4.4f, -3.2f, RH * 1.05f, -RH * 0.02f, 1.7f }; }
    static P2 proj (const Geom& G, float x, float y, float z) { const float d = juce::jmax (0.2f, z - G.camZ), s = G.F / d; return { RW / 2.0f + x * s, G.CY - (y - G.camY) * s, s }; }
    float srcZ (const Geom& G) const { return juce::jmap (shownDepth / 100.0f, G.zNear + 1.1f, G.D - 1.2f); }
    float depthFromY (float py) const
    {
        const auto G = geom();
        const float z = G.camZ + G.camY * G.F / juce::jmax (1.0f, py - G.CY);
        return juce::jlimit (0.0f, 100.0f, (z - (G.zNear + 1.1f)) / ((G.D - 1.2f) - (G.zNear + 1.1f)) * 100.0f);
    }

    void renderStatic (juce::Graphics& g, const Geom& G) const
    {
        const auto& t = themeOf (*this);
        const auto sc = t.screen, gr = t.grid;
        juce::Path clip; clip.addRoundedRectangle (0, 0, (float) RW, (float) RH, 14.0f);
        g.reduceClipRegion (clip);
        juce::ColourGradient bg (mix (sc, juce::Colours::black, 0.55f), 0, 0, mix (sc, gr, 0.12f), 0, (float) RH, false);
        bg.addColour (0.45, sc);
        g.setGradientFill (bg); g.fillRect (0, 0, RW, RH);
        const float hw = G.W / 2;
        std::vector<std::pair<float, float>> prof;
        for (int i = 0; i <= 10; ++i) { const float tt = (float) i / 10.0f; prof.push_back ({ hw - std::sin (tt * 1.57f) * 1.6f + 0.4f, std::sin (tt * 1.2f) * 2.6f }); }
        for (int side : { -1, 1 })
        {
            juce::Path wall;
            for (size_t i = 0; i < prof.size(); ++i) { const auto p = proj (G, (float) side * prof[i].first, prof[i].second, G.D); if (i == 0) wall.startNewSubPath (p.x, p.y); else wall.lineTo (p.x, p.y); }
            for (int i = (int) prof.size() - 1; i >= 0; --i) { const auto p = proj (G, (float) side * prof[(size_t) i].first, prof[(size_t) i].second, 0.6f); wall.lineTo (p.x, p.y); }
            wall.closeSubPath();
            g.setGradientFill (juce::ColourGradient (rgb (30, 34, 42).withAlpha (0.95f), side < 0 ? 0.0f : (float) RW, 0, rgb (10, 12, 16).withAlpha (0.95f), RW / 2.0f, 0, false));
            g.fillPath (wall);
            for (int k = 0; k <= 7; ++k)
            {
                const float z = juce::jmap (std::pow ((float) k / 7.0f, 1.15f), 0.6f, G.D);
                juce::Path rib;
                for (size_t i = 0; i < prof.size(); ++i) { const auto p = proj (G, (float) side * prof[i].first, prof[i].second, z); if (i == 0) rib.startNewSubPath (p.x, p.y); else rib.lineTo (p.x, p.y); }
                g.setColour (rgb (120, 140, 170).withAlpha (0.13f)); g.strokePath (rib, juce::PathStrokeType (1.0f));
            }
            for (int k = 1; k <= 4; ++k)
            {
                const float z = juce::jmap ((float) (k - 1) / 3.0f, 1.4f, G.D - 1.0f);
                const auto p = proj (G, (float) side * (hw - 0.1f), 1.25f, z);
                const float lw = juce::jmax (2.0f, p.s * 0.16f), lh = juce::jmax (4.0f, p.s * 0.42f);
                g.setGradientFill (juce::ColourGradient (t.lamp.withAlpha (0.18f), p.x, p.y, t.lamp.withAlpha (0.0f), p.x + p.s * 1.4f, p.y, true));
                g.fillEllipse (p.x - p.s * 1.4f, p.y - p.s * 1.4f, p.s * 2.8f, p.s * 2.8f);
                for (int q = 3; q >= 1; --q) { g.setColour (t.lamp.withAlpha (0.12f)); g.fillRoundedRectangle (p.x - lw / 2 - (float) q * 2, p.y - lh / 2 - (float) q * 2, lw + (float) q * 4, lh + (float) q * 4, 2.0f); }
                g.setColour (mix (t.lamp, juce::Colours::white, 0.3f).withAlpha (0.95f));
                g.fillRect (p.x - lw / 2, p.y - lh / 2, lw, lh);
            }
        }
        const auto b0 = proj (G, -hw, 0, G.D), b1 = proj (G, hw, 2.4f, G.D);
        g.setColour (rgb (4, 8, 16).withAlpha (0.85f)); g.fillRect (b0.x, b1.y, b1.x - b0.x, b0.y - b1.y);
        juce::Path floor;
        { const auto a = proj (G, -hw, 0, 0.6f), b = proj (G, hw, 0, 0.6f), c = proj (G, hw, 0, G.D), d = proj (G, -hw, 0, G.D);
          floor.startNewSubPath (a.x, a.y); floor.lineTo (b.x, b.y); floor.lineTo (c.x, c.y); floor.lineTo (d.x, d.y); floor.closeSubPath(); }
        g.setGradientFill (juce::ColourGradient (mix (sc, gr, 0.18f).withAlpha (0.9f), 0, b0.y, mix (sc, gr, 0.32f).withAlpha (0.95f), 0, (float) RH, false));
        g.fillPath (floor);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (floor);
            for (int i = -6; i <= 6; ++i)
            {
                if (i == 0) continue;
                const float x = (float) i / 6.0f * hw;
                const auto a = proj (G, x, 0, 0.6f), b = proj (G, x, 0, G.D);
                g.setColour (gr.withAlpha (0.22f)); g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
            }
            for (int k = 0; k <= 12; ++k)
            {
                const float z = juce::jmap (std::pow ((float) k / 12.0f, 1.35f), 0.6f, G.D);
                const auto a = proj (G, -hw, 0, z), b = proj (G, hw, 0, z);
                g.setColour (gr.withAlpha (0.16f + 0.1f * (1.0f - (float) k / 12.0f))); g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
            }
            const auto ax0 = proj (G, 0, 0, G.D), ax1 = proj (G, 0, 0, 0.6f);
            juce::Path axis; axis.startNewSubPath (ax0.x, ax0.y); axis.lineTo (ax1.x, ax1.y);
            glowStroke (g, axis, mix (gr, juce::Colours::white, 0.3f), 1.6f, 4.0f, 0.75f);
        }
        // LISTENER: a small lit bust in the front
        const auto l0 = proj (G, 0, 0, G.zNear), lh = proj (G, 0, 1.05f, G.zNear);
        const float lr = lh.s * 0.2f;
        for (int q = 4; q >= 1; --q) { g.setColour (gr.withAlpha (0.05f)); g.fillEllipse (lh.x - lr * 2.2f - (float) q * 3, l0.y - lr * 3.2f - (float) q * 3, lr * 4.4f + (float) q * 6, lr * 3.4f + (float) q * 6); }
        juce::ColourGradient bust (rgb (238, 242, 247), lh.x - lr * 0.5f, l0.y - lr * 0.9f, rgb (42, 47, 55), lh.x + lr * 1.9f, l0.y, true);
        bust.addColour (0.45, rgb (138, 147, 160));
        g.setGradientFill (bust);
        juce::Path shoulders; shoulders.addPieSegment (lh.x - lr * 1.9f, l0.y + 2 - lr * 1.5f, lr * 3.8f, lr * 3.0f, -kTau * 0.25f, kTau * 0.25f, 0.0f);
        g.fillPath (shoulders);
        juce::ColourGradient hd (juce::Colours::white, lh.x - lr * 0.35f, l0.y - lr * 2.25f, rgb (38, 42, 49), lh.x + lr, l0.y - lr * 1.85f, true);
        hd.addColour (0.35, rgb (154, 163, 175));
        g.setGradientFill (hd); g.fillEllipse (lh.x - lr, l0.y - lr * 2.85f, 2 * lr, 2 * lr);
        g.setFont (inter (9.0f, true, 0.12f)); g.setColour (rgb (205, 215, 230).withAlpha (0.75f));
        g.drawText ("LISTENER", juce::Rectangle<float> (lh.x - 50, (float) RH - 16, 100, 12), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (0.5f, 0.5f, RW - 1.0f, RH - 1.0f, 14.0f, 1.0f);
    }

    MasterSpaceAudioProcessor& proc;
    juce::RangedAudioParameter* depthParam = nullptr;
    Panel::Cache cache;
    float shownDepth = -1, shownWidth = -1, shownSize = -1, earlyFrac = 0.5f, dragOff = 0, srcR = 10;
    bool hasSignal = false, hover = false, hot = false, enabledAlgo = true;
    juce::Point<float> srcPix;
};

namespace
{
    // drawer pieces: the bar with the title and chevron, the body with the room and the two faders, the bottom bezel
    class DrawerBar : public juce::Component
    {
    public:
        DrawerBar (Panel& p, MasterSpaceAudioProcessor& pr) : panel (p), proc (pr) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics& g) override
        {
            const auto& t = themeOf (*this);
            const bool open = panel.isDrawerOpen();
            Panel::drawCached (g, cache, juce::String (t.name) + (open ? "o" : "c"), getWidth(), getHeight(), [&] (juce::Graphics& gg)
            {
                const float W = (float) getWidth(), H = (float) getHeight();
                Panel::drawFrame (gg, W, H, false, false, t);
                plate (gg, { 26, 2, W - 52, H - 4 }, 14.0f, t, 0.05f, false);
                screw (gg, { 40, H / 2 }, 3.6f, 0.5f, t); screw (gg, { W - 40, H / 2 }, 3.6f, 1.2f, t);
                engraved (gg, "SPATIAL POSITION", { W / 2 - 150 + 14, 0, 300, H }, inter (13.0f, true, 0.3f), mix (t.gold, juce::Colours::white, 0.25f));
                gg.setColour (t.gold);
                for (int k = 0; k < 3; ++k) gg.drawEllipse (W / 2 - 110 - 11, H / 2 - 4 + (float) k * 4 - 3.6f, 22, 7.2f, 1.3f);
                juce::Path ch; const float cx = W - 74, cy = H / 2;
                if (open) { ch.startNewSubPath (cx - 8, cy + 4); ch.lineTo (cx, cy - 4); ch.lineTo (cx + 8, cy + 4); }
                else      { ch.startNewSubPath (cx - 8, cy - 4); ch.lineTo (cx, cy + 4); ch.lineTo (cx + 8, cy - 4); }
                gg.strokePath (ch, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            });
            const bool ir = plainOf (proc.apvts, ParamIDs::mode) > 0.5f;
            const juce::String info = ir ? "IR  " + (proc.getIRDisplayName().isNotEmpty() ? proc.getIRDisplayName() : juce::String ("(none)"))
                                         : "DEPTH  " + juce::String (juce::roundToInt (plainOf (proc.apvts, ParamIDs::depth))) + " %";
            engraved (g, info, { 64, 0, 300, (float) getHeight() }, inter (10.5f, true, 0.18f), t.gold, juce::Justification::centredLeft);
        }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) panel.setDrawer (! panel.isDrawerOpen()); }
    private:
        Panel& panel; MasterSpaceAudioProcessor& proc; Panel::Cache cache;
    };

    class DrawerBody : public juce::Component
    {
    public:
        DrawerBody (MasterSpaceAudioProcessor& p, DepthRoom*& roomOut) : proc (p)
        {
            room = std::make_unique<DepthRoom> (p);
            addAndMakeVisible (*room);
            room->setBounds (152, 16, DepthRoom::RW, DepthRoom::RH);
            roomOut = room.get();
            addFader (width, ParamIDs::width, "WIDTH", 90);
            addFader (size, ParamIDs::size, "SIZE", 1010);
        }
        void paint (juce::Graphics& g) override
        {
            const auto& t = themeOf (*this);
            Panel::drawCached (g, cache, t.name, getWidth(), getHeight(), [&] (juce::Graphics& gg)
            {
                const float W = (float) getWidth(), H = (float) getHeight();
                Panel::drawFrame (gg, W, H, false, false, t);
                plate (gg, { 26, 4, W - 52, H - 8 }, 16.0f, t);
                recess (gg, { 146, 10, 808, H - 20 }, 18.0f, rgb (3, 4, 6));
            });
        }
    private:
        struct F { juce::Slider s; std::unique_ptr<EngravedLabel> label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
        void addFader (F& f, const char* id, const char* name, int cx)
        {
            f.s.setSliderStyle (juce::Slider::LinearVertical);
            f.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
            f.s.getProperties().set ("fader", true);
            f.s.setBounds (cx - 35, 44, 70, 236);
            addAndMakeVisible (f.s);
            f.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, f.s);
            if (auto* p = proc.apvts.getParameter (id)) f.s.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
            f.label = std::make_unique<EngravedLabel> (name);
            f.label->setBounds (cx - 60, 20, 120, 14);
            addAndMakeVisible (*f.label);
        }
        MasterSpaceAudioProcessor& proc;
        std::unique_ptr<DepthRoom> room;
        F width, size;
        Panel::Cache cache;
    };

    class Bezel : public juce::Component
    {
    public:
        Bezel() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            const auto& t = themeOf (*this);
            Panel::drawCached (g, cache, t.name, getWidth(), getHeight(), [&] (juce::Graphics& gg) { Panel::drawFrame (gg, (float) getWidth(), (float) getHeight(), false, true, t); });
        }
    private:
        Panel::Cache cache;
    };
}

//==============================================================================
void Panel::drawCached (juce::Graphics& g, Cache& c, const juce::String& key, int w, int h, const std::function<void (juce::Graphics&)>& render)
{
    const float scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int pw = juce::roundToInt ((float) w * scale), ph = juce::roundToInt ((float) h * scale);
    const auto fullKey = key + "|" + juce::String (pw) + "x" + juce::String (ph);
    if (c.key != fullKey || ! c.img.isValid())
    {
        c.img = juce::Image (juce::Image::ARGB, juce::jmax (1, pw), juce::jmax (1, ph), true);
        juce::Graphics gg (c.img);
        gg.addTransform (juce::AffineTransform::scale ((float) pw / (float) w, (float) ph / (float) h));
        render (gg);
        c.key = fullKey;
    }
    g.drawImageTransformed (c.img, juce::AffineTransform::scale ((float) w / (float) pw, (float) h / (float) ph));
}

void Panel::drawFrame (juce::Graphics& g, float w, float h, bool top, bool bottom, const Theme& t)
{
    const float y0 = top ? 4.0f : -60.0f, y1 = bottom ? h - 4.0f : h + 60.0f;
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path outer; outer.addRoundedRectangle (4.0f, y0, w - 8.0f, y1 - y0, 34.0f);
        g.reduceClipRegion (outer);
        juce::ColourGradient band (rgb (43, 47, 54), 0, 0, rgb (37, 41, 47), w, 0, false);
        band.addColour (0.012, rgb (124, 131, 141)); band.addColour (0.02, rgb (48, 52, 59)); band.addColour (0.5, rgb (28, 31, 36));
        band.addColour (0.98, rgb (48, 52, 59)); band.addColour (0.988, rgb (111, 118, 128));
        g.setGradientFill (band); g.fillRect (0.0f, y0, w, y1 - y0);
        for (float sx : { 8.0f, w - 8.0f })
        {
            juce::ColourGradient rim (t.rim.withAlpha (0.0f), sx - 6, 0, t.rim.withAlpha (0.0f), sx + 6, 0, false);
            rim.addColour (0.5, t.rim.withAlpha (0.32f));
            g.setGradientFill (rim); g.fillRect (sx - 6, y0, 12.0f, y1 - y0);
        }
    }
    const float ix = 16.0f, iy = top ? 16.0f : -60.0f, iw = w - 32.0f, ih = (bottom ? h - 16.0f : h + 60.0f) - iy;
    juce::Path inner; inner.addRoundedRectangle (ix, iy, iw, ih, 22.0f);
    brushFill (g, inner, t.tint, 0.6f);
    g.setColour (juce::Colours::black.withAlpha (0.08f)); g.fillPath (inner);
    g.setColour (juce::Colours::black.withAlpha (0.8f)); g.drawRoundedRectangle (ix + 0.5f, iy + 0.5f, iw - 1, ih - 1, 22.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.05f)); g.drawRoundedRectangle (ix + 1.5f, iy + 1.5f, iw - 3, ih - 3, 21.0f, 1.0f);
    if (top)
        for (float cx : { 44.0f, w - 44.0f })
        {
            g.setGradientFill (juce::ColourGradient (t.rim.withAlpha (0.45f), cx, 10, t.rim.withAlpha (0.0f), cx + 70, 10, true));
            g.fillRect (cx - 70, 0.0f, 140.0f, 60.0f);
        }
    if (bottom)
        for (float cx : { 60.0f, w - 60.0f })
        {
            g.setGradientFill (juce::ColourGradient (t.rim.withAlpha (0.3f), cx, h - 8, t.rim.withAlpha (0.0f), cx + 80, h - 8, true));
            g.fillRect (cx - 80, h - 60, 160.0f, 60.0f);
        }
}

//==============================================================================
Panel::Panel (MasterSpaceAudioProcessor& p, LookAndFeel& l) : proc (p), lnf (l)
{
    setLookAndFeel (&lnf);
    wordmark = juce::ImageCache::getFromMemory (BinaryData::wordmark_png, BinaryData::wordmark_pngSize);
    themeIndex = juce::jlimit (0, kNumThemes - 1, (int) proc.apvts.state.getProperty ("studioTheme", 0));
    lnf.setThemeIndex (themeIndex);
    pro = (bool) proc.apvts.state.getProperty ("studioPro", false);
    drawerOpen = (bool) proc.apvts.state.getProperty ("studioDrawer", false);
    drawerT = drawerOpen ? 1.0f : 0.0f;

    classic = std::make_unique<MASPanel> (proc, lnf);
    addChildComponent (*classic);
    classic->setHosted ([this] (int i) { if (i != 0 && i != 12) setPro (true); setPage (i); });

    makeKey (*this, themeKey, true, false);
    themeKey.setBounds (924, 32, 140, 36);
    themeKey.onClick = [this] { showThemeMenu(); };
    makeKey (*this, mixKey, true); makeKey (*this, proKey, true);
    mixKey.setBounds (960, 108, 48, 28); proKey.setBounds (1012, 108, 48, 28);
    mixKey.onClick = [this] { setPro (false); };
    proKey.onClick = [this] { setPro (true); };
    mixKey.setTooltip ("MIX: MAIN and REVERB EQ");
    proKey.setTooltip ("PRO: every page");
    makeKey (*this, navMain); makeKey (*this, navEq);
    navMain.setBounds (754, 108, 84, 28); navEq.setBounds (844, 108, 100, 28);
    navMain.onClick = [this] { setPage (0); };
    navEq.onClick = [this] { setPage (12); };

    const juce::StringArray names { "MAIN", "SPACE", "EARLY", "LATE", "DECAY EQ", "MOD", "STEREO", "ADVANCED", "ATMOS",
                                    "DUCKER", "GUARD", "COLOR", "REVERB EQ", "PRESETS" };
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->getProperties().set ("tab", true);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        b->onClick = [this, i] { setPage (i); };
        addChildComponent (b);
    }

    if (auto* pb = classic->getPresetBar()) { addAndMakeVisible (*pb); pb->setBounds (36, 108, 706, 28); }

    main = std::make_unique<MainPage> (*this, proc);
    addAndMakeVisible (*main);
    bar = std::make_unique<DrawerBar> (*this, proc);
    addAndMakeVisible (*bar);
    bodyClip = std::make_unique<juce::Component>();
    bodyClip->setInterceptsMouseClicks (false, true);
    addAndMakeVisible (*bodyClip);
    body = std::make_unique<DrawerBody> (proc, room);
    bodyClip->addAndMakeVisible (*body);
    bezel = std::make_unique<Bezel>();
    addAndMakeVisible (*bezel);

    if (auto* d = classic->getPresetDialog()) addChildComponent (*d);

    sendLookAndFeelChange();     // children built before they had this parent: give them the studio look (text boxes etc.)
    setSize (kW, logicalHeight());
    page = -1;
    setPage (0);
    startTimerHz (30);
}

Panel::~Panel()
{
    stopTimer();
    anim.reset();
    proc.spaceLevelsWanted.store (false);
    setLookAndFeel (nullptr);
}

int Panel::logicalHeight() const
{
    if (page == 0) return barY() + kBarH + juce::roundToInt ((float) kBodyH * easeInOut (drawerT)) + kBezelH;
    return contentY() + kPageH + kBezelH;
}

void Panel::setPage (int index)
{
    index = juce::jlimit (0, tabs.size() - 1, index);
    if (! pro && index != 0 && index != 12) setPro (true);
    if (index == page) { updateKeys(); return; }
    page = index;
    main->setVisible (page == 0);
    classic->setVisible (page != 0);
    if (page != 0) classic->showHostedPage (page);
    for (auto* c : { bar.get(), bodyClip.get() }) c->setVisible (page == 0);
    updateKeys();
    resized();
    if (onHeightChange) onHeightChange();
}

void Panel::setPro (bool p)
{
    if (p == pro) { updateKeys(); return; }
    pro = p;
    if (! pro && page != 0 && page != 12) { page = -1; setPage (0); }
    saveUiState();
    updateKeys();
    resized();
    if (onHeightChange) onHeightChange();
}

void Panel::updateKeys()
{
    mixKey.setToggleState (! pro, juce::dontSendNotification);
    proKey.setToggleState (pro, juce::dontSendNotification);
    navMain.setVisible (! pro); navEq.setVisible (! pro);
    navMain.setToggleState (page == 0, juce::dontSendNotification);
    navEq.setToggleState (page == 12, juce::dontSendNotification);
    for (int i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setVisible (pro);
        tabs[i]->setToggleState (i == page, juce::dontSendNotification);
    }
    if (main) main->sync();
}

void Panel::setDrawer (bool open, bool animate)
{
    drawerOpen = open;
    saveUiState();
    if (main) main->sync();
    bar->repaint();
    animFrom = drawerT; animTo = open ? 1.0f : 0.0f;
    if (! animate || ! isShowing())
    {
        anim.reset();
        drawerT = animTo;
        layoutDrawer();
        if (onHeightChange) onHeightChange();
        return;
    }
    animStart = juce::Time::getMillisecondCounterHiRes();
    if (anim == nullptr) anim = std::make_unique<juce::TimedCallback> ([this] { animateDrawer(); });
    anim->startTimerHz (120);
}

void Panel::animateDrawer()
{
    const double dur = 220.0 * std::abs (animTo - animFrom);
    const float k = dur <= 0.0 ? 1.0f : (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - animStart) / dur);
    drawerT = animFrom + (animTo - animFrom) * k;
    if (k >= 1.0f) anim->stopTimer();
    layoutDrawer();
    if (onHeightChange) onHeightChange();
}

void Panel::layoutDrawer()
{
    const int by = barY() + kBarH;
    const float e = easeInOut (drawerT);
    const int h = juce::roundToInt ((float) kBodyH * e);
    bodyClip->setBounds (0, by, kW, h);
    body->setBounds (0, -juce::roundToInt ((1.0f - e) * 60.0f), kW, kBodyH);
    if (page == 0) bezel->setBounds (0, by + h, kW, kBezelH);
    const int H = logicalHeight();
    if (getHeight() != H) setSize (kW, H);
}

void Panel::setTheme (int i)
{
    themeIndex = juce::jlimit (0, kNumThemes - 1, i);
    lnf.setThemeIndex (themeIndex);
    saveUiState();
    main->themeChanged();
    repaint();
    std::function<void (juce::Component&)> all = [&] (juce::Component& c) { c.repaint(); for (auto* ch : c.getChildren()) all (*ch); };
    all (*this);
}

void Panel::saveUiState()
{
    proc.apvts.state.setProperty ("studioTheme", themeIndex, nullptr);
    proc.apvts.state.setProperty ("studioPro", pro, nullptr);
    proc.apvts.state.setProperty ("studioDrawer", drawerOpen, nullptr);
}

void Panel::showThemeMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("THEME");
    for (int i = 0; i < kNumThemes; ++i) m.addItem (i + 1, theme (i).name, true, i == themeIndex);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeKey),
                     [safe = juce::Component::SafePointer<Panel> (this)] (int r) { if (safe != nullptr && r > 0) safe->setTheme (r - 1); });
}

void Panel::loadIRFile (const juce::File& f)
{
    classic->loadIRFile (f);
    main->sync();
    bar->repaint();
}

void Panel::timerCallback()
{
    main->sync();
#ifdef MAS_STUDIO_DYN
    // Valve light behind the side grilles follows the real HARMONIC COLOR state: ON, its DRIVE and MIX set the
    // filament heat; the signal it is actually working on (input level, measured by the engine) adds a little
    // "load". Warm-up ~0.3 s, cool-down ~0.8 s. Visual only.
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double dt = lampLastMs > 0.0 ? juce::jlimit (0.0, 0.1, (nowMs - lampLastMs) * 0.001) : 0.03;
        lampLastMs = nowMs;
        const bool hc = plainOf (proc.apvts, ParamIDs::hcOn) > 0.5f && plainOf (proc.apvts, ParamIDs::mode) < 0.5f;
        const float drive = juce::jlimit (0.0f, 1.0f, plainOf (proc.apvts, ParamIDs::hcDrive) / 100.0f);
        const float hmix = juce::jlimit (0.0f, 1.0f, plainOf (proc.apvts, ParamIDs::hcMix) / 100.0f);
        const float inDb = proc.spaceLevel[0].load (std::memory_order_relaxed);
        const float load = juce::jlimit (0.0f, 1.0f, (inDb + 48.0f) / 36.0f);
        lampLoad += (float) (1.0 - std::exp (-dt / 0.35)) * (load - lampLoad);
        const float goal = hc ? juce::jlimit (0.0f, 1.0f, (0.22f + 0.58f * std::sqrt (drive)) * (0.55f + 0.45f * hmix) + 0.16f * lampLoad) : 0.0f;
        const double tau = goal > lampHeat ? 0.28 : 0.72;
        lampHeat += (float) (1.0 - std::exp (-dt / tau)) * (goal - lampHeat);
        if (goal == 0.0f && lampHeat < 0.003f) lampHeat = 0.0f;
        if (page == 0) main->setLampHeat (lampHeat);
    }
#endif
   #ifdef MAS_STUDIO_DYN
    const bool wantLevels = page == 0 && isVisible() && (drawerT > 0.0f || plainOf (proc.apvts, ParamIDs::hcOn) > 0.5f);
   #else
    const bool wantLevels = page == 0 && drawerT > 0.0f && isVisible();
   #endif
    if (proc.spaceLevelsWanted.load() != wantLevels) proc.spaceLevelsWanted.store (wantLevels);
    if (room != nullptr && page == 0 && drawerT > 0.0f) room->tick();
    static int slow = 0;
    if (++slow % 6 == 0) bar->repaint();
}

void Panel::resized()
{
    const int cy = contentY();
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setBounds (juce::roundToInt (30.0f + (float) i * 74.3f), 148, 74, 36);
    main->setBounds (0, cy, kW, kContentH);
    classic->setBounds (36, cy + 8, kW - 72, kPageH - 16);
    bar->setBounds (0, barY(), kW, kBarH);
    if (page != 0) bezel->setBounds (0, cy + kPageH, kW, kBezelH);
    layoutDrawer();
    if (auto* d = classic->getPresetDialog()) { d->setBounds (getLocalBounds()); if (d->isVisible()) d->toFront (true); }
}

void Panel::paint (juce::Graphics& g)
{
    const int h = page == 0 ? barY() : contentY() + kPageH;
    const auto& t = theme (themeIndex);
    const juce::String cacheKey = juce::String (t.name) + (pro ? "P" : "M") + (page == 0 ? "0" : "x");
    drawCached (g, topCache, cacheKey, kW, h, [this, h] (juce::Graphics& gg) { renderTop (gg, h); });
}

void Panel::renderTop (juce::Graphics& g, int h)
{
    const auto& t = theme (themeIndex);
    const float W = (float) kW;
    drawFrame (g, W, (float) h, true, false, t);

    // header: raised title plate between two light slits, brand on the left
    juce::Path tp;
    tp.startNewSubPath (300, 20); tp.lineTo (800, 20); tp.lineTo (772, 88); tp.lineTo (328, 88); tp.closeSubPath();
    juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 10, { 0, 4 }).drawForPath (g, tp);
    brushFill (g, tp, mix (t.tint, juce::Colours::white, 0.06f), 0.75f);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (tp);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), 0, 16, juce::Colours::black.withAlpha (0.25f), 0, 96, false));
        g.fillPath (tp);
    }
    g.setColour (juce::Colours::white.withAlpha (0.14f)); g.strokePath (tp, juce::PathStrokeType (1.0f));
    slit (g, { 214, 52, 70, 5 }, t);
    slit (g, { 816, 52, 70, 5 }, t);
    {
        const auto f = cinzel (38.0f, 0.24f);
        const juce::Rectangle<float> tr (300, 30, 508, 46);
        g.setFont (f);
        g.setColour (juce::Colours::black.withAlpha (0.85f)); g.drawText ("MASTER SPACE", tr.translated (0, 2), juce::Justification::centred);
        g.setColour (t.gold.withAlpha (0.12f));
        for (auto o : { juce::Point<float> (-1.5f, 0), { 1.5f, 0 }, { 0, -1.5f }, { 0, 1.5f } }) g.drawText ("MASTER SPACE", tr.translated (o.x, o.y), juce::Justification::centred);
        juce::ColourGradient tg (mix (t.gold, juce::Colours::white, 0.55f), 0, 36, mix (t.gold, juce::Colours::black, 0.35f), 0, 72, false);
        tg.addColour (0.5, t.gold);
        g.setGradientFill (tg);
        g.drawText ("MASTER SPACE", tr, juce::Justification::centred);
    }
    {
        juce::Path wave;
        const float wx = 40, wy = 46;
        const float amps[] = { 0, 2, 5, 9, 3, 12, 6, 14, 4, 9, 2, 0 };
        wave.startNewSubPath (wx, wy);
        for (int i = 0; i < 12; ++i) wave.lineTo (wx + 12 + (float) i * 7, wy - amps[i] * ((i % 2) ? 1.0f : -1.0f));
        wave.lineTo (wx + 110, wy);
        glowStroke (g, wave, t.ind, 1.5f, 2.5f, 0.95f);
        if (wordmark.isValid())
        {
            const float ww = 150.0f, wh = ww * (float) wordmark.getHeight() / (float) wordmark.getWidth();
            g.setColour (rgb (200, 208, 220).withAlpha (0.8f));
            g.drawImage (wordmark, { 40, 64, ww, wh }, juce::RectanglePlacement::centred, true);
        }
    }
    // command strip (+ PRO tabs)
    recess (g, { 26, 102, W - 52, 40 }, 12.0f, mix (t.tint, juce::Colours::black, 0.55f));
    if (pro) recess (g, { 26, 148, W - 52, 34 }, 10.0f, mix (t.tint, juce::Colours::black, 0.5f));
    const float y = (float) contentY();
    if (page == 0)
    {
        plate (g, { 26, y, 236, 316 }, 16.0f, t);
        plate (g, { 270, y, 560, 316 }, 18.0f, t);
        plate (g, { 838, y, 236, 316 }, 16.0f, t);
#ifdef MAS_STUDIO_DYN
        // the side grilles are LampGrille components (lit by HARMONIC COLOR)
#else
        grille (g, { 36, y + 52, 22, 212 }, t);
        grille (g, { 1042, y + 52, 22, 212 }, t);
#endif
        goldLine (g, 476, y + 30, y + 290, t);
        goldLine (g, 624, y + 30, y + 290, t);
        plate (g, { 26, y + 324, W - 52, 56 }, 14.0f, t, 0.035f, false);
        recess (g, { 36, y + 332, W - 72, 42 }, 10.0f, mix (t.tint, juce::Colours::black, 0.5f));
        // the light line from DEPTH down to the drawer
        g.setColour (mix (t.ind, juce::Colours::white, 0.3f).withAlpha (0.9f));
        g.fillRect (549.0f, y + 380.0f, 2.0f, 8.0f);
    }
    else
        plate (g, { 26, y, W - 52, (float) kPageH }, 18.0f, t);
}

//==============================================================================
Editor::Editor (MasterSpaceAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    panel = std::make_unique<Panel> (proc, lnf);
    addAndMakeVisible (*panel);
    panel->onHeightChange = [this] { updateSize(); };
    setResizable (true, true);
    const int w = juce::jlimit (kMinWidth, kMaxWidth, (int) proc.apvts.state.getProperty ("uiWidth", Panel::kW));
    setSize (w, juce::roundToInt ((float) w * (float) panel->logicalHeight() / (float) Panel::kW));
    ready = true;
    updateSize();
}

Editor::~Editor()
{
    panel.reset();
    setLookAndFeel (nullptr);
}

void Editor::updateSize()
{
    const int H = panel->logicalHeight();
    const double ratio = (double) Panel::kW / (double) H;
    const int w = getWidth() > 0 ? getWidth() : Panel::kW;      // the width stays; only the height follows the drawer
    const int h = juce::roundToInt (w / ratio);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
    setResizeLimits (kMinWidth, juce::roundToInt (kMinWidth / ratio), kMaxWidth, juce::roundToInt (kMaxWidth / ratio));
    if (getHeight() != h) setSize (w, h);
    else resized();
}

void Editor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void Editor::resized()
{
    if (panel == nullptr) return;
    const float s = (float) getWidth() / (float) Panel::kW;
    panel->setBounds (0, 0, Panel::kW, panel->logicalHeight());
    panel->setTransform (juce::AffineTransform::scale (s));
    if (ready) proc.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}

bool Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac")) return true;
    return false;
}

void Editor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac")) { panel->loadIRFile (juce::File (f)); break; }
}
} // namespace studio
