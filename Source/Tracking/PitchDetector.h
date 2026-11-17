#pragma once

#include <algorithm>
#include <vector>

namespace gl
{
    // A simple low-pass filter section (one "biquad"). Used before downsampling.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void setLowPass (double sampleRate, double cutoffHz, double q = 0.7071);
        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() { z1 = z2 = 0; }
    };

    // Finds the pitch of ONE note in an audio signal (YIN method).
    // Works on a downsampled copy (about 12 kHz) to save CPU: guitar notes are all below 1.5 kHz.
    class PitchDetector
    {
    public:
        struct Result
        {
            double hz = 0.0;       // 0 = no clear pitch
            double clarity = 0.0;  // 0..1, how sure the detector is (1 = perfect)
            double rms = 0.0;      // loudness of the last hop
            double peak = 0.0;     // highest sample of the last hop
        };

        // minHz / maxHz: the range of notes to look for. A narrow range = faster and fewer octave mistakes.
        // Allocates memory: call from prepareToPlay, never from the audio thread.
        void prepare (double sampleRate, double minHz, double maxHz);
        void reset();

        // Feeds audio. Calls onHop (offset in this block, result) every hop (~5 ms).
        template <typename Callback>
        void process (const float* samples, int numSamples, Callback&& onHop)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float filtered = lp2.process (lp1.process (samples[i]));
                if (++decimCounter < decimation)
                    continue;
                decimCounter = 0;

                ring[(size_t) writePos] = filtered;
                writePos = (writePos + 1) % (int) ring.size();
                history[(size_t) historyPos] = filtered;
                historyPos = (historyPos + 1) % (int) history.size();
                hopPeak = std::max (hopPeak, filtered < 0 ? -filtered : filtered);
                hopSumSq += (double) filtered * filtered;

                if (++hopCounter >= hopSize)
                {
                    hopCounter = 0;
                    analyse();
                    onHop (i, latest);
                }
            }
        }

        const Result& last() const { return latest; }

        // Copies the newest n samples (at analysisRate()), oldest first. n <= kHistory.
        static constexpr int kHistory = 4096;
        void copyHistory (float* dest, int n) const
        {
            n = std::min (n, kHistory);
            const int size = (int) history.size();
            for (int i = 0; i < n; ++i)
                dest[i] = history[(size_t) ((historyPos - n + i + size) % size)];
        }
        double analysisRate() const { return rate; }
        int    hopSamplesAtInputRate() const { return hopSize * decimation; }
        double windowSeconds() const { return window / rate; }

    private:
        void analyse();

        Biquad lp1, lp2;
        int    decimation = 4;
        int    decimCounter = 0;
        double rate = 12000.0;

        int tauMin = 2, tauMax = 300, window = 600, hopSize = 64;
        std::vector<float> ring, frame;
        std::vector<float> history = std::vector<float> ((size_t) kHistory, 0.0f);
        int historyPos = 0;
        std::vector<double> diff;
        int writePos = 0, hopCounter = 0;
        float hopPeak = 0.0f;
        double hopSumSq = 0.0;

        double threshold = 0.15; // YIN threshold: lower = stricter
        Result latest;
    };

    // Frequency <-> MIDI note (with a reference A4, normally 440 Hz). Result can have a fraction (cents).
    double hzToMidi (double hz, double a4 = 440.0);
    double midiToHz (double midi, double a4 = 440.0);
}
