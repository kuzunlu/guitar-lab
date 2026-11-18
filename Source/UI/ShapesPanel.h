#pragma once

#include "PluginProcessor.h"
#include "Theory/ChordFinder.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"

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
        void setPreferFlats (bool f) { grid.flats = f; grid.repaint(); }

        std::vector<FretboardComponent::Mark> marks() const;
        std::map<PitchClass, juce::Colour> chordLayer() const;
        std::function<void()> onDisplayChanged;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void search();
        void choose (int index);
        void playShape (const Voicing& v);
        VoicingOptions options() const;

        GuitarLabProcessor& proc;

        juce::Label chordLabel, rootLabel, typeLabel, stretchLabel, areaLabel, notesLabel, status;
        juce::TextEditor chordInput;
        juce::ComboBox rootBox, typeBox, areaBox, notesBox;
        juce::Slider stretch;
        juce::ToggleButton rootBass { "Root lowest" }, openStrings { "Open strings" }, innerMutes { "Skip a string inside" };
        juce::Viewport view;
        ShapeGrid grid;

        std::optional<ParsedChord> parsed;
        int playToken = 0;
        bool updating = false;
    };
}
