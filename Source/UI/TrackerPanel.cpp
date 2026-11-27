#include "UI/TrackerPanel.h"

#include "Theory/ChordFinder.h"
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
    // NoteLogView
    // =======================================================================
    void NoteLogView::noteOn (juce::int64 sample, int note, int string, int fret, int velocity, juce::int64 pick)
    {
        Entry e { sample, -1, pick, note, string, fret, velocity, false };
        e.samePick = ! entries.empty() && (double) (sample - entries.back().start) < 0.04 * rate;
        if (e.samePick) entries.back().samePick = true;
        entries.push_back (e);
        if (entries.size() > 200) entries.erase (entries.begin());
        repaint();
    }

    void NoteLogView::noteOff (juce::int64 sample, int note, int string)
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            if (it->note == note && it->string == string && it->end < 0) { it->end = sample; repaint(); return; }
    }

    void NoteLogView::paint (juce::Graphics& g)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        auto r = getLocalBounds().reduced (8, 4);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (theme::textDim);
        auto header = r.removeFromTop (20);
        g.drawText ("Notes sent (newest first)", header.removeFromLeft (180), juce::Justification::centredLeft);

        // The typical delay: the middle value of the last 20 notes that had a pick.
        std::vector<double> delays;
        for (auto it = entries.rbegin(); it != entries.rend() && delays.size() < 20; ++it)
            if (it->pick >= 0) delays.push_back ((double) (it->start - it->pick) * 1000.0 / rate);
        if (! delays.empty())
        {
            std::nth_element (delays.begin(), delays.begin() + (long) delays.size() / 2, delays.end());
            g.setColour (theme::accent);
            g.drawText ("typical delay " + juce::String (juce::roundToInt (delays[delays.size() / 2])) + " ms",
                        header.withTrimmedRight (62), juce::Justification::centredLeft);
        }
        g.setFont (juce::FontOptions (13.0f));
        const int rowH = 18;
        for (auto it = entries.rbegin(); it != entries.rend() && r.getHeight() >= rowH; ++it)
        {
            auto row = r.removeFromTop (rowH);
            if (it->samePick)
            {
                g.setColour (theme::accent.withAlpha (0.12f));
                g.fillRect (row);
            }
            g.setColour (it->end < 0 ? theme::sounding : theme::text);
            g.drawText (midiNoteName (it->note), row.removeFromLeft (44), juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.drawText (it->string >= 0 ? "str " + juce::String (it->string + 1) + "  fr " + juce::String (it->fret) : juce::String ("?"),
                        row.removeFromLeft (92), juce::Justification::centredLeft);
            g.drawText ("vel " + juce::String (it->velocity), row.removeFromLeft (60), juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            const juce::String len = it->end < 0 ? juce::String ("ringing")
                                                 : juce::String ((double) (it->end - it->start) / rate, 2) + " s";
            g.drawText (len, row.removeFromLeft (64), juce::Justification::centredLeft);
            g.setColour (theme::accent);
            g.drawText (it->pick >= 0 ? "+" + juce::String (juce::roundToInt ((double) (it->start - it->pick) * 1000.0 / rate)) + " ms"
                                      : juce::String ("-"),
                        row.removeFromLeft (56), juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            g.drawText (juce::String ((double) it->start / rate, 2), row, juce::Justification::centredRight);
        }
    }

    juce::String NoteLogView::getTooltip()
    {
        return "Notes sent\nEvery note the tracker sent as MIDI, newest at the top: note, string and fret, velocity, how long "
               "it rang (green = still ringing), its delay, and when it started (seconds since the plugin started).\n"
               "Delay (yellow, +14 ms): from the pick in the guitar sound to the MIDI note, exact to the sample. It is only "
               "Guitar Lab's part: your audio interface and DAW buffers add their own delay on top. '-' = no pick was found "
               "(a hammer-on, pull-off or slide). 'Typical delay' at the top is the middle value of the last 20 picked notes.\n"
               "Notes started by the same pick (within 40 ms) have a yellow band. If you played ONE note and see a band "
               "with several notes, those are overtones taken for notes: try the Hysteresis slider.";
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

        headline.setJustificationType (juce::Justification::centred);
        headline.setColour (juce::Label::textColourId, theme::accent);
        headline.setMinimumHorizontalScale (0.5f);

        for (auto* c : { &title, &body, &headline, &instruction, &status, &stepLabel })
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

        extrasToggle.setTooltip ("Extra Poly practice (Full calibration, about 5 minutes more)\n"
            "Adds about 40 short steps at the end: octaves, fifths, octave + fifth, and two-note shapes (thirds and "
            "sixths) up the neck, each played 3 times. These are the shapes Poly mixes up most often, because one note "
            "sits on the other's overtones. Poly learns from them together with the frets and the chords.\n"
            "Only for a normal guitar (not a hex pickup). Mono does not use them.");
        addChildComponent (extrasToggle);

        endFretsButton.setTooltip ("My guitar ends here\nPress this when the fret asked for does not exist on your guitar "
                                   "(for example fret 23 on a 22-fret guitar). The tracker will never guess frets past this one, "
                                   "on any string.");
        endFretsButton.onClick = [this] { session.noMoreFrets(); tick(); };

        cancelButton.setTooltip ("Cancel\nStops calibration. Your old calibration (if any) is kept.");
        cancelButton.onClick = [this] { finish (false); };

        theme::primary (listenButton);
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
        memory.remember (extrasToggle, "polyExtras");

        backButton.setTooltip ("Back\nGo back one step, for example when you played the wrong thing and it was still accepted. "
                               "The result of that step is forgotten; press Listen and play it again. Press Back more times "
                               "to go further back.");
        backButton.onClick = [this]
        {
            if (trying()) stopTrying();
            session.back();
            proc.work.calLearning.reset();      // the result changes: learn again after the next Finish
            proc.work.calConfirmed = false;
            if (page == Page::Done) showPage (Page::Running);
            tick();
        };

        acceptButton.setTooltip ("Use it anyway\nKeep this measurement even though it looked wrong. "
                                 "Use it when your string is tuned like this on purpose (for example a slightly flat guitar). "
                                 "The tracker will then read that string correctly.");
        acceptButton.onClick = [this] { session.accept(); tick(); };

        skipButton.setTooltip ("Skip\nSkip this step. Safe defaults are used for it.");
        skipButton.onClick = [this] { session.skip(); if (session.finished()) showDone(); tick(); };

        tryButton.setTooltip ("Try it\nTurns the new calibration on right now, before you save it: play notes and chords, "
                              "and the notes Guitar Lab hears (and the chord name) show here in big letters. "
                              "MIDI is sent as normal, so you can also listen with your instrument.\n"
                              "Then press 'Save calibration' to keep it, or 'Discard' to go back to the old one.");
        tryButton.onClick = [this] { if (trying()) stopTrying(); else startTrying(); tick(); };
        tryLabel.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        tryLabel.setJustificationType (juce::Justification::centred);
        tryLabel.setColour (juce::Label::textColourId, theme::sounding);
        addChildComponent (tryLabel);

        theme::primary (saveButton);
        saveButton.setTooltip ("Save calibration\nUse these results for tracking from now on. They are saved with your project.");
        saveButton.onClick = [this] { finish (true); };

        discardButton.setTooltip ("Discard\nThrow these results away and keep the previous calibration.");
        discardButton.onClick = [this] { finish (false); };

        theme::primary (finishButton);
        finishButton.setTooltip ("Finish\nEverything was played right: show the results. After a Full calibration, Poly mode "
                                 "then learns your guitar's sound (1-2 minutes).\n"
                                 "If the last thing you played went wrong, press Back instead and play it again.");
        finishButton.onClick = [this] { proc.work.calConfirmed = true; showPage (Page::Done); };

        for (auto* b : { &startButton, &fullButton, &endFretsButton, &cancelButton, &listenButton, &backButton, &acceptButton,
                         &skipButton, &tryButton, &saveButton, &discardButton, &finishButton })
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
        if (page == Page::Done && trying())
        {
            proc.calibrating = false;       // still trying the new calibration
            proc.rebuildTracker();
        }
        else if (page != Page::Tuning)
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
        extrasToggle.setVisible (tuningPage);
        endFretsButton.setVisible (false); // shown by tick() during "every note"
        cancelButton.setVisible (! done);
        listenButton.setVisible (running);
        backButton.setVisible (running || done);
        acceptButton.setVisible (false);   // shown by tick() when there is a decision to make
        skipButton.setVisible (running);
        saveButton.setVisible (done && confirmed());
        discardButton.setVisible (done);
        tryButton.setVisible (done && confirmed());
        finishButton.setVisible (done && ! confirmed());
        tryLabel.setVisible (done && trying());
        autoToggle.setVisible (! done);
        autoDelay.setVisible (! done);
        cuesToggle.setVisible (! done);
        body.setVisible (! running);
        headline.setVisible (running);
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
        session.start (proc.instrument, hexMode (proc), std::max (1, proc.numGuitarInputs()), full,
                       full && extrasToggle.getToggleState());
        proc.work.calLearning.reset();
        proc.work.calAudio.clear();
        pendingCaptures.clear();
        lastStepNumber = -1;
        shownProgress = 0.0;
        showPage (Page::Running);
    }

    void CalibrationWizard::feed (const AnalysisFrame& f, double hopSeconds)
    {
        if (page != Page::Running) return;
        session.feed (f, hopSeconds);
        if (session.finished())
            showDone();
    }

    void CalibrationWizard::showDone()
    {
        proc.work.calConfirmed = false;
        showPage (Page::Done);
    }

    void CalibrationWizard::startTrying()
    {
        if (trying() || ! session.finished()) return;
        proc.work.calTryBackup = proc.calibration;
        proc.calibration = newCalibration();
        proc.calibrating = false;           // track and send MIDI as normal, with the new calibration
        proc.rebuildTracker();
    }

    void CalibrationWizard::stopTrying()
    {
        if (! trying()) return;
        proc.calibration = *proc.work.calTryBackup;
        proc.work.calTryBackup.reset();
        startTracker();                     // back to the quiet wizard tracker
    }

    void CalibrationWizard::finish (bool save)
    {
        if (trying())
        {
            if (! save) proc.calibration = *proc.work.calTryBackup;
            proc.work.calTryBackup.reset();
        }
        if (save && session.finished())
            proc.calibration = newCalibration();
        proc.work.calLearning.reset(); // stops it if it still runs (Discard)
        // The sound of every fret (Full calibration) is kept in a file, so Poly can learn from it again later.
        if (save && session.finished() && ! proc.work.calAudio.empty())
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                 .getChildFile ("Guitar Lab").getChildFile ("Calibration audio");
            if (dir.createDirectory())
                proc.work.calAudio.save (std::filesystem::path (dir.getChildFile ("latest.glaudio").getFullPathName().toWideCharPointer()));
        }

        proc.calibrating = false;
        proc.rebuildTracker();
        page = Page::Tuning;
        proc.work.calPage = 0;
        proc.work.calOpen = false;
        if (onClose) onClose();
    }

    void CalibrationWizard::startLearning()
    {
        auto& w = proc.work;
        if (w.calLearning != nullptr || w.calAudio.notes.size() < 24)
            return; // already learning or learned, or not a Full calibration
        w.calLearning = std::make_unique<GuitarLabProcessor::PanelWork::NoteLearning>();
        auto* job = w.calLearning.get();
        job->worker = std::thread ([job, audio = w.calAudio, inst = proc.instrument, cal = session.result()]
        {
            job->result = learnNoteNet (audio, inst, cal, [job] (double p) { job->progress = p; return ! job->cancel.load(); });
            job->done = true;
        });
    }

    bool CalibrationWizard::learning() const
    {
        const auto* job = proc.work.calLearning.get();
        return job != nullptr && ! job->done.load();
    }

    Calibration CalibrationWizard::newCalibration() const
    {
        auto c = session.result();
        if (const auto* job = proc.work.calLearning.get(); job != nullptr && job->done.load() && job->result.usable())
            c.net = job->result;
        return c;
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

    juce::String CalibrationWizard::headlineText() const
    {
        using Step = CalibrationSession::Step;
        const int s = session.currentString();
        const auto& tuning = proc.instrument.tuning();
        const juce::String str = s >= 0 && s < (int) tuning.size()
            ? "String " + juce::String (s + 1) + " (" + juce::String (midiNoteName (tuning[(size_t) s])) + ")" : juce::String();
        const juce::String dot = "  " + juce::String::charToString ((juce::juce_wchar) 0x00b7) + "  "; // middle dot
        switch (session.step())
        {
            case Step::Silence:     return "Quiet";
            case Step::OpenString:  return str + dot + "open";
            case Step::TwelfthFret: return str + dot + "fret 12";
            case Step::EveryNote:   return str + dot + "fret " + juce::String (session.expectedFret());
            case Step::Chord:       return session.currentChord() != nullptr ? juce::String (session.currentChord()->name) : juce::String();
            case Step::Soft:        return str + dot + "soft";
            case Step::Hard:        return str + dot + "HARD";
            case Step::Done:        break;
        }
        return {};
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

    void CalibrationWizard::captureFretAudio()
    {
        // A fret or chord of a Full calibration passed (it was listening, and the next item came): copy its sound 0.6 s
        // later, so the recording holds the pick(s) and most of the sound. The input is copied while calibrating
        // (tunerRing). A fret keeps 2 s, a chord everything since Listen (its 3 strums).
        const auto now = juce::Time::getMillisecondCounter();
        const int step = session.stepNumber();
        if (session.isListening() && ! lastListening)
            listenStartedAt = now;
        if (lastListening && step == lastStepNumber + 1)
        {
            if (lastFretItem.string >= 0)
                pendingCaptures.push_back ({ lastFretItem.string, lastFretItem.fret, {}, now + 600, 2000 });
            else if (! lastShape.empty())
                pendingCaptures.push_back ({ -1, -1, lastShape, now + 600, now - listenStartedAt + 600 });
        }

        const double rate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        for (auto it = pendingCaptures.begin(); it != pendingCaptures.end();)
        {
            if ((juce::int32) (now - it->at) < 0) { ++it; continue; }
            const int count = std::min (GuitarLabProcessor::tunerRingSize(), (int) (it->milliseconds / 1000.0 * rate));
            captureBuffer.resize ((size_t) count);
            proc.readTunerInput (captureBuffer.data(), count);
            if (it->shape.empty()) proc.work.calAudio.put (it->string, it->fret, captureBuffer.data(), count, rate);
            else                   proc.work.calAudio.putShape (it->shape, captureBuffer.data(), count, rate);
            it = pendingCaptures.erase (it);
        }

        lastStepNumber = step;
        lastListening = session.isListening();
        const bool fretItem = session.step() == CalibrationSession::Step::EveryNote && session.expectedFret() >= 0;
        lastFretItem = fretItem ? FretItem { session.currentString(), session.expectedFret() } : FretItem {};
        const auto* chord = session.step() == CalibrationSession::Step::Chord ? session.currentChord() : nullptr;
        lastShape = chord != nullptr ? chord->frets : std::vector<int> {};
    }

    void CalibrationWizard::tick()
    {
        playCues();
        captureFretAudio();
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
                headline.setText (headlineText(), juce::dontSendNotification);
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
                    showDone();
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
            {
                if (! confirmed())
                {
                    title.setText ("Calibration - all steps played", juce::dontSendNotification);
                    body.setText ("That was the last step.\n\n"
                                  "If the last thing you played went wrong (a wrong fret, a buzz, another string ringing), "
                                  "press Back and play it again.\n\n"
                                  "If everything went well, press Finish to see the results.",
                                  juce::dontSendNotification);
                    break;
                }
                title.setText ("Calibration - done", juce::dontSendNotification);
                startLearning();
                juce::String text = summary();
                if (learning())
                    text << "\n\nLearning your guitar's sound for Poly mode... "
                         << juce::String ((int) std::lround (100.0 * proc.work.calLearning->progress.load())) << "%  "
                         << "(1-2 minutes; Save and Try it wait for it).";
                else if (const auto* job = proc.work.calLearning.get(); job != nullptr && job->result.usable())
                    text << "\n\nPoly mode learned your guitar's sound: it now tells real notes from overtones much better.";
                body.setText (text, juce::dontSendNotification);
                saveButton.setEnabled (! learning());
                tryButton.setEnabled (! learning());
                tryButton.setButtonText (trying() ? "Stop trying" : "Try it");
                if (tryLabel.isVisible() != trying())
                {
                    tryLabel.setVisible (trying());
                    resized();
                }
                if (trying())
                {
                    // The notes heard right now with the new calibration, and their chord name.
                    std::vector<int> heard;
                    for (int s = proc.instrument.numStrings() - 1; s >= 0; --s)
                        if (const int f = proc.soundingFret[(size_t) s].load(); f >= 0)
                            heard.push_back (proc.instrument.noteAt ({ s, f }));
                    juce::String text ("Trying the new calibration: play something.");
                    if (! heard.empty())
                    {
                        text = "Heard:";
                        for (int n : heard) text << "  " << juce::String (midiNoteName (n));
                        if (heard.size() >= 2)
                            if (const auto names = nameChord (heard); ! names.empty())
                                text << "   =   " << juce::String (names.front().name);
                    }
                    tryLabel.setText (text, juce::dontSendNotification);
                }
                break;
            }
        }
    }

    void CalibrationWizard::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
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
        for (juce::Button* b : { (juce::Button*) &startButton, (juce::Button*) &fullButton, (juce::Button*) &extrasToggle,
                                 (juce::Button*) &listenButton, (juce::Button*) &acceptButton, (juce::Button*) &backButton,
                                 (juce::Button*) &skipButton, (juce::Button*) &endFretsButton, (juce::Button*) &finishButton,
                                 (juce::Button*) &tryButton, (juce::Button*) &saveButton, (juce::Button*) &discardButton,
                                 (juce::Button*) &cancelButton })
            if (b->isVisible())
            {
                b->setBounds (buttons.removeFromLeft (b == &startButton || b == &fullButton || b == &endFretsButton
                                                          || b == &extrasToggle ? 210
                                                      : (b == &listenButton ? 160 : 120)));
                buttons.removeFromLeft (8);
            }

        r.removeFromBottom (8);
        if (page == Page::Running)
        {
            stepLabel.setBounds (r.removeFromTop (22));
            // The big headline gets what is left after the other lines, so it can be read from a distance.
            const int rest = std::max (0, r.getHeight() - 22 - 44 - 70);
            headline.setBounds (r.removeFromTop (rest));
            headline.setFont (juce::FontOptions ((float) juce::jlimit (28, 110, (int) (rest * 0.6)), juce::Font::bold));
            instruction.setFont (juce::FontOptions (rest > 120 ? 20.0f : 24.0f, juce::Font::bold));
            instruction.setBounds (r.removeFromTop (70));
            status.setBounds (r.removeFromTop (44));
        }
        else
        {
            if (tryLabel.isVisible())
            {
                tryLabel.setBounds (r.removeFromTop (56));
                r.removeFromTop (6);
            }
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
            "How many checks the pitch must stay the same before a note is sent (5 ms each in mono, 11 ms in poly).\n"
            "1-2 = fastest response, but more wrong 'blip' notes on the pick attack.\n"
            "3 = good balance (default). 5-8 = slower, but very few wrong notes.\n"
            "Low notes always need a little longer, because one wave of a low note takes longer.\n"
            "Mono and poly each keep their own value: this knob shows the one of the mode chosen on the left.");
        safety.setSliderStyle (juce::Slider::LinearHorizontal);
        safety.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
        safety.setTooltip (safetyLabel.getTooltip());
        addAndMakeVisible (safety);

        label (sensLabel, "Sensitivity", "Sensitivity\n"
            "Higher = soft notes and ghost notes are tracked too, but noise may start notes.\n"
            "Lower = only clear, strong notes are tracked.\n"
            "1.0 uses the level found by calibration. The yellow line in the level meters shows the result.\n"
            "Mono and poly each keep their own value: this knob shows the one of the mode chosen on the left.");
        sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        sensitivity.setTooltip (sensLabel.getTooltip());
        addAndMakeVisible (sensitivity);
        attachModeKnobs();

        label (windowLabel, "Pitch window", "Pitch window (mono)\n"
            "How many waves of a note the pitch detector looks at before it names the note.\n"
            "Fewer = the note is sent sooner, but more wrong notes. More = slower, surer. 'full' = the old window, "
            "the same length for every note (sized for the lowest string).\n"
            "4 (default): high notes start about 13 ms sooner than with 'full', with no more wrong notes on real "
            "guitar licks. The string guess is a little less sure (it hears less of the note before it starts).\n"
            "Low notes gain little: their waves are long. Watch the delay in 'Notes sent' while you try it.");
        pitchWindow.setSliderStyle (juce::Slider::LinearHorizontal);
        pitchWindow.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        pitchWindow.setTooltip (windowLabel.getTooltip());
        addAndMakeVisible (pitchWindow);
        windowAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::trackPitchWindow, pitchWindow);

        label (waitLabel, "Wait after pick", "Wait after pick (poly)\n"
            "Poly waits this long after a pick before it decides which notes sound. It hears more of the strum, so "
            "fewer wrong extra notes, but every note comes later by up to this much.\n"
            "Off (default) = no added delay. In tests after a Full calibration, 11 ms cut the wrong notes in single-note "
            "playing by about a fifth and found almost as many notes. Without a calibration, 43 ms removed about 30% of "
            "the wrong notes in chord licks.\n"
            "Change it at any time: it does not need a new calibration.");
        waitBox.addItemList ({ "Off", "11 ms", "21 ms", "43 ms" }, 1);
        waitBox.setTooltip (waitLabel.getTooltip());
        addAndMakeVisible (waitBox);
        waitAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.params, ParamIDs::trackWaitPoly, waitBox);
        waitBox.setVisible (false); // the timer shows it (instead of the pitch window) in poly mode
        waitLabel.setVisible (false);

        label (hystLabel, "Hysteresis", "Hysteresis (poly mode)\n"
            "Stops a single note from being heard as a chord. Every guitar note also sounds an octave, an octave + 5th, "
            "two octaves... higher (its overtones). With this turned up, a note on one of those spots must be clearly "
            "stronger before it starts, but only right after you picked the louder note below it.\n"
            "0 = off (the normal behaviour). 1 = strongest: about a third fewer wrong extra notes, but a quiet real "
            "octave strummed together with its lower note may be missed.\n"
            "Arpeggios over ringing strings are not affected, and it never adds delay. Mono and hex modes ignore it "
            "(it is greyed out there).\n"
            "After a Full calibration it is greyed out too: Poly then uses the judge it learned from your guitar, "
            "which decides instead of this rule.");
        hysteresis.setSliderStyle (juce::Slider::LinearHorizontal);
        hysteresis.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        hysteresis.setTooltip (hystLabel.getTooltip());
        addAndMakeVisible (hysteresis);
        hystAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::trackHysteresis, hysteresis);

        label (styleLabel, "Playing style", "Playing style\n"
            "Sets Speed / safety, Sensitivity and Hysteresis together for the way you play:\n"
            "  - Balanced: the normal settings.\n"
            "  - Lead / single notes: the fewest wrong extra notes (a single note is not heard as a chord).\n"
            "  - Rhythm / chords: fewer short 'blip' notes in strums, keeps most quiet octave notes.\n"
            "  - Arpeggios / fingerpicking: also hears quieter notes; notes over ringing strings still start.\n"
            "It sets the knobs of the mode chosen now. Hysteresis only acts in poly without a Full calibration "
            "(after one, Lead and Balanced are the same).\n"
            "Change any knob afterwards and this shows 'Custom'.");
        styleBox.addItem ("Custom", 1);
        styleBox.addItem ("Balanced (default)", 2);
        styleBox.addItem ("Lead / single notes", 3);
        styleBox.addItem ("Rhythm / chords", 4);
        styleBox.addItem ("Arpeggios / fingerpicking", 5);
        styleBox.setItemEnabled (1, false); // shown, not chosen
        styleBox.setTooltip (styleLabel.getTooltip());
        styleBox.onChange = [this] { applyStyle (styleBox.getSelectedId()); };
        addAndMakeVisible (styleBox);

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

        theme::primary (calibrateButton);
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

        tunerButton.setTooltip ("Lighthouse tuner\nA big, smooth strobe tuner. Play one string: the lighthouse beam and the "
                                "strobe bands turn while the string is out of tune (clockwise / to the right = sharp) and "
                                "stand still when it is in tune. The nearest string of your tuning lights up.\n"
                                "It only works while it is open, so it costs nothing the rest of the time. Esc closes it.");
        tunerButton.onClick = [this] { lighthouse.open(); };
        addAndMakeVisible (tunerButton);
        addChildComponent (lighthouse);

        // --- Presets: settings, strings + tuning and calibration together ---
        presetLabel.setText ("Presets", juce::dontSendNotification);
        presetLabel.setJustificationType (juce::Justification::centredRight);
        const juce::String presetTip ("Presets\n"
            "Pick one to load it. A preset holds your whole setup: the tracking settings on this page, the MIDI and "
            "sound settings, Strings and Tuning, and the calibration (if you had one when you saved it). "
            "Use this when you switch guitars or tunings: set up and calibrate each guitar once, save it, and load it "
            "again later.\n"
            "Not in a preset: the metronome and the tempo (they belong to the song).\n"
            "Presets saved by older versions hold only a calibration: loading one changes only the calibration.\n"
            "'Default' goes back to the plain settings with no calibration (Strings and Tuning stay as they are).\n"
            "Presets are shared by all your projects and by the standalone app. They are saved as small text files in:\n"
            + presetFolder().getFullPathName());
        presetLabel.setTooltip (presetTip);
        presetBox.setTooltip (presetTip);
        presetBox.setTextWhenNothingSelected ("Choose a preset...");
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id == kDefaultPresetId) loadDefaultCalibration();
            else if (id - 2 >= 0 && id - 2 < presetFiles.size()) loadPreset (presetFiles[id - 2]);
        };
        addAndMakeVisible (presetLabel);
        addAndMakeVisible (presetBox);

        presetName.setTextToShowWhenEmpty ("Name, e.g. Strat Drop D", theme::textDim);
        presetName.setTooltip ("Preset name\nA name for the setup you are using now, for example the guitar and its tuning. "
                               "Then press 'Save as preset'. A preset with the same name is replaced.");
        presetName.onReturnKey = [this] { savePreset(); };
        addAndMakeVisible (presetName);

        savePresetButton.setTooltip ("Save as preset\nSaves the settings, Strings and Tuning, and the calibration you are "
                                     "using now under the name on the left, so you can load them again in any project.");
        savePresetButton.onClick = [this] { savePreset(); };
        addAndMakeVisible (savePresetButton);

        deletePresetButton.setTooltip ("Delete preset\nDeletes the preset chosen on the left (the file, not the "
                                       "settings in use). Click twice to be sure. This cannot be undone.");
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
        addAndMakeVisible (noteLog);
        clearLogButton.setTooltip ("Clear\nEmpties the list of notes sent.");
        clearLogButton.onClick = [this] { noteLog.clear(); };
        addAndMakeVisible (clearLogButton);

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
        presetBox.addItem ("Default (plain settings, no calibration)", kDefaultPresetId);
        if (! presetFiles.isEmpty()) presetBox.addSeparator();
        for (int i = 0; i < presetFiles.size(); ++i)
            presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), i + 2);
        deleteArmedAt = 0;
        deletePresetButton.setButtonText ("Delete");
    }

    namespace
    {
        // The parameters a preset holds: everything except the metronome and the tempo (they belong to the song).
        const char* const kPresetParams[] = {
            ParamIDs::trackMode, ParamIDs::trackInput, ParamIDs::trackStability, ParamIDs::trackSensitivity,
            ParamIDs::trackStabilityPoly, ParamIDs::trackSensitivityPoly, ParamIDs::trackPitchWindow, ParamIDs::trackWaitPoly,
            ParamIDs::trackHysteresis, ParamIDs::trackBends, ParamIDs::trackMonitor,
            ParamIDs::bendRange, ParamIDs::firstChannel, ParamIDs::sendMpe, ParamIDs::passThrough,
            ParamIDs::synthOn, ParamIDs::synthVolume, ParamIDs::synthTone,
            ParamIDs::legatoOverlap, ParamIDs::legatoVelocity, ParamIDs::liveVelocity };

        void setParam (juce::RangedAudioParameter& p, float normalised)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (normalised);
            p.endChangeGesture();
        }
    }

    void TrackerPanel::loadPreset (const juce::File& file)
    {
        SetupPreset preset;
        if (! preset.deserialise (file.loadFileAsString().toStdString()))
        {
            showPresetMessage ("Could not read the preset '" + file.getFileNameWithoutExtension() + "'.");
            return;
        }

        for (const auto& [id, value] : preset.settings)
            for (const char* known : kPresetParams)
                if (id == known)
                    if (auto* p = proc.params.getParameter (known))
                        setParam (*p, p->convertTo0to1 (value));

        // Older presets had one Speed / safety and Sensitivity for every mode: Poly gets the same values.
        auto has = [&preset] (const char* id) { return std::any_of (preset.settings.begin(), preset.settings.end(),
                                                                    [id] (const auto& s) { return s.first == id; }); };
        for (const auto& [shared, poly] : { std::pair { ParamIDs::trackStability, ParamIDs::trackStabilityPoly },
                                            std::pair { ParamIDs::trackSensitivity, ParamIDs::trackSensitivityPoly } })
            if (has (shared) && ! has (poly))
                if (auto* p = proc.params.getParameter (poly))
                    setParam (*p, p->convertTo0to1 (proc.params.getRawParameterValue (shared)->load()));

        proc.calibration = preset.cal; // a preset saved without a calibration = no calibration
        // A calibration belongs to the tuning it was made for: switch to it.
        const auto& tuning = preset.tuning.empty() ? preset.cal.tuning : preset.tuning;
        if (! tuning.empty() && tuning != proc.instrument.tuning() && onTuningWanted)
            onTuningWanted (tuning);
        proc.rebuildTracker();
        presetName.setText (file.getFileNameWithoutExtension(), juce::dontSendNotification);
        showPresetMessage ("Loaded: " + file.getFileNameWithoutExtension()
                           + (preset.calibrationOnly ? " (an older preset: only the calibration)" : ""));
    }

    void TrackerPanel::loadDefaultCalibration()
    {
        for (const char* id : kPresetParams)
            if (auto* p = proc.params.getParameter (id))
                setParam (*p, p->getDefaultValue());
        proc.calibration = Calibration {};
        proc.rebuildTracker();
        presetName.clear();
        showPresetMessage ("Back to the default: plain settings, no calibration in use.");
    }

    void TrackerPanel::savePreset()
    {
        const auto name = juce::File::createLegalFileName (presetName.getText().trim());
        if (name.isEmpty())
        {
            showPresetMessage ("Type a name for the preset first (box left of 'Save as preset').");
            return;
        }

        SetupPreset preset;
        for (const char* id : kPresetParams)
            if (auto* p = proc.params.getParameter (id))
                preset.settings.push_back ({ id, p->convertFrom0to1 (p->getValue()) });
        preset.tuning = proc.instrument.tuning();
        preset.cal = proc.calibration;

        const auto file = presetFolder().getChildFile (name + ".glcal");
        if (! presetFolder().createDirectory() || ! file.replaceWithText (preset.serialise()))
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

    void TrackerPanel::attachModeKnobs()
    {
        const int mode = (int) trackMode (proc);
        if (mode == attachedMode) return;
        attachedMode = mode;
        safetyAtt.reset(); // detach first: the new attachment sets the slider to the other mode's value
        sensAtt.reset();
        safetyAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::stabilityFor (mode), safety);
        sensAtt = std::make_unique<APVTS::SliderAttachment> (proc.params, ParamIDs::sensitivityFor (mode), sensitivity);
    }

    namespace
    {
        struct Style { int id; float safety, sensitivity, hysteresis; };
        constexpr Style kStyles[] = { { 2, 3.0f, 1.0f, 0.0f }, { 3, 3.0f, 1.0f, 1.0f }, { 4, 4.0f, 1.0f, 0.5f }, { 5, 3.0f, 1.2f, 0.5f } };
    }

    void TrackerPanel::applyStyle (int id)
    {
        for (const auto& st : kStyles)
        {
            if (st.id != id) continue;
            const int mode = (int) trackMode (proc); // the knobs of the mode chosen now
            const std::pair<const char*, float> values[] = { { ParamIDs::stabilityFor (mode), st.safety },
                                                             { ParamIDs::sensitivityFor (mode), st.sensitivity },
                                                             { ParamIDs::trackHysteresis, st.hysteresis } };
            for (const auto& [paramId, v] : values)
                if (auto* p = proc.params.getParameter (paramId))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
                    p->endChangeGesture();
                }
        }
    }

    int TrackerPanel::matchingStyle() const
    {
        const int mode = (int) trackMode (proc);
        const float safety = proc.params.getRawParameterValue (ParamIDs::stabilityFor (mode))->load();
        const float sens = proc.params.getRawParameterValue (ParamIDs::sensitivityFor (mode))->load();
        const float hyst = proc.params.getRawParameterValue (ParamIDs::trackHysteresis)->load();
        for (const auto& st : kStyles)
            if (std::abs (safety - st.safety) < 0.01f && std::abs (sens - st.sensitivity) < 0.015f && std::abs (hyst - st.hysteresis) < 0.015f)
                return st.id;
        return 1;
    }

    void TrackerPanel::restoreUi()
    {
        wizard.restoreUi();
        if (proc.work.calOpen && ! wizard.isVisible())
        {
            wizard.setVisible (true);
            wizard.resume();
        }
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

        // The playing style box follows the knobs.
        if (const int style = matchingStyle(); styleBox.getSelectedId() != style)
            styleBox.setSelectedId (style, juce::dontSendNotification);

        // Hysteresis only works in poly mode without a learned judge (the judge decides instead of the rules): grey it
        // out otherwise (the value is kept).
        const bool poly = trackMode (proc) == TrackMode::Poly;
        const auto& inUse = proc.calibration;
        const bool judge = inUse.valid && inUse.tuning == proc.instrument.tuning() && inUse.net.usable()
                           && inUse.net.inputs == PolyTracker::kNumNoteFeatures;
        if (hysteresis.isEnabled() != (poly && ! judge))
        {
            hysteresis.setEnabled (poly && ! judge);
            hystLabel.setEnabled (poly && ! judge);
        }

        // Per-mode knobs: Speed / safety and Sensitivity follow the mode; poly shows its wait instead of the pitch window.
        attachModeKnobs();
        const bool mono = trackMode (proc) == TrackMode::Mono;
        if (waitBox.isVisible() != poly)
        {
            waitBox.setVisible (poly);   waitLabel.setVisible (poly);
            pitchWindow.setVisible (! poly); windowLabel.setVisible (! poly);
        }
        if (pitchWindow.isEnabled() != mono) { pitchWindow.setEnabled (mono); windowLabel.setEnabled (mono); }

        // Notes the tracker sent: into the note log (string from the MIDI channel).
        noteLog.rate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const int numTracked = proc.popTrackedEvents (trackedBuffer.data(), (int) trackedBuffer.size());
        for (int i = 0; i < numTracked; ++i)
        {
            const auto& e = trackedBuffer[(size_t) i];
            const int s = (e.status & 0x0F) + 1 - proc.midiSettings.firstChannel;
            const bool validString = s >= 0 && s < proc.instrument.numStrings();
            const int fret = validString ? e.data1 - proc.instrument.tuning()[(size_t) s] : -1;
            if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
            {
                noteLog.noteOn (e.sample, e.data1, validString ? s : -1, fret, e.data2, e.pick);
                const int movedFrom = e.data1 == lastOffNote && e.sample == lastOffSample && lastOffString != s ? lastOffString : -1;
                if (validString && onTrackedNote) onTrackedNote (s, fret, (double) e.sample / noteLog.rate, movedFrom);
            }
            else
            {
                noteLog.noteOff (e.sample, e.data1, validString ? s : -1);
                lastOffNote = e.data1;
                lastOffString = validString ? s : -1;
                lastOffSample = e.sample;
            }
        }

        if (wizard.isVisible())
            wizard.tick();

        // Tuner rows
        const auto mode = trackMode (proc);
        const auto& cal = proc.calibration;
        tuner.rows.clear();
        tuner.gate = cal.gateOn() / std::max (0.25f, proc.params.getRawParameterValue (ParamIDs::sensitivityFor ((int) trackMode (proc)))->load());
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
        {
            auto spot = a; // the pitch window (mono, hex) and the wait (poly) share one place
            put (spot, windowLabel, 86); put (spot, pitchWindow, 150);
            put (a, waitLabel, 100);     put (a, waitBox, 90);
        }

        auto b = row();
        put (b, bendsToggle, 230);
        put (b, monitorToggle, 110);
        put (b, soundToggle, 130);
        put (b, hystLabel, 74);     put (b, hysteresis, 150);
        put (b, calibrateButton, 110);
        put (b, intonationButton, 100);
        put (b, tunerButton, 80);

        calStatus.setBounds (row (20)); // its own line: the text can be long

        auto c = row();
        put (c, presetLabel, 130);       put (c, presetBox, 260);
        put (c, deletePresetButton, 70); c.removeFromLeft (16);
        put (c, presetName, 200);        put (c, savePresetButton, 120);
        c.removeFromLeft (16);
        put (c, styleLabel, 90);         put (c, styleBox, 210);

        warning.setBounds (row (20));
        help.setBounds (row (20));
        auto logArea = r.removeFromRight (juce::jmin (400, r.getWidth() / 3));
        r.removeFromRight (6);
        clearLogButton.setBounds (logArea.getRight() - 58, logArea.getY() + 2, 54, 20);
        noteLog.setBounds (logArea);
        tuner.setBounds (r);
        wizard.setBounds (getLocalBounds().reduced (20, 10));
        intonation.setBounds (getLocalBounds().reduced (10));
        lighthouse.setBounds (getLocalBounds().reduced (6));
    }
}
