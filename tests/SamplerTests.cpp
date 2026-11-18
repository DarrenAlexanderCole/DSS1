#include "engine/Sampler.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace dss1;
using engine::Sampler;

namespace
{
    std::shared_ptr<const io::DecodedAudio> clip (double rate, std::size_t frames, float value)
    {
        auto a = std::make_shared<io::DecodedAudio>();
        a->info.sampleRate = rate;
        a->info.channels = 2;
        a->info.lengthFrames = static_cast<decltype (a->info.lengthFrames)> (frames);
        a->channels.assign (2, std::vector<float> (frames, value));
        return a;
    }

    std::vector<float> render (Sampler& s, int frames)
    {
        std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (static_cast<std::size_t> (frames), 0.0f);
        float* out[2] { l.data(), r.data() };
        s.render (out, 2, frames);
        return l;
    }
}

TEST_CASE ("A pad plays its sample once, at the device rate", "[sampler]")
{
    Sampler s;
    s.prepare (48000.0);
    s.setSample (17, clip (24000.0, 2400, 0.5f));  // 0.1 s at 24 kHz → 4800 device frames
    CHECK (render (s, 512)[0] == 0.0f);             // silent until pressed
    s.trigger (17, 1.0f);
    const auto out = render (s, 6000);
    CHECK (out[0] == 0.0f);    // fades in over 3 ms …
    CHECK (out[300] == 0.5f);
    CHECK (out[4600] == 0.5f);
    CHECK (out[4790] < 0.1f);  // … and out
    CHECK (out[4900] == 0.0f);
    CHECK_FALSE (s.isPlaying (17));
}

TEST_CASE ("Pads mix, restart, stop, and empty slots do nothing", "[sampler]")
{
    Sampler s;
    s.prepare (44100.0);
    s.setSample (0, clip (44100.0, 44100, 0.25f));
    s.setSample (127, clip (44100.0, 44100, 0.5f));
    s.trigger (0);
    s.trigger (127);
    s.trigger (64);  // empty
    auto out = render (s, 200);
    CHECK (out[150] == 0.75f);
    CHECK (s.isPlaying (0));
    CHECK (s.isPlaying (127));

    s.stopAll();
    out = render (s, 200);
    CHECK (out[150] == 0.0f);

    // Replacing a sample while it plays is safe; the old one stays alive until it can't be playing.
    s.trigger (0);
    render (s, 200);
    s.setSample (0, clip (44100.0, 100, 0.1f));
    out = render (s, 100);
    CHECK (out[10] == 0.25f);
    s.collectGarbage();
    s.trigger (0);
    out = render (s, 50);
    CHECK (out[40] == 0.1f);
}

TEST_CASE ("Slices: pads share one track and play only their part of it", "[sampler]")
{
    auto track = std::make_shared<io::DecodedAudio>();
    track->info.sampleRate = 1000.0;
    track->channels.assign (2, std::vector<float> (4000));
    for (std::size_t i = 0; i < 4000; ++i)
        track->channels[0][i] = track->channels[1][i] = static_cast<float> (i / 1000) + 1.0f;  // 1, 2, 3, 4 per second

    Sampler s;
    s.prepare (1000.0);
    s.setSample (0, track, 1000, 2000);
    s.setSample (1, track, 2000, -1);
    s.trigger (0);
    auto out = render (s, 1200);
    CHECK (out[500] == 2.0f);
    CHECK (out[1100] == 0.0f);  // stopped at its end

    // Moving the end (a new slice marked after it) takes effect on the next press.
    s.setRegion (0, 1000, 1500);
    s.trigger (0);
    out = render (s, 1200);
    CHECK (out[400] == 2.0f);
    CHECK (out[600] == 0.0f);

    s.trigger (1);
    out = render (s, 2200);
    CHECK (out[500] == 3.0f);
    CHECK (out[1500] == 4.0f);
    CHECK (out[2100] == 0.0f);
}

#include "engine/TakePlayer.h"

TEST_CASE ("The take player plays a take from anywhere and stops at the end", "[sampler]")
{
    engine::TakePlayer t;
    t.prepare (1000.0);
    CHECK_FALSE (t.hasTake());
    t.play();
    CHECK_FALSE (t.isPlaying());

    auto take = std::make_shared<io::DecodedAudio>();
    take->info.sampleRate = 1000.0;
    take->channels.assign (2, std::vector<float> (1000, 0.5f));
    t.setTake (take);
    CHECK (t.lengthSeconds() == 1.0);
    t.play (0.5);
    std::vector<float> l (800), r (800);
    float* out[2] { l.data(), r.data() };
    t.render (out, 2, 800);
    CHECK (l[100] == 0.5f);
    CHECK (l[600] == 0.0f);
    CHECK_FALSE (t.isPlaying());

    t.setTake (nullptr);
    CHECK_FALSE (t.hasTake());
}
