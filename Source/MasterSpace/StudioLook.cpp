#include "StudioLook.h"
#include "StudioFonts.h"

namespace studio
{
namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi, kTau = juce::MathConstants<float>::twoPi;

    juce::Typeface::Ptr typeface (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    juce::Typeface::Ptr interRegular() { static auto t = typeface (StudioFonts::InterRegular_ttf, StudioFonts::InterRegular_ttfSize); return t; }
    juce::Typeface::Ptr interSemi()    { static auto t = typeface (StudioFonts::InterSemiBold_ttf, StudioFonts::InterSemiBold_ttfSize); return t; }
    juce::Typeface::Ptr cinzelSemi()   { static auto t = typeface (StudioFonts::CinzelSemiBold_ttf, StudioFonts::CinzelSemiBold_ttfSize); return t; }

    juce::Colour rgb (int r, int g, int b) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b); }

    // the brushed-metal grain: a grey noise tile with long horizontal streaks, stored as light / dark alpha
    const juce::Image& brushTile()
    {
        static juce::Image tile = []
        {
            constexpr int w = 512, h = 128;
            juce::Image img (juce::Image::ARGB, w, h, true);
            juce::Random rnd (7);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                const float row = rnd.nextFloat() * 2.0f - 1.0f;
                float streak = 0.0f; int left = 0;
                for (int x = 0; x < w; ++x)
                {
                    if (left <= 0) { left = 20 + rnd.nextInt (160); streak = (rnd.nextFloat() - 0.5f) * 0.9f; }
                    --left;
                    const float v = row * 15.0f + streak * 16.0f + (rnd.nextFloat() - 0.5f) * 10.0f;   // about -30..30
                    const float a = juce::jlimit (0.0f, 1.0f, std::abs (v) / 34.0f);
                    bd.setPixelColour (x, y, (v > 0 ? juce::Colours::white : juce::Colours::black).withAlpha (a));
                }
            }
            return img;
        }();
        return tile;
    }

    // per-pixel shading of a ring (r0..r1, logical units) into an image rendered at `scale`; antialiased edges
    template <typename ColourAt>
    void shadeRing (juce::Image& img, float scale, juce::Point<float> c, float r0, float r1, ColourAt&& colourAt)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        const float cx = c.x * scale, cy = c.y * scale, R0 = r0 * scale, R1 = r1 * scale;
        const int x0 = juce::jmax (0, (int) std::floor (cx - R1 - 1)), x1 = juce::jmin (bd.width - 1, (int) std::ceil (cx + R1 + 1));
        const int y0 = juce::jmax (0, (int) std::floor (cy - R1 - 1)), y1 = juce::jmin (bd.height - 1, (int) std::ceil (cy + R1 + 1));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float dx = (float) x + 0.5f - cx, dy = (float) y + 0.5f - cy, d = std::sqrt (dx * dx + dy * dy);
                const float cov = juce::jlimit (0.0f, 1.0f, d - R0 + 0.5f) * juce::jlimit (0.0f, 1.0f, R1 - d + 0.5f);
                if (cov <= 0.0f) continue;
                const float ang = std::atan2 (dy, dx);                 // canvas convention: from +x, clockwise (y down)
                auto col = colourAt (d / scale, ang, d > 1e-4f ? dx / d : 0.0f, d > 1e-4f ? dy / d : 0.0f);
                col = col.withMultipliedAlpha (cov);
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))->blend (col.getPixelARGB());
            }
    }

    struct Stop { float p, v; };
    float conic (float ang, float phase, std::initializer_list<Stop> stops)
    {
        float t = (ang - phase) / kTau;
        t -= std::floor (t);
        const Stop* prev = stops.begin();
        for (auto* s = stops.begin() + 1; s != stops.end(); ++s)
        {
            if (t <= s->p) { const float k = (t - prev->p) / juce::jmax (1e-6f, s->p - prev->p); return prev->v + (s->v - prev->v) * k; }
            prev = s;
        }
        return prev->v;
    }

    constexpr float kLightX = -0.7071f, kLightY = -0.7071f;   // light from the top left

    // brushed, lathe-turned dark cap with a bronze chamfer ring around it
    void cap (juce::Image& img, float scale, juce::Point<float> c, float r, const Theme& t, int seed)
    {
        const juce::Colour steel = rgb (13, 15, 18), hi = rgb (150, 157, 168);
        std::vector<float> rings ((size_t) (r / 0.9f) + 4);
        juce::Random rnd (seed);
        for (auto& v : rings) v = (rnd.nextFloat() < 0.5f ? 1.0f : -1.0f) * (0.015f + rnd.nextFloat() * 0.05f);
        const auto goldDark = t.gold.interpolatedWith (juce::Colours::black, 0.72f), goldLight = t.gold.interpolatedWith (rgb (255, 240, 210), 0.35f);
        const float chamfer = juce::jmax (1.6f, r * 0.07f);

        shadeRing (img, scale, c, 0.0f, r, [&] (float d, float ang, float nx, float ny)
        {
            const float v = conic (ang, -0.25f, { { 0, .18f }, { .08f, .95f }, { .16f, .35f }, { .3f, .08f }, { .42f, .22f }, { .5f, .12f },
                                                  { .58f, .88f }, { .66f, .3f }, { .8f, .06f }, { .9f, .2f }, { 1, .18f } });
            auto col = steel.interpolatedWith (hi, v);
            const float ring = rings[juce::jlimit<size_t> (0, rings.size() - 1, (size_t) (d / 0.9f))];
            col = ring > 0 ? col.interpolatedWith (juce::Colours::white, ring) : col.interpolatedWith (juce::Colours::black, -ring);
            const float vig = juce::jlimit (0.0f, 1.0f, (d / r - 0.55f) / 0.45f);
            col = col.interpolatedWith (juce::Colours::black, 0.35f * vig);
            const float sx = (nx * d) + 0.45f * r, sy = (ny * d) + 0.5f * r, sd = std::sqrt (sx * sx + sy * sy) / (0.9f * r);
            if (sd < 1.0f) col = col.interpolatedWith (juce::Colours::white, 0.13f * (1.0f - sd));
            if (d > r - chamfer)
            {
                const float k = 1.0f - std::abs ((d - (r - chamfer * 0.5f)) / (chamfer * 0.5f));
                const float l = nx * kLightX + ny * kLightY;
                col = l > 0 ? col.interpolatedWith (juce::Colours::white, 0.75f * l * k) : col.interpolatedWith (juce::Colours::black, 0.85f * -l * k);
            }
            if (d < r * 0.07f) col = col.interpolatedWith (juce::Colours::black, 0.55f * (1.0f - d / (r * 0.07f)));
            return col;
        });
        shadeRing (img, scale, c, r, r + 0.5f, [] (float, float, float, float) { return juce::Colours::black.withAlpha (0.85f); });
        shadeRing (img, scale, c, r + 0.45f, r + juce::jmax (2.0f, r * 0.075f), [&] (float, float ang, float, float)
        {
            const float v = conic (ang, -0.6f, { { 0, .15f }, { .1f, 1 }, { .22f, .35f }, { .4f, .1f }, { .55f, .25f }, { .62f, .9f },
                                                 { .72f, .3f }, { .9f, .08f }, { 1, .15f } });
            return goldDark.interpolatedWith (goldLight, v);
        });
    }

    void knurl (juce::Graphics& g, juce::Point<float> c, float r0, float r1, int teeth, juce::Colour metal, juce::Colour dark)
    {
        juce::Path ring; ring.addEllipse (c.x - r1, c.y - r1, 2 * r1, 2 * r1); ring.addEllipse (c.x - r0, c.y - r0, 2 * r0, 2 * r0);
        ring.setUsingNonZeroWinding (false);
        g.setColour (dark); g.fillPath (ring);
        const float hw = kPi / (float) teeth * 0.62f;
        for (int i = 0; i < teeth; ++i)
        {
            const float a = (float) i / (float) teeth * kTau;
            const float l = 0.5f + 0.5f * std::cos (a - std::atan2 (kLightY, kLightX)), l2 = std::pow (l, 1.6f);
            const auto col = metal.interpolatedWith (juce::Colours::black, 0.62f).interpolatedWith (metal.interpolatedWith (juce::Colours::white, 0.25f), l2);
            juce::Path p;
            p.startNewSubPath (c.x + std::cos (a - hw) * r0, c.y + std::sin (a - hw) * r0);
            p.lineTo (c.x + std::cos (a - hw * 0.7f) * r1, c.y + std::sin (a - hw * 0.7f) * r1);
            p.lineTo (c.x + std::cos (a + hw * 0.7f) * r1, c.y + std::sin (a + hw * 0.7f) * r1);
            p.lineTo (c.x + std::cos (a + hw) * r0, c.y + std::sin (a + hw) * r0);
            p.closeSubPath();
            g.setColour (col); g.fillPath (p);
        }
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.12f), c.x - r1, c.y - r1, juce::Colours::black.withAlpha (0.45f), c.x + r1, c.y + r1, false);
        sh.addColour (0.5, juce::Colours::transparentBlack);
        g.setGradientFill (sh); g.fillPath (ring);
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawEllipse (c.x - r1 + 0.5f, c.y - r1 + 0.5f, 2 * r1 - 1, 2 * r1 - 1, 1.0f);
    }

    juce::Point<float> polar (juce::Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }   // JUCE angle: 0 = up, clockwise
}

