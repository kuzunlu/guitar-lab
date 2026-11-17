#pragma once

#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"

namespace gl
{
    class TrainerPanel : public juce::Component
    {
    public:
        explicit TrainerPanel (GuitarLabProcessor& p);

        void refresh();     // after the instrument changed or a project was loaded

        // Fretboard and piano input while this tab is shown.
        void fretPressed (FretPos pos);
        void fretReleased();
        void pianoPressed (MidiNote note);

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
        juce::TextButton nextButton { "Start" }, checkButton { "Check" }, resetButton { "Reset stats" };
        juce::OwnedArray<juce::TextButton> answerButtons; // 12 notes or 12 intervals

        std::optional<Question> question;
        std::vector<FretPos> clicked, missed, wrong;
        bool answered = false;
        double askedAt = 0.0;
        int liveString = -1;
    };
}
