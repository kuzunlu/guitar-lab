#pragma once

#include "Theory/Instrument.h"
#include "UI/Theme.h"

#include <functional>
#include <map>
#include <optional>
#include <set>

namespace gl
{
    // Piano keyboard across the instrument's range. Shows the same notes as the fretboard.
    class PianoComponent : public juce::Component,
                           public juce::TooltipClient
    {
    public:
        PianoComponent();

        void setInstrument (const Instrument& inst);

        void setPitchClassLayer (std::map<PitchClass, juce::Colour> pcs) { pcLayer = std::move (pcs); repaint(); }
        void setNoteMarks (std::map<MidiNote, juce::Colour> notes)        { noteMarks = std::move (notes); repaint(); }
        void setSounding (const std::set<MidiNote>& notes);
        void setPreferFlats (bool b) { preferFlats = b; repaint(); }

        std::function<void (MidiNote)> onKeyPress;
        std::function<void (MidiNote)> onKeyRelease;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        static bool isBlack (MidiNote n);
        juce::Rectangle<float> keyRect (MidiNote n) const;
        int  whiteIndex (MidiNote n) const;
        int  numWhites() const;
        std::optional<MidiNote> noteAt (juce::Point<float> p) const;

        Instrument inst;
        MidiNote lowest = 28, highest = 88;
        std::map<PitchClass, juce::Colour> pcLayer;
        std::map<MidiNote, juce::Colour> noteMarks;
        std::set<MidiNote> sounding;
        bool preferFlats = false;
        std::optional<MidiNote> hover, pressed;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoComponent)
    };
}
