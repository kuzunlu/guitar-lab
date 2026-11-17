#include "Midi/PhraseRecorder.h"

#include <algorithm>
#include <cmath>

namespace gl
{
    namespace
    {
        using Point = std::pair<double, double>; // (position, semitones)

        // Ramer-Douglas-Peucker: keeps only the points needed to stay within "epsilon" of the curve.
        void simplify (const std::vector<Point>& in, size_t first, size_t last, double epsilon, std::vector<Point>& out)
        {
            double worst = 0.0;
            size_t index = first;
            const auto [x1, y1] = in[first];
            const auto [x2, y2] = in[last];
            for (size_t i = first + 1; i < last; ++i)
            {
                const double t = x2 > x1 ? (in[i].first - x1) / (x2 - x1) : 0.0;
                const double d = std::abs (in[i].second - (y1 + t * (y2 - y1)));
                if (d > worst) { worst = d; index = i; }
            }

            if (worst > epsilon)
            {
                simplify (in, first, index, epsilon, out);
                out.pop_back(); // "index" is added again by the second half
                simplify (in, index, last, epsilon, out);
            }
            else
            {
                out.push_back (in[first]);
                out.push_back (in[last]);
            }
        }
    }

    void PhraseRecorder::start (const Instrument& i, const Settings& s)
    {
        inst = i;
        settings = s;
        active = {};
        pendingBend.fill (0.0);
        done.clear();
        origin = -1.0;
    }

    int PhraseRecorder::countActive() const
    {
        int c = 0;
        for (const auto& a : active) if (a.on) ++c;
        return c;
    }

    void PhraseRecorder::add (double beat, uint8_t status, uint8_t d1, uint8_t d2)
    {
        const int type = status & 0xF0;
        const int string = (status & 0x0F) + 1 - settings.firstChannel;
        if (string < 0 || string >= inst.numStrings())
            return;

        auto& a = active[(size_t) string];

        if (type == 0xE0)
        {
            const double semis = (((d2 << 7) | d1) - 8192) / 8192.0 * settings.bendRange;
            if (a.on) a.bends.push_back ({ beat, semis });
            else      pendingBend[(size_t) string] = semis;
            return;
        }

        if (type == 0x90 && d2 > 0)
        {
            if (origin < 0.0) origin = beat;

            Technique technique = Technique::Normal;
            if (a.on)
            {
                // The tracker sends the NEW note's bend just before its note-on (same time).
                // That bend belongs to the new note, not to the end of the old one.
                if (! a.bends.empty() && a.bends.back().first == beat)
                {
                    pendingBend[(size_t) string] = a.bends.back().second;
                    a.bends.pop_back();
                }

                // The tracker starts the new note before ending the old one: that is legato.
                technique = d1 > a.note ? Technique::HammerOn : Technique::PullOff;

                // If the old note was already bent most of the way to the new one, it was a slide.
                if (! a.bends.empty())
                {
                    const double reached = a.note + a.bends.back().second;
                    if (std::abs (a.bends.back().second) >= 0.75 && std::abs (reached - d1) < 0.6)
                    {
                        a.slideTo = d1;
                        technique = Technique::Normal; // a slide target is not a hammer-on
                    }
                }
                close (string, beat);
            }

            Active fresh;
            fresh.on = true;
            fresh.note = d1;
            fresh.velocity = d2;
            fresh.start = beat;
            fresh.technique = technique;
            if (std::abs (pendingBend[(size_t) string]) > 0.05)
                fresh.bends.push_back ({ beat, pendingBend[(size_t) string] });
            pendingBend[(size_t) string] = 0.0;
            a = fresh;
            return;
        }

        if ((type == 0x80 || type == 0x90) && a.on && a.note == d1)
            close (string, beat);
    }

    void PhraseRecorder::close (int string, double beat)
    {
        auto& a = active[(size_t) string];
        if (! a.on) return;
        done.push_back (makeNote (string, a, beat));
        a.on = false;
    }

