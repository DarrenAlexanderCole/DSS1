#pragma once

#include <cmath>

namespace dss1::engine
{

/// A biquad filter (RBJ cookbook): peaking, low shelf and high shelf, for the EQs. Transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    enum class Kind { peak, lowShelf, highShelf };

    void set (Kind kind, double rate, double hz, double q, double gainDb) noexcept
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * 3.141592653589793 * std::min (hz, rate * 0.49) / rate;
        const auto cw = std::cos (w), sw = std::sin (w);
        const auto alpha = sw / (2.0 * q);
        double nb0, nb1, nb2, na0, na1, na2;
        switch (kind)
        {
            case Kind::peak:
                nb0 = 1 + alpha * a; nb1 = -2 * cw; nb2 = 1 - alpha * a;
                na0 = 1 + alpha / a; na1 = -2 * cw; na2 = 1 - alpha / a;
                break;
            case Kind::lowShelf:
            {
                const auto s = 2.0 * std::sqrt (a) * alpha;
                nb0 = a * ((a + 1) - (a - 1) * cw + s); nb1 = 2 * a * ((a - 1) - (a + 1) * cw); nb2 = a * ((a + 1) - (a - 1) * cw - s);
                na0 = (a + 1) + (a - 1) * cw + s; na1 = -2 * ((a - 1) + (a + 1) * cw); na2 = (a + 1) + (a - 1) * cw - s;
                break;
            }
            case Kind::highShelf:
            default:
            {
                const auto s = 2.0 * std::sqrt (a) * alpha;
                nb0 = a * ((a + 1) + (a - 1) * cw + s); nb1 = -2 * a * ((a - 1) + (a + 1) * cw); nb2 = a * ((a + 1) + (a - 1) * cw - s);
                na0 = (a + 1) - (a - 1) * cw + s; na1 = 2 * ((a - 1) - (a + 1) * cw); na2 = (a + 1) - (a - 1) * cw - s;
                break;
            }
        }
        b0 = static_cast<float> (nb0 / na0); b1 = static_cast<float> (nb1 / na0); b2 = static_cast<float> (nb2 / na0);
        a1 = static_cast<float> (na1 / na0); a2 = static_cast<float> (na2 / na0);
    }

    float process (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    /// The filter's gain at `hz`, in dB (for drawing the curve).
    double magnitudeDb (double rate, double hz) const noexcept
    {
        const auto w = 2.0 * 3.141592653589793 * hz / rate;
        const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const auto nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const auto dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10 ((nr * nr + ni * ni) / std::max (1.0e-12, dr * dr + di * di));
    }
};

} // namespace dss1::engine
