// Plain tests for the music logic. No test framework: run the exe, it prints failures and returns 1.

#include "Midi/MidiRenderer.h"
#include "Midi/PhraseRecorder.h"
#include "Midi/TabExport.h"
#include "Midi/ChordTimeline.h"
#include "Theory/ChordFinder.h"
#include "Theory/Reharm.h"
#include "Theory/Trainer.h"
#include "Theory/Voicing.h"
#include "Tracking/GuitarTracker.h"

#include <cmath>
#include <cstdio>
#include <set>
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

static void testChordNames()
{
    auto best = [] (std::vector<int> notes) { auto m = nameChord (notes); return m.empty() ? std::string ("-") : m[0].name; };
    auto offers = [] (std::vector<int> notes, const std::string& name)
    {
        for (const auto& m : nameChord (notes)) if (m.name == name) return true;
        return false;
    };
    auto meaningHas = [] (std::vector<int> notes, const std::string& text)
    {
        auto m = nameChord (notes);
        return ! m.empty() && m[0].meaning.find (text) != std::string::npos;
    };
    // MIDI: C3 = 48. Lowest note = bass.
    struct Case { std::vector<int> notes; const char* name; };
    const std::vector<Case> cases {
        // Triads and their inversions
        { { 48, 52, 55 }, "C" },          { { 52, 55, 60 }, "C/E" },        { { 55, 60, 64 }, "C/G" },
        { { 48, 51, 55 }, "Cm" },         { { 48, 51, 54 }, "Cdim" },       { { 48, 52, 56 }, "Caug" },
        { { 48, 53, 55 }, "Csus4" },      { { 48, 50, 55 }, "Csus2" },      { { 48, 51, 56 }, "Ab/C" },
        { { 43, 47, 52 }, "Em/G" },       { { 48, 52, 55, 60, 64 }, "C" },  { { 45, 52, 57, 60, 64 }, "Am" },
        // Sevenths
        { { 48, 52, 55, 58 }, "C7" },     { { 48, 52, 55, 59 }, "Cmaj7" },  { { 48, 51, 55, 58 }, "Cm7" },
        { { 48, 51, 54, 58 }, "Cm7b5" },  { { 48, 51, 54, 57 }, "Cdim7" },  { { 48, 51, 55, 59 }, "Cm(maj7)" },
        { { 48, 52, 56, 58 }, "C7#5" },   { { 48, 52, 56, 59 }, "Cmaj7#5" },{ { 48, 53, 55, 58 }, "C7sus4" },
        { { 43, 47, 53 }, "G7" },         { { 43, 47, 50, 53 }, "G7" },     { { 47, 50, 53, 57 }, "Bm7b5" },
        { { 43, 45, 48, 52 }, "Am7/G" },  { { 45, 48, 52, 55 }, "Am7" },    { { 48, 53, 58 }, "C7sus4" },
        // Sixths, add chords, extensions
        { { 48, 52, 55, 57 }, "C6" },     { { 48, 51, 55, 57 }, "Cm6" },    { { 48, 52, 55, 57, 62 }, "C6/9" },
        { { 48, 52, 55, 62 }, "Cadd9" },  { { 48, 51, 55, 62 }, "Cmadd9" }, { { 48, 50, 52, 55 }, "Cadd9" },
        { { 48, 52, 55, 58, 62 }, "C9" }, { { 48, 52, 55, 59, 62 }, "Cmaj9" }, { { 48, 51, 55, 58, 62 }, "Cm9" },
        { { 48, 51, 58, 62, 65 }, "Cm11" }, { { 40, 45, 50, 55, 59 }, "Em11" }, // "So What" voicing
        { { 48, 52, 58, 62, 69 }, "C13" }, { { 48, 52, 59, 62, 69 }, "Cmaj13" },
        // Altered
        { { 48, 52, 58, 61 }, "C7b9" },   { { 48, 52, 55, 58, 63 }, "C7#9" }, { { 40, 47, 52, 56, 62, 67 }, "E7#9" },
        { { 48, 52, 59, 66 }, "Cmaj7(#11)" }, { { 48, 52, 58, 62, 66, 69 }, "C13#11" },
        { { 48, 52, 58, 61, 63, 68 }, "C7(b9,#9,b13)" }, { { 48, 52, 54, 58 }, "C7b5" },
        // Over a bass note, power chords, two notes
        { { 43, 53, 57, 60 }, "G9sus4" }, { { 48, 50, 54, 57 }, "D7/C" },
        { { 45, 52 }, "A5" },             { { 48, 55, 60 }, "C5" },         { { 43, 48 }, "C5/G" },
        { { 48, 52 }, "C + E" },
    };
    for (const auto& c : cases)
        check (best (c.notes) == c.name, std::string ("chord name: ") + c.name + " (got " + best (c.notes) + ")");

    check (offers ({ 43, 53, 57, 60 }, "F/G"), "chord name: G F A C is also offered as F/G");
    check (offers ({ 48, 50, 54, 57 }, "D/C"), "chord name: C D F# A is also offered as D/C");
    check (offers ({ 52, 55, 58, 62 }, "C9") || best ({ 52, 55, 58, 62 }) == "Em7b5", "chord name: rootless C9 reads as Em7b5 first");
    check (meaningHas ({ 48, 51, 54, 57 }, "Bbb = diminished 7th"), "chord name: Cdim7 spells its 7th as Bbb");
    check (meaningHas ({ 48, 52, 55, 58, 63 }, "D# = sharp 9th"), "chord name: C7#9 spells the #9 as D#, not Eb");
    check (meaningHas ({ 52, 55, 60 }, "E in the bass"), "chord name: inversions say which note is in the bass");
    checkEq (best ({ 48, 60 }), std::string ("-"), "chord name: an octave is not a chord");
    check (nameChord ({ 48, 51, 54, 57 }).size() >= 4, "chord name: dim7 can be named from any of its notes");
    checkEq (nameChord ({ 49, 53, 56 }, true)[0].name, std::string ("Db"), "chord name: flats when asked (Db, not C#)");
    checkEq (nameChord ({ 49, 52, 56 })[0].name, std::string ("C#m"), "chord name: C#m (not Dbm)");

    // Every possible set of note names gets a name (no crash, never empty).
    int unnamed = 0;
    for (unsigned mask = 1; mask < 4096; ++mask)
    {
        std::vector<int> notes;
        for (int pc = 0; pc < 12; ++pc) if (mask & (1u << pc)) notes.push_back (48 + pc);
        const auto m = nameChord (notes);
        if (notes.size() >= 2 && (m.empty() || m[0].name.empty())) ++unnamed;
    }
    checkEq (unnamed, 0, "chord name: all 4095 note sets get a name");
}

static void testTabExport()
{
    Instrument g;
    Phrase p;
    p.lengthBeats = 8.0;
    auto note = [&] (int s, int f, double start, Technique t = Technique::Normal)
    {
        NoteEvent n;
        n.string = s; n.fret = f; n.start = start; n.length = 0.5; n.technique = t;
        p.notes.push_back (n);
        return &p.notes.back();
    };
    note (5, 0, 0.0);                                   // low E open
    note (3, 5, 1.0);
    note (3, 7, 1.5, Technique::HammerOn);
    auto* b = note (2, 7, 2.0);
    b->bend = makeBend (BendPreset::BendAndRelease, 2.0);
    note (0, 12, 4.0, Technique::NaturalHarmonic);
    p.sort();

    TabExportInfo info;
    info.title = "Test (riff)";
    const auto text = phraseToTextTab (p, g, info);
    check (text.find ("e|") != std::string::npos && text.find ("E|0") != std::string::npos, "text tab: string names, open low E at the start");
    check (text.find ("h7") != std::string::npos && text.find ("7b2r") != std::string::npos && text.find ("<12>") != std::string::npos,
           "text tab: hammer-on, bend + release and harmonic symbols");
    check (text.find ("Tuning (low to high): E2 A2 D3 G3 B3 E4") != std::string::npos, "text tab: tuning line");

    const auto pdf = phraseToPdf (p, g, info);
    check (pdf.rfind ("%PDF-1.4", 0) == 0 && pdf.find ("%%EOF") != std::string::npos && pdf.find ("xref") != std::string::npos,
           "pdf: header, cross-reference table and end marker");
    check (pdf.find ("(Test \\(riff\\)) Tj") != std::string::npos, "pdf: title written, brackets escaped");
    check (pdf.find ("(7b2r) Tj") != std::string::npos, "pdf: tab symbols written");
    {
        // The cross-reference offsets must point at the objects.
        const auto xref = pdf.find ("xref\n0 ");
        const auto firstEntry = pdf.find ("0000000000 65535 f \n", xref) + 20;
        const size_t obj1 = (size_t) std::stoul (pdf.substr (firstEntry, 10));
        check (pdf.compare (obj1, 8, "1 0 obj\n") == 0, "pdf: object offsets are right");
    }

    const auto xml = phraseToMusicXml (p, g, info);
    check (xml.find ("<sign>TAB</sign>") != std::string::npos && xml.find ("<staff-lines>6</staff-lines>") != std::string::npos,
           "musicxml: tab staff with 6 lines");
    check (xml.find ("<string>6</string><fret>0</fret>") != std::string::npos, "musicxml: low E open as string 6, fret 0");
    check (xml.find ("<hammer-on type=\"start\">H</hammer-on>") != std::string::npos
           && xml.find ("<hammer-on type=\"stop\"/>") != std::string::npos, "musicxml: hammer-on from fret 5 to 7");
    check (xml.find ("<bend-alter>2.0</bend-alter><release/>") != std::string::npos, "musicxml: bend with release");
    check (xml.find ("<harmonic><natural/></harmonic>") != std::string::npos, "musicxml: natural harmonic");
    {
        // Every measure adds up to 4 beats (48 divisions).
        size_t pos = 0;
        int measures = 0, wrong = 0;
        while ((pos = xml.find ("<measure number=", pos)) != std::string::npos)
        {
            const auto end = xml.find ("</measure>", pos);
            const auto body = xml.substr (pos, end - pos);
            int total = 0;
            size_t q = 0;
            while ((q = body.find ("<note>", q)) != std::string::npos)
            {
                const auto noteEnd = body.find ("</note>", q);
                const auto n = body.substr (q, noteEnd - q);
                if (n.find ("<chord/>") == std::string::npos)
                {
                    const auto d = n.find ("<duration>") + 10;
                    total += std::stoi (n.substr (d, n.find ("</duration>") - d));
                }
                q = noteEnd;
            }
            if (total != 48) ++wrong;
            ++measures;
            pos = end;
        }
        check (measures == 2 && wrong == 0, "musicxml: 2 measures, each exactly 4 beats long");
    }
}

