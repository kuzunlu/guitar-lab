// Plain tests for the music logic. No test framework: run the exe, it prints failures and returns 1.

#include "Midi/MidiRenderer.h"
#include "Theory/Reharm.h"
#include "Theory/Trainer.h"
#include "Theory/Voicing.h"
#include "Tracking/GuitarTracker.h"

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

    // Silence
    for (int i = 0; i < 700 && cs.step() == CalibrationSession::Step::Silence; ++i)
        cs.feed ({ 0, 0.0f, 0.0f, 0.0005f, 0.001f }, hop);
    check (cs.step() == CalibrationSession::Step::OpenString, "silence step finishes");

    // Open strings: play every string 10 cents flat.
    int guard = 0;
    while (cs.step() == CalibrationSession::Step::OpenString && guard++ < 20)
    {
        const double hz = midiToHz (cs.expectedNote() - 0.10);
        for (int i = 0; i < 300 && ! cs.waitingForUser(); ++i)
        {
            const auto before = cs.stepNumber();
            cs.feed ({ 0, (float) hz, 0.97f, 0.1f, 0.3f }, hop);
            if (cs.stepNumber() != before) break;
        }
    }
    check (cs.step() == CalibrationSession::Step::TwelfthFret, "all open strings measured");
    check (std::abs (cs.result().strings[5].centsOffset + 10.0) < 1.0, "low E measured as 10 cents flat");

    // 12th fret, low string: play it 80 cents sharp -> needs a decision.
    {
        const double hz = midiToHz (cs.expectedNote() + 0.8);
        for (int i = 0; i < 300 && ! cs.waitingForUser(); ++i)
            cs.feed ({ 0, (float) hz, 0.97f, 0.1f, 0.3f }, hop);
        check (cs.waitingForUser(), "badly out-of-tune note asks the player");
        cs.skip();
    }

    // 12th fret on the high E, in tune -> accepted, next step.
    {
        const int before = cs.stepNumber();
        for (int i = 0; i < 300; ++i)
            cs.feed ({ 0, (float) midiToHz (64 + 12), 0.97f, 0.1f, 0.3f }, hop); // correct: high E 12th fret
        check (cs.stepNumber() == before + 1, "12th fret on the high string accepted");
    }

    // Soft and hard: 4 plucks each.
    auto hits = [&] (float peak)
    {
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

        for (int i = 0; i < 200; ++i)
        {
            cs.feed (f, hop);
            if (cs.expectedFret() != fret || cs.currentString() != s || cs.waitingForUser()) break;
            if (s == 5 && fret == 3 && ! wrongFretTested && cs.status().find ("That was fret 4") != std::string::npos)
            {
                wrongFretTested = true;
                break;
            }
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
    check (shapes.size() >= 6, "at least 6 chord shapes found for standard tuning");
    guard = 0;
    while (cs.step() == CalibrationSession::Step::Chord && guard++ < 20)
    {
        const auto* ch = cs.currentChord();
        Chroma ideal {};
        for (int s = 0; s < (int) ch->frets.size(); ++s)
            if (ch->frets[(size_t) s] >= 0) ideal[(size_t) pitchClassOf (g.tuning()[(size_t) s] + ch->frets[(size_t) s])] += 0.5f;

        AnalysisFrame f { 0, 0.0f, 0.3f, 0.1f, 0.3f };
        f.hasFeatures = true;
        f.chroma = ideal;
        for (int strum = 0; strum < 3; ++strum)
        {
            for (int i = 0; i < 40; ++i) cs.feed (f, hop);
            for (int i = 0; i < 5; ++i)  cs.feed ({ 0, 0.0f, 0.0f, 0.0004f, 0.0005f }, hop);
        }
    }
    check (cs.step() == CalibrationSession::Step::Soft, "all chords learned");
    checkEq ((int) cs.result().chords.size(), (int) shapes.size(), "one fingerprint per chord shape");

    auto copy = Calibration {};
    copy.deserialise (cs.result().serialise());
    check (copy.positions[5][7].measured && copy.chords.size() == cs.result().chords.size() && copy.lastFret[3] == 22,
           "full calibration survives save/load");
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

int main()
{
    testSpectrum();
    testFullCalibration();
    testTimbreString();
    testChordTracking();
    testPitchDetector();
    testTracker();
    testCalibration();
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