    NoteEvent PhraseRecorder::makeNote (int string, const Active& a, double end) const
    {
        NoteEvent n;
        n.string = string;
        n.fret = a.note - inst.tuning()[(size_t) string];
        if (n.fret < 0 || n.fret > kNumFrets)
        {
            // Not playable on this string: use the first spot that has this note.
            const auto spots = inst.positionsOf (a.note);
            if (! spots.empty()) { n.string = spots.front().string; n.fret = spots.front().fret; }
            else                 n.fret = std::clamp (n.fret, 0, kNumFrets);
        }
        n.start = a.start;
        n.length = std::max (0.05, end - a.start);
        n.velocity = a.velocity;
        n.technique = a.technique;

        const double len = n.length;
        auto pos = [&] (double beat) { return std::clamp ((beat - a.start) / len, 0.0, 1.0); };

        // --- Slid into the next note ---
        if (a.slideTo >= 0)
        {
            n.slide = SlideType::SlideTo;
            n.slideToFret = std::clamp (n.fret + (a.slideTo - a.note), 0, kNumFrets);
            n.slideStart = 0.5;
            for (const auto& [b, s] : a.bends)
                if (std::abs (s) > 0.2) { n.slideStart = pos (b); break; }
            return n;
        }

        if (a.bends.empty())
            return n;

        double biggest = 0.0;
        for (const auto& p : a.bends) biggest = std::max (biggest, std::abs (p.second));
        if (biggest < 0.15)
            return n;

        // --- Split the pitch movement into a slow part (bend) and a fast wobble (vibrato) ---
        std::vector<Point> smooth;
        std::vector<double> wobble;
        auto split = [&] (double windowBeats)
        {
            smooth.clear();
            wobble.clear();
            const double firstBeat = a.bends.front().first, lastBeat = a.bends.back().first;
            for (size_t i = 0; i < a.bends.size(); ++i)
            {
                // Near the ends a window would only see half a wave. Use the nearest full window instead.
                double centre = a.bends[i].first;
                if (lastBeat - firstBeat > windowBeats)
                    centre = std::clamp (centre, firstBeat + windowBeats * 0.5, lastBeat - windowBeats * 0.5);

                double sum = 0.0; int count = 0;
                for (size_t j = 0; j < a.bends.size(); ++j)
                    if (std::abs (a.bends[j].first - centre) <= windowBeats * 0.5) { sum += a.bends[j].second; ++count; }
                const double avg = sum / std::max (1, count);
                smooth.push_back ({ a.bends[i].first, avg });
                wobble.push_back (a.bends[i].second - avg);
            }
        };
        split (0.12 * settings.bpm / 60.0); // 120 ms, in beats

        // Vibrato: the wobble changes direction again and again (with some size).
        int crossings = 0, sign = 0;
        double firstCross = -1.0, lastCross = -1.0, wobbleSum = 0.0;
        for (size_t i = 0; i < wobble.size(); ++i)
        {
            const double w = wobble[i];
            wobbleSum += std::abs (w);
            const int s = w > 0.06 ? 1 : (w < -0.06 ? -1 : 0);
            if (s != 0 && s != sign)
            {
                if (sign != 0)
                {
                    ++crossings;
                    if (firstCross < 0) firstCross = a.bends[i].first;
                    lastCross = a.bends[i].first;
                }
                sign = s;
            }
        }

        if (crossings >= 4 && lastCross > firstCross)
        {
            const double seconds = (lastCross - firstCross) * 60.0 / settings.bpm;
            n.vibrato = true;
            n.vibratoRateHz = std::clamp ((crossings / 2.0) / seconds, 1.0, 12.0);
            n.vibratoDepth = std::clamp (wobbleSum / wobble.size() * 1.57, 0.05, 1.0); // mean |sine| -> its size
            n.vibratoDelay = std::clamp (pos (firstCross) - 0.05, 0.0, 0.9);

            // Average over exactly one vibrato wave: that cancels the wobble and leaves only a real bend.
            split ((1.0 / n.vibratoRateHz) * settings.bpm / 60.0);
        }

        // --- The slow part ---
        double slowBiggest = 0.0;
        for (const auto& p : smooth) slowBiggest = std::max (slowBiggest, std::abs (p.second));
        if (slowBiggest < 0.25)
            return n;

        const double final = smooth.back().second;
        if (final <= -0.75)
        {
            // Pitch fell a long way and stayed there: the player slid down the neck.
            n.slide = SlideType::SlideOutDown;
            n.slideDistance = std::max (1, (int) std::lround (-final));
            n.slideStart = 0.5;
            for (const auto& [b, s] : smooth)
                if (s < -0.2) { n.slideStart = pos (b); break; }
            return n;
        }

        // Bend curve, simplified to the few points that matter.
        std::vector<Point> curve;
        // Start from the raw pitch: smoothing would pull part of a later bend into the first point
        // and make the note look "pre-bent".
        curve.push_back ({ 0.0, a.bends.front().first > a.start + 1e-9 ? 0.0 : a.bends.front().second });
        for (const auto& [b, s] : smooth) curve.push_back ({ pos (b), s });
        curve.push_back ({ 1.0, smooth.back().second });

        std::vector<Point> simple;
        simplify (curve, 0, curve.size() - 1, 0.12, simple);
        for (const auto& [p, s] : simple)
            n.bend.push_back ({ p, std::round (s * 20.0) / 20.0 }); // to 5 cents, keeps the tab readable
        return n;
    }

