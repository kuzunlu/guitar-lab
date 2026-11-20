#include "UI/LighthouseTuner.h"

#include "UI/Theme.h"

namespace gl
{
    namespace
    {
        constexpr int kWindow = 4096;   // newest input samples analysed each frame (~85 ms at 48 kHz)
    }

    LighthouseTuner::LighthouseTuner (GuitarLabProcessor& p)
        : proc (p), vblank (this, [this] (double t) { frame (t); })
    {
        input.assign (kWindow, 0.0f);
        work.assign (kWindow, 0.0f);
        closeButton.setTooltip ("Close\nCloses the tuner (Esc does the same). While it is closed it uses no processing at all.");
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
        setWantsKeyboardFocus (true);
    }

    LighthouseTuner::~LighthouseTuner()
    {
        proc.tunerOn = false;
    }

    void LighthouseTuner::open()
    {
        hasNote = false;
        note = -1;
        recentCount = 0;
        proc.tunerOn = true;
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
    }

    void LighthouseTuner::close()
    {
        proc.tunerOn = false;
        setVisible (false);
        if (onClose) onClose();
    }

    bool LighthouseTuner::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    double LighthouseTuner::detectHz (const float* x, int n, double rate, float& clarity) const
    {
        // YIN on a copy at half the rate (plenty for 60-1400 Hz), with parabolic interpolation of the dip.
        auto& y = const_cast<std::vector<float>&> (work);
        const int m = n / 2;
        for (int i = 0; i < m; ++i) y[(size_t) i] = 0.5f * (x[2 * i] + x[2 * i + 1]);
        const double r = rate / 2.0;
        const int tauMin = std::max (2, (int) (r / 1400.0));
        const int tauMax = std::min (m / 2, (int) (r / 60.0));
        const int w = m - tauMax;

        std::vector<float> d ((size_t) tauMax + 2, 1.0f);
        double running = 0.0;
        for (int tau = 1; tau <= tauMax + 1 && tau < m; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < w; ++j)
            {
                const double diff = (double) y[(size_t) j] - (double) y[(size_t) (j + tau)];
                sum += diff * diff;
            }
            running += sum;
            d[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            clarity = 0.0f;
            return 0.0;
        }

        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double denom = a - 2.0 * b + c;
        const double shift = std::abs (denom) > 1e-12 ? std::clamp (0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
        clarity = 1.0f - (float) b;
        return r / (best + shift);
    }

    void LighthouseTuner::frame (double now)
    {
        if (! isVisible()) { lastTime = now; return; }
        const double dt = std::clamp (now - lastTime, 0.0, 0.1);
        lastTime = now;

        proc.readTunerInput (input.data(), kWindow);
        double sq = 0.0;
        for (float v : input) sq += (double) v * v;
        level = (float) std::sqrt (sq / kWindow);

        const double rate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        float clarity = 0.0f;
        const double hz = level > 0.002f ? detectHz (input.data(), kWindow, rate, clarity) : 0.0;
        if (hz > 0.0 && clarity > 0.85f)
        {
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int n = (int) std::lround (midi);
            const double c = (midi - n) * 100.0;
            if (n != note || ! hasNote)
            {
                note = n;
                cents = c;
                recentCount = 0;
            }
            recent[(size_t) (recentCount++ % (int) recent.size())] = c;
            std::array<double, 5> sorted = recent;
            const int k = std::min (recentCount, (int) recent.size());
            std::sort (sorted.begin(), sorted.begin() + k);
            const double median = sorted[(size_t) (k / 2)];
            cents += (median - cents) * (1.0 - std::exp (-dt / 0.07)); // glide, no jumps
            hasNote = true;
            lastHeard = now;
        }
        if (now - lastHeard > 0.8) hasNote = false;

        if (hasNote)
        {
            beam += cents * 0.02 * dt;    // turns per second: 10 cents off = one turn every 5 s
            strobe += cents * 0.08 * dt;  // strobe pattern cycles per second
        }
        repaint();
    }

    void LighthouseTuner::resized()
    {
        closeButton.setBounds (getWidth() - 100, 12, 86, 26);
    }

    void LighthouseTuner::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
        auto area = getLocalBounds().reduced (20, 12);
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("Lighthouse tuner", area.removeFromTop (28), juce::Justification::centredLeft);

        const bool inTune = hasNote && std::abs (cents) < 2.0;
        const auto live = inTune ? theme::good : theme::accent;
        const float dim = hasNote ? 1.0f : 0.35f;

        // Bottom: the open strings of the tuning, the nearest one lit.
        auto strings = area.removeFromBottom (36);
        const auto& tuning = proc.instrument.tuning();
        int nearest = -1;
        if (hasNote)
            for (int s = 0; s < (int) tuning.size(); ++s)
                if (std::abs (tuning[(size_t) s] - note) <= 2
                    && (nearest < 0 || std::abs (tuning[(size_t) s] - note) < std::abs (tuning[(size_t) nearest] - note)))
                    nearest = s;
        const int count = (int) tuning.size();
        const int pill = std::min (90, strings.getWidth() / std::max (1, count) - 8);
        auto row = strings.withSizeKeepingCentre ((pill + 8) * count, 30);
        for (int i = count - 1; i >= 0; --i) // low to high, left to right
        {
            auto b = row.removeFromLeft (pill + 8).reduced (4, 0).toFloat();
            const bool lit = i == nearest;
            g.setColour (lit ? live.withAlpha (0.9f) : theme::panelLight);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (lit ? theme::background : theme::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (juce::String (midiNoteName (tuning[(size_t) i])), b, juce::Justification::centred);
        }
        area.removeFromBottom (12);

        // Three strobe bands (coarse to fine): they slide while out of tune and stand still in tune.
        auto bands = area.removeFromBottom (std::min (120, area.getHeight() / 3));
        const int bandH = bands.getHeight() / 3;
        for (int k = 0; k < 3; ++k)
        {
            auto band = bands.removeFromTop (bandH).reduced (0, 3).toFloat();
            g.setColour (theme::panel);
            g.fillRoundedRectangle (band, 4.0f);
            const float period = 64.0f / (float) (1 << k);
            const double phase = strobe * (double) (1 << k);
            const float offset = (float) (phase - std::floor (phase)) * period;
            g.saveState();
            g.reduceClipRegion (band.toNearestInt());
            g.setColour (live.withAlpha (0.85f * dim));
            for (float x = band.getX() - period + offset; x < band.getRight(); x += period)
                g.fillRect (x, band.getY(), period * 0.5f, band.getHeight());
            g.restoreState();
        }
        area.removeFromBottom (10);

        // Left: the lighthouse lamp. Its beam turns while out of tune (clockwise = sharp) and stops in tune.
        auto lampArea = area.removeFromLeft (area.getWidth() / 2).toFloat();
        const float radius = std::min (lampArea.getWidth(), lampArea.getHeight()) * 0.42f;
        const auto centre = lampArea.getCentre();
        const float angle = (float) (beam * juce::MathConstants<double>::twoPi);
        for (int side = 0; side < 2; ++side)
        {
            juce::Path wedge;
            const float a = angle + (float) side * juce::MathConstants<float>::pi;
            wedge.addPieSegment (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, a - 0.32f, a + 0.32f, 0.0f);
            juce::ColourGradient grad (live.withAlpha (0.75f * dim), centre.x, centre.y,
                                       live.withAlpha (0.0f), centre.x + radius * std::sin (a), centre.y - radius * std::cos (a), false);
            g.setGradientFill (grad);
            g.fillPath (wedge);
        }
        g.setColour (theme::panelLight);
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
        const float lamp = radius * 0.18f;
        g.setColour (live.withAlpha (dim));
        g.fillEllipse (centre.x - lamp, centre.y - lamp, lamp * 2.0f, lamp * 2.0f);

        // Right: the note and how far off it is.
        auto text = area.reduced (10, 0);
        const juce::String name = note >= 0 ? juce::String (midiNoteName (note)) : juce::String ("-");
        g.setColour ((hasNote ? theme::text : theme::textDim));
        g.setFont (juce::FontOptions ((float) std::min (150, text.getHeight() / 2), juce::Font::bold));
        g.drawText (name, text.removeFromTop (text.getHeight() * 6 / 10), juce::Justification::centredBottom);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.setColour (live.withAlpha (dim));
        const juce::String verdict = ! hasNote ? juce::String ("play one string")
                                   : inTune     ? juce::String ("in tune")
                                   : juce::String (cents > 0 ? "+" : "") + juce::String (cents, 1) + " cents  "
                                         + (cents > 0 ? "(sharp: tune down)" : "(flat: tune up)");
        g.drawText (verdict, text.removeFromTop (40), juce::Justification::centred);
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (theme::textDim);
        g.drawText ("A4 = 440 Hz.  Beam and bands stand still when the string is in tune.", text.removeFromTop (24),
                    juce::Justification::centred);
    }
}
