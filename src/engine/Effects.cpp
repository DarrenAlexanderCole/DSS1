#include "engine/Effects.h"

#include <algorithm>
#include <cmath>

namespace dss1::engine
{

namespace
{
    constexpr float pi = 3.14159265358979f;

    // Freeverb's tunings (at 44.1 kHz), the right channel offset by 23 samples for width.
    constexpr int combTunings[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    constexpr int allpassTunings[4] { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    /// One TPT state-variable filter step; returns {low, high}.
    inline std::pair<float, float> svf (float& ic1, float& ic2, float x, float g, float k) noexcept
    {
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        const auto low = v2;
        const auto band = v1;
        const auto high = x - k * band - low;
        return { low, high };
    }
}

void Effects::prepare (double sampleRate, int)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    const auto scale = rate / 44100.0;
    for (auto& line : echoLine)
        line.assign (static_cast<std::size_t> (rate * 6.0) + 1, 0.0f);  // up to 6 s of delay
    for (std::size_t c = 0; c < 2; ++c)
    {
        for (std::size_t i = 0; i < 8; ++i)
            combs[c][i].buffer.assign (static_cast<std::size_t> ((combTunings[i] + static_cast<int> (c) * stereoSpread) * scale) + 1, 0.0f);
        for (std::size_t i = 0; i < 4; ++i)
            allpasses[c][i].buffer.assign (static_cast<std::size_t> ((allpassTunings[i] + static_cast<int> (c) * stereoSpread) * scale) + 1, 0.0f);
    }
    // Smoothing: about 20 ms for levels and the filter, 150 ms for delay time (a tape-like glide).
    const auto per = [this] (double seconds) { return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * rate))); };
    for (auto* s : { &filterAmount, &filterWet, &echoWet, &echoFb, &reverbWet, &reverbRoom, &crushWet, &compWet, &gateMix, &shimmerMix, &freezeMix })
        s->step = per (0.02);
    for (auto& line : shimmerLine)
        line.assign (static_cast<std::size_t> (rate * 0.1) + 2, 0.0f);  // a 100 ms window for the octave shifter
    echoTime.step = per (0.15);
    echoTime.value = echoTime.target = static_cast<float> (60.0 / 120.0 * 0.75 * rate);
    reset();
}

void Effects::updateEq()
{
    // RBJ cookbook shelves and bell, recomputed only when a band moves.
    const float want[3] { std::clamp (eqLowDb.load (std::memory_order_relaxed), -12.0f, 12.0f), std::clamp (eqMidDb.load (std::memory_order_relaxed), -12.0f, 12.0f),
                          std::clamp (eqHighDb.load (std::memory_order_relaxed), -12.0f, 12.0f) };
    const float freq[3] { 120.0f, 1000.0f, 6000.0f };
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (std::abs (want[i] - eqSet[i]) < 0.01f)
            continue;
        eqSet[i] = want[i];
        const auto A = std::pow (10.0f, want[i] / 40.0f);
        const auto w0 = 2.0f * pi * freq[i] / static_cast<float> (rate);
        const auto cw = std::cos (w0), sw = std::sin (w0);
        float b0, b1, b2, a0, a1, a2;
        if (i == 1)
        {
            const auto alpha = sw / (2.0f * 0.8f);
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        }
        else
        {
            const auto alpha = sw / 2.0f * std::sqrt (2.0f);  // shelf slope 1
            const auto sq = 2.0f * std::sqrt (A) * alpha;
            if (i == 0)
            {
                b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sq);
                a0 = (A + 1) + (A - 1) * cw + sq; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq;
            }
            else
            {
                b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
                a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq;
            }
        }
        auto& band = eq[i];
        band.b0 = b0 / a0; band.b1 = b1 / a0; band.b2 = b2 / a0; band.a1 = a1 / a0; band.a2 = a2 / a0;
    }
}

