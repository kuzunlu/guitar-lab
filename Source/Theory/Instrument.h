#pragma once

#include "Theory/Note.h"

#include <string>
#include <vector>

namespace gl
{
    constexpr int kMinStrings = 6;
    constexpr int kMaxStrings = 8;
    constexpr int kNumFrets   = 24;

    // A spot on the neck. String 0 is the HIGHEST-pitched string (like tab: top line = high E).
    struct FretPos
    {
        int string = 0;
        int fret   = 0;

        bool operator== (const FretPos&) const = default;
    };

    struct TuningPreset
    {
        std::string name;
        std::vector<MidiNote> openNotes; // index 0 = highest string
    };

    // Built-in tunings for a given string count.
    std::vector<TuningPreset> tuningPresets (int numStrings);

    class Instrument
    {
    public:
        Instrument();

        int  numStrings() const { return (int) openNotes.size(); }
        int  numFrets() const   { return kNumFrets; }

        // Replaces the tuning. Size must be 6..8, otherwise it is ignored and false is returned.
        bool setTuning (const std::vector<MidiNote>& newOpenNotes);
        const std::vector<MidiNote>& tuning() const { return openNotes; }

        // Changes one string's open note (custom tuning).
        void setStringOpenNote (int string, MidiNote note);

        // Switch string count: loads the first ("Standard") preset for that count.
        void setNumStrings (int count);

        // Shifts every string by semitones (for example -1 = "Eb standard" from E standard).
        void transposeAll (int semitones);

        MidiNote noteAt (FretPos p) const { return openNotes[(size_t) p.string] + p.fret; }
        bool     isValid (FretPos p) const;

        // All places on the neck where this exact MIDI note can be played.
        std::vector<FretPos> positionsOf (MidiNote note) const;

        // All places where this pitch class appears (any octave).
        std::vector<FretPos> positionsOfPitchClass (PitchClass pc) const;

        // "E A D G B E" style label, low to high.
        std::string tuningLabel (bool preferFlats = false) const;

    private:
        std::vector<MidiNote> openNotes;
    };
}
