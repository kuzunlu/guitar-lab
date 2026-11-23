#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Calibration.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace gl
{
    // The sound of every fret of every string, recorded during a Full calibration. Poly learns from it: mixed
    // together, these notes make chords, arpeggios and melodies whose right answer is known.
    struct FretRecording
    {
        int string = 0, fret = 0;
        std::vector<float> samples;   // from ~20 ms before the pick, until the next pick or the end
    };

    struct CalibrationAudio
    {
        double rate = 0.0;            // of the stored samples (the input rate divided down to ~24 kHz)
        std::vector<FretRecording> notes;

        // Stores the audio around one fret's note (one recording per string and fret: a new one replaces the old one).
        // The audio may start well before the pick and may hold the next pick at the end (see findPick).
        void put (int string, int fret, const float* samples, int count, double sampleRate);
        const FretRecording* find (int string, int fret) const;
        bool empty() const { return notes.empty(); }
        void clear() { notes.clear(); rate = 0.0; }

        bool save (const std::filesystem::path& path) const;   // small binary file (16-bit samples)
        bool load (const std::filesystem::path& path);
    };

    // The note in a recording: [start, end) from ~20 ms before the strongest pick (in all but the last `tailSeconds`)
    // to just before the next pick after it. start < 0 when there is no clear pick.
    struct PickSpan { int start = -1, end = 0; };
    PickSpan findPick (const float* samples, int count, double rate, double tailSeconds);

    // Learning Poly's note scorer (Calibration::net) for one guitar. The recorded frets are mixed into chords,
    // arpeggios and melodies (the right answer is known), Poly listens to them, and a small network learns which
    // possible new notes were real. Then its strictness is set on another mix ("balanced").
    struct LearnSettings
    {
        double mixSeconds = 200.0;     // each of the two training mixes
        double checkSeconds = 90.0;    // the mix that sets the strictness
        int    hidden = 24;
        int    epochs = 20;
        double keepFakes = 0.2;        // share of the (many, easy) not-a-note examples used; they weigh 1 / keepFakes
        double balance = 1.0;          // added to the strictness found on the check mix: > 0 = more notes
        unsigned seed = 1;
    };

    // A mix of the recorded notes and its right answer (used by learnNoteNet; public for tests and the benchmark).
    struct MixNote { double onset = 0.0, offset = 0.0; int pitch = 0, string = 0, fret = 0; };
    struct LearnMix { std::vector<float> audio; double rate = 0.0; std::vector<MixNote> truth; };

    // Slow (tens of seconds): call it on a background thread. progress (0..1) returns false to stop: the result is
    // then empty (as it is when there are too few recordings). Uses its own trackers: safe next to the audio thread.
    // `playing`: real playing with known notes (chords, octaves...), learned from as well as the mixes.
    NoteNet learnNoteNet (const CalibrationAudio& audio, const Instrument& inst, const Calibration& cal,
                          const std::function<bool (double)>& progress = {}, const LearnSettings& settings = {},
                          const std::vector<LearnMix>& playing = {});
    LearnMix makeLearnMix (const CalibrationAudio& audio, const Instrument& inst, unsigned seed, double seconds);
}