//==============================================================================
const Theme& theme (int index)
{
    static const Theme themes[kNumThemes] = {
        { "MIDNIGHT BLUE", rgb (21, 25, 33), juce::Colour (0xff4fb4ff), juce::Colour (0xffd6aa62), rgb (60, 120, 255), rgb (5, 18, 42),  rgb (45, 140, 255), rgb (255, 196, 120), true },
        { "GRAPHITE GOLD", rgb (27, 26, 25), juce::Colour (0xffe8b35e), juce::Colour (0xffd6aa62), rgb (215, 160, 95), rgb (18, 15, 11), rgb (220, 165, 90), rgb (255, 196, 120), true },
        { "DEEP RED",      rgb (25, 22, 23), juce::Colour (0xffff5145), juce::Colour (0xffd6aa62), rgb (225, 60, 50),  rgb (26, 7, 9),   rgb (255, 85, 65),  rgb (255, 180, 120), true },
        { "PURPLE VELVET", rgb (24, 21, 32), juce::Colour (0xffa98bff), juce::Colour (0xffe0b27a), rgb (140, 95, 255), rgb (16, 9, 38),  rgb (155, 115, 255), rgb (255, 190, 140), false },
        { "PURE BLACK",    rgb (14, 14, 15), juce::Colour (0xffe6eefb), juce::Colour (0xffc9cfd8), rgb (150, 160, 180), rgb (6, 7, 9),  rgb (190, 200, 220), rgb (255, 214, 160), false } };
    return themes[juce::jlimit (0, kNumThemes - 1, index)];
}

juce::Font inter (float height, bool semiBold, float kerning)
{
    return juce::Font (juce::FontOptions (semiBold ? interSemi() : interRegular()).withHeight (height)).withExtraKerningFactor (kerning);
}

juce::Font cinzel (float height, float kerning)
{
    return juce::Font (juce::FontOptions (cinzelSemi()).withHeight (height)).withExtraKerningFactor (kerning);
}

//==============================================================================
FontProvider::FontProvider()  { juce::LookAndFeel::setDefaultLookAndFeel (this); }
FontProvider::~FontProvider() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

juce::Typeface::Ptr FontProvider::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? interSemi() : interRegular();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
void brushFill (juce::Graphics& g, const juce::Path& area, juce::Colour base, float amount)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area);
    g.setColour (base);
    g.fillPath (area);
    g.setTiledImageFill (brushTile(), 0, 0, juce::jlimit (0.0f, 1.0f, amount * 0.3f));
    g.fillPath (area);
}

