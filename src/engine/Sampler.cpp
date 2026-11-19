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
    for (std::size_t i = 0; i < static_cast<std::size_t> (slots); ++i)
    {
        regionStart[i].store (0);
        regionEnd[i].store (-1);
    }
    for (std::size_t v = 0; v < static_cast<std::size_t> (maxVoices); ++v)
    {
        shownSlot[v].store (-1);
        shownPosition[v].store (0);
    }
}

Sampler::~Sampler() = default;

void Sampler::setRegion (int slot, std::int64_t start, std::int64_t end)
{
    if (slot < 0 || slot >= slots)
        return;
    regionStart[static_cast<std::size_t> (slot)].store (std::max<std::int64_t> (0, start));
    regionEnd[static_cast<std::size_t> (slot)].store (end);
}

void Sampler::setSample (int slot, std::shared_ptr<const io::DecodedAudio> audio, std::int64_t start, std::int64_t end)
{
    if (slot < 0 || slot >= slots)
        return;
    const auto i = static_cast<std::size_t> (slot);
    if (owned[i] && owned[i] != audio)
    {
        // A voice may still be reading the old sample: keep it until it would have finished from the start.
        auto length = static_cast<std::int64_t> (owned[i]->channels.empty() ? 0 : owned[i]->channels.front().size());
        if (const auto e = regionEnd[i].load(); e > 0)
            length = std::min (length, e - regionStart[i].load());
        const auto deviceFrames = static_cast<std::uint64_t> (static_cast<double> (std::max<std::int64_t> (0, length)) / std::max (1.0, owned[i]->info.sampleRate) * rate.load()) + 1;
        retired.push_back ({ owned[i], framesRendered.load() + deviceFrames + static_cast<std::uint64_t> (rate.load()) });
    }
    live[i].store (nullptr);
    setRegion (slot, start, end);
    owned[i] = std::move (audio);
    live[i].store (owned[i].get());
}

std::shared_ptr<const io::DecodedAudio> Sampler::sample (int slot) const
{
    return slot >= 0 && slot < slots ? owned[static_cast<std::size_t> (slot)] : nullptr;
}

void Sampler::push (Command c) noexcept
{
    const auto w = queueWrite.load();
    if ((w + 1) % queueSize == queueRead.load())
        return;  // full: drop it rather than block
    queue[static_cast<std::size_t> (w)] = c;
    queueWrite.store ((w + 1) % queueSize);
}

void Sampler::trigger (int slot, float gain)
{
    if (slot >= 0 && slot < slots)
        push ({ Command::trigger, static_cast<std::int16_t> (slot), std::clamp (gain, 0.0f, 1.0f) });
}

void Sampler::release (int slot)
{
    if (slot >= 0 && slot < slots)
        push ({ Command::release, static_cast<std::int16_t> (slot), 0 });
}

