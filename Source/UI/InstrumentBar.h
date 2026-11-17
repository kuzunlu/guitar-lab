#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"

#include <map>

namespace gl
{
    // Top bar: strings, tuning, scale overlay, MIDI + sound settings.
    class InstrumentBar : public juce::Component
    {
    public:
        explicit InstrumentBar (GuitarLabProcessor& p);
        ~InstrumentBar() override;

        // Called after the instrument or the scale overlay changes.
        std::function<void()> onChange;

        // Re-reads everything from the processor (after a project is loaded).
        void refresh();

        // Pitch classes of the chosen scale (empty map when the overlay is off).
        std::map<PitchClass, juce::Colour> scaleLayer() const;
        bool showAllNames() const { return namesToggle.getToggleState(); }
        bool preferFlats() const  { return flatsToggle.getToggleState(); }

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void stringsChanged();
        void presetChanged();
        void tuningComboChanged (int string);
        void transpose (int semitones);
        void instrumentEdited();
        void showMidiSettings();
        void fillPresetBox();

        GuitarLabProcessor& proc;

        juce::Label     stringsLabel { {}, "Strings" };
        juce::ComboBox  stringsBox;
        juce::Label     tuningLabel { {}, "Tuning" };
        juce::ComboBox  presetBox;
        juce::TextButton downButton { "-1" }, upButton { "+1" };
        juce::OwnedArray<juce::ComboBox> stringTuning; // shown low string -> high string

        juce::Label     scaleLabel { {}, "Show scale" };
        juce::ComboBox  scaleRootBox, scaleTypeBox;
        juce::ToggleButton namesToggle { "All note names" }, flatsToggle { "Flats" };

        juce::TextButton midiButton { "MIDI / Sound..." };

        bool updating = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentBar)
    };
}
