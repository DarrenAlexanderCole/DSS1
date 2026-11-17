#include "engine/Deck.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace dss1;
using namespace dss1::engine;
using Catch::Approx;

namespace
{
    constexpr double fileRate = 44100.0;
    constexpr double deviceRate = 48000.0;
    constexpr int block = 512;

    std::shared_ptr<const io::DecodedAudio> sine (double hz, double seconds)
    {
        auto audio = std::make_shared<io::DecodedAudio>();
        const auto n = static_cast<std::size_t> (seconds * fileRate);
        audio->info = { io::AudioFormat::wav, fileRate, 1, static_cast<Frame> (n), 0 };
        audio->channels.assign (1, std::vector<float> (n));
        for (std::size_t i = 0; i < n; ++i)
            audio->channels[0][i] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (i) / fileRate));
        return audio;
    }

    /// Renders `seconds` of stereo output and returns the left channel.
    std::vector<float> renderFor (Deck& deck, double seconds)
    {
        std::vector<float> left, l (block), r (block);
        float* outs[2] { l.data(), r.data() };
        for (int done = 0; done < static_cast<int> (seconds * deviceRate); done += block)
        {
            deck.render (outs, 2, block);
            left.insert (left.end(), l.begin(), l.end());
        }
        return left;
    }

    /// Frequency from zero crossings, ignoring the first 0.3 s (key lock's start-up).
    double frequencyOf (const std::vector<float>& x)
    {
        const auto skip = static_cast<std::size_t> (0.3 * deviceRate);
        int crossings = 0;
        std::size_t first = 0, last = 0;
        for (auto i = skip + 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                if (crossings == 0)
                    first = i;
                last = i;
                ++crossings;
            }
        return crossings > 1 ? (crossings - 1) * deviceRate / static_cast<double> (last - first) : 0.0;
    }
}

TEST_CASE ("A loaded deck is silent until played, then plays at the right pitch", "[deck]")
{
    Deck deck;
    deck.prepare (deviceRate, block);
    deck.load (sine (440.0, 10.0));
    deck.setKeyLock (false);

    const auto quiet = renderFor (deck, 0.1);
    CHECK (*std::max_element (quiet.begin(), quiet.end()) == 0.0f);
    CHECK (deck.position() == 0.0);

    deck.play();
    const auto out = renderFor (deck, 1.0);
    CHECK (frequencyOf (out) == Approx (440.0).epsilon (0.002));
    // One second at the device rate is one second of the track at its own rate.
    CHECK (deck.position() == Approx (fileRate * 1.0).margin (fileRate * 0.02));
}

TEST_CASE ("Without key lock, tempo changes pitch like a turntable", "[deck]")
{
    Deck deck;
    deck.prepare (deviceRate, block);
    deck.load (sine (440.0, 10.0));
    deck.setKeyLock (false);
    deck.setTempo (1.08);
    deck.play();
    CHECK (frequencyOf (renderFor (deck, 1.0)) == Approx (440.0 * 1.08).epsilon (0.003));
}

TEST_CASE ("With key lock, tempo changes speed but not pitch", "[deck]")
{
    Deck deck;
    deck.prepare (deviceRate, block);
    deck.load (sine (440.0, 10.0));
    deck.setKeyLock (true);
    deck.setTempo (1.08);
    deck.play();

    const auto out = renderFor (deck, 1.0);
    CHECK (frequencyOf (out) == Approx (440.0).epsilon (0.01));

    // Once running, each second of output moves 1.08 s through the track.
    const auto before = deck.position();
    renderFor (deck, 1.0);
    CHECK (deck.position() - before == Approx (fileRate * 1.08).margin (fileRate * 0.02));
}

TEST_CASE ("Seek, pause and the end of the track", "[deck]")
{
    Deck deck;
    deck.prepare (deviceRate, block);
    deck.load (sine (440.0, 2.0));
    deck.setKeyLock (false);

    deck.seek (static_cast<Frame> (1.5 * fileRate));
    CHECK (deck.position() == Approx (1.5 * fileRate));
    deck.play();
    renderFor (deck, 1.0);  // only 0.5 s left
    CHECK_FALSE (deck.isPlaying());

    deck.seek (0);
    deck.play();
    renderFor (deck, 0.2);
    deck.pause();
    const auto at = deck.position();
    const auto quiet = renderFor (deck, 0.2);
    CHECK (*std::max_element (quiet.begin(), quiet.end()) == 0.0f);
    CHECK (deck.position() == at);
}

TEST_CASE ("Loading a new track replaces the old one safely", "[deck]")
{
    Deck deck;
    deck.prepare (deviceRate, block);
    deck.setKeyLock (false);
    deck.load (sine (440.0, 5.0));
    deck.play();
    renderFor (deck, 0.2);

    deck.load (sine (880.0, 5.0));
    CHECK_FALSE (deck.isPlaying());
    CHECK (deck.position() == 0.0);
    deck.play();
    CHECK (frequencyOf (renderFor (deck, 1.0)) == Approx (880.0).epsilon (0.003));

    deck.unload();
    renderFor (deck, 0.1);
    CHECK (deck.length() == 0);
}
