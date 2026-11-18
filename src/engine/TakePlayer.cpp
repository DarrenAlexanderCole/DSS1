#include "engine/TakePlayer.h"

#include <algorithm>

namespace dss1::engine
{

void TakePlayer::setTake (std::shared_ptr<const io::DecodedAudio> take)
{
    playing.store (false);
    live.store (nullptr);
    // The audio thread may be inside a block that read the old pointer; it's done once two more blocks have run.
    const auto now = blocks.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now >= r.second + 2; }), retired.end());
    if (owned != nullptr)
        retired.emplace_back (std::move (owned), now);
    owned = std::move (take);
    position.store (0);
    live.store (owned.get());
}

void TakePlayer::play (double fraction)
{
    if (owned == nullptr || owned->channels.empty())
        return;
    const auto length = static_cast<double> (owned->channels.front().size());
    const auto at = std::clamp (fraction, 0.0, 1.0) * length;
    position.store (at >= length - 1 ? 0.0 : at);
    playing.store (true);
}

double TakePlayer::progress() const noexcept
{
    const auto* t = live.load();
    if (t == nullptr || t->channels.empty() || t->channels.front().empty())
        return 0;
    return position.load() / static_cast<double> (t->channels.front().size());
}

double TakePlayer::lengthSeconds() const noexcept
{
    if (owned == nullptr || owned->channels.empty())
        return 0;
    return static_cast<double> (owned->channels.front().size()) / std::max (1.0, owned->info.sampleRate);
}

void TakePlayer::render (float* const* outputs, int numChannels, int numFrames) noexcept
{
    blocks.fetch_add (1);
    const auto* t = live.load();
    if (! playing.load() || t == nullptr || t->channels.empty())
        return;
    const auto& ch = t->channels;
    const auto length = ch.front().size();
    const auto step = t->info.sampleRate / rate.load();
    auto pos = position.load();
    for (int i = 0; i < numFrames; ++i)
    {
        const auto p = static_cast<std::size_t> (pos);
        if (p + 1 >= length)
        {
            playing.store (false);
            pos = 0;
            break;
        }
        const auto frac = static_cast<float> (pos - static_cast<double> (p));
        for (int c = 0; c < std::min (numChannels, 2); ++c)
        {
            const auto& src = ch[std::min (static_cast<std::size_t> (c), ch.size() - 1)];
            outputs[c][i] += src[p] + (src[p + 1] - src[p]) * frac;
        }
        pos += step;
    }
    position.store (pos);
}

} // namespace dss1::engine