static void testChordSymbols()
{
    auto ivs = [] (const char* text)
    {
        auto c = parseChordName (text);
        if (! c) return std::string ("none");
        auto v = c->intervals;
        std::sort (v.begin(), v.end());
        std::string s;
        for (int i : v) s += std::to_string (i) + " ";
        return s;
    };
    checkEq (ivs ("C"), std::string ("0 4 7 "), "symbol: C");
    checkEq (ivs ("Cmaj7"), std::string ("0 4 7 11 "), "symbol: Cmaj7");
    checkEq (ivs ("CM7"), std::string ("0 4 7 11 "), "symbol: CM7 = Cmaj7");
    checkEq (ivs ("C\xCE\x94" "7"), std::string ("0 4 7 11 "), "symbol: C(delta)7 = Cmaj7");
    checkEq (ivs ("C-7"), std::string ("0 3 7 10 "), "symbol: C-7 = Cm7");
    checkEq (ivs ("Cm7b5"), std::string ("0 3 6 10 "), "symbol: Cm7b5");
    checkEq (ivs ("C\xC3\xB8"), std::string ("0 3 6 10 "), "symbol: C(o-slash) = half-diminished");
    checkEq (ivs ("Cdim7"), std::string ("0 3 6 9 "), "symbol: Cdim7");
    checkEq (ivs ("Cm(maj7)"), std::string ("0 3 7 11 "), "symbol: Cm(maj7)");
    checkEq (ivs ("C7#9"), std::string ("0 3 4 7 10 "), "symbol: C7#9");
    checkEq (ivs ("C7b9"), std::string ("0 1 4 7 10 "), "symbol: C7b9 has no natural 9");
    checkEq (ivs ("C13"), std::string ("0 2 4 7 9 10 "), "symbol: C13");
    checkEq (ivs ("C6/9"), std::string ("0 2 4 7 9 "), "symbol: C6/9 (the /9 is not a bass note)");
    checkEq (ivs ("Csus2"), std::string ("0 2 7 "), "symbol: Csus2");
    checkEq (ivs ("C7sus4"), std::string ("0 5 7 10 "), "symbol: C7sus4");
    checkEq (ivs ("Cadd9"), std::string ("0 2 4 7 "), "symbol: Cadd9");
    checkEq (ivs ("C5"), std::string ("0 7 "), "symbol: C5 power chord");
    checkEq (ivs ("C+"), std::string ("0 4 8 "), "symbol: C+ = augmented");
    checkEq (ivs ("C7(no3)"), std::string ("0 7 10 "), "symbol: C7(no3)");
    checkEq (ivs ("Cmaj7#11"), std::string ("0 4 6 7 11 "), "symbol: Cmaj7#11");
    checkEq (ivs ("Hm7"), std::string ("none"), "symbol: H is not a note name");
    checkEq (ivs ("Cxyz"), std::string ("none"), "symbol: nonsense is refused");
    {
        auto d = parseChordName ("D/F#");
        check (d && d->root == 2 && d->bass == 6, "symbol: D/F# has F# in the bass");
        auto bb = parseChordName ("Bbm7");
        check (bb && bb->root == 10, "symbol: Bbm7 root is Bb");
    }

    // Round trip: symbol -> notes -> name gives the same symbol back.
    for (const char* name : { "C", "Cm", "C7", "Cmaj7", "Cm7", "Cm7b5", "Cdim7", "Caug", "Csus4", "Csus2", "C6", "Cm6",
                              "C9", "Cmaj9", "Cm9", "C13", "C7b9", "C7#9", "C7sus4", "Cadd9", "C6/9", "F#m7", "Bb7", "Ebmaj7" })
    {
        auto c = parseChordName (name);
        std::vector<int> notes;
        if (c) for (int iv : c->intervals) notes.push_back (48 + c->root + iv);
        const auto named = nameChord (notes);
        check (! named.empty() && named[0].name == name,
               std::string ("symbol round trip: ") + name + " (got " + (named.empty() ? "-" : named[0].name) + ")");
    }

    // Shapes for typed chords.
    Instrument g;
    VoicingOptions opt;
    opt.minStrings = 3;
    auto shapes = [&] (const char* name, bool rootInBass = true)
    {
        auto o = opt;
        o.rootInBass = rootInBass;
        std::vector<std::string> out;
        for (const auto& v : findShapes (shapeRequestFor (*parseChordName (name)), g, o, 500)) out.push_back (voicingToText (v));
        return out;
    };
    auto contains = [] (const std::vector<std::string>& v, const char* s) { return std::find (v.begin(), v.end(), s) != v.end(); };
    check (contains (shapes ("C"), "x32010"), "shapes: open C is found");
    check (contains (shapes ("C"), "x35553"), "shapes: barre C at the 3rd fret is found");
    check (contains (shapes ("D/F#"), "2x0232"), "shapes: D/F# with F# on the low E string is found");
    check (contains (shapes ("Cmaj7"), "x32000"), "shapes: open Cmaj7 is found");
    {
        const auto e = shapes ("E7#9");
        std::string some;
        for (size_t i = 0; i < e.size() && i < 12; ++i) some += e[i] + " ";
        check (contains (e, "x7678x"), "shapes: the 'Hendrix' E7#9 is found (" + std::to_string (e.size()) + " found: " + some + ")");
    }
    check (shapes ("C").size() > 20, "shapes: many C shapes all over the neck");

    // The fingering most guitarists play comes first.
    struct Usual { const char* chord; const char* shape; };
    for (const auto& u : std::vector<Usual> {
             { "E", "022100" }, { "A", "x02220" }, { "C", "x32010" }, { "G", "320003" }, { "D", "xx0232" },
             { "Am", "x02210" }, { "Em", "022000" }, { "Dm", "xx0231" }, { "F", "133211" }, { "Bm", "x24432" },
             { "Bb", "x13331" }, { "F#m", "244222" }, { "Cm", "x35543" }, { "C#m", "x46654" },
             { "E7", "020100" }, { "A7", "x02020" }, { "B7", "x21202" }, { "Am7", "x02010" }, { "Cmaj7", "x32000" },
             { "Bb7", "x13131" }, { "Cm7", "x35343" }, { "Asus4", "x02230" }, { "E5", "022xxx" } })
    {
        const auto c = commonShapes (*parseChordName (u.chord), g);
        const std::string first = c.empty() ? "-" : voicingToText (c.front().voicing);
        check (first == u.shape, std::string ("usual fingering first: ") + u.chord + " = " + u.shape + " (got " + first + ")");
    }
    {
        const auto e = commonShapes (*parseChordName ("E"), g);
        check (e.size() >= 3 && e[0].kind == "Open chord" && e[1].kind.rfind ("Barre", 0) == 0,
               "usual fingerings: open E first, then the barre shapes up the neck");
    }
    {
        auto o = opt;
        int wrong = 0;
        for (const auto& v : findShapes (shapeRequestFor (*parseChordName ("D/F#")), g, o, 200))
        {
            const auto n = v.notes (g);
            if (pitchClassOf (*std::min_element (n.begin(), n.end())) != 6) ++wrong;
        }
        checkEq (wrong, 0, "shapes: every D/F# shape has F# as its lowest note");
    }
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

static void testTimedProgression()
{
    Key c { 0, false };
    auto beatsOf = [&] (const std::string& text)
    {
        std::string s;
        for (auto& tc : parseTimedProgression (text, c))
            s += tc.chord.name() + ":" + std::to_string ((int) std::lround (tc.beats * 100)) + " ";
        return s;
    };
    checkEq (beatsOf ("C G Am F"), std::string ("C:400 G:400 Am:400 F:400 "), "no lengths: one bar each");
    checkEq (beatsOf ("C:2 G:2 Am F:8"), std::string ("C:200 G:200 Am:400 F:800 "), "typed lengths");
    checkEq (beatsOf ("| C G | Am | F |"), std::string ("C:200 G:200 Am:400 F:400 "), "two chords in a bar share it");
    checkEq (beatsOf ("|C:3 G|Am7 D7 G|"), std::string ("C:300 G:100 Am7:133 D7:133 G:133 "), "bar: typed length first, the rest shares");
    checkEq (beatsOf ("I-V:2 vi:1.5"), std::string ("C:200 G:200 Am:150 "), "length on a dashed word and a numeral");
    checkEq (beatsOf ("C-7:2 F7"), std::string ("Cm7:200 F7:400 "), "C-7 with a length");

    std::string err;
    check (parseTimedProgression ("C:x G", c, &err).empty() && err.find ("C:x") != std::string::npos, "bad length is an error");
    check (parseTimedProgression ("C:0", c).empty(), "zero length is an error");
    check (parseTimedProgression ("I V xyz", c, &err).empty() && ! err.empty(), "bad chord is an error");

    // Back to text: typed names are kept, 4 beats is not written.
    checkEq (progressionToText (parseTimedProgression ("vi:1.5 IV Cmaj7:2", c), false), std::string ("vi:1.5 IV Cmaj7:2"), "text round trip");
    checkEq (progressionToText (parseTimedProgression ("| C G | F |", c), false), std::string ("C:2 G:2 F"), "bars written as lengths");
    auto subs = toTimed ({ Chord { 1, ChordQuality::Dominant7 } }, 2.25);
    checkEq (progressionToText (subs, true), std::string ("Db7:2.25"), "engine chord written by name");
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

static void testGuitarPro3()
{
    Instrument g;
    Phrase p;
    p.lengthBeats = 8.0;
    auto add = [&] (int s, int f, double start, double len, Technique t = Technique::Normal)
    {
        NoteEvent n; n.string = s; n.fret = f; n.start = start; n.length = len; n.technique = t;
        p.notes.push_back (n);
    };
    add (5, 0, 0.0, 0.5);
    add (3, 5, 0.5, 0.5);
    add (3, 7, 1.0, 0.5, Technique::HammerOn);
    add (2, 7, 2.0, 2.0);
    p.notes.back().bend = makeBend (BendPreset::Bend, 2.0);
    add (1, 5, 4.0, 1.0); add (0, 5, 4.0, 1.0);             // a two-note chord
    add (4, 3, 5.0 + 1.0 / 3.0, 1.0 / 3.0);                 // a triplet note
    p.sort();

    const auto f = phraseToGuitarPro3 (p, g, {});

    // A small reader, the way Guitar Pro reads the file.
    size_t at = 0;
    bool ok = true;
    auto u8 = [&] { if (at >= f.size()) { ok = false; return 0; } return (int) (unsigned char) f[at++]; };
    auto i8 = [&] { return (int) (signed char) u8(); };
    auto i32 = [&] { int v = 0; for (int k = 0; k < 4; ++k) v |= u8() << (8 * k); return v; };
    auto skip = [&] (size_t n) { at += n; if (at > f.size()) ok = false; };

    const int vlen = u8();
    check (vlen == 24 && f.compare (1, 24, "FICHIER GUITAR PRO v3.00") == 0, "gp3: version string");
    at = 31;
    for (int k = 0; k < 8; ++k) { const int len = i32(); skip ((size_t) len); }   // info strings
    const int notice = i32();
    check (notice == 0, "gp3: no notice lines");
    u8(); const int tempo = i32(); i32();
    checkEq (tempo, 120, "gp3: tempo");
    skip (64 * 12);
    const int measures = i32(), tracks = i32();
    check (measures == 2 && tracks == 1, "gp3: 2 measures, 1 track");
    for (int m = 0; m < measures; ++m) { const int fl = u8(); if (fl & 1) i8(); if (fl & 2) i8(); }
    u8(); skip (41); const int strings = i32();
    checkEq (strings, 6, "gp3: 6 strings");
    std::vector<int> tuning;
    for (int k = 0; k < 7; ++k) tuning.push_back (i32());
    check (tuning[0] == 64 && tuning[5] == 40 && tuning[6] == 0, "gp3: tuning, highest string first");
    i32(); i32(); i32(); const int frets = i32(); i32(); skip (4);
    checkEq (frets, 24, "gp3: 24 frets");

    int hammers = 0, bends = 0, chordBeats = 0, wrongBars = 0;
    for (int m = 0; m < measures && ok; ++m)
    {
        const int beats = i32();
        double length = 0.0;
        for (int b = 0; b < beats && ok; ++b)
        {
            const int fl = u8();
            if (fl & 0x40) u8();
            const int code = i8();
            double beatsLong = 4.0 / std::pow (2.0, code + 2);   // -2 = whole (4 beats) ... 2 = 16th
            if (fl & 0x01) beatsLong *= 1.5;
            if (fl & 0x20) { const int t = i32(); beatsLong *= 2.0 / t; }
            if (fl & 0x08) u8();
            length += beatsLong;
            const int sf = u8();
            int count = 0;
            for (int s = 1; s <= 7; ++s)
                if (sf & (1 << (7 - s)))
                {
                    ++count;
                    const int nf = u8();
                    if (nf & 0x20) u8();
                    if (nf & 0x20) i8();
                    if (nf & 0x08)
                    {
                        const int ef = u8();
                        if (ef & 0x02) ++hammers;
                        if (ef & 0x01)
                        {
                            ++bends;
                            i8(); i32();
                            const int points = i32();
                            for (int q = 0; q < points; ++q) { i32(); i32(); u8(); }
                        }
                    }
                }
            if (count == 2) ++chordBeats;
        }
        if (std::abs (length - 4.0) > 1e-6) ++wrongBars;
    }
    const int end = i32();
    check (ok && end == 0 && at == f.size(), "gp3: the whole file reads back to its exact end");
    checkEq (wrongBars, 0, "gp3: every bar is exactly 4 beats");
    check (hammers == 1 && bends >= 1 && chordBeats >= 1, "gp3: hammer-on, bend and the two-note chord are there");
}

static void testChordTimeline()
{
    Instrument g;
    Phrase p;
    p.lengthBeats = 16.0;
    auto strum = [&] (const char* shape, double beat)
    {
        Voicing v { std::vector<int> (6, kMuted) };
        for (int i = 0; i < 6; ++i) v.frets[(size_t) (5 - i)] = shape[i] == 'x' ? kMuted : shape[i] - '0';
        p.addChord (v, beat, 4.0, 90, true, 15.0, 120.0);
    };
    strum ("x02210", 0.0);   // Am
    strum ("xx3211", 4.0);   // F
    strum ("x32010", 8.0);   // C
    // Bar 4: G as an arpeggio, one note per 8th, letting ring
    const int g6[] = { 3, 2, 0, 0, 0, 3 };
    for (int i = 0; i < 6; ++i)
    {
        NoteEvent n;
        n.string = 5 - i; n.fret = g6[i]; n.start = 12.0 + i * 0.5; n.length = 0.5; n.letRing = true;
        p.notes.push_back (n);
    }
    p.sort();

    const auto spans = chordTimeline (p, g);
    std::string names;
    for (const auto& s : spans) names += s.name + " ";
    checkEq (names, std::string ("Am F C G "), "chord timeline: strummed chords and an arpeggio (got " + names + ")");
    check (spans.size() == 4 && spans[1].start == 4.0 && spans[3].end == 16.0, "chord timeline: chords start and end on their bars");
    checkEq (chordsForReharm (spans), std::string ("Am F C G"), "chord timeline: line for the Reharm Lab");
    {
        std::vector<ChordSpan> uneven { { 0, 2, "Am", {} }, { 2, 4, "Am7", {} }, { 4, 6, "F", {} }, { 6, 8, "F", {} }, { 8, 9.5, "G", {} } };
        checkEq (chordsForReharm (uneven), std::string ("Am:2 Am7:2 F G:1.5"), "chord timeline: lengths for the Reharm Lab");
    }

    // A single-note melody has no chords.
    Phrase melody;
    for (int i = 0; i < 8; ++i) { NoteEvent n; n.string = 1; n.fret = i % 3 * 2; n.start = i * 0.5; n.length = 0.5; melody.notes.push_back (n); }
    check (chordTimeline (melody, g).empty(), "chord timeline: a melody gives no chords");

    // Exports carry the chord names.
    TabExportInfo info;
    const auto text = phraseToTextTab (p, g, info);
    check (text.find ("Am") != std::string::npos && text.find ("\n  1") != std::string::npos, "text tab: chord names line");
    const auto xml = phraseToMusicXml (p, g, info);
    check (xml.find ("<root-step>A</root-step></root><kind text=\"m\">minor</kind>") != std::string::npos, "musicxml: Am chord symbol");
    check (phraseToPdf (p, g, info).find ("(Am) Tj") != std::string::npos, "pdf: chord names");
}

static void testEarTraining()
{
    Trainer t (11);
    Instrument g;
    t.setInstrument (g);
    QuizFocus f;
    f.strings.assign (6, true);
    f.minFret = 0;
    f.maxFret = 12;
    t.setFocus (f);

    for (int i = 0; i < 30; ++i)
    {
        const auto q = t.next (QuizMode::EarInterval);
        const int d = q.shown.size() == 2 ? g.noteAt (q.shown[1]) - g.noteAt (q.shown[0]) : -1;
        check (d >= 1 && d <= 12 && d == q.interval && ! q.playTogether, "ear interval: two notes, low then high, up to an octave");
        check (t.answerInterval (q, q.interval % 12, 1.0), "ear interval: the right answer counts (octave = button 0)");
    }

    int seen = 0;
    for (int i = 0; i < 40; ++i)
    {
        const auto q = t.next (QuizMode::EarChord);
        std::set<int> heard, wanted;
        for (auto p : q.shown) heard.insert (pitchClassOf (g.noteAt (p)));
        for (auto pc : q.chord.pitchClasses()) wanted.insert (pc);
        bool inside = true;
        for (int pc : heard) inside = inside && wanted.count (pc) > 0;
        check (q.shown.size() >= 3 && inside && q.playTogether, "ear chord: a real shape, only chord notes, strummed");
        check (pitchClassOf (g.noteAt (q.shown.back())) == q.chord.root || q.shown.size() < 3, "ear chord: the lowest note is the root");
        check (t.answerChordQuality (q, q.chord.quality, 1.0), "ear chord: the right type counts");
        check (! t.answerChordQuality (q, q.chord.quality == ChordQuality::Major ? ChordQuality::Minor : ChordQuality::Major, 1.0),
               "ear chord: a wrong type does not count");
        seen += (int) heard.size();
    }

    const auto q = t.next (QuizMode::EarPlayBack);
    check (q.shown.size() == 1 && t.answerPitchClass (q, q.targetPc, 1.0), "ear play-back: the played note name is the answer");
    check (isEarQuiz (QuizMode::EarChord) && ! isEarQuiz (QuizMode::NameTheNote), "ear quizzes are marked");
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

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------
namespace
{
    constexpr double kRate = 48000.0;

    // Guitar-like test tone: sawtooth (rich in harmonics) with a decay. freqAt(t) allows bends.
    template <typename FreqFn>
    std::vector<float> pluck (double seconds, FreqFn freqAt, double amp = 0.5, double decay = 1.5)
    {
        std::vector<float> out ((size_t) (seconds * kRate));
        double phase = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = i / kRate;
            phase += freqAt (t) / kRate;
            phase -= std::floor (phase);
            out[i] = (float) ((2.0 * phase - 1.0) * amp * std::exp (-t * decay));
        }
        return out;
    }

    std::vector<float> silenceOf (double seconds) { return std::vector<float> ((size_t) (seconds * kRate), 0.0f); }

    void append (std::vector<float>& a, const std::vector<float>& b) { a.insert (a.end(), b.begin(), b.end()); }

    struct Captured { int offset; int status; int d1; int d2; };

    std::vector<Captured> runTracker (GuitarTracker& t, const std::vector<float>& audio)
    {
        std::vector<Captured> out;
        int base = 0;
        for (size_t pos = 0; pos < audio.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, audio.size() - pos);
            const float* chans[1] = { audio.data() + pos };
            t.process (chans, 1, n,
                       [&] (int off, uint8_t s, uint8_t a, uint8_t b) { out.push_back ({ base + off, s, a, b }); },
                       [] (const AnalysisFrame&) {});
            base += n;
        }
        return out;
    }

    int count (const std::vector<Captured>& ev, int statusHigh)
    {
        int c = 0;
        for (auto& e : ev) if ((e.status & 0xF0) == statusHigh && (statusHigh != 0x90 || e.d2 > 0)) ++c;
        return c;
    }
}

static void testPitchDetector()
{
    for (double hz : { 46.25, 82.41, 110.0, 196.0, 329.63, 1318.5 })
    {
        PitchDetector d;
        d.prepare (kRate, 40.0, 1500.0);
        auto audio = pluck (0.3, [hz] (double) { return hz; }, 0.5, 0.5);
        PitchDetector::Result last;
        d.process (audio.data(), (int) audio.size(), [&] (int, const PitchDetector::Result& r) { last = r; });
        const double centsOff = 1200.0 * std::log2 (last.hz / hz);
        check (std::abs (centsOff) < 10.0, "pitch of " + std::to_string (hz) + " Hz found within 10 cents (got "
                                             + std::to_string (last.hz) + ")");
    }

    checkEq ((int) std::lround (hzToMidi (440.0)), 69, "440 Hz = A4 = 69");
    check (std::abs (midiToHz (40) - 82.4069) < 0.01, "E2 = 82.41 Hz");
}

static void testTracker()
{
    Instrument g;
    Calibration cal;
    cal.valid = true;
    cal.noiseFloor = 0.001f;
    cal.softPeak = 0.05f;
    cal.hardPeak = 0.5f;

    TrackerSettings s;
    s.mode = TrackMode::Mono;
    s.monoInput = 0;

    // One plucked A2 (MIDI 45), then silence.
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto audio = pluck (0.6, [] (double) { return 110.0; });
        append (audio, silenceOf (0.3));
        auto ev = runTracker (t, audio);

        checkEq (count (ev, 0x90), 1, "one pluck = one note-on");
        checkEq (count (ev, 0x80), 1, "silence = note-off");
        for (auto& e : ev)
            if ((e.status & 0xF0) == 0x90)
            {
                checkEq (e.d1, 45, "plucked note is A2 (45)");
                check (e.offset < (int) (0.06 * kRate), "note starts within 60 ms");
            }
    }

    // A whole-step bend: pitch bend messages, no second note.
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto audio = pluck (0.8, [] (double tm) { return tm < 0.2 ? 110.0 : 110.0 * std::pow (2.0, std::min (1.0, (tm - 0.2) / 0.3) * 2.0 / 12.0); }, 0.5, 0.8);
        auto ev = runTracker (t, audio);
        checkEq (count (ev, 0x90), 1, "a bend does not start new notes");
        check (count (ev, 0xE0) > 10, "a bend sends pitch bend");
    }

    // Hammer-on: sudden jump A2 -> B2 without a new pluck = second (legato) note.
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto audio = pluck (0.7, [] (double tm) { return tm < 0.3 ? 110.0 : 123.47; }, 0.5, 0.8);
        auto ev = runTracker (t, audio);
        checkEq (count (ev, 0x90), 2, "hammer-on gives a second note");
        bool gotB2 = false;
        for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && e.d1 == 47) gotB2 = true;
        check (gotB2, "hammer-on note is B2 (47)");
    }

    // An overtone heard for a moment (A3 for 20 ms inside a ringing A2 = 2 readings) is not a new note...
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto audio = pluck (0.7, [] (double tm) { return tm >= 0.3 && tm < 0.32 ? 220.0 : 110.0; }, 0.5, 0.8);
        auto ev = runTracker (t, audio);
        checkEq (count (ev, 0x90), 1, "a short overtone blip inside a note starts no new note");
    }

    // ...but a real big jump (a tap from A2 to A3) still is.
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto audio = pluck (0.7, [] (double tm) { return tm < 0.3 ? 110.0 : 220.0; }, 0.5, 0.8);
        auto ev = runTracker (t, audio);
        bool gotA3 = false;
        for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && e.d1 == 57) gotA3 = true;
        check (gotA3, "a tap an octave up gives the new note A3 (57)");
    }

    // Hex mode: string index 1 (B string) on input channel 1 plays on MIDI channel 3.
    {
        TrackerSettings hs = s;
        hs.mode = TrackMode::Hex;
        GuitarTracker t;
        Calibration hc = cal;
        t.prepare (kRate, 512, g, hc, hs);

        auto b = pluck (0.4, [] (double) { return 246.94; }); // B3, open B string
        std::vector<std::vector<float>> chans (6, std::vector<float> (b.size(), 0.0f));
        chans[1] = b;

        std::vector<Captured> ev;
        for (size_t pos = 0; pos < b.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, b.size() - pos);
            const float* ptrs[6];
            for (int c = 0; c < 6; ++c) ptrs[c] = chans[(size_t) c].data() + pos;
            t.process (ptrs, 6, n, [&] (int off, uint8_t st, uint8_t a, uint8_t bb) { ev.push_back ({ off, st, a, bb }); },
                       [] (const AnalysisFrame&) {});
        }
        bool ok = false;
        for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && (e.status & 0x0F) == 2 && e.d1 == 59) ok = true;
        check (ok, "hex: B string note on MIDI channel 3");
    }
}

