// MASTER SPACE 1.5 — LIVING SPACE: the central display of the MAIN page (MAS_EQ builds only).
// A room in perspective, the direct source, early wavefronts and reflection points, the late cloud,
// the ATMOSPHERIC TAIL dome. Brightness comes ONLY from levels measured in the DSP (input, early L/R,
// late L/R, ducker gain reduction); shapes and motion are an artistic reading steered by DEPTH, SIZE,
// BLOOM, MOTION and WIDTH. Dragging the source up / down moves DEPTH. The audio thread only writes a few
// numbers; all drawing happens here, and nothing is drawn while the window is closed or the room is silent.

#pragma once

namespace living
{
struct RGB { float r, g, b; };
inline RGB mix (RGB a, RGB b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
inline juce::Colour toColour (RGB c, float alpha) { return juce::Colour::fromFloatRGBA (c.r, c.g, c.b, juce::jlimit (0.0f, 1.0f, alpha)); }
const RGB kEarly { 0.353f, 0.839f, 1.0f }, kEarly2 { 0.231f, 0.890f, 0.769f }, kLate { 1.0f, 0.52f, 0.10f }, kLate2 { 1.0f, 0.72f, 0.26f },
          kAtm { 0.239f, 0.373f, 1.0f }, kAtm2 { 0.604f, 0.502f, 1.0f }, kAtmDeep { 0.114f, 0.169f, 0.604f }, kDirect { 1.0f, 0.902f, 0.690f },
          kLine { 0.49f, 0.627f, 0.902f }, kGold { 0.886f, 0.722f, 0.4f };

class View : public juce::Component, private juce::Timer
{
public:
    View (MasterSpaceAudioProcessor& p, juce::Image logo) : proc (p), emblem (std::move (logo))
    {
        auto raw = [&] (const char* id) { return p.apvts.getRawParameterValue (id); };
        pDecay = raw (ParamIDs::decay); pSize = raw (ParamIDs::size); pDepth = raw (ParamIDs::depth); pWidth = raw (ParamIDs::width);
        pAlgo = raw (ParamIDs::algorithm); pTail = raw (ParamIDs::tail); pBloom = raw (ParamIDs::bloom); pMotion = raw (ParamIDs::motion);
        pDuckOn = raw (ParamIDs::duckOn);
        depthPrm = p.apvts.getParameter (ParamIDs::depth);
        for (auto& q : parts) q.w = 0.0f;
        setOpaque (true);
        startTimerHz (60);
    }
    ~View() override { stopTimer(); proc.spaceLevelsWanted.store (false); }

    void setIRMode (bool) {}

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        const auto area = getLocalBounds().toFloat();
        g.fillAll (MASColours::bgBottom);
        juce::Path clip; clip.addRoundedRectangle (area.reduced (0.5f), 14.0f);
        g.saveState(); g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff02050d), 0, 0, juce::Colour (0xff02050d), 0, h, false));
        g.fillRect (area);
        { juce::ColourGradient bg (juce::Colour (0xff02050d), 0, 0, juce::Colour (0xff02050d), 0, h, false); bg.addColour (0.32, juce::Colour (0xff071430)); bg.addColour (0.62, juce::Colour (0xff0b1d40)); g.setGradientFill (bg); g.fillRect (area); }
        { juce::ColourGradient hz (juce::Colour::fromFloatRGBA (0.216f, 0.412f, 0.824f, 0.22f), w * 0.5f, CY, juce::Colour::fromFloatRGBA (0.216f, 0.412f, 0.824f, 0.0f), w * 0.5f + w * 0.55f, CY, true); g.setGradientFill (hz); g.fillRect (area); }

        const int mode = proc.spaceVisual.load();
        const bool avail = proc.eqAvailable();
        drawRoom (g);
        if (mode != 2 && avail)
        {
            drawSideGlow (g);
            renderGlow();
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (glowImg, area);
            drawSharp (g);
        }
        drawSource (g, mode != 2 && avail);
        drawOverlay (g, mode, avail);
        g.restoreState();
        g.setColour (juce::Colour (0xff96b9ff).withAlpha (0.14f)); g.drawRoundedRectangle (area.reduced (0.5f), 14.0f, 1.0f);
    }

    void resized() override
    {
        const float h = (float) getHeight();
        F = h * 0.98f; CY = h * 0.34f;
        gw = juce::jmax (8, getWidth() / 2); gh = juce::jmax (8, getHeight() / 2);
        glow.assign ((size_t) (gw * gh * 3), 0.0f);
        glowImg = juce::Image (juce::Image::ARGB, gw, gh, true);
    }

    // ---- the source can be dragged: up = farther (DEPTH)
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (nearSource (e.position) ? juce::MouseCursor::DraggingHandCursor : chip.contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (chip.contains (e.position)) { proc.spaceVisual.store ((proc.spaceVisual.load() + 1) % 3); applyRate(); repaint(); return; }
        if (! nearSource (e.position)) return;
        dragging = true; dragStart = depthPrm->getValue(); depthPrm->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        depthPrm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / ((float) getHeight() * 0.45f)));
        wake = 30;
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) depthPrm->endChangeGesture(); dragging = false; }

