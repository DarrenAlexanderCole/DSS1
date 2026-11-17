#include "core/Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>

namespace dss1::analysis
{

// --- FFT --------------------------------------------------------------------------------------------------

void fft (std::vector<double>& re, std::vector<double>& im)
{
    const auto n = re.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = -2.0 * std::numbers::pi / static_cast<double> (len);
        const auto wr = std::cos (angle), wi = std::sin (angle);
        for (std::size_t i = 0; i < n; i += len)
        {
            double cr = 1, ci = 0;
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto a = i + k, b = i + k + len / 2;
                const auto tr = re[b] * cr - im[b] * ci;
                const auto ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const auto next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }
}

namespace
{
    std::vector<double> hann (std::size_t n)
    {
        std::vector<double> w (n);
        for (std::size_t i = 0; i < n; ++i)
            w[i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n));
        return w;
    }

    /// Magnitude spectra of successive frames (first n/2 bins each).
    template <typename Visit>
    void spectra (const std::vector<float>& x, std::size_t size, std::size_t hop, Visit&& visit)
    {
        if (x.size() < size)
            return;
        const auto window = hann (size);
        std::vector<double> re (size), im (size), mag (size / 2);
        for (std::size_t start = 0; start + size <= x.size(); start += hop)
        {
            for (std::size_t i = 0; i < size; ++i)
            {
                re[i] = static_cast<double> (x[start + i]) * window[i];
                im[i] = 0;
            }
            fft (re, im);
            for (std::size_t k = 0; k < size / 2; ++k)
                mag[k] = std::sqrt (re[k] * re[k] + im[k] * im[k]);
            visit (mag);
        }
    }

    /// Spectral-flux onset strength, one value per hop, with slow trends removed.
    /// `changeRatio` is how much the spectrum changes from frame to frame, relative to its level: a few
    /// thousandths for a steady tone, several hundredths or more for music with hits.
    std::vector<double> onsetEnvelope (const std::vector<float>& mono, std::size_t size, std::size_t hop, double& changeRatio)
    {
        std::vector<double> flux;
        std::vector<double> previous;
        double totalFlux = 0, totalLevel = 0;
        spectra (mono, size, hop, [&] (const std::vector<double>& mag) {
            std::vector<double> level (mag.size());
            for (std::size_t k = 0; k < mag.size(); ++k)
                level[k] = std::log1p (100.0 * mag[k]);
            double sum = 0;
            if (! previous.empty())
                for (std::size_t k = 1; k < mag.size(); ++k)
                {
                    sum += std::max (0.0, level[k] - previous[k]);
                    totalLevel += level[k];
                }
            totalFlux += sum;
            flux.push_back (sum);
            previous = std::move (level);
        });
        changeRatio = totalLevel > 0 ? totalFlux / totalLevel : 0.0;

        // Subtract a ~1 s moving average so only the pulses remain.
        const std::size_t half = 40;
        std::vector<double> out (flux.size());
        double running = 0;
        std::size_t lo = 0, hi = 0;
        for (std::size_t i = 0; i < flux.size(); ++i)
        {
            while (hi < flux.size() && hi <= i + half)
                running += flux[hi++];
            while (lo + half < i)
                running -= flux[lo++];
            out[i] = std::max (0.0, flux[i] - running / static_cast<double> (hi - lo));
        }
        return out;
    }

    std::vector<double> autocorrelation (const std::vector<double>& x, std::size_t maxLag)
    {
        std::vector<double> ac (maxLag + 1, 0.0);
        for (std::size_t lag = 0; lag <= maxLag && lag < x.size(); ++lag)
        {
            double sum = 0;
            for (std::size_t i = lag; i < x.size(); ++i)
                sum += x[i] * x[i - lag];
            ac[lag] = sum / static_cast<double> (x.size() - lag);
        }
        return ac;
    }

    /// Peak position in [from, to] refined by fitting a parabola through the top three points.
    double refinedPeak (const std::vector<double>& v, std::size_t from, std::size_t to)
    {
        to = std::min (to, v.size() - 2);
        from = std::max<std::size_t> (from, 1);
        if (from > to)
            return static_cast<double> (from);
        std::size_t best = from;
        for (auto i = from; i <= to; ++i)
            if (v[i] > v[best])
                best = i;
        const auto a = v[best - 1], b = v[best], c = v[best + 1];
        const auto denominator = a - 2 * b + c;
        const auto shift = std::abs (denominator) > 1e-12 ? 0.5 * (a - c) / denominator : 0.0;
        return static_cast<double> (best) + std::clamp (shift, -0.5, 0.5);
    }