static void testCalibration()
{
    Instrument g;
    CalibrationSession cs;
    cs.start (g, false, 1);
    const double hop = 0.005;
    const AnalysisFrame quiet { 0, 0.0f, 0.0f, 0.0004f, 0.0005f };
    // The player presses Listen, the strings are quiet for a moment, then the note comes.
    auto listen = [&] { cs.listen(); for (int i = 0; i < 3; ++i) cs.feed (quiet, hop); };
    auto play = [&] (double midi, int frames = 300)
    {
        for (int i = 0; i < frames && cs.isListening(); ++i)
            cs.feed ({ 0, (float) midiToHz (midi), 0.97f, 0.1f, 0.3f }, hop);
    };

    // Nothing happens before Listen is pressed.
    for (int i = 0; i < 700; ++i) cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);
    check (cs.step() == CalibrationSession::Step::Silence && cs.phase() == CalibrationSession::Phase::Ready,
           "calibration waits for Listen");

    // Silence
    cs.listen();
    for (int i = 0; i < 700 && cs.step() == CalibrationSession::Step::Silence; ++i)
        cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);
    check (cs.step() == CalibrationSession::Step::OpenString, "silence step finishes");
    check (cs.phase() == CalibrationSession::Phase::Ready, "after OK the next item waits for Listen");

    // A note that still rings from before is not taken: only a new pick attack counts.
    {
        const double ringing = cs.expectedNote() + 5.0; // the wrong note, still sounding when Listen is pressed
        cs.listen();
        for (int i = 0; i < 300; ++i) cs.feed ({ 0, (float) midiToHz (ringing), 0.97f, 0.1f, 0.3f * std::pow (0.995f, (float) i) }, hop);
        check (cs.isListening() && cs.step() == CalibrationSession::Step::OpenString && cs.status().find ("That") == std::string::npos,
               "a note still ringing after Listen is ignored");
        cs.stop();
    }

    // Open strings: play every string 10 cents flat.
    int guard = 0;
    while (cs.step() == CalibrationSession::Step::OpenString && guard++ < 20)
    {
        listen();
        play (cs.expectedNote() - 0.10);
    }
    check (cs.step() == CalibrationSession::Step::TwelfthFret, "all open strings measured");
    check (std::abs (cs.result().strings[5].centsOffset + 10.0) < 1.0, "low E measured as 10 cents flat");

    // Back: the high E is forgotten and measured again, 20 cents flat this time.
    cs.back();
    check (cs.step() == CalibrationSession::Step::OpenString && cs.currentString() == 0 && ! cs.result().strings[0].measured,
           "Back returns to the item before and forgets its result");
    check (cs.result().strings[5].measured, "Back keeps the items before it");
    listen();
    play (cs.expectedNote() - 0.20);
    check (cs.step() == CalibrationSession::Step::TwelfthFret && std::abs (cs.result().strings[0].centsOffset + 20.0) < 1.0,
           "the item is measured again after Back");

    // 12th fret, low string: play it 80 cents sharp -> needs a decision.
    {
        listen();
        play (cs.expectedNote() + 0.8);
        check (cs.waitingForUser(), "badly out-of-tune note asks the player");
        cs.skip();
    }

    // 12th fret on the high E, in tune -> accepted, next step.
    {
        const int before = cs.stepNumber();
        listen();
        play (64 + 12); // correct: high E 12th fret
        check (cs.stepNumber() == before + 1, "12th fret on the high string accepted");
    }

    // Nothing played for more than 10 seconds: back to Ready.
    {
        cs.listen();
        for (int i = 0; i < 2200; ++i) cs.feed (quiet, hop);
        check (! cs.isListening() && cs.status().find ("Nothing clear") != std::string::npos, "listening stops after 10 s of nothing");
    }

    // Soft and hard: 4 plucks each.
    auto hits = [&] (float peak)
    {
        listen();
        for (int h = 0; h < 4; ++h)
        {
            for (int i = 0; i < 20; ++i) cs.feed ({ 0, 196.0f, 0.95f, peak * 0.5f, i < 3 ? peak : peak * 0.4f }, hop);
            for (int i = 0; i < 20; ++i) cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
        }
    };
    hits (0.04f);
    check (cs.step() == CalibrationSession::Step::Hard, "soft step done after 4 plucks");
    hits (0.5f);
    check (cs.finished(), "calibration finished");
    check (cs.result().valid, "result is valid");
    check (cs.result().velocityFromPeak (0.04f) < 45 && cs.result().velocityFromPeak (0.5f) > 100, "velocity follows soft/hard");

    Calibration copy;
    copy.deserialise (cs.result().serialise());
    check (copy.valid && std::abs (copy.strings[5].centsOffset - cs.result().strings[5].centsOffset) < 0.01,
           "calibration survives save/load");
}

