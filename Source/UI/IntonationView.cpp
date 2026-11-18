#include "UI/IntonationView.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr float kLeft = 40.0f, kTop = 64.0f, kBottom = 24.0f, kAdviceH = 150.0f;

        juce::Colour colourFor (double cents)
        {
            const double a = std::abs (cents);
            return a <= 5.0 ? theme::good : a <= 12.0 ? theme::accent : theme::bad;
        }
    }

    IntonationView::IntonationView()
    {
        closeButton.setTooltip ("Close\nBack to the Guitar -> MIDI tab.");
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);
    }

    void IntonationView::show (const Instrument& i, const Calibration& c)
    {
        inst = i;
        cal = c;
        setVisible (true);
        toFront (true);
        repaint();
    }

    double IntonationView::relativeCents (const Calibration& cal, int string, int fret)
    {
        const auto* p = cal.position (string, fret);
        if (p == nullptr) return std::nan ("");
        if (fret == 0) return 0.0;
        const auto* open = cal.position (string, 0);
        return p->cents - (open != nullptr ? open->cents : cal.centsFor (string));
    }

    juce::Rectangle<float> IntonationView::gridArea() const
    {
        return getLocalBounds().toFloat().reduced (16.0f).withTrimmedLeft (kLeft).withTrimmedTop (kTop - 16.0f)
                   .withTrimmedBottom (kAdviceH + kBottom);
    }

    juce::String IntonationView::adviceFor (int s) const
    {
        const juce::String name = juce::String (midiNoteName (inst.tuning()[(size_t) s])) + " string";
        double at12 = relativeCents (cal, s, 12);
        if (std::isnan (at12))
        {
            // 12th fret not measured: use the frets around it.
            double sum = 0; int n = 0;
            for (int f : { 11, 13 }) if (! std::isnan (relativeCents (cal, s, f))) { sum += relativeCents (cal, s, f); ++n; }
            if (n == 0) return name + ": not measured (run a Full calibration up to the 12th fret).";
            at12 = sum / n;
        }

        const int rounded = (int) std::lround (at12);
        juce::String text = name + ": 12th fret " + (rounded >= 0 ? "+" : "") + juce::String (rounded) + " cents. ";
        if (at12 > 5.0)       text << "Sharp: make the string a little longer - move its bridge saddle BACK (away from the neck).";
        else if (at12 < -5.0) text << "Flat: make the string a little shorter - move its bridge saddle FORWARD (toward the neck).";
        else                  text << "Good.";

        double low = 0; int n = 0;
        for (int f = 1; f <= 3; ++f) if (! std::isnan (relativeCents (cal, s, f))) { low += relativeCents (cal, s, f); ++n; }
        if (n > 0 && low / n > 10.0)
            text << " The first frets play sharp: the nut may be too high, or you press very hard there.";
        return text;
    }

    void IntonationView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.5f);

        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("Intonation report", r.reduced (16.0f).removeFromTop (24.0f), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("How far each fret plays from where it should, compared with its open string (cents; 100 cents = one half step). "
                    "Green = within 5 cents, yellow = 5-12, red = more.",
                    r.reduced (16.0f).withTrimmedTop (24.0f).removeFromTop (20.0f), juce::Justification::centredLeft);

        if (! cal.hasPositions())
        {
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText ("No data yet. Run a FULL calibration (Calibrate... > Full): it measures the real pitch of every fret, "
                              "and this report shows the result.", getLocalBounds().reduced (40), juce::Justification::centred, 3);
            return;
        }

        const auto grid = gridArea();
        const int strings = inst.numStrings();
        int frets = 0;
        for (int s = 0; s < strings; ++s) frets = std::max (frets, cal.lastFret[(size_t) s]);
        const float cw = grid.getWidth() / (float) (frets + 1), ch = grid.getHeight() / (float) strings;

        for (int s = 0; s < strings; ++s)
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::String (midiNoteName (inst.tuning()[(size_t) s])),
                        juce::Rectangle<float> (grid.getX() - kLeft, grid.getY() + s * ch, kLeft - 6.0f, ch), juce::Justification::centredRight);

            for (int f = 0; f <= frets; ++f)
            {
                const auto cell = juce::Rectangle<float> (grid.getX() + f * cw, grid.getY() + s * ch, cw, ch).reduced (1.0f);
                const double c = relativeCents (cal, s, f);
                if (std::isnan (c))
                {
                    g.setColour (theme::panelLight);
                    g.fillRect (cell);
                    continue;
                }
                g.setColour (colourFor (c).withAlpha (0.75f));
                g.fillRect (cell);
                if (cw > 22.0f && f > 0)
                {
                    g.setColour (juce::Colours::black);
                    g.setFont (juce::FontOptions (std::min (11.0f, ch * 0.5f)));
                    g.drawText ((std::lround (c) > 0 ? "+" : "") + juce::String ((int) std::lround (c)), cell, juce::Justification::centred);
                }
            }
        }
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        for (int f = 0; f <= frets; ++f)
            g.drawText (juce::String (f), juce::Rectangle<float> (grid.getX() + f * cw, grid.getBottom() + 2.0f, cw, 16.0f),
                        juce::Justification::centred);

        // What to do, string by string (lowest string first)
        auto advice = juce::Rectangle<float> (grid.getX() - kLeft, grid.getBottom() + kBottom, grid.getWidth() + kLeft, kAdviceH);
        g.setFont (juce::FontOptions (12.5f));
        const float lineH = std::min (20.0f, kAdviceH / (float) (strings + 1));
        for (int s = strings - 1; s >= 0; --s)
        {
            g.setColour (theme::text);
            g.drawText (adviceFor (s), advice.removeFromTop (lineH), juce::Justification::centredLeft);
        }
        g.setColour (theme::textDim);
        g.drawText ("Tip: tune the open string first, then compare the 12th fret. Change one saddle a little at a time, "
                    "then calibrate again to check.", advice.removeFromTop (lineH), juce::Justification::centredLeft);
    }

    void IntonationView::resized()
    {
        closeButton.setBounds (getWidth() - 96, 12, 80, 26);
    }

    juce::String IntonationView::getTooltip()
    {
        const auto grid = gridArea();
        const auto m = getMouseXYRelative().toFloat();
        if (! cal.hasPositions() || ! grid.contains (m))
            return "Intonation report\nMade from your Full calibration. If the 12th fret is sharp, the string is too short "
                   "(move its saddle back); if it is flat, the string is too long (move the saddle forward).";
        int frets = 0;
        for (int s = 0; s < inst.numStrings(); ++s) frets = std::max (frets, cal.lastFret[(size_t) s]);
        const int f = (int) ((m.x - grid.getX()) / (grid.getWidth() / (float) (frets + 1)));
        const int s = (int) ((m.y - grid.getY()) / (grid.getHeight() / (float) inst.numStrings()));
        const double c = relativeCents (cal, s, f);
        return "String " + juce::String (s + 1) + " (" + juce::String (midiNoteName (inst.tuning()[(size_t) s])) + "), fret " + juce::String (f)
             + (std::isnan (c) ? juce::String (": not measured.")
                               : ": " + juce::String (c, 1) + " cents " + (c >= 0 ? "sharp" : "flat") + " compared with the open string.");
    }
}
