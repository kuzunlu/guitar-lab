#include "Theory/Chord.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace gl
{
    namespace
    {
        const char* const kHalfDimUtf8 = "\xC3\xB8"; // "ø"
        const char* const kDeltaUtf8   = "\xCE\x94"; // "Δ"

        // Reads letter + accidentals ("C", "F#", "Bb"). Returns raw value (not wrapped).
        std::optional<int> readRoot (const std::string& s, size_t& pos)
        {
            static const int letterValues[7] = { 9, 11, 0, 2, 4, 5, 7 };
            if (pos >= s.size())
                return std::nullopt;

            const char letter = (char) std::toupper ((unsigned char) s[pos]);
            if (letter < 'A' || letter > 'G')
                return std::nullopt;

            int v = letterValues[letter - 'A'];
            ++pos;
            while (pos < s.size() && (s[pos] == '#' || s[pos] == 'b'))
                v += (s[pos++] == '#') ? 1 : -1;
            return v;
        }

        const std::map<std::string, ChordQuality>& suffixTable()
        {
            static const std::map<std::string, ChordQuality> table = {
                { "",      ChordQuality::Major },      { "M",     ChordQuality::Major },
                { "maj",   ChordQuality::Major },
                { "m",     ChordQuality::Minor },      { "min",   ChordQuality::Minor },
                { "-",     ChordQuality::Minor },
                { "dim",   ChordQuality::Diminished }, { "o",     ChordQuality::Diminished },
                { "aug",   ChordQuality::Augmented },  { "+",     ChordQuality::Augmented },
                { "maj7",  ChordQuality::Major7 },     { "M7",    ChordQuality::Major7 },
                { kDeltaUtf8, ChordQuality::Major7 },  { std::string (kDeltaUtf8) + "7", ChordQuality::Major7 },
                { "m7",    ChordQuality::Minor7 },     { "min7",  ChordQuality::Minor7 },
                { "-7",    ChordQuality::Minor7 },
                { "7",     ChordQuality::Dominant7 },  { "dom7",  ChordQuality::Dominant7 },
                { "m7b5",  ChordQuality::HalfDiminished7 },
                { kHalfDimUtf8, ChordQuality::HalfDiminished7 },
                { std::string (kHalfDimUtf8) + "7", ChordQuality::HalfDiminished7 },
                { "dim7",  ChordQuality::Diminished7 },{ "o7",    ChordQuality::Diminished7 },
                { "sus2",  ChordQuality::Sus2 },
                { "sus4",  ChordQuality::Sus4 },       { "sus",   ChordQuality::Sus4 },
                { "9",     ChordQuality::Dominant9 },
                { "m9",    ChordQuality::Minor9 },
                { "maj9",  ChordQuality::Major9 },     { "M9",    ChordQuality::Major9 },
                { "7b9",   ChordQuality::Dominant7b9 },
                { "7#9",   ChordQuality::Dominant7Sharp9 },
            };
            return table;
        }

        // Suffix for a lowercase Roman numeral (minor family).
        std::optional<ChordQuality> lowerNumeralQuality (const std::string& suffix)
        {
            if (suffix.empty())                                   return ChordQuality::Minor;
            if (suffix == "7")                                    return ChordQuality::Minor7;
            if (suffix == "9")                                    return ChordQuality::Minor9;
            if (suffix == "o" || suffix == "dim")                 return ChordQuality::Diminished;
            if (suffix == "o7" || suffix == "dim7")               return ChordQuality::Diminished7;
            if (suffix == kHalfDimUtf8 || suffix == std::string (kHalfDimUtf8) + "7"
                || suffix == "7b5" || suffix == "m7b5")           return ChordQuality::HalfDiminished7;
            return std::nullopt;
        }

        // Reads "I".."VII" (any case). Returns degree 0..6 and whether it was uppercase.
        bool readNumeral (const std::string& s, size_t& pos, int& degree, bool& upper)
        {
            static const char* const numerals[7] = { "VII", "VI", "IV", "V", "III", "II", "I" };
            static const int degrees[7]          = { 6,     5,    3,    4,   2,     1,    0   };

            for (int i = 0; i < 7; ++i)
            {
                const std::string n = numerals[i];
                if (pos + n.size() > s.size())
                    continue;

                std::string part = s.substr (pos, n.size());
                std::string partUpper = part;
                for (auto& c : partUpper) c = (char) std::toupper ((unsigned char) c);

                if (partUpper != n)
                    continue;

                const bool allUpper = std::all_of (part.begin(), part.end(), [] (char c) { return std::isupper ((unsigned char) c); });
                const bool allLower = std::all_of (part.begin(), part.end(), [] (char c) { return std::islower ((unsigned char) c); });
                if (! allUpper && ! allLower)
                    continue;

                degree = degrees[i];
                upper  = allUpper;
                pos   += n.size();
                return true;
            }
            return false;
        }

        const char* const kNumeralNames[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

        bool isMinorFamily (ChordQuality q)
        {
            switch (q)
            {
                case ChordQuality::Minor: case ChordQuality::Minor7: case ChordQuality::Minor9:
                case ChordQuality::Diminished: case ChordQuality::Diminished7:
                case ChordQuality::HalfDiminished7:
                    return true;
                default:
                    return false;
            }
        }
    }

    // -----------------------------------------------------------------------
    std::vector<int> Chord::intervals() const
    {
        switch (quality)
        {
            case ChordQuality::Major:           return { 0, 4, 7 };
            case ChordQuality::Minor:           return { 0, 3, 7 };
            case ChordQuality::Diminished:      return { 0, 3, 6 };
            case ChordQuality::Augmented:       return { 0, 4, 8 };
            case ChordQuality::Major7:          return { 0, 4, 7, 11 };
            case ChordQuality::Minor7:          return { 0, 3, 7, 10 };
            case ChordQuality::Dominant7:       return { 0, 4, 7, 10 };
            case ChordQuality::HalfDiminished7: return { 0, 3, 6, 10 };
            case ChordQuality::Diminished7:     return { 0, 3, 6, 9 };
            case ChordQuality::Sus2:            return { 0, 2, 7 };
            case ChordQuality::Sus4:            return { 0, 5, 7 };
            case ChordQuality::Dominant9:       return { 0, 4, 7, 10, 14 };
            case ChordQuality::Minor9:          return { 0, 3, 7, 10, 14 };
            case ChordQuality::Major9:          return { 0, 4, 7, 11, 14 };
            case ChordQuality::Dominant7b9:     return { 0, 4, 7, 10, 13 };
            case ChordQuality::Dominant7Sharp9: return { 0, 4, 7, 10, 15 };
        }
        return { 0, 4, 7 };
    }

    std::vector<PitchClass> Chord::pitchClasses() const
    {
        std::vector<PitchClass> pcs;
        for (int i : intervals())
            pcs.push_back (wrap12 (root + i));
        return pcs;
    }

    std::string qualitySuffix (ChordQuality q)
    {
        switch (q)
        {
            case ChordQuality::Major:           return "";
            case ChordQuality::Minor:           return "m";
            case ChordQuality::Diminished:      return "dim";
            case ChordQuality::Augmented:       return "aug";
            case ChordQuality::Major7:          return "maj7";
            case ChordQuality::Minor7:          return "m7";
            case ChordQuality::Dominant7:       return "7";
            case ChordQuality::HalfDiminished7: return "m7b5";
            case ChordQuality::Diminished7:     return "dim7";
            case ChordQuality::Sus2:            return "sus2";
            case ChordQuality::Sus4:            return "sus4";
            case ChordQuality::Dominant9:       return "9";
            case ChordQuality::Minor9:          return "m9";
            case ChordQuality::Major9:          return "maj9";
            case ChordQuality::Dominant7b9:     return "7b9";
            case ChordQuality::Dominant7Sharp9: return "7#9";
        }
        return "";
    }

    std::string Chord::name (bool preferFlats) const
    {
        return pitchClassName (root, preferFlats) + qualitySuffix (quality);
    }

    std::optional<Chord> parseChordSymbol (const std::string& text)
    {
        size_t pos = 0;
        auto root = readRoot (text, pos);
        if (! root)
            return std::nullopt;

        const auto& table = suffixTable();
        auto it = table.find (text.substr (pos));
        if (it == table.end())
            return std::nullopt;

        return Chord { wrap12 (*root), it->second };
    }

    // -----------------------------------------------------------------------
    std::string Key::name() const
    {
        return pitchClassName (tonic, prefersFlats()) + (minor ? " minor" : " major");
    }

    std::vector<PitchClass> Key::scale() const
    {
        static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };

        std::vector<PitchClass> notes;
        for (int i = 0; i < 7; ++i)
            notes.push_back (wrap12 (tonic + (minor ? minorSteps[i] : majorSteps[i])));
        return notes;
    }

    Chord Key::diatonicChord (int degree, bool sevenths) const
    {
        const auto sc = scale();
        degree = ((degree % 7) + 7) % 7;

        const int r       = sc[(size_t) degree];
        const int third   = wrap12 (sc[(size_t) ((degree + 2) % 7)] - r);
        const int fifth   = wrap12 (sc[(size_t) ((degree + 4) % 7)] - r);
        const int seventh = wrap12 (sc[(size_t) ((degree + 6) % 7)] - r);

        ChordQuality q = ChordQuality::Major;

        if (third == 4 && fifth == 7)       q = sevenths ? (seventh == 11 ? ChordQuality::Major7 : ChordQuality::Dominant7) : ChordQuality::Major;
        else if (third == 3 && fifth == 7)  q = sevenths ? ChordQuality::Minor7 : ChordQuality::Minor;
        else if (third == 3 && fifth == 6)  q = sevenths ? (seventh == 9 ? ChordQuality::Diminished7 : ChordQuality::HalfDiminished7) : ChordQuality::Diminished;
        else if (third == 4 && fifth == 8)  q = ChordQuality::Augmented;

        return { r, q };
    }

    // -----------------------------------------------------------------------
    std::optional<Chord> parseRomanNumeral (const std::string& text, const Key& key)
    {
        // Secondary chords: "V/V", "vii°/ii" -> left part is read in the key of the target chord.
        if (auto slash = text.find ('/'); slash != std::string::npos)
        {
            auto target = parseRomanNumeral (text.substr (slash + 1), key);
            if (! target)
                return std::nullopt;

            const bool targetMinor = isMinorFamily (target->quality);
            return parseRomanNumeral (text.substr (0, slash), Key { target->root, targetMinor });
        }

        size_t pos = 0;
        int shift = 0;
        while (pos < text.size() && (text[pos] == 'b' || text[pos] == '#'))
            shift += (text[pos++] == '#') ? 1 : -1;

        int degree = 0;
        bool upper = false;
        if (! readNumeral (text, pos, degree, upper))
            return std::nullopt;

        const std::string suffix = text.substr (pos);
        const PitchClass root = wrap12 (key.scale()[(size_t) degree] + shift);

        if (upper)
        {
            const auto& table = suffixTable();
            auto it = table.find (suffix);
            if (it == table.end())
                return std::nullopt;
            return Chord { root, it->second };
        }

        auto q = lowerNumeralQuality (suffix);
        if (! q)
            return std::nullopt;
        return Chord { root, *q };
    }

    std::vector<Chord> parseProgression (const std::string& text, const Key& key, std::string* errorOut)
    {
        std::vector<Chord> chords;
        std::string token;

        auto flush = [&]() -> bool
        {
            if (token.empty())
                return true;

            // Roman numerals first ("vi" must not be read as a chord symbol); then chord symbols.
            auto chord = parseRomanNumeral (token, key);
            if (! chord)
                chord = parseChordSymbol (token);

            if (! chord)
            {
                if (errorOut != nullptr)
                    *errorOut = "Could not read \"" + token + "\"";
                return false;
            }

            chords.push_back (*chord);
            token.clear();
            return true;
        };

        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];

            // "-" separates chords ("I-V-vi-IV"), except "C-7" where it means minor 7.
            if (c == '-')
            {
                const bool minorSeven = i + 1 < text.size() && text[i + 1] == '7'
                                        && ! parseRomanNumeral (token, key) && parseChordSymbol (token);
                if (! minorSeven)
                {
                    if (! flush()) return {};
                    continue;
                }
            }

            if (c == ' ' || c == ',' || c == '|' || c == '\t' || c == '\n'
                || (c == '\xE2' && i + 2 < text.size() && text[i + 1] == '\x80' && (text[i + 2] == '\x93' || text[i + 2] == '\x94'))) // en/em dash
            {
                if (c == '\xE2') i += 2;
                if (! flush()) return {};
                continue;
            }

            token += c;
        }

        if (! flush())
            return {};

        if (chords.empty() && errorOut != nullptr)
            *errorOut = "No chords found";

        return chords;
    }

    std::string romanLabel (const Chord& chord, const Key& key)
    {
        const auto sc = key.scale();

        int degree = -1;
        std::string prefix;

        for (int d = 0; d < 7 && degree < 0; ++d)
            if (sc[(size_t) d] == chord.root)
                degree = d;

        for (int d = 0; d < 7 && degree < 0; ++d)
            if (wrap12 (sc[(size_t) d] - 1) == chord.root) { degree = d; prefix = "b"; }

        for (int d = 0; d < 7 && degree < 0; ++d)
            if (wrap12 (sc[(size_t) d] + 1) == chord.root) { degree = d; prefix = "#"; }

        if (degree < 0)
            return chord.name (key.prefersFlats());

        std::string numeral = kNumeralNames[degree];
        if (isMinorFamily (chord.quality))
            for (auto& c : numeral) c = (char) std::tolower ((unsigned char) c);

        std::string suffix;
        switch (chord.quality)
        {
            case ChordQuality::Major: case ChordQuality::Minor: break;
            case ChordQuality::Diminished:      suffix = "o"; break;
            case ChordQuality::Diminished7:     suffix = "o7"; break;
            case ChordQuality::HalfDiminished7: suffix = std::string (kHalfDimUtf8) + "7"; break;
            case ChordQuality::Minor7:          suffix = "7"; break;
            case ChordQuality::Minor9:          suffix = "9"; break;
            default:                            suffix = qualitySuffix (chord.quality); break;
        }

        return prefix + numeral + suffix;
    }
}
