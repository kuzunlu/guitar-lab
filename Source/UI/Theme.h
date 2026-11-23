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
    inline const juce::Colour onAccent    { 0xff1b1c20 }; // text on an accent-coloured button
    inline const juce::Colour outline     { 0xff3a3f4a }; // thin edge of buttons, boxes and text fields

    // The main action of a tab (Calibrate, Play, Reharmonize...): filled with the accent colour, dark text.
    // Use it on one or two buttons per tab, so the eye finds them first.
    inline void primary (juce::Button& b)
    {
        b.setColour (juce::TextButton::buttonColourId, accent);
        b.setColour (juce::TextButton::textColourOffId, onAccent);
        b.setColour (juce::TextButton::textColourOnId, onAccent);
    }

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
            setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::backgroundColourId, panelLight.brighter (0.35f)); // the empty part of the track stays visible
            setColour (juce::TextButton::buttonColourId, panelLight);
            setColour (juce::ComboBox::backgroundColourId, panelLight);
            setColour (juce::ComboBox::outlineColourId, outline);
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::TextEditor::backgroundColourId, background);
            setColour (juce::TextEditor::outlineColourId, outline);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::Slider::textBoxOutlineColourId, outline);
            setColour (juce::Slider::textBoxBackgroundColourId, background);
        }

        // Buttons: rounded, flat, a thin edge; lighter when the mouse is over them, darker while pressed.
        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                   bool highlighted, bool down) override
        {
            const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
            auto fill = colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);
            if (down)             fill = fill.darker (0.25f);
            else if (highlighted) fill = fill.brighter (fill.getPerceivedBrightness() > 0.5f ? 0.08f : 0.25f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 6.0f);
            if (colour == panelLight) // plain buttons get an edge; coloured ones stand out by their fill
            {
                g.setColour (button.hasKeyboardFocus (false) ? accent.withAlpha (0.7f) : outline);
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
        }

        // Check boxes: a rounded box with an accent-coloured tick and edge when on (calm, so the main buttons stay the brightest).
        void drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                          bool ticked, bool enabled, bool highlighted, bool) override
        {
            const auto r = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
            const float alpha = enabled ? 1.0f : 0.4f;
            if (ticked)
            {
                g.setColour (accent.withAlpha (0.15f * alpha));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (accent.withMultipliedAlpha (0.8f * alpha));
                g.drawRoundedRectangle (r, 4.0f, 1.2f);
                auto tick = getTickShape (0.75f);
                g.setColour (accent.withMultipliedAlpha (alpha));
                g.fillPath (tick, tick.getTransformToScaleToFit (r.reduced (3.0f, 3.5f), false));
            }
            else
            {
                g.setColour (background.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour ((highlighted ? textDim.brighter (0.3f) : textDim).withMultipliedAlpha (alpha));
                g.drawRoundedRectangle (r, 4.0f, 1.2f);
            }
        }

        // Drop-down boxes: same shape as the buttons, with a small arrow.
        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
            auto fill = box.findColour (juce::ComboBox::backgroundColourId);
            if (box.isMouseOver (true) && box.isEnabled()) fill = fill.brighter (0.25f);
            g.setColour (fill.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.45f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (box.hasKeyboardFocus (true) ? accent.withAlpha (0.7f) : box.findColour (juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
            juce::Path arrow;
            arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
            arrow.lineTo (cx, cy + 2.5f);
            arrow.lineTo (cx + 4.5f, cy - 2.0f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.3f));
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Tabs: plain text on the page colour; the open tab is bright, bold and underlined in the accent colour.
        int getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth) override
        {
            const auto font = getTabButtonFont (button, (float) tabDepth);
            return juce::GlyphArrangement::getStringWidthInt (font, button.getButtonText().trim()) + 36;
        }

        int getTabButtonOverlap (int) override { return -2; } // a small gap between tabs

        juce::Font getTabButtonFont (juce::TabBarButton&, float) override
        {
            return juce::Font (juce::FontOptions (15.0f, juce::Font::bold));
        }

        void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool over, bool) override
        {
            const auto r = button.getActiveArea().toFloat();
            const bool front = button.isFrontTab();
            if (front || over)
            {
                g.setColour (front ? panel : panelLight.withAlpha (0.5f));
                g.fillRoundedRectangle (r.withTrimmedBottom (-6.0f), 6.0f); // rounded top corners only (the bottom is cut off)
            }
            if (front)
            {
                g.setColour (accent);
                g.fillRect (r.withTop (r.getBottom() - 3.0f).reduced (10.0f, 0.0f));
            }
            g.setColour (front ? text : (over ? text.withAlpha (0.85f) : textDim));
            g.setFont (getTabButtonFont (button, r.getHeight()));
            g.drawText (button.getButtonText().trim(), button.getTextArea(), juce::Justification::centred, false);
        }

        void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override
        {
            g.setColour (background);
            g.fillRect (bar.getLocalBounds());
        }

        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h) override
        {
            juce::ignoreUnused (bar);
            g.setColour (outline);
            g.fillRect (0, h - 1, w, 1); // a thin line under the whole tab row
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
