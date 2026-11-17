#pragma once

#include "Theory/Chord.h"
#include "Theory/Instrument.h"

#include <vector>

namespace gl
{
    constexpr int kMuted = -1;

    // One fret per string (index 0 = highest string). kMuted = string not played.
    struct Voicing
    {
        std::vector<int> frets;

        int lowestFrettedFret() const; // ignores open strings; 0 if all open
        int highestFret() const;
        int span() const;              // highest - lowest fretted fret
        int numPlayed() const;
        std::vector<FretPos> positions() const;
        std::vector<MidiNote> notes (const Instrument& inst) const; // low to high

        bool operator== (const Voicing&) const = default;
    };

    struct VoicingOptions
    {
        int  maxSpan          = 4;     // frets the hand must stretch (4 = normal, 5 = wide)
        int  minStrings       = 3;
        int  maxStrings       = 6;     // even on 8 strings, more than 6 notes gets muddy
        bool rootInBass       = true;  // lowest note must be the root
        bool allowOpenStrings = true;
        bool allowInnerMutes  = true;  // skip a string in the middle (jazz style)
        int  minFret          = 0;     // search only this part of the neck
        int  maxFret          = kNumFrets;
        bool dropFifthIfNeeded = true; // the 5th may be left out of 7th/9th chords
    };

    // All playable voicings, best first. Every result has the root, 3rd and 7th (when the chord has one).
    std::vector<Voicing> findVoicings (const Chord& chord, const Instrument& inst, const VoicingOptions& options, int maxResults = 40);

    // Picks one voicing per chord so the hand moves as little as possible.
    // startFret: the area of the neck to start near.
    std::vector<Voicing> voiceLead (const std::vector<Chord>& chords, const Instrument& inst,
                                    const VoicingOptions& options, int startFret = 3);

    // Text like "x32010" (low to high). Frets above 9 are written in brackets: "x(10)(12)..."
    std::string voicingToText (const Voicing& v);
}
