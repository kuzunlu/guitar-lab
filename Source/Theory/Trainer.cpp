#include "Theory/Trainer.h"
#include "Theory/Voicing.h"

#include <algorithm>
#include <sstream>

namespace gl
{
    const std::vector<ScaleType>& scaleTypes()
    {
        static const std::vector<ScaleType> types = {
            { "Major (Ionian)",      { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural minor",       { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic minor",      { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic minor",       { 0, 2, 3, 5, 7, 9, 11 } },
            { "Dorian",              { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",            { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian",              { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian",          { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian",             { 0, 1, 3, 5, 6, 8, 10 } },
            { "Major pentatonic",    { 0, 2, 4, 7, 9 } },
            { "Minor pentatonic",    { 0, 3, 5, 7, 10 } },
            { "Blues",               { 0, 3, 5, 6, 7, 10 } },
            { "Phrygian dominant",   { 0, 1, 4, 5, 7, 8, 10 } },
            { "Whole tone",          { 0, 2, 4, 6, 8, 10 } },
            { "Diminished (H-W)",    { 0, 1, 3, 4, 6, 7, 9, 10 } },
            { "Diminished (W-H)",    { 0, 2, 3, 5, 6, 8, 9, 11 } },
            { "Chromatic",           { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        };
        return types;
    }

    std::string intervalName (int semitones)
    {
        static const char* const names[12] = {
            "unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
            "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th"
        };
        return names[wrap12 (semitones)];
    }

    // -----------------------------------------------------------------------
    Trainer::Trainer (unsigned seed)
        : rng (seed),
          table ((size_t) (kMaxStrings * (kNumFrets + 1)))
    {
        setInstrument (Instrument{});
    }

    void Trainer::setInstrument (const Instrument& i)
    {
        inst = i;
        focus_.strings.assign ((size_t) inst.numStrings(), true);
        focus_.minFret = std::clamp (focus_.minFret, 0, kNumFrets);
        focus_.maxFret = std::clamp (focus_.maxFret, focus_.minFret, kNumFrets);
    }

    SpotStats& Trainer::stats (FretPos p)
    {
        return table[(size_t) (p.string * (kNumFrets + 1) + p.fret)];
    }

    const SpotStats& Trainer::statsAt (FretPos p) const
    {
        return table[(size_t) (p.string * (kNumFrets + 1) + p.fret)];
    }

    double Trainer::accuracyAt (FretPos p) const
    {
        const auto& s = statsAt (p);
        return s.asked == 0 ? -1.0 : (double) s.correct / s.asked;
    }

    void Trainer::resetStats()
    {
        std::fill (table.begin(), table.end(), SpotStats{});
        totalAsked_ = totalCorrect_ = streak_ = 0;
    }

    std::vector<FretPos> Trainer::allowedSpots() const
    {
        std::vector<FretPos> spots;
        for (int s = 0; s < inst.numStrings(); ++s)
        {
            if (s < (int) focus_.strings.size() && ! focus_.strings[(size_t) s])
                continue;

            for (int f = focus_.minFret; f <= focus_.maxFret; ++f)
            {
                const PitchClass pc = pitchClassOf (inst.noteAt ({ s, f }));
                const bool natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
                if (focus_.naturalsOnly && ! natural)
                    continue;
                spots.push_back ({ s, f });
            }
        }

        if (spots.empty()) // never return nothing: fall back to the whole neck
            for (int s = 0; s < inst.numStrings(); ++s)
                for (int f = 0; f <= kNumFrets; ++f)
                    spots.push_back ({ s, f });

        return spots;
    }

    double Trainer::weightFor (FretPos p) const
    {
        if (! adaptive)
            return 1.0;

        const auto& s = statsAt (p);
        if (s.asked == 0)
            return 2.0; // new spots are interesting

        const double missRate = 1.0 - (double) s.correct / s.asked;
        const double slowness = std::min (s.avgSeconds / 4.0, 2.0); // slow answers count as weak too
        return 0.3 + missRate * 4.0 + slowness;
    }

    FretPos Trainer::pickSpot()
    {
        const auto spots = allowedSpots();
        std::vector<double> weights;
        for (auto p : spots)
            weights.push_back (weightFor (p));

        std::discrete_distribution<size_t> pick (weights.begin(), weights.end());
        return spots[pick (rng)];
    }

    void Trainer::record (FretPos p, bool ok, double seconds)
    {
        auto& s = stats (p);
        s.avgSeconds = (s.avgSeconds * s.asked + seconds) / (s.asked + 1);
        ++s.asked;
        if (ok) ++s.correct;

        ++totalAsked_;
        if (ok) { ++totalCorrect_; ++streak_; }
        else    streak_ = 0;
    }

    Question Trainer::next (QuizMode mode)
    {
        Question q;
        q.mode = mode;
        const bool flats = false;

        switch (mode)
        {
            case QuizMode::NameTheNote:
            {
                const auto p = pickSpot();
                q.shown = { p };
                q.targetPc = pitchClassOf (inst.noteAt (p));
                q.prompt = "What note is this? (string " + std::to_string (p.string + 1)
                         + ", fret " + std::to_string (p.fret) + ")";
                break;
            }

            case QuizMode::FindTheNote:
            {
                const auto p = pickSpot();
                q.targetPc = pitchClassOf (inst.noteAt (p));
                q.prompt = "Click every " + pitchClassName (q.targetPc, flats) + " between fret "
                         + std::to_string (focus_.minFret) + " and " + std::to_string (focus_.maxFret) + " on the chosen strings";
                break;
            }

            case QuizMode::ArpeggioTones:
            {
                static const ChordQuality qualities[] = {
                    ChordQuality::Major, ChordQuality::Minor, ChordQuality::Major7, ChordQuality::Minor7,
                    ChordQuality::Dominant7, ChordQuality::HalfDiminished7, ChordQuality::Diminished7
                };
                const auto p = pickSpot(); // root picked from a weak spot
                std::uniform_int_distribution<int> qPick (0, (int) std::size (qualities) - 1);
                q.chord = { pitchClassOf (inst.noteAt (p)), qualities[qPick (rng)] };
                q.shown = { p };
                q.prompt = "Click all " + q.chord.name (flats) + " chord tones between fret "
                         + std::to_string (focus_.minFret) + " and " + std::to_string (focus_.maxFret);
                break;
            }

            case QuizMode::IntervalName:
            {
                const auto a = pickSpot();
                auto spots = allowedSpots();
                std::vector<FretPos> near;
                for (auto b : spots)
                {
                    const int d = inst.noteAt (b) - inst.noteAt (a);
                    if (! (b == a) && d > 0 && d < 12 && std::abs (b.fret - a.fret) <= 5)
                        near.push_back (b);
                }
                FretPos b = a;
                if (! near.empty())
                {
                    std::uniform_int_distribution<size_t> pick (0, near.size() - 1);
                    b = near[pick (rng)];
                }
                q.shown = { a, b };
                q.interval = wrap12 (inst.noteAt (b) - inst.noteAt (a));
                q.prompt = "Name the interval from the blue dot up to the orange dot";
                break;
            }

            case QuizMode::EarInterval:
            {
                // Two notes, low then high (up to an octave), close together on the neck. Only heard, not shown.
                const auto a = pickSpot();
                std::vector<FretPos> near;
                for (int s = 0; s < inst.numStrings(); ++s)
                    for (int f = std::max (0, a.fret - 5); f <= std::min (kNumFrets, a.fret + 5); ++f)
                    {
                        const FretPos b { s, f };
                        const int d = inst.noteAt (b) - inst.noteAt (a);
                        if (d >= 1 && d <= 12) near.push_back (b);
                    }
                FretPos b = a;
                if (! near.empty())
                {
                    std::uniform_int_distribution<size_t> pick (0, near.size() - 1);
                    b = near[pick (rng)];
                }
                q.shown = { a, b };
                q.interval = inst.noteAt (b) - inst.noteAt (a);
                q.playTogether = false;
                q.prompt = "Listen: which interval is it? (the low note first, then the high one)";
                break;
            }

            case QuizMode::EarChord:
            {
                // A real chord shape, strummed, built on a spot from the chosen area.
                const auto p = pickSpot();
                const auto& qualities = earChordQualities();
                std::uniform_int_distribution<size_t> pick (0, qualities.size() - 1);
                q.chord = { pitchClassOf (inst.noteAt (p)), qualities[pick (rng)] };
                VoicingOptions opt;
                opt.minStrings = 3;
                opt.minFret = std::max (0, focus_.minFret);
                opt.maxFret = std::max (opt.minFret + 4, focus_.maxFret);
                auto found = findVoicings (q.chord, inst, opt, 8);
                if (found.empty())
                {
                    opt.minFret = 0;
                    opt.maxFret = kNumFrets;
                    found = findVoicings (q.chord, inst, opt, 8);
                }
                if (! found.empty())
                {
                    std::uniform_int_distribution<size_t> v (0, found.size() - 1);
                    for (auto pos : found[v (rng)].positions()) q.shown.push_back (pos);
                    // Lowest note last (answerChordQuality records it as the root spot)
                    std::sort (q.shown.begin(), q.shown.end(), [this] (FretPos x, FretPos y) { return inst.noteAt (x) > inst.noteAt (y); });
                }
                else
                    q.shown = { p };
                q.playTogether = true;
                q.prompt = "Listen: what kind of chord is it?";
                break;
            }

            case QuizMode::EarPlayBack:
            {
                const auto p = pickSpot();
                q.shown = { p };
                q.targetPc = pitchClassOf (inst.noteAt (p));
                q.playTogether = true;
                q.prompt = "Listen, then play the same note on your guitar (or click it on the neck or the piano)";
                break;
            }
        }
        return q;
    }

    bool Trainer::answerPitchClass (const Question& q, PitchClass answer, double seconds)
    {
        const bool ok = wrap12 (answer) == q.targetPc;
        if (! q.shown.empty())
            record (q.shown.front(), ok, seconds);
        return ok;
    }

    const std::vector<ChordQuality>& earChordQualities()
    {
        static const std::vector<ChordQuality> q {
            ChordQuality::Major, ChordQuality::Minor, ChordQuality::Dominant7, ChordQuality::Major7, ChordQuality::Minor7,
            ChordQuality::Diminished, ChordQuality::Augmented, ChordQuality::Sus2, ChordQuality::Sus4,
            ChordQuality::HalfDiminished7, ChordQuality::Diminished7
        };
        return q;
    }

    std::string chordQualityName (ChordQuality q)
    {
        switch (q)
        {
            case ChordQuality::Major:           return "major";
            case ChordQuality::Minor:           return "minor";
            case ChordQuality::Dominant7:       return "dominant 7th";
            case ChordQuality::Major7:          return "major 7th";
            case ChordQuality::Minor7:          return "minor 7th";
            case ChordQuality::Diminished:      return "diminished";
            case ChordQuality::Augmented:       return "augmented";
            case ChordQuality::Sus2:            return "sus2";
            case ChordQuality::Sus4:            return "sus4";
            case ChordQuality::HalfDiminished7: return "half-diminished";
            case ChordQuality::Diminished7:     return "diminished 7th";
            default:                            return "other";
        }
    }

    bool Trainer::answerChordQuality (const Question& q, ChordQuality answer, double seconds)
    {
        const bool ok = answer == q.chord.quality;
        if (! q.shown.empty())
            record (q.shown.back(), ok, seconds); // the lowest note = the root
        return ok;
    }

    bool Trainer::answerInterval (const Question& q, int semitones, double seconds)
    {
        const bool ok = wrap12 (semitones) == wrap12 (q.interval);
        if (! q.shown.empty())
            record (q.shown.front(), ok, seconds);
        return ok;
    }

    std::vector<FretPos> Trainer::correctPositions (const Question& q) const
    {
        std::vector<PitchClass> wanted;
        if (q.mode == QuizMode::ArpeggioTones) wanted = q.chord.pitchClasses();
        else                                   wanted = { q.targetPc };

        // "Naturals only" limits what is asked, not what counts as a right answer (chord tones can be F#).
        std::vector<FretPos> out;
        for (int s = 0; s < inst.numStrings(); ++s)
        {
            if (s < (int) focus_.strings.size() && ! focus_.strings[(size_t) s])
                continue;

            for (int f = focus_.minFret; f <= focus_.maxFret; ++f)
                if (std::find (wanted.begin(), wanted.end(), pitchClassOf (inst.noteAt ({ s, f }))) != wanted.end())
                    out.push_back ({ s, f });
        }
        return out;
    }

    bool Trainer::answerPositions (const Question& q, const std::vector<FretPos>& clicked, double seconds,
                                   std::vector<FretPos>* missedOut, std::vector<FretPos>* wrongOut)
    {
        const auto correct = correctPositions (q);
        std::vector<FretPos> missed, wrong;

        for (auto c : correct)
            if (std::find (clicked.begin(), clicked.end(), c) == clicked.end())
                missed.push_back (c);

        for (auto c : clicked)
            if (std::find (correct.begin(), correct.end(), c) == correct.end())
                wrong.push_back (c);

        // Record each correct spot on its own, so the heat map learns exactly where you are weak.
        for (auto c : correct)
        {
            const bool found = std::find (missed.begin(), missed.end(), c) == missed.end();
            auto& s = stats (c);
            s.avgSeconds = (s.avgSeconds * s.asked + seconds) / (s.asked + 1);
            ++s.asked;
            if (found) ++s.correct;
        }

        const bool ok = missed.empty() && wrong.empty();
        ++totalAsked_;
        if (ok) { ++totalCorrect_; ++streak_; } else streak_ = 0;

        if (missedOut) *missedOut = missed;
        if (wrongOut)  *wrongOut  = wrong;
        return ok;
    }

    std::string Trainer::serialise() const
    {
        std::ostringstream out;
        out << totalAsked_ << ' ' << totalCorrect_ << '\n';
        for (size_t i = 0; i < table.size(); ++i)
            if (table[i].asked > 0)
                out << i << ' ' << table[i].asked << ' ' << table[i].correct << ' ' << table[i].avgSeconds << '\n';
        return out.str();
    }

    void Trainer::deserialise (const std::string& text)
    {
        resetStats();
        std::istringstream in (text);
        in >> totalAsked_ >> totalCorrect_;

        size_t index; SpotStats s;
        while (in >> index >> s.asked >> s.correct >> s.avgSeconds)
            if (index < table.size())
                table[index] = s;
    }
}
