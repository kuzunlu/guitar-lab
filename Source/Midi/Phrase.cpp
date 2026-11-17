#include "Midi/Phrase.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace gl
{
    std::string techniqueName (Technique t)
    {
        switch (t)
        {
            case Technique::Normal:          return "Normal";
            case Technique::HammerOn:        return "Hammer-on";
            case Technique::PullOff:         return "Pull-off";
            case Technique::Tap:             return "Tap";
            case Technique::PalmMute:        return "Palm mute";
            case Technique::DeadNote:        return "Dead note";
            case Technique::NaturalHarmonic: return "Natural harmonic";
            case Technique::PinchHarmonic:   return "Pinch harmonic";
            case Technique::NumTechniques:   break;
        }
        return {};
    }

    std::string bendPresetName (BendPreset p)
    {
        switch (p)
        {
            case BendPreset::None:              return "No bend";
            case BendPreset::Bend:              return "Bend";
            case BendPreset::BendAndRelease:    return "Bend + release";
            case BendPreset::PreBend:           return "Pre-bend";
            case BendPreset::PreBendAndRelease: return "Pre-bend + release";
            case BendPreset::BendHold:          return "Slow bend + hold";
            case BendPreset::QuarterBend:       return "Quarter-tone bend";
        }
        return {};
    }

    std::vector<BendPoint> makeBend (BendPreset preset, double st)
    {
        switch (preset)
        {
            case BendPreset::None:              return {};
            case BendPreset::Bend:              return { { 0.0, 0 }, { 0.25, st }, { 1.0, st } };
            case BendPreset::BendAndRelease:    return { { 0.0, 0 }, { 0.25, st }, { 0.55, st }, { 0.8, 0 }, { 1.0, 0 } };
            case BendPreset::PreBend:           return { { 0.0, st }, { 1.0, st } };
            case BendPreset::PreBendAndRelease: return { { 0.0, st }, { 0.3, st }, { 0.6, 0 }, { 1.0, 0 } };
            case BendPreset::BendHold:          return { { 0.0, 0 }, { 0.6, st }, { 1.0, st } };
            case BendPreset::QuarterBend:       return { { 0.0, 0 }, { 0.2, 0.5 }, { 1.0, 0.5 } };
        }
        return {};
    }

    std::string slideTypeName (SlideType s)
    {
        switch (s)
        {
            case SlideType::None:         return "No slide";
            case SlideType::SlideTo:      return "Slide to fret";
            case SlideType::SlideInBelow: return "Slide in from below";
            case SlideType::SlideInAbove: return "Slide in from above";
            case SlideType::SlideOutDown: return "Slide out down";
            case SlideType::SlideOutUp:   return "Slide out up";
        }
        return {};
    }

    // -----------------------------------------------------------------------
    void Phrase::add (const NoteEvent& n)
    {
        notes.push_back (n);
        sort();
    }

    void Phrase::addChord (const Voicing& v, double start, double length, int velocity,
                           bool down, double strumMs, double bpm, Technique technique)
    {
        // Played strings from low (high index) to high (index 0).
        std::vector<int> strings;
        for (int s = (int) v.frets.size() - 1; s >= 0; --s)
            if (v.frets[(size_t) s] != kMuted)
                strings.push_back (s);

        if (! down)
            std::reverse (strings.begin(), strings.end());

        const double totalSpread = (strumMs / 1000.0) * (bpm / 60.0); // ms -> beats
        const int group = nextGroup++;
        const int count = (int) strings.size();

        for (int i = 0; i < count; ++i)
        {
            const double offset = count > 1 ? totalSpread * i / (count - 1) : 0.0;
            NoteEvent n;
            n.string    = strings[(size_t) i];
            n.fret      = v.frets[(size_t) n.string];
            n.start     = start + offset;
            n.length    = std::max (0.05, length - offset);
            n.velocity  = std::clamp (velocity - i * 2, 1, 127); // later strings a little softer, like a real strum
            n.technique = technique;
            n.group     = group;
            notes.push_back (n);
        }
        sort();
    }

    void Phrase::removeAt (double beat, int string)
    {
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const NoteEvent& n)
        {
            return n.string == string && beat >= n.start && beat < n.start + n.length;
        }), notes.end());
    }

    void Phrase::clear()
    {
        notes.clear();
        nextGroup = 0;
    }

    void Phrase::sort()
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });
    }

    double Phrase::endBeat() const
    {
        double end = 0;
        for (const auto& n : notes)
            end = std::max (end, n.start + n.length);
        return end;
    }

    int soundingNote (const NoteEvent& n, const Instrument& inst)
    {
        const int fretted = inst.noteAt ({ n.string, n.fret });
        const int open    = inst.noteAt ({ n.string, 0 });

        switch (n.technique)
        {
            case Technique::NaturalHarmonic:
                switch (n.fret)
                {
                    case 12: case 24:       return open + (n.fret == 24 ? 24 : 12);
                    case 7:  case 19:       return open + 19;
                    case 5:                 return open + 24;
                    case 4:  case 9: case 16: return open + 28;
                    case 3:                 return open + 31;
                    default:                return fretted + 12; // not a real node: act like a touch harmonic
                }

            case Technique::PinchHarmonic:
                return fretted + 19;

            default:
                return fretted;
        }
    }

    // -----------------------------------------------------------------------
    std::string phraseToText (const Phrase& p)
    {
        std::ostringstream out;
        out << "L " << p.lengthBeats << ' ' << p.nextGroup << '\n';
        for (const auto& n : p.notes)
        {
            out << "N " << n.string << ' ' << n.fret << ' ' << n.start << ' ' << n.length << ' ' << n.velocity << ' '
                << (int) n.technique << ' ' << (int) n.slide << ' ' << n.slideToFret << ' ' << n.slideStart << ' '
                << n.slideDistance << ' ' << (int) n.vibrato << ' ' << n.vibratoDepth << ' ' << n.vibratoRateHz << ' '
                << n.vibratoDelay << ' ' << (int) n.letRing << ' ' << n.group << ' ' << n.bend.size();
            for (const auto& b : n.bend)
                out << ' ' << b.position << ' ' << b.semitones;
            out << '\n';
        }
        return out.str();
    }

    Phrase phraseFromText (const std::string& text)
    {
        Phrase p;
        std::istringstream in (text);
        std::string line;

        while (std::getline (in, line))
        {
            std::istringstream ls (line);
            std::string tag;
            ls >> tag;

            if (tag == "L")
            {
                ls >> p.lengthBeats >> p.nextGroup;
            }
            else if (tag == "N")
            {
                NoteEvent n;
                int tech = 0, slide = 0, vib = 0, ring = 0;
                size_t bendCount = 0;
                ls >> n.string >> n.fret >> n.start >> n.length >> n.velocity >> tech >> slide >> n.slideToFret
                   >> n.slideStart >> n.slideDistance >> vib >> n.vibratoDepth >> n.vibratoRateHz >> n.vibratoDelay
                   >> ring >> n.group >> bendCount;

                if (! ls)
                    continue;

                n.technique = (Technique) std::clamp (tech, 0, (int) Technique::NumTechniques - 1);
                n.slide     = (SlideType) std::clamp (slide, 0, (int) SlideType::SlideOutUp);
                n.vibrato   = vib != 0;
                n.letRing   = ring != 0;

                for (size_t i = 0; i < bendCount && i < 64; ++i)
                {
                    BendPoint b;
                    if (ls >> b.position >> b.semitones)
                        n.bend.push_back (b);
                }
                p.notes.push_back (n);
            }
        }
        p.sort();
        return p;
    }
}