    // --- Loudness ----------------------------------------------------------------------------------------

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x)
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    /// The two K-weighting stages for any sample rate (the BS.1770 filters, derived as in libebur128;
    /// at 48 kHz these give exactly the coefficients printed in the standard).
    std::array<Biquad, 2> kWeighting (double rate)
    {
        std::array<Biquad, 2> f;
        {
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const auto K = std::tan (std::numbers::pi * f0 / rate);
            const auto vh = std::pow (10.0, gainDb / 20.0);
            const auto vb = std::pow (vh, 0.4996667741545416);
            const auto a0 = 1.0 + K / q + K * K;
            f[0].b0 = (vh + vb * K / q + K * K) / a0;
            f[0].b1 = 2.0 * (K * K - vh) / a0;
            f[0].b2 = (vh - vb * K / q + K * K) / a0;
            f[0].a1 = 2.0 * (K * K - 1.0) / a0;
            f[0].a2 = (1.0 - K / q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const auto K = std::tan (std::numbers::pi * f0 / rate);
            const auto a0 = 1.0 + K / q + K * K;
            f[1].b0 = 1.0;
            f[1].b1 = -2.0;
            f[1].b2 = 1.0;
            f[1].a1 = 2.0 * (K * K - 1.0) / a0;
            f[1].a2 = (1.0 - K / q + K * K) / a0;
        }
        return f;
    }

    constexpr const char* noteNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
} // namespace

// --- Tempo ------------------------------------------------------------------------------------------------

std::optional<double> detectBpm (const std::vector<float>& mono, double sampleRate, double minBpm, double maxBpm)
{
    if (sampleRate <= 0 || mono.size() < static_cast<std::size_t> (sampleRate * 8))
        return std::nullopt;

    constexpr std::size_t size = 1024, hop = 512;
    double changeRatio = 0;
    const auto envelope = onsetEnvelope (mono, size, hop, changeRatio);
    if (changeRatio < 0.02)
        return std::nullopt;  // a steady sound: nothing that pulses
    const auto hopsPerSecond = sampleRate / static_cast<double> (hop);
    const auto lagOf = [&] (double bpm) { return 60.0 * hopsPerSecond / bpm; };

    // Look for the beat between 60 and 200 BPM, preferring tempos near 120 (a gentle log-normal weighting,
    // which picks the danceable reading when half and double tempo both fit).
    const auto maxLag = static_cast<std::size_t> (std::ceil (lagOf (60.0))) + 2;
    if (envelope.size() < maxLag * 4)
        return std::nullopt;
    const auto ac = autocorrelation (envelope, std::min (envelope.size() / 2, maxLag * 16 + 32));
    if (ac[0] <= 0)
        return std::nullopt;

    std::size_t bestLag = 0;
    double bestScore = 0;
    for (auto lag = static_cast<std::size_t> (std::floor (lagOf (200.0))); lag <= maxLag; ++lag)
    {
        const auto bpm = 60.0 * hopsPerSecond / static_cast<double> (lag);
        const auto prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0));
        const auto score = ac[lag] * prior;
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestLag == 0)
        return std::nullopt;

    // A steady pulse makes the beat's autocorrelation stand well above the average over all candidate lags.
    double average = 0;
    std::size_t counted = 0;
    for (auto lag = static_cast<std::size_t> (std::floor (lagOf (200.0))); lag <= maxLag; ++lag, ++counted)
        average += ac[lag];
    average /= static_cast<double> (std::max<std::size_t> (counted, 1));
    const auto peakEnvelope = *std::max_element (envelope.begin(), envelope.end());
    if (peakEnvelope < 1e-3 || ac[bestLag] < 1.5 * average || ac[bestLag] < 0.05 * ac[0])
        return std::nullopt;  // no steady pulse

    // Half-time trap: a backbeat (snare on 2 and 4) makes the two-beat period strongest, so 174 BPM drum & bass
    // reads as 87. If the one-beat period is nearly as strong and still in range, take the faster tempo.
    const auto half = static_cast<std::size_t> (std::lround (static_cast<double> (bestLag) / 2.0));
    if (half >= 2 && 60.0 * hopsPerSecond / static_cast<double> (half) < maxBpm)
    {
        const auto peak = static_cast<std::size_t> (std::lround (refinedPeak (ac, half - 1, half + 1)));
        if (ac[peak] >= 0.5 * ac[bestLag])
            bestLag = peak;
    }

    // Refine on a multiple of the beat: a peak 16 beats away pins the tempo 16 times more precisely.
    auto period = refinedPeak (ac, bestLag - 1, bestLag + 1);
    for (const int beats : { 4, 8, 16 })
    {
        const auto centre = period * beats;
        if (centre + beats + 2 >= static_cast<double> (ac.size()))
            break;
        const auto from = static_cast<std::size_t> (std::floor (centre - beats));
        const auto to = static_cast<std::size_t> (std::ceil (centre + beats));
        period = refinedPeak (ac, from, to) / beats;
    }

    auto bpm = 60.0 * hopsPerSecond / period;
    while (bpm < minBpm)
        bpm *= 2;
    while (bpm >= maxBpm)
        bpm /= 2;
    return std::round (bpm * 100.0) / 100.0;
}

