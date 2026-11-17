#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Spectrum.h"

#include <algorithm>
#include <array>
#include <string>
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
    };

    // A chord the player taught the tracker (mono mode): the shape and its sound fingerprint.
    struct ChordTemplate
    {
        std::string name;
        std::vector<int> frets;  // per string (index 0 = highest), -1 = not played
        Chroma chroma {};
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
        int velocityFromPeak (float peak) const;

        // Total cents correction for a string (whole-guitar offset + this string's own offset).
        double centsFor (int string) const;

        std::string serialise() const;
        void deserialise (const std::string& text);
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
    };

    // Walks the player through calibration. Feed it analysis frames; it moves through the steps.
    class CalibrationSession
    {
    public:
        enum class Step { Silence, OpenString, TwelfthFret, EveryNote, Chord, Soft, Hard, Done };

        // The instrument (tuning) must be chosen BEFORE starting.
        // full = also play every note on every string, and (mono mode) a set of chords.
        void start (const Instrument& inst, bool hexMode, int numInputChannels, bool full = false);

        void feed (const AnalysisFrame& f, double hopSeconds);

        // Buttons in the wizard.
        void accept();   // keep the last measurement even though it looked wrong (e.g. string out of tune)
        void retry();    // measure this step again
        void skip();     // skip this step (keeps default values). In "every note": skips one fret.
        void noMoreFrets(); // "every note": this guitar has no more frets (applies to all strings)
        bool canEndFrets() const { return current == Step::EveryNote; }

        Step step() const          { return current; }
        bool finished() const      { return current == Step::Done; }
        bool waitingForUser() const{ return needDecision; }
        int  stepNumber() const    { return stepIndex + 1; }
        int  totalSteps() const    { return (int) plan.size(); }
        double stepProgress() const{ return progress; }

        std::string instruction() const;  // what the player should do now
        std::string status() const        { return statusText; } // what was heard / problems

        const Calibration& result() const { return cal; }

        // Expected note for the current step (for the tuner display). -1 when there is none.
        int expectedNote() const;
        int currentString() const;
        int expectedFret() const;                        // -1 when the step has no single fret
        const ChordTemplate* currentChord() const;       // chord step: the shape to play

        // Common chord shapes for this tuning (used by the chord step). Public for tests.
        static std::vector<ChordTemplate> chordShapesFor (const Instrument& inst);

    private:
        struct PlanItem { Step step; int string; };

        void beginStep();
        void finishStep();
        void evaluatePitch();
        void evaluateEveryNote();
        void feedChord (const AnalysisFrame& f, double hop);
        void finalise();
        std::string stringName (int s) const;

        Instrument inst;
        Calibration cal;
        std::vector<PlanItem> plan;
        int stepIndex = 0;
        Step current = Step::Silence;
        int inputs = 1;

        // Per-step data
        double elapsed = 0.0;
        double progress = 0.0;
        bool   needDecision = false;
        bool   noteActive = false;
        double noteTime = 0.0;
        std::vector<double> pitches;
        std::vector<float> peaks;
        std::vector<float> silence[kMaxStrings];
        std::array<float, kMaxStrings> channelEnergy {};
        double pendingCents = 0.0;
        std::string statusText;

        // Full calibration
        bool full = false;
        int  fret = 0;                    // "every note": fret being measured
        int  previousNote = -1;           // ignore the last note while it still rings
        std::vector<Timbre> timbres;
        std::array<double, kMaxStrings> bleedSum {};
        std::array<int, kMaxStrings> bleedCount {};
        std::vector<float> spotLevels;    // all measured pluck levels, for typicalLevel
        std::vector<ChordTemplate> chordList;
        int chordIndex = 0;
        int strums = 0;
        size_t chromasAtStrumStart = 0;
        std::vector<Chroma> chromas;
        bool forceAccept = false;
    };
}