// ---------------------------------------------------------------------------
// Full calibration: spectrum, every note, chords
// ---------------------------------------------------------------------------
namespace
{
    // Several sawtooth notes together (a strummed chord), decaying.
    std::vector<float> chordAudio (const std::vector<int>& notes, double seconds, double amp = 0.15)
    {
        std::vector<float> out ((size_t) (seconds * kRate), 0.0f);
        for (int n : notes)
        {
            const double hz = midiToHz (n);
            double phase = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                phase += hz / kRate;
                phase -= std::floor (phase);
                out[i] += (float) ((2.0 * phase - 1.0) * amp * std::exp (-(i / kRate) * 1.2));
            }
        }
        return out;
    }

    // Runs a tracker with features on and returns the frames that carry features.
    std::vector<AnalysisFrame> featureFrames (const Instrument& g, const std::vector<float>& audio)
    {
        Calibration none;
        TrackerSettings fs;
        fs.mode = TrackMode::Mono;
        fs.monoInput = 0;
        fs.features = true;
        GuitarTracker t;
        t.prepare (kRate, 512, g, none, fs);

        std::vector<AnalysisFrame> frames;
        for (size_t pos = 0; pos < audio.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, audio.size() - pos);
            const float* chans[1] = { audio.data() + pos };
            t.process (chans, 1, n, [] (int, uint8_t, uint8_t, uint8_t) {},
                       [&] (const AnalysisFrame& f) { if (f.hasFeatures) frames.push_back (f); });
        }
        return frames;
    }
}

