#include "UI/FretboardComponent.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr float kNameColumn = 34.0f;
        constexpr float kOpenColumn = 30.0f;
        constexpr float kNumberRow  = 18.0f;
        constexpr double kEndSlot   = kNumFrets + 0.6; // a little room after the last fret
    }

    FretboardComponent::FretboardComponent()
    {
        setInstrument (Instrument{});
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void FretboardComponent::setInstrument (const Instrument& i)
    {
        inst = i;
        soundFrets.assign ((size_t) inst.numStrings(), -1);
        soundBends.assign ((size_t) inst.numStrings(), 0.0f);
        repaint();
    }

    void FretboardComponent::setMarks (std::vector<Mark> m)
    {
        marks = std::move (m);
        repaint();
    }

    void FretboardComponent::setPitchClassLayer (std::map<PitchClass, juce::Colour> pcs)
    {
        pcLayer = std::move (pcs);
        repaint();
    }

    void FretboardComponent::setSounding (const std::vector<int>& frets, const std::vector<float>& bends)
    {
        if (frets == soundFrets && bends == soundBends)
            return;
        soundFrets = frets;
        soundBends = bends;
        repaint();
    }

    // -----------------------------------------------------------------------
    void FretboardComponent::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
        r.removeFromBottom (kNumberRow);
        r.removeFromLeft (kNameColumn + kOpenColumn);
        r.removeFromRight (4.0f);
        board = r;
    }

    float FretboardComponent::fretX (int fret) const
    {
        // Mix of real guitar spacing and equal spacing, so the high frets stay big enough to click.
        const double real  = (1.0 - std::pow (2.0, -fret / 12.0)) / (1.0 - std::pow (2.0, -kEndSlot / 12.0));
        const double equal = fret / kEndSlot;
        return board.getX() + (float) (0.55 * real + 0.45 * equal) * board.getWidth();
    }

    float FretboardComponent::gapY() const
    {
        return board.getHeight() / (float) std::max (1, inst.numStrings());
    }

    float FretboardComponent::stringY (int string) const
    {
        return board.getY() + gapY() * (string + 0.5f);
    }

    juce::Point<float> FretboardComponent::centreOf (FretPos p) const
    {
        const float x = p.fret == 0 ? board.getX() - kOpenColumn * 0.5f
                                    : (fretX (p.fret - 1) + fretX (p.fret)) * 0.5f;
        return { x, stringY (p.string) };
    }

    std::optional<FretPos> FretboardComponent::posAt (juce::Point<float> p) const
    {
        if (p.y < board.getY() || p.y >= board.getBottom())
            return std::nullopt;

        const int string = juce::jlimit (0, inst.numStrings() - 1, (int) ((p.y - board.getY()) / gapY()));

        if (p.x < board.getX())
            return p.x >= board.getX() - kOpenColumn ? std::optional<FretPos> (FretPos { string, 0 }) : std::nullopt;

        for (int f = 1; f <= kNumFrets; ++f)
            if (p.x < fretX (f))
                return FretPos { string, f };

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    void FretboardComponent::drawDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col,
                                      const juce::String& text, bool filled)
    {
        auto area = juce::Rectangle<float> (r * 2, r * 2).withCentre (c);
        if (filled)
        {
            g.setColour (col);
            g.fillEllipse (area);
            g.setColour (col.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        }
        else
        {
            g.setColour (col);
            g.drawEllipse (area.reduced (1.0f), 2.0f);
        }

        if (text.isNotEmpty())
        {
            g.setFont (juce::FontOptions (juce::jmin (13.0f, r * 1.05f), juce::Font::bold));
            g.drawText (text, area, juce::Justification::centred, false);
        }
    }

    void FretboardComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);

        const int strings = inst.numStrings();
        const float gap = gapY();

        // Open-string column and neck wood
        g.setColour (theme::panel);
        g.fillRect (juce::Rectangle<float> (board.getX() - kOpenColumn, board.getY(), kOpenColumn, board.getHeight()));

        g.setGradientFill (juce::ColourGradient (theme::wood, board.getX(), board.getY(),
                                                 theme::woodDark, board.getX(), board.getBottom(), false));
        g.fillRect (juce::Rectangle<float> (board.getX(), board.getY(), fretX (kNumFrets) - board.getX(), board.getHeight()));

        // Practice range
        if (rangeLo >= 0 && rangeHi >= rangeLo)
        {
            const float x0 = rangeLo == 0 ? board.getX() - kOpenColumn : fretX (rangeLo - 1);
            const float x1 = fretX (juce::jmax (rangeHi, 0));
            g.setColour (theme::accent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (x0, board.getY(), x1 - x0, board.getHeight()));
        }

        // Inlays
        g.setColour (theme::inlay.withAlpha (0.55f));
        for (int f : { 3, 5, 7, 9, 12, 15, 17, 19, 21, 24 })
        {
            const float x = (fretX (f - 1) + fretX (f)) * 0.5f;
            const float r = juce::jmin (6.0f, (fretX (f) - fretX (f - 1)) * 0.18f);
            if (f == 12 || f == 24)
            {
                g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre ({ x, board.getY() + board.getHeight() * 0.25f }));
                g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre ({ x, board.getY() + board.getHeight() * 0.75f }));
            }
            else
            {
                g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre ({ x, board.getCentreY() }));
            }
        }

        // Heat map (trainer)
        if (heat)
        {
            for (int s = 0; s < strings; ++s)
                for (int f = 0; f <= kNumFrets; ++f)
                {
                    const double acc = heat ({ s, f });
                    if (acc < 0) continue;
                    const float x0 = f == 0 ? board.getX() - kOpenColumn : fretX (f - 1);
                    const float x1 = f == 0 ? board.getX() : fretX (f);
                    g.setColour (theme::bad.interpolatedWith (theme::good, (float) acc).withAlpha (0.38f));
                    g.fillRect (juce::Rectangle<float> (x0 + 1, board.getY() + gap * s + 1, x1 - x0 - 2, gap - 2));
                }
        }

        // Frets and nut
        g.setColour (theme::fretWire);
        for (int f = 1; f <= kNumFrets; ++f)
            g.fillRect (juce::Rectangle<float> (fretX (f) - 1.0f, board.getY(), 2.0f, board.getHeight()));
        g.setColour (theme::nut);
        g.fillRect (juce::Rectangle<float> (board.getX() - 3.0f, board.getY(), 5.0f, board.getHeight()));

        // Fret numbers
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        for (int f = 0; f <= kNumFrets; ++f)
        {
            const float cx = centreOf ({ 0, f }).x;
            g.drawText (juce::String (f), juce::Rectangle<float> (30.0f, kNumberRow).withCentre ({ cx, board.getBottom() + kNumberRow * 0.5f + 2 }),
                        juce::Justification::centred);
        }

        // Strings + names
        for (int s = 0; s < strings; ++s)
        {
            const float y = stringY (s);
            const float thick = 1.0f + 0.35f * s * (6.0f / (float) strings);
            g.setColour (theme::stringCol.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (board.getX() - kOpenColumn, y - thick * 0.5f,
                                                fretX (kNumFrets) - board.getX() + kOpenColumn, thick));

            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (juce::String (midiNoteName (inst.tuning()[(size_t) s], preferFlats)),
                        juce::Rectangle<float> (4.0f, y - 9.0f, kNameColumn - 4.0f, 18.0f), juce::Justification::centredLeft);
        }

        const float dotR = juce::jmin (gap * 0.38f, 13.0f);

        // All note names (faint)
        if (showAllNames)
        {
            g.setFont (juce::FontOptions (10.5f));
            for (int s = 0; s < strings; ++s)
                for (int f = 0; f <= kNumFrets; ++f)
                {
                    if (pcLayer.count (pitchClassOf (inst.noteAt ({ s, f })))) continue;
                    g.setColour (theme::text.withAlpha (0.45f));
                    g.drawText (juce::String (pitchClassName (pitchClassOf (inst.noteAt ({ s, f })), preferFlats)),
                                juce::Rectangle<float> (28.0f, 14.0f).withCentre (centreOf ({ s, f })), juce::Justification::centred);
                }
        }

        // Scale / chord layer
        for (int s = 0; s < strings; ++s)
            for (int f = 0; f <= kNumFrets; ++f)
            {
                const auto pc = pitchClassOf (inst.noteAt ({ s, f }));
                auto it = pcLayer.find (pc);
                if (it != pcLayer.end())
                    drawDot (g, centreOf ({ s, f }), dotR * 0.85f, it->second.withAlpha (0.9f),
                             juce::String (pitchClassName (pc, preferFlats)), true);
            }

        // Marks
        for (const auto& m : marks)
        {
            if (! inst.isValid (m.pos)) continue;
            const auto text = m.label.isNotEmpty() ? m.label
                                                   : juce::String (pitchClassName (pitchClassOf (inst.noteAt (m.pos)), preferFlats));
            drawDot (g, centreOf (m.pos), dotR, m.colour, text, m.filled);
        }

        // Sounding notes: green dot, moved up by the bend (one string gap = a whole step)
        for (int s = 0; s < strings && s < (int) soundFrets.size(); ++s)
        {
            const int f = soundFrets[(size_t) s];
            if (f < 0) continue;

            const auto base = centreOf ({ s, f });
            const float bend = (size_t) s < soundBends.size() ? soundBends[(size_t) s] : 0.0f;
            const auto c = base.translated (0.0f, -bend * gap * 0.5f);

            if (std::abs (bend) > 0.05f)
            {
                g.setColour (theme::sounding.withAlpha (0.6f));
                g.drawLine (base.x, base.y, c.x, c.y, 3.0f);
            }

            g.setColour (theme::sounding.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (dotR * 3.0f, dotR * 3.0f).withCentre (c));
            drawDot (g, c, dotR, theme::sounding, juce::String (pitchClassName (pitchClassOf (inst.noteAt ({ s, f })), preferFlats)), true);
        }

        // Hover
        if (hover)
        {
            g.setColour (theme::text.withAlpha (0.7f));
            g.drawEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (centreOf (*hover)), 1.5f);
        }
    }

    // -----------------------------------------------------------------------
    void FretboardComponent::mouseDown (const juce::MouseEvent& e)
    {
        pressed = posAt (e.position);
        pressPoint = e.position;
        if (pressed && onPress)
            onPress (*pressed, e.mods);
    }

    void FretboardComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (! pressed || ! onDrag)
            return;

        const float dx = e.position.x - pressPoint.x;
        const float dy = e.position.y - pressPoint.y;

        double bend = 0.0, slide = 0.0;

        if (std::abs (dy) >= std::abs (dx))
        {
            // Up or down both bend the note up, like pushing or pulling a real string.
            bend = std::abs (dy) / gapY() * 2.0;
        }
        else
        {
            // Fractional fret under the mouse, so slides glide smoothly.
            double fretUnder = pressed->fret;
            const float x = e.position.x;
            if (x < board.getX()) fretUnder = 0.0;
            else
                for (int f = 1; f <= kNumFrets; ++f)
                    if (x < fretX (f))
                    {
                        const float lo = fretX (f - 1), hi = fretX (f);
                        fretUnder = (f - 0.5) + (x - lo) / (hi - lo); // centre of fret f = f
                        break;
                    }
            slide = fretUnder - pressed->fret;
        }

        onDrag (*pressed, bend, slide);
    }

    void FretboardComponent::mouseUp (const juce::MouseEvent&)
    {
        if (pressed && onRelease)
            onRelease (*pressed);
        pressed.reset();
    }

    void FretboardComponent::mouseMove (const juce::MouseEvent& e)
    {
        auto p = posAt (e.position);
        if (p != hover)
        {
            hover = p;
            repaint();
        }
    }

    void FretboardComponent::mouseExit (const juce::MouseEvent&)
    {
        hover.reset();
        repaint();
    }

    juce::String FretboardComponent::getTooltip()
    {
        if (! hover)
            return "Fretboard (24 frets)\n"
                   "Top line = highest string, like guitar tab.\n"
                   "Click a spot to play it. While holding the mouse:\n"
                   "  - drag up or down to BEND the note (one string gap = one whole step),\n"
                   "  - drag left or right to SLIDE along the string.\n"
                   "Green dots = notes sounding now. Coloured dots = the scale or chord you picked.";

        const auto note = inst.noteAt (*hover);
        const int stringNumber = hover->string + 1;
        return juce::String ("String ") + juce::String (stringNumber) + " (" + juce::String (midiNoteName (inst.tuning()[(size_t) hover->string], preferFlats))
             + "), fret " + juce::String (hover->fret) + "\n"
             + "Note: " + juce::String (midiNoteName (note, preferFlats)) + "   MIDI number: " + juce::String (note) + "\n"
             + "Click to play. Hold and drag up/down to bend, left/right to slide.";
    }
}
