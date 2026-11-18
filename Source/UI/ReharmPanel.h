#pragma once

#include "PluginProcessor.h"
#include "Theory/Reharm.h"
#include "Theory/Voicing.h"
#include "UI/FretboardComponent.h"
#include "UI/PianoComponent.h"
#include "UI/Theme.h"

namespace gl
{
    // A row of chord "cards".
    class ChordStrip : public juce::Component,
                       public juce::TooltipClient
    {
    public:
        std::vector<TimedChord> chords;
        Key key;
        int selected = -1;
        juce::String title;
        std::function<void (int)> onClick;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        juce::Rectangle<float> cardBounds (int i) const;
        int cardAt (juce::Point<float> p) const;
    };

    class ReharmPanel : public juce::Component,
                        private juce::ListBoxModel
    {
    public:
        ReharmPanel (GuitarLabProcessor& p, FretboardComponent& board, PianoComponent& piano);

        void refresh();                 // after the instrument changed or a project was loaded
        void setProgression (const juce::String& text) { input.setText (text, juce::dontSendNotification); parseInput(); }
        std::vector<FretboardComponent::Mark> marks() const;
        std::map<PitchClass, juce::Colour> chordLayer() const; // for the piano

        void fretPressed (FretPos pos);
        void fretReleased();

        std::function<void()> onPhraseReplaced; // "Send to phrase" wrote new notes
        std::function<void()> onDisplayChanged; // fretboard / piano should redraw marks

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        // ListBoxModel (suggestion list)
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        juce::String getTooltipForRow (int row) override;
        void applyRow (int row);

        Key currentKey() const;
        void parseInput();
        void runReharm();
        void revoice();
        void selectCard (bool resultRow, int index);
        void nextVoicing();
        void playVoicing (const Voicing& v);
        void sendToPhrase();
        VoicingOptions voicingOptions() const;

        GuitarLabProcessor& proc;
        FretboardComponent& board;
        PianoComponent& piano;

        juce::Label inputLabel, keyLabel, amountLabel, spanLabel, areaLabel, errorLabel, suggestionsLabel;
        juce::TextEditor input;
        juce::ComboBox keyRoot, keyMode;
        juce::OwnedArray<juce::ToggleButton> kindToggles;
        juce::Slider amount, span, area;
        juce::ToggleButton rootBass { "Root in bass" }, openStrings { "Open strings" };
        juce::TextButton reharmButton { "Reharmonize" }, newIdeaButton { "New idea" }, resetButton { "Reset" },
                         nextVoicingButton { "Next voicing" }, applyButton { "Apply suggestion" }, sendButton { "Send to phrase" };

        ChordStrip originalStrip, resultStrip;
        juce::ListBox suggestionList { "Suggestions", this };

        std::vector<TimedChord> original, result;
        std::vector<Suggestion> suggestions;
        std::vector<Voicing> voicings;           // one per result chord
        std::vector<int> voicingChoice;          // which alternative is used (for "Next voicing")
        bool selectedInResult = true;
        int  selectedIndex = -1;
        Voicing shownVoicing;
        unsigned seed = 1;
        int liveString = -1;
        int playToken = 0;
    };
}
