#include "Theory/Reharm.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace gl
{
    namespace
    {
        bool isDominantQuality (ChordQuality q)
        {
            return q == ChordQuality::Dominant7 || q == ChordQuality::Dominant9
                || q == ChordQuality::Dominant7b9 || q == ChordQuality::Dominant7Sharp9;
        }

        bool isMinorQuality (ChordQuality q)
        {
            return q == ChordQuality::Minor || q == ChordQuality::Minor7 || q == ChordQuality::Minor9;
        }

        bool isDiminishedQuality (ChordQuality q)
        {
            return q == ChordQuality::Diminished || q == ChordQuality::Diminished7 || q == ChordQuality::HalfDiminished7;
        }

        bool hasSeventh (ChordQuality q)
        {
            return q != ChordQuality::Major && q != ChordQuality::Minor && q != ChordQuality::Diminished
                && q != ChordQuality::Augmented && q != ChordQuality::Sus2 && q != ChordQuality::Sus4;
        }

        // A chord "acts as a dominant" when it is a 7th chord, the major V of the key,
        // or a major triad that falls a 5th to the next chord.
        bool actsAsDominant (const Chord& c, const Chord& next, const Key& key)
        {
            if (isDominantQuality (c.quality))
                return true;
            return c.quality == ChordQuality::Major
                && (wrap12 (next.root - c.root) == 5 || wrap12 (c.root - key.tonic) == 7);
        }

        std::string n (const Chord& c, const Key& k) { return c.name (k.prefersFlats()); }
        std::string pc (PitchClass p, const Key& k)  { return pitchClassName (p, k.prefersFlats()); }

        TimedChord sub (Chord c, double beats, std::string reason)
        {
            return { c, beats, true, std::move (reason) };
        }
    }

    std::string subKindName (SubKind k)
    {
        switch (k)
        {
            case SubKind::TritoneSub:        return "Tritone sub";
            case SubKind::SecondaryDominant: return "Secondary dominant";
            case SubKind::BorrowedChord:     return "Borrowed chord";
            case SubKind::PassingDiminished: return "Passing diminished";
            case SubKind::RelatedTwoFive:    return "Related ii-V";
            case SubKind::DiatonicSub:       return "Diatonic sub";
            case SubKind::BackdoorDominant:  return "Backdoor dominant";
            case SubKind::Extension:         return "Extensions";
            case SubKind::NumKinds:          break;
        }
        return {};
    }

    std::string subKindDescription (SubKind k)
    {
        switch (k)
        {
            case SubKind::TritoneSub:
                return "Tritone substitution.\n"
                       "A dominant 7th chord (like G7) is swapped for the dominant 7th a tritone (6 half steps) away (Db7).\n"
                       "Both chords share the same two most important notes (the 3rd and the 7th: B and F), so it still pulls home.\n"
                       "The bass now moves down by a half step (Db -> C) instead of a 5th. Sounds jazzy and smooth.";
            case SubKind::SecondaryDominant:
                return "Secondary dominant.\n"
                       "Before a chord, play the dominant 7th chord that 'belongs' to it (its V7). Example in C: before Am, play E7.\n"
                       "This borrows the strong pull of V7 -> I and points the ear at the next chord.\n"
                       "It takes the second half of the chord before it.";
            case SubKind::BorrowedChord:
                return "Borrowed chord (modal interchange).\n"
                       "Take a chord from the parallel key: in C major, borrow from C minor (Fm, Ab, Bb, Eb, Dm7b5).\n"
                       "Same root key, darker colour. Classic examples: IV -> iv (F -> Fm), vi -> bVI (Am -> Ab), V -> bVII (G -> Bb).\n"
                       "In a minor key it borrows from the parallel major (iv -> IV, last i -> I = 'Picardy third').";
            case SubKind::PassingDiminished:
                return "Passing diminished chord.\n"
                       "A diminished 7th chord one half step below the next chord (C#dim7 before Dm).\n"
                       "It acts like a V7b9 without the root and makes the bass climb by half steps.\n"
                       "It takes the second half of the chord before it.";
            case SubKind::RelatedTwoFive:
                return "Related ii-V.\n"
                       "Before a chord, play its own ii7 and V7 (before F: Gm7 - C7).\n"
                       "This is the most common jazz move. It creates a short trip to a new key and back.\n"
                       "If the target chord is minor, the ii becomes half-diminished (m7b5).";
            case SubKind::DiatonicSub:
                return "Diatonic substitution.\n"
                       "Swap a chord for another chord from the same key that shares two of its three notes.\n"
                       "C (C E G) <-> Am (A C E) or Em (E G B). F <-> Dm.\n"
                       "Same job in the song, a different colour, and no notes outside the key.";
            case SubKind::BackdoorDominant:
                return "Backdoor dominant.\n"
                       "Instead of V7 -> I (G7 -> C), use bVII7 -> I (Bb7 -> C).\n"
                       "Bb7 is borrowed from C minor. It resolves 'through the back door': softer, a bit of a soul/gospel sound.";
            case SubKind::Extension:
                return "Extensions.\n"
                       "Add colour notes without changing the chord's job: C -> Cmaj7, Dm -> Dm7 or Dm9, G7 -> G9 or G7b9.\n"
                       "7b9 is used when the next chord is minor, because the b9 note belongs to that minor key.";
            case SubKind::NumKinds:
                break;
        }
        return {};
    }

    std::vector<TimedChord> toTimed (const std::vector<Chord>& chords, double beatsEach)
    {
        std::vector<TimedChord> out;
        for (const auto& c : chords)
            out.push_back ({ c, beatsEach, false, {} });
        return out;
    }

    std::vector<TimedChord> parseTimedProgression (const std::string& text, const Key& key, std::string* errorOut)
    {
        constexpr double kBar = 4.0;
        const bool bars = text.find ('|') != std::string::npos;
        std::vector<TimedChord> out;

        // Bars first (the whole text is one piece when there are no bar lines), then words.
        std::vector<std::string> pieces (1);
        for (char c : text)
            if (c == '|') pieces.emplace_back();
            else pieces.back() += c;

        for (const auto& piece : pieces)
        {
            std::vector<TimedChord> bar;     // beats <= 0: no length typed
            std::string word;
            auto flushWord = [&]() -> bool
            {
                if (word.empty()) return true;
                std::string name = word;
                double beats = 0.0;
                const auto colon = word.rfind (':');
                if (colon != std::string::npos)
                {
                    name = word.substr (0, colon);
                    const std::string number = word.substr (colon + 1);
                    char* end = nullptr;
                    beats = std::strtod (number.c_str(), &end);
                    if (number.empty() || end != number.c_str() + number.size() || ! (beats >= 0.25) || beats > 256.0)
                    {
                        if (errorOut != nullptr) *errorOut = "Could not read the length in \"" + word + "\" (write e.g. C:2 for 2 beats)";
                        return false;
                    }
                }
                std::string err;
                const auto chords = parseProgression (name, key, &err);
                if (chords.empty())
                {
                    if (errorOut != nullptr) *errorOut = err;
                    return false;
                }
                // "I-V-vi" is one word with three chords: the typed text only fits a single chord.
                for (const auto& c : chords)
                    bar.push_back ({ c, beats, false, {}, chords.size() == 1 ? name : std::string() });
                word.clear();
                return true;
            };

            for (char c : piece)
            {
                if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r')
                {
                    if (! flushWord()) return {};
                }
                else
                    word += c;
            }
            if (! flushWord()) return {};

            double typed = 0.0;
            int open = 0;
            for (const auto& tc : bar)
                if (tc.beats > 0.0) typed += tc.beats; else ++open;
            // In a bar, the chords without a length share what is left (at least half a beat each).
            const double share = bars ? std::max (0.5, (kBar - typed) / std::max (1, open)) : kBar;
            for (auto& tc : bar)
            {
                if (tc.beats <= 0.0) tc.beats = share;
                out.push_back (tc);
            }
        }

        if (out.empty() && errorOut != nullptr)
            *errorOut = "No chords found";
        return out;
    }

    std::string progressionToText (const std::vector<TimedChord>& chords, bool preferFlats)
    {
        std::string s;
        for (const auto& tc : chords)
        {
            if (! s.empty()) s += ' ';
            s += tc.text.empty() ? tc.chord.name (preferFlats) : tc.text;
            if (std::abs (tc.beats - 4.0) > 1.0e-6)
            {
                char number[32];
                std::snprintf (number, sizeof (number), "%.3f", tc.beats);
                std::string n (number);
                while (n.back() == '0') n.pop_back();   // 2.500 -> 2.5, 2.000 -> 2.
                if (n.back() == '.') n.pop_back();
                s += ":" + n;
            }
        }
        return s;
    }

    // -----------------------------------------------------------------------
    std::vector<Suggestion> findSuggestions (const std::vector<TimedChord>& prog, const Key& key)
    {
        std::vector<Suggestion> out;
        const int count = (int) prog.size();
        if (count == 0)
            return out;

        for (int i = 0; i < count; ++i)
        {
            const Chord& c    = prog[(size_t) i].chord;
            const Chord& next = prog[(size_t) ((i + 1) % count)].chord;
            const Chord& prev = prog[(size_t) ((i - 1 + count) % count)].chord;
            const double beats     = prog[(size_t) i].beats;
            const double prevBeats = prog[(size_t) ((i - 1 + count) % count)].beats;
            const int degreeFromTonic = wrap12 (c.root - key.tonic);

            // --- Tritone sub (replace) ---
            if (actsAsDominant (c, next, key))
            {
                Chord t { wrap12 (c.root + 6), ChordQuality::Dominant7 };
                const PitchClass third = wrap12 (c.root + 4), seventh = wrap12 (c.root + 10);
                out.push_back ({ SubKind::TritoneSub, i, false,
                    { sub (t, beats, "Tritone sub of " + n (c, key)) },
                    n (c, key) + " -> " + n (t, key) + ": both share " + pc (third, key) + " and " + pc (seventh, key)
                        + ". The bass now steps down a half step into " + n (next, key) + "." });
            }

            // Insertions need time to steal from the chord before.
            const bool canInsert = prevBeats >= 2.0 && count > 1;

            // --- Secondary dominant (insert) ---
            if (canInsert && ! isDiminishedQuality (c.quality))
            {
                Chord v { wrap12 (c.root + 7), ChordQuality::Dominant7 };
                if (! (prev.root == v.root && isDominantQuality (prev.quality)) && c.root != key.tonic)
                    out.push_back ({ SubKind::SecondaryDominant, i, true,
                        { sub (v, 1.0, "V7 of " + n (c, key)) },
                        "Insert " + n (v, key) + " before " + n (c, key) + ". It is the V7 of " + n (c, key)
                            + ", so it pulls strongly into it." });
            }

            // --- Related ii-V (insert) ---
            if (canInsert && ! isDiminishedQuality (c.quality))
            {
                const bool targetMinor = isMinorQuality (c.quality);
                Chord ii { wrap12 (c.root + 2), targetMinor ? ChordQuality::HalfDiminished7 : ChordQuality::Minor7 };
                Chord v  { wrap12 (c.root + 7), targetMinor ? ChordQuality::Dominant7b9 : ChordQuality::Dominant7 };
                out.push_back ({ SubKind::RelatedTwoFive, i, true,
                    { sub (ii, 0.5, "ii of " + n (c, key)), sub (v, 0.5, "V of " + n (c, key)) },
                    "Insert " + n (ii, key) + " - " + n (v, key) + " (the ii-V of " + n (c, key) + ") before it." });
            }

            // --- Passing diminished (insert) ---
            if (canInsert && ! isDiminishedQuality (c.quality) && wrap12 (prev.root - c.root) != 11)
            {
                Chord d { wrap12 (c.root - 1), ChordQuality::Diminished7 };
                const bool chromaticBass = wrap12 (c.root - prev.root) == 2;
                out.push_back ({ SubKind::PassingDiminished, i, true,
                    { sub (d, 1.0, "Passing dim7 into " + n (c, key)) },
                    "Insert " + n (d, key) + " before " + n (c, key) + "."
                        + (chromaticBass ? " The bass walks up by half steps: " + pc (prev.root, key) + " - " + pc (d.root, key) + " - " + pc (c.root, key) + "."
                                         : " It leads in from a half step below.") });
            }

            // --- Borrowed chords (replace) ---
            {
                std::vector<std::pair<Chord, std::string>> borrowed;
                if (! key.minor)
                {
                    if (degreeFromTonic == 5 && c.quality == ChordQuality::Major)
                        borrowed.push_back ({ { c.root, ChordQuality::Minor }, "IV -> iv: the minor four, a sad, cinematic sound" });
                    if (degreeFromTonic == 9 && isMinorQuality (c.quality))
                        borrowed.push_back ({ { wrap12 (key.tonic + 8), ChordQuality::Major }, "vi -> bVI: a big, epic rock sound" });
                    if (degreeFromTonic == 7 && (c.quality == ChordQuality::Major || isDominantQuality (c.quality)))
                        borrowed.push_back ({ { wrap12 (key.tonic + 10), ChordQuality::Major }, "V -> bVII: the 'Mixolydian' rock move" });
                    if (degreeFromTonic == 4 && isMinorQuality (c.quality))
                        borrowed.push_back ({ { wrap12 (key.tonic + 3), ChordQuality::Major }, "iii -> bIII: bright but unexpected" });
                    if (degreeFromTonic == 2 && isMinorQuality (c.quality))
                        borrowed.push_back ({ { c.root, ChordQuality::HalfDiminished7 }, "ii -> iiø7: darker pre-dominant from the minor key" });
                }
                else
                {
                    if (degreeFromTonic == 5 && isMinorQuality (c.quality))
                        borrowed.push_back ({ { c.root, ChordQuality::Major }, "iv -> IV: the Dorian lift" });
                    if (degreeFromTonic == 7 && isMinorQuality (c.quality))
                        borrowed.push_back ({ { c.root, ChordQuality::Dominant7 }, "v -> V7: a real dominant pull home" });
                    if (degreeFromTonic == 0 && isMinorQuality (c.quality) && i == count - 1)
                        borrowed.push_back ({ { c.root, ChordQuality::Major }, "Last i -> I: the 'Picardy third', a bright ending" });
                }

                for (auto& [b, why] : borrowed)
                    out.push_back ({ SubKind::BorrowedChord, i, false,
                        { sub (b, beats, "Borrowed from parallel " + std::string (key.minor ? "major" : "minor")) },
                        n (c, key) + " -> " + n (b, key) + ". " + why + "." });
            }

            // --- Diatonic substitution (replace) ---
            {
                // Only offer for chords that really are diatonic triads/7ths.
                int degree = -1;
                for (int d = 0; d < 7; ++d)
                    if (key.diatonicChord (d, hasSeventh (c.quality)) == c)
                        degree = d;

                if (degree >= 0)
                {
                    for (int offset : { -2, 2 })
                    {
                        const int d2 = (degree + offset + 7) % 7;
                        Chord s = key.diatonicChord (d2, hasSeventh (c.quality));
                        if (isDiminishedQuality (s.quality))
                            continue;
                        out.push_back ({ SubKind::DiatonicSub, i, false,
                            { sub (s, beats, "Shares two notes with " + n (c, key)) },
                            n (c, key) + " -> " + n (s, key) + ": same key, shares two notes, new colour." });
                    }
                }
            }

            // --- Backdoor dominant (replace V that goes to I) ---
            if (! key.minor && degreeFromTonic == 7 && actsAsDominant (c, next, key) && next.root == key.tonic)
            {
                Chord b { wrap12 (key.tonic + 10), ChordQuality::Dominant7 };
                Chord iv { wrap12 (key.tonic + 5), ChordQuality::Minor7 };
                out.push_back ({ SubKind::BackdoorDominant, i, false,
                    { sub (b, beats, "Backdoor dominant") },
                    n (c, key) + " -> " + n (b, key) + ": resolves to " + n (next, key) + " through the back door." });

                if (beats >= 2.0)
                    out.push_back ({ SubKind::BackdoorDominant, i, false,
                        { sub (iv, beats / 2, "Backdoor ii (iv7)"), sub (b, beats / 2, "Backdoor dominant") },
                        n (c, key) + " -> " + n (iv, key) + " - " + n (b, key) + ": the full backdoor ii-V." });
            }

            // --- Extensions (replace) ---
            {
                std::vector<Chord> ext;
                switch (c.quality)
                {
                    case ChordQuality::Major:
                        if (actsAsDominant (c, next, key))  { ext.push_back ({ c.root, ChordQuality::Dominant7 }); ext.push_back ({ c.root, ChordQuality::Dominant9 }); }
                        else                           { ext.push_back ({ c.root, ChordQuality::Major7 });    ext.push_back ({ c.root, ChordQuality::Major9 }); }
                        break;
                    case ChordQuality::Minor:      ext.push_back ({ c.root, ChordQuality::Minor7 }); ext.push_back ({ c.root, ChordQuality::Minor9 }); break;
                    case ChordQuality::Minor7:     ext.push_back ({ c.root, ChordQuality::Minor9 }); break;
                    case ChordQuality::Major7:     ext.push_back ({ c.root, ChordQuality::Major9 }); break;
                    case ChordQuality::Dominant7:
                        ext.push_back ({ c.root, isMinorQuality (next.quality) ? ChordQuality::Dominant7b9 : ChordQuality::Dominant9 });
                        break;
                    default: break;
                }

                for (const auto& e : ext)
                    out.push_back ({ SubKind::Extension, i, false,
                        { sub (e, beats, "Added colour notes") },
                        n (c, key) + " -> " + n (e, key) + ": same job, richer sound." });
            }
        }

        return out;
    }

    // -----------------------------------------------------------------------
    namespace
    {
        // Builds the result from at most one chosen suggestion per chord index.
        std::vector<TimedChord> build (const std::vector<TimedChord>& prog, const std::vector<const Suggestion*>& chosen)
        {
            const int count = (int) prog.size();
            std::vector<std::vector<TimedChord>> slots ((size_t) count);

            for (int i = 0; i < count; ++i)
            {
                const Suggestion* s = chosen[(size_t) i];
                if (s != nullptr && ! s->insertsBefore)
                    slots[(size_t) i] = s->replacement;
                else
                    slots[(size_t) i] = { prog[(size_t) i] };
            }

            // Insertions take the last half of the slot before them.
            for (int i = 0; i < count; ++i)
            {
                const Suggestion* s = chosen[(size_t) i];
                if (s == nullptr || ! s->insertsBefore)
                    continue;

                auto& prevSlot = slots[(size_t) ((i - 1 + count) % count)];
                double total = 0;
                for (auto& t : prevSlot) total += t.beats;

                const double stolen = total / 2.0;
                for (auto& t : prevSlot) t.beats *= 0.5;

                for (auto t : s->replacement)
                {
                    t.beats = t.beats * stolen; // replacement beats are weights summing to 1
                    prevSlot.push_back (t);
                }
            }

            std::vector<TimedChord> out;
            for (auto& slot : slots)
                for (auto& t : slot)
                    out.push_back (t);
            return out;
        }
    }

    std::vector<TimedChord> applySuggestion (const std::vector<TimedChord>& prog, const Suggestion& s)
    {
        std::vector<const Suggestion*> chosen (prog.size(), nullptr);
        if (s.index >= 0 && s.index < (int) prog.size())
            chosen[(size_t) s.index] = &s;
        return build (prog, chosen);
    }

    std::vector<TimedChord> reharmonize (const std::vector<TimedChord>& prog, const Key& key, const ReharmOptions& options)
    {
        const auto all = findSuggestions (prog, key);

        std::mt19937 rng (options.seed);
        std::uniform_real_distribution<double> chance (0.0, 1.0);

        std::vector<const Suggestion*> chosen (prog.size(), nullptr);

        for (int i = 0; i < (int) prog.size(); ++i)
        {
            std::vector<const Suggestion*> forThis;
            for (const auto& s : all)
                if (s.index == i && options.enabled[(int) s.kind])
                    forThis.push_back (&s);

            if (forThis.empty() || chance (rng) >= options.amount)
                continue;

            std::uniform_int_distribution<size_t> pick (0, forThis.size() - 1);
            chosen[(size_t) i] = forThis[pick (rng)];
        }

        return build (prog, chosen);
    }
}
