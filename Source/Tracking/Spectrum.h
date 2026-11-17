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

    // How strong each of the 12 note names is in the sound (unit length). Used to recognise chords.
    using Chroma = std::array<float, 12>;

    // Magnitude spectrum of a block of audio (radix-2 FFT, Hann window).
    class Spectrum
    {
    public:
        // size must be a power of two. Allocates: call outside the audio thread.
        void prepare (int size);
        int  size() const { return n; }

        // Uses exactly size() samples, oldest first.
        void compute (const float* samples);

        Timbre timbre (double f0, double sampleRate) const;
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
