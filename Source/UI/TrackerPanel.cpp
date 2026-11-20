#include "UI/TrackerPanel.h"

#include "Tracking/PitchDetector.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr double kHopSeconds = 0.005; // PitchDetector hop (~5 ms)

        juce::String tuningLowToHigh (const Instrument& inst)
        {
            juce::StringArray names;
            for (int s = inst.numStrings() - 1; s >= 0; --s)
                names.add (juce::String (midiNoteName (inst.tuning()[(size_t) s])));
            return names.joinIntoString ("   ");
        }

        bool hexMode (const GuitarLabProcessor& p)
        {
            return (int) p.params.getRawParameterValue (ParamIDs::trackMode)->load() == (int) TrackMode::Hex;
        }

        TrackMode trackMode (const GuitarLabProcessor& p)
        {
            return (TrackMode) (int) p.params.getRawParameterValue (ParamIDs::trackMode)->load();
        }
    }

    // =======================================================================
    // TunerView
    // =======================================================================
    void TunerView::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
        if (rows.empty())
        {
            g.setColour (theme::textDim);
            g.drawText ("Tracking is off. Choose a mode above.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const float rowH = std::min (60.0f, getHeight() / (float) rows.size());
        auto area = getLocalBounds().toFloat().reduced (6.0f, 4.0f);

        for (const auto& row : rows)
        {
            auto r = area.removeFromTop (rowH).reduced (0, 2);
            const bool big = rows.size() == 1;

            // Name
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (row.name, r.removeFromLeft (120), juce::Justification::centredLeft);

            // Heard note + cents
            const bool heard = row.hz > 0 && row.clarity > 0.8f;
            const double midi = heard ? hzToMidi (row.hz) - centsCorrection / 100.0 : 0.0;
            const int nearest = heard ? (int) std::lround (midi) : -1;
            const double cents = heard ? (midi - nearest) * 100.0 : 0.0;

            auto noteArea = r.removeFromLeft (big ? 110.0f : 70.0f);
            g.setColour (heard ? theme::text : theme::textDim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (big ? 30.0f : 18.0f, juce::Font::bold));
            g.drawText (heard ? juce::String (midiNoteName (nearest)) : juce::String ("-"), noteArea, juce::Justification::centred);

            // Cents bar (-50 .. +50)
            auto centsArea = r.removeFromLeft (r.getWidth() * 0.55f).reduced (8, rowH * 0.25f);
            g.setColour (theme::panelLight);
            g.fillRoundedRectangle (centsArea, 3.0f);
            g.setColour (theme::textDim);
            g.fillRect (centsArea.getCentreX() - 0.5f, centsArea.getY(), 1.0f, centsArea.getHeight());
            if (heard)
            {
                const float x = centsArea.getCentreX() + (float) (cents / 50.0) * centsArea.getWidth() * 0.5f;
                g.setColour (std::abs (cents) < 8 ? theme::good : (std::abs (cents) < 25 ? theme::accent : theme::bad));
                g.fillRoundedRectangle (x - 3, centsArea.getY() - 3, 6, centsArea.getHeight() + 6, 2.0f);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ((cents >= 0 ? "+" : "") + juce::String ((int) std::lround (cents)) + " c",
                            centsArea.withX (centsArea.getRight() + 4).withWidth (44), juce::Justification::centredLeft);
            }

            // Expected note (calibration)
            if (row.expected >= 0)
            {
                g.setColour (theme::accent);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("want " + juce::String (midiNoteName (row.expected)), r.removeFromRight (80), juce::Justification::centredRight);
            }

            // Level meter (dB), with the gate line
            auto meter = r.reduced (50, rowH * 0.3f).withTrimmedLeft (0);
            auto toX = [&meter] (float level)
            {
                const float db = juce::Decibels::gainToDecibels (level, -70.0f);
                return meter.getX() + juce::jmap (db, -70.0f, 0.0f, 0.0f, meter.getWidth());
            };
            g.setColour (theme::panelLight);
            g.fillRoundedRectangle (meter, 2.0f);
            g.setColour (row.rms > gate ? theme::sounding : theme::textDim);
            g.fillRoundedRectangle (meter.withRight (toX (row.rms)), 2.0f);
            g.setColour (theme::accent);
            g.fillRect (toX (gate) - 0.5f, meter.getY() - 3, 1.5f, meter.getHeight() + 6);
        }
    }

    juce::String TunerView::getTooltip()
    {
        return "Live input view\n"
               "Left: the note the tracker hears. Middle bar: how far it is from that note in cents "
               "(100 cents = one half step). Green = in tune, yellow = a little off, red = far off.\n"
               "Right bar: input level. The yellow line is the level a note must reach to start "
               "(set by calibration and the sensitivity knob).\n"
               "In hex mode there is one row per string, so you can check every pickup channel.";
    }

    // =======================================================================
    // CalibrationWizard
    // =======================================================================
    CalibrationWizard::CalibrationWizard (GuitarLabProcessor& p) : proc (p)
    {
        title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::accent);
        body.setJustificationType (juce::Justification::topLeft);
        body.setFont (juce::FontOptions (15.0f));
        instruction.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        instruction.setJustificationType (juce::Justification::centred);
        status.setFont (juce::FontOptions (15.0f));
        status.setJustificationType (juce::Justification::centred);
        stepLabel.setColour (juce::Label::textColourId, theme::textDim);
        stepLabel.setJustificationType (juce::Justification::centred);

        for (auto* c : { &title, &body, &instruction, &status, &stepLabel })
            addAndMakeVisible (c);

        startButton.setTooltip ("Quick calibration (1-2 minutes)\nPress this when the tuning shown above matches your guitar.\n"
                                "Learns: noise level, how each open string is really tuned, the 12th fret on the lowest and "
                                "highest string, and your soft and hard picking. For each step: press Listen (or Space), "
                                "play what is shown, see OK, then the next step appears.");
        startButton.onClick = [this] { startListening (false); };

        fullButton.setTooltip ("Full calibration (15-20 minutes, best results)\nEverything from the quick calibration, plus:\n"
            "  - EVERY fret on EVERY string (0 to 24, or until you press 'My guitar ends here'). This teaches the real pitch "
            "of each fret, how loud each spot is, and the tone colour of each string. In normal-guitar mode the tone colour "
            "helps the tracker guess WHICH string you played, and poly mode uses it as the fingerprint of every note. "
            "In hex mode it learns how much each string leaks into the other inputs.\n"
            "  - (Normal guitar) about 37 chords, strummed 3 times each: open chords, 7th, maj7 and sus chords, barre chords "
            "up the neck and power chords. Mono mode can then recognise them; poly mode uses them to fill in octave notes that "
            "hide inside other strings' sound.\n"
            "  - Soft and hard picking on EVERY string, so velocity fits each string (thin strings are often louder).\n"
            "You can stop a step with Skip at any time. Save the result as a preset afterwards (Saved calibrations).");
        fullButton.onClick = [this] { startListening (true); };

        endFretsButton.setTooltip ("My guitar ends here\nPress this when the fret asked for does not exist on your guitar "
                                   "(for example fret 23 on a 22-fret guitar). The tracker will never guess frets past this one, "
                                   "on any string.");
        endFretsButton.onClick = [this] { session.noMoreFrets(); tick(); };

        cancelButton.setTooltip ("Cancel\nStops calibration. Your old calibration (if any) is kept.");
        cancelButton.onClick = [this] { finish (false); };

        listenButton.setTooltip ("Listen (Space bar or Enter)\nPress it, then play what is shown above. Guitar Lab listens only "
                                 "after this press, and only to a NEW note: a string that still rings from before does not count. "
                                 "When it heard the right thing it says OK and shows the next step; press Listen again for it.\n"
                                 "While listening, press it again to stop. After a problem it says 'Again': listen once more.\n"
                                 "In a DAW the Space bar may start the DAW instead: then click this button.");
        listenButton.onClick = [this]
        {
            if (session.isListening()) { session.stop(); autoPausedStep = session.stepNumber(); }
            else                       { session.listen(); autoPausedStep = -1; }
            tick();
        };

        autoToggle.setTooltip ("Auto listen\nPresses Listen for you. When a step is ready, Guitar Lab waits the time on the "
                               "right, then starts listening by itself. So both hands can stay on the guitar for the whole "
                               "calibration: play, wait for OK, wait for the next step, play.\n"
                               "It never decides for you: after a problem ('Again' / 'Use it anyway') you still choose. "
                               "'Stop listening' pauses it until the next step.");
        autoToggle.setWantsKeyboardFocus (false);
        autoDelay.setSliderStyle (juce::Slider::LinearHorizontal);
        autoDelay.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        autoDelay.setRange (0.5, 10.0, 0.5);
        autoDelay.setValue (2.0, juce::dontSendNotification);
        autoDelay.setTextValueSuffix (" s");
        autoDelay.setWantsKeyboardFocus (false);
        autoDelay.setTooltip ("Auto listen delay\nHow long Guitar Lab waits after a step is ready before it starts "
                              "listening by itself (with 'Auto listen' on). Give yourself time to move your hand to the "
                              "next fret or chord: 1-2 s for the frets, 3-4 s for the chords.");
        cuesToggle.setTooltip ("Sound cues\nShort sounds, so you do not have to look at the screen:\n"
                               "  - a short tick when Guitar Lab starts listening: play now,\n"
                               "  - a rising 'ding-ding' when the step is OK,\n"
                               "  - a low tone when something is wrong (wrong fret, nothing heard, or you need to choose).\n"
                               "They go to Guitar Lab's audio output only, never to MIDI. Best together with Auto listen.");
        cuesToggle.setWantsKeyboardFocus (false);
        cuesToggle.setToggleState (true, juce::dontSendNotification);
        memory.remember (autoToggle, "autoListen");
        memory.remember (autoDelay, "autoDelay");
        memory.remember (cuesToggle, "soundCues");

        backButton.setTooltip ("Back\nGo back one step, for example when you played the wrong thing and it was still accepted. "
                               "The result of that step is forgotten; press Listen and play it again. Press Back more times "
                               "to go further back.");
        backButton.onClick = [this]
        {
            session.back();
            if (page == Page::Done) showPage (Page::Running);
            tick();
        };

        acceptButton.setTooltip ("Use it anyway\nKeep this measurement even though it looked wrong. "
                                 "Use it when your string is tuned like this on purpose (for example a slightly flat guitar). "
                                 "The tracker will then read that string correctly.");
        acceptButton.onClick = [this] { session.accept(); tick(); };

        skipButton.setTooltip ("Skip\nSkip this step. Safe defaults are used for it.");
        skipButton.onClick = [this] { session.skip(); if (session.finished()) showPage (Page::Done); tick(); };

        saveButton.setTooltip ("Save calibration\nUse these results for tracking from now on. They are saved with your project.");
        saveButton.onClick = [this] { finish (true); };

        discardButton.setTooltip ("Discard\nThrow these results away and keep the previous calibration.");
        discardButton.onClick = [this] { finish (false); };

        for (auto* b : { &startButton, &fullButton, &endFretsButton, &cancelButton, &listenButton, &backButton, &acceptButton,
                         &skipButton, &saveButton, &discardButton })
            addChildComponent (b);
        addChildComponent (autoToggle);
        addChildComponent (autoDelay);
        addChildComponent (cuesToggle);
        setWantsKeyboardFocus (true);
    }

    bool CalibrationWizard::keyPressed (const juce::KeyPress& k)
    {
        if (page == Page::Running && (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey))
        {
            listenButton.triggerClick();
            return true;
        }
        return false;
    }

    std::vector<FretPos> CalibrationWizard::spotsToPlay() const
    {
        std::vector<FretPos> spots;
        if (page != Page::Running) return spots;

        if (auto* ch = session.currentChord())
        {
            for (int s = 0; s < (int) ch->frets.size(); ++s)
                if (ch->frets[(size_t) s] >= 0) spots.push_back ({ s, ch->frets[(size_t) s] });
        }
        else if (session.currentString() >= 0 && session.expectedFret() >= 0)
        {
            spots.push_back ({ session.currentString(), session.expectedFret() });
        }
        return spots;
    }

    int CalibrationWizard::displayKey() const
    {
        if (page != Page::Running) return -1;
        return session.stepNumber() * 100 + std::max (0, session.expectedFret());
    }

    void CalibrationWizard::begin()
    {
        proc.work.calOpen = true;
        showPage (Page::Tuning);
    }

    void CalibrationWizard::resume()
    {
        page = (Page) juce::jlimit (0, 2, proc.work.calPage);
        if (page != Page::Tuning)
        {
            startTracker();
            session.stop(); // the sound was not heard while the window was closed
        }
        showPage (page);
    }

    void CalibrationWizard::startTracker()
    {
        // Calibrate from a clean state: default channel order, no gain or cents corrections.
        auto backup = proc.calibration;
        proc.calibration = Calibration {};
        proc.calibrating = true;
        proc.rebuildTracker();
        proc.calibration = backup; // restored now; the tracker already runs with the clean copy
    }

    void CalibrationWizard::showPage (Page p)
    {
        page = p;
        proc.work.calPage = (int) p;
        const bool tuningPage = p == Page::Tuning, running = p == Page::Running, done = p == Page::Done;

        startButton.setVisible (tuningPage);
        fullButton.setVisible (tuningPage);
        endFretsButton.setVisible (false); // shown by tick() during "every note"
        cancelButton.setVisible (! done);
        listenButton.setVisible (running);
        backButton.setVisible (running || done);
        acceptButton.setVisible (false);   // shown by tick() when there is a decision to make
        skipButton.setVisible (running);
        saveButton.setVisible (done);
        discardButton.setVisible (done);
        autoToggle.setVisible (! done);
        autoDelay.setVisible (! done);
        cuesToggle.setVisible (! done);
        body.setVisible (! running);
        instruction.setVisible (running);
        status.setVisible (running);
        stepLabel.setVisible (running);

        tick();
        resized();
        repaint();
        if (running && isShowing()) grabKeyboardFocus(); // for the Space bar
    }

    void CalibrationWizard::startListening (bool full)
    {
        if (trackMode (proc) == TrackMode::Off)
            return;

        startTracker();
        session.start (proc.instrument, hexMode (proc), std::max (1, proc.numGuitarInputs()), full);
        shownProgress = 0.0;
        showPage (Page::Running);
    }

    void CalibrationWizard::feed (const AnalysisFrame& f, double hopSeconds)
    {
        if (page != Page::Running) return;
        session.feed (f, hopSeconds);
        if (session.finished())
            showPage (Page::Done);
    }

    void CalibrationWizard::finish (bool save)
    {
        if (save && session.finished())
            proc.calibration = session.result();

        proc.calibrating = false;
        proc.rebuildTracker();
        page = Page::Tuning;
        proc.work.calPage = 0;
        proc.work.calOpen = false;
        if (onClose) onClose();
    }

    juce::String CalibrationWizard::summary() const
    {
        const auto& c = session.result();
        juce::String s;
        s << "Calibration finished. Here is what Guitar Lab learned:\n\n";
        s << "Whole guitar: " << juce::String (std::abs (c.globalCents), 0) << " cents " << (c.globalCents >= 0 ? "sharp" : "flat")
          << " on average.\n";
        for (int str = proc.instrument.numStrings() - 1; str >= 0; --str)
        {
            const auto& sc = c.strings[(size_t) str];
            s << "  " << juce::String (midiNoteName (proc.instrument.tuning()[(size_t) str])) << ": ";
            if (! sc.measured) { s << "skipped\n"; continue; }
            s << juce::String (sc.centsOffset, 0) << " cents";
            if (std::abs (sc.intonationCents) > 0.01) s << ", 12th fret " << juce::String (sc.intonationCents, 0) << " cents";
            if (c.hexMode) s << ", on input " << (sc.inputChannel + 1) << " (gain x" << juce::String (sc.gain, 1) << ")";
            s << "\n";
        }
        s << "\nNoise level: " << juce::String (juce::Decibels::gainToDecibels (c.noiseFloor), 0) << " dB.  "
          << "Soft pick: " << juce::String (juce::Decibels::gainToDecibels (c.softPeak), 0) << " dB,  hard pick: "
          << juce::String (juce::Decibels::gainToDecibels (c.hardPeak), 0) << " dB.\n"
          << "Notes start above " << juce::String (juce::Decibels::gainToDecibels (c.gateOn()), 0) << " dB.\n";

        if (c.hasPositions())
        {
            int spots = 0, withColour = 0;
            for (const auto& str : c.positions)
                for (const auto& p : str)
                    if (p.measured) { ++spots; if (p.hasTimbre) ++withColour; }
            s << "Every note: " << spots << " spots measured (" << withColour << " with tone colour), up to fret "
              << c.lastFret[0] << ".\n";
        }
        if (! c.chords.empty())
        {
            juce::StringArray names;
            for (const auto& ch : c.chords) names.add (juce::String (ch.name));
            s << "Chords it can now recognise: " << names.joinIntoString (", ") << ".\n";
        }

        s << "\nPress 'Save calibration' to use it.";
        return s;
    }

    void CalibrationWizard::playCues()
    {
        using Phase = CalibrationSession::Phase;
        if (page == Page::Tuning) { lastCuePhase = lastCueStep = -1; return; }

        const int phase = (int) session.phase(), step = session.stepNumber();
        if (lastCuePhase >= 0 && cuesToggle.getToggleState())
        {
            const bool wasListening = lastCuePhase == (int) Phase::Listening;
            if (phase == (int) Phase::Listening && ! wasListening)                         proc.playCue (GuitarLabProcessor::Cue::Start);
            else if (wasListening && (step != lastCueStep || phase == (int) Phase::Done))  proc.playCue (GuitarLabProcessor::Cue::Ok);
            else if (wasListening && phase == (int) Phase::Decide)                        proc.playCue (GuitarLabProcessor::Cue::Problem);
            else if (wasListening && phase == (int) Phase::Ready && ! session.statusIsGood()) proc.playCue (GuitarLabProcessor::Cue::Problem);
        }
        lastCuePhase = phase;
        lastCueStep = step;
    }

    void CalibrationWizard::tick()
    {
        playCues();
        switch (page)
        {
            case Page::Tuning:
            {
                title.setText ("Calibration - step 1: your tuning", juce::dontSendNotification);
                const auto mode = trackMode (proc);
                const int inputs = proc.numGuitarInputs();

                juce::String text;
                text << "Guitar Lab will listen for this tuning (lowest string first):\n\n"
                     << "      " << tuningLowToHigh (proc.instrument) << "\n\n"
                     << "If your guitar is tuned differently, change 'Strings' and 'Tuning' in the top bar now. "
                        "This page updates by itself.\n\n";

                if (mode == TrackMode::Off)
                    text << "First choose a tracking mode (Normal guitar or Hex pickup) on the Guitar -> MIDI tab.\n";
                else if (mode == TrackMode::Hex)
                    text << "Mode: hex pickup. Inputs found: " << inputs << " (you need one per string: "
                         << proc.instrument.numStrings() << ").\n";
                else
                    text << "Mode: normal guitar. Inputs found: " << inputs << ".\n";

                text << "\nQUICK (1-2 min): stay quiet for 3 seconds, play each open string, play the 12th fret on the "
                        "lowest and highest string, then pick softly 4 times and hard 4 times.\n\n"
                        "FULL (15-20 min, best results): stay quiet, then play EVERY fret on EVERY string, one after another"
                     << (mode == TrackMode::Hex ? "" : ", then strum about 37 chords 3 times each (open, 7th, sus, barre and power chords)")
                     << ", then soft and hard picking on every string. The fretboard shows what to play next.\n\n"
                        "For EVERY step: press Listen (or the Space bar), play what is shown, and wait for OK. "
                        "Then the next step appears. Played something wrong? Press Back and play it again.";

                body.setText (text, juce::dontSendNotification);
                startButton.setEnabled (mode != TrackMode::Off && inputs > 0);
                fullButton.setEnabled (startButton.isEnabled());
                break;
            }

            case Page::Running:
            {
                using Phase = CalibrationSession::Phase;

                // Auto listen: press Listen by itself a moment after a step is ready.
                juce::String waiting ("   -   press Listen (or Space), then play");
                if (session.phase() == Phase::Ready && autoToggle.getToggleState() && autoPausedStep != session.stepNumber())
                {
                    const auto now = juce::Time::getMillisecondCounter();
                    if (readySince == 0) readySince = now;
                    const double left = autoDelay.getValue() - (now - readySince) / 1000.0;
                    if (left <= 0.0) session.listen();
                    else waiting = "   -   listening starts by itself in " + juce::String (left, 1) + " s";
                }
                if (session.phase() != Phase::Ready) readySince = 0;

                const auto phase = session.phase();
                title.setText ("Calibration", juce::dontSendNotification);
                stepLabel.setText ("Step " + juce::String (session.stepNumber()) + " of " + juce::String (session.totalSteps())
                                   + (phase == Phase::Listening ? juce::String ("   -   listening: play now") : waiting),
                                   juce::dontSendNotification);
                instruction.setText (juce::String (session.instruction()), juce::dontSendNotification);
                status.setText (juce::String (session.status()), juce::dontSendNotification);
                status.setColour (juce::Label::textColourId, phase == Phase::Listening ? theme::accent
                                                           : (session.statusIsGood() ? theme::good : theme::bad));
                listenButton.setButtonText (phase == Phase::Listening ? "Stop listening" : (phase == Phase::Decide ? "Again" : "Listen"));
                backButton.setEnabled (session.canGoBack());
                const bool buttonsChanged = endFretsButton.isVisible() != session.canEndFrets()
                                         || acceptButton.isVisible() != (phase == Phase::Decide);
                endFretsButton.setVisible (session.canEndFrets());
                acceptButton.setVisible (phase == Phase::Decide);
                if (buttonsChanged) resized();
                if (session.finished())
                {
                    showPage (Page::Done);
                    break;
                }
                if (std::abs (shownProgress - session.stepProgress()) > 0.005)
                {
                    shownProgress = session.stepProgress();
                    repaint();
                }
                break;
            }

            case Page::Done:
                title.setText ("Calibration - done", juce::dontSendNotification);
                body.setText (summary(), juce::dontSendNotification);
                break;
        }
    }

    void CalibrationWizard::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background.withAlpha (0.97f));
        g.setColour (theme::accent.withAlpha (0.5f));
        g.drawRect (getLocalBounds(), 1);

        if (page == Page::Running)
        {
            auto bar = getLocalBounds().reduced (40, 0).withHeight (10).withY (status.getBottom() + 8).toFloat();
            g.setColour (theme::panelLight);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (theme::sounding);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, shownProgress)), 4.0f);
        }
    }

    void CalibrationWizard::resized()
    {
        auto r = getLocalBounds().reduced (14);
        auto top = r.removeFromTop (30);
        autoDelay.setBounds (top.removeFromRight (170).reduced (0, 4));
        autoToggle.setBounds (top.removeFromRight (110));
        cuesToggle.setBounds (top.removeFromRight (110));
        title.setBounds (top);

        auto buttons = r.removeFromBottom (30);
        for (auto* b : { &startButton, &fullButton, &listenButton, &acceptButton, &backButton, &skipButton, &endFretsButton,
                         &saveButton, &discardButton, &cancelButton })
            if (b->isVisible())
            {
                b->setBounds (buttons.removeFromLeft (b == &startButton || b == &fullButton || b == &endFretsButton ? 210
                                                      : (b == &listenButton ? 160 : 120)));
                buttons.removeFromLeft (8);
            }

        r.removeFromBottom (8);
        if (page == Page::Running)
        {
            stepLabel.setBounds (r.removeFromTop (22));
            instruction.setBounds (r.removeFromTop (r.getHeight() / 2));
            status.setBounds (r.removeFromTop (44));
        }
        else
        {
            body.setBounds (r);
        }
    }

    // =======================================================================
    // TrackerPanel
    // =======================================================================
    TrackerPanel::TrackerPanel (GuitarLabProcessor& p) : proc (p), wizard (p)
    {
        auto label = [this] (juce::Label& l, const juce::String& text, const juce::String& tip)
        {
            l.setText (text, juce::dontSendNotification);
            l.setTooltip (tip);
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        };

        label (modeLabel, "Tracking", "Guitar tracking mode\n"
            "Off: Guitar Lab does not listen to audio.\n"
            "Normal guitar: single notes (mono): a normal guitar on one cable, one note at a time. The fastest and most "
            "exact mode for lead playing: bends, slides, vibrato, hammer-ons. Chords only work if you taught them in a "
            "Full calibration. The string is guessed from where your hand was last.\n"
            "Normal guitar: chords too (poly): a normal guitar on one cable, several notes at once: chords, double stops, "
            "ringing arpeggios, with bends per note. It explains the sound as a mix of notes, then finds a string and fret "
            "for each one. A little slower than mono (about 50-65 ms), and it can miss an octave note that hides inside "
            "another string's sound. A Full calibration makes it much better (it learns your guitar's tone).\n"
            "Hex pickup: a hexaphonic pickup (like Roland GK) with one audio channel per string. Each string is tracked on its own, "
            "so chords, per-string bends and the exact string are all correct. Needs one input per string.");
        modeBox.addItemList ({ "Off", "Normal guitar: single notes (mono)", "Hex pickup (one input per string)",
                               "Normal guitar: chords too (poly)" }, 1);
        modeBox.setTooltip (modeLabel.getTooltip());
        addAndMakeVisible (modeBox);
        modeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.params, ParamIDs::trackMode, modeBox);

        label (inputLabel, "Input", "Input (normal guitar mode)\n"
            "Which audio input your guitar is plugged into. On most audio interfaces the instrument (Hi-Z) input is Input 1.\n"
            "'Mix of all inputs' adds all inputs together.\n"
            "In hex mode this is ignored: calibration finds which input belongs to which string.");
        inputBox.addItemList ({ "Mix of all inputs", "Input 1", "Input 2" }, 1);
        inputBox.setTooltip (inputLabel.getTooltip());
        addAndMakeVisible (inputBox);
        inputAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.params, ParamIDs::trackInput, inputBox);

        label (safetyLabel, "Speed / safety", "Speed vs safety\n"
            "How many 5 ms checks the pitch must stay the same before a note is sent.\n"
            "1-2 = fastest response, but more wrong 'blip' notes on the pick attack.\n"
            "3 = good balance (default). 5-8 = slower, but very few wrong notes.\n"
            "Low notes always need a little longer, because one wave of a low note takes longer.");
        safety.setSliderStyle (juce::Slider::LinearHorizontal);
        safety.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
        safety.setTooltip (safetyLabel.getTooltip());
        addAndMakeVisible (safety);
        safetyAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::trackStability, safety);

        label (sensLabel, "Sensitivity", "Sensitivity\n"
            "Higher = soft notes and ghost notes are tracked too, but noise may start notes.\n"
            "Lower = only clear, strong notes are tracked.\n"
            "1.0 uses the level found by calibration. The yellow line in the level meters shows the result.");
        sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        sensitivity.setTooltip (sensLabel.getTooltip());
        addAndMakeVisible (sensitivity);
        sensAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::trackSensitivity, sensitivity);

        label (hystLabel, "Hysteresis", "Hysteresis (poly mode)\n"
            "Stops a single note from being heard as a chord. Every guitar note also sounds an octave, an octave + 5th, "
            "two octaves... higher (its overtones). With this turned up, a note on one of those spots must be clearly "
            "stronger before it starts, but only right after you picked the louder note below it.\n"
            "0 = off (the normal behaviour). 1 = strongest: about a third fewer wrong extra notes, but a quiet real "
            "octave strummed together with its lower note may be missed.\n"
            "Arpeggios over ringing strings are not affected, and it never adds delay. Mono and hex modes ignore it.");
        hysteresis.setSliderStyle (juce::Slider::LinearHorizontal);
        hysteresis.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        hysteresis.setTooltip (hystLabel.getTooltip());
        addAndMakeVisible (hysteresis);
        hystAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::trackHysteresis, hysteresis);

        bendsToggle.setTooltip ("Track bends, slides and vibrato\n"
            "On: small pitch changes while a note rings are sent as pitch bend on that string's channel "
            "(within the pitch bend range from MIDI settings). Bigger jumps start a new legato note.\n"
            "Off: every pitch change of more than about half a semitone starts a new note. "
            "Use Off for synths or instruments that do not handle pitch bend well.");
        addAndMakeVisible (bendsToggle);
        bendsAtt = std::make_unique<APVTS::ButtonAttachment> (proc.params, ParamIDs::trackBends, bendsToggle);

        monitorToggle.setTooltip ("Hear guitar\n"
            "Also sends your real guitar sound to Guitar Lab's audio output, so you can hear it next to the MIDI sound.\n"
            "Leave it off if you already hear the guitar another way (amp, interface direct monitoring), "
            "or you will hear it twice. Beware of feedback with speakers.");
        addAndMakeVisible (monitorToggle);
        monitorAtt = std::make_unique<APVTS::ButtonAttachment> (proc.params, ParamIDs::trackMonitor, monitorToggle);

        soundToggle.setTooltip ("Built-in sound\n"
            "On: Guitar Lab plays its own plucked-string sound for the tracked notes (and the phrase), "
            "so you can practise without loading another instrument.\n"
            "Off: Guitar Lab is silent and only sends MIDI. Use Off when the MIDI plays another instrument "
            "in your DAW, so you do not hear both.\n"
            "The MIDI output is the same either way. Same switch as 'Built-in sound' in MIDI / Sound settings.");
        addAndMakeVisible (soundToggle);
        soundAtt = std::make_unique<APVTS::ButtonAttachment> (proc.params, ParamIDs::synthOn, soundToggle);

        calibrateButton.setTooltip ("Calibrate\n"
            "Teaches the tracker YOUR guitar. First it asks for your tuning, then it listens while you:\n"
            "  1. stay quiet (to learn the noise level),\n"
            "  2. play every open string (to learn how each string is really tuned, and in hex mode which input it is on),\n"
            "  3. play the 12th fret on the lowest and highest string (to check intonation and high notes),\n"
            "  4. pick softly and hard (to map your picking strength to MIDI velocity).\n"
            "FULL calibration also asks for every fret on every string, (normal guitar) about 37 chords, and soft and hard "
            "picking on every string. That lets the tracker guess the string from its sound, measure every fret's real pitch, "
            "recognise the chords you taught it, and give poly mode the real sound of your guitar.\n"
            "Calibrate again after changing strings, tuning, pickup or audio interface. Save each guitar as a preset "
            "(Saved calibrations) so you only do it once.");
        calibrateButton.onClick = [this]
        {
            wizard.begin();
            wizard.setVisible (true);
            wizard.toFront (true);
        };
        addAndMakeVisible (calibrateButton);

        intonationButton.setTooltip ("Intonation report\nShows, from your Full calibration, how in tune every fret of your guitar is "
                                     "compared with its open string, and what to adjust (the bridge saddles) if the 12th fret is sharp or flat.");
        intonationButton.onClick = [this] { intonation.show (proc.instrument, proc.calibration); };
        addAndMakeVisible (intonationButton);
        addChildComponent (intonation);

        // --- Calibration presets ---
        presetLabel.setText ("Saved calibrations", juce::dontSendNotification);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        const juce::String presetTip ("Saved calibrations (presets)\n"
            "Pick one to load it. Use this when you switch guitars or tunings: calibrate each guitar once, save it, "
            "and load it again later instead of calibrating again.\n"
            "Loading replaces the calibration in use, and also sets Strings and Tuning in the top bar to the tuning "
            "the calibration was made for.\n"
            "'Default (no calibration)' goes back to the plain settings, as if you never calibrated.\n"
            "Presets are shared by all your projects and by the standalone app. They are saved as small text files in:\n"
            + presetFolder().getFullPathName());
        presetLabel.setTooltip (presetTip);
        presetBox.setTooltip (presetTip);
        presetBox.setTextWhenNothingSelected ("Choose a saved calibration...");
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id == kDefaultPresetId) loadDefaultCalibration();
            else if (id - 2 >= 0 && id - 2 < presetFiles.size()) loadPreset (presetFiles[id - 2]);
        };
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);

        presetName.setTextToShowWhenEmpty ("Name, e.g. Strat Drop D", theme::textDim);
        presetName.setTooltip ("Preset name\nA name for the calibration you are using now, for example the guitar and its tuning. "
                               "Then press 'Save as preset'. A preset with the same name is replaced.");
        presetName.onReturnKey = [this] { savePreset(); };
        addAndMakeVisible (presetName);

        savePresetButton.setTooltip ("Save as preset\nSaves the calibration you are using now under the name on the left, "
                                     "so you can load it again in any project. Calibrate first.");
        savePresetButton.onClick = [this] { savePreset(); };
        addAndMakeVisible (savePresetButton);

        deletePresetButton.setTooltip ("Delete preset\nDeletes the saved calibration chosen on the left (the file, not the "
                                       "calibration in use). Click twice to be sure. This cannot be undone.");
        deletePresetButton.onClick = [this] { deletePreset(); };
        addAndMakeVisible (deletePresetButton);
        refreshPresets();

        calStatus.setColour (juce::Label::textColourId, theme::textDim);
        calStatus.setTooltip ("Calibration status\nShows whether the tracker is calibrated, and for which tuning.");
        addAndMakeVisible (calStatus);

        warning.setColour (juce::Label::textColourId, theme::bad);
        addAndMakeVisible (warning);

        help.setColour (juce::Label::textColourId, theme::textDim);
        help.setText ("Tracked notes go to the MIDI output (one channel per string) and show in green. "
                      "To write them down as editable tab, use 'Record guitar' in the Play & Write tab.",
                      juce::dontSendNotification);
        help.setTooltip ("Using it in a DAW\n"
                         "Guitar Lab is an audio effect. Put it on the AUDIO track your guitar is plugged into.\n"
                         "To play another instrument with it, make a MIDI track, set its 'MIDI From' to the guitar track "
                         "and choose 'Guitar Lab' as the source (Ableton: the second box under MIDI From).\n"
                         "For bends per string, turn on MPE for the instrument that receives the MIDI.");
        addAndMakeVisible (help);

        addAndMakeVisible (tuner);

        wizard.onClose = [this] { wizard.setVisible (false); updateStatus(); };
        addChildComponent (wizard);
        if (proc.work.calOpen)
        {
            wizard.setVisible (true);
            wizard.resume();
        }

        refresh();
    }

    TrackerPanel::~TrackerPanel()
    {
        // The window closes: normal tracking goes on. The wizard keeps its place (proc.work) and waits for Listen.
        if (proc.calibrating.load())
        {
            proc.calibrating = false;
            proc.rebuildTracker();
        }
    }

    juce::File TrackerPanel::presetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Guitar Lab").getChildFile ("Calibrations");
    }

    void TrackerPanel::showPresetMessage (const juce::String& text)
    {
        presetMessage = text;
        presetMessageAt = juce::Time::getMillisecondCounter();
        updateStatus();
    }

    void TrackerPanel::refreshPresets()
    {
        presetFiles = presetFolder().findChildFiles (juce::File::findFiles, false, "*.glcal");
        presetFiles.sort();
        presetBox.clear (juce::dontSendNotification);
        presetBox.addItem ("Default (no calibration)", kDefaultPresetId);
        if (! presetFiles.isEmpty()) presetBox.addSeparator();
        for (int i = 0; i < presetFiles.size(); ++i)
            presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), i + 2);
        deleteArmedAt = 0;
        deletePresetButton.setButtonText ("Delete");
    }

    void TrackerPanel::loadPreset (const juce::File& file)
    {
        Calibration c;
        c.deserialise (file.loadFileAsString().toStdString());
        if (! c.valid)
        {
            showPresetMessage ("Could not read the preset '" + file.getFileNameWithoutExtension() + "'.");
            return;
        }

        proc.calibration = c;
        // A calibration belongs to the tuning it was made for: switch to it.
        if (! c.tuning.empty() && c.tuning != proc.instrument.tuning() && onTuningWanted)
            onTuningWanted (c.tuning);
        proc.rebuildTracker();
        presetName.setText (file.getFileNameWithoutExtension(), juce::dontSendNotification);
        showPresetMessage ("Loaded: " + file.getFileNameWithoutExtension());
    }

    void TrackerPanel::loadDefaultCalibration()
    {
        proc.calibration = Calibration {};
        proc.rebuildTracker();
        presetName.clear();
        showPresetMessage ("Back to the default: no calibration in use.");
    }

    void TrackerPanel::savePreset()
    {
        const auto name = juce::File::createLegalFileName (presetName.getText().trim());
        if (! proc.calibration.valid)
        {
            showPresetMessage ("Nothing to save yet: calibrate first (Calibrate...).");
            return;
        }
        if (name.isEmpty())
        {
            showPresetMessage ("Type a name for the preset first (box left of 'Save as preset').");
            return;
        }

        const auto file = presetFolder().getChildFile (name + ".glcal");
        if (! presetFolder().createDirectory() || ! file.replaceWithText (proc.calibration.serialise()))
        {
            showPresetMessage ("Could not save the preset to " + file.getFullPathName());
            return;
        }
        refreshPresets();
        presetBox.setSelectedId (presetFiles.indexOf (file) + 2, juce::dontSendNotification);
        showPresetMessage ("Saved: " + name);
    }

    void TrackerPanel::deletePreset()
    {
        const int i = presetBox.getSelectedId() - 2;
        if (i < 0 || i >= presetFiles.size()) return;

        // First click arms the button, a second click within 3 seconds deletes.
        const auto now = juce::Time::getMillisecondCounter();
        if (deleteArmedAt == 0 || now - deleteArmedAt > 3000)
        {
            deleteArmedAt = now;
            deletePresetButton.setButtonText ("Sure?");
            return;
        }
        presetFiles[i].deleteFile();
        refreshPresets();
    }

    void TrackerPanel::refresh()
    {
        updateStatus();
        resized();
    }

    void TrackerPanel::updateStatus()
    {
        const auto& cal = proc.calibration;
        if (! cal.valid)
            calStatus.setText ("Not calibrated yet. Press 'Calibrate...' for much better tracking.", juce::dontSendNotification);
        else
        {
            juce::String text = "Calibrated (" + juce::String (cal.hexMode ? "hex pickup" : "normal guitar") + "). Guitar is "
                              + juce::String (std::abs (cal.globalCents), 0) + " cents " + (cal.globalCents >= 0 ? "sharp" : "flat") + " on average.";
            if (cal.tuning != proc.instrument.tuning())
                text << "  The tuning changed since then: please calibrate again.";
            calStatus.setText (text, juce::dontSendNotification);
        }

        const auto mode = trackMode (proc);
        const int inputs = proc.numGuitarInputs();
        juce::String warn;
        if (mode != TrackMode::Off && inputs == 0)
            warn = "No audio input. In the standalone app: Options > Audio/MIDI Settings > enable an input. "
                   "In a DAW: put Guitar Lab on an audio track and set the track input to your guitar.";
        else if (mode == TrackMode::Hex && inputs < proc.instrument.numStrings())
            warn = "Hex mode needs " + juce::String (proc.instrument.numStrings()) + " inputs (one per string), but only "
                 + juce::String (inputs) + " are active. Enable more input channels in your audio settings or DAW track.";
        else if ((mode == TrackMode::Mono || mode == TrackMode::Poly) && inputBox.getSelectedItemIndex() > inputs)
            warn = "The chosen input does not exist on this audio device.";
        else if (mode != TrackMode::Off && cal.valid && cal.hexMode != (mode == TrackMode::Hex))
            warn = "The calibration was made for the other mode. Please calibrate again.";

        // Messages from the preset buttons stay for a few seconds.
        const auto now = juce::Time::getMillisecondCounter();
        if (presetMessage.isNotEmpty() && now - presetMessageAt < 5000)
            warn = presetMessage;
        if (deleteArmedAt != 0 && now - deleteArmedAt > 3000)
        {
            deleteArmedAt = 0;
            deletePresetButton.setButtonText ("Delete");
        }
        warning.setText (warn, juce::dontSendNotification);
    }

    void TrackerPanel::timerTick()
    {
        // Drain analysis frames from the audio thread.
        std::array<bool, kMaxStrings> fresh {};
        const int n = proc.popFrames (frameBuffer.data(), (int) frameBuffer.size());
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frameBuffer[(size_t) i];
            if (f.channel >= 0 && f.channel < kMaxStrings)
            {
                last[(size_t) f.channel] = { f.hz, f.clarity, f.rms };
                fresh[(size_t) f.channel] = true;
            }
            wizard.feed (f, kHopSeconds);
        }
        for (int c = 0; c < kMaxStrings; ++c)
            if (! fresh[(size_t) c]) last[(size_t) c].rms *= 0.8f;

        if (wizard.isVisible())
            wizard.tick();

        // Tuner rows
        const auto mode = trackMode (proc);
        const auto& cal = proc.calibration;
        tuner.rows.clear();
        tuner.gate = cal.gateOn() / std::max (0.25f, proc.params.getRawParameterValue (ParamIDs::trackSensitivity)->load());
        tuner.centsCorrection = cal.valid ? cal.globalCents : 0.0;

        if (mode == TrackMode::Mono || mode == TrackMode::Poly)
        {
            TunerView::Row row { "Guitar", last[0].hz, last[0].clarity, last[0].rms, -1 };
            if (wizard.isListening()) row.expected = wizard.expectedNote();
            tuner.rows.push_back (row);
        }
        else if (mode == TrackMode::Hex)
        {
            for (int s = 0; s < proc.instrument.numStrings(); ++s)
            {
                // While calibrating, inputs are in their default order (input s = string s).
                const int ch = (! wizard.isListening() && cal.valid && cal.strings[(size_t) s].inputChannel >= 0)
                             ? cal.strings[(size_t) s].inputChannel : s;
                const auto& l = last[(size_t) std::clamp (ch, 0, kMaxStrings - 1)];
                TunerView::Row row { "String " + juce::String (s + 1) + " (" + juce::String (midiNoteName (proc.instrument.tuning()[(size_t) s]))
                                     + ") in " + juce::String (ch + 1), l.hz, l.clarity, l.rms, -1 };
                if (wizard.isListening() && wizard.expectedString() == s) row.expected = wizard.expectedNote();
                tuner.rows.push_back (row);
            }
        }
        tuner.repaint();

        if (++slowTick % 15 == 0) updateStatus(); // twice a second is enough

        // Calibration moved to another fret or chord: show it on the fretboard.
        const int key = wizard.isVisible() ? wizard.displayKey() : -1;
        if (key != lastDisplayKey)
        {
            lastDisplayKey = key;
            if (onDisplayChanged) onDisplayChanged();
        }
    }

    std::vector<FretboardComponent::Mark> TrackerPanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        if (! wizard.isVisible()) return m;
        for (auto p : wizard.spotsToPlay())
            m.push_back ({ p, theme::accent, {}, true });
        return m;
    }

    void TrackerPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
    }

    void TrackerPanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (4); return x; };
        auto put = [] (juce::Rectangle<int>& x, juce::Component& c, int w) { c.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4); };

        auto a = row();
        put (a, modeLabel, 60);    put (a, modeBox, 260);
        put (a, inputLabel, 40);   put (a, inputBox, 150);
        put (a, safetyLabel, 96);  put (a, safety, 150);
        put (a, sensLabel, 74);    put (a, sensitivity, 170);

        auto b = row();
        put (b, bendsToggle, 230);
        put (b, monitorToggle, 110);
        put (b, soundToggle, 130);
        put (b, hystLabel, 74);     put (b, hysteresis, 150);
        put (b, calibrateButton, 110);
        put (b, intonationButton, 100);
        calStatus.setBounds (b);

        auto c = row();
        put (c, presetLabel, 130);       put (c, presetBox, 260);
        put (c, deletePresetButton, 70); c.removeFromLeft (16);
        put (c, presetName, 200);        put (c, savePresetButton, 120);

        warning.setBounds (row (20));
        help.setBounds (row (20));
        tuner.setBounds (r);
        wizard.setBounds (getLocalBounds().reduced (20, 10));
        intonation.setBounds (getLocalBounds().reduced (10));
    }
}