void screw (juce::Graphics& g, juce::Point<float> c, float r, float rot, const Theme& t)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - r - 1, c.y - r - 0.2f, 2 * r + 2, 2 * r + 2);
    juce::ColourGradient gr (rgb (241, 215, 166), c.x - r * 0.35f, c.y - r * 0.4f, rgb (74, 58, 34), c.x + r, c.y + r, true);
    gr.addColour (0.45, t.gold);
    g.setGradientFill (gr);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (rgb (30, 22, 10).withAlpha (0.85f));
    const auto tr = juce::AffineTransform::rotation (rot, c.x, c.y);
    juce::Path cross;
    cross.addRectangle (c.x - r * 0.75f, c.y - 0.7f, r * 1.5f, 1.4f);
    cross.addRectangle (c.x - 0.7f, c.y - r * 0.75f, 1.4f, r * 1.5f);
    g.fillPath (cross, tr);
}

void plate (juce::Graphics& g, juce::Rectangle<float> r, float rad, const Theme& t, float light, bool screws)
{
    juce::Path p; p.addRoundedRectangle (r, rad);
    juce::DropShadow (juce::Colours::black.withAlpha (0.75f), 14, { 0, 5 }).drawForPath (g, p);
    brushFill (g, p, mix (t.tint, juce::Colours::white, light), 0.7f);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (p);
        juce::ColourGradient v (juce::Colours::white.withAlpha (0.05f), 0, r.getY(), juce::Colours::black.withAlpha (0.22f), 0, r.getBottom(), false);
        v.addColour (0.5, juce::Colours::transparentWhite);
        g.setGradientFill (v); g.fillRect (r);
        juce::ColourGradient h (juce::Colours::white.withAlpha (0.025f), r.getX(), 0, juce::Colours::black.withAlpha (0.12f), r.getRight(), 0, false);
        h.addColour (0.5, juce::Colours::transparentBlack);
        g.setGradientFill (h); g.fillRect (r);
    }
    juce::ColourGradient bv (juce::Colours::white.withAlpha (0.22f), 0, r.getY(), juce::Colours::black.withAlpha (0.6f), 0, r.getBottom(), false);
    bv.addColour (0.15, juce::Colours::white.withAlpha (0.07f));
    g.setGradientFill (bv);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (2.5f), juce::jmax (1.0f, rad - 2.0f), 1.0f);
    if (screws)
    {
        const float s = 11.0f;
        screw (g, { r.getX() + s, r.getY() + s }, 3.6f, 0.4f, t);
        screw (g, { r.getRight() - s, r.getY() + s }, 3.6f, 1.1f, t);
        screw (g, { r.getX() + s, r.getBottom() - s }, 3.6f, 0.9f, t);
        screw (g, { r.getRight() - s, r.getBottom() - s }, 3.6f, 0.2f, t);
    }
}

void recess (juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour fill)
{
    juce::Path p; p.addRoundedRectangle (r, rad);
    g.setColour (fill); g.fillPath (p);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (p);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.85f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 12.0f, false));
        g.fillRect (r.withHeight (12.0f));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0, r.getY(), juce::Colours::white.withAlpha (0.12f), 0, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
}

void slit (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t)
{
    const float rr = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    recess (g, r.expanded (3.0f), rr + 3.0f, rgb (4, 6, 9));
    for (int k = 4; k >= 1; --k)
    {
        g.setColour (t.ind.withAlpha (0.07f));
        g.fillRoundedRectangle (r.expanded ((float) k * 2.0f), rr + (float) k * 2.0f);
    }
    const bool horiz = r.getWidth() > r.getHeight();
    juce::ColourGradient gr (t.ind.withAlpha (0.25f), horiz ? r.getX() : 0.0f, horiz ? 0.0f : r.getY(),
                             t.ind.withAlpha (0.25f), horiz ? r.getRight() : 0.0f, horiz ? 0.0f : r.getBottom(), false);
    gr.addColour (0.5, mix (t.ind, juce::Colours::white, 0.35f));
    g.setGradientFill (gr);
    g.fillRoundedRectangle (r, rr);
}

void grille (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t)
{
    recess (g, r.expanded (3.0f), 7.0f, rgb (3, 4, 6));
    juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (clip);
    juce::ColourGradient gl (t.lamp.withAlpha (0.05f), 0, r.getY(), t.lamp.withAlpha (0.05f), 0, r.getBottom(), false);
    gl.addColour (0.5, t.lamp.withAlpha (0.55f));
    g.setGradientFill (gl); g.fillRect (r);
    // the metal web: the plate minus hexagonal cells
    juce::Path web; web.addRectangle (r);
    const float cr = 3.1f, dx = cr * 1.8f, dy = cr * 1.56f, ir = cr * 0.78f;
    int j = 0;
    for (float y = r.getY() - 2.0f; y < r.getBottom() + 4.0f; y += dy, ++j)
        for (float x = r.getX() - 2.0f + ((j & 1) ? dx * 0.5f : 0.0f); x < r.getRight() + 4.0f; x += dx)
        {
            juce::Path h;
            for (int k = 0; k < 6; ++k)
            {
                const float a = kPi / 6.0f + (float) k * kPi / 3.0f;
                if (k == 0) h.startNewSubPath (x + std::cos (a) * ir, y + std::sin (a) * ir); else h.lineTo (x + std::cos (a) * ir, y + std::sin (a) * ir);
            }
            h.closeSubPath();
            web.addPath (h);
        }
    web.setUsingNonZeroWinding (false);
    g.setColour (rgb (21, 23, 27));
    g.fillPath (web);
}


