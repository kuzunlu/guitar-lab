#include "Theory/Instrument.h"

#include <algorithm>

namespace gl
{
    namespace
    {
        // Notes are written low to high (the way players say them), then flipped.
        TuningPreset make (std::string name, std::vector<MidiNote> lowToHigh)
        {
            std::reverse (lowToHigh.begin(), lowToHigh.end());
            return { std::move (name), std::move (lowToHigh) };
        }

        // MIDI numbers: E2=40 A2=45 D3=50 G3=55 B3=59 E4=64, B1=35, F#1=30, E1=28
    }

    std::vector<TuningPreset> tuningPresets (int numStrings)
    {
        switch (numStrings)
        {
            case 7:
                return {
                    make ("Standard (B E A D G B E)",      { 35, 40, 45, 50, 55, 59, 64 }),
                    make ("Drop A (A E A D G B E)",        { 33, 40, 45, 50, 55, 59, 64 }),
                    make ("A Standard (A D G C F A D)",    { 33, 38, 43, 48, 53, 57, 62 }),
                    make ("Bb Standard (Bb Eb Ab Db Gb Bb Eb)", { 34, 39, 44, 49, 54, 58, 63 }),
                    make ("Drop G# (G# D# G# C# F# A# D#)", { 32, 39, 44, 49, 54, 58, 63 }),
                };

            case 8:
                return {
                    make ("Standard (F# B E A D G B E)",   { 30, 35, 40, 45, 50, 55, 59, 64 }),
                    make ("Drop E (E B E A D G B E)",      { 28, 35, 40, 45, 50, 55, 59, 64 }),
                    make ("E Standard (E A D G C F A D)",  { 28, 33, 38, 43, 48, 53, 57, 62 }),
                    make ("F Standard (F Bb Eb Ab Db Gb Bb Eb)", { 29, 34, 39, 44, 49, 54, 58, 63 }),
                };

            default:
                return {
                    make ("Standard (E A D G B E)",        { 40, 45, 50, 55, 59, 64 }),
                    make ("Eb Standard",                   { 39, 44, 49, 54, 58, 63 }),
                    make ("D Standard",                    { 38, 43, 48, 53, 57, 62 }),
                    make ("Drop D (D A D G B E)",          { 38, 45, 50, 55, 59, 64 }),
                    make ("Drop C (C G C F A D)",          { 36, 43, 48, 53, 57, 62 }),
                    make ("Drop B (B F# B E G# C#)",       { 35, 42, 47, 52, 56, 61 }),
                    make ("Open G (D G D G B D)",          { 38, 43, 50, 55, 59, 62 }),
                    make ("Open D (D A D F# A D)",         { 38, 45, 50, 54, 57, 62 }),
                    make ("Open E (E B E G# B E)",         { 40, 47, 52, 56, 59, 64 }),
                    make ("DADGAD",                        { 38, 45, 50, 55, 57, 62 }),
                    make ("All Fourths (E A D G C F)",     { 40, 45, 50, 55, 60, 65 }),
                };
        }
    }

    Instrument::Instrument()
    {
        setNumStrings (6);
    }

    bool Instrument::setTuning (const std::vector<MidiNote>& newOpenNotes)
    {
        if ((int) newOpenNotes.size() < kMinStrings || (int) newOpenNotes.size() > kMaxStrings)
            return false;

        openNotes = newOpenNotes;
        for (auto& n : openNotes)
            n = std::clamp (n, 0, 127 - kNumFrets);
        return true;
    }

    void Instrument::setStringOpenNote (int string, MidiNote note)
    {
        if (string >= 0 && string < numStrings())
            openNotes[(size_t) string] = std::clamp (note, 0, 127 - kNumFrets);
    }

    void Instrument::setNumStrings (int count)
    {
        count = std::clamp (count, kMinStrings, kMaxStrings);
        setTuning (tuningPresets (count).front().openNotes);
    }

    void Instrument::transposeAll (int semitones)
    {
        for (auto& n : openNotes)
            n = std::clamp (n + semitones, 0, 127 - kNumFrets);
    }

    bool Instrument::isValid (FretPos p) const
    {
        return p.string >= 0 && p.string < numStrings() && p.fret >= 0 && p.fret <= kNumFrets;
    }

    std::vector<FretPos> Instrument::positionsOf (MidiNote note) const
    {
        std::vector<FretPos> result;
        for (int s = 0; s < numStrings(); ++s)
        {
            const int fret = note - openNotes[(size_t) s];
            if (fret >= 0 && fret <= kNumFrets)
                result.push_back ({ s, fret });
        }
        return result;
    }

    std::vector<FretPos> Instrument::positionsOfPitchClass (PitchClass pc) const
    {
        std::vector<FretPos> result;
        for (int s = 0; s < numStrings(); ++s)
            for (int f = 0; f <= kNumFrets; ++f)
                if (pitchClassOf (openNotes[(size_t) s] + f) == wrap12 (pc))
                    result.push_back ({ s, f });
        return result;
    }

    std::string Instrument::tuningLabel (bool preferFlats) const
    {
        std::string label;
        for (int s = numStrings() - 1; s >= 0; --s)
        {
            label += pitchClassName (pitchClassOf (openNotes[(size_t) s]), preferFlats);
            if (s > 0)
                label += ' ';
        }
        return label;
    }
}
