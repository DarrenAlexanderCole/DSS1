#include "engine/Sampler.h"

#include <algorithm>
#include <cmath>

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
        padGain[i].store (1.0f);
        padRatio[i].store (1.0f);
        padAttack[i].store (0.0f);
        padFade[i].store (0.0f);
        padReverse[i].store (false);
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

void Sampler::setPadSettings (int slot, const PadSettings& p)
{
    if (slot < 0 || slot >= slots)
        return;
    const auto i = static_cast<std::size_t> (slot);
    padGain[i].store (std::pow (10.0f, std::clamp (p.gainDb, -60.0f, 24.0f) / 20.0f));
    padRatio[i].store (std::pow (2.0f, std::clamp (p.semitones, -36.0f, 36.0f) / 12.0f));
    padAttack[i].store (std::max (0.0f, p.attackMs) / 1000.0f);
    padFade[i].store (std::max (0.0f, p.fadeMs) / 1000.0f);
    padReverse[i].store (p.reverse);
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

        const auto i = static_cast<std::size_t> (slot);
        const auto sr = audio->info.sampleRate;
        const auto ratio = static_cast<double> (padRatio[i].load());
        auto& v = *chosen;
        v = {};
        v.audio = audio;
        v.slot = slot;
        v.start = from;
        v.end = to;
        v.direction = padReverse[i].load() ? -1 : 1;
        v.position = v.direction > 0 ? from : to - 1.001;
        v.origin = v.position;
        v.step = sr / rate.load() * ratio;
        // When it cuts another pad, it fades in exactly as fast as that one fades out: an equal crossfade, so the
        // two never add up louder than one. Fades are in source frames, so tuning scales them with the sound.
        const auto attackSeconds_ = std::max (static_cast<double> (padAttack[i].load()), (cut ? cutSeconds : attackSeconds));
        v.attack = std::max (1.0, std::min (sr * ratio * attackSeconds_, (to - from) / 4));
        v.fadeEnd = std::max (1.0, std::min (sr * ratio * std::max (static_cast<double> (padFade[i].load()), attackSeconds), (to - from) / 4));
        v.gain = c.gain * padGain[i].load();
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
                const auto remaining = v.direction > 0 ? v.end - 1 - v.position : v.position - v.start;
                if (v.position < 0 || remaining <= 0 || v.level <= 0.0f)
                {
                    v = {};
                    break;
                }
                const auto p = static_cast<std::size_t> (v.position);
                if (p + 1 >= length)
                {
                    v = {};
                    break;
                }
                const auto frac = static_cast<float> (v.position - static_cast<double> (p));
                // Fade in from where it started and out towards where it stops, and out when cut or released.
                const auto elapsed = std::abs (v.position - v.origin);
                const auto g = v.gain * v.level * static_cast<float> (std::min (1.0, elapsed / v.attack) * std::min (1.0, remaining / v.fadeEnd));
                for (int c = 0; c < std::min (numChannels, 2); ++c)
                {
                    const auto& src = ch[std::min (static_cast<std::size_t> (c), ch.size() - 1)];
                    outputs[c][i] += (src[p] + (src[p + 1] - src[p]) * frac) * g;
                }
                v.position += v.step * v.direction;
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
