#include "Tracking/PitchDetector.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTargetRate = 12000.0;
    }

    double hzToMidi (double hz, double a4)  { return 69.0 + 12.0 * std::log2 (hz / a4); }
    double midiToHz (double midi, double a4) { return a4 * std::pow (2.0, (midi - 69.0) / 12.0); }

    void Biquad::setLowPass (double sampleRate, double cutoffHz, double q)
    {
        // Standard "RBJ cookbook" low-pass.
        const double w0 = 2.0 * kPi * cutoffHz / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cosw = std::cos (w0);
        const double a0 = 1.0 + alpha;

        b0 = (1.0 - cosw) / 2.0 / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = b0;
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
        reset();
    }

    void PitchDetector::prepare (double sampleRate, double minHz, double maxHz)
    {
        // At least ~16 samples per wave of the highest note, so high notes stay accurate.
        // Low strings get a lower rate, which keeps the CPU cost small.
        const double target = std::clamp (maxHz * 16.0, 8000.0, 2.0 * kTargetRate);
        decimation = std::max (1, (int) std::floor (sampleRate / target));
        rate = sampleRate / decimation;

        const double cutoff = std::min (rate * 0.35, sampleRate * 0.45);
        lp1.setLowPass (sampleRate, cutoff);
        lp2.setLowPass (sampleRate, cutoff);

        minHz = std::max (20.0, minHz);
        maxHz = std::clamp (maxHz, minHz * 1.5, rate * 0.25);

        tauMin = std::max (2, (int) std::floor (rate / maxHz));
        tauMax = std::max (tauMin + 2, (int) std::ceil (rate / minHz));
        window = tauMax * 2;           // one period of the lowest note to compare, plus the lag
        hopSize = std::max (16, (int) std::lround (rate * 0.005)); // ~5 ms

        ring.assign ((size_t) window, 0.0f);
        frame.assign ((size_t) window, 0.0f);
        diff.assign ((size_t) tauMax + 2, 0.0);
        reset();
    }

    void PitchDetector::reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (history.begin(), history.end(), 0.0f);
        historyPos = 0;
        lp1.reset();
        lp2.reset();
        writePos = hopCounter = decimCounter = 0;
        hopPeak = 0.0f;
        hopSumSq = 0.0;
        latest = {};
    }

    void PitchDetector::analyse()
    {
        latest.rms  = std::sqrt (hopSumSq / std::max (1, hopSize));
        latest.peak = hopPeak;
        hopSumSq = 0.0;
        hopPeak = 0.0f;

        if (latest.rms < 1.0e-5)
        {
            latest.hz = 0.0;
            latest.clarity = 0.0;
            return;
        }

        // Oldest sample first.
        for (int i = 0; i < window; ++i)
            frame[(size_t) i] = ring[(size_t) ((writePos + i) % window)];

        // YIN step 1+2: difference function for each lag (tau).
        const int w = tauMax;
        if (windowWaves > 0.0)
        {
            // Only the newest `windowWaves` waves of each possible pitch: a high note is read as soon as a few of
            // its short waves have sounded, instead of waiting for a window sized for the lowest note.
            const int wMin = std::max (8, (int) (rate * 0.002));
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                const int wt = std::clamp ((int) (windowWaves * tau), wMin, window - tau);
                double sum = 0.0;
                for (int j = window - wt; j < window; ++j)
                {
                    const double d = (double) frame[(size_t) j] - frame[(size_t) (j - tau)];
                    sum += d * d;
                }
                diff[(size_t) tau] = sum / wt;
            }
        }
        else
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < w; ++j)
            {
                const double d = (double) frame[(size_t) j] - frame[(size_t) (j + tau)];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        // Step 3: cumulative mean normalised difference.
        diff[0] = 1.0;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0.0 ? diff[(size_t) tau] * tau / running : 1.0;
        }

        // Step 4: first dip under the threshold (then walk to the bottom of that dip).
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (diff[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        // No dip under the threshold: take the lowest point, but only if it is still fairly clear.
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (diff[(size_t) tau] < diff[(size_t) best])
                    best = tau;
            if (diff[(size_t) best] > 0.35)
            {
                latest.hz = 0.0;
                latest.clarity = 1.0 - diff[(size_t) best];
                return;
            }
        }

        // Step 5: parabolic interpolation for a finer lag.
        double refined = best;
        if (best > 1 && best < tauMax)
        {
            const double a = diff[(size_t) (best - 1)], b = diff[(size_t) best], c = diff[(size_t) (best + 1)];
            const double denom = a - 2.0 * b + c;
            if (std::abs (denom) > 1e-12)
                refined = best + 0.5 * (a - c) / denom;
        }

        latest.hz = rate / refined;
        latest.clarity = std::clamp (1.0 - diff[(size_t) best], 0.0, 1.0);
    }
}
