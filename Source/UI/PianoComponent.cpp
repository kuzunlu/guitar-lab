#include "UI/PianoComponent.h"

namespace gl
{
    PianoComponent::PianoComponent()
    {
        setInstrument (Instrument{});
    }

    void PianoComponent::setInstrument (const Instrument& i)
    {
        inst = i;
        int lo = 127, hi = 0;
        for (auto n : inst.tuning())
        {
            lo = std::min (lo, n);
            hi = std::max (hi, n + kNumFrets);
        }
        // Start on a C and end on a B so the keyboard looks complete.
        lowest  = std::max (0, lo - pitchClassOf (lo));
        highest = std::min (127, hi + (11 - pitchClassOf (hi)));
        repaint();
    }

    void PianoComponent::setSounding (const std::set<MidiNote>& notes)
    {
        if (notes == sounding)
            return;
        sounding = notes;
        repaint();
    }

    bool PianoComponent::isBlack (MidiNote n)
    {
        switch (pitchClassOf (n)) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; }
    }

    int PianoComponent::whiteIndex (MidiNote n) const
    {
        int count = 0;
        for (int i = lowest; i < n; ++i)
            if (! isBlack (i)) ++count;
        return count;
    }

    int PianoComponent::numWhites() const
    {
        return whiteIndex (highest + 1);
    }

    juce::Rectangle<float> PianoComponent::keyRect (MidiNote n) const
    {
        const auto area = getLocalBounds().toFloat().reduced (2.0f);
        const float w = area.getWidth() / (float) std::max (1, numWhites());

        if (! isBlack (n))
            return { area.getX() + whiteIndex (n) * w, area.getY(), w, area.getHeight() };

        const float bw = w * 0.62f;
        return { area.getX() + whiteIndex (n) * w - bw * 0.5f, area.getY(), bw, area.getHeight() * 0.62f };
    }

    std::optional<MidiNote> PianoComponent::noteAt (juce::Point<float> p) const
    {
        for (int n = lowest; n <= highest; ++n)       // black keys sit on top: check them first
            if (isBlack (n) && keyRect (n).contains (p))
                return n;
        for (int n = lowest; n <= highest; ++n)
            if (! isBlack (n) && keyRect (n).contains (p))
                return n;
        return std::nullopt;
    }

    void PianoComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);

        int instLo = 127, instHi = 0;
        for (auto n : inst.tuning()) { instLo = std::min (instLo, n); instHi = std::max (instHi, n + kNumFrets); }

        auto keyColour = [&] (MidiNote n, juce::Colour base) -> juce::Colour
        {
            if (sounding.count (n))            return theme::sounding;
            if (auto it = noteMarks.find (n); it != noteMarks.end()) return it->second;
            if (auto it = pcLayer.find (pitchClassOf (n)); it != pcLayer.end())
                return base.interpolatedWith (it->second, 0.55f);
            if (n < instLo || n > instHi)      return base.interpolatedWith (theme::background, 0.5f); // outside the guitar's range
            return base;
        };

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int n = lowest; n <= highest; ++n)
            {
                if (isBlack (n) != (pass == 1))
                    continue;

                auto r = keyRect (n);
                const auto base = isBlack (n) ? juce::Colour (0xff1a1a1a) : juce::Colour (0xfff2f0ea);
                g.setColour (keyColour (n, base));
                g.fillRoundedRectangle (r.reduced (0.5f), 2.5f);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);

                if (hover && *hover == n)
                {
                    g.setColour (theme::accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (r.reduced (1.0f), 2.5f);
                }

                // Label every C, and every marked/sounding key.
                const bool marked = sounding.count (n) || noteMarks.count (n);
                if (pitchClassOf (n) == 0 || marked)
                {
                    g.setColour (isBlack (n) && ! marked ? theme::text : juce::Colours::black);
                    g.setFont (juce::FontOptions (r.getWidth() < 14 ? 8.5f : 10.0f, marked ? juce::Font::bold : juce::Font::plain));
                    const auto label = pitchClassOf (n) == 0 ? juce::String (midiNoteName (n))
                                                             : juce::String (pitchClassName (pitchClassOf (n), preferFlats));
                    g.drawText (label, r.removeFromBottom (16.0f), juce::Justification::centred, false);
                }
            }
        }
    }

    void PianoComponent::mouseDown (const juce::MouseEvent& e)
    {
        pressed = noteAt (e.position);
        if (pressed && onKeyPress)
            onKeyPress (*pressed);
    }

    void PianoComponent::mouseUp (const juce::MouseEvent&)
    {
        if (pressed && onKeyRelease)
            onKeyRelease (*pressed);
        pressed.reset();
    }

    void PianoComponent::mouseMove (const juce::MouseEvent& e)
    {
        auto n = noteAt (e.position);
        if (n != hover) { hover = n; repaint(); }
    }

    void PianoComponent::mouseExit (const juce::MouseEvent&)
    {
        hover.reset();
        repaint();
    }

    juce::String PianoComponent::getTooltip()
    {
        if (! hover)
            return "Piano view\n"
                   "Shows the same notes as the fretboard, so you can see them as a keyboard player would.\n"
                   "Green = sounding now. Coloured = notes of the chosen scale or chord.\n"
                   "Dimmed keys are outside your guitar's range (lowest open string to fret 24 on the highest string).\n"
                   "Click a key to hear it and to show every place it can be played on the neck.";

        const auto spots = inst.positionsOf (*hover);
        juce::String where;
        for (auto p : spots)
            where << "\n  string " << (p.string + 1) << ", fret " << p.fret;

        return juce::String (midiNoteName (*hover, preferFlats)) + "  (MIDI " + juce::String (*hover) + ")\n"
             + (spots.empty() ? juce::String ("Not playable on this tuning (outside the range).")
                              : "Can be played on " + juce::String ((int) spots.size()) + " spot(s):" + where);
    }
}
