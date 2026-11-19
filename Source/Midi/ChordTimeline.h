#pragma once

#include "Midi/Phrase.h"

#include <string>
#include <vector>

namespace gl
{
    // A chord over a stretch of the phrase.
    struct ChordSpan
    {
        double start = 0.0, end = 0.0;   // beats
        std::string name;                // "Am7", "C/E", "A5"
        std::string meaning;             // "A minor 7th. Notes: ..."
    };

    // Names the harmony of a phrase over time. Looks at every 2 beats (or every beat, when the chord changes on
    // the beat), so arpeggios count as their chord. Single notes (melody) give no chord.
    std::vector<ChordSpan> chordTimeline (const Phrase& phrase, const Instrument& inst, bool flats = false);

    // The chords as one line for the Reharm Lab ("Am F:2 C:6 G"): names it can read, without slash basses, with
    // ":beats" when a chord is not one bar (4 beats) long. The same chord twice in a row is one longer chord.
    std::string chordsForReharm (const std::vector<ChordSpan>& spans);
}
