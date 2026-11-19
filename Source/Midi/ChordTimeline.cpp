#include "Midi/ChordTimeline.h"
#include "Theory/ChordFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace gl
{
    namespace
    {
        struct Sounding { double start, end; int pitch, string; };

        // Every note with the time it really rings (a "let ring" note rings until the next note on its string).
        std::vector<Sounding> soundingNotes (const Phrase& p, const Instrument& inst)
        {
            std::vector<Sounding> out;
            for (const auto& n : p.notes)
            {
                double end = n.start + n.length;
                if (n.letRing)
                {
                    double next = p.lengthBeats;
                    for (const auto& m : p.notes)
                        if (m.string == n.string && m.start > n.start + 1e-9) next = std::min (next, m.start);
                    end = std::max (end, next);
                }
                out.push_back ({ n.start, end, soundingNote (n, inst), n.string });
            }
            return out;
        }

        // The chord made by the notes that sound in [from, to). Empty when it is not a chord (one note, a melody...).
        ChordMatch chordIn (const std::vector<Sounding>& notes, double from, double to, bool flats)
        {
            std::vector<int> pitches;
            std::set<int> classes;
            std::vector<const Sounding*> inside;
            for (const auto& s : notes)
                if (s.start < to - 1e-9 && s.end > from + 1e-9)
                {
                    pitches.push_back (s.pitch);
                    classes.insert (((s.pitch % 12) + 12) % 12);
                    inside.push_back (&s);
                }
            ChordMatch none;
            if (classes.size() < 2) return none;

            // A chord needs notes that ring together, or an arpeggio over 3+ strings (one note per string).
            // A quick melody on one or two strings is not a chord.
            bool together = false;
            for (const auto* a : inside)
                for (const auto* b : inside)
                    if (a != b && a->string != b->string && b->start >= a->start && b->start < a->end - 1e-6) together = true;
            std::set<int> strings;
            bool repeated = false;
            for (const auto* a : inside) repeated = repeated || ! strings.insert (a->string).second;
            if (! together && (strings.size() < 3 || repeated)) return none;
            const auto names = nameChord (pitches, flats);
            if (names.empty()) return none;
            // Two notes only count when they make a power chord (root + 5th).
            if (classes.size() == 2 && names.front().name.find ('5') == std::string::npos) return none;
            return names.front();
        }
    }

    std::vector<ChordSpan> chordTimeline (const Phrase& phrase, const Instrument& inst, bool flats)
    {
        std::vector<ChordSpan> spans;
        const auto notes = soundingNotes (phrase, inst);
        const double end = std::max (phrase.lengthBeats, phrase.endBeat());

        auto add = [&] (double a, double b, const ChordMatch& m)
        {
            if (m.name.empty()) return;
            if (! spans.empty() && spans.back().name == m.name && std::abs (spans.back().end - a) < 1e-6)
                spans.back().end = b;  // the same chord goes on
            else
                spans.push_back ({ a, b, m.name, m.meaning });
        };

        for (double w = 0.0; w < end - 1e-9; w += 2.0)
        {
            const double mid = w + 1.0, stop = std::min (w + 2.0, end);
            const auto whole = chordIn (notes, w, stop, flats);
            const auto first = chordIn (notes, w, mid, flats);
            const auto second = chordIn (notes, mid, stop, flats);

            // The chord changes on the beat: two chords. Otherwise one chord over both beats.
            if (! first.name.empty() && ! second.name.empty() && first.name != second.name)
            {
                add (w, mid, first);
                add (mid, stop, second);
            }
            else
                add (w, stop, whole.name.empty() ? (first.name.empty() ? second : first) : whole);
        }
        return spans;
    }

    std::string chordsForReharm (const std::vector<ChordSpan>& spans)
    {
        // The Reharm Lab reads common symbols (triads, 7ths, sus, dim...): turn each chord into the nearest of those.
        std::string out, last;
        double beats = 0.0;
        auto flush = [&]
        {
            if (last.empty()) return;
            out += (out.empty() ? "" : " ") + last;
            if (std::abs (beats - 4.0) > 1.0e-6)
            {
                char number[32];
                std::snprintf (number, sizeof (number), "%g", std::round (beats * 100.0) / 100.0);
                out += ":" + std::string (number);
            }
        };
        for (const auto& s : spans)
        {
            const auto parsed = parseChordName (s.name);
            if (! parsed) continue;
            auto has = [&] (int i) { return std::find (parsed->intervals.begin(), parsed->intervals.end(), i) != parsed->intervals.end(); };
            std::string root = s.name.substr (0, s.name.size() > 1 && (s.name[1] == '#' || s.name[1] == 'b') ? 2 : 1);
            std::string q;
            if (has (3) && has (6) && has (9))       q = "dim7";
            else if (has (3) && has (6) && has (10)) q = "m7b5";
            else if (has (3) && has (6))             q = "dim";
            else if (has (4) && has (8) && ! has (7)) q = "aug";
            else if (has (3)) q = has (10) ? "m7" : "m";
            else if (has (4)) q = has (10) ? "7" : has (11) ? "maj7" : "";
            else if (has (5)) q = has (10) ? "7sus4" : "sus4";
            else if (has (2)) q = "sus2";
            else              q = "";                              // power chord: play it as major
            const std::string name = root + q;
            if (name == last) { beats += s.end - s.start; continue; } // the same chord again: one longer chord
            flush();
            last = name;
            beats = s.end - s.start;
        }
        flush();
        return out;
    }
}
