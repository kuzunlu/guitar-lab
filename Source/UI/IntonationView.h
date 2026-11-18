#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Calibration.h"
#include "UI/Theme.h"

namespace gl
{
    // Intonation report: how far each fret plays from where it should, compared with its own open string.
    // Made from a Full calibration (it measured the real pitch of every fret). Shown over the Guitar -> MIDI tab.
    class IntonationView : public juce::Component,
                           public juce::TooltipClient
    {
    public:
        IntonationView();

        void show (const Instrument& inst, const Calibration& cal);

        void paint (juce::Graphics&) override;
        void resized() override;
        juce::String getTooltip() override;

        // Cents of a fret compared with the open string (+ = sharp). NAN when not measured.
        static double relativeCents (const Calibration& cal, int string, int fret);

    private:
        juce::Rectangle<float> gridArea() const;
        juce::String adviceFor (int string) const;

        Instrument inst;
        Calibration cal;
        juce::TextButton closeButton { "Close" };
    };
}
