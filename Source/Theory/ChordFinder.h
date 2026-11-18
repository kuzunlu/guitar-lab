#pragma once

#include "Theory/Voicing.h"

#include <optional>
#include <string>
#include <vector>

namespace gl
{
    // One way to name a group of notes.
    struct ChordMatch
    {
        std::string name;      // "Cmaj7/E", "Am7", "G5", "C (no 5th)"...
        std::string meaning;   // "C major 7th, E in the bass"
        int root = 0;          // pitch class 0..11
        int bass = 0;          // pitch class of the lowest note
        int score = 0;         // higher = more likely (the first match is the best)
    };

    // Names the chord made by these notes (MIDI note numbers, any octave, doubles are fine).
    // Returns the possible names, best first. Empty when fewer than 2 different note names sound.
    // Example: { 52, 55, 60, 64 } (E G C E)  ->  "C/E", then other readings.
    std::vector<ChordMatch> nameChord (const std::vector<int>& midiNotes, bool preferFlats = false);

    // A chord symbol read back into notes (the other direction).
    struct ParsedChord
    {
        int root = 0;                  // pitch class
        int bass = -1;                 // slash chord bass ("D/F#"), -1 = none
        std::vector<int> intervals;    // above the root, root (0) first, then the 3rd (if any) ...
        std::vector<int> optional;     // intervals a guitarist may leave out (the 5th; the 9th/11th in big chords)
    };

    // Reads any usual chord symbol: C, Am, F#m7b5, Bbmaj9, G7#9, E7alt, Dadd9/F#, C6/9, Asus2, G13(#11),
    // Cm(maj7), D5, Bdim7, C-7, CM7, CΔ7, Cø, C°7, C+, C7(no3). Returns nothing if it cannot read it.
    std::optional<ParsedChord> parseChordName (const std::string& text);

    // What the shape finder needs for this chord.
    ShapeRequest shapeRequestFor (const ParsedChord& chord);

    // The usual fingerings guitarists play for this chord: open chords, barre chords on the E / A / D shapes and
    // common jazz shapes. Most played first (open chord, then barre shapes low on the neck, then the rest).
    struct CommonShape
    {
        Voicing voicing;
        int rank = 0;          // smaller = played more often
        std::string kind;      // "Open chord", "Barre, E shape", "Barre, A shape", "D shape", "Jazz shape"
    };
    std::vector<CommonShape> commonShapes (const ParsedChord& chord, const Instrument& inst);
}
