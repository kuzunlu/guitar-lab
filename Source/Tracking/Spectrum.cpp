#include "Tracking/Spectrum.h"
#include "Tracking/PitchDetector.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
    }

    void Spectrum::prepare (int size, int fallLength)
    {
        n = 1;
        while (n < size) n <<= 1;

        data.assign ((size_t) n, {});
        mags.assign ((size_t) n / 2, 0.0f);
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1)));

        if (fallLength > 0 && fallLength < n)
        {
            const int rise = n - fallLength;
            for (int i = 0; i < n; ++i)
            {
                const double t = i < rise ? (double) i / rise : 1.0 + (double) (i - rise) / fallLength;
                window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kPi * t)); // 0 -> 1 over the rise, 1 -> 0 over the fall
            }
        }

        twiddles.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            twiddles[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));

        int bits = 0;
        while ((1 << bits) < n) ++bits;
        bitReverse.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitReverse[(size_t) i] = r;
        }
    }

    void Spectrum::compute (const float* samples)
    {
        for (int i = 0; i < n; ++i)
            data[(size_t) bitReverse[(size_t) i]] = { samples[i] * window[(size_t) i], 0.0f };

        // Iterative radix-2 FFT.
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const auto t = twiddles[(size_t) (k * step)] * data[(size_t) (start + k + half)];
                    const auto u = data[(size_t) (start + k)];
                    data[(size_t) (start + k)] = u + t;
                    data[(size_t) (start + k + half)] = u - t;
                }
        }

        for (int i = 0; i < n / 2; ++i)
            mags[(size_t) i] = std::abs (data[(size_t) i]);
    }

    float Spectrum::energy() const
    {
        float sum = 0.0f;
        for (float m : mags) sum += m;
        return sum;
    }

    float Spectrum::peakNear (double hz, double rate) const
    {
        // Strongest bin within 3% of the frequency (string harmonics are a little sharp).
        const double binHz = rate / n;
        const int lo = std::max (1, (int) std::floor (hz * 0.97 / binHz));
        const int hi = std::min (n / 2 - 1, (int) std::ceil (hz * 1.03 / binHz));
        float best = 0.0f;
        for (int i = lo; i <= hi; ++i)
            best = std::max (best, mags[(size_t) i]);
        return best;
    }

    Timbre Spectrum::timbre (double f0, double rate) const
    {
        Timbre t {};
        if (f0 <= 0.0) return t;

        const float h1 = peakNear (f0, rate) + 1e-6f;
        for (int k = 2; k < 2 + kTimbreBins; ++k)
        {
            const float hk = (k * f0 < rate * 0.45) ? peakNear (k * f0, rate) : 0.0f;
            t[(size_t) (k - 2)] = std::clamp (std::log2 ((hk + 1e-6f) / h1), -10.0f, 4.0f);
        }
        return t;
    }

    Overtones Spectrum::overtones (double f0, double rate) const
    {
        Overtones t {};
        if (f0 <= 0.0) return t;

        const float h1 = peakNear (f0, rate) + 1e-6f;
        for (int k = 2; k < 2 + kOvertones; ++k)
        {
            const float hk = (k * f0 < rate * 0.45) ? peakNear (k * f0, rate) : 0.0f;
            t[(size_t) (k - 2)] = std::clamp (std::log2 ((hk + 1e-6f) / h1), -10.0f, 4.0f);
        }
        return t;
    }

    Chroma Spectrum::chroma (double rate, double centsOffset, double minHz, double maxHz) const
    {
        Chroma c {};
        const double binHz = rate / n;
        const int lo = std::max (1, (int) (minHz / binHz));
        const int hi = std::min (n / 2 - 1, (int) (maxHz / binHz));

        for (int i = lo; i <= hi; ++i)
        {
            const double midi = hzToMidi (i * binHz) - centsOffset / 100.0;
            const double nearest = std::round (midi);
            if (std::abs (midi - nearest) > 0.35)
                continue; // between two notes: ignore
            const int pc = (((int) nearest % 12) + 12) % 12;
            // Square root: loud bass does not hide everything else.
            // Less weight up high: there the harmonics (3rd = a 5th above) would add notes that are not in the chord.
            const double weight = 1.0 / (1.0 + i * binHz / 400.0);
            c[(size_t) pc] += (float) (std::sqrt (mags[(size_t) i]) * weight);
        }

        float len = 0.0f;
        for (float v : c) len += v * v;
        len = std::sqrt (len);
        if (len > 0.0f)
            for (auto& v : c) v /= len;
        return c;
    }

    float timbreDistance (const Timbre& a, const Timbre& b)
    {
        float sum = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            sum += std::abs (a[i] - b[i]);
        return sum / (float) a.size();
    }

    float chromaSimilarity (const Chroma& a, const Chroma& b)
    {
        float dot = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            dot += a[i] * b[i];
        return dot;
    }
}
