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
    CHECK (out[0] == 0.5f);
    CHECK (out[4700] == 0.5f);
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
    auto out = render (s, 100);
    CHECK (out[50] == 0.75f);
    CHECK (s.isPlaying (0));
    CHECK (s.isPlaying (127));

    s.stopAll();
    out = render (s, 100);
    CHECK (out[50] == 0.0f);

    // Replacing a sample while it plays is safe; the old one stays alive until it can't be playing.
    s.trigger (0);
    render (s, 100);
    s.setSample (0, clip (44100.0, 100, 0.1f));
    out = render (s, 100);
    CHECK (out[10] == 0.25f);
    s.collectGarbage();
    s.trigger (0);
    out = render (s, 50);
    CHECK (out[10] == 0.1f);
}
