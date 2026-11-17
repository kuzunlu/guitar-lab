#pragma once

#include <optional>
#include <string>

namespace gl
{
    // Pitch class: 0 = C, 1 = C#/Db ... 11 = B.
    using PitchClass = int;

    // MIDI note number: 60 = middle C (C4).
    using MidiNote = int;

    inline PitchClass pitchClassOf (MidiNote n) { return ((n % 12) + 12) % 12; }
    inline int wrap12 (int x)                   { return ((x % 12) + 12) % 12; }

    // "C", "C#", "Db" ... preferFlats picks Db over C#.
    std::string pitchClassName (PitchClass pc, bool preferFlats = false);

    // "E2", "Bb3" ... (C4 = 60).
    std::string midiNoteName (MidiNote n, bool preferFlats = false);

    // Parses "C", "c#", "Eb", "F##"... into a pitch class.
    std::optional<PitchClass> parsePitchClass (const std::string& text);

    // Parses "E2", "Bb1", "F#3" into a MIDI note.
    std::optional<MidiNote> parseMidiNote (const std::string& text);

    // Keys with flats in their signature (F, Bb, Eb, Ab, Db, Gb) read better with flat names.
    bool keyPrefersFlats (PitchClass tonic, bool minor);
}