static void testSpectrum()
{
    constexpr double rate = 24000.0;
    std::vector<float> triad (4096, 0.0f);
    for (int n : { 48, 52, 55 }) // C3 E3 G3
    {
        double phase = 0.0;
        for (auto& s : triad)
        {
            phase += midiToHz (n) / rate;
            phase -= std::floor (phase);
            s += (float) (2.0 * phase - 1.0) * 0.2f;
        }
    }

    Spectrum spec;
    spec.prepare (4096);
    spec.compute (triad.data());
    const auto c = spec.chroma (rate, 0.0);

    std::vector<int> order (12);
    for (int i = 0; i < 12; ++i) order[(size_t) i] = i;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return c[(size_t) a] > c[(size_t) b]; });
    std::vector<int> top { order[0], order[1], order[2] };
    std::sort (top.begin(), top.end());
    check (top == std::vector<int> ({ 0, 4, 7 }), "C major chord: strongest note names are C, E, G");

    // Tone colour: a sawtooth has strong harmonics, a sine has none.
    std::vector<float> saw (2048), sine (2048);
    double ph = 0.0;
    for (size_t i = 0; i < saw.size(); ++i)
    {
        ph += 220.0 / rate; ph -= std::floor (ph);
        saw[i]  = (float) (2.0 * ph - 1.0);
        sine[i] = (float) std::sin (2.0 * 3.14159265358979 * 220.0 * i / rate);
    }
    Spectrum ts;
    ts.prepare (2048);
    ts.compute (saw.data());
    const auto tSaw = ts.timbre (220.0, rate);
    ts.compute (sine.data());
    const auto tSine = ts.timbre (220.0, rate);
    check (std::abs (tSaw[0] + 1.0f) < 0.4f, "sawtooth 2nd harmonic is about half the fundamental");
    check (timbreDistance (tSaw, tSine) > 3.0f, "sawtooth and sine have very different tone colour");
    check (chromaSimilarity (c, c) > 0.999f, "a chord is similar to itself");
}

static void testFullCalibration()
{
    Instrument g;
    CalibrationSession cs;
    cs.start (g, false, 1, true);
    const double hop = 0.005;
    auto listen = [&] { cs.listen(); for (int i = 0; i < 3; ++i) cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop); };

    cs.listen();
    for (int i = 0; i < 700 && cs.step() == CalibrationSession::Step::Silence; ++i)
        cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);

    // Every note. Each string gets its own made-up tone colour. Fret 3 of the low E is first played wrong.
    bool wrongFretTested = false, endTested = false;
    int guard = 0;
    while (cs.step() == CalibrationSession::Step::EveryNote && guard++ < 400)
    {
        const int s = cs.currentString();
        const int fret = cs.expectedFret();

        if (s == 4 && fret == 23 && ! endTested) // "my guitar has 22 frets", said on the A string
        {
            cs.noMoreFrets();
            endTested = true;
            continue;
        }

        int note = cs.expectedNote();
        if (s == 5 && fret == 3 && ! wrongFretTested)
            note += 1; // wrong fret

        AnalysisFrame f { 0, (float) midiToHz (note), 0.97f, 0.1f, 0.3f };
        f.hasFeatures = true;
        f.timbre.fill (-0.5f * (float) s);
        f.hasOvertones = true; // the tracker sends the attack's overtones while calibrating
        for (int k = 0; k < kOvertones; ++k) f.overtones[(size_t) k] = -0.3f * (float) k - 0.1f * (float) s;

        listen();
        for (int i = 0; i < 200; ++i)
        {
            cs.feed (f, hop);
            if (s == 5 && fret == 3 && ! wrongFretTested && cs.status().find ("That was fret 4") != std::string::npos)
            {
                wrongFretTested = true;
                break;
            }
            if (cs.expectedFret() != fret || cs.currentString() != s || cs.waitingForUser() || ! cs.isListening()) break;
        }
        // Let the note stop, so the next one is a new pluck.
        for (int i = 0; i < 3; ++i) cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
    }
    check (wrongFretTested, "every note: a wrong fret is noticed");
    check (endTested, "every note: 'my guitar ends here' works");
    check (cs.step() == CalibrationSession::Step::Chord, "every note done, chord step next (mono)");
    checkEq (cs.result().lastFret[0], 22, "22-fret guitar remembered for all strings");
    check (cs.result().positions[5][7].measured && cs.result().positions[5][7].hasTimbre, "low E fret 7 measured with tone colour");
    check (std::abs (cs.result().positions[2][5].timbre[0] + 1.0f) < 0.01f, "tone colour stored per string");

    // Chords: strum each shown shape 3 times; fingerprints match the shape.
    const auto shapes = CalibrationSession::chordShapesFor (g);
    check (shapes.size() >= 30, "full calibration asks for 30+ chords (open, 7ths, sus, barre, power chords)");
    {
        bool power = false, barre = false, seventh = false;
        for (const auto& sh : shapes)
        {
            if (sh.name == "E5") power = true;
            if (sh.name == "F") barre = std::find (sh.frets.begin(), sh.frets.end(), 0) == sh.frets.end();
            if (sh.name == "B7") seventh = true;
        }
        check (power && barre && seventh, "chord list has power chords, a barre F and B7");
    }
    guard = 0;
    while (cs.step() == CalibrationSession::Step::Chord && guard++ < 200)
    {
        const auto* ch = cs.currentChord();
        Chroma ideal {};
        for (int s = 0; s < (int) ch->frets.size(); ++s)
            if (ch->frets[(size_t) s] >= 0) ideal[(size_t) pitchClassOf (g.tuning()[(size_t) s] + ch->frets[(size_t) s])] += 0.5f;

        AnalysisFrame f { 0, 0.0f, 0.3f, 0.1f, 0.3f };
        f.hasFeatures = true;
        f.chroma = ideal;
        listen();
        for (int strum = 0; strum < 3; ++strum)
        {
            for (int i = 0; i < 40; ++i) cs.feed (f, hop);
            for (int i = 0; i < 5; ++i)  cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
        }
    }
    check (cs.step() == CalibrationSession::Step::Soft, "all chords learned");
    checkEq ((int) cs.result().chords.size(), (int) shapes.size(), "one fingerprint per chord shape");

    // Soft and hard picking on every string; the high strings are picked a little louder here.
    for (int step = 0; step < 12 && ! cs.finished(); ++step)
    {
        const int s = cs.currentString();
        const float peak = (cs.step() == CalibrationSession::Step::Soft ? 0.04f : 0.5f) * (s < 3 ? 1.5f : 1.0f);
        listen();
        for (int h = 0; h < 4; ++h)
        {
            for (int i = 0; i < 20; ++i) cs.feed ({ 0, 196.0f, 0.95f, peak * 0.5f, i < 3 ? peak : peak * 0.4f }, hop);
            for (int i = 0; i < 20; ++i) cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
        }
    }
    check (cs.finished(), "full calibration: soft and hard picking asked on every string, then done");
    check (cs.result().stringSoft[0] > cs.result().stringSoft[5] && cs.result().stringHard[5] > 0.0f,
           "full calibration: each string has its own soft and hard level");
    check (cs.result().velocityFromPeak (0.08f, 0) + 8 < cs.result().velocityFromPeak (0.08f, 5),
           "the same pick strength is softer on a string that is louder by nature");

    auto copy = Calibration {};
    copy.deserialise (cs.result().serialise());
    check (copy.positions[5][7].measured && copy.chords.size() == cs.result().chords.size() && copy.lastFret[3] == 22
           && copy.stringSoft[0] == cs.result().stringSoft[0], "full calibration survives save/load");

    // Poly's attack fingerprints: measured at every fret, and kept through save/load.
    const auto& spot = cs.result().positions[5][7];
    check (spot.hasAttack, "full calibration: low E fret 7 has an attack fingerprint");
    check (copy.positions[5][7].hasAttack && std::abs (copy.positions[5][7].attack[0] - spot.attack[0]) < 1.0e-3f
           && std::abs (copy.positions[5][7].attack[kOvertones - 1] - spot.attack[kOvertones - 1]) < 1.0e-3f,
           "attack fingerprints survive save/load");
}

