#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gl::theme
{
    inline const juce::Colour background  { 0xff16181d };
    inline const juce::Colour panel       { 0xff1f2229 };
    inline const juce::Colour panelLight  { 0xff2a2e37 };
    inline const juce::Colour text        { 0xffe6e6e6 };
    inline const juce::Colour textDim     { 0xff9aa0aa };

    inline const juce::Colour wood        { 0xff3b2a1e };
    inline const juce::Colour woodDark    { 0xff2b1f16 };
    inline const juce::Colour fretWire    { 0xffb9bcc2 };
    inline const juce::Colour nut         { 0xffe8e2d0 };
    inline const juce::Colour stringCol   { 0xffd8d2c4 };
    inline const juce::Colour inlay       { 0xffcfc6b0 };

    inline const juce::Colour root        { 0xffff8a3d }; // orange: root notes
    inline const juce::Colour chordTone   { 0xff4aa3ff }; // blue: chord tones
    inline const juce::Colour scaleTone   { 0xff7d8590 }; // grey: other scale notes
    inline const juce::Colour sounding    { 0xff5dff9a }; // green: notes playing right now
    inline const juce::Colour good        { 0xff3ecf6e };
    inline const juce::Colour bad         { 0xffff4d5e };
    inline const juce::Colour accent      { 0xffffc94a };

    // Applies the dark colours to standard JUCE widgets.
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ panel, background, panelLight, textDim, text, accent, juce::Colours::black, accent, text });
            setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff101216));
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.6f));
            setColour (juce::TabbedButtonBar::tabOutlineColourId, panelLight);
            setColour (juce::TabbedComponent::backgroundColourId, panel);
            setColour (juce::Slider::backgroundColourId, panelLight.brighter (0.35f)); // the empty part of the track stays visible
        }

        // Sliders: a small stop at each end of the track (taller than the knob), so it is clear where the range starts and ends.
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (! slider.isBar() && slider.isHorizontal())
            {
                const float mid = (float) y + (float) height * 0.5f;
                const float h = juce::jmin ((float) height * 0.8f, 18.0f);
                g.setColour (textDim);
                g.fillRoundedRectangle ((float) x - 1.0f, mid - h * 0.5f, 2.0f, h, 1.0f);
                g.fillRoundedRectangle ((float) (x + width) - 1.0f, mid - h * 0.5f, 2.0f, h, 1.0f);
            }
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        }

        // Tooltips: wider and left-aligned so long help texts are easy to read.
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override
        {
            const auto layout = makeTooltipLayout (tipText);
            const int w = (int) layout.getWidth() + 24;
            const int h = (int) layout.getHeight() + 16;
            return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                         screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                         w, h).constrainedWithin (parentArea);
        }

        void drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height) override
        {
            g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
            g.fillRoundedRectangle (0, 0, (float) width, (float) height, 5.0f);
            g.setColour (findColour (juce::TooltipWindow::outlineColourId));
            g.drawRoundedRectangle (0.5f, 0.5f, width - 1.0f, height - 1.0f, 5.0f, 1.0f);
            makeTooltipLayout (tipText).draw (g, { 12.0f, 8.0f, width - 24.0f, height - 16.0f });
        }

    private:
        static juce::TextLayout makeTooltipLayout (const juce::String& tip)
        {
            // First line = bold title, rest = normal text.
            const auto title = tip.upToFirstOccurrenceOf ("\n", false, false);
            const auto body  = tip.fromFirstOccurrenceOf ("\n", false, false);

            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.append (title, juce::Font (juce::FontOptions (14.5f, juce::Font::bold)), accent);
            if (body.isNotEmpty())
                s.append ("\n" + body, juce::Font (juce::FontOptions (13.5f)), text);

            juce::TextLayout layout;
            layout.createLayout (s, 420.0f);
            return layout;
        }
    };
}