#ifdef MAS_STUDIO_DYN
// A warm filament glow BEHIND the existing hexagonal metal mesh. UI-only.
// Every update is message-thread cached by Panel; no work in the audio callback.
void glowingGrille (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, float heat)
{
    heat = juce::jlimit (0.0f, 1.0f, heat);
    recess (g, r.expanded (3.0f), 7.0f, rgb (3, 4, 6));
    juce::Path clip;
    clip.addRoundedRectangle (r, 5.0f);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (clip);
    g.setColour (rgb (5, 5, 7));
    g.fillRect (r);

    if (heat > 0.001f)
    {
        const juce::Colour amber (0xffffa04c), core (0xffffce83), red (0xff76280d);
        juce::ColourGradient background (red.withAlpha (0.20f * heat), r.getCentreX(), r.getY(),
                                          red.withAlpha (0.16f * heat), r.getCentreX(), r.getBottom(), false);
        background.addColour (0.5, amber.withAlpha (0.34f * heat));
        g.setGradientFill (background);
        g.fillRect (r);

        // Three hidden heat sources: soft, uneven radiation diffusing through the apertures.
        for (int n = 0; n < 3; ++n)
        {
            const float cy = r.getY() + r.getHeight() * (0.21f + 0.29f * n);
            const float asym = n == 1 ? -3.0f : 2.0f;
            const float cx = r.getCentreX() + asym;
            juce::ColourGradient glow (core.withAlpha (0.55f * heat), cx, cy,
                                        red.withAlpha (0.0f), cx + 25.0f, cy + 37.0f, true);
            glow.addColour (0.40, amber.withAlpha (0.33f * heat));
            g.setGradientFill (glow);
            g.fillEllipse (cx - 25.0f, cy - 39.0f, 50.0f, 78.0f);
        }
    }

    // The mesh is painted LAST: warm light remains recessed inside the chassis.
    juce::Path web;
    web.addRectangle (r);
    const float cr = 3.1f, dx = cr * 1.8f, dy = cr * 1.56f, ir = cr * 0.78f;
    int j = 0;
    for (float y = r.getY() - 2.0f; y < r.getBottom() + 4.0f; y += dy, ++j)
        for (float x = r.getX() - 2.0f + ((j & 1) ? dx * 0.5f : 0.0f); x < r.getRight() + 4.0f; x += dx)
        {
            juce::Path h;
            for (int k = 0; k < 6; ++k)
            {
                const float a = kPi / 6.0f + (float) k * kPi / 3.0f;
                if (k == 0) h.startNewSubPath (x + std::cos (a) * ir, y + std::sin (a) * ir);
                else h.lineTo (x + std::cos (a) * ir, y + std::sin (a) * ir);
            }
            h.closeSubPath();
            web.addPath (h);
        }
    web.setUsingNonZeroWinding (false);
    g.setColour (rgb (21, 23, 27));
    g.fillPath (web);

    if (heat > 0.001f)
    {
        g.setColour (juce::Colour (0xffffb35f).withAlpha (0.12f * heat));
        g.drawRoundedRectangle (r.reduced (0.9f), 4.2f, 0.75f);
    }
    juce::ignoreUnused (t);
}
#endif

void goldLine (juce::Graphics& g, float x, float y0, float y1, const Theme& t)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (x + 1.0f, y0, 1.0f, y1 - y0);
    juce::ColourGradient gr (t.gold.withAlpha (0.0f), 0, y0, t.gold.withAlpha (0.0f), 0, y1, false);
    gr.addColour (0.5, t.gold.withAlpha (0.85f));
    g.setGradientFill (gr);
    g.fillRect (x, y0, 1.2f, y1 - y0);
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow, float alpha)
{
    const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
    for (int k = 3; k >= 1; --k)
    {
        g.setColour (c.withAlpha (0.10f * alpha));
        g.strokePath (p, juce::PathStrokeType (width + glow * (float) k * 0.7f, juce::PathStrokeType::curved, cap));
    }
    g.setColour (c.withAlpha (alpha));
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, cap));
}

void engraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Font f, juce::Colour c, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawText (text, r.translated (0.0f, 1.0f), j, false);
    g.setColour (c);
    g.drawText (text, r, j, false);
}

void key (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, const KeyState& s)
{
    const float rad = juce::jmin (7.0f, r.getHeight() * 0.28f);
    const float alpha = s.enabled ? 1.0f : 0.4f;
    const bool pressed = s.down || s.on;
    juce::Graphics::ScopedSaveState ss (g);
    g.beginTransparencyLayer (alpha);
    // lip and drop shadow (the key stands on the plate; pressed keys sit lower)
    {
        juce::Path body; body.addRoundedRectangle (r, rad);
        if (! pressed) juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 7, { 0, 4 }).drawForPath (g, body);
        g.setColour (rgb (9, 10, 13));
        g.fillRoundedRectangle (r.translated (0.0f, pressed ? 0.5f : 2.0f), rad);
    }
    const auto b = r.translated (0.0f, s.down ? 1.5f : (s.on ? 1.0f : 0.0f));
    juce::Path p; p.addRoundedRectangle (b, rad);
    juce::ColourGradient gr (s.on ? rgb (48, 53, 61) : rgb (61, 67, 76), 0, b.getY(), s.on ? rgb (26, 29, 35) : rgb (29, 32, 38), 0, b.getBottom(), false);
    gr.addColour (0.48, s.on ? rgb (34, 38, 45) : rgb (42, 46, 53));
    g.setGradientFill (gr); g.fillPath (p);
    {
        juce::Graphics::ScopedSaveState s2 (g);
        g.reduceClipRegion (p);
        g.setTiledImageFill (brushTile(), 0, 0, 0.32f);
        g.fillPath (p);
        if (pressed)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, b.getY(), juce::Colours::transparentBlack, 0, b.getY() + 5.0f, false));
            g.fillRect (b.withHeight (5.0f));
        }
        if (s.on)
        {
            const auto glow = s.gold ? t.gold : t.ind;
            for (int k = 1; k <= 4; ++k)
            {
                g.setColour (glow.withAlpha (0.05f));
                g.drawRoundedRectangle (b.reduced ((float) k * 1.5f), juce::jmax (1.0f, rad - (float) k), 3.0f);
            }
            g.setColour (glow.withAlpha (0.3f));
            g.drawRoundedRectangle (b.reduced (0.5f), rad, 1.0f);
        }
        if (s.hover && ! s.down) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillPath (p); }
    }
    if (! pressed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawLine (b.getX() + rad * 0.6f, b.getY() + 1.0f, b.getRight() - rad * 0.6f, b.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (b.getX() + rad * 0.6f, b.getBottom() - 0.5f, b.getRight() - rad * 0.6f, b.getBottom() - 0.5f, 1.0f);
    }
    g.endTransparencyLayer();
}