void Effects::reset() noexcept
{
    for (auto& f : lowpass)
        f.reset();
    for (auto& f : highpass)
        f.reset();
    for (auto& line : echoLine)
        std::fill (line.begin(), line.end(), 0.0f);
    echoTone = {};
    for (auto& side : combs)
        for (auto& c : side)
        {
            std::fill (c.buffer.begin(), c.buffer.end(), 0.0f);
            c.store = 0;
        }
    for (auto& side : allpasses)
        for (auto& a : side)
            std::fill (a.buffer.begin(), a.buffer.end(), 0.0f);
    crushHeld = {};
    crushCounter = 0;
    driveX = driveY = {};
    punchFast = punchSlow = duckEnv = gateEnv = gateGain = 0;
    gateHold = 0;
    for (auto& line : shimmerLine)
        std::fill (line.begin(), line.end(), 0.0f);
    shimmerBack = {};
    compEnvelope = compRms = compSlow = 0;
    for (auto& band : eq)
        band.z1 = band.z2 = {};
    eqSet = { 999.0f, 999.0f, 999.0f };
    sidechainState = {};
    gainReduction.store (0.0f);
}

void Effects::process (float* const* channels, int numChannels, int numFrames) noexcept
{
    if (numChannels <= 0 || numFrames <= 0 || echoLine[0].empty())
        return;
    const auto stereo = numChannels >= 2;
    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    // Targets for this block.
    filterAmount.target = std::clamp (filter.load (std::memory_order_relaxed), -1.0f, 1.0f);
    filterWet.target = filterOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const auto beats = std::clamp (echoBeats.load (std::memory_order_relaxed), 0.0625f, 4.0f);
    const auto tempo = std::clamp (bpm.load (std::memory_order_relaxed), 40.0f, 300.0f);
    echoTime.target = std::min (static_cast<float> (beats * 60.0 / tempo * rate), static_cast<float> (echoLine[0].size() - 2));
    echoWet.target = echoOn.load (std::memory_order_relaxed) ? std::clamp (echoMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    echoFb.target = std::clamp (echoFeedback.load (std::memory_order_relaxed), 0.0f, 0.95f);
    reverbWet.target = reverbOn.load (std::memory_order_relaxed) ? std::clamp (reverbMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    reverbRoom.target = 0.7f + 0.28f * std::clamp (reverbSize.load (std::memory_order_relaxed), 0.0f, 1.0f);
    // CRUSH lives in the COMP bay, so it works while COMP is on.
    crushWet.target = crushOn.load (std::memory_order_relaxed) && compOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    const bool drive = compDrive.load (std::memory_order_relaxed), punch = compPunch.load (std::memory_order_relaxed);
    const bool squash = compSquash.load (std::memory_order_relaxed);
    const bool pingPong = echoPingPong.load (std::memory_order_relaxed), tape = echoTape.load (std::memory_order_relaxed);
    const bool duck = echoDuck.load (std::memory_order_relaxed);
    freezeMix.target = echoFreeze.load (std::memory_order_relaxed) && echoOn.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    gateMix.target = reverbGated.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    shimmerMix.target = reverbShimmer.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    // A tape echo's feedback can pass unity (it saturates instead of exploding), for the dub howl.
    if (tape)
        echoFb.target = std::clamp (echoFeedback.load (std::memory_order_relaxed), 0.0f, 0.95f) * 1.12f;
    const auto levels = std::pow (2.0f, 11.0f);  // 12 bit
    const int hold = 3;                           // a third of the rate

    // Compressor settings for this block, by type.
    compWet.target = compOn.load (std::memory_order_relaxed) ? std::clamp (compMix.load (std::memory_order_relaxed), 0.0f, 1.0f) : 0.0f;
    const auto type = static_cast<CompType> (std::clamp (compType.load (std::memory_order_relaxed), 0, 3));
    const bool fastA = compFastAttack.load (std::memory_order_relaxed), fastR = compFastRelease.load (std::memory_order_relaxed);
    const bool high = compHighRatio.load (std::memory_order_relaxed);
    struct Character
    {
        float attackMs, releaseMs, ratio, kneeDb, drive;
    };
    Character ch {};
    switch (type)
    {
        case CompType::opto:
            ch = { std::clamp (compAttackMs.load (std::memory_order_relaxed), 0.5f, 300.0f), std::clamp (compReleaseMs.load (std::memory_order_relaxed), 20.0f, 5000.0f),
                   high ? 6.0f : 3.0f, 10.0f, 0.0f };
            break;
        case CompType::vca:  ch = { fastA ? 0.3f : 3.0f, fastR ? 40.0f : 120.0f, high ? 10.0f : 4.0f, 2.0f, 0.0f }; break;
        case CompType::tube: ch = { fastA ? 8.0f : 20.0f, fastR ? 150.0f : 400.0f, high ? 4.0f : 2.0f, 18.0f, 0.5f }; break;
        case CompType::fet:  ch = { fastA ? 0.05f : 0.8f, fastR ? 30.0f : 80.0f, high ? 20.0f : 4.0f, 1.0f, 0.25f }; break;
    }
    if (squash)
        ch = { 0.1f, 50.0f, 20.0f, 1.0f, ch.drive };
    const auto thresholdDb = -36.0f * std::clamp (compAmount.load (std::memory_order_relaxed), 0.0f, 1.0f) - (squash ? 12.0f : 0.0f);
    const auto punchFastAtt = std::exp (-1.0f / (0.0005f * static_cast<float> (rate))), punchFastRel = std::exp (-1.0f / (0.02f * static_cast<float> (rate)));
    const auto punchSlowAtt = std::exp (-1.0f / (0.025f * static_cast<float> (rate))), punchSlowRel = std::exp (-1.0f / (0.25f * static_cast<float> (rate)));
    const auto attackCoeff = std::exp (-1.0f / (0.001f * ch.attackMs * static_cast<float> (rate)));
    const auto releaseCoeff = std::exp (-1.0f / (0.001f * ch.releaseMs * static_cast<float> (rate)));
    const auto rmsCoeff = std::exp (-1.0f / (0.010f * static_cast<float> (rate)));
    const auto hpfCoeff = std::exp (-2.0f * pi * 100.0f / static_cast<float> (rate));
    const bool hpf = compSidechainHpf.load (std::memory_order_relaxed);
    // Make-up gain: about half of what the threshold and ratio take away at full scale.
    const auto makeupDb = -thresholdDb * (1.0f - 1.0f / ch.ratio) * 0.5f;
    float deepest = 0;
    updateEq();

    const auto lineSize = echoLine[0].size();
    const auto nyquistGuard = static_cast<float> (rate * 0.45);

    for (int i = 0; i < numFrames; ++i)
    {
        float in[2] { left[i], right[i] };
        float out[2] { in[0], in[1] };

        // --- Filter: low-pass below centre, high-pass above. Off fades out through filterWet.
        const auto amount = filterAmount.next();
        const auto fWet = filterWet.next();
        if (fWet > 1e-4f)
        {
            const auto closeness = std::abs (amount);
            float cutoff;
            if (amount < 0)
                cutoff = 20000.0f * std::pow (60.0f / 20000.0f, closeness);  // 20 kHz → 60 Hz
            else
                cutoff = 20.0f * std::pow (9000.0f / 20.0f, closeness);      // 20 Hz → 9 kHz
            cutoff = std::min (cutoff, nyquistGuard);
            const auto g = std::tan (pi * cutoff / static_cast<float> (rate));
            const auto k = 2.0f - 1.3f * closeness;  // a little resonance as it closes
            for (std::size_t c = 0; c < 2; ++c)
            {
                const auto lp = svf (lowpass[c].ic1, lowpass[c].ic2, out[c], g, k).first;
                const auto hp = svf (highpass[c].ic1, highpass[c].ic2, out[c], g, k).second;
                const auto filtered = amount < 0 ? lp : hp;
                out[c] += (filtered - out[c]) * fWet;
            }
        }

        // --- Crush switch: hold samples (lower rate), then quantise (fewer bits).
        const auto cWet = crushWet.next();
        if (cWet > 1e-4f)
        {
            if (crushCounter == 0)
                for (std::size_t c = 0; c < 2; ++c)
                    crushHeld[c] = std::round (out[c] * levels) / levels;
            crushCounter = (crushCounter + 1) % hold;
            for (std::size_t c = 0; c < 2; ++c)
                out[c] += (crushHeld[c] - out[c]) * cWet;
        }

        // --- Compressor: a stereo-linked detector, a soft-knee gain computer, attack/release in dB, make-up,
        // and each type's colour.
        const auto kWet = compWet.next();
        if (kWet > 1e-4f)
        {
            // The EQ first, then the compressor hears (and squeezes) the EQ'd sound.
            float eqd[2] { out[0], out[1] };
            for (std::size_t c = 0; c < 2; ++c)
                for (auto& band : eq)
                    eqd[c] = band.process (eqd[c], c);
            if (drive)
                for (std::size_t c = 0; c < 2; ++c)
                {
                    // +18 dB into an asymmetric saturator (even and odd harmonics), back down, and the DC the
                    // asymmetry makes taken out again.
                    const auto shaped = (std::tanh (eqd[c] * 8.0f + 0.25f) - std::tanh (0.25f)) * 0.32f;
                    driveY[c] = shaped - driveX[c] + 0.9985f * driveY[c];
                    driveX[c] = shaped;
                    eqd[c] = driveY[c];
                }
            float detect[2] { eqd[0], eqd[1] };
            if (hpf)  // ignore the low end, so the kick doesn't pump everything
                for (std::size_t c = 0; c < 2; ++c)
                {
                    sidechainState[c] = hpfCoeff * sidechainState[c] + (1.0f - hpfCoeff) * detect[c];
                    detect[c] -= sidechainState[c];
                }
            auto level = std::max (std::abs (detect[0]), std::abs (detect[1]));
            if (type == CompType::vca)
            {
                compRms = rmsCoeff * compRms + (1.0f - rmsCoeff) * level * level;
                level = std::sqrt (compRms) * 1.414f;
            }
            const auto levelDb = 20.0f * std::log10 (std::max (level, 1e-6f));
            auto ratio = ch.ratio;
            if (type == CompType::tube)  // vari-mu: the ratio rises the harder it's pushed
                ratio = std::min (ch.ratio * 3.0f, ch.ratio + std::max (0.0f, levelDb - thresholdDb) * 0.15f);
            const auto over = levelDb - thresholdDb;
            float target = 0;
            if (2.0f * over < -ch.kneeDb)
                target = 0;
            else if (2.0f * std::abs (over) <= ch.kneeDb)
                target = (1.0f / ratio - 1.0f) * (over + ch.kneeDb / 2.0f) * (over + ch.kneeDb / 2.0f) / (2.0f * std::max (ch.kneeDb, 0.01f));
            else
                target = (1.0f / ratio - 1.0f) * over;
            auto release = releaseCoeff;
            if (type == CompType::opto)
            {
                // The light cell: the longer it's been squeezing, the slower it lets go.
                compSlow = 0.9999f * compSlow + 0.0001f * std::min (0.0f, compEnvelope);
                release = std::pow (releaseCoeff, 1.0f / (1.0f + std::abs (compSlow) * 0.4f));
            }
            compEnvelope = target < compEnvelope ? attackCoeff * compEnvelope + (1.0f - attackCoeff) * target
                                                 : release * compEnvelope + (1.0f - release) * target;
            deepest = std::min (deepest, compEnvelope);
            const auto gain = std::pow (10.0f, (compEnvelope + makeupDb) / 20.0f);
            // PUNCH: how far the fast envelope jumps above the slow one is a hit; lift it (up to +12 dB).
            float punchGain = 1.0f;
            if (punch)
            {
                const auto l = std::max (std::abs (eqd[0]), std::abs (eqd[1])) * gain;
                punchFast = l > punchFast ? punchFastAtt * punchFast + (1.0f - punchFastAtt) * l : punchFastRel * punchFast + (1.0f - punchFastRel) * l;
                punchSlow = l > punchSlow ? punchSlowAtt * punchSlow + (1.0f - punchSlowAtt) * l : punchSlowRel * punchSlow + (1.0f - punchSlowRel) * l;
                punchGain = std::clamp (std::pow ((punchFast + 1e-5f) / (punchSlow + 1e-5f), 1.4f), 0.7f, 4.0f);
            }
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto squeezed = eqd[c] * gain;
                if (punch)
                    squeezed = std::tanh (squeezed * punchGain * 0.9f) / 0.9f;  // soft ceiling so the hits don't clip
                if (squash)
                    squeezed = std::tanh (squeezed * 1.4f) / 1.4f;
                if (ch.drive > 0)
                {
                    // Tube: warm, even harmonics (a little asymmetry). FET: a touch of odd-harmonic bite.
                    const auto d = 1.0f + ch.drive * 2.0f;
                    squeezed = type == CompType::tube ? (std::tanh (d * squeezed + 0.1f) - std::tanh (0.1f)) / d
                                                      : std::tanh (d * squeezed) / d;
                }
                out[c] += (squeezed - out[c]) * kWet;
            }
        }

        // --- Echo: read behind the write head (linear interpolation), darken the repeats, feed back.
        auto delay = echoTime.next();
        const auto eWet = echoWet.next();
        const auto fb = echoFb.next();
        const auto freeze = freezeMix.next();
        if (tape)
        {
            // Wow (slow) and flutter (fast) move the tape under the head.
            wowPhase += 0.6 / rate;
            flutterPhase += 7.0 / rate;
            if (wowPhase >= 1.0) wowPhase -= 1.0;
            if (flutterPhase >= 1.0) flutterPhase -= 1.0;
            delay *= 1.0f + 0.004f * static_cast<float> (std::sin (2.0 * pi * wowPhase)) + 0.0012f * static_cast<float> (std::sin (2.0 * pi * flutterPhase));
            delay = std::min (delay, static_cast<float> (lineSize - 2));
        }
        if (duck)
        {
            const auto l = std::max (std::abs (out[0]), std::abs (out[1]));
            duckEnv = l > duckEnv ? 0.01f * l + 0.99f * duckEnv : 0.99985f * duckEnv + 0.00015f * l;
        }
        const auto duckGain = duck ? 1.0f / (1.0f + duckEnv * 8.0f) : 1.0f;
        auto readPos = static_cast<double> (echoWrite) - static_cast<double> (delay);
        while (readPos < 0)
            readPos += static_cast<double> (lineSize);
        const auto r0 = static_cast<std::size_t> (readPos);
        const auto frac = static_cast<float> (readPos - static_cast<double> (r0));
        const auto r1 = (r0 + 1) % lineSize;
        float delayed[2];
        for (std::size_t c = 0; c < 2; ++c)
        {
            delayed[c] = echoLine[c][r0] * (1.0f - frac) + echoLine[c][r1] * frac;
            // Gentle low-pass in the loop, like tape (darker with TAPE; none while frozen, so the loop holds).
            const auto tone = (tape ? 0.22f : 0.35f) + (1.0f - (tape ? 0.22f : 0.35f)) * freeze;
            echoTone[c] += (delayed[c] - echoTone[c]) * tone;
        }
        for (std::size_t c = 0; c < 2; ++c)
        {
            // Only what goes in while echo is on gets echoed; the tail keeps ringing after it's turned off.
            // PING PONG: the sound goes in on the left and each repeat crosses to the other side.
            const auto input = pingPong ? (c == 0 ? (out[0] + out[1]) * 0.5f * eWet : 0.0f) : out[c] * eWet;
            auto back = (pingPong ? echoTone[1 - c] : echoTone[c]) * fb;
            if (tape)
                back = std::tanh (back * 1.3f) / 1.3f;  // saturates instead of running away
            // FREEZE: no new sound in, and the loop goes round at exactly unity.
            const auto loop = pingPong ? echoTone[1 - c] : echoTone[c];
            echoLine[c][echoWrite] = (input + back) * (1.0f - freeze) + loop * freeze;
            out[c] += echoTone[c] * duckGain;
        }
        echoWrite = (echoWrite + 1) % lineSize;

        // --- Reverb (Freeverb): eight damped combs in parallel, four allpasses in series.
        const auto rWet = reverbWet.next();
        const auto room = reverbRoom.next();
        const auto gated = gateMix.next();
        const auto shimmer = shimmerMix.next();
        if (rWet > 1e-4f)
        {
            // GATED: a big room that slams shut when the hit is over.
            const auto level = std::max (std::abs (out[0]), std::abs (out[1]));
            gateEnv = level > gateEnv ? level : gateEnv * 0.9995f;
            if (level > 0.08f)
                gateHold = static_cast<int> (rate * 0.13);
            else if (gateHold > 0)
                --gateHold;
            const auto gateTarget = gateHold > 0 ? 1.0f : 0.0f;
            gateGain += (gateTarget - gateGain) * (gateTarget > gateGain ? 0.02f : 0.004f);
            const auto shapedRoom = room + (0.985f - room) * gated;
            auto input = (out[0] + out[1]) * 0.015f * rWet;
            input += (shimmerBack[0] + shimmerBack[1]) * 0.5f * 0.012f * shimmer;
            for (std::size_t c = 0; c < 2; ++c)
            {
                float acc = 0;
                for (auto& comb : combs[c])
                {
                    auto& v = comb.buffer[comb.index];
                    comb.store = v * 0.8f + comb.store * 0.2f;  // damping
                    acc += v;
                    v = input + comb.store * shapedRoom;
                    comb.index = (comb.index + 1) % comb.buffer.size();
                }
                for (auto& ap : allpasses[c])
                {
                    auto& v = ap.buffer[ap.index];
                    const auto o = v - acc;
                    v = acc + v * 0.5f;
                    acc = o;
                    ap.index = (ap.index + 1) % ap.buffer.size();
                }
                // SHIMMER: the tail, an octave up, goes back in (two taps sweep through a 100 ms window at
                // twice the speed, crossfaded so the jumps don't click).
                if (shimmer > 1e-4f)
                {
                    auto& line = shimmerLine[c];
                    const auto n = line.size();
                    line[shimmerWrite] = acc;
                    const auto window = static_cast<double> (n - 2);
                    float shifted = 0;
                    for (int tap = 0; tap < 2; ++tap)
                    {
                        auto ph = shimmerPhase + 0.5 * tap;
                        if (ph >= 1.0)
                            ph -= 1.0;
                        // Reading twice as fast as writing: the distance behind the write head shrinks.
                        auto pos = static_cast<double> (shimmerWrite) - (1.0 - ph) * window;
                        while (pos < 0)
                            pos += static_cast<double> (n);
                        const auto i0 = static_cast<std::size_t> (pos) % n;
                        const auto f = static_cast<float> (pos - std::floor (pos));
                        const auto s = line[i0] * (1.0f - f) + line[(i0 + 1) % n] * f;
                        shifted += s * static_cast<float> (std::sin (pi * ph));  // a smooth window per tap
                    }
                    shimmerBack[c] = shifted;
                }
                const auto gate = 1.0f - gated + gated * gateGain;
                out[c] += acc * 3.0f * gate + shimmerBack[c] * 1.2f * shimmer * rWet;
            }
            shimmerWrite = (shimmerWrite + 1) % shimmerLine[0].size();
            shimmerPhase += 1.0 / static_cast<double> (shimmerLine[0].size() - 2);
            if (shimmerPhase >= 1.0)
                shimmerPhase -= 1.0;
        }

        left[i] = out[0];
        if (stereo)
            right[i] = out[1];
    }
    gainReduction.store (compWet.value > 1e-4f ? deepest : 0.0f, std::memory_order_relaxed);
}

} // namespace dss1::engine
