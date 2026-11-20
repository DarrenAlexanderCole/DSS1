#include "BeatScope.h"
#include "Hardware.h"

namespace dss1::ui
{

namespace
{
    constexpr float pitch = 4.0f, dotSize = 3.0f;

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
    startTimerHz (40);
}

void BeatScope::resized()
{
    rows = juce::jmax (1, static_cast<int> (static_cast<float> (getHeight()) / pitch));
    cells.assign (static_cast<std::size_t> (bands * rows), 0.0f);
}

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
        // Log-spaced bands, 40 Hz … 16 kHz, tilted up 3 dB an octave so music reads level across.
        const auto f0 = 40.0 * std::pow (400.0, static_cast<double> (b) / bands), f1 = 40.0 * std::pow (400.0, static_cast<double> (b + 1) / bands);
        const auto k0 = juce::jlimit (1, fftSize / 2 - 1, static_cast<int> (f0 / rate * fftSize));
        const auto k1 = juce::jlimit (k0 + 1, fftSize / 2, static_cast<int> (f1 / rate * fftSize));
        float energy = 0;
        for (int k = k0; k < k1; ++k)
            energy = juce::jmax (energy, std::abs (spectrum[static_cast<std::size_t> (k)]));
        const auto db = 20.0f * std::log10 (energy / (fftSize * 0.25f) + 1e-9f) + 3.0f * static_cast<float> (std::log2 (f0 / 40.0)) + 6.0f;
        const auto target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
        auto& l = level[static_cast<std::size_t> (b)];
        l = target > l ? target : l + (target - l) * 0.18f;   // jump up, fall gently
        if (b < 4)
            bass += target;
    }
    // A kick: the low bands jumping above where they've been.
    bass /= 4.0f;
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
    for (auto& c : cells)
        c *= 0.82f;  // trails
    for (int b = 0; b < bands; ++b)
    {
        const auto lit = static_cast<int> (std::round (level[static_cast<std::size_t> (b)] * static_cast<float> (rows)));
        for (int r = 0; r < lit && r < rows; ++r)
            cells[static_cast<std::size_t> (b * rows + r)] = 1.0f;
        // Peak dots: pushed up by the bar, fall with gravity.
        auto& p = peak[static_cast<std::size_t> (b)];
        auto& v = peakVelocity[static_cast<std::size_t> (b)];
        if (level[static_cast<std::size_t> (b)] >= p)
        {
            p = level[static_cast<std::size_t> (b)];
            v = 0;
        }
        else
        {
            v += 0.0025f;
            p = juce::jmax (0.0f, p - v);
        }
    }
    repaint();
}

void BeatScope::paint (juce::Graphics& g)
{
    const auto lit = hw::light();
    const auto glow = 1.0f + 0.45f * pulse;  // the beat
    const auto colW = static_cast<float> (getWidth()) / bands;
    const auto amber = hw::colour::amber;
    const auto bottom = static_cast<float> (getHeight());
    for (int b = 0; b < bands; ++b)
    {
        const auto x0 = static_cast<float> (b) * colW;
        const auto dotsAcross = juce::jmax (1, static_cast<int> ((colW - 2.0f) / pitch));
        for (int r = 0; r < rows; ++r)
        {
            const auto value = cells[static_cast<std::size_t> (b * rows + r)];
            const auto y = bottom - static_cast<float> (r + 1) * pitch;
            for (int d = 0; d < dotsAcross; ++d)
            {
                const auto x = x0 + 1.0f + static_cast<float> (d) * pitch;
                if (value < 0.03f)
                {
                    g.setColour (amber.withAlpha (0.05f));  // unlit pixels
                    g.fillRect (x, y, dotSize, dotSize);
                    continue;
                }
                const auto a = juce::jlimit (0.0f, 1.0f, value * lit * glow);
                // Halo, then the dot: brighter towards the top of a bar.
                g.setColour (amber.withAlpha (0.12f * a));
                g.fillRect (x - 1.0f, y - 1.0f, dotSize + 2.0f, dotSize + 2.0f);
                g.setColour (amber.interpolatedWith (juce::Colour (0xffffe2b0), 0.35f * static_cast<float> (r) / static_cast<float> (rows)).withAlpha (0.25f + 0.75f * a));
                g.fillRect (x, y, dotSize, dotSize);
            }
        }
        // The peak dot row.
        const auto pr = static_cast<int> (peak[static_cast<std::size_t> (b)] * static_cast<float> (rows));
        if (pr > 0 && pr <= rows)
        {
            g.setColour (juce::Colour (0xffffd9a0).withAlpha (0.9f * lit));
            for (int d = 0; d < dotsAcross; ++d)
                g.fillRect (x0 + 1.0f + static_cast<float> (d) * pitch, bottom - static_cast<float> (pr) * pitch - pitch, dotSize, dotSize);
        }
    }
}

} // namespace dss1::ui