void keyText (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, const Theme& t, const KeyState& s, float fontH)
{
    const auto b = r.translated (0.0f, s.down ? 1.5f : (s.on ? 1.0f : 0.0f));
    const auto f = inter (fontH, true, 0.14f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, text);
    const float led = s.led ? 13.0f : 0.0f;
    const float total = tw + led, x0 = b.getCentreX() - total * 0.5f;
    const float alpha = s.enabled ? 1.0f : 0.4f;
    if (s.led)
    {
        const juce::Rectangle<float> l (x0, b.getCentreY() - 3.0f, 6.0f, 6.0f);
        if (s.on)
        {
            g.setColour (t.ind.withAlpha (0.25f * alpha)); g.fillEllipse (l.expanded (3.0f));
            g.setColour (t.ind.withAlpha (alpha)); g.fillEllipse (l);
            g.setColour (juce::Colours::white.withAlpha (0.6f * alpha)); g.fillEllipse (l.reduced (1.8f));
        }
        else
        {
            g.setColour (rgb (20, 23, 28).withAlpha (alpha)); g.fillEllipse (l);
            g.setColour (juce::Colours::white.withAlpha (0.08f * alpha)); g.drawEllipse (l.translated (0, 0.5f), 0.8f);
        }
    }
    const juce::Rectangle<float> tr (x0 + led, b.getY(), tw + 2.0f, b.getHeight());
    g.setFont (f);
    if (s.on)
    {
        const auto glow = s.gold ? t.gold : t.ind;
        g.setColour (glow.withAlpha (0.28f * alpha));
        for (auto o : { juce::Point<float> (-1, 0), { 1, 0 }, { 0, -1 }, { 0, 1 } })
            g.drawText (text, tr.translated (o.x, o.y), juce::Justification::centredLeft, false);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f * alpha));
    g.drawText (text, tr.translated (0, 1), juce::Justification::centredLeft, false);
    const auto col = s.on ? (s.gold ? rgb (255, 244, 223) : rgb (238, 246, 255)) : rgb (185, 195, 209);
    g.setColour (col.withAlpha (alpha));
    g.drawText (text, tr, juce::Justification::centredLeft, false);
}

//==============================================================================
Knob::Knob()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    setMouseDragSensitivity (220);
    setScrollWheelEnabled (true);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    last = virt = e.position;
    Slider::mouseDown (e);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - last;
    last = e.position;
    virt += d * (e.mods.isShiftDown() ? 0.15f : 1.0f);
    Slider::mouseDrag (e.withNewPosition (virt));
}

//==============================================================================
LookAndFeel::LookAndFeel() { applyColours(); }
LookAndFeel::~LookAndFeel() = default;

void LookAndFeel::setThemeIndex (int i)
{
    i = juce::jlimit (0, kNumThemes - 1, i);
    if (i == themeIndex) return;
    themeIndex = i;
    knobCache.clear();
    applyColours();
}

void LookAndFeel::applyColours()
{
    const auto& t = th();
    const auto text = rgb (221, 230, 241);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, t.gold.withAlpha (0.35f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::outlineWhenEditingColourId, t.gold.withAlpha (0.7f));
    setColour (juce::Label::backgroundWhenEditingColourId, rgb (10, 13, 18));
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, rgb (10, 13, 18));
    setColour (juce::TextEditor::highlightColourId, t.gold.withAlpha (0.35f));
    setColour (juce::TextEditor::focusedOutlineColourId, t.gold.withAlpha (0.8f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.1f));
    setColour (juce::CaretComponent::caretColourId, t.gold);
    setColour (juce::TextButton::textColourOffId, rgb (185, 195, 209));
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, t.gold);
    setColour (juce::PopupMenu::backgroundColourId, rgb (26, 29, 35));
    setColour (juce::PopupMenu::textColourId, rgb (223, 230, 239));
    setColour (juce::PopupMenu::headerTextColourId, rgb (138, 149, 166));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::white.withAlpha (0.08f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.gold.interpolatedWith (juce::Colours::white, 0.2f));
    setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::ListBox::backgroundColourId, rgb (8, 10, 14));
    setColour (juce::ListBox::outlineColourId, juce::Colours::white.withAlpha (0.06f));
    setColour (juce::TooltipWindow::backgroundColourId, rgb (10, 13, 18));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::Slider::trackColourId, t.ind);
    setColour (juce::Slider::backgroundColourId, rgb (6, 8, 11));
    setColour (juce::Slider::thumbColourId, rgb (200, 206, 214));
}

LookAndFeel::KnobKind LookAndFeel::kindOf (const juce::Slider& s, float d)
{
    const auto k = s.getProperties()["studioKnob"].toString();
    if (k == "big") return KnobKind::big;
    if (k == "medium") return KnobKind::medium;
    if (k == "small") return KnobKind::small;
    return d >= 96.0f ? KnobKind::medium : KnobKind::small;
}

