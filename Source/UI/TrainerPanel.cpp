#include "UI/TrainerPanel.h"

namespace gl
{
    TrainerPanel::TrainerPanel (GuitarLabProcessor& p) : proc (p)
    {
        auto label = [this] (juce::Label& l, const juce::String& text, const juce::String& tip)
        {
            l.setText (text, juce::dontSendNotification);
            l.setTooltip (tip);
            addAndMakeVisible (l);
        };

        label (modeLabel, "Quiz", "Quiz type\n"
            "Name the note: a dot appears on the neck. Click the note name (or a piano key).\n"
            "Find the note: a note name is shown. Click EVERY place it appears in the chosen strings and frets, then press Check.\n"
            "Arpeggio tones: a chord is shown. Click every chord tone in the chosen area, then press Check.\n"
            "Interval name: two dots appear. Name the distance from the blue dot up to the orange dot.");
        modeBox.addItem ("Name the note", 1);
        modeBox.addItem ("Find the note", 2);
        modeBox.addItem ("Arpeggio tones", 3);
        modeBox.addItem ("Interval name", 4);
        modeBox.setSelectedId (1, juce::dontSendNotification);
        modeBox.setTooltip (modeLabel.getTooltip());
        modeBox.onChange = [this] { updateAnswerButtons(); nextQuestion(); };
        addAndMakeVisible (modeBox);

        label (stringsLabel, "Strings", "Strings to practise\nOnly the ticked strings are used in questions.\n"
                                        "Example: tick only the 4th string (D on a 6-string) to drill its notes.");

        label (fretsLabel, "Frets", "Fret range\nOnly frets between these two numbers are used. The range is shaded yellow on the neck.\n"
                                    "Example: 12 to 24 to drill notes and arpeggios past the 12th fret.");
        for (auto* s : { &minFret, &maxFret })
        {
            s->setRange (0, kNumFrets, 1);
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
            s->setTooltip (fretsLabel.getTooltip());
            s->onValueChange = [this, s]
            {
                if (minFret.getValue() > maxFret.getValue())
                    (s == &minFret ? maxFret : minFret).setValue (s->getValue(), juce::dontSendNotification);
                applyFocus();
            };
            addAndMakeVisible (*s);
        }
        minFret.setValue (0, juce::dontSendNotification);
        maxFret.setValue (12, juce::dontSendNotification);

        naturalsToggle.setTooltip ("Natural notes only\nOnly ask about C D E F G A B (no sharps or flats). Good for beginners.\n"
                                   "Arpeggio answers still include sharps and flats when the chord needs them.");
        naturalsToggle.onClick = [this] { applyFocus(); };
        addAndMakeVisible (naturalsToggle);

        adaptiveToggle.setToggleState (true, juce::dontSendNotification);
        adaptiveToggle.setTooltip ("Drill my weak spots\nOn: spots you miss, or answer slowly, are asked much more often. New spots are asked often too.\n"
                                   "Off: every spot in the range is equally likely.");
        adaptiveToggle.onClick = [this] { proc.trainer.setAdaptive (adaptiveToggle.getToggleState()); };
        addAndMakeVisible (adaptiveToggle);

        heatToggle.setTooltip ("Heat map\nColours every spot you have been asked about: green = you know it, red = you miss it.\n"
                               "Your scores are saved with the project / plugin state.");
        heatToggle.onClick = [this] { if (onDisplayChanged) onDisplayChanged(); };
        addAndMakeVisible (heatToggle);

        nextButton.setTooltip ("Next question\nAsks a new question. Answer speed is measured from this moment.");
        nextButton.onClick = [this] { nextQuestion(); };
        addAndMakeVisible (nextButton);

        checkButton.setTooltip ("Check\nFor 'Find the note' and 'Arpeggio tones': checks the spots you clicked.\n"
                                "Green = right, red dot = wrong click, red ring = a spot you missed.");
        checkButton.onClick = [this] { checkPositions(); };
        addAndMakeVisible (checkButton);

        resetButton.setTooltip ("Reset stats\nDeletes all your scores and the heat map. This cannot be undone.");
        resetButton.onClick = [this]
        {
            proc.trainer.resetStats();
            updateScore();
            if (onDisplayChanged) onDisplayChanged();
        };
        addAndMakeVisible (resetButton);

        for (int i = 0; i < 12; ++i)
        {
            auto* b = answerButtons.add (new juce::TextButton());
            b->onClick = [this, i]
            {
                if (mode() == QuizMode::IntervalName) answerInterval (i);
                else                                  answerName (i);
            };
            addAndMakeVisible (b);
        }

        promptLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        promptLabel.setColour (juce::Label::textColourId, theme::text);
        promptLabel.setText ("Press Start. Pick strings and frets on the left to focus your practice.", juce::dontSendNotification);
        addAndMakeVisible (promptLabel);

        feedbackLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (feedbackLabel);

        scoreLabel.setColour (juce::Label::textColourId, theme::textDim);
        scoreLabel.setTooltip ("Score\nTotal questions, how many you got right, and your current streak of right answers in a row.");
        addAndMakeVisible (scoreLabel);

        refresh();
    }

