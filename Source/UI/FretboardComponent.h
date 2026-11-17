#pragma once

#include "Theory/Instrument.h"
#include "UI/Theme.h"

#include <functional>
#include <map>
#include <optional>

namespace gl
{
    // A 24-fret neck for 6, 7 or 8 strings. String 0 (highest) is drawn at the top, like tab.
    class FretboardComponent : public juce::Component,
                               public juce::TooltipClient
    {
    public:
        struct Mark
        {
            FretPos pos;
            juce::Colour colour;
            juce::String label;     // text inside the dot ("" = note name)
            bool filled = true;     // false = ring only
        };

        FretboardComponent();

        void setInstrument (const Instrument& inst);
        const Instrument& getInstrument() const { return inst; }

        // --- What to draw ---
        void setMarks (std::vector<Mark> m);
        void clearMarks()                                         { setMarks ({}); }

        // Pitch classes to show on the whole neck (scale / chord view). Colour per pitch class.
        void setPitchClassLayer (std::map<PitchClass, juce::Colour> pcs);

        // Optional heat map: 0..1 accuracy per spot (-1 = no data). Red = weak, green = strong.
        void setHeatMap (std::function<double (FretPos)> fn) { heat = std::move (fn); repaint(); }

        // Notes sounding right now: fret per string (-1 = silent) and bend in semitones.
        void setSounding (const std::vector<int>& frets, const std::vector<float>& bends);

        void setShowAllNoteNames (bool b) { showAllNames = b; repaint(); }
        void setPreferFlats (bool b)      { preferFlats = b; repaint(); }
        void setFretRangeHighlight (int lo, int hi) { rangeLo = lo; rangeHi = hi; repaint(); }

        // --- Mouse callbacks ---
        std::function<void (FretPos, const juce::ModifierKeys&)> onPress;
        std::function<void (FretPos start, double bendSemis, double slideFrets)> onDrag; // live bends / slides
        std::function<void (FretPos)> onRelease;

        // --- Component ---
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        juce::String getTooltip() override;

        std::optional<FretPos> posAt (juce::Point<float> p) const;
        juce::Point<float> centreOf (FretPos p) const;

    private:
        float fretX (int fret) const;    // x of the fret wire (0 = nut)
        float stringY (int string) const;
        float gapY() const;

        void drawDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, const juce::String& text, bool filled);

        Instrument inst;
        std::vector<Mark> marks;
        std::map<PitchClass, juce::Colour> pcLayer;
        std::function<double (FretPos)> heat;
        std::vector<int> soundFrets;
        std::vector<float> soundBends;

        bool showAllNames = false;
        bool preferFlats = false;
        int rangeLo = -1, rangeHi = -1;

        juce::Rectangle<float> board;
        float openColumn = 0;

        std::optional<FretPos> hover, pressed;
        juce::Point<float> pressPoint;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FretboardComponent)
    };
}