const juce::Image& LookAndFeel::knobBase (KnobKind kind, float D, float scale)
{
    const int px = juce::jmax (8, juce::roundToInt (D * scale));
    const auto keyStr = juce::String ((int) kind) + "|" + juce::String (px) + "|" + juce::String (themeIndex);
    if (auto it = knobCache.find (keyStr); it != knobCache.end()) return it->second;
    if (knobCache.size() > 48) knobCache.clear();

    const float S = (float) px / scale;                    // logical size, exactly matching the pixel grid
    juce::Image img (juce::Image::ARGB, px, px, true);
    const auto& t = th();
    const juce::Point<float> c (S * 0.5f, S * 0.5f);
    const auto bronze = t.gold.interpolatedWith (rgb (120, 110, 100), 0.35f), steelK = rgb (150, 156, 166);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        if (kind == KnobKind::big)
        {
            const float well = .484f * S, tick0 = .393f * S, tick1 = .440f * S, bez0 = .349f * S, bez1 = .377f * S, skirt = .31f * S;
            juce::ColourGradient wl (rgb (10, 12, 16), c.x, c.y - 4, rgb (2, 3, 4), c.x, c.y + well, true);
            wl.addColour (0.92, rgb (5, 7, 10));
            g.setGradientFill (wl); g.fillEllipse (c.x - well, c.y - well, 2 * well, 2 * well);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.95f), 0, c.y - well, juce::Colours::white.withAlpha (0.22f), 0, c.y + well, false));
            g.drawEllipse (c.x - well + 0.6f, c.y - well + 0.6f, 2 * well - 1.2f, 2 * well - 1.2f, 1.4f);
            {
                juce::Path ring; ring.addEllipse (c.x - tick1 - 3, c.y - tick1 - 3, 2 * tick1 + 6, 2 * tick1 + 6);
                ring.addEllipse (c.x - tick0 + 3, c.y - tick0 + 3, 2 * tick0 - 6, 2 * tick0 - 6); ring.setUsingNonZeroWinding (false);
                g.setColour (t.screen.interpolatedWith (juce::Colours::black, 0.35f)); g.fillPath (ring);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), c.x, c.y + 9, juce::Colours::transparentBlack, c.x, c.y + 9 + bez1 + 10, true));
            g.fillEllipse (c.x - bez1 - 10, c.y + 9 - bez1 - 10, 2 * (bez1 + 10), 2 * (bez1 + 10));
            knurl (g, c, skirt - 1, bez0, 180, bronze.interpolatedWith (rgb (20, 20, 22), 0.55f), rgb (8, 8, 10));
        }
        else
        {
            const float seat = .395f * S, skirt = .303f * S, capR = .244f * S;
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), c.x, c.y + skirt * 0.22f, juce::Colours::transparentBlack, c.x, c.y + skirt * 0.22f + skirt * 1.45f, true));
            g.fillEllipse (c.x - skirt * 1.45f, c.y + skirt * 0.22f - skirt * 1.45f, skirt * 2.9f, skirt * 2.9f);
            g.setColour (rgb (8, 9, 12)); g.fillEllipse (c.x - seat, c.y - seat, 2 * seat, 2 * seat);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0, c.y - seat, juce::Colours::white.withAlpha (0.2f), 0, c.y + seat, false));
            g.drawEllipse (c.x - seat + 0.6f, c.y - seat + 0.6f, 2 * seat - 1.2f, 2 * seat - 1.2f, 1.3f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), c.x, c.y + 5, juce::Colours::transparentBlack, c.x, c.y + 5 + seat, true));
            g.fillEllipse (c.x - seat, c.y + 5 - seat, 2 * seat, 2 * seat);
            const int teeth = kind == KnobKind::medium ? 96 : 72;
            knurl (g, c, capR + 2, skirt, teeth, bronze.interpolatedWith (steelK, 0.3f).interpolatedWith (rgb (20, 20, 22), 0.45f), rgb (10, 10, 12));
        }
    }
    if (kind == KnobKind::big)
    {
        const float bez0 = .349f * S, bez1 = .377f * S, skirt = .31f * S, capR = .254f * S;
        shadeRing (img, scale, c, bez0, bez1, [] (float, float ang, float, float)
        {
            const float v = conic (ang, 0.4f, { { 0, .18f }, { .08f, .95f }, { .16f, .35f }, { .3f, .08f }, { .42f, .22f }, { .5f, .12f },
                                                { .58f, .88f }, { .66f, .3f }, { .8f, .06f }, { .9f, .2f }, { 1, .18f } });
            return rgb (28, 30, 35).interpolatedWith (rgb (214, 220, 228), v);
        });
        const auto gd = t.gold.interpolatedWith (juce::Colours::black, 0.7f), gl = t.gold.interpolatedWith (rgb (255, 244, 220), 0.45f);
        shadeRing (img, scale, c, capR + 1, skirt, [&] (float d, float ang, float nx, float ny)
        {
            const float v = conic (ang, -0.5f, { { 0, .2f }, { .09f, 1 }, { .2f, .4f }, { .35f, .12f }, { .5f, .3f }, { .6f, .95f },
                                                 { .7f, .35f }, { .85f, .1f }, { 1, .2f } });
            auto col = gd.interpolatedWith (gl, v);
            const float l = nx * kLightX + ny * kLightY;
            col = l > 0 ? col.interpolatedWith (juce::Colours::white, 0.12f * l) : col.interpolatedWith (juce::Colours::black, 0.4f * -l);
            if (d > skirt - 0.8f) col = col.interpolatedWith (juce::Colours::black, 0.6f);
            return col;
        });
        cap (img, scale, c, capR, t, 11);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        juce::ColourGradient bz (juce::Colours::white.withAlpha (0.5f), c.x - bez1, c.y - bez1, juce::Colours::black.withAlpha (0.5f), c.x + bez1, c.y + bez1, false);
        bz.addColour (0.5, juce::Colours::transparentWhite);
        g.setGradientFill (bz); g.drawEllipse (c.x - bez1 + 0.6f, c.y - bez1 + 0.6f, 2 * bez1 - 1.2f, 2 * bez1 - 1.2f, 1.2f);
    }
    else
        cap (img, scale, c, .244f * S, t, kind == KnobKind::medium ? 9 : 5);

    return knobCache[keyStr] = img;
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const float D = (float) juce::jmin (w, h);
    const auto kind = kindOf (slider, D);
    const juce::Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto& t = th();
    const bool active = slider.isMouseButtonDown(), hover = slider.isMouseOverOrDragging();
    const float dim = (slider.isEnabled() ? 1.0f : 0.35f) * (slider.getProperties()["dim"] ? 0.45f : 1.0f);
    const float a = startAngle + pos * (endAngle - startAngle);

    const auto& base = knobBase (kind, D, scale);
    const float S = (float) base.getWidth() / scale;
    g.setOpacity (1.0f);
    g.drawImage (base, juce::Rectangle<float> (c.x - S * 0.5f, c.y - S * 0.5f, S, S), juce::RectanglePlacement::stretchToFit);

    const auto ind = t.ind, indHi = mix (t.ind, juce::Colours::white, 0.15f);
    if (kind == KnobKind::big)
    {
        const float tick0 = .393f * S, tick1 = .440f * S;
        constexpr int N = 64;
        for (int i = 0; i <= N; ++i)
        {
            const float tt = (float) i / N, aa = startAngle + (endAngle - startAngle) * tt;
            const bool lit = tt <= pos + 1e-6f, maj = i % 8 == 0;
            juce::Path p; p.startNewSubPath (polar (c, tick0 + (maj ? 0.0f : 3.0f), aa)); p.lineTo (polar (c, tick1, aa));
            if (lit) glowStroke (g, p, indHi, maj ? 1.8f : 1.3f, active ? 3.0f : 2.0f, (active ? 1.0f : 0.92f) * dim);
            else { g.setColour (rgb (150, 165, 185).withAlpha (0.22f * dim)); g.strokePath (p, juce::PathStrokeType (maj ? 1.4f : 1.0f)); }
        }
        if (pos > 0.003f)
        {
            juce::Path arc; arc.addCentredArc (c.x, c.y, tick0 - 6, tick0 - 6, 0, startAngle, a, true);
            glowStroke (g, arc, ind, 2.4f, active ? 4.5f : 3.2f, (active ? 0.95f : 0.75f) * dim);
        }
    }
    else
    {
        const float R = .445f * S;
        juce::Path track; track.addCentredArc (c.x, c.y, R, R, 0, startAngle, endAngle, true);
        g.setColour (juce::Colours::black.withAlpha (0.6f * dim)); g.strokePath (track, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (rgb (150, 165, 185).withAlpha (0.14f * dim)); g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i <= 10; ++i)
        {
            const float aa = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            g.setColour (rgb (150, 165, 185).withAlpha (0.35f * dim));
            g.drawLine ({ polar (c, R + 4, aa), polar (c, R + (i % 5 == 0 ? 8.0f : 6.0f), aa) }, 1.0f);
        }
        if (pos > 0.003f)
        {
            juce::Path arc; arc.addCentredArc (c.x, c.y, R, R, 0, startAngle, a, true);
            glowStroke (g, arc, mix (ind, juce::Colours::white, 0.12f), active ? 2.8f : 2.3f, active ? 3.4f : 2.2f, (active ? 1.0f : 0.92f) * dim);
        }
    }
    const float skirt = (kind == KnobKind::big ? .31f : .303f) * S, capR = (kind == KnobKind::big ? .254f : .244f) * S;
    if (hover || active)
    {
        g.setColour (ind.withAlpha ((active ? 0.35f : 0.2f) * dim));
        g.drawEllipse (c.x - skirt - 1, c.y - skirt - 1, 2 * skirt + 2, 2 * skirt + 2, 1.4f);
    }
    // engraved, lit pointer on the cap
    const auto p0 = polar (c, capR * 0.36f, a), p1 = polar (c, capR * 0.93f, a);
    const float pw = kind == KnobKind::big ? 2.6f : 2.1f;
    g.setColour (juce::Colours::black.withAlpha (0.7f * dim));
    g.drawLine ({ p0.translated (0.6f, 0.9f), p1.translated (0.6f, 0.9f) }, pw + 1.2f);
    juce::Path ptr; ptr.startNewSubPath (p0); ptr.lineTo (p1);
    glowStroke (g, ptr, mix (ind, juce::Colours::white, active ? 0.45f : 0.3f), pw, active ? 3.5f : 2.5f, dim);
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& s)
{
    const auto kindStr = s.getProperties()["studioKnob"].toString();
    if (kindStr.isEmpty() || ! s.isRotary())
        return MASLookAndFeel::getSliderLayout (s);
    juce::Slider::SliderLayout l;
    const int w = s.getWidth();
    l.sliderBounds = { 0, 0, w, w };
    if (s.getTextBoxPosition() == juce::Slider::NoTextBox) return l;
    if (kindStr == "big")
        l.textBoxBounds = { w / 2 - 60, w, 120, juce::jmax (0, s.getHeight() - w) };
    else
        l.textBoxBounds = { w / 2 - 50, juce::roundToInt ((float) w * 0.8f) + 2, 100, 18 };
    return l;
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = MASLookAndFeel::createSliderTextBox (slider);
    const bool big = slider.getProperties()["studioKnob"].toString() == "big";
    l->setFont (inter (big ? 15.0f : 13.0f, big));
    l->setColour (juce::Label::textColourId, rgb (221, 230, 241));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::focusedOutlineColourId, th().gold.withAlpha (0.8f));
    l->setColour (juce::TextEditor::backgroundColourId, rgb (10, 13, 18));
    return l;
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto& t = th();
    const float dim = s.isEnabled() ? 1.0f : 0.4f;
    if (s.getProperties()["fader"] && style == juce::Slider::LinearVertical)
    {
        const float cx = (float) x + (float) w * 0.5f, top = (float) y, bot = (float) (y + h);
        recess (g, { cx - 5.0f, top - 12.0f, 10.0f, bot - top + 24.0f }, 5.0f, rgb (6, 8, 11));
        const float fillTop = juce::jlimit (top - 10.0f, bot + 10.0f, pos);
        juce::Path f; f.addRoundedRectangle (cx - 2.0f, fillTop, 4.0f, juce::jmax (0.0f, bot + 9.0f - fillTop), 2.0f);
        for (int k = 3; k >= 1; --k) { g.setColour (t.ind.withAlpha (0.1f * dim)); g.fillRoundedRectangle (f.getBounds().expanded ((float) k * 1.6f), 3.0f); }
        g.setColour (t.ind.withAlpha (dim)); g.fillPath (f);
        // chrome cap
        const juce::Rectangle<float> cap (cx - 19.0f, pos - 12.0f, 38.0f, 24.0f);
        juce::Path cp; cp.addRoundedRectangle (cap, 6.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 8, { 0, 4 }).drawForPath (g, cp);
        juce::ColourGradient cg (rgb (233, 237, 242), 0, cap.getY(), rgb (208, 214, 221), 0, cap.getBottom(), false);
        cg.addColour (0.28, rgb (168, 175, 185)); cg.addColour (0.52, rgb (93, 100, 110)); cg.addColour (0.74, rgb (143, 151, 162));
        g.setGradientFill (cg); g.fillPath (cp);
        if (s.isMouseOverOrDragging()) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillPath (cp); }
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.drawLine (cap.getX() + 5, cap.getY() + 1, cap.getRight() - 5, cap.getY() + 1, 1.0f);
        g.setColour (rgb (27, 30, 35)); g.fillRoundedRectangle (cap.getX() + 5, cap.getCentreY() - 1, cap.getWidth() - 10, 2.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRect (cap.getX() + 5, cap.getCentreY() + 1, cap.getWidth() - 10, 1.0f);
        return;
    }
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        recess (g, r, 5.0f, rgb (7, 9, 12));
        const auto fill = style == juce::Slider::LinearBar ? r.withRight (pos).reduced (2.0f) : r.withTop (pos).reduced (2.0f);
        g.setColour (t.ind.withAlpha (0.35f * dim)); g.fillRoundedRectangle (fill, 3.0f);
        return;
    }
    MASLookAndFeel::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto& props = b.getProperties();
    if (props["presetName"]) return;
    const auto& t = th();
    const auto r = b.getLocalBounds().toFloat();
    if (props["tab"])
    {
        if (b.getToggleState())
        {
            const auto u = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.18f, r.getBottom() - 3.0f, r.getWidth() * 0.64f, 2.0f);
            for (int k = 3; k >= 1; --k) { g.setColour (t.ind.withAlpha (0.12f)); g.fillRoundedRectangle (u.expanded ((float) k * 1.5f), 2.0f); }
            g.setColour (t.ind); g.fillRoundedRectangle (u, 1.0f);
        }
        else if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f)); g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
        }
        return;
    }
    KeyState s;
    s.on = b.getToggleState(); s.hover = highlighted; s.down = down; s.gold = props["gold"]; s.enabled = b.isEnabled();
    key (g, r.reduced (1.0f, 1.0f).withTrimmedBottom (1.0f), t, s);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool down)
{
    const auto& props = b.getProperties();
    if (props["presetName"]) return;
    const auto& t = th();
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const juce::String text = props["onoff"] ? juce::String (on ? "ON" : "OFF") : b.getButtonText();
    if (props["tab"])
    {
        const auto f = inter (10.0f, true, 0.14f);
        g.setFont (f);
        if (on)
        {
            g.setColour (t.ind.withAlpha (0.3f));
            for (auto o : { juce::Point<float> (-1, 0), { 1, 0 }, { 0, -1 }, { 0, 1 } })
                g.drawText (text, r.translated (o.x, o.y - 1), juce::Justification::centred, false);
        }
        g.setColour (juce::Colours::black.withAlpha (0.8f)); g.drawText (text, r.translated (0, 0), juce::Justification::centred, false);
        g.setColour (on ? rgb (241, 245, 250) : highlighted ? rgb (196, 205, 217) : rgb (127, 138, 155));
        g.drawText (text, r.translated (0, -1), juce::Justification::centred, false);
        return;
    }
    KeyState s;
    s.on = on; s.hover = highlighted; s.down = down; s.gold = props["gold"]; s.enabled = b.isEnabled();
    s.led = props["led"] || props["onoff"];
    const float fh = props.contains ("fontH") ? (float) props["fontH"] : juce::jlimit (9.0f, 11.0f, (float) b.getHeight() * 0.36f);
    keyText (g, r.reduced (1.0f, 1.0f).withTrimmedBottom (1.0f), text, t, s, fh);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return inter (10.0f, true, 0.14f); }

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    juce::ColourGradient gr (rgb (7, 9, 13), 0, 0, rgb (13, 17, 23), 0, r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r, 6.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0, 0, juce::Colours::white.withAlpha (0.08f), 0, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    juce::Path chev;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    chev.startNewSubPath (ax - 4.0f, ay - 2.0f); chev.lineTo (ax, ay + 2.0f); chev.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (th().gold.withAlpha (box.isEnabled() ? 0.9f : 0.35f));
    g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return inter (12.5f); }
juce::Font LookAndFeel::getPopupMenuFont() { return inter (13.0f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setGradientFill (juce::ColourGradient (rgb (34, 38, 45), 0, 0, rgb (21, 24, 29), 0, (float) height, false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (rgb (10, 13, 18).withAlpha (0.96f)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (rgb (238, 243, 249)); g.setFont (inter (12.0f));
    g.drawFittedText (text, r.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}

const Theme& themeOf (const juce::Component& c)
{
    if (auto* l = dynamic_cast<LookAndFeel*> (&c.getLookAndFeel())) return l->th();
    return theme (0);
}
} // namespace studio