    std::vector<NoteEvent> PhraseRecorder::finish (double endBeat)
    {
        for (int s = 0; s < kMaxStrings; ++s)
            close (s, endBeat);

        std::vector<NoteEvent> out = done;
        if (out.empty() || origin < 0.0)
            return out;

        std::sort (out.begin(), out.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });

        // Chords: notes on different strings that start (almost) together.
        int group = 0;
        for (size_t i = 0; i < out.size();)
        {
            size_t j = i + 1;
            while (j < out.size() && out[j].start - out[i].start < 0.03) ++j;
            if (j - i >= 2)
            {
                for (size_t k = i; k < j; ++k) out[k].group = group;
                ++group;
            }
            i = j;
        }

        // The tracker first sends a single note, and ~60 ms later swaps it for a taught chord.
        // Drop that short single note: it ends exactly where a chord with the same pitch starts.
        std::vector<bool> drop (out.size(), false);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const auto& n = out[i];
            if (n.group >= 0 || n.length > 0.3) continue;
            const double end = n.start + n.length;
            const int pitch = inst.noteAt ({ n.string, n.fret });
            for (const auto& c : out)
                if (c.group >= 0 && std::abs (c.start - end) < 1e-9 && inst.noteAt ({ c.string, c.fret }) == pitch)
                    drop[i] = true;
        }
        std::vector<NoteEvent> kept;
        for (size_t i = 0; i < out.size(); ++i)
            if (! drop[i]) kept.push_back (out[i]);
        out = std::move (kept);

        // Move to the phrase: at the cursor, or (DAW was playing) keep the place inside the bar.
        const double base = settings.alignToBars ? std::floor (origin / 4.0) * 4.0 : origin;
        const double target = settings.alignToBars ? std::floor (settings.startAt / 4.0) * 4.0 : settings.startAt;

        for (auto& n : out)
        {
            double start = target + (n.start - base);
            double end = start + n.length;
            if (settings.snapBeats > 0.0)
            {
                start = std::round (start / settings.snapBeats) * settings.snapBeats;
                end = std::max (start + settings.snapBeats, std::round (end / settings.snapBeats) * settings.snapBeats);
            }
            n.start = std::max (0.0, start);
            n.length = std::max (0.05, end - start);
        }
        return out;
    }
}
