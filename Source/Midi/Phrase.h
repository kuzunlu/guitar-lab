#pragma once

#include "Theory/Instrument.h"
#include "Theory/Voicing.h"

#include <string>
#include <vector>

namespace gl
{
    enum class Technique
    {
        Normal,
        HammerOn,        // legato: no new pick attack, softer
        PullOff,         // legato: no new pick attack, softer
        Tap,             // legato, but strong
        PalmMute,        // short and a bit softer
        DeadNote,        // muted "chk" sound: very short, quiet
        NaturalHarmonic, // pitch comes from the harmonic node (12th = +12, 7th = +19, 5th = +24 ...)
        PinchHarmonic,   // fretted note + 19 semitones (octave + fifth)
        NumTechniques
    };

    std::string techniqueName (Technique t);

    // Bend shape: points over the note's length.
    struct BendPoint
    {
        double position  = 0.0; // 0 = note start, 1 = note end
        double semitones = 0.0; // 1 = half step, 2 = whole step
    };

    enum class BendPreset
    {
        None, Bend, BendAndRelease, PreBend, PreBendAndRelease, BendHold, QuarterBend
    };

    std::string bendPresetName (BendPreset p);
    std::vector<BendPoint> makeBend (BendPreset preset, double semitones);

    enum class SlideType
    {
        None,
        SlideTo,         // glide from this fret to slideToFret during the note (legato)
        SlideInBelow,    // start a few frets below and glide up into the note
        SlideInAbove,    // start a few frets above and glide down into the note
        SlideOutDown,    // fall off down the neck at the end
        SlideOutUp       // slide up the neck at the end
    };

    std::string slideTypeName (SlideType s);

    struct NoteEvent
    {
        int    string = 0;
        int    fret   = 0;
        double start  = 0.0;  // beats from the phrase start
        double length = 1.0;  // beats
        int    velocity = 100;

        Technique technique = Technique::Normal;

        std::vector<BendPoint> bend;   // empty = no bend

        SlideType slide = SlideType::None;
        int    slideToFret   = 0;      // used by SlideTo
        double slideStart    = 0.5;    // where in the note the slide begins (0..1), SlideTo / SlideOut
        int    slideDistance = 3;      // frets, for slide in/out

        bool   vibrato = false;
        double vibratoDepth = 0.4;     // semitones up and down
        double vibratoRateHz = 5.5;
        double vibratoDelay = 0.3;     // 0..1 of note length before vibrato starts

        bool   letRing = false;        // ring until the next note on the same string

        int    group = -1;             // notes of the same chord share a group number
    };

    struct Phrase
    {
        std::vector<NoteEvent> notes;
        double lengthBeats = 16.0;     // loop length (4 bars of 4/4)
        int    nextGroup = 0;

        void add (const NoteEvent& n);

        // Adds a chord shape. strumMs spreads the notes in time; down = low string first.
        void addChord (const Voicing& v, double start, double length, int velocity,
                       bool down, double strumMs, double bpm, Technique technique = Technique::Normal);

        void removeAt (double beat, int string);
        void clear();
        void sort();

        // The last beat used by any note.
        double endBeat() const;
    };

    // Sounding pitch for a note, including harmonics. Bends/slides are added on top as pitch bend.
    int soundingNote (const NoteEvent& n, const Instrument& inst);

    // ---- Plain-text save format (stored in the plugin state) ----
    std::string phraseToText (const Phrase& p);
    Phrase phraseFromText (const std::string& text);
}