private:
    //========================================================================== measured levels -> display state
    static float sm (float cur, float tgt, float dt, float up, float dn) { return cur + (tgt - cur) * (1.0f - std::exp (-dt / (tgt > cur ? up : dn))); }
    bool shownInTree() const { for (const juce::Component* c = this; c != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return false; return true; }
    void applyRate() { const int m = proc.spaceVisual.load(); startTimerHz (m == 0 ? 60 : m == 1 ? 30 : 4); wake = 30; }

    void timerCallback() override
    {
        const bool vis = shownInTree();
        const int mode = proc.spaceVisual.load();
        proc.spaceLevelsWanted.store (vis && mode != 2, std::memory_order_relaxed);
        if (mode != lastMode) { lastMode = mode; applyRate(); }
        if (! vis) return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit (0.001, 0.1, (now - lastT) * 0.001); lastT = now;

        // parameters (they only steer the shape)
        algo = pTail->load() > 0.5f ? 3 : juce::jlimit (0, 2, juce::roundToInt (pAlgo->load()));
        decay = pDecay->load(); size = pSize->load() * 0.01f; depth = pDepth->load() * 0.01f; width = pWidth->load() * 0.01f;
        bloom = algo == 3 ? pBloom->load() * 0.01f : 0.35f; motion = algo == 3 ? pMotion->load() * 0.01f : 0.4f;
        const float sig = decay + size * 3 + depth * 5 + width + bloom + motion + (float) algo;
        if (std::abs (sig - paramSig) > 1e-5f) { paramSig = sig; wake = 45; }
        if (mode == 2) { if (wake > 0) { --wake; settleGeometry (1.0f); repaint(); } return; }

        float db[5];
        for (int k = 0; k < 5; ++k) db[k] = proc.spaceLevel[k].load (std::memory_order_relaxed);
        const float gr = juce::jmax (proc.riMeters.duckEarlyDb.load(), proc.riMeters.duckLateDb.load());
        // auto exposure: the top of the scale follows the programme peak and holds it through the decay,
        // so a tail fades at its real rate; only the absolute brightness adapts to the level
        const float wet = juce::jmax (juce::jmax (db[1], db[2]), juce::jmax (db[3], db[4]));
        if (wet > aeRef) { aeRef = wet; aeHold = decay * 1.25f + 0.6f; } else if (aeHold > 0) aeHold -= dt; else aeRef -= 24.0f * dt;
        aeRef = juce::jmax (aeRef, -52.0f);
        if (db[0] > aeIn) { aeIn = db[0]; aeInHold = 1.5f; } else if (aeInHold > 0) aeInHold -= dt; else aeIn -= 15.0f * dt;
        aeIn = juce::jmax (aeIn, -44.0f);
        auto wetN = [this] (float d) { return juce::jlimit (0.0f, 1.0f, (d - (aeRef + 8.0f - 64.0f)) / 64.0f); };
        inN = sm (inN, juce::jlimit (0.0f, 1.0f, (db[0] - (aeIn + 6.0f - 50.0f)) / 50.0f), dt, 0.012f, 0.09f);
        eLN = sm (eLN, wetN (db[1]), dt, 0.012f, 0.07f); eRN = sm (eRN, wetN (db[2]), dt, 0.012f, 0.07f);
        lLN = sm (lLN, wetN (db[3]), dt, 0.03f, 0.12f); lRN = sm (lRN, wetN (db[4]), dt, 0.03f, 0.12f);
        eN = 0.5f * (eLN + eRN); lN = 0.5f * (lLN + lRN);
        grDb = sm (grDb, gr, dt, 0.02f, 0.1f);
        t += dt;
        // onsets of the input start an early wavefront and a burst of late energy
        fastDb = sm (fastDb, db[0], dt, 0.004f, 0.03f); slowDb = sm (slowDb, db[0], dt, 0.15f, 0.3f);
        if (fastDb - slowDb > 7.0f && fastDb > -50.0f && t - lastOnset > 0.09f)
        {
            lastOnset = t; const float str = juce::jlimit (0.35f, 1.0f, (fastDb - slowDb - 7.0f) / 16.0f + 0.45f);
            flash = juce::jmax (flash, str); spawnRing (eN * 0.6f, true); emit (juce::roundToInt ((mode == 1 ? 40.0f : 90.0f) * str));
        }
        flash *= std::exp (-dt / 0.14f);
        settleGeometry (1.0f - std::exp (-dt / 0.5f));
        updateEarly (dt); updateParticles (dt, mode);
        // energy trace (true dBFS, fixed scale)
        trAcc += dt;
        while (trAcc >= 1.0f / 30.0f)
        {
            trAcc -= 1.0f / 30.0f;
            auto mN = [] (float d) { return juce::jlimit (0.0f, 1.0f, (d + 78.0f) / 72.0f); };
            trIn[trHead] = mN (db[0]); trE[trHead] = mN (juce::jmax (db[1], db[2])); trL[trHead] = mN (juce::jmax (db[3], db[4])); trHead = (trHead + 1) % kTrace;
            if (trIn[(trHead + kTrace - 1) % kTrace] > 0.0f || trE[(trHead + kTrace - 1) % kTrace] > 0.0f || trL[(trHead + kTrace - 1) % kTrace] > 0.0f) traceLive = kTrace;
            else if (traceLive > 0) --traceLive;
        }
        const bool active = inN > 0.004f || eN > 0.004f || lN > 0.004f || flash > 0.01f || ! rings.empty() || traceLive > 0 || dragging;
        if (active) wake = 20;
        if (wake > 0) { --wake; repaint(); }              // silent room: nothing is redrawn
    }

    void settleGeometry (float k)
    {
        static const float G[4][6] = { { 6, 8, 8, 10, 3, 2 }, { 7, 9, 10, 12, 5, 4 }, { 14, 16, 20, 22, 9, 8 }, { 16, 18, 22, 24, 12, 10 } };
        const float tW = G[algo][0] + G[algo][1] * size, tD = G[algo][2] + G[algo][3] * size, tH = G[algo][4] + G[algo][5] * size;
        W += (tW - W) * k; D += (tD - D) * k; H += (tH - H) * k;
        atm += ((algo == 3 ? 1.0f : 0.0f) - atm) * juce::jmin (1.0f, k * 0.85f);
        camY = 1.5f + 0.11f * H;
        const float tz = 2.2f + depth * (D * 0.62f - 2.2f);
        sz += (tz - sz) * juce::jmin (1.0f, k * 4.0f);
    }

    //========================================================================== particles (the late cloud)
    struct Part { float ex, ey, ez, r, v, k, phi, om, h, hf, age, w, s, s1, s2, s3; int c; };
    static constexpr int kParts = 520;
    Part parts[kParts];
    int head = 0; float emitAcc = 0.0f;
    juce::Random rnd { 11 };
    float rf() { return rnd.nextFloat(); }
    void emit (int count)
    {
        const int N = proc.spaceVisual.load() == 1 ? 220 : kParts;
        for (int c = 0; c < count; ++c)
        {
            auto& p = parts[head]; head = (head + 1) % N;
            p.ex = sx + (rf() - 0.5f) * 0.3f; p.ey = sy + (rf() - 0.5f) * 0.2f; p.ez = sz + (rf() - 0.5f) * 0.3f;
            const float rInf = (0.2f + 0.22f * bloom + 0.07f * atm) * W * (0.32f + rf() * 0.88f);
            p.k = (1.35f - 0.78f * bloom) * (1.0f - 0.25f * atm) * (0.75f + rf() * 0.5f);
            p.r = 0.05f; p.v = rInf * p.k; p.phi = rf() * 6.2832f; p.om = (rf() < 0.5f ? -1.0f : 1.0f) * (0.5f + rf());
            p.h = 0.0f; p.hf = rf() < 0.3f ? -rf() * 0.9f * sy : std::pow (rf(), 1.7f) * H * (0.12f + 0.2f * bloom + 0.2f * atm);
            p.age = -std::pow (rf(), 1.4f) * (0.01f + 0.9f * std::pow (bloom, 1.3f) + 0.45f * atm);
            p.w = 0.3f + rf() * 0.7f; p.s = 0.06f + std::pow (rf(), 2.2f) * 0.26f; p.c = rnd.nextInt (5);
            p.s1 = rf() * 6.2832f; p.s2 = rf() * 6.2832f; p.s3 = rf() * 6.2832f;
        }
    }
    void updateParticles (float dt, int mode)
    {
        const int N = mode == 1 ? 220 : kParts;
        const float act = juce::jmax (lN, eN * 0.5f), swirl = (0.05f + 0.5f * motion) * (0.5f + bloom) * (0.12f + 0.88f * act);
        emitAcc += dt * (mode == 1 ? 45.0f : 110.0f) * std::pow (inN, 1.6f);
        const int n = (int) emitAcc; if (n > 0) { emitAcc -= (float) n; emit (n); }
        for (int i = 0; i < N; ++i)
        {
            auto& p = parts[i];
            if (p.w == 0.0f) continue;
            p.age += dt; if (p.age < 0.0f) continue;
            p.r += p.v * dt; p.v *= std::exp (-p.k * dt);
            p.phi += p.om * swirl * dt / (0.6f + p.r / W * 3.0f);
            p.h += (p.hf - p.h) * (1.0f - std::exp (-p.k * 0.6f * dt));
        }
    }

    //========================================================================== early wavefronts + reflection points
    struct Ring { float r, prev, amp, track, x, z; };
    struct Refl { float x, y, z, w, b; };
    std::vector<Ring> rings; std::vector<Refl> refl; float lastRing = -9.0f; juce::String reflKey;
    void spawnRing (float amp, bool track) { if (rings.size() >= 10) rings.erase (rings.begin()); rings.push_back ({ 0.15f, 0.0f, amp, track ? 0.16f : 0.0f, sx, sz }); lastRing = t; }
    void buildReflections()
    {
        std::vector<Refl> old; old.swap (refl);
        const float lx = 0.0f, ly = 1.3f, lz = 0.8f;
        auto add = [this] (float x, float y, float z, float w) { if (z > 0.2f && z <= D && std::abs (x) <= W * 0.5f + 0.01f && y >= 0.0f && y <= H) refl.push_back ({ x, y, z, w, 0.0f }); };
        const float img[5][3] = { { -W - sx, sy, sz }, { W - sx, sy, sz }, { sx, sy, 2 * D - sz }, { sx, -sy, sz }, { sx, 2 * H - sy, sz } };
        for (int n = 0; n < 5; ++n)
        {
            const float ix = img[n][0], iy = img[n][1], iz = img[n][2]; float tt;
            if (n < 2) tt = ((ix < sx ? -W * 0.5f : W * 0.5f) - lx) / (ix - lx);
            else if (n == 2) tt = (D - lz) / (iz - lz);
            else tt = ((iy < 0 ? 0.0f : H) - ly) / (iy - ly);
            add (lx + tt * (ix - lx), ly + tt * (iy - ly), lz + tt * (iz - lz), n >= 3 ? 0.45f : 1.0f);
        }
        juce::Random r2 (7);
        for (int k = 0; k < 14; ++k)
        {
            const int side = k % 3;
            if (side == 0) add (-W * 0.5f, 0.4f + r2.nextFloat() * H * 0.7f, 1.0f + r2.nextFloat() * (D - 1.0f), 0.45f);
            else if (side == 1) add (W * 0.5f, 0.4f + r2.nextFloat() * H * 0.7f, 1.0f + r2.nextFloat() * (D - 1.0f), 0.45f);
            else add (-W * 0.5f + r2.nextFloat() * W, 0.4f + r2.nextFloat() * H * 0.7f, D, 0.4f);
        }
        for (size_t i = 0; i < refl.size() && i < old.size(); ++i) refl[i].b = old[i].b;
    }
    void updateEarly (float dt)
    {
        const auto key = juce::String (W, 1) + juce::String (D, 1) + juce::String (H, 1) + juce::String (sz, 1);
        if (key != reflKey) { reflKey = key; buildReflections(); }
        if (eN > 0.45f && t - lastRing > 0.5f + 0.5f * (1.0f - eN)) spawnRing (eN * 0.38f, false);
        const float speed = (5.0f + 9.0f * size) * (atm > 0.5f ? 0.75f : 1.0f), rMax = 0.5f * W + 2.0f;
        for (auto& r : rings)
        {
            if (r.track > 0.0f) { r.track -= dt; r.amp = juce::jmax (r.amp, eN * 0.95f); }
            r.prev = r.r; r.r += speed * dt;
            for (auto& p : refl) { const float dd = std::hypot (p.x - r.x, p.z - r.z); if (dd > r.prev && dd <= r.r) p.b = juce::jmax (p.b, r.amp * p.w); }
        }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [rMax] (const Ring& r) { return r.r > rMax; }), rings.end());
        for (auto& p : refl) p.b *= std::exp (-dt / 0.32f);
    }

    //========================================================================== drawing
    static constexpr float kCamZ = -3.2f;
    bool proj (float x, float y, float z, juce::Point<float>& o, float& ps) const
    {
        const float d = z - kCamZ; if (d < 0.35f) return false;
        ps = F / d; o = { (float) getWidth() * 0.5f + x * ps, CY - (y - camY) * ps }; return true;
    }
    void line3 (juce::Graphics& g, float ax, float ay, float az, float bx, float by, float bz) const
    {
        juce::Point<float> a, b; float ps;
        if (proj (ax, ay, az, a, ps) && proj (bx, by, bz, b, ps)) g.drawLine ({ a, b }, 1.0f);
    }
    bool nearSource (juce::Point<float> p) const { juce::Point<float> s; float ps; return proc.eqAvailable() && proj (sx, sy, sz, s, ps) && p.getDistanceFrom (s) < 30.0f; }

    void drawRoom (juce::Graphics& g) const
    {
        const float wall = 1.0f - atm * 0.92f, gA = 0.085f * wall + 0.03f;
        for (int i = 0; i <= 10; ++i) { const float x = -W * 0.5f + W * (float) i / 10.0f; g.setColour (toColour (kLine, gA * (i == 0 || i == 10 ? 1.6f : 1.0f))); line3 (g, x, 0, 0, x, 0, D); }
        for (int j = 0; j <= 10; ++j) { const float z = D * (float) j / 10.0f; g.setColour (toColour (kLine, gA * (0.55f + 0.45f * (1.0f - (float) j / 10.0f)))); line3 (g, -W * 0.5f, 0, z, W * 0.5f, 0, z); }
        if (atm > 0.02f)
        {
            const float Rd = W * 0.46f, Hd = H * 0.72f, cz = sz + 1.0f;
            g.setColour (toColour (mix (kAtm, kAtm2, 0.35f), atm * (0.11f + 0.2f * lN)));
            for (int k = 0; k < 12; ++k)
            {
                const float phi = 3.14159f * (float) k / 11.0f; juce::Path pth; bool on = false;
                for (int i = 0; i <= 24; ++i) { const float th = 1.5708f * (float) i / 24.0f; juce::Point<float> o; float ps; if (proj (sx + Rd * std::cos (th) * std::cos (phi), Hd * std::sin (th), cz + Rd * std::cos (th) * std::sin (phi) * 1.1f, o, ps)) { if (on) pth.lineTo (o); else pth.startNewSubPath (o); on = true; } else on = false; }
                g.strokePath (pth, juce::PathStrokeType (1.0f));
            }
            for (float lat : { 0.25f, 0.5f, 0.75f })
            {
                const float th = 1.5708f * lat; juce::Path pth; bool on = false;
                for (int i = 0; i <= 48; ++i) { const float phi = 3.14159f * (float) i / 48.0f; juce::Point<float> o; float ps; if (proj (sx + Rd * std::cos (th) * std::cos (phi), Hd * std::sin (th), cz + Rd * std::cos (th) * std::sin (phi) * 1.1f, o, ps)) { if (on) pth.lineTo (o); else pth.startNewSubPath (o); on = true; } else on = false; }
                g.strokePath (pth, juce::PathStrokeType (1.0f));
            }
        }
        if (wall < 0.02f) return;
        g.setColour (toColour (kLine, 0.16f * wall));
        line3 (g, -W * 0.5f, 0, D, -W * 0.5f, H, D); line3 (g, W * 0.5f, 0, D, W * 0.5f, H, D); line3 (g, -W * 0.5f, H, D, W * 0.5f, H, D);
        g.setColour (toColour (kLine, 0.09f * wall));
        line3 (g, -W * 0.5f, H, 0.1f, -W * 0.5f, H, D); line3 (g, W * 0.5f, H, 0.1f, W * 0.5f, H, D);
        juce::Point<float> a, b; float ps;
        if (proj (-W * 0.5f, H, D, a, ps) && proj (W * 0.5f, 0, D, b, ps))
        {
            const RGB c = mix (kLine, atm > 0.5f ? kAtm : kLate, 0.5f);
            g.setGradientFill (juce::ColourGradient (toColour (c, 0.0f), 0, a.y, toColour (c, (0.025f + 0.06f * lN) * wall), 0, b.y, false));
            g.fillRect (juce::Rectangle<float> (a, b));
        }
        const int nP = algo == 2 ? 6 : (algo == 1 ? 3 : 0);
        for (int k = 0; k < nP; ++k)
        {
            const float z = D * (0.18f + 0.72f * (float) k / (float) juce::jmax (1, nP - 1));
            for (float side : { -1.0f, 1.0f })
            {
                const float x = side * (W * 0.5f - 0.4f);
                g.setColour (toColour (kLine, 0.1f * wall)); line3 (g, x, 0, z, x, H * 0.78f, z);
                if (proj (x, H * 0.78f, z, a, ps)) { const float r = juce::jmax (1.0f, ps * 0.09f); g.setColour (toColour (kGold, (0.25f + 0.55f * lN) * wall)); g.fillEllipse (a.x - r, a.y - r, 2 * r, 2 * r); }
            }
        }
    }

    void drawSideGlow (juce::Graphics& g) const
    {
        const float wall = 1.0f - atm * 0.9f;
        const RGB c = mix (kEarly, atm > 0.5f ? kAtm : kLate, juce::jlimit (0.0f, 1.0f, lN / (lN + eN + 1e-3f)));
        for (float side : { -1.0f, 1.0f })
        {
            const float e = side < 0 ? 0.5f * (eLN + lLN) : 0.5f * (eRN + lRN), a = 0.11f * std::pow (e, 1.4f) * wall;
            if (a < 0.003f) continue;
            juce::Point<float> p1, p2, p3, p4; float ps; const float x = side * W * 0.5f;
            if (! (proj (x, 0, 0.4f, p1, ps) && proj (x, H, 0.4f, p2, ps) && proj (x, H, D, p3, ps) && proj (x, 0, D, p4, ps))) continue;
            juce::Path q; q.addQuadrilateral (p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y);
            g.setGradientFill (juce::ColourGradient (toColour (c, a), p1.x, 0, toColour (c, a * 0.25f), p3.x, 0, false)); g.fillPath (q);
        }
    }

    // additive glow layer at 1/2 resolution (particles, nebula, reflection points, source)
    void splat (juce::Point<float> p, float radius, RGB c, float a)
    {
        const float cx = p.x * 0.5f, cy = p.y * 0.5f, r = juce::jmax (1.0f, radius * 0.5f), inv = 1.0f / (r * r);
        const int x0 = juce::jmax (0, (int) (cx - r)), x1 = juce::jmin (gw - 1, (int) (cx + r) + 1), y0 = juce::jmax (0, (int) (cy - r)), y1 = juce::jmin (gh - 1, (int) (cy + r) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float d2 = (((float) x - cx) * ((float) x - cx) + ((float) y - cy) * ((float) y - cy)) * inv;
                if (d2 >= 1.0f) continue;
                const float k = (1.0f - d2) * (1.0f - d2) * a; float* q = &glow[(size_t) ((y * gw + x) * 3)];
                q[0] += c.r * k; q[1] += c.g * k; q[2] += c.b * k;
            }
    }
    void renderGlow()
    {
        std::fill (glow.begin(), glow.end(), 0.0f);
        const float hgt = (float) getHeight();
        const int mode = proc.spaceVisual.load(), N = mode == 1 ? 220 : kParts;
        juce::Point<float> o; float ps;
        if (atm > 0.02f && lN > 0.01f)
            for (int k = 0; k < 5; ++k)
            {
                const float ang = t * (0.03f + 0.06f * motion) * (k % 2 ? 1.0f : -1.0f) + (float) k * 1.3f;
                if (proj (sx + std::cos (ang) * W * (0.18f + 0.06f * (float) k), H * (0.22f + 0.08f * std::sin (ang * 0.7f + (float) k)), sz + 2.0f + std::sin (ang) * D * 0.15f, o, ps))
                    splat (o, hgt * (0.32f + 0.1f * (float) (k % 3)) * (0.85f + 0.3f * bloom), k % 3 == 2 ? mix (kAtmDeep, kAtm2, 0.45f) : kAtmDeep, atm * std::pow (lN, 1.1f) * 0.34f);
            }
        if (atm < 0.98f && lN > 0.01f && proj (sx, sy + H * 0.15f, sz + 1.0f, o, ps))
            splat (o, hgt * 0.42f * (0.7f + 0.5f * size), mix (kLate, kLate2, 0.3f), (1.0f - atm) * std::pow (lN, 1.3f) * 0.09f);

        const float lv = std::pow (lN, 1.25f), turb = (0.12f + 0.6f * motion) * (0.1f + 0.9f * lN), fm = 0.4f + 1.2f * motion;
        const float wS = 0.5f + 0.5f * juce::jlimit (0.0f, 2.0f, width), grK = 1.0f - 0.2f * juce::jlimit (0.0f, 1.0f, grDb / 12.0f);
        const float tau = 0.5f + 0.25f * decay, ramp = 0.18f + 0.7f * bloom + 0.3f * atm, bal = juce::jlimit (-1.0f, 1.0f, (lRN - lLN) * 2.0f);
        const float dens = 0.62f * juce::jlimit (0.55f, 1.25f, std::sqrt (250.0f / ((float) visCount + 1.0f)));
        int vc = 0;
        if (lv > 0.004f)
            for (int i = 0; i < N; ++i)
            {
                const auto& p = parts[i];
                if (p.w == 0.0f || p.age < 0.0f) continue;
                float x = p.ex + std::cos (p.phi) * p.r * wS + turb * std::sin (t * 0.7f * fm + p.s1);
                float y = p.ey + p.h + turb * 0.5f * std::sin (t * 0.53f * fm + p.s2);
                float z = p.ez + std::sin (p.phi) * p.r * 0.85f + turb * std::cos (t * 0.61f * fm + p.s3);
                x = sx + (x - sx) * grK; z = sz + (z - sz) * grK; y = juce::jmax (0.05f, sy + (y - sy) * grK);
                if (! proj (x, y, z, o, ps)) continue;
                const float rad = juce::jlimit (2.0f, 13.0f, p.s * ps * 1.7f), ri = juce::jmin (1.0f, p.age / ramp);
                float a = lv * p.w * ri * ri * (3.0f - 2.0f * ri) * (0.5f + 0.5f * std::exp (-p.age / tau)) * dens;
                a *= 1.0f + 0.5f * bal * juce::jlimit (-1.0f, 1.0f, (x - sx) / (W * 0.5f));
                a *= juce::jmin (1.0f, 6.0f / rad);
                if (a < 0.004f) continue;
                ++vc;
                const RGB cl = mix (kLate, kLate2, (float) p.c / 4.0f), ca = p.c == 4 ? kAtm2 : mix (kAtm, mix (kAtm, kEarly, 0.35f), (float) p.c / 3.0f);
                splat (o, rad, mix (cl, ca, atm), a * (1.0f + 0.15f * atm));
            }
        visCount = vc;
        const float earlyDim = 1.0f - 0.45f * atm;
        for (const auto& p : refl)
            if (p.b >= 0.01f && proj (p.x, p.y, p.z, o, ps)) splat (o, 6.0f + 34.0f * p.b * juce::jmin (1.6f, ps / 40.0f), kEarly, p.b * 0.8f * earlyDim);
        if (proj (sx, sy, sz, o, ps))
        {
            const float lev = juce::jmax (inN, flash * 0.9f);
            splat (o, (8.0f + 30.0f * lev) * juce::jmin (1.4f, ps / 45.0f), kDirect, 0.25f + 0.45f * lev);
        }
        juce::Image::BitmapData bd (glowImg, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < gh; ++y)
        {
            auto* row = (juce::PixelARGB*) bd.getLinePointer (y);
            const float* q = &glow[(size_t) (y * gw * 3)];
            for (int x = 0; x < gw; ++x, q += 3)
            {
                const float r = 1.0f - std::exp (-q[0] * 1.5f), gg = 1.0f - std::exp (-q[1] * 1.5f), b = 1.0f - std::exp (-q[2] * 1.5f), a = juce::jmax (r, gg, b);
                row[x].setARGB ((juce::uint8) (a * 255.0f), (juce::uint8) (r * 255.0f), (juce::uint8) (gg * 255.0f), (juce::uint8) (b * 255.0f));   // premultiplied
            }
        }
    }

    void drawSharp (juce::Graphics& g) const
    {
        const float earlyDim = 1.0f - 0.45f * atm, rMax = 0.5f * W + 2.0f;
        for (const auto& r : rings)
        {
            const float fade = std::pow (juce::jlimit (0.0f, 1.0f, 1.0f - r.r / rMax), 2.0f) * r.amp * earlyDim;
            if (fade < 0.01f) continue;
            juce::Path pth; bool on = false;
            for (int i = 0; i <= 64; ++i)
            {
                const float th = 6.2832f * (float) i / 64.0f, x = r.x + r.r * std::cos (th), z = r.z + r.r * std::sin (th);
                juce::Point<float> o; float ps;
                if (std::abs (x) > W * 0.5f || z < 2.0f || z > D || ! proj (x, 0.03f, z, o, ps)) { on = false; continue; }
                if (on) pth.lineTo (o); else pth.startNewSubPath (o); on = true;
            }
            g.setColour (toColour (mix (kEarly, kEarly2, 0.4f), fade * 0.22f)); g.strokePath (pth, juce::PathStrokeType (5.0f));
            g.setColour (toColour (mix (kEarly, { 1, 1, 1 }, 0.15f), fade * 0.7f)); g.strokePath (pth, juce::PathStrokeType (1.1f));
        }
        // a quiet wave across the room: one strand for the early energy, one for the late
        const float w = (float) getWidth();
        for (int s = 0; s < 2; ++s)
        {
            const float e = s == 0 ? eN : lN; if (e < 0.02f) continue;
            const float zr = sz + (D - sz) * 0.5f + (s == 0 ? -0.7f : 0.4f), amp = 0.17f * H * (0.15f + 0.85f * e) * (0.7f + 0.3f * width), yb = 0.1f * H + 0.5f + (s == 0 ? -0.2f : 0.1f);
            juce::Path pth; bool on = false;
            for (int i = 0; i <= 90; ++i)
            {
                const float u = (float) i / 90.0f, x = (-0.48f + 0.96f * u) * W;
                float dy = 0.0f; static const float kf[4] = { 0.6f, 1.25f, 2.2f, 3.5f }, ka[4] = { 1.0f, 0.8f, 0.6f, 0.45f };
                for (int k = 0; k < 4; ++k) dy += ka[k] * std::sin (6.2832f * u * kf[k] * (s == 0 ? 1.6f : 1.0f) + t * (0.15f + 0.9f * motion) * (0.5f + 0.4f * (float) k) * 1.57f + (float) (s * 2 + k));
                juce::Point<float> o; float ps;
                if (! proj (x, yb + dy * amp * 0.25f * std::pow (std::sin (3.14159f * u), 0.7f), zr, o, ps)) { on = false; continue; }
                if (on) pth.lineTo (o); else pth.startNewSubPath (o); on = true;
            }
            const RGB c = s == 0 ? kEarly : mix (kLate2, kAtm, atm);
            juce::ColourGradient gr (toColour (c, 0.0f), 0, 0, toColour (c, 0.0f), w, 0, false);
            const float al = 0.34f * std::pow (e, 1.3f);
            gr.addColour (0.14, toColour (c, al)); gr.addColour (0.86, toColour (c, al));
            g.setGradientFill (gr); g.strokePath (pth, juce::PathStrokeType (1.0f));
        }
    }

    void drawSource (juce::Graphics& g, bool live) const
    {
        juce::Point<float> o; float ps;
        if (! proj (sx, sy, sz, o, ps)) return;
        const float lev = live ? juce::jmax (inN, flash) : 0.0f;
        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.98f, 0.94f, 0.55f + 0.45f * lev)); g.fillEllipse (o.x - 2.2f - 2.8f * lev, o.y - 2.2f - 2.8f * lev, 4.4f + 5.6f * lev, 4.4f + 5.6f * lev);
        if (dragging) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawEllipse (o.x - 12.0f, o.y - 12.0f, 24.0f, 24.0f, 1.0f); }
        if (live && pDuckOn->load() > 0.5f)
        {
            const float gN = juce::jlimit (0.0f, 1.0f, grDb / 12.0f), r = 16.0f + 6.0f * lev;
            juce::Path a1; a1.addCentredArc (o.x, o.y, r, r, 0.0f, -2.356f, 2.356f, true);
            g.setColour (MASColours::textDim.withAlpha (0.35f)); g.strokePath (a1, juce::PathStrokeType (1.4f));
            if (gN > 0.005f) { juce::Path a2; a2.addCentredArc (o.x, o.y, r, r, 0.0f, -2.356f, -2.356f + gN * 4.712f, true); g.setColour (MASColours::accent.withAlpha (0.9f)); g.strokePath (a2, juce::PathStrokeType (1.4f)); }
        }
    }

    void drawOverlay (juce::Graphics& g, int mode, bool avail)
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        auto f = [] (float size, bool bold, float kern) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kern); };
        static const char* names[4] = { "STUDIO ROOM", "CHAMBER", "HALL", "ATMOSPHERIC TAIL" };
        g.setFont (f (10.0f, true, 0.18f)); g.setColour (MASColours::textDim.withAlpha (0.9f)); g.drawText ("LIVING SPACE", juce::Rectangle<float> (16, 10, 200, 12), juce::Justification::centredLeft);
        g.setFont (f (15.5f, false, 0.14f)); g.setColour (MASColours::text.withAlpha (0.94f)); g.drawText (names[algo], juce::Rectangle<float> (16, 25, 260, 18), juce::Justification::centredLeft);
        g.setFont (f (10.5f, false, 0.02f)); g.setColour (MASColours::textDim.withAlpha (0.85f));
        juce::String line; const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
        line << "DECAY " << juce::String (decay, decay < 10.0f ? 2 : 1) << " s" << dot << "SIZE " << juce::roundToInt (size * 100.0f) << "%" << dot << "DEPTH " << juce::roundToInt (depth * 100.0f) << "%";
        g.drawText (line, juce::Rectangle<float> (16, 45, 320, 13), juce::Justification::centredLeft);
        if (emblem.isValid()) { const float eh = 18.0f, ew = eh * (float) emblem.getWidth() / (float) emblem.getHeight(); g.setOpacity (0.4f); g.drawImage (emblem, juce::Rectangle<float> (w * 0.5f - ew * 0.5f, 9.0f, ew, eh)); g.setOpacity (1.0f); }
        chip = { w - 70.0f, 6.0f, 60.0f, 20.0f };
        g.setFont (f (10.0f, true, 0.12f)); g.setColour ((mode == 1 ? MASColours::accent : MASColours::textDim).withAlpha (0.95f));
        g.drawText (mode == 0 ? "FULL" : mode == 1 ? "ECO" : "OFF", chip, juce::Justification::centredRight);
        if (! avail) { g.setFont (f (10.5f, true, 0.12f)); g.setColour (MASColours::textDim); g.drawText ("LIVING SPACE: ALGO MODE, ENGINE 1.2", juce::Rectangle<float> (0, h * 0.5f, w, 14), juce::Justification::centred); return; }
        if (mode == 2) { g.setFont (f (10.5f, true, 0.14f)); g.setColour (MASColours::textDim); g.drawText ("VISUALIZATION OFF", juce::Rectangle<float> (0, h * 0.58f, w, 14), juce::Justification::centred); return; }
        // energy, last 10 s (true dBFS)
        const float th = juce::jmin (46.0f, h * 0.15f), x0 = 16.0f, x1 = w - 16.0f, y1 = h - 10.0f, y0 = y1 - th, tw = x1 - x0;
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0, y0 - 18.0f, juce::Colours::black.withAlpha (0.45f), 0, h, false)); g.fillRect (0.0f, y0 - 18.0f, w, h - y0 + 18.0f);
        g.setColour (toColour (kLine, 0.12f)); g.fillRect (x0, y1, tw, 1.0f); g.fillRect (x0, y0, tw, 1.0f);
        auto path = [&] (const float* arr, bool fill) { juce::Path p; for (int i = 0; i < kTrace; ++i) { const float v = arr[(trHead + i) % kTrace], x = x0 + tw * (float) i / (float) (kTrace - 1), y = y1 - th * v; if (i == 0) p.startNewSubPath (x, fill ? y1 : y); if (fill || i > 0) p.lineTo (x, y); } if (fill) { p.lineTo (x1, y1); p.closeSubPath(); } return p; };
        const RGB lc = mix (kLate, kAtm2, atm);
        g.setColour (toColour (lc, 0.16f)); g.fillPath (path (trL, true)); g.setColour (toColour (lc, 0.85f)); g.strokePath (path (trL, false), juce::PathStrokeType (1.3f));
        g.setColour (toColour (kEarly, 0.85f)); g.strokePath (path (trE, false), juce::PathStrokeType (1.1f));
        g.setColour (toColour (kDirect, 0.5f)); g.strokePath (path (trIn, false), juce::PathStrokeType (0.9f));
        g.setFont (f (9.0f, true, 0.06f));
        g.setColour (MASColours::textDim.withAlpha (0.85f)); g.drawText ("ENERGY  10 s", juce::Rectangle<float> (x0, y0 - 14.0f, 80, 11), juce::Justification::centredLeft);
        g.setColour (toColour (kDirect, 0.8f)); g.drawText ("IN", juce::Rectangle<float> (x0 + 84, y0 - 14.0f, 20, 11), juce::Justification::centredLeft);
        g.setColour (toColour (kEarly, 0.9f)); g.drawText ("EARLY", juce::Rectangle<float> (x0 + 104, y0 - 14.0f, 40, 11), juce::Justification::centredLeft);
        g.setColour (toColour (lc, 0.95f)); g.drawText (atm > 0.5f ? "ATMOS" : "LATE", juce::Rectangle<float> (x0 + 146, y0 - 14.0f, 44, 11), juce::Justification::centredLeft);
    }

    MasterSpaceAudioProcessor& proc;
    juce::Image emblem, glowImg;
    std::atomic<float>* pDecay, *pSize, *pDepth, *pWidth, *pAlgo, *pTail, *pBloom, *pMotion, *pDuckOn;
    juce::RangedAudioParameter* depthPrm = nullptr;
    std::vector<float> glow; int gw = 8, gh = 8, visCount = 200;
    float F = 300, CY = 100;
    // state
    int algo = 2, lastMode = -1, wake = 60;
    float decay = 2, size = 0.5f, depth = 0.35f, width = 1, bloom = 0.35f, motion = 0.4f, paramSig = -1;
    float inN = 0, eLN = 0, eRN = 0, lLN = 0, lRN = 0, eN = 0, lN = 0, grDb = 0, flash = 0, t = 0, atm = 0;
    float W = 20, D = 28, H = 10, sx = 0, sy = 1.45f, sz = 8, camY = 2.5f;
    float aeRef = -40, aeHold = 0, aeIn = -30, aeInHold = 0, fastDb = -120, slowDb = -120, lastOnset = -9;
    double lastT = 0;
    static constexpr int kTrace = 300;
    float trIn[kTrace] = {}, trE[kTrace] = {}, trL[kTrace] = {}; int trHead = 0, traceLive = 0; float trAcc = 0;
    bool dragging = false; float dragStart = 0;
    juce::Rectangle<float> chip;
};
} // namespace living