static void testCalibrationDraft()
{
    // An unfinished Full calibration is saved as a draft and goes on after loading, at the same step.
    Instrument g;
    g.setTuning ({ 62, 57, 53, 48, 43, 38 }); // D standard: the draft must bring its own tuning
    CalibrationSession cs;
    cs.start (g, false, 1, true);
    const double hop = 0.005;
    cs.listen();
    for (int i = 0; i < 700 && cs.step() == CalibrationSession::Step::Silence; ++i)
        cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);
    auto playFret = [&] (CalibrationSession& s)
    {
        AnalysisFrame f { 0, (float) midiToHz (s.expectedNote()), 0.97f, 0.1f, 0.3f };
        f.hasFeatures = true;
        s.listen();
        for (int i = 0; i < 3; ++i) s.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
        for (int i = 0; i < 200 && s.isListening(); ++i) s.feed (f, hop);
        for (int i = 0; i < 3; ++i) s.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
    };
    for (int k = 0; k < 3; ++k) playFret (cs);
    checkEq (cs.expectedFret(), 3, "draft: three frets of the low string done");

    CalibrationSession back;
    check (! back.loadDraft ("not a draft"), "draft: other text is refused");
    check (back.loadDraft (cs.saveDraft()), "draft: loads");
    checkEq (back.stepNumber(), cs.stepNumber(), "draft: same step");
    checkEq (back.totalSteps(), cs.totalSteps(), "draft: same plan");
    checkEq (back.expectedNote(), cs.expectedNote(), "draft: same note to play (its own tuning)");
    check (back.result().positions[5][2].measured && ! back.result().valid, "draft: measured frets kept, not finished");
    check (std::abs (back.result().noiseFloor - cs.result().noiseFloor) < 1.0e-6f, "draft: noise level kept");
    check (! back.canGoBack(), "draft: Back cannot go before the loaded step");
    playFret (back);
    checkEq (back.expectedFret(), 4, "draft: goes on with the next fret");
    check (back.canGoBack(), "draft: Back works for steps after loading");
}

static void testCalibrationHints()
{
    // A note that fades before the bar is full: the wizard says so, keeps what it has, and a second pick finishes it.
    Instrument g;
    CalibrationSession cs;
    cs.start (g, false, 1, false);
    const double hop = 0.005;
    cs.listen();
    for (int i = 0; i < 700 && cs.step() == CalibrationSession::Step::Silence; ++i)
        cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);
    const AnalysisFrame quiet { 0, 0.0f, 0.0f, 0.0004f, 0.0005f };
    const AnalysisFrame note { 0, (float) midiToHz (cs.expectedNote()), 0.97f, 0.1f, 0.3f };
    const int step = cs.stepNumber();

    cs.listen();
    for (int i = 0; i < 3; ++i) cs.feed (quiet, hop);
    for (int i = 0; i < 50; ++i) cs.feed (note, hop);   // 0.25 s: not enough for an open string (0.6 s)
    for (int i = 0; i < 3; ++i) cs.feed (quiet, hop);
    check (cs.status().find ("faded too soon") != std::string::npos, "hint: a note that faded too soon is named");
    check (cs.stepProgress() > 0.1 && cs.stepProgress() < 1.0 && cs.isListening(), "hint: the bar keeps what it has");

    for (int i = 0; i < 150 && cs.stepNumber() == step; ++i) cs.feed (note, hop);
    checkEq (cs.stepNumber(), step + 1, "hint: a second pick finishes the step");
}

static void testTimbreString()
{
    Instrument g;

    // D3 (50) can be played on: D string open (index 3), A string fret 5 (index 4), low E fret 10 (index 5).
    auto audio = pluck (0.5, [] (double) { return midiToHz (50); });
    const auto frames = featureFrames (g, audio);
    check (! frames.empty(), "features are measured while calibrating");
    if (frames.empty()) return;

    Timbre real = frames[frames.size() / 2].timbre;
    Timbre dark; dark.fill (-8.0f);

    Calibration cal;
    cal.valid = true;
    cal.noiseFloor = 0.001f; cal.softPeak = 0.05f; cal.hardPeak = 0.5f;
    cal.typicalLevel = 0.3f;
    auto setSpot = [&] (int s, int f, const Timbre& t) { auto& p = cal.positions[(size_t) s][(size_t) f]; p.measured = true; p.hasTimbre = true; p.timbre = t; p.level = 0.3f; };
    setSpot (3, 0, dark);
    setSpot (4, 5, dark);
    setSpot (5, 10, real); // the sound matches the low E string, although the hand is near fret 5

    TrackerSettings s;
    s.mode = TrackMode::Mono;
    s.monoInput = 0;
    GuitarTracker t;
    t.prepare (kRate, 512, g, cal, s);
    auto ev = runTracker (t, audio);

    int channel = -1;
    for (auto& e : ev) if ((e.status & 0xF0) == 0x90) { channel = (e.status & 0x0F) + 1; break; }
    checkEq (channel, 7, "tone colour picks the low E string (channel 7) instead of the closest fret");
}

static void testChordTracking()
{
    Instrument g;
    // Open E major, low to high: 0 2 2 1 0 0  ->  index 5..0
    const std::vector<int> frets { 0, 0, 1, 2, 2, 0 };
    std::vector<int> notes;
    for (int s = 0; s < 6; ++s) notes.push_back (g.tuning()[(size_t) s] + frets[(size_t) s]);

    auto audio = chordAudio (notes, 0.8);
    append (audio, silenceOf (0.3));

    // Teach the chord, the same way the calibration does (frames 80..600 ms after the strum).
    const auto frames = featureFrames (g, audio);
    std::vector<Chroma> used;
    for (size_t i = 0; i < frames.size(); ++i)
        if (i >= 4 && i < 30) used.push_back (frames[i].chroma);
    Chroma avg {};
    for (auto& c : used) for (int k = 0; k < 12; ++k) avg[(size_t) k] += c[(size_t) k];
    float len = 0; for (float v : avg) len += v * v; len = std::sqrt (len);
    for (auto& v : avg) v /= len;

    Calibration cal;
    cal.valid = true;
    cal.noiseFloor = 0.001f; cal.softPeak = 0.05f; cal.hardPeak = 0.5f;
    cal.chords.push_back ({ "E", frets, avg });

    TrackerSettings s;
    s.mode = TrackMode::Mono;
    s.monoInput = 0;

    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto ev = runTracker (t, audio);

        // Count notes still sounding after the chord was recognised.
        std::array<int, 16> sounding {};
        sounding.fill (-1);
        int maxTogether = 0;
        for (auto& e : ev)
        {
            const int ch = e.status & 0x0F;
            if ((e.status & 0xF0) == 0x90) sounding[(size_t) ch] = e.d1;
            if ((e.status & 0xF0) == 0x80 && sounding[(size_t) ch] == e.d1) sounding[(size_t) ch] = -1;
            int together = 0;
            for (int n : sounding) if (n >= 0) ++together;
            maxTogether = std::max (maxTogether, together);
        }
        checkEq (maxTogether, 6, "taught E chord: all 6 strings sound together");

        bool lowE = false;
        for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && (e.status & 0x0F) == 6 && e.d1 == 40) lowE = true;
        check (lowE, "chord: low E string note on its own channel (7)");
    }

    // A single A2 note must stay a single note, even with the E chord taught.
    {
        GuitarTracker t;
        t.prepare (kRate, 512, g, cal, s);
        auto single = pluck (0.5, [] (double) { return 110.0; });
        auto ev = runTracker (t, single);
        checkEq (count (ev, 0x90), 1, "single note is not mistaken for the taught chord");
    }
}

// ---------------------------------------------------------------------------
// Poly mode: several notes at once from one cable
// ---------------------------------------------------------------------------
namespace
{
    // Karplus-Strong plucked string: noise burst in a feedback delay line. More like a real string than a
    // sawtooth: random harmonic strengths, and the high harmonics die faster. semisAt(t) bends the note.
    template <typename BendFn>
    std::vector<float> ksString (int midi, double seconds, double amp, unsigned seed, BendFn semisAt)
    {
        std::vector<float> out ((size_t) (seconds * kRate), 0.0f);
        unsigned state = seed * 2654435761u + 12345u;
        auto noise = [&state] { state = state * 1664525u + 1013904223u; return (double) (state >> 8) / (1 << 24) * 2.0 - 1.0; };

        // The pick pulls the string into a triangle; where it is picked changes the harmonics (different per seed).
        const double period0 = kRate / midiToHz (midi);
        const int burst = (int) period0;
        const double pickAt = 0.1 + 0.2 * std::fmod (seed * 0.618034, 1.0);
        for (int i = 0; i < burst && i < (int) out.size(); ++i)
        {
            const double u = (double) i / burst;
            const double shape = u < pickAt ? u / pickAt : (1.0 - u) / (1.0 - pickAt);
            out[(size_t) i] = (float) ((shape - 0.5 + 0.1 * noise()) * amp * 2.0);
        }
        auto at = [&out] (double pos)
        {
            if (pos < 0.0) return 0.0;
            const auto k = (size_t) pos;
            const double t = pos - (double) k;
            return out[k] * (1.0 - t) + out[k + 1] * t;
        };
        for (size_t n = (size_t) burst; n < out.size(); ++n)
        {
            const double period = kRate / midiToHz (midi + semisAt (n / kRate));
            const double d = (double) n - (period - 0.5);
            out[n] = (float) (0.996 * 0.5 * (at (d) + at (d - 1.0)));
        }
        return out;
    }

    std::vector<float> ksString (int midi, double seconds, double amp = 0.3, unsigned seed = 1)
    {
        return ksString (midi, seconds, amp, seed, [] (double) { return 0.0; });
    }

    void mixAt (std::vector<float>& into, const std::vector<float>& add, double atSeconds)
    {
        const auto start = (size_t) (atSeconds * kRate);
        if (into.size() < start + add.size()) into.resize (start + add.size(), 0.0f);
        for (size_t i = 0; i < add.size(); ++i) into[start + i] += add[i];
    }

