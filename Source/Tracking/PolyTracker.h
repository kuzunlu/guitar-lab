#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Calibration.h"
#include "Tracking/PitchDetector.h"
#include "Tracking/Spectrum.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gl
{
    // Hears several notes at once on a normal guitar (one cable): chords, double stops, ringing arpeggios.
    //
    // How it works, about 90 times a second:
    //  1. Spectrum of the last ~170 ms (low-latency window, so the newest sound counts most),
    //     on a log scale with 3 bins per semitone.
    //  2. That spectrum is explained as a sum of note "fingerprints" (the harmonic pattern of each playable
    //     pitch), with non-negative matrix factorisation (fixed templates, KL divergence). After a full
    //     calibration the fingerprints use the real tone colour of this guitar; otherwise a typical pattern.
    //  3. Pitches that stay strong for a few frames start notes. A small solver gives every new note a
    //     string and fret (one note per string, a playable hand span, close to where the hand was).
    //  4. Each note's exact pitch is followed in a short spectrum (~43 ms) and sent as pitch bend.
    class PolyTracker
    {
    public:
        struct Settings
        {
            int    stableFrames = 3;   // from the "Speed / safety" knob
            double sensitivity = 1.0;
            bool   trackBends = true;
            int    bendRange = 12;
            int    firstChannel = 2;
            double hysteresis = 0.0;   // 0..1: a pitch on an overtone of a louder, just-picked note needs up to 4x more energy (0 = off)
        };

        // Allocates and builds the fingerprints: call from the message thread.
        void prepare (double sampleRate, const Instrument& inst, const Calibration& cal, const Settings& settings);
        void reset();

        // The one-note pitch detector's reading at an input sample (counted since reset): the note it hears and how
        // clear it is. Given in time order, before process() reaches that sample.
        void hint (int64_t sample, float midi, float clarity)
        {
            if (hintWrite - hintRead < (int) hints.size())
                hints[(size_t) (hintWrite++ % (int) hints.size())] = { sample, midi, clarity };
        }

        // emit (sampleOffset, status, data1, data2) for each MIDI message.
        template <typename Emit>
        void process (const float* samples, int numSamples, Emit&& emit)
        {
            if (! prepared) return;
            for (int i = 0; i < numSamples; ++i)
            {
                ++samplesIn;
                float x = lp2.process (lp1.process (samples[i]));
                if (++decimCounter < decimation)
                    continue;
                decimCounter = 0;

                // Low cut (~30 Hz): rumble and DC would look like the lowest notes.
                const float y = x - dcIn + dcCoeff * dcOut;
                dcIn = x;
                dcOut = y;
                x = y;

                ring[(size_t) writePos] = x;
                writePos = (writePos + 1) & (kLong - 1);
                hopPeak = std::max (hopPeak, x < 0 ? -x : x);
                hopSumSq += (double) x * x;

                if (++hopCounter >= kHop)
                {
                    hopCounter = 0;
                    outCount = 0;
                    analyse();
                    for (int k = 0; k < outCount; ++k)
                        emit (i, out[(size_t) k].status, out[(size_t) k].d1, out[(size_t) k].d2);
                }
            }
        }

        // Note-offs for everything sounding, and pitch bends back to the middle.
        template <typename Emit>
        void allOff (Emit&& emit)
        {
            outCount = 0;
            for (int p = 0; p < numPitches; ++p)
                if (notes[(size_t) p].on) stop (p);
            for (int s = 0; s < kMaxStrings; ++s)
                if (bend[(size_t) s] != 8192) sendBend (s, 8192);
            for (int k = 0; k < outCount; ++k)
                emit (0, out[(size_t) k].status, out[(size_t) k].d1, out[(size_t) k].d2);
            outCount = 0;
        }

        double hopSeconds() const { return kHop * decimation / inputRate; }

        static constexpr int kLong = 2048;            // ~170 ms at ~12 kHz: fine enough to split low notes
        static constexpr int kShort = 512;            // ~43 ms: pick attacks and exact pitch (bends, vibrato)
        static constexpr int kHop = 128;              // ~10.7 ms between analyses
        static constexpr int kBinsPerSemitone = 3;
        static constexpr int kMaxHarmonics = 30;

    private:
        struct Msg { uint8_t status = 0, d1 = 0, d2 = 0; };

        // A chord shape: the note on each string (-1 = not played).
        struct Shape
        {
            std::array<int, kMaxStrings> note {};
            int bass = 0;          // lowest note
            int size = 0;          // strings played
        };
        struct Entry { int bin; float w; };

        struct Note
        {
            bool   on = false;
            int    string = -1;
            int    fret = 0;
            float  peak = 0.0f;   // strongest activation since the note started
            int    low = 0;       // frames in a row where it seemed to have stopped
            double dev = 0.0;     // bend, in semitones
            int    age = 0;       // frames since start
            bool   hidden = false;// octave note added from a chord shape (lives as long as its lower octave)
            bool   repicked = false; // started again by a pick attack no new note explained (not a fresh note for hysteresis)
        };

        void analyse();
        void solve();
        void updateNotes();
        void startNotes (int count);
        void completeChord();
        void buildShapes();
        void stop (int p);
        void followPitch (int p);
        bool isGhost (int p, bool picked) const; // picked: its energy just jumped after a pick attack
        bool isClaimed (int p) const;
        bool hasOwnFundamental (int p, int above) const;
        bool hasHarmonicPeaks (int p) const;
        float ownEnergyAt (int p, double midi) const;
        float spectrumAt (double midi) const;   // square-rooted spectrum near a pitch
        void toLog (const std::vector<float>& linear, std::vector<float>& logOut) const;
        std::array<float, kMaxHarmonics + 1> harmonicProfile (int midi) const;
        int  velocityFor (int p, int string, int fret, bool legato) const;

        void send (uint8_t status, uint8_t d1, uint8_t d2)
        {
            if (outCount < (int) out.size()) out[(size_t) outCount++] = { status, d1, d2 };
        }
        void sendBend (int string, int value);
        int  channelOf (int string) const { return std::clamp (settings.firstChannel + string, 1, 16); }

        Instrument inst;
        Calibration cal;
        Settings settings;
        bool prepared = false;

        // Input: low-pass + downsample to ~12 kHz
        Biquad lp1, lp2;
        double inputRate = 48000.0, rate = 12000.0;
        int    decimation = 4, decimCounter = 0, hopCounter = 0, writePos = 0;
        float  hopPeak = 0.0f;
        double hopSumSq = 0.0;
        float  dcIn = 0.0f, dcOut = 0.0f, dcCoeff = 0.98f;
        std::vector<float> ring, frame;

        Spectrum longSpec, shortSpec;
        std::vector<float> prevShort;

        // Log-frequency bins
        int    numBins = 0;
        double midiLo = 0.0;
        std::vector<int> binLo, binHi;          // linear bins inside each log bin
        std::vector<float> binPos;              // log bin centre, in linear bins (for interpolation)
        std::vector<float> V, WX, ratio;        // spectrum, model, V / model

        // Pitches and their fingerprints (last template = broadband noise, for pick attacks)
        int lowestPitch = 40, numPitches = 0;
        // One fingerprint per pitch; after a full calibration with attack fingerprints, one per string and fret
        // instead (the noise fingerprint is always the last one).
        std::vector<std::vector<Entry>> templates;
        std::vector<float> dense;               // the same fingerprints as a full table [fingerprint * numBins + bin]
        std::vector<int> templatePitch;         // pitch of each fingerprint (numPitches = noise)
        std::vector<std::vector<int>> pitchTemplates; // the fingerprints of each pitch
        std::vector<float> act;                 // activation per fingerprint
        std::vector<float> x;                   // activation per pitch (its fingerprints added up; last = noise)
        std::vector<std::array<float, 4>> history;
        int historyPos = 0;
        std::vector<int> candidate;             // frames in a row a pitch looked like a new note
        std::vector<Note> notes;                // per pitch
        std::array<int, kMaxStrings> bend {};   // last pitch bend sent per string

        // Level and onsets
        float envelope = 0.0f, attackPeak = 0.0f, fluxAverage = 0.0f, level = 0.0f, levelPeak = 0.0f;
        int   sinceOnset = 100;     // frames since the last pick attack
        int   sinceBurst = 100;     // frames since the first attack of the current strum
        int   startedInBurst = 0;   // notes started in the current strum
        int   startedSinceOnset = 0;// notes started since the last pick attack
        bool  repickPending = false;
        std::vector<float> onsetX;  // activations just before the last pick attack
        int   handFret = 5;

        std::vector<Shape> shapes;              // common shapes + taught chords, for completing strummed chords

        // New notes this frame (fixed size: no allocation on the audio thread)
        std::array<int, 128> fresh {};

        std::array<Msg, 128> out {};
        int outCount = 0;

        // Readings of the one-note pitch detector (see hint): one clear note = no notes on its overtones.
        struct Hint { int64_t sample = 0; float midi = 0.0f, clarity = 0.0f; };
        std::array<Hint, 256> hints {};
        int hintWrite = 0, hintRead = 0;
        int64_t samplesIn = 0;
        int clearNote = -1, clearRun = 0;     // the note heard clearly, and for how many readings in a row
        bool overtoneOfClearNote (int p) const;
    };
}
