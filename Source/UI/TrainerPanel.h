#pragma once

#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"
#include "UI/UiMemory.h"

#include <set>

namespace gl
{
    class TrainerPanel : public juce::Component
    {
    public:
        explicit TrainerPanel (GuitarLabProcessor& p);

        void refresh();     // after the instrument changed or a project was loaded
        void restoreUi();   // after a project was loaded: the controls come back

        // Fretboard and piano input while this tab is shown.
        void fretPressed (FretPos pos);
        void fretReleased();
        void pianoPressed (MidiNote note);

        // Notes sounding now (the editor calls this ~30 times a second): "Ear: play it back" listens to your guitar.
        void heard (const std::set<MidiNote>& sounding);

        std::vector<FretboardComponent::Mark> marks() const;
        bool showHeatMap() const { return heatToggle.getToggleState(); }
        std::pair<int, int> fretRange() const { return { (int) minFret.getValue(), (int) maxFret.getValue() }; }

        std::function<void()> onDisplayChanged;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        QuizMode mode() const { return (QuizMode) (modeBox.getSelectedId() - 1); }
        void applyFocus();
        void nextQuestion();
        void answerName (PitchClass pc);
        void answerInterval (int semitones);
        void answerChord (int index);
        void playQuestion();
        void checkPositions();
        void showResult (bool ok, const juce::String& detail);
        void updateScore();
        void updateAnswerButtons();
        double secondsSinceAsked() const;

        GuitarLabProcessor& proc;

        juce::Label modeLabel, stringsLabel, fretsLabel, promptLabel, feedbackLabel, scoreLabel;
        juce::ComboBox modeBox;
        juce::OwnedArray<juce::ToggleButton> stringToggles;
        juce::Slider minFret, maxFret;
        juce::ToggleButton naturalsToggle { "Natural notes only" }, adaptiveToggle { "Drill my weak spots" },
                           heatToggle { "Show heat map" };
        juce::TextButton nextButton { "Start" }, checkButton { "Check" }, resetButton { "Reset stats" }, replayButton { "Play again" };
        juce::OwnedArray<juce::TextButton> answerButtons; // 12 notes or 12 intervals

        // The open question lives in the processor, so it is still there when the window opens again.
        std::optional<Question>& question = proc.work.question;
        std::vector<FretPos>& clicked = proc.work.clicked;
        std::vector<FretPos>& missed = proc.work.missed;
        std::vector<FretPos>& wrong = proc.work.wrong;
        bool& answered = proc.work.answered;
        double askedAt = 0.0;
        int liveString = -1;
        int playToken = 0;
        double ignoreUntil = 0.0;          // the question's own notes are not an answer
        std::set<MidiNote> lastHeard;
        UiMemory memory { proc.uiState, "Trainer" }; // last member: see UiMemory
    };
}
