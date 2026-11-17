// Plain tests for the music logic. No test framework: run the exe, it prints failures and returns 1.

#include "Midi/MidiRenderer.h"
#include "Theory/Reharm.h"
#include "Theory/Trainer.h"
#include "Theory/Voicing.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    template <typename A, typename B>
    void checkEq (const A& a, const B& b, const std::string& what)
    {
        check (a == b, what);
    }
}

using namespace gl;

static void testNotes()
{
    checkEq (parsePitchClass ("C").value_or (-1), 0, "C = 0");
    checkEq (parsePitchClass ("Bb").value_or (-1), 10, "Bb = 10");
    checkEq (parsePitchClass ("F#").value_or (-1), 6, "F# = 6");
    checkEq (parseMidiNote ("E2").value_or (-1), 40, "E2 = 40");
    checkEq (parseMidiNote ("C4").value_or (-1), 60, "C4 = 60");
    checkEq (parseMidiNote ("Cb4").value_or (-1), 59, "Cb4 = 59");
    checkEq (midiNoteName (64), std::string ("E4"), "64 = E4");
    check (! parsePitchClass ("H").has_value(), "H is not a note");
}

static void testInstrument()
{
    Instrument g;
    checkEq (g.numStrings(), 6, "default 6 strings");
    checkEq (g.noteAt ({ 5, 0 }), 40, "low E open = E2");
    checkEq (g.noteAt ({ 0, 24 }), 88, "high E 24th fret = E6");
    checkEq (g.tuningLabel(), std::string ("E A D G B E"), "standard label");

    g.setNumStrings (8);
    checkEq (g.numStrings(), 8, "8 strings");
    checkEq (g.noteAt ({ 7, 0 }), 30, "8-string low F#1");

    g.setNumStrings (7);
    checkEq (g.noteAt ({ 6, 0 }), 35, "7-string low B1");

    checkEq ((int) g.positionsOf (64).size(), 6, "E4 appears on 6 strings of a 7-string (24 frets)");
}

static void testChords()
{
    Key c { 0, false };
    auto prog = parseProgression ("I-V-vi-IV", c);
    checkEq ((int) prog.size(), 4, "I-V-vi-IV parses to 4 chords");
    if (prog.size() == 4)
    {
        checkEq (prog[0].name(), std::string ("C"),  "I = C");
        checkEq (prog[1].name(), std::string ("G"),  "V = G");
        checkEq (prog[2].name(), std::string ("Am"), "vi = Am");
        checkEq (prog[3].name(), std::string ("F"),  "IV = F");
    }

    auto mixed = parseProgression ("Cmaj7 | Am7 | Dm7 G7", c);
    checkEq ((int) mixed.size(), 4, "chord symbols parse");

    auto minus = parseProgression ("C-7 F7", c);
    checkEq ((int) minus.size(), 2, "C-7 is one chord");
    if (minus.size() == 2) checkEq (minus[0].quality, ChordQuality::Minor7, "C-7 = Cm7");

    auto sec = parseRomanNumeral ("V/V", c);
    check (sec && sec->root == 2 && sec->quality == ChordQuality::Major, "V/V in C = D");

    auto bvii = parseRomanNumeral ("bVII7", c);
    check (bvii && bvii->root == 10 && bvii->quality == ChordQuality::Dominant7, "bVII7 = Bb7");

    checkEq (romanLabel ({ 10, ChordQuality::Major }, c), std::string ("bVII"), "Bb label in C = bVII");
    checkEq (romanLabel ({ 9, ChordQuality::Minor7 }, c), std::string ("vi7"), "Am7 label = vi7");

    std::string err;
    auto bad = parseProgression ("I V xyz", c, &err);
    check (bad.empty() && ! err.empty(), "bad token gives an error");

    checkEq (c.diatonicChord (4, true).quality, ChordQuality::Dominant7, "V7 in C is dominant");
    checkEq (c.diatonicChord (6, true).quality, ChordQuality::HalfDiminished7, "vii in C is m7b5");
}

static void testReharm()
{
    Key c { 0, false };
    auto prog = toTimed (parseProgression ("I V vi IV", c));
    auto subs = findSuggestions (prog, c);
    check (! subs.empty(), "suggestions exist");

    bool tritone = false, borrowedIv = false, passingDim = false;
    for (auto& s : subs)
    {
        if (s.kind == SubKind::TritoneSub && s.index == 1 && s.replacement[0].chord.root == 1) tritone = true;
        if (s.kind == SubKind::BorrowedChord && s.index == 3 && s.replacement[0].chord.quality == ChordQuality::Minor) borrowedIv = true;
        if (s.kind == SubKind::PassingDiminished) passingDim = true;
    }
    check (tritone, "G -> Db7 tritone sub found");
    check (borrowedIv, "F -> Fm borrowed found");
    check (passingDim, "passing diminished found");

    // Insertions keep the total length the same.
    for (auto& s : subs)
    {
        auto result = applySuggestion (prog, s);
        double total = 0;
        for (auto& t : result) total += t.beats;
        check (std::abs (total - 16.0) < 1e-9, "total length stays 16 beats after " + subKindName (s.kind));
    }

    ReharmOptions all;
    all.amount = 1.0;
    auto full = reharmonize (prog, c, all);
    check (full.size() >= prog.size(), "full reharm has at least as many chords");

    ReharmOptions none;
    none.amount = 0.0;
    auto same = reharmonize (prog, c, none);
    checkEq ((int) same.size(), 4, "amount 0 changes nothing");
}

