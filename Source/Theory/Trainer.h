#pragma once

#include "Theory/Chord.h"
#include "Theory/Instrument.h"

#include <random>
#include <string>
#include <vector>

namespace gl
{
    // ---------------------------------------------------------------------
    // Scales and arpeggios (shared by the trainer and the fretboard display)
    // ---------------------------------------------------------------------
    struct ScaleType
    {
        std::string name;
        std::vector<int> intervals;
    };

    const std::vector<ScaleType>& scaleTypes();

    // ---------------------------------------------------------------------
    // Quiz
    // ---------------------------------------------------------------------
    enum class QuizMode
    {
        NameTheNote,   // a dot is shown, you type/click its name
        FindTheNote,   // a name is shown, you click every place it lives in the fret range
        ArpeggioTones, // a chord is shown, you click its chord tones in the fret range
        IntervalName   // two dots are shown, you name the interval
    };

    struct QuizFocus
    {
        std::vector<bool> strings; // which strings can be asked (index 0 = highest)
        int  minFret = 0;
        int  maxFret = kNumFrets;
        bool naturalsOnly = false; // only C D E F G A B
    };

    struct Question
    {
        QuizMode mode = QuizMode::NameTheNote;
        std::vector<FretPos> shown;  // dots to show
        PitchClass targetPc = 0;     // the answer for name/find questions
        Chord chord;                 // for arpeggio questions
        int interval = 0;            // for interval questions (0..11)
        std::string prompt;
    };

    // Per-spot score. More misses = asked more often.
    struct SpotStats
    {
        int asked = 0;
        int correct = 0;
        double avgSeconds = 0.0;
    };

    class Trainer
    {
    public:
        explicit Trainer (unsigned seed = 12345);

        void setInstrument (const Instrument& inst);
        void setFocus (const QuizFocus& focus) { focus_ = focus; }
        const QuizFocus& focus() const        { return focus_; }

        void setAdaptive (bool on) { adaptive = on; }

        Question next (QuizMode mode);

        // Name / interval answers. Returns true when correct, and records it.
        bool answerPitchClass (const Question& q, PitchClass answer, double seconds);
        bool answerInterval (const Question& q, int semitones, double seconds);

        // Find / arpeggio answers: the spots the player clicked. All correct spots must be found, no wrong ones.
        bool answerPositions (const Question& q, const std::vector<FretPos>& clicked, double seconds,
                              std::vector<FretPos>* missedOut = nullptr, std::vector<FretPos>* wrongOut = nullptr);

        // Every spot that is a correct answer for a find/arpeggio question.
        std::vector<FretPos> correctPositions (const Question& q) const;

        // 0..1 accuracy per spot, -1 when never asked. Used for the "weak spots" heat map.
        double accuracyAt (FretPos p) const;
        const SpotStats& statsAt (FretPos p) const;

        int totalAsked() const   { return totalAsked_; }
        int totalCorrect() const { return totalCorrect_; }
        int streak() const       { return streak_; }

        void resetStats();

        // Save/load stats as plain text (stored inside the plugin state).
        std::string serialise() const;
        void deserialise (const std::string& text);

    private:
        std::vector<FretPos> allowedSpots() const;
        FretPos pickSpot();
        double weightFor (FretPos p) const;
        void record (FretPos p, bool ok, double seconds);
        SpotStats& stats (FretPos p);

        Instrument inst;
        QuizFocus  focus_;
        bool adaptive = true;
        std::mt19937 rng;

        std::vector<SpotStats> table; // kMaxStrings * (kNumFrets + 1)
        int totalAsked_ = 0, totalCorrect_ = 0, streak_ = 0;
    };

    std::string intervalName (int semitones); // "minor 3rd", "perfect 5th"...
}