    // Strum low string to high string, 12 ms apart.
    std::vector<float> strum (const std::vector<int>& lowToHigh, double seconds, double gap = 0.012)
    {
        std::vector<float> out;
        for (size_t i = 0; i < lowToHigh.size(); ++i)
            mixAt (out, ksString (lowToHigh[i], seconds, 0.22, (unsigned) (i + 3)), i * gap);
        return out;
    }

    Calibration polyTestCalibration()
    {
        Calibration cal;
        cal.valid = true;
        cal.noiseFloor = 0.001f; cal.softPeak = 0.05f; cal.hardPeak = 0.5f;
        return cal;
    }

    std::vector<Captured> runPoly (const std::vector<float>& audio, const Instrument& g = Instrument(), double hysteresis = 0.0)
    {
        TrackerSettings s;
        s.mode = TrackMode::Poly;
        s.monoInput = 0;
        s.hysteresis = hysteresis;
        GuitarTracker t;
        t.prepare (kRate, 512, g, polyTestCalibration(), s);
        return runTracker (t, audio);
    }

    std::vector<int> pitchesStarted (const std::vector<Captured>& ev, int fromSample = 0, int toSample = 1 << 30)
    {
        std::vector<int> p;
        for (auto& e : ev)
            if ((e.status & 0xF0) == 0x90 && e.d2 > 0 && e.offset >= fromSample && e.offset < toSample) p.push_back (e.d1);
        std::sort (p.begin(), p.end());
        p.erase (std::unique (p.begin(), p.end()), p.end()); // a note that moved to another string counts once
        return p;
    }

    std::string listOf (const std::vector<int>& v)
    {
        std::string s;
        for (int n : v) s += std::to_string (n) + " ";
        return s;
    }

    // Channel of the last note-on of this pitch (notes of a strum may move to their final string).
    int channelOfNote (const std::vector<Captured>& ev, int note)
    {
        int ch = -1;
        for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && e.d2 > 0 && e.d1 == note) ch = (e.status & 0x0F) + 1;
        return ch;
    }
}

static void testPolyTracking()
{
    // Single note: one note, then a note-off in the silence.
    {
        auto audio = ksString (45, 1.0);
        append (audio, silenceOf (0.4));
        const auto ev = runPoly (audio);
        checkEq (listOf (pitchesStarted (ev)), listOf ({ 45 }), "poly: single A2 gives one note A2");
        checkEq (count (ev, 0x80), 1, "poly: single A2 ends in the silence");
    }

    // Open chords: every note, nothing else, each on its own string.
    struct ChordCase { const char* name; std::vector<int> lowToHigh; std::vector<int> channels; };
    const std::vector<ChordCase> chords {
        { "C major (x32010)",  { 48, 52, 55, 60, 64 },     { 6, 5, 4, 3, 2 } },
        { "E minor (022000)",  { 40, 47, 52, 55, 59, 64 }, { 7, 6, 5, 4, 3, 2 } },
        { "G major (320003)",  { 43, 47, 50, 55, 59, 67 }, { 7, 6, 5, 4, 3, 2 } },
        { "A minor (x02210)",  { 45, 52, 57, 60, 64 },     { 6, 5, 4, 3, 2 } },
        { "F barre (133211)",  { 41, 48, 53, 57, 60, 65 }, { 7, 6, 5, 4, 3, 2 } },
        { "Power chord A5",    { 45, 52 },                 {} }, // open (x02) or 5th fret (57): both fine
    };
    for (const auto& c : chords)
    {
        auto audio = strum (c.lowToHigh, 1.5);
        append (audio, silenceOf (0.3));
        const auto ev = runPoly (audio);
        auto want = c.lowToHigh;
        std::sort (want.begin(), want.end());
        const auto early = pitchesStarted (ev, 0, (int) (0.25 * kRate));
        const auto all = pitchesStarted (ev);
        check (listOf (early) == listOf (want), std::string ("poly: ") + c.name + " found within 250 ms (got " + listOf (early) + ")");
        check (listOf (all) == listOf (want), std::string ("poly: ") + c.name + " has no extra notes (got " + listOf (all) + ")");
        for (size_t i = 0; i < c.channels.size(); ++i)
            checkEq (channelOfNote (ev, c.lowToHigh[i]), c.channels[i],
                     std::string ("poly: ") + c.name + " note " + std::to_string (c.lowToHigh[i]) + " on its string's channel");
    }

    // Barre chords whose octave strings are quiet: hidden in the harmonics of the lower notes, so chord completion
    // fills them in. It must not wait: every note within 130 ms of the first string, each on its own string.
    {
        struct Hidden { const char* name; std::vector<int> lowToHigh; std::vector<int> channels; };
        const std::vector<Hidden> hidden {
            { "F barre (133211)",  { 41, 48, 53, 57, 60, 65 }, { 7, 6, 5, 4, 3, 2 } },
            { "Bb barre (x13331)", { 46, 53, 58, 62, 65 },     { 6, 5, 4, 3, 2 } },
        };
        for (const auto& c : hidden)
        {
            std::vector<float> audio;
            for (size_t i = 0; i < c.lowToHigh.size(); ++i)
            {
                bool octave = false;
                for (size_t k = 0; k < i; ++k) if ((c.lowToHigh[i] - c.lowToHigh[k]) % 12 == 0) octave = true;
                mixAt (audio, ksString (c.lowToHigh[i], 1.0, octave ? 0.03 : 0.22, (unsigned) (i + 3)), i * 0.02);
            }
            const auto ev = runPoly (audio);
            const auto early = pitchesStarted (ev, 0, (int) (0.13 * kRate));
            check (listOf (early) == listOf (c.lowToHigh), std::string ("poly: ") + c.name + " with quiet octaves complete within 130 ms (got "
                                                          + listOf (early) + ")");
            check (listOf (pitchesStarted (ev)) == listOf (c.lowToHigh), std::string ("poly: ") + c.name + " with quiet octaves has no extra notes");
            for (size_t i = 0; i < c.channels.size(); ++i)
                checkEq (channelOfNote (ev, c.lowToHigh[i]), c.channels[i],
                         std::string ("poly: ") + c.name + " with quiet octaves, note " + std::to_string (c.lowToHigh[i]) + " on its string's channel");
        }
    }

    // Arpeggio: one note every 150 ms, all ringing. Each note starts soon after its pluck.
    {
        const std::vector<int> arp { 45, 52, 57, 60, 64 };
        std::vector<float> audio;
        for (size_t i = 0; i < arp.size(); ++i)
            mixAt (audio, ksString (arp[i], 1.4 - i * 0.15, 0.25, (unsigned) (i + 11)), i * 0.15);
        const auto ev = runPoly (audio);
        std::vector<int> order;
        double worst = 0.0;
        for (size_t i = 0; i < arp.size(); ++i)
            for (auto& e : ev)
                if ((e.status & 0xF0) == 0x90 && e.d2 > 0 && e.d1 == arp[i])
                {
                    order.push_back (e.d1);
                    worst = std::max (worst, e.offset / kRate - i * 0.15);
                    break;
                }
        checkEq (listOf (order), listOf (arp), "poly: arpeggio, every note found");
        check (listOf (pitchesStarted (ev)) == listOf ({ 45, 52, 57, 60, 64 }), "poly: arpeggio has no extra notes (got "
                                                                                  + listOf (pitchesStarted (ev)) + ")");
        check (worst < 0.1, "poly: arpeggio notes start within 100 ms (worst " + std::to_string ((int) (worst * 1000)) + " ms)");
    }

    // Latency of single notes across the neck.
    {
        double worst = 0.0;
        std::string all;
        for (int midi : { 40, 45, 50, 55, 59, 64, 69, 76, 84 })
        {
            const auto ev = runPoly (ksString (midi, 0.6, 0.3, (unsigned) midi));
            double t = -1.0;
            for (auto& e : ev) if ((e.status & 0xF0) == 0x90 && e.d2 > 0) { t = e.offset / kRate; break; }
            check (t >= 0.0 && pitchesStarted (ev) == std::vector<int> { midi },
                   "poly: single note " + std::to_string (midi) + " found alone (got " + listOf (pitchesStarted (ev)) + ")");
            worst = std::max (worst, t);
            all += std::to_string (midi) + ":" + std::to_string ((int) (t * 1000)) + "ms ";
        }
        std::printf ("  poly latency: %s\n", all.c_str());
        check (worst < 0.08, "poly: single notes start within 80 ms (worst " + std::to_string ((int) (worst * 1000)) + " ms)");
    }

    // Hysteresis: new notes need more energy while louder notes ring. A single note, a ringing arpeggio and an
    // open chord must still come out complete with it turned up.
    for (double h : { 0.5, 1.0 })
    {
        const std::string tag = "poly, hysteresis " + std::to_string (h).substr (0, 3) + ": ";
        auto single = ksString (45, 1.0);
        append (single, silenceOf (0.4));
        checkEq (listOf (pitchesStarted (runPoly (single, Instrument(), h))), listOf ({ 45 }), tag + "single A2 gives one note A2");

        const std::vector<int> arp { 45, 52, 57, 60, 64 };
        std::vector<float> audio;
        for (size_t i = 0; i < arp.size(); ++i)
            mixAt (audio, ksString (arp[i], 1.4 - i * 0.15, 0.25, (unsigned) (i + 11)), i * 0.15);
        const auto got = listOf (pitchesStarted (runPoly (audio, Instrument(), h)));
        check (got == listOf (arp), tag + "arpeggio, every note and nothing else (got " + got + ")");

        auto chord = strum ({ 48, 52, 55, 60, 64 }, 1.5);
        append (chord, silenceOf (0.3));
        checkEq (listOf (pitchesStarted (runPoly (chord, Instrument(), h))), listOf ({ 48, 52, 55, 60, 64 }), tag + "C major complete");
    }

    // Same note picked twice = two notes.
    {
        auto audio = ksString (50, 0.5, 0.3, 4);
        mixAt (audio, ksString (50, 0.6, 0.3, 5), 0.5);
        const int ons = count (runPoly (audio), 0x90);
        check (ons == 2, "poly: picking the same note again starts it again (got " + std::to_string (ons) + " note-ons)");
    }

    // Double stop with a bend: D3 holds, A3 bends up a whole step. The bend is pitch bend, not new notes.
    {
        auto audio = ksString (50, 1.2, 0.25, 6);
        mixAt (audio, ksString (57, 1.2, 0.25, 7, [] (double t) { return t < 0.3 ? 0.0 : std::min (1.0, (t - 0.3) / 0.3) * 2.0; }), 0.0);
        const auto ev = runPoly (audio);
        check (listOf (pitchesStarted (ev)) == listOf ({ 50, 57 }), "poly: bent double stop starts only its two notes (got " + listOf (pitchesStarted (ev)) + ")");
        const int ch = channelOfNote (ev, 57);
        int maxBend = 8192;
        for (auto& e : ev)
            if ((e.status & 0xF0) == 0xE0 && (e.status & 0x0F) + 1 == ch) maxBend = std::max (maxBend, e.d1 | (e.d2 << 7));
        const double semis = (maxBend - 8192) / 8192.0 * 12.0;
        check (semis > 1.6 && semis < 2.4, "poly: whole-step bend sent as pitch bend (got " + std::to_string (semis) + " semitones)");
    }

    // Quiet noise: no notes.
    {
        std::vector<float> audio ((size_t) kRate, 0.0f);
        unsigned st = 99;
        for (auto& v : audio) { st = st * 1664525u + 1013904223u; v = ((float) (st >> 8) / (1 << 24) - 0.5f) * 0.002f; }
        checkEq ((int) pitchesStarted (runPoly (audio)).size(), 0, "poly: hiss gives no notes");
    }
}