static void testVoicings()
{
    Instrument g;
    VoicingOptions opt;
    auto cMajor = findVoicings ({ 0, ChordQuality::Major }, g, opt);
    check (! cMajor.empty(), "C major voicings found");

    bool openC = false;
    for (auto& v : cMajor)
        if (voicingToText (v) == "x32010") openC = true;
    check (openC, "open C shape x32010 is found");

    for (auto& v : cMajor)
    {
        check (v.span() <= opt.maxSpan, "span within limit");
        auto notes = v.notes (g);
        check (! notes.empty() && pitchClassOf (notes.front()) == 0, "root in bass");
    }

    Instrument eight;
    eight.setNumStrings (8);
    auto g7 = findVoicings ({ 7, ChordQuality::Dominant7 }, eight, opt);
    check (! g7.empty(), "G7 voicings on 8-string");

    Key c { 0, false };
    auto led = voiceLead (parseProgression ("ii7 V7 Imaj7", c), g, opt);
    checkEq ((int) led.size(), 3, "voice leading returns one voicing per chord");
}

static void testTrainer()
{
    Trainer t (7);
    Instrument g;
    t.setInstrument (g);

    QuizFocus f;
    f.strings = { false, false, true, false, false, false }; // only the G string
    f.minFret = 0; f.maxFret = 12;
    t.setFocus (f);

    auto q = t.next (QuizMode::NameTheNote);
    check (q.shown.size() == 1 && q.shown[0].string == 2, "question uses the chosen string");

    check (t.answerPitchClass (q, q.targetPc, 1.0), "right answer is right");
    checkEq (t.streak(), 1, "streak counts");

    auto find = t.next (QuizMode::FindTheNote);
    auto correct = t.correctPositions (find);
    check (t.answerPositions (find, correct, 2.0), "clicking all correct spots passes");

    auto saved = t.serialise();
    Trainer t2;
    t2.deserialise (saved);
    checkEq (t2.totalAsked(), t.totalAsked(), "stats survive save/load");
}

static void testMidi()
{
    Instrument g;
    MidiSettings s;
    s.bendRange = 12;

    Phrase p;
    p.lengthBeats = 4;

    NoteEvent bent;
    bent.string = 1; bent.fret = 15; bent.start = 0; bent.length = 2;
    bent.bend = makeBend (BendPreset::Bend, 2.0);
    p.add (bent);

    NoteEvent hammer;
    hammer.string = 1; hammer.fret = 17; hammer.start = 2; hammer.length = 1;
    hammer.technique = Technique::HammerOn;
    p.add (hammer);

    auto events = renderPhrase (p, g, s, 120);

    int onCount = 0, bendCount = 0;
    bool rightChannel = true;
    for (auto& e : events)
    {
        const int status = e.data[0] & 0xF0, ch = (e.data[0] & 0x0F) + 1;
        if (status == 0x90) { ++onCount; if (ch != 3) rightChannel = false; }
        if (status == 0xE0) ++bendCount;
    }
    checkEq (onCount, 2, "two note-ons");
    check (rightChannel, "B string (index 1) plays on channel 3");
    check (bendCount > 5, "bend makes many pitch bend messages");

    checkEq (pitchBendValue (0, 12), 8192, "no bend = centre");
    checkEq (pitchBendValue (12, 12), 16383, "full bend clamps to max");
    checkEq (pitchBendValue (-12, 12), 0, "full down = 0");

    // Legato: hammer-on starts before the bent note ends.
    double bentOff = -1, hammerOn = -1;
    for (auto& e : events)
    {
        if ((e.data[0] & 0xF0) == 0x80 && e.data[1] == 59 + 15) bentOff = e.beat;
        if ((e.data[0] & 0xF0) == 0x90 && e.data[1] == 59 + 17) hammerOn = e.beat;
    }
    check (hammerOn >= 0 && bentOff > hammerOn, "legato overlap: next note-on before previous note-off");

    auto setup = setupMessages (s, 6);
    check (setup.size() >= 6 * 7, "setup has RPN for every string");

    auto text = phraseToText (p);
    auto back = phraseFromText (text);
    checkEq ((int) back.notes.size(), 2, "phrase survives save/load");
    if (back.notes.size() == 2) checkEq ((int) back.notes[0].bend.size(), 3, "bend points saved");
}

int main()
{
    testNotes();
    testInstrument();
    testChords();
    testReharm();
    testVoicings();
    testTrainer();
    testMidi();

    if (failures == 0)
        std::printf ("All tests passed.\n");
    return failures == 0 ? 0 : 1;
}
