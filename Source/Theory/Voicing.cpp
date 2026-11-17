#include "Theory/Voicing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace gl
{
    int Voicing::lowestFrettedFret() const
    {
        int low = std::numeric_limits<int>::max();
        for (int f : frets)
            if (f > 0) low = std::min (low, f);
        return low == std::numeric_limits<int>::max() ? 0 : low;
    }

    int Voicing::highestFret() const
    {
        int high = 0;
        for (int f : frets) high = std::max (high, f);
        return high;
    }

    int Voicing::span() const
    {
        int low = std::numeric_limits<int>::max(), high = 0;
        for (int f : frets)
            if (f > 0) { low = std::min (low, f); high = std::max (high, f); }
        return high == 0 ? 0 : high - low;
    }

    int Voicing::numPlayed() const
    {
        return (int) std::count_if (frets.begin(), frets.end(), [] (int f) { return f != kMuted; });
    }

    std::vector<FretPos> Voicing::positions() const
    {
        std::vector<FretPos> out;
        for (int s = 0; s < (int) frets.size(); ++s)
            if (frets[(size_t) s] != kMuted)
                out.push_back ({ s, frets[(size_t) s] });
        return out;
    }

    std::vector<MidiNote> Voicing::notes (const Instrument& inst) const
    {
        std::vector<MidiNote> out;
        for (int s = (int) frets.size() - 1; s >= 0; --s)
            if (frets[(size_t) s] != kMuted)
                out.push_back (inst.noteAt ({ s, frets[(size_t) s] }));
        return out;
    }

    std::string voicingToText (const Voicing& v)
    {
        std::string text;
        for (int s = (int) v.frets.size() - 1; s >= 0; --s)
        {
            const int f = v.frets[(size_t) s];
            if (f == kMuted)  text += 'x';
            else if (f < 10)  text += std::to_string (f);
            else              text += "(" + std::to_string (f) + ")";
        }
        return text;
    }

    // -----------------------------------------------------------------------
    namespace
    {
        struct Search
        {
            const Chord& chord;
            const Instrument& inst;
            const VoicingOptions& opt;
            std::vector<PitchClass> chordPcs;
            std::vector<PitchClass> required;
            std::set<std::vector<int>> seen;
            std::vector<std::pair<double, Voicing>> results;
        };

        // Lower score = better voicing.
        double scoreVoicing (const Voicing& v, const Search& s, int innerMutes, bool missingFifth)
        {
            double score = 0;
            score += v.span() * 0.8;
            score += v.lowestFrettedFret() * 0.04;
            score += innerMutes * 1.2;
            score += missingFifth ? 0.4 : 0.0;
            score -= v.numPlayed() * 0.35;

            // Doubling the 3rd sounds thin and muddy: small penalty per extra copy.
            int thirds = 0;
            const PitchClass third = s.chordPcs.size() > 1 ? s.chordPcs[1] : -1;
            for (auto p : v.positions())
                if (pitchClassOf (s.inst.noteAt (p)) == third) ++thirds;
            score += std::max (0, thirds - 1) * 0.6;

            // The exact same note twice (e.g. open A and A on the E string, 5th fret) adds nothing.
            const auto notes = v.notes (s.inst);
            for (size_t i = 1; i < notes.size(); ++i)
                for (size_t j = 0; j < i; ++j)
                    if (notes[i] == notes[j]) score += 1.0;

            return score;
        }

        void check (Search& s, const std::vector<int>& frets)
        {
            Voicing v { frets };
            const int played = v.numPlayed();
            if (played < s.opt.minStrings || played > s.opt.maxStrings)
                return;

            // Played strings: low (high index) to high (index 0). Count inner mutes.
            int first = -1, last = -1;
            for (int i = 0; i < (int) frets.size(); ++i)
                if (frets[(size_t) i] != kMuted) { if (first < 0) first = i; last = i; }

            int innerMutes = 0;
            for (int i = first; i <= last; ++i)
                if (frets[(size_t) i] == kMuted) ++innerMutes;

            if (innerMutes > (s.opt.allowInnerMutes ? 1 : 0))
                return;

            // Bass note = note on the lowest played string (highest index).
            if (s.opt.rootInBass && pitchClassOf (s.inst.noteAt ({ last, frets[(size_t) last] })) != s.chord.root)
                return;

            // Required notes present?
            std::set<PitchClass> present;
            for (auto p : v.positions())
                present.insert (pitchClassOf (s.inst.noteAt (p)));

            for (auto r : s.required)
                if (! present.count (r))
                    return;

            // Fingers: max 4 fretted notes, unless the lowest fret is a barre.
            std::vector<int> fretted;
            for (int f : frets) if (f > 0) fretted.push_back (f);
            if (fretted.size() > 4)
            {
                const int low = v.lowestFrettedFret();
                const auto aboveBarre = std::count_if (fretted.begin(), fretted.end(), [low] (int f) { return f > low; });
                const auto onBarre    = (long) fretted.size() - aboveBarre;
                if (onBarre < 2 || aboveBarre > 3)
                    return;
            }

            if (! s.seen.insert (frets).second)
                return;

            const PitchClass fifth = wrap12 (s.chord.root + 7);
            const bool chordHasFifth = std::find (s.chordPcs.begin(), s.chordPcs.end(), fifth) != s.chordPcs.end();
            const bool missingFifth = chordHasFifth && ! present.count (fifth);

            s.results.push_back ({ scoreVoicing (v, s, innerMutes, missingFifth), v });
        }

        void walk (Search& s, std::vector<int>& frets, int string, int winLow, int winHigh, int played)
        {
            if (string < 0)
            {
                check (s, frets);
                return;
            }

            // Muted
            frets[(size_t) string] = kMuted;
            walk (s, frets, string - 1, winLow, winHigh, played);

            if (played >= s.opt.maxStrings)
                return;

            const MidiNote open = s.inst.tuning()[(size_t) string];
            auto inChord = [&] (int fret)
            {
                return std::find (s.chordPcs.begin(), s.chordPcs.end(), pitchClassOf (open + fret)) != s.chordPcs.end();
            };

            if (s.opt.allowOpenStrings && s.opt.minFret == 0 && inChord (0))
            {
                frets[(size_t) string] = 0;
                walk (s, frets, string - 1, winLow, winHigh, played + 1);
            }

            for (int f = std::max (1, winLow); f <= winHigh; ++f)
                if (inChord (f))
                {
                    frets[(size_t) string] = f;
                    walk (s, frets, string - 1, winLow, winHigh, played + 1);
                }

            frets[(size_t) string] = kMuted;
        }
    }

    std::vector<Voicing> findVoicings (const Chord& chord, const Instrument& inst, const VoicingOptions& opt, int maxResults)
    {
        Search s { chord, inst, opt, chord.pitchClasses(), {}, {}, {} };

        // Required: everything except the 5th (it may be dropped in chords with 4+ notes).
        const PitchClass fifth = wrap12 (chord.root + 7);
        for (auto p : s.chordPcs)
            if (! (opt.dropFifthIfNeeded && s.chordPcs.size() >= 4 && p == fifth))
                s.required.push_back (p);

        std::vector<int> frets ((size_t) inst.numStrings(), kMuted);
        const int lastStart = std::max (opt.minFret, opt.maxFret - opt.maxSpan);

        for (int start = std::max (1, opt.minFret); start <= std::max (1, lastStart); ++start)
            walk (s, frets, inst.numStrings() - 1, start, std::min (start + opt.maxSpan, opt.maxFret), 0);

        std::sort (s.results.begin(), s.results.end(), [] (auto& a, auto& b) { return a.first < b.first; });

        std::vector<Voicing> out;
        for (auto& r : s.results)
        {
            out.push_back (r.second);
            if ((int) out.size() >= maxResults)
                break;
        }
        return out;
    }

    // -----------------------------------------------------------------------
    namespace
    {
        double centre (const Voicing& v)
        {
            double sum = 0; int count = 0;
            for (int f : v.frets)
                if (f > 0) { sum += f; ++count; }
            return count == 0 ? 0.0 : sum / count;
        }

        double moveCost (const Voicing& a, const Voicing& b)
        {
            double cost = std::abs (centre (a) - centre (b));
            for (size_t i = 0; i < a.frets.size(); ++i)
                if (a.frets[i] != b.frets[i]) cost += 0.25;
            return cost;
        }
    }

    std::vector<Voicing> voiceLead (const std::vector<Chord>& chords, const Instrument& inst,
                                    const VoicingOptions& opt, int startFret)
    {
        const size_t count = chords.size();
        std::vector<std::vector<Voicing>> cand (count);
        for (size_t i = 0; i < count; ++i)
        {
            cand[i] = findVoicings (chords[i], inst, opt, 25);
            if (cand[i].empty()) // relax rules so we always return something
            {
                auto loose = opt;
                loose.rootInBass = false;
                loose.maxSpan = 5;
                loose.minStrings = 2;
                cand[i] = findVoicings (chords[i], inst, loose, 25);
            }
        }

        // Dynamic programming: cheapest path through one voicing per chord.
        std::vector<std::vector<double>> cost (count);
        std::vector<std::vector<int>> from (count);

        for (size_t i = 0; i < count; ++i)
        {
            cost[i].assign (cand[i].size(), std::numeric_limits<double>::max());
            from[i].assign (cand[i].size(), -1);

            for (size_t j = 0; j < cand[i].size(); ++j)
            {
                const double rank = (double) j * 0.15; // earlier = better voicing
                if (i == 0 || cand[i - 1].empty())
                {
                    cost[i][j] = rank + std::abs (centre (cand[i][j]) - startFret) * 0.3;
                    continue;
                }

                for (size_t k = 0; k < cand[i - 1].size(); ++k)
                {
                    const double c = cost[i - 1][k] + rank + moveCost (cand[i - 1][k], cand[i][j]);
                    if (c < cost[i][j]) { cost[i][j] = c; from[i][j] = (int) k; }
                }
            }
        }

        std::vector<Voicing> out (count);
        if (count == 0)
            return out;

        // Walk back from the cheapest last voicing.
        int best = -1;
        for (size_t j = 0; j < cost[count - 1].size(); ++j)
            if (best < 0 || cost[count - 1][j] < cost[count - 1][(size_t) best]) best = (int) j;

        for (size_t i = count; i-- > 0;)
        {
            if (best >= 0 && best < (int) cand[i].size())
            {
                out[i] = cand[i][(size_t) best];
                best = from[i][(size_t) best];
            }
            else
            {
                out[i] = cand[i].empty() ? Voicing { std::vector<int> ((size_t) inst.numStrings(), kMuted) } : cand[i].front();
                best = -1;
            }
        }
        return out;
    }
}
