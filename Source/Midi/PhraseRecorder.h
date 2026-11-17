#pragma once

#include "Midi/Phrase.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gl
{
    // Turns the tracker's MIDI (one channel per string, with pitch bend) back into phrase notes:
    // string + fret, timing, velocity, hammer-ons / pull-offs, bends, slides, vibrato and chords.
    class PhraseRecorder
    {
    public:
        struct Settings
        {
            int    firstChannel = 2;    // MIDI channel of the highest string
            int    bendRange = 12;      // semitones of a full pitch bend
            double bpm = 120.0;         // for vibrato speed (Hz)
            double snapBeats = 0.0;     // 0 = no snapping; 0.25 = 1/16 ...
            double startAt = 0.0;       // phrase beat where the first note goes
            bool   alignToBars = false; // true: keep the position inside the bar (DAW was playing)
        };

        void start (const Instrument& inst, const Settings& settings);
        void setAlignToBars (bool b) { settings.alignToBars = b; } // known only when the first event arrives

        // beat: time of the message on the recording clock. Only the tracker's messages belong here.
        void add (double beat, uint8_t status, uint8_t data1, uint8_t data2);

        // Ends notes still sounding, and returns all recorded notes (already moved to startAt).
        std::vector<NoteEvent> finish (double endBeat);

        int  notesSoFar() const { return (int) done.size() + countActive(); }
        bool hasStarted() const { return origin >= 0.0; }

    private:
        struct Active
        {
            bool   on = false;
            int    note = 0;
            int    velocity = 100;
            double start = 0.0;
            Technique technique = Technique::Normal;
            int    slideTo = -1;   // the next note was reached by gliding: this note slides into it
            std::vector<std::pair<double, double>> bends; // (beat, semitones)
        };

        void close (int string, double beat);
        int  countActive() const;
        NoteEvent makeNote (int string, const Active& a, double end) const;

        Instrument inst;
        Settings settings;
        std::array<Active, kMaxStrings> active {};
        std::array<double, kMaxStrings> pendingBend {}; // bend sent just before a note-on
        std::vector<NoteEvent> done;
        double origin = -1.0;        // recording-clock beat of the first note
    };
}
