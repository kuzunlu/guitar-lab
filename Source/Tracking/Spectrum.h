#pragma once

#include <array>
#include <complex>
#include <vector>

namespace gl
{
    constexpr int kTimbreBins = 8;

    // "Tone colour": strength of harmonics 2..9 compared to the fundamental (log2 ratio).
    // The same pitch on a thicker string has a different pattern, which helps guess the string.
    using Timbre = std::array<float, kTimbreBins>;

    // The same idea with more harmonics (2..16), measured right at the pick attack during a full calibration.
    // Poly mode builds one fingerprint per string and fret from it (what this guitar sounds like when a note starts).
    constexpr int kOvertones = 15;
    using Overtones = std::array<float, kOvertones>;

    // How strong each of the 12 note names is in the sound (unit length). Used to recognise chords.
    using Chroma = std::array<float, 12>;

    // Magnitude spectrum of a block of audio (radix-2 FFT, Hann window).
    class Spectrum
    {
    public:
        // size must be a power of two. Allocates: call outside the audio thread.
        // fallLength > 0: low-latency window (slow rise, fast fall over the last fallLength samples), so the
        // newest sound counts most. 0 = normal Hann window.
        void prepare (int size, int fallLength = 0);
        int  size() const { return n; }

        const std::vector<float>& magnitudes() const { return mags; } // size() / 2 bins
        const std::vector<float>& windowShape() const { return window; }

        // Uses exactly size() samples, oldest first.
        void compute (const float* samples);

        Timbre timbre (double f0, double sampleRate) const;
        Overtones overtones (double f0, double sampleRate) const; // harmonics 2..16, log2 of their strength vs. the fundamental
        Chroma chroma (double sampleRate, double centsOffset, double minHz = 60.0, double maxHz = 1000.0) const;

        // Sum of magnitudes, to check there is any sound at all.
        float energy() const;

    private:
        float peakNear (double hz, double sampleRate) const;

        int n = 0;
        std::vector<std::complex<float>> data, twiddles;
        std::vector<int> bitReverse;
        std::vector<float> window, mags;
    };

    // Smaller = more alike. About 0.2-0.6 for the same string, 1+ for different strings.
    float timbreDistance (const Timbre& a, const Timbre& b);

    // Cosine similarity, 0..1 (1 = same chord sound).
    float chromaSimilarity (const Chroma& a, const Chroma& b);
}