// --- Key --------------------------------------------------------------------------------------------------

std::string Key::name() const
{
    return std::string (noteNames[tonic % 12]) + (minor ? " minor" : " major");
}

std::string Key::camelot() const
{
    // Camelot wheel numbers by tonic (C = 0 … B = 11).
    static constexpr int minorNumber[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    static constexpr int majorNumber[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    const auto t = static_cast<std::size_t> (tonic % 12);
    return std::to_string (minor ? minorNumber[t] : majorNumber[t]) + (minor ? "A" : "B");
}

std::optional<Key> detectKey (const std::vector<float>& mono, double sampleRate)
{
    if (sampleRate <= 0)
        return std::nullopt;

    constexpr std::size_t size = 8192, hop = 4096;
    std::array<double, 12> chroma {};
    const auto binHz = sampleRate / static_cast<double> (size);

    spectra (mono, size, hop, [&] (const std::vector<double>& mag) {
        std::array<double, 12> frame {};
        for (std::size_t k = 1; k < mag.size(); ++k)
        {
            const auto hz = static_cast<double> (k) * binHz;
            if (hz < 55.0 || hz > 2000.0)
                continue;
            const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const auto pc = static_cast<std::size_t> (((static_cast<long> (std::lround (midi)) % 12) + 12) % 12);
            frame[pc] += mag[k] * mag[k];
        }
        const auto total = std::accumulate (frame.begin(), frame.end(), 0.0);
        if (total > 1e-9)
            for (std::size_t i = 0; i < 12; ++i)
                chroma[i] += std::sqrt (frame[i] / total);  // each frame counts equally
    });

    const auto total = std::accumulate (chroma.begin(), chroma.end(), 0.0);
    if (total <= 1e-9)
        return std::nullopt;

    // Krumhansl–Kessler key profiles.
    static constexpr std::array<double, 12> major { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    const auto correlate = [&] (const std::array<double, 12>& profile, int tonic) {
        const auto mean = [] (const auto& v) { return std::accumulate (v.begin(), v.end(), 0.0) / 12.0; };
        const auto mc = mean (chroma), mp = mean (profile);
        double num = 0, dc = 0, dp = 0;
        for (std::size_t i = 0; i < 12; ++i)
        {
            const auto c = chroma[(i + static_cast<std::size_t> (tonic)) % 12] - mc;
            const auto p = profile[i] - mp;
            num += c * p;
            dc += c * c;
            dp += p * p;
        }
        return (dc > 0 && dp > 0) ? num / std::sqrt (dc * dp) : 0.0;
    };

    Key best;
    best.confidence = -2;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (const bool isMinor : { false, true })
            if (const auto r = correlate (isMinor ? minor : major, tonic); r > best.confidence)
                best = { tonic, isMinor, r };

    if (best.confidence < 0.3)
        return std::nullopt;  // no clear tonal centre
    return best;
}

// --- Loudness ---------------------------------------------------------------------------------------------

std::optional<double> integratedLoudness (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty() || sampleRate <= 0)
        return std::nullopt;

    const auto frames = channels.front().size();
    const auto block = static_cast<std::size_t> (std::lround (0.4 * sampleRate));
    const auto step = static_cast<std::size_t> (std::lround (0.1 * sampleRate));
    if (frames < block)
        return std::nullopt;

    // K-weighted squares, summed over channels (front channels all weigh 1).
    std::vector<double> power (frames, 0.0);
    for (const auto& channel : channels)
    {
        auto filters = kWeighting (sampleRate);
        for (std::size_t i = 0; i < frames && i < channel.size(); ++i)
        {
            const auto y = filters[1].process (filters[0].process (static_cast<double> (channel[i])));
            power[i] += y * y;
        }
    }

    // Mean power of each 400 ms block, stepping 100 ms (75% overlap), using a running sum.
    std::vector<double> blocks;
    std::vector<double> prefix (frames + 1, 0.0);
    for (std::size_t i = 0; i < frames; ++i)
        prefix[i + 1] = prefix[i] + power[i];
    for (std::size_t start = 0; start + block <= frames; start += step)
        blocks.push_back ((prefix[start + block] - prefix[start]) / static_cast<double> (block));

    const auto lufs = [] (double z) { return -0.691 + 10.0 * std::log10 (z); };
    const auto gatedMean = [&] (double threshold) {
        double sum = 0;
        std::size_t n = 0;
        for (const auto z : blocks)
            if (z > 0 && lufs (z) > std::max (threshold, -70.0))
            {
                sum += z;
                ++n;
            }
        return n > 0 ? std::optional<double> (sum / static_cast<double> (n)) : std::nullopt;
    };

    const auto absolute = gatedMean (-70.0);
    if (! absolute)
        return std::nullopt;
    const auto relative = gatedMean (lufs (*absolute) - 10.0);
    return relative ? std::optional<double> (lufs (*relative)) : std::nullopt;
}

} // namespace dss1::analysis
