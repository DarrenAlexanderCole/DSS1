#include "TempoSteps.h"
#include "Hardware.h"

#include <cmath>

namespace dss1::ui
{

TempoSteps::TempoSteps (Session& s, int n) : session (s), steps (juce::jmax (1, n))
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
    startTimerHz (60);
}

void TempoSteps::timerCallback()
{
    if (isShowing())
        repaint();
}

std::optional<double> TempoSteps::beatNow() const
{
    const auto* r = session.record();
    if (r == nullptr || session.state() != Session::State::loaded)
        return std::nullopt;
    const auto at = session.deck().position();
    if (r->grid && ! r->grid->isEmpty())
        return r->grid->beatAt (at);
    if (r->info.bpm && r->sampleRate > 0)
        return at / r->sampleRate * *r->info.bpm / 60.0;
    return std::nullopt;
}

void TempoSteps::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto gap = juce::jmax (2.0f, b.getHeight() * 0.3f);
    const auto size = juce::jmin (b.getHeight(), (b.getWidth() - gap * static_cast<float> (steps - 1)) / static_cast<float> (steps));
    const auto total = size * static_cast<float> (steps) + gap * static_cast<float> (steps - 1);
    const auto x0 = b.getX() + (b.getWidth() - total) * 0.5f;
    const auto y0 = b.getCentreY() - size * 0.5f;
    const auto lit = hw::light();
    const auto amber = hw::colour::amber;
    const auto hot = juce::Colour (0xfffff0d8);

    // Where in the bar we are, in steps (4 beats a bar).
    const auto beat = beatNow();
    double pos = -1;
    if (beat)
    {
        const auto perBar = static_cast<double> (steps);
        pos = std::fmod (*beat * perBar / 4.0, perBar);
        if (pos < 0)
            pos += perBar;
    }
    const auto current = pos >= 0 ? static_cast<int> (pos) : -1;
    const bool running = session.deck().isPlaying();

    for (int i = 0; i < steps; ++i)
    {
        const auto r = juce::Rectangle<float> (x0 + static_cast<float> (i) * (size + gap), y0, size, size);
        const bool onBeat = steps > 4 && i % (steps / 4) == 0;
        // How long since this step lit, in steps: the trail behind the running light.
        float glow = 0;
        if (current >= 0)
        {
            auto since = pos - static_cast<double> (i);
            if (since < 0)
                since += steps;
            glow = i == current ? 1.0f : (running ? static_cast<float> (std::exp (-(since - 1.0) * 1.1)) * 0.55f : 0.0f);
            if (! running && i == current)
                glow = 0.7f;
        }
        // The unlit cell, with the beats marked a little brighter.
        g.setColour (amber.withAlpha ((onBeat ? 0.16f : 0.08f) * lit));
        g.fillRoundedRectangle (r, size * 0.18f);
        g.setColour (amber.withAlpha ((onBeat ? 0.32f : 0.16f) * lit));
        g.drawRoundedRectangle (r.reduced (0.5f), size * 0.18f, 1.0f);
        if (glow <= 0.01f)
            continue;
        // The lit cell: a soft bloom, then the square, white-hot at the centre on the step itself.
        const auto a = glow * lit;
        g.setColour (amber.withAlpha (0.18f * a));
        g.fillRoundedRectangle (r.expanded (size * 0.35f), size * 0.4f);
        juce::ColourGradient face (amber.interpolatedWith (hot, i == current ? 0.6f : 0.15f).withAlpha (a), r.getCentreX(), r.getCentreY(),
                                   amber.withAlpha (0.85f * a), r.getX(), r.getY(), true);
        g.setGradientFill (face);
        g.fillRoundedRectangle (r, size * 0.18f);
    }
}

} // namespace dss1::ui