// ---------------------------------------------------------------------------
// Recording tracked notes into the phrase
// ---------------------------------------------------------------------------
namespace
{
    struct Rec
    {
        PhraseRecorder r;
        Instrument g;
        explicit Rec (PhraseRecorder::Settings s = {}) { r.start (g, s); }

        // string index -> channel (first channel 2)
        void on (double b, int string, int note, int vel = 100) { r.add (b, (uint8_t) (0x90 | (string + 1)), (uint8_t) note, (uint8_t) vel); }
        void off (double b, int string, int note)               { r.add (b, (uint8_t) (0x80 | (string + 1)), (uint8_t) note, 0); }
        void bend (double b, int string, double semis)
        {
            const int v = pitchBendValue (semis, 12);
            r.add (b, (uint8_t) (0xE0 | (string + 1)), (uint8_t) (v & 0x7F), (uint8_t) (v >> 7));
        }
    };
}

static void testRecorder()
{
    // Single note goes to the cursor (startAt), on the right string and fret.
    {
        PhraseRecorder::Settings s; s.startAt = 2.0;
        Rec t (s);
        t.on (10.0, 5, 45);
        t.off (10.5, 5, 45);
        auto notes = t.r.finish (11.0);
        check (notes.size() == 1 && notes[0].string == 5 && notes[0].fret == 5, "recorded A2 = low E string, fret 5");
        if (notes.size() == 1)
        {
            check (std::abs (notes[0].start - 2.0) < 1e-9, "first note placed at the cursor");
            check (std::abs (notes[0].length - 0.5) < 1e-9, "length kept");
        }
    }

    // Hammer-on and pull-off (new note-on before the old note-off, same string).
    {
        Rec t;
        t.on (0.0, 2, 62);
        t.bend (1.0, 2, 0.05); t.on (1.0, 2, 64); t.off (1.0, 2, 62);
        t.bend (2.0, 2, -0.03); t.on (2.0, 2, 62); t.off (2.0, 2, 64);
        t.off (2.5, 2, 62);
        auto n = t.r.finish (3.0);
        check (n.size() == 3, "hammer/pull: three notes");
        if (n.size() == 3)
        {
            checkEq (n[1].technique, Technique::HammerOn, "higher legato note = hammer-on");
            checkEq (n[2].technique, Technique::PullOff, "lower legato note = pull-off");
            check (n[0].bend.empty() && n[0].slide == SlideType::None, "the new note's bend did not leak into the old note");
        }
    }

    // A bend up a whole step, held.
    {
        Rec t;
        t.on (0.0, 1, 66);
        for (int i = 0; i <= 40; ++i) t.bend (0.02 * i, 1, std::min (2.0, 2.0 * i / 12.0));
        t.off (1.0, 1, 66);
        auto n = t.r.finish (1.0);
        double peak = 0;
        if (! n.empty()) for (auto& b : n[0].bend) peak = std::max (peak, b.semitones);
        check (! n.empty() && ! n[0].bend.empty() && std::abs (peak - 2.0) < 0.15, "whole-step bend recorded (peak ~2)");
        check (! n.empty() && n[0].bend.size() <= 6, "bend curve simplified to a few points");
    }

    // Vibrato: 5.5 Hz, 0.4 semitones, at 120 bpm (1 beat = 0.5 s).
    {
        PhraseRecorder::Settings s; s.bpm = 120.0;
        Rec t (s);
        t.on (0.0, 2, 64);
        for (int i = 0; i <= 200; ++i)
        {
            const double beat = 0.01 * i, sec = beat * 0.5;
            t.bend (beat, 2, 0.4 * std::sin (2.0 * 3.14159265 * 5.5 * sec));
        }
        t.off (2.0, 2, 64);
        auto n = t.r.finish (2.0);
        check (! n.empty() && n[0].vibrato, "vibrato recognised");
        if (! n.empty())
        {
            check (std::abs (n[0].vibratoRateHz - 5.5) < 1.0, "vibrato speed about 5.5 Hz");
            check (n[0].vibratoDepth > 0.2 && n[0].vibratoDepth < 0.6, "vibrato depth about 0.4");
            check (n[0].bend.empty(), "vibrato is not saved as a bend");
        }
    }

    // Slide into the next note: pitch glides +2, then the next note starts there.
    {
        Rec t;
        t.on (0.0, 4, 57);
        for (int i = 0; i <= 10; ++i) t.bend (0.5 + 0.05 * i, 4, 0.2 * i);
        t.bend (1.0, 4, 0.02); t.on (1.0, 4, 59); t.off (1.0, 4, 57);
        t.off (2.0, 4, 59);
        auto n = t.r.finish (2.0);
        check (n.size() == 2 && n[0].slide == SlideType::SlideTo && n[0].slideToFret == n[0].fret + 2, "slide into the next note");
        check (n.size() == 2 && n[1].technique == Technique::Normal, "slide target is not a hammer-on");
    }

    // Chord: the single note sent first is dropped, the chord is one group.
    {
        Rec t;
        t.on (0.0, 5, 45);
        t.off (0.12, 5, 45);
        t.on (0.12, 5, 45); t.on (0.12, 4, 52); t.on (0.12, 3, 57);
        t.off (1.0, 5, 45); t.off (1.0, 4, 52); t.off (1.0, 3, 57);
        auto n = t.r.finish (1.0);
        checkEq ((int) n.size(), 3, "chord: 3 notes, the swapped single note is gone");
        bool sameGroup = n.size() == 3 && n[0].group >= 0 && n[0].group == n[1].group && n[1].group == n[2].group;
        check (sameGroup, "chord notes share one group");
    }

    // Snap to 1/8 and keep the place in the bar when the DAW was playing.
    {
        PhraseRecorder::Settings s; s.snapBeats = 0.5; s.startAt = 4.3; s.alignToBars = true;
        Rec t (s);
        t.on (9.47, 2, 64); t.off (9.9, 2, 64);   // bar 3 (beats 8..12), 1.47 beats in
        auto n = t.r.finish (10.0);
        check (n.size() == 1 && std::abs (n[0].start - 5.5) < 1e-9, "aligned to the bar and snapped: 4 + 1.5 = 5.5");
    }

    // End to end: guitar audio -> tracker -> recorder. A2 then a hammer-on to B2.
    {
        Instrument g;
        Calibration cal;
        cal.valid = true; cal.noiseFloor = 0.001f; cal.softPeak = 0.05f; cal.hardPeak = 0.5f;
        TrackerSettings ts; ts.mode = TrackMode::Mono; ts.monoInput = 0;
        GuitarTracker tracker;
        tracker.prepare (kRate, 512, g, cal, ts);

        PhraseRecorder rec;
        rec.start (g, {});
        const double samplesPerBeat = kRate * 0.5; // 120 bpm

        auto audio = pluck (0.7, [] (double tm) { return tm < 0.3 ? 110.0 : 123.47; }, 0.5, 0.8);
        append (audio, silenceOf (0.3));
        int base = 0;
        for (size_t pos = 0; pos < audio.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, audio.size() - pos);
            const float* chans[1] = { audio.data() + pos };
            tracker.process (chans, 1, n,
                [&] (int off, uint8_t s, uint8_t a, uint8_t b) { rec.add ((base + off) / samplesPerBeat, s, a, b); },
                [] (const AnalysisFrame&) {});
            base += n;
        }
        auto notes = rec.finish (base / samplesPerBeat);
        check (notes.size() == 2, "end to end: two notes recorded");
        if (notes.size() == 2)
        {
            check (notes[0].string == 5 && notes[0].fret == 5, "end to end: A2 on the low E string, fret 5");
            check (notes[1].fret == 7 && notes[1].technique == Technique::HammerOn, "end to end: B2 as a hammer-on at fret 7");
        }
    }
}

int main()
{
    testChordNames();
    testChordSymbols();
    testTabExport();
    testEarTraining();
    testChordTimeline();
    testGuitarPro3();
    testPolyTracking();
    testRecorder();
    testSpectrum();
    testFullCalibration();
    testCalibrationDraft();
    testCalibrationHints();
    testTimbreString();
    testChordTracking();
    testPitchDetector();
    testTracker();
    testCalibration();
    testNotes();
    testInstrument();
    testChords();
    testReharm();
    testTimedProgression();
    testVoicings();
    testTrainer();
    testMidi();

    if (failures == 0)
        std::printf ("All tests passed.\n");
    return failures == 0 ? 0 : 1;
}