    // -----------------------------------------------------------------------
    void TrainerPanel::refresh()
    {
        stringToggles.clear();
        const int count = proc.instrument.numStrings();
        for (int s = 0; s < count; ++s)
        {
            auto* t = stringToggles.add (new juce::ToggleButton (juce::String (s + 1) + " "
                                         + juce::String (pitchClassName (pitchClassOf (proc.instrument.tuning()[(size_t) s])))));
            t->setToggleState (true, juce::dontSendNotification);
            t->setTooltip ("String " + juce::String (s + 1) + " (" + juce::String (midiNoteName (proc.instrument.tuning()[(size_t) s])) + ")\n"
                           "Tick to include this string in questions. String 1 is the highest string.");
            t->onClick = [this] { applyFocus(); };
            addAndMakeVisible (t);
        }

        proc.trainer.setInstrument (proc.instrument);
        updateAnswerButtons();
        applyFocus();
        updateScore();
        resized();
    }

    void TrainerPanel::applyFocus()
    {
        QuizFocus f;
        for (auto* t : stringToggles)
            f.strings.push_back (t->getToggleState());
        f.minFret = (int) minFret.getValue();
        f.maxFret = (int) maxFret.getValue();
        f.naturalsOnly = naturalsToggle.getToggleState();
        proc.trainer.setFocus (f);

        if (onDisplayChanged) onDisplayChanged();
    }

    void TrainerPanel::updateAnswerButtons()
    {
        const bool intervals = mode() == QuizMode::IntervalName;
        const bool names = mode() == QuizMode::NameTheNote;

        for (int i = 0; i < 12; ++i)
        {
            auto* b = answerButtons[i];
            if (intervals)
            {
                b->setButtonText (juce::String (intervalName (i)));
                b->setTooltip ("Answer: " + juce::String (intervalName (i)) + " (" + juce::String (i) + " half steps)");
            }
            else
            {
                b->setButtonText (juce::String (pitchClassName (i, false)) + (pitchClassName (i, false) != pitchClassName (i, true)
                                  ? "/" + juce::String (pitchClassName (i, true)) : juce::String()));
                b->setTooltip ("Answer: " + b->getButtonText() + "\nYou can also click a piano key with this note name.");
            }
            b->setVisible (intervals || names);
        }
        checkButton.setVisible (! intervals && ! names);
        resized();
    }

    double TrainerPanel::secondsSinceAsked() const
    {
        return (juce::Time::getMillisecondCounterHiRes() - askedAt) / 1000.0;
    }

    void TrainerPanel::nextQuestion()
    {
        applyFocus();
        question = proc.trainer.next (mode());
        clicked.clear();
        missed.clear();
        wrong.clear();
        answered = false;
        askedAt = juce::Time::getMillisecondCounterHiRes();

        promptLabel.setText (juce::String (question->prompt), juce::dontSendNotification);
        feedbackLabel.setText ({}, juce::dontSendNotification);
        nextButton.setButtonText ("Next");

        // Play the shown note(s), so you also train your ear.
        for (auto p : question->shown)
        {
            proc.liveNoteOn (p.string, p.fret);
            const int s = p.string;
            juce::Component::SafePointer<TrainerPanel> safe (this);
            juce::Timer::callAfterDelay (700, [safe, s] { if (safe != nullptr) safe->proc.liveNoteOff (s); });
        }

        if (onDisplayChanged) onDisplayChanged();
    }

    void TrainerPanel::showResult (bool ok, const juce::String& detail)
    {
        answered = true;
        feedbackLabel.setColour (juce::Label::textColourId, ok ? theme::good : theme::bad);
        feedbackLabel.setText ((ok ? "Right! " : "Not quite. ") + detail + "   (" + juce::String (secondsSinceAsked(), 1) + " s)",
                               juce::dontSendNotification);
        updateScore();
        if (onDisplayChanged) onDisplayChanged();

        // Right answers move on by themselves after a short pause.
        if (ok)
        {
            juce::Component::SafePointer<TrainerPanel> safe (this);
            juce::Timer::callAfterDelay (900, [safe] { if (safe != nullptr && safe->answered) safe->nextQuestion(); });
        }
    }

