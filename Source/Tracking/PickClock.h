#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gl
{
    // Remembers when the guitar was picked, so the note log can show how long the tracker took to send a note.
    // Keeps the peak of every short piece of input (32 samples, 0.67 ms at 48 kHz) for the last ~1.4 s.
    // A pick = a piece whose peak is at least twice the highest peak of the ~27 ms before it (longer than one
    // wave of a low B string, so a ringing note never looks like a pick). Fixed memory: safe on the audio thread.
    class PickClock
    {
    public:
        static constexpr int kPiece = 32;

        // firstSample: the stream position of the first sample (the same clock the caller uses for its notes).
        void feed (const float* const* channels, int numChannels, int numSamples, int64_t firstSample)
        {
            if (firstSample != next) piecePeak = 0.0f; // a gap: the piece being filled is incomplete
            next = firstSample + numSamples;
            for (int i = 0; i < numSamples; ++i)
            {
                for (int c = 0; c < numChannels; ++c)
                    piecePeak = std::max (piecePeak, std::abs (channels[c][i]));
                const int64_t at = firstSample + i;
                if ((at + 1) % kPiece == 0)
                {
                    const int64_t p = at / kPiece;
                    peaks[(size_t) (p % kSize)] = piecePeak;
                    owners[(size_t) (p % kSize)] = p;
                    piecePeak = 0.0f;
                }
            }
        }

        // Where the last pick before `noteSample` started (a sample number), or -1 when there was none in the
        // `lookBack` samples before it (a hammer-on or slide without a new pick, or too quiet).
        int64_t pickBefore (int64_t noteSample, int64_t lookBack) const
        {
            const int64_t last = noteSample / kPiece;
            const int64_t first = std::max ((noteSample - lookBack) / kPiece, last - kSize + kBefore + 1);
            int64_t start = -1;
            float top = 0.0f;
            for (int64_t p = last; p >= first; --p)
            {
                if (rises (p))       { start = p; top = std::max (top, peaks[(size_t) (p % kSize)]); } // back to the rise's start
                else if (start >= 0) break;
            }
            if (start < 0) return -1;

            // The pick is where the rise reaches a tenth of its top: a slow swell before it (the pick pressing the
            // string, or the hand touching it) is not the pick.
            while (start < last && peaks[(size_t) (start % kSize)] < 0.1f * top)
                ++start;
            return start * kPiece;
        }

    private:
        static constexpr int kSize = 2048;   // pieces kept
        static constexpr int kBefore = 40;   // pieces compared with (~27 ms at 48 kHz)
        static constexpr float kQuiet = 0.002f;

        bool known (int64_t p, float& peak) const
        {
            if (p < 0 || owners[(size_t) (p % kSize)] != p) return false;
            peak = peaks[(size_t) (p % kSize)];
            return true;
        }

        bool rises (int64_t p) const
        {
            float peak = 0.0f;
            if (! known (p, peak) || peak < kQuiet) return false;
            float before = 0.0f;
            for (int64_t q = p - kBefore; q < p; ++q)
            {
                float f = 0.0f;
                if (! known (q, f)) return false;
                before = std::max (before, f);
            }
            return peak > 2.0f * before;
        }

        std::array<float, kSize> peaks {};
        std::array<int64_t, kSize> owners = [] { std::array<int64_t, kSize> a {}; a.fill (-1); return a; }();
        float piecePeak = 0.0f;
        int64_t next = 0;
    };
}
