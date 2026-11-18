#include "engine/Sampler.h"

#include <algorithm>

namespace dss1::engine
{

Sampler::Sampler()
{
    for (auto& s : live)
        s.store (nullptr);
    for (auto& p : playing)
        p.store (false);
}

Sampler::~Sampler() = default;

void Sampler::setSample (int slot, std::shared_ptr<const io::DecodedAudio> audio)
{
    if (slot < 0 || slot >= slots)
        return;
    const auto i = static_cast<std::size_t> (slot);
    if (owned[i])
    {
        // A voice may still be reading the old sample: keep it until it would have finished from the start.
        const auto length = owned[i]->channels.empty() ? 0 : owned[i]->channels.front().size();
        const auto deviceFrames = static_cast<std::uint64_t> (static_cast<double> (length) / std::max (1.0, owned[i]->info.sampleRate) * rate.load()) + 1;
        retired.push_back ({ owned[i], framesRendered.load() + deviceFrames + static_cast<std::uint64_t> (rate.load()) });
    }
    owned[i] = std::move (audio);
    live[i].store (owned[i].get());
}

std::shared_ptr<const io::DecodedAudio> Sampler::sample (int slot) const
{
    return slot >= 0 && slot < slots ? owned[static_cast<std::size_t> (slot)] : nullptr;
}

void Sampler::trigger (int slot, float gain)
{
    if (slot < 0 || slot >= slots)
        return;
    const auto g = std::clamp (static_cast<int> (gain * 255.0f), 0, 255);
    const auto w = queueWrite.load();
    if ((w + 1) % queueSize == queueRead.load())
        return;  // full: drop the press rather than block
    queue[static_cast<std::size_t> (w)].store (slot << 8 | g);
    queueWrite.store ((w + 1) % queueSize);
}

void Sampler::stopAll()
{
    const auto w = queueWrite.load();
    if ((w + 1) % queueSize == queueRead.load())
        return;
    queue[static_cast<std::size_t> (w)].store (-1);
    queueWrite.store ((w + 1) % queueSize);
}

void Sampler::collectGarbage()
{
    const auto now = framesRendered.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) { return r.freeAfterFrame <= now; }), retired.end());
}

bool Sampler::isPlaying (int slot) const noexcept
{
    return slot >= 0 && slot < slots && playing[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed);
}

void Sampler::prepare (double deviceRate)
{
    rate.store (deviceRate > 0 ? deviceRate : 44100.0);
    for (auto& v : voices)
        v = {};
}

void Sampler::render (float* const* outputs, int numChannels, int numFrames) noexcept
{
    // Take pad presses.
    for (auto r = queueRead.load(); r != queueWrite.load(); r = (r + 1) % queueSize)
    {
        const auto message = queue[static_cast<std::size_t> (r)].load();
        queueRead.store ((r + 1) % queueSize);
        if (message < 0)
        {
            for (auto& v : voices)
                v = {};
            continue;
        }
        const auto slot = message >> 8;
        const auto* audio = live[static_cast<std::size_t> (slot)].load();
        if (audio == nullptr || audio->channels.empty() || audio->channels.front().empty())
            continue;
        // Restart the pad if it's playing; otherwise take a free voice, or the oldest.
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.slot == slot)
                chosen = &v;
        if (chosen == nullptr)
            for (auto& v : voices)
                if (v.audio == nullptr)
                {
                    chosen = &v;
                    break;
                }
        if (chosen == nullptr)
            chosen = &*std::max_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.position < b.position; });
        chosen->audio = audio;
        chosen->slot = slot;
        chosen->position = 0;
        chosen->step = audio->info.sampleRate / rate.load();
        chosen->gain = static_cast<float> (message & 255) / 255.0f;
    }

    std::array<bool, slots> nowPlaying {};
    for (auto& v : voices)
    {
        if (v.audio == nullptr)
            continue;
        const auto& ch = v.audio->channels;
        const auto length = ch.front().size();
        for (int i = 0; i < numFrames; ++i)
        {
            const auto p = static_cast<std::size_t> (v.position);
            if (p + 1 >= length)
            {
                v = {};
                break;
            }
            const auto frac = static_cast<float> (v.position - static_cast<double> (p));
            for (int c = 0; c < std::min (numChannels, 2); ++c)
            {
                const auto& src = ch[std::min (static_cast<std::size_t> (c), ch.size() - 1)];
                outputs[c][i] += (src[p] + (src[p + 1] - src[p]) * frac) * v.gain;
            }
            v.position += v.step;
        }
        if (v.audio != nullptr)
            nowPlaying[static_cast<std::size_t> (v.slot)] = true;
    }
    for (std::size_t s = 0; s < static_cast<std::size_t> (slots); ++s)
        playing[s].store (nowPlaying[s], std::memory_order_relaxed);
    framesRendered.fetch_add (static_cast<std::uint64_t> (numFrames));
}

} // namespace dss1::engine
