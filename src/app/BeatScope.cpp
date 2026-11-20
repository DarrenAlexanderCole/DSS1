#include "BeatScope.h"
#include "Hardware.h"
#include "Scope.h"

namespace dss1::ui
{

namespace
{
    void fft (std::vector<std::complex<float>>& a)
    {
        // In place, iterative radix-2.
        const auto n = a.size();
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const auto angle = -2.0f * juce::MathConstants<float>::pi / static_cast<float> (len);
            const std::complex<float> wl (std::cos (angle), std::sin (angle));
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }
}

BeatScope::BeatScope (AudioEngine& e) : engine (e)
{
    setInterceptsMouseClicks (false, false);
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<std::size_t> (i)] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / (fftSize - 1));
    spectrum.resize (fftSize);
    startTimerHz (50);
}

void BeatScope::resized() {}

void BeatScope::analyse()
{
    engine.recentOutput (samples, fftSize);
    for (int i = 0; i < fftSize; ++i)
        spectrum[static_cast<std::size_t> (i)] = { samples[static_cast<std::size_t> (i)] * window[static_cast<std::size_t> (i)], 0.0f };
    fft (spectrum);
    const auto rate = engine.sampleRate() > 0 ? engine.sampleRate() : 44100.0;
    float bass = 0;
    for (int b = 0; b < bands; ++b)
    {
        // Log-spaced bands, 30 Hz … 16 kHz, tilted up 3 dB an octave so music reads level across.
        const auto f0 = 30.0 * std::pow (533.0, static_cast<double> (b) / bands), f1 = 30.0 * std::pow (533.0, static_cast<double> (b + 1) / bands);
        const auto k0 = juce::jlimit (1, fftSize / 2 - 1, static_cast<int> (f0 / rate * fftSize));
        const auto k1 = juce::jlimit (k0 + 1, fftSize / 2, static_cast<int> (f1 / rate * fftSize));
        float energy = 0;
        for (int k = k0; k < k1; ++k)
            energy = juce::jmax (energy, std::abs (spectrum[static_cast<std::size_t> (k)]));
        // Low bands narrower than one bin share it; interpolate between bins so the curve stays smooth.
        if (f1 / rate * fftSize - f0 / rate * fftSize < 1.0)
        {
            const auto kf = (f0 + f1) * 0.5 / rate * fftSize;
            const auto ka = juce::jlimit (1, fftSize / 2 - 2, static_cast<int> (kf));
            const auto t = static_cast<float> (kf - ka);
            energy = std::abs (spectrum[static_cast<std::size_t> (ka)]) * (1.0f - t) + std::abs (spectrum[static_cast<std::size_t> (ka + 1)]) * t;
        }
        const auto db = 20.0f * std::log10 (energy / (fftSize * 0.25f) + 1e-9f) + 3.0f * static_cast<float> (std::log2 (f0 / 40.0)) + 6.0f;
        const auto target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
        auto& l = level[static_cast<std::size_t> (b)];
        l = target > l ? target : l + (target - l) * 0.18f;   // jump up, fall gently
        if (b < 10)
            bass += target;
    }
    // A kick: the low bands jumping above where they've been.
    bass /= 10.0f;
    if (bass > bassAverage + 0.12f && bass > 0.35f)
        pulse = 1.0f;
    bassAverage += (bass - bassAverage) * 0.08f;
    pulse *= 0.86f;
}

void BeatScope::timerCallback()
{
    if (! isShowing())
        return;
    analyse();
    // Keep a ghost every other frame, so the trail reaches back about a quarter of a second.
    if (++frame % 2 == 0)
    {
        for (int i = ghosts - 1; i > 0; --i)
            history[static_cast<std::size_t> (i)] = history[static_cast<std::size_t> (i - 1)];
        history[0] = level;
    }
    for (int b = 0; b < bands; ++b)
    {
        // Peak hold: pushed up by the curve, falls with gravity.
        auto& p = peak[static_cast<std::size_t> (b)];
        auto& v = peakVelocity[static_cast<std::size_t> (b)];
        if (level[static_cast<std::size_t> (b)] >= p)
        {
            p = level[static_cast<std::size_t> (b)];
            v = 0;
        }
        else
        {
            v += 0.0016f;
            p = juce::jmax (0.0f, p - v);
        }
    }
    repaint();
}

void BeatScope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    if (area.isEmpty())
        return;
    const auto amber = hw::colour::amber;
    const auto beat = 1.0f + 0.6f * pulse;
    const auto floorY = area.getBottom();
    const auto toPoints = [&] (const std::array<float, bands>& v) {
        std::vector<juce::Point<float>> pts;
        pts.reserve (bands);
        for (int b = 0; b < bands; ++b)
            pts.push_back ({ area.getX() + area.getWidth() * static_cast<float> (b) / static_cast<float> (bands - 1),
                             floorY - juce::jlimit (0.0f, 1.0f, v[static_cast<std::size_t> (b)]) * area.getHeight() });
        return pts;
    };

    // Graticule: octaves across (100 Hz, 1 kHz, 10 kHz brighter, with small labels), levels up.
    const auto lit = hw::light();
    const auto xForHz = [&] (double hz) { return area.getX() + area.getWidth() * static_cast<float> (std::log (hz / 30.0) / std::log (533.0)); };
    g.setFont (hw::label (9.0f, false));
    for (const int hz : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
    {
        const bool decade = hz == 100 || hz == 1000 || hz == 10000;
        const auto x = xForHz (hz);
        g.setColour (amber.withAlpha ((decade ? 0.12f : 0.05f) * lit));
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        if (decade)
        {
            g.setColour (amber.withAlpha (0.35f * lit));
            g.drawText (hz >= 1000 ? juce::String (hz / 1000) + "K" : juce::String (hz),
                        juce::Rectangle<float> (x + 3, floorY - 12, 30, 11), juce::Justification::centredLeft, false);
        }
    }
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (amber.withAlpha (0.05f * lit));
        g.fillRect (area.getX(), area.getY() + area.getHeight() * static_cast<float> (i) / 4.0f, area.getWidth(), 1.0f);
    }

    // Ghosts of the last moments, oldest faintest, so each beat leaves a fading wake.
    for (int i = ghosts - 1; i >= 0; --i)
    {
        const auto fade = 1.0f - static_cast<float> (i + 1) / static_cast<float> (ghosts + 1);
        const auto curve = scope::smooth (toPoints (history[static_cast<std::size_t> (i)]));
        g.setColour (amber.withAlpha (0.16f * fade * fade * lit));
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    }

    // Now: a filled glowing curve that blooms on each kick.
    const auto now = scope::smooth (toPoints (level));
    scope::fillUnder (g, now, area.getY(), floorY, amber, juce::jlimit (0.0f, 1.0f, 0.42f * beat));
    if (pulse > 0.05f)
    {
        juce::ColourGradient bloom (amber.withAlpha (0.16f * pulse * lit), area.getCentreX(), floorY, juce::Colours::transparentBlack,
                                    area.getCentreX(), area.getY(), false);
        g.setGradientFill (bloom);
        g.fillRect (area);
    }
    scope::trace (g, now, amber, 1.6f, beat);

    // Peak hold: a thin warm-white line drifting down.
    const auto peaks = scope::smooth (toPoints (peak));
    g.setColour (juce::Colour (0xffffe2b8).withAlpha (0.55f * lit));
    g.strokePath (peaks, juce::PathStrokeType (1.0f));
}

} // namespace dss1::ui
