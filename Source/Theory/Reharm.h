#pragma once

#include "Theory/Chord.h"

#include <string>
#include <vector>

namespace gl
{
    // A chord with a length in beats (4 beats = one bar of 4/4).
    struct TimedChord
    {
        Chord  chord;
        double beats = 4.0;
        bool   substituted = false; // true when the reharm engine created or changed it
        std::string reason;         // short explanation, empty for original chords
        std::string text;           // the chord as the user typed it ("vi", "Em7"), empty for engine chords
    };

    enum class SubKind
    {
        TritoneSub,         // V7 -> bII7
        SecondaryDominant,  // put V7 of the next chord before it
        BorrowedChord,      // take a chord from the parallel minor/major key
        PassingDiminished,  // dim7 chord a half step below the next chord
        RelatedTwoFive,     // ii7 - V7 of the next chord
        DiatonicSub,        // chord from the same key that shares two notes (I <-> vi, IV <-> ii)
        BackdoorDominant,   // bVII7 resolving to I
        Extension,          // add 7ths and 9ths
        NumKinds
    };

    std::string subKindName (SubKind k);
    std::string subKindDescription (SubKind k); // long text, used for tooltips

    struct Suggestion
    {
        SubKind kind;
        int     index = 0;            // chord the suggestion acts on
        bool    insertsBefore = false;// true: new chords are inserted BEFORE chord[index] and steal half its time
        std::vector<TimedChord> replacement; // chords that take the place of chord[index] (or are inserted)
        std::string explanation;
    };

    // Every single substitution the engine can find for this progression.
    std::vector<Suggestion> findSuggestions (const std::vector<TimedChord>& progression, const Key& key);

    // Applies one suggestion. Returns the new progression.
    std::vector<TimedChord> applySuggestion (const std::vector<TimedChord>& progression, const Suggestion& s);

    struct ReharmOptions
    {
        bool     enabled[(int) SubKind::NumKinds] = { true, true, true, true, true, true, true, true };
        double   amount = 0.5;   // 0 = change nothing, 1 = change as much as possible
        unsigned seed   = 1;     // same seed + same input = same result
    };

    // Builds a full reharmonized version. Each chord gets at most one change.
    std::vector<TimedChord> reharmonize (const std::vector<TimedChord>& progression, const Key& key, const ReharmOptions& options);

    // Helper: wraps plain chords as one bar each.
    std::vector<TimedChord> toTimed (const std::vector<Chord>& chords, double beatsEach = 4.0);

    // Like parseProgression, with lengths. "C:2" = 2 beats (any length, like G:1.5). Between bar lines
    // ("| C G | Am |") the chords share the 4-beat bar equally, after the ones with their own length.
    // Without bar lines every chord without a length gets one bar (4 beats).
    std::vector<TimedChord> parseTimedProgression (const std::string& text, const Key& key, std::string* errorOut = nullptr);

    // The other way: each chord as typed (or its name), with ":beats" when it is not 4 beats long.
    std::string progressionToText (const std::vector<TimedChord>& chords, bool preferFlats);
}
