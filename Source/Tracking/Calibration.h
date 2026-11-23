#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Spectrum.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace gl
{
    struct StringCalibration
    {
        bool   measured = false;
        double centsOffset = 0.0;      // how far the open string is from the expected note (+ = sharp)
        double intonationCents = 0.0;  // 12th fret error compared to the open string (0 = perfect)
        int    inputChannel = -1;      // hex mode: which audio input carries this string
        float  gain = 1.0f;            // hex mode: makes all strings equally loud for the tracker
    };

    // Full calibration: what one spot on the neck (string + fret) really sounds like.
    struct PositionCalibration
    {
        bool   measured = false;
        float  cents = 0.0f;   // real pitch of this fret vs. the perfect note (+ = sharp)
        float  level = 0.0f;   // typical pluck peak here (dead spots are quieter)
        Timbre timbre {};      // tone colour here (for guessing the string in mono mode)
        bool   hasTimbre = false;
        Overtones attack {};   // harmonics 2..16 at the pick attack (Poly's fingerprint for this string and fret)
        bool   hasAttack = false;
    };

    // A chord the player taught the tracker (mono mode): the shape and its sound fingerprint.
    struct ChordTemplate
    {
        std::string name;
        std::vector<int> frets;  // per string (index 0 = highest), -1 = not played
        Chroma chroma {};
        bool learnOnly = false;  // Poly practice (octaves, fifths, double stops): Poly learns from it, it is no taught chord
    };

    // Learned from a Full calibration's recordings (NoteLearner): is a possible new Poly note real, or an overtone /
    // a guess? A small neural network: one hidden layer (tanh), a score > 0 = real. Empty = Poly's rules decide.
    struct NoteNet
    {
        int inputs = 0, hidden = 0;
        std::vector<float> mean, scale, w1, b1, w2;  // w1: hidden x inputs
        float b2 = 0.0f, offset = 0.0f;              // offset: how strict (set when learning: "balanced")

        bool usable() const;
        float score (const float* features) const;
    };

    struct Calibration
    {
        bool valid = false;
        bool hexMode = false;
        std::vector<int> tuning;       // tuning used when calibrating (index 0 = highest string)

        double globalCents = 0.0;      // average tuning offset of the whole guitar (A=432 is about -32 cents)
        float  noiseFloor = 0.002f;    // loudness (RMS) of silence
        float  softPeak = 0.03f;       // peak of a soft pluck
        float  hardPeak = 0.4f;        // peak of a hard pluck
        std::array<float, kMaxStrings> stringSoft {}, stringHard {}; // full calibration: per string (0 = not measured)
        std::array<float, kMaxStrings> channelNoise {};   // hex: silence level per input
        std::array<StringCalibration, kMaxStrings> strings {};

        // --- Full calibration (empty after a quick calibration) ---
        std::array<std::array<PositionCalibration, kNumFrets + 1>, kMaxStrings> positions {};
        std::array<int, kMaxStrings> lastFret { kNumFrets, kNumFrets, kNumFrets, kNumFrets,
                                                kNumFrets, kNumFrets, kNumFrets, kNumFrets }; // guitars with 21/22 frets
        std::array<std::array<float, kMaxStrings>, kMaxStrings> bleed {}; // hex: [string played][input] level ratio
        std::vector<ChordTemplate> chords;
        float typicalLevel = 0.0f;   // median pluck level over all measured spots

        bool hasPositions() const { return typicalLevel > 0.0f; }
        const PositionCalibration* position (int string, int fret) const
        {
            if (string < 0 || string >= kMaxStrings || fret < 0 || fret > kNumFrets) return nullptr;
            const auto& p = positions[(size_t) string][(size_t) fret];
            return p.measured ? &p : nullptr;
        }

        // Cents correction for one spot: the fret's own value if measured, else the string's.
        double centsAt (int string, int fret) const
        {
            if (auto* p = position (string, fret)) return p->cents;
            return centsFor (string);
        }

        // Loudness above which a note can start / below which it ends.
        float gateOn() const  { return std::max (noiseFloor * 4.0f, softPeak * 0.2f); }
        float gateOff() const { return gateOn() * 0.5f; }

        // Maps a pluck peak to MIDI velocity: soft pluck -> about 30, hard pluck -> about 120.
        // With a string (full calibration), that string's own soft and hard picking are used.
        int velocityFromPeak (float peak, int string = -1) const;

        // Total cents correction for a string (whole-guitar offset + this string's own offset).
        double centsFor (int string) const;

        NoteNet net;    // learned from the Full calibration's recordings (empty = none)

        std::string serialise() const;
        void deserialise (const std::string& text);
    };

    // A preset: the settings (parameter id -> plain value), the strings + tuning, and the calibration (if any).
    // Old preset files are a bare calibration: they load as a preset with no settings and no tuning.
    struct SetupPreset
    {
        std::vector<std::pair<std::string, float>> settings;
        std::vector<int> tuning;           // empty = keep the tuning in use
        Calibration cal;                   // cal.valid == false: no calibration
        bool calibrationOnly = false;      // read from an old preset file (settings and tuning stay as they are)

        std::string serialise() const;
        bool deserialise (const std::string& text); // false when the text is neither a preset nor a calibration
    };

    // One analysis frame from the audio thread (one per hop, per input channel).
    struct AnalysisFrame
    {
        int   channel = 0;
        float hz = 0.0f;
        float clarity = 0.0f;
        float rms = 0.0f;
        float peak = 0.0f;

        // Only filled while calibrating (every few hops), to keep the audio thread light.
        bool   hasFeatures = false;
        Timbre timbre {};
        Chroma chroma {};
        bool   hasOvertones = false;   // calibrating, normal guitar: every 2nd hop, for the attack fingerprints
        Overtones overtones {};
    };

    // Walks the player through calibration, one thing to play at a time (an "item": silence, one string, one fret,
    // one chord, soft or hard picking):
    //   Ready      - waiting. The player presses Listen (listen()) and then plays.
    //   Listening  - collects the sound. Only a NEW pick attack counts (or sound after a quiet moment), so a note that
    //                still rings from the item before is never taken for the answer. Nothing clear for 10 s: Ready again.
    //   OK         - the result is stored and the next item is Ready at once (status() says what was heard).
    //   Decide     - it sounded wrong (out of tune, not the chord): Again (listen()), Use it anyway (accept()) or skip().
    // back() goes to the item before and forgets its result (everything measured is put back as it was before it).
    class CalibrationSession
    {
    public:
        enum class Step { Silence, OpenString, TwelfthFret, EveryNote, Chord, Soft, Hard, Done };
        enum class Phase { Ready, Listening, Decide, Done };

        // The instrument (tuning) must be chosen BEFORE starting.
        // full = also play every note on every string, and (mono mode) a set of chords.
        // polyExtras (Full calibration, normal guitar): ~40 more shapes for Poly to learn from (polyExtraShapesFor).
        void start (const Instrument& inst, bool hexMode, int numInputChannels, bool full = false, bool polyExtras = false);

        // Analysis frames. They only count while Listening.
        void feed (const AnalysisFrame& f, double hopSeconds);

        // Buttons in the wizard.
        void listen();   // Ready or Decide: listen to what is played next (in Decide: "Again")
        void stop();     // Listening: stop listening, back to Ready (nothing is stored)
        void back();     // go to the item before; its result is forgotten
        void accept();   // Decide: keep the last measurement even though it looked wrong (e.g. string tuned like this on purpose)
        void retry() { listen(); }
        void skip();     // skip this item (keeps default values)
        void noMoreFrets(); // "every note": this guitar has no more frets (applies to all strings)
        bool canEndFrets() const { return current == Step::EveryNote; }
        bool canGoBack() const     { return stepIndex > backLimit; }

        // An unfinished calibration as text, saved with the project so it can go on after the DAW or app closed.
        // loadDraft goes on at the same item (Ready); Back cannot go to items from before the save.
        std::string saveDraft() const;
        bool loadDraft (const std::string& text); // false (and nothing changes) when the text is not a draft

        Phase phase() const;
        Step step() const          { return current; }
        bool finished() const      { return current == Step::Done; }
        bool waitingForUser() const{ return needDecision; }
        bool isListening() const   { return listening; }
        int  stepNumber() const    { return stepIndex + 1; }
        int  totalSteps() const    { return (int) plan.size(); }
        double stepProgress() const{ return progress; }

        std::string instruction() const;  // what the player should do now
        std::string status() const        { return statusText; } // what was heard / problems
        bool statusIsGood() const         { return statusGood; }

        const Calibration& result() const { return cal; }

        // Expected note for the current step (for the tuner display). -1 when there is none.
        int expectedNote() const;
        int currentString() const;
        int expectedFret() const;                        // -1 when the step has no single fret
        const ChordTemplate* currentChord() const;       // chord step: the shape to play

        // Common chord shapes for this tuning (used by the chord step). Public for tests.
        static std::vector<ChordTemplate> chordShapesFor (const Instrument& inst);
        // The shapes Poly mixes up most (one note on the other's overtones): octaves, fifths, octave + fifth, thirds and
        // sixths, up the neck. Played like chords, learned from only.
        static std::vector<ChordTemplate> polyExtraShapesFor (const Instrument& inst);

    private:
        struct PlanItem { Step step; int string; int fret = 0; }; // Chord: string = chord index

        // Everything measured so far, taken before each item (back() puts it back).
        struct Snapshot
        {
            Calibration cal;
            std::vector<PlanItem> plan;
            std::vector<float> spotLevels, softLevels, hardLevels;
            std::array<double, kMaxStrings> bleedSum {};
            std::array<int, kMaxStrings> bleedCount {};
        };

        void beginStep();
        void finishStep();
        void clearMeasurement();
        bool newAttack (const AnalysisFrame& f);
        void say (const std::string& text, bool good) { statusText = text; statusGood = good; }
        void evaluatePitch();
        void evaluateEveryNote();
        void feedChord (const AnalysisFrame& f, double hop);
        void finalise();
        std::string stringName (int s) const;

        Instrument inst;
        Calibration cal;
        std::vector<PlanItem> plan;
        std::vector<Snapshot> history;   // history[i] = state before plan item i
        int stepIndex = 0;
        int backLimit = 0;               // back() stops here (the item a draft was loaded at)
        Step current = Step::Silence;
        int inputs = 1;

        // Per-item data
        bool   listening = false;
        double listenTime = 0.0;
        bool   armed = false;                  // a new pick attack was heard since Listen
        std::array<bool, kMaxStrings> quietSeen {};
        std::array<bool, kMaxStrings> heardBefore {};
        std::array<float, kMaxStrings> attackEnvelope {};
        double elapsed = 0.0;
        double progress = 0.0;
        bool   needDecision = false;
        bool   noteActive = false;
        double noteTime = 0.0;
        int    unclearFrames = 0;              // frames in a row without a clear pitch (to say why the bar stopped)
        std::vector<double> pitches;
        std::vector<float> peaks;
        std::vector<float> silence[kMaxStrings];
        std::array<float, kMaxStrings> channelEnergy {};
        double pendingCents = 0.0;
        std::string statusText;
        bool statusGood = true;

        // Full calibration
        bool full = false;
        int  fret = 0;                    // "every note": fret being measured
        int  previousNote = -1;           // ignore the last note while it still rings
        std::vector<Timbre> timbres;
        std::vector<Overtones> attacks;  // "every note": overtones heard 40-70 ms after the pick attack
        std::array<double, kMaxStrings> bleedSum {};
        std::array<int, kMaxStrings> bleedCount {};
        std::vector<float> spotLevels;    // all measured pluck levels, for typicalLevel
        std::vector<ChordTemplate> chordList;
        std::vector<float> softLevels, hardLevels; // full calibration: every string's result, for the overall values
        int chordIndex = 0;
        int strums = 0;
        size_t chromasAtStrumStart = 0;
        std::vector<Chroma> chromas;
        bool forceAccept = false;
    };
}