    void TrainerPanel::answerName (PitchClass pc)
    {
        if (! question || answered || question->mode != QuizMode::NameTheNote) return;
        const bool ok = proc.trainer.answerPitchClass (*question, pc, secondsSinceAsked());
        showResult (ok, "It is " + juce::String (pitchClassName (question->targetPc)) + ".");
    }

    void TrainerPanel::answerInterval (int semitones)
    {
        if (! question || answered || question->mode != QuizMode::IntervalName) return;
        const bool ok = proc.trainer.answerInterval (*question, semitones, secondsSinceAsked());
        showResult (ok, "It is a " + juce::String (intervalName (question->interval)) + ".");
    }

    void TrainerPanel::checkPositions()
    {
        if (! question || answered) return;
        if (question->mode != QuizMode::FindTheNote && question->mode != QuizMode::ArpeggioTones) return;

        const bool ok = proc.trainer.answerPositions (*question, clicked, secondsSinceAsked(), &missed, &wrong);
        showResult (ok, juce::String ((int) missed.size()) + " missed, " + juce::String ((int) wrong.size()) + " wrong.");
    }

    void TrainerPanel::updateScore()
    {
        const auto& t = proc.trainer;
        const int pct = t.totalAsked() > 0 ? (int) std::round (100.0 * t.totalCorrect() / t.totalAsked()) : 0;
        scoreLabel.setText ("Asked: " + juce::String (t.totalAsked()) + "   Right: " + juce::String (pct) + "%   Streak: "
                            + juce::String (t.streak()), juce::dontSendNotification);
    }

    // -----------------------------------------------------------------------
    void TrainerPanel::fretPressed (FretPos pos)
    {
        proc.liveNoteOn (pos.string, pos.fret);
        liveString = pos.string;

        if (! question || answered) return;
        if (question->mode == QuizMode::FindTheNote || question->mode == QuizMode::ArpeggioTones)
        {
            auto it = std::find (clicked.begin(), clicked.end(), pos);
            if (it != clicked.end()) clicked.erase (it);
            else                     clicked.push_back (pos);
            if (onDisplayChanged) onDisplayChanged();
        }
    }

    void TrainerPanel::fretReleased()
    {
        proc.liveNoteOff (liveString);
        liveString = -1;
    }

    void TrainerPanel::pianoPressed (MidiNote note)
    {
        if (question && question->mode == QuizMode::NameTheNote)
            answerName (pitchClassOf (note));
    }

    std::vector<FretboardComponent::Mark> TrainerPanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        if (! question) return m;

        // Question dots. For Name and Interval questions the name must stay hidden: label "?".
        for (size_t i = 0; i < question->shown.size(); ++i)
        {
            const bool hideName = ! answered && (question->mode == QuizMode::NameTheNote || question->mode == QuizMode::IntervalName);
            const auto colour = i == 0 ? theme::chordTone : theme::root;
            m.push_back ({ question->shown[i], colour, hideName ? "?" : juce::String(), true });
        }

        for (auto p : clicked)
        {
            const bool isWrong = std::find (wrong.begin(), wrong.end(), p) != wrong.end();
            m.push_back ({ p, answered ? (isWrong ? theme::bad : theme::good) : theme::accent, answered ? juce::String() : "?", true });
        }

        for (auto p : missed)
            m.push_back ({ p, theme::bad, {}, false });

        return m;
    }

    // -----------------------------------------------------------------------
    void TrainerPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
    }

    void TrainerPanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (4); return x; };
        auto put = [] (juce::Rectangle<int>& x, juce::Component& c, int w) { c.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4); };

        auto a = row();
        put (a, modeLabel, 36);
        put (a, modeBox, 160);
        put (a, nextButton, 80);
        put (a, checkButton, 80);
        put (a, naturalsToggle, 150);
        put (a, adaptiveToggle, 160);
        put (a, heatToggle, 130);
        put (a, resetButton, 100);
        scoreLabel.setBounds (a);

        auto b = row();
        put (b, stringsLabel, 50);
        for (auto* t : stringToggles) put (b, *t, 64);
        put (b, fretsLabel, 40);
        put (b, minFret, 150);
        put (b, maxFret, 150);

        promptLabel.setBounds (row (32));
        feedbackLabel.setBounds (row (26));

        auto c = row (30);
        const bool intervals = mode() == QuizMode::IntervalName;
        const int w = intervals ? 100 : 66;
        for (auto* btn : answerButtons)
            if (btn->isVisible()) put (c, *btn, w);
    }
}
