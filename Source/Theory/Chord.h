#pragma once

#include "Theory/Note.h"

#include <optional>
#include <string>
#include <vector>

namespace gl
{
    enum class ChordQuality
    {
        Major, Minor, Diminished, Augmented,
        Major7, Minor7, Dominant7, HalfDiminished7, Diminished7,
        Sus2, Sus4, Dominant9, Minor9, Major9, Dominant7b9, Dominant7Sharp9
    };

    struct Chord
    {
        PitchClass   root = 0;
        ChordQuality quality = ChordQuality::Major;

        // Intervals above the root, in semitones (root = 0 is included).
        std::vector<int> intervals() const;
        std::vector<PitchClass> pitchClasses() const;

        // "Cmaj7", "F#m7b5", "Bb7" ...
        std::string name (bool preferFlats = false) const;

        bool operator== (const Chord&) const = default;
    };

    std::string qualitySuffix (ChordQuality q);

    // Parses a chord symbol like "C", "Am7", "F#m7b5", "Bbmaj7", "G7b9", "Edim7", "Dsus4".
    std::optional<Chord> parseChordSymbol (const std::string& text);

    // A key: tonic + major or minor (natural minor for diatonic chords).
    struct Key
    {
        PitchClass tonic = 0;
        bool minor = false;

        std::string name() const;
        bool prefersFlats() const { return keyPrefersFlats (tonic, minor); }

        // Scale notes, degree 0..6.
        std::vector<PitchClass> scale() const;

        // Diatonic chord on a degree (0..6). sevenths = add the 7th.
        Chord diatonicChord (int degree, bool sevenths) const;
    };

    // Parses Roman numerals relative to a key: "I", "vi", "IV", "V7", "bVII", "ii7", "viio", "iiø7", "V/V".
    std::optional<Chord> parseRomanNumeral (const std::string& text, const Key& key);

    // Parses a whole progression. Tokens split by spaces, commas, dashes or "|".
    // Each token can be a Roman numeral or a chord symbol.
    std::vector<Chord> parseProgression (const std::string& text, const Key& key, std::string* errorOut = nullptr);

    // Roman numeral label for a chord in a key ("bVII7", "V/ii" not tried — plain degree only).
    std::string romanLabel (const Chord& chord, const Key& key);
}
