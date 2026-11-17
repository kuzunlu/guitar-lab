#include "Midi/MidiRenderer.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        enum Order { OffOrder = 0, ControlOrder = 1, OnOrder = 2 };

        MidiEvent msg (double beat, int order, int status, int d1, int d2, int size = 3)
        {
            MidiEvent e;
            e.beat = beat;
            e.order = order;
            e.data[0] = (uint8_t) status;
            e.data[1] = (uint8_t) std::clamp (d1, 0, 127);
            e.data[2] = (uint8_t) std::clamp (d2, 0, 127);
            e.size = size;
            return e;
        }

        MidiEvent noteOn  (double b, int ch, int note, int vel) { return msg (b, OnOrder,  0x90 | (ch - 1), note, std::max (1, vel)); }
        MidiEvent noteOff (double b, int ch, int note)          { return msg (b, OffOrder, 0x80 | (ch - 1), note, 0); }
        MidiEvent cc      (double b, int ch, int num, int val)  { return msg (b, ControlOrder, 0xB0 | (ch - 1), num, val); }

        MidiEvent bendMsg (double b, int ch, int value)
        {
            return msg (b, ControlOrder, 0xE0 | (ch - 1), value & 0x7F, (value >> 7) & 0x7F);
        }

        // Registered parameter (RPN) message: 4 CCs.
        void rpn (std::vector<MidiEvent>& out, int ch, int paramMsb, int paramLsb, int valueMsb, int valueLsb)
        {
            out.push_back (cc (0, ch, 101, paramMsb));
            out.push_back (cc (0, ch, 100, paramLsb));
            out.push_back (cc (0, ch, 6,   valueMsb));
            out.push_back (cc (0, ch, 38,  valueLsb));
            out.push_back (cc (0, ch, 101, 127)); // "null" RPN so later data entry does nothing
            out.push_back (cc (0, ch, 100, 127));
        }

        // Linear interpolation of the bend curve at position 0..1.
        double bendAt (const std::vector<BendPoint>& pts, double pos)
        {
            if (pts.empty())             return 0.0;
            if (pos <= pts.front().position) return pts.front().semitones;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                if (pos <= pts[i].position)
                {
                    const auto& a = pts[i - 1];
                    const auto& b = pts[i];
                    const double span = b.position - a.position;
                    double t = span <= 0 ? 1.0 : (pos - a.position) / span;
                    t = t * t * (3.0 - 2.0 * t); // smooth start and end, like a real finger
                    return a.semitones + (b.semitones - a.semitones) * t;
                }
            }
            return pts.back().semitones;
        }

        bool isLegato (Technique t)
        {
            return t == Technique::HammerOn || t == Technique::PullOff || t == Technique::Tap;
        }

        bool hasMovingPitch (const NoteEvent& n)
        {
            return ! n.bend.empty() || n.slide != SlideType::None || n.vibrato;
        }

        // Pitch offset (semitones) at "pos" (0..1) of the note. seconds = time since note start.
        double pitchOffset (const NoteEvent& n, double pos, double seconds, double lengthBeats)
        {
            double st = bendAt (n.bend, pos);

            switch (n.slide)
            {
                case SlideType::SlideTo:
                {
                    const double from = n.slideStart;
                    const double to   = from + (1.0 - from) * 0.8; // arrive a bit before the end
                    if (pos > from)
                    {
                        const double t = std::min (1.0, (pos - from) / std::max (0.001, to - from));
                        st += (n.slideToFret - n.fret) * t;
                    }
                    break;
                }
                case SlideType::SlideInBelow:
                case SlideType::SlideInAbove:
                {
                    const double inLen = std::min (0.25, 0.5 / std::max (0.25, lengthBeats)); // about half a beat max
                    const double sign  = n.slide == SlideType::SlideInBelow ? -1.0 : 1.0;
                    if (pos < inLen)
                        st += sign * n.slideDistance * (1.0 - pos / inLen);
                    break;
                }
                case SlideType::SlideOutDown:
                case SlideType::SlideOutUp:
                {
                    const double sign = n.slide == SlideType::SlideOutDown ? -1.0 : 1.0;
                    if (pos > n.slideStart)
                        st += sign * n.slideDistance * (pos - n.slideStart) / std::max (0.001, 1.0 - n.slideStart);
                    break;
                }
                case SlideType::None:
                    break;
            }

            if (n.vibrato && pos > n.vibratoDelay)
            {
                const double vibPos = (pos - n.vibratoDelay) / std::max (0.001, 1.0 - n.vibratoDelay);
                const double fadeIn = std::min (1.0, vibPos * 4.0);
                st += n.vibratoDepth * fadeIn * std::sin (2.0 * kPi * n.vibratoRateHz * seconds);
            }

            return st;
        }
    }

    int pitchBendValue (double semitones, int bendRange)
    {
        const double range = std::max (1, bendRange);
        const int v = (int) std::lround (8192.0 + semitones / range * 8192.0);
        return std::clamp (v, 0, 16383);
    }

    std::vector<MidiEvent> setupMessages (const MidiSettings& s, int numStrings)
    {
        std::vector<MidiEvent> out;

        // MPE Configuration Message: RPN 6 on the master channel (1). Value = number of member channels.
        if (s.sendMpeConfig)
            rpn (out, 1, 0, 6, std::clamp (numStrings, 1, 15), 0);

        // Pitch bend sensitivity (RPN 0) on every string channel.
        for (int str = 0; str < numStrings; ++str)
        {
            const int ch = s.channelForString (str);
            rpn (out, ch, 0, 0, std::clamp (s.bendRange, 1, 96), 0);
            out.push_back (bendMsg (0, ch, 8192));
        }
        return out;
    }

    std::vector<MidiEvent> renderPhrase (const Phrase& phrase, const Instrument& inst, const MidiSettings& s, double bpm)
    {
        std::vector<MidiEvent> out;
        bpm = std::max (1.0, bpm);
        const double loopEnd = phrase.lengthBeats;

        for (int str = 0; str < inst.numStrings(); ++str)
        {
            const int ch = s.channelForString (str);

            std::vector<const NoteEvent*> notes;
            for (const auto& n : phrase.notes)
                if (n.string == str && n.start < loopEnd)
                    notes.push_back (&n);

            std::stable_sort (notes.begin(), notes.end(), [] (auto* a, auto* b) { return a->start < b->start; });

            int lastBend = 8192;

            for (size_t i = 0; i < notes.size(); ++i)
            {
                const NoteEvent& n = *notes[i];
                const NoteEvent* next = i + 1 < notes.size() ? notes[i + 1] : nullptr;

                // --- Length ---
                double length = n.length;
                if (n.technique == Technique::PalmMute) length *= s.palmMuteLengthScale;
                if (n.technique == Technique::DeadNote) length = std::min (length, s.deadNoteLengthBeats);

                double end = n.start + length;
                if (n.letRing)
                    end = next != nullptr ? next->start : loopEnd;

                // One string = one sounding note. The next note cuts this one (with overlap for legato).
                if (next != nullptr)
                {
                    const bool nextLegato = isLegato (next->technique)
                                          || (n.slide == SlideType::SlideTo && next->fret == n.slideToFret);
                    // A legato note that follows straight on (no gap) overlaps a little, so the synth glides.
                    if (nextLegato && s.legatoOverlap && end >= next->start - 1e-6)
                        end = next->start + s.legatoOverlapBeats;
                    else
                        end = std::min (end, next->start);
                }
                end = std::min (end, loopEnd);
                if (end <= n.start)
                    end = n.start + 0.01;

                // --- Velocity ---
                double vel = n.velocity;
                if (isLegato (n.technique) && n.technique != Technique::Tap) vel *= s.legatoVelocityScale;
                if (n.technique == Technique::PalmMute) vel *= s.palmMuteVelocityScale;
                if (n.technique == Technique::DeadNote) vel *= s.deadNoteVelocityScale;

                const int pitch = std::clamp (soundingNote (n, inst), 0, 127);

                // --- Keyswitch / CC ---
                const int ks = s.keyswitch[(int) n.technique];
                if (ks >= 0)
                {
                    const int ksCh = s.keyswitchChannel > 0 ? s.keyswitchChannel : ch;
                    out.push_back (msg (n.start, ControlOrder, 0x90 | (ksCh - 1), ks, 100));
                    out.push_back (msg (n.start + 0.01, OffOrder, 0x80 | (ksCh - 1), ks, 0));
                }

                if (s.palmMuteCC >= 0 && n.technique == Technique::PalmMute)
                {
                    out.push_back (cc (n.start, ch, s.palmMuteCC, 127));
                    out.push_back (cc (end, ch, s.palmMuteCC, 0));
                }

                // --- Pitch bend at the start (reset, or pre-bend) ---
                const double lenBeats = end - n.start;
                const int startBend = pitchBendValue (pitchOffset (n, 0.0, 0.0, lenBeats), s.bendRange);
                if (startBend != lastBend)
                {
                    out.push_back (bendMsg (n.start, ch, startBend));
                    lastBend = startBend;
                }

                out.push_back (noteOn (n.start, ch, pitch, (int) std::lround (vel)));

                // --- Moving pitch: bends, slides, vibrato ---
                if (hasMovingPitch (n))
                {
                    for (double b = n.start + s.bendStepBeats; b < end; b += s.bendStepBeats)
                    {
                        const double pos = (b - n.start) / std::max (0.001, n.length); // shape follows the written length
                        const double secs = (b - n.start) * 60.0 / bpm;
                        const int v = pitchBendValue (pitchOffset (n, std::min (pos, 1.0), secs, lenBeats), s.bendRange);
                        if (v != lastBend)
                        {
                            out.push_back (bendMsg (b, ch, v));
                            lastBend = v;
                        }
                    }
                }

                out.push_back (noteOff (end, ch, pitch));
            }

            // Leave the channel clean at the loop end.
            if (lastBend != 8192)
                out.push_back (bendMsg (loopEnd - 0.001, ch, 8192));
        }

        std::stable_sort (out.begin(), out.end(), [] (const MidiEvent& a, const MidiEvent& b)
        {
            if (a.beat != b.beat) return a.beat < b.beat;
            return a.order < b.order;
        });
        return out;
    }
}
