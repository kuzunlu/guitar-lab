#pragma once

#include "PluginProcessor.h"
#include "Theory/ChordFinder.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"
#include "UI/UiMemory.h"

#include <optional>

namespace gl
{
    // A grid of chord diagrams (like a chord book page).
    class ShapeGrid : public juce::Component,
                      public juce::TooltipClient
    {
    public:
        std::vector<Voicing> shapes;
        std::vector<juce::String> kinds;   // "Open chord", "Barre, E shape"... for the usual fingerings, "" for the rest
        Instrument inst;
        int root = 0;
        int selected = -1;
        bool flats = false;
        std::function<void (int)> onClick;

        static constexpr int kCardW = 118, kCardH = 156;
        int columnsFor (int width) const { return std::max (1, width / kCardW); }
        int heightFor (int width) const;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        int cardAt (juce::Point<int> p) const;
        void drawCard (juce::Graphics&, juce::Rectangle<float> r, const Voicing& v, bool isSelected, const juce::String& kind) const;
    };

    // "Chord Shapes" tab: type or choose any chord and see every way to play it.
    class ShapesPanel : public juce::Component
    {
    public:
        explicit ShapesPanel (GuitarLabProcessor& p);

        void refresh();   // instrument changed or project loaded
        void restoreUi(); // after a project was loaded: the controls come back
        void setPreferFlats (bool f) { grid.flats = f; grid.repaint(); }

        std::vector<FretboardComponent::Mark> marks() const;
        std::map<PitchClass, juce::Colour> chordLayer() const;
        std::function<void()> onDisplayChanged;
        std::function<void()> onHeightChanged;  // the number of shapes changed: the page must be laid out again
        int heightFor (int width) const;        // height that shows every shape (the whole page scrolls, not the list)

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void search();
        void restoreSelection();
        void choose (int index);
        void playShape (const Voicing& v);
        VoicingOptions options() const;
        void fillStringBoxes(); // 2 .. number of strings, keeps the choice when it still fits

        GuitarLabProcessor& proc;

        juce::Label chordLabel, rootLabel, typeLabel, stretchLabel, areaLabel, notesLabel, notesToLabel, status;
        juce::TextEditor chordInput;
        juce::ComboBox rootBox, typeBox, areaBox, notesBox, maxNotesBox; // strings played: from notesBox to maxNotesBox
        juce::Slider stretch;
        juce::ToggleButton rootBass { "Root lowest" }, openStrings { "Open strings" }, innerMutes { "Skip a string inside" };
        ShapeGrid grid;

        std::optional<ParsedChord> parsed;
        int playToken = 0;
        bool updating = false;
        UiMemory memory { proc.uiState, "Shapes" }; // last member: see UiMemory
    };
}
