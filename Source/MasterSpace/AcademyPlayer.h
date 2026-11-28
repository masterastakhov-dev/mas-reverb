#pragma once

#include "MSEngine.h"

// Preview-only transport. The message thread builds and publishes immutable audio once.
// SHOW and session recall revoke playback without touching any musical parameter.
class AcademyPlayer
{
public:
    enum Mode { showMode, armed, playing };
    void show() { mode.store (showMode); }
    void arm() { int expected = showMode; mode.compare_exchange_strong (expected, armed); }
    bool available() const { return mode.load() != showMode; }
    void stop() { int expected = playing; mode.compare_exchange_strong (expected, armed); }
    void play()
    {
        if (!available()) return;
        if (!published.load()) buildExample();
        int expected = armed;
        mode.compare_exchange_strong (expected, playing);
    }
    void resetAudio() { stop(); gain = 0; position = 0; }
    std::atomic<int> delayMs { 5 };
    std::atomic<bool> dryOnly { false };
    void render (juce::AudioBuffer<float>& output, double rate)
    {
        const bool running = mode.load() == playing;
        if ((!running && gain == 0) || rate <= 0 || !published.load()) return;
        const double step = sourceRate / rate;
        const float fade = static_cast<float> (1.0 / (0.008 * rate));
        const float targetDelay = delayMs.load() * sourceRate / 1000.f;
        const float targetWet = dryOnly.load() ? 0.f : 1.f;
        if (gain == 0) { delay = targetDelay; wet = targetWet; }
        for (int i = 0; i < output.getNumSamples(); ++i)
        {
            gain = juce::jlimit (0.f, 1.f, gain + (running ? fade : -fade));
            if (gain == 0) { position = 0; break; }
            delay += juce::jlimit (-0.25f, 0.25f, targetDelay - delay);
            wet += juce::jlimit (-fade, fade, targetWet - wet);
            for (int ch = 0; ch < juce::jmin (2, output.getNumChannels()); ++ch)
                output.addSample (ch, i, gain * (at (0, position) + wet * at (ch + 1, position - delay)));
            position += step;
            if (position >= frames) position -= frames;
        }
    }
private:
    static constexpr int sourceRate = 48000, frames = sourceRate * 3;
    std::atomic<int> mode { showMode };
    std::atomic<bool> published { false };
    juce::AudioBuffer<float> material;
    float gain = 0, delay = 0, wet = 1;
    double position = 0;
    float at (int ch, double pos) const
    {
        if (pos < 0 || pos >= frames - 1) return 0;
        const int n = static_cast<int> (pos);
        const auto* s = material.getReadPointer (ch);
        return s[n] + static_cast<float> (pos - n) * (s[n+1] - s[n]);
    }
    void buildExample()
    {
        material.setSize (3, frames);
        material.clear();
        uint32_t seed = 1926;
        for (int hit : { 0, 16000, 24000, 32000 })
            for (int n = 0; n < 12000; ++n)
            {
                seed = seed * 1664525u + 1013904223u;
                const double t = double (n) / sourceRate;
                const double noise = double (seed) / 2147483648.0 - 1;
                const float sample = static_cast<float> (std::min (1., n / 24.) *
                    (0.14 * noise * std::exp (-t*28) + 0.11 * std::sin (t*1160) * std::exp (-t*34)));
                material.addSample (0, hit+n, sample);
            }
        for (int ch : { 1, 2 }) material.copyFrom (ch, 0, material, 0, 0, frames);
        auto engine = std::make_unique<mspace::Engine>();
        mspace::Params scene;
        scene.mix = 1; scene.decay = 0.85f;
        engine->setParams (scene);
        engine->prepare (sourceRate);
        engine->process (material.getWritePointer (1), material.getWritePointer (2), frames);
        for (int ch : { 1, 2 }) material.applyGainRamp (ch, frames-4800, 4800, 1.f, 0.f);
        published.store (true);
    }
};
