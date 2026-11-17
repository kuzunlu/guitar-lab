#include "Theory/Note.h"

#include <cctype>

namespace gl
{
    namespace
    {
        const char* const sharpNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const char* const flatNames[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

        // Reads a letter plus accidentals from text starting at pos. Moves pos past them.
        std::optional<PitchClass> readPitchClass (const std::string& text, size_t& pos)
        {
            if (pos >= text.size())
                return std::nullopt;

            static const int letterValues[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
            const char letter = (char) std::toupper ((unsigned char) text[pos]);

            if (letter < 'A' || letter > 'G')
                return std::nullopt;

            int value = letterValues[letter - 'A'];
            ++pos;

            while (pos < text.size())
            {
                const char c = text[pos];
                if (c == '#')                 { ++value; ++pos; }
                else if (c == 'b' && pos > 0) { --value; ++pos; }
                else break;
            }

            return value; // not wrapped: Cb = -1, B# = 12
        }
    }

    std::string pitchClassName (PitchClass pc, bool preferFlats)
    {
        return preferFlats ? flatNames[wrap12 (pc)] : sharpNames[wrap12 (pc)];
    }

    std::string midiNoteName (MidiNote n, bool preferFlats)
    {
        const int octave = (n / 12) - 1;
        return pitchClassName (pitchClassOf (n), preferFlats) + std::to_string (octave);
    }

    std::optional<PitchClass> parsePitchClass (const std::string& text)
    {
        size_t pos = 0;
        auto pc = readPitchClass (text, pos);
        if (! pc || pos != text.size())
            return std::nullopt;
        return wrap12 (*pc);
    }

    std::optional<MidiNote> parseMidiNote (const std::string& text)
    {
        size_t pos = 0;
        auto pc = readPitchClass (text, pos);
        if (! pc || pos >= text.size())
            return std::nullopt;

        // The accidental loop may have read "b" as a flat. Octave digits (and a minus sign) follow.
        try
        {
            size_t used = 0;
            const int octave = std::stoi (text.substr (pos), &used);
            if (pos + used != text.size())
                return std::nullopt;

            // Cb4 and B#3 cross octaves: compute from the letter, not the wrapped pitch class.
            const int note = (octave + 1) * 12 + *pc;
            if (note < 0 || note > 127)
                return std::nullopt;
            return note;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool keyPrefersFlats (PitchClass tonic, bool minor)
    {
        // Use the relative major for minor keys.
        const PitchClass major = minor ? wrap12 (tonic + 3) : wrap12 (tonic);
        switch (major)
        {
            case 5: case 10: case 3: case 8: case 1: case 6: return true; // F Bb Eb Ab Db Gb
            default: return false;
        }
    }
}
