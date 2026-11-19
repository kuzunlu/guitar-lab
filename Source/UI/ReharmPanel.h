#pragma once

#include "PluginProcessor.h"
#include "Theory/Reharm.h"
#include "Theory/Voicing.h"
#include "UI/FretboardComponent.h"
#include "UI/PianoComponent.h"
#include "UI/Theme.h"
#include "UI/UiMemory.h"

namespace gl
{
    // A row of chord "cards". The width of a card is its length. Dragging the line after a card changes its length.
    class ChordStrip : public juce::Component,
                       public juce::TooltipClient
    {
    public:
        std::vector<TimedChord> chords;
        Key key;
        int selected = -1;
        juce::String title;
        std::function<void (int)> onClick;                                  // -1 = empty space
        std::function<void (const std::vector<double>&)> onLengthsChanged;  // new beats per chord, after a drag

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        juce::Rectangle<float> cardBounds (int i) const;
        int cardAt (juce::Point<float> p) const;
        int edgeAt (juce::Point<float> p) const;    // card whose right edge is under the mouse, -1 = none

        int dragEdge = -1;
        std::vector<double> dragStartBeats;
        double dragTotal = 0.0;                     // length shown while dragging (the scale stays put)
        float  dragPxPerBeat = 1.0f;
    };

    class ReharmPanel : public juce::Component,
                        private juce::ListBoxModel
    {
    public:
        ReharmPanel (GuitarLabProcessor& p, FretboardComponent& board, PianoComponent& piano);

        void refresh();                 // after the instrument changed or a project was loaded
        void restoreUi();               // after a project was loaded: the controls come back
        void setProgression (const juce::String& text) { input.setText (text, juce::dontSendNotification); parseInput(); }
        std::vector<FretboardComponent::Mark> marks() const;
        std::map<PitchClass, juce::Colour> chordLayer() const; // for the piano

        void fretPressed (FretPos pos);
        void fretReleased();

        std::function<void()> onPhraseReplaced; // "Send to phrase" wrote new notes
        std::function<void()> onDisplayChanged; // fretboard / piano should redraw marks
        std::function<void()> onHeightChanged;  // the number of suggestions changed: the page must be laid out again
        int heightFor (int width);              // height that shows every suggestion (the whole page scrolls)

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        // ListBoxModel (suggestion list)
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        juce::String getTooltipForRow (int row) override;
        void applyRow (int row);
        static constexpr int kStripsHeight = 170;   // the two chord rows together
        int  filterIndex() const;                   // bottom-row chord whose suggestions are listed, -1 = all
        bool isListed (const Suggestion& s) const;
        const Suggestion* listedSuggestion (int row) const;
        void updateSuggestionHeader();

        Key currentKey() const;
        void parseInput();
        void readOriginal();            // reads the typed progression into `original` (the result stays)
        void runReharm();
        void revoice();
        void updateView();              // strips, suggestion list and fretboard for the current result
        juce::String workKey() const;   // input, key and tuning: the saved result only fits these
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
                         nextVoicingButton { "Next voicing" }, applyButton { "Apply suggestion" }, sendButton { "Send to phrase" },
                         showAllButton { "Show all" };

        ChordStrip originalStrip, resultStrip;
        juce::ListBox suggestionList { "Suggestions", this };

        // The result and the choices live in the processor, so they are still there when the window opens again.
        std::vector<TimedChord> original;
        std::vector<TimedChord>& result = proc.work.reharmResult;
        std::vector<Suggestion> suggestions;
        std::vector<Voicing>& voicings = proc.work.reharmVoicings;         // one per result chord
        std::vector<int>& voicingChoice = proc.work.reharmVoicingChoice;   // which alternative is used (for "Next voicing")
        bool& selectedInResult = proc.work.reharmSelectedInResult;
        int&  selectedIndex = proc.work.reharmSelected;
        Voicing& shownVoicing = proc.work.reharmShown;
        unsigned& seed = proc.work.reharmSeed;
        int liveString = -1;
        int playToken = 0;
        UiMemory memory { proc.uiState, "Reharm" }; // last member: see UiMemory
    };
}
