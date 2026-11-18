#pragma once

#include "Midi/Phrase.h"

#include <string>

namespace gl
{
    // Short tab text for one note, as on screen: "h7", "PM5", "<12>", "7b2r", "5/9", "7~", "x".
    std::string noteTabText (const NoteEvent& n);

    struct TabExportInfo
    {
        std::string title = "Guitar Lab phrase";
        double bpm = 120.0;
        bool   flats = false;
        bool   chordNames = true;   // chord names above the tab (from the chord timeline)
        int    barsPerLine = 4;
    };

    // Plain text tab (e|--3--5--|), with a short legend of the symbols.
    std::string phraseToTextTab (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info);

    // A printable PDF (A4 pages): title, tempo, tuning, then tab lines with bar lines, bar numbers and symbols.
    // Returns the file's bytes.
    std::string phraseToPdf (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info);

    // MusicXML 3.1 with a tab staff (string + fret for every note, bends, harmonics, hammer-ons / pull-offs).
    // Opens in MuseScore, Guitar Pro, Sibelius, Finale.
    std::string phraseToMusicXml (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info);

    // Guitar Pro 3 (.gp3): opens in Guitar Pro 3-8, TuxGuitar, MuseScore and Songsterr. Strings + frets, chords, rhythm,
    // bends, hammer-ons / pull-offs, slides, vibrato, natural harmonics, dead notes, let ring (no palm mute in GP3).
    // Up to 7 strings. Returns the file's bytes.
    std::string phraseToGuitarPro3 (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info);
}