void Sampler::stopAll()
{
    push ({ Command::stopAll, 0, 0 });
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

std::pair<int, double> Sampler::voicePosition (int v) const noexcept
{
    if (v < 0 || v >= maxVoices)
        return { -1, 0 };
    return { shownSlot[static_cast<std::size_t> (v)].load (std::memory_order_relaxed), shownPosition[static_cast<std::size_t> (v)].load (std::memory_order_relaxed) };
}

void Sampler::prepare (double deviceRate)
{
    rate.store (deviceRate > 0 ? deviceRate : 44100.0);
    for (auto& v : voices)
        v = {};
}

void Sampler::fadeOut (Voice& v, double seconds) noexcept
{
    if (v.audio == nullptr)
        return;
    const auto step = static_cast<float> (1.0 / std::max (1.0, seconds * rate.load()));
    v.fadeStep = std::max (v.fadeStep, step);  // a faster fade wins
}

void Sampler::render (float* const* outputs, int numChannels, int numFrames) noexcept
{
    for (auto r = queueRead.load(); r != queueWrite.load(); r = (r + 1) % queueSize)
    {
        const auto c = queue[static_cast<std::size_t> (r)];
        queueRead.store ((r + 1) % queueSize);

        if (c.type == Command::stopAll)
        {
            for (auto& v : voices)
                fadeOut (v, cutSeconds);
            continue;
        }
        if (c.type == Command::release)
        {
            for (auto& v : voices)
                if (v.slot == c.slot && v.fadeStep == 0)
                    fadeOut (v, releaseSeconds);
            continue;
        }

        const auto slot = static_cast<int> (c.slot);
        const auto* audio = live[static_cast<std::size_t> (slot)].load();
        if (audio == nullptr || audio->channels.empty() || audio->channels.front().empty())
            continue;
        const auto length = static_cast<double> (audio->channels.front().size());
        const auto from = std::min (static_cast<double> (regionStart[static_cast<std::size_t> (slot)].load()), length);
        const auto e = regionEnd[static_cast<std::size_t> (slot)].load();
        const auto to = e > 0 ? std::clamp (static_cast<double> (e), from, length) : length;
        if (to - from < 2)
            continue;

        // Cut what this pad is playing (and, with choke, the rest of its bank) with a short fade.
        const bool chokeBank = choke.load();
        bool cut = false;
        for (auto& v : voices)
            if (v.audio != nullptr && v.fadeStep == 0 && (v.slot == slot || (chokeBank && v.slot / padsPerBank == slot / padsPerBank)))
            {
                fadeOut (v, cutSeconds);
                cut = true;
            }

        // A free voice; failing that, the quietest fading one; failing that, the oldest.
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.audio == nullptr)
            {
                chosen = &v;
                break;
            }
        if (chosen == nullptr)
            for (auto& v : voices)
                if (v.fadeStep > 0 && (chosen == nullptr || v.level < chosen->level))
                    chosen = &v;
        if (chosen == nullptr)
            chosen = &*std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.age < b.age; });

        auto& v = *chosen;
        v = {};
        v.audio = audio;
        v.slot = slot;
        v.start = from;
        v.end = to;
        v.position = from;
        v.step = audio->info.sampleRate / rate.load();
        // When it cuts another pad, it fades in exactly as fast as that one fades out: an equal crossfade, so the
        // two never add up louder than one.
        v.edgeFade = std::max (1.0, std::min (audio->info.sampleRate * (cut ? cutSeconds : attackSeconds), (to - from) / 4));
        v.gain = c.gain;
        v.age = ++voiceCounter;
    }

    std::array<bool, slots> nowPlaying {};
    for (std::size_t vi = 0; vi < voices.size(); ++vi)
    {
        auto& v = voices[vi];
        if (v.audio != nullptr)
        {
            const auto& ch = v.audio->channels;
            const auto length = ch.front().size();
            for (int i = 0; i < numFrames; ++i)
            {
                const auto p = static_cast<std::size_t> (v.position);
                if (p + 1 >= length || v.position >= v.end - 1 || v.level <= 0.0f)
                {
                    v = {};
                    break;
                }
                const auto frac = static_cast<float> (v.position - static_cast<double> (p));
                // Fade in at the slice start and out at its end (edge), and out when cut or released (level).
                const auto edge = std::min (v.position - v.start, v.end - 1 - v.position);
                const auto g = v.gain * v.level * static_cast<float> (std::min (1.0, edge / v.edgeFade));
                for (int c = 0; c < std::min (numChannels, 2); ++c)
                {
                    const auto& src = ch[std::min (static_cast<std::size_t> (c), ch.size() - 1)];
                    outputs[c][i] += (src[p] + (src[p + 1] - src[p]) * frac) * g;
                }
                v.position += v.step;
                v.level -= v.fadeStep;
            }
        }
        if (v.audio != nullptr && v.fadeStep == 0)
            nowPlaying[static_cast<std::size_t> (v.slot)] = true;
        shownSlot[vi].store (v.audio != nullptr && v.fadeStep == 0 ? v.slot : -1, std::memory_order_relaxed);
        shownPosition[vi].store (v.position, std::memory_order_relaxed);
    }
    for (std::size_t s = 0; s < static_cast<std::size_t> (slots); ++s)
        playing[s].store (nowPlaying[s], std::memory_order_relaxed);
    framesRendered.fetch_add (static_cast<std::uint64_t> (numFrames));
}

} // namespace dss1::engine
