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
            "Interval name: two dots appear. Name the distance from the blue dot up to the orange dot.\n"
            "EAR TRAINING (nothing is shown until you answer; press 'Play again' to hear it again):\n"
            "Ear: interval: two notes are played, low then high. Name the interval.\n"
            "Ear: chord type: a chord is strummed (a real guitar shape). Name its type (major, minor, 7th, ...).\n"
            "Ear: play it back: a note is played. Play the same note on your REAL guitar (turn on Guitar -> MIDI), "
            "or click it on the neck or the piano. Any octave counts.");
        modeBox.addItem ("Name the note", 1);
        modeBox.addItem ("Find the note", 2);
        modeBox.addItem ("Arpeggio tones", 3);
        modeBox.addItem ("Interval name", 4);
        modeBox.addItem ("Ear: interval", 5);
        modeBox.addItem ("Ear: chord type", 6);
        modeBox.addItem ("Ear: play it back", 7);
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

        replayButton.setTooltip ("Play again\nPlays the question once more (ear training).");
        replayButton.onClick = [this] { playQuestion(); };
        addAndMakeVisible (replayButton);

        for (int i = 0; i < 12; ++i)
        {
            auto* b = answerButtons.add (new juce::TextButton());
            b->onClick = [this, i]
            {
                if (mode() == QuizMode::IntervalName || mode() == QuizMode::EarInterval) answerInterval (i);
                else if (mode() == QuizMode::EarChord)                                    answerChord (i);
                else                                                                      answerName (i);
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
        const bool intervals = mode() == QuizMode::IntervalName || mode() == QuizMode::EarInterval;
        const bool names = mode() == QuizMode::NameTheNote || mode() == QuizMode::EarPlayBack;
        const bool chords = mode() == QuizMode::EarChord;
        const auto& qualities = earChordQualities();

        for (int i = 0; i < 12; ++i)
        {
            auto* b = answerButtons[i];
            bool visible = intervals || names;
            if (intervals)
            {
                // Ear intervals go up to the octave: the first button means "octave" there.
                const juce::String name = (i == 0 && mode() == QuizMode::EarInterval) ? juce::String ("octave") : juce::String (intervalName (i));
                b->setButtonText (name);
                b->setTooltip ("Answer: " + name + " (" + juce::String (i == 0 && mode() == QuizMode::EarInterval ? 12 : i) + " half steps)");
            }
            else if (chords)
            {
                visible = i < (int) qualities.size();
                if (visible)
                {
                    b->setButtonText (juce::String (chordQualityName (qualities[(size_t) i])));
                    b->setTooltip ("Answer: " + b->getButtonText() + " chord");
                }
            }
            else
            {
                b->setButtonText (juce::String (pitchClassName (i, false)) + (pitchClassName (i, false) != pitchClassName (i, true)
                                  ? "/" + juce::String (pitchClassName (i, true)) : juce::String()));
                b->setTooltip ("Answer: " + b->getButtonText() + "\nYou can also click a piano key with this note name"
                               + juce::String (mode() == QuizMode::EarPlayBack ? ", a spot on the neck, or play it on your guitar." : "."));
            }
            b->setVisible (visible);
        }
        checkButton.setVisible (! intervals && ! names && ! chords);
        replayButton.setVisible (isEarQuiz (mode()));
        resized();
    }

    void TrainerPanel::playQuestion()
    {
        if (! question) return;
        // Chords are strummed (40 ms between strings), intervals are played one note after the other.
        const int token = ++playToken;
        juce::Component::SafePointer<TrainerPanel> safe (this);
        const bool together = question->playTogether;
        const auto spots = question->shown;
        int delay = 0;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            const auto p = spots[i];
            juce::Timer::callAfterDelay (delay, [safe, token, p]
            {
                if (safe != nullptr && safe->playToken == token) safe->proc.liveNoteOn (p.string, p.fret);
            });
            const int length = together ? 1400 : 650;
            juce::Timer::callAfterDelay (delay + length, [safe, token, p]
            {
                if (safe != nullptr && safe->playToken == token) safe->proc.liveNoteOff (p.string);
            });
            delay += together ? 40 : 700;
        }
        ignoreUntil = juce::Time::getMillisecondCounterHiRes() + delay + (together ? 1500 : 700);
    }

    void TrainerPanel::heard (const std::set<MidiNote>& sounding)
    {
        // "Ear: play it back": a new note from the guitar (or the DAW) answers the question.
        std::set<MidiNote> fresh;
        for (auto n : sounding) if (! lastHeard.count (n)) fresh.insert (n);
        lastHeard = sounding;
        if (! question || answered || question->mode != QuizMode::EarPlayBack || fresh.empty()) return;
        if (juce::Time::getMillisecondCounterHiRes() < ignoreUntil) return;
        answerName (pitchClassOf (*fresh.begin()));
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

        // Play the shown note(s), so you also train your ear (ear quizzes: strummed or one after another).
        if (isEarQuiz (question->mode))
            playQuestion();
        else
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
        if (! question || answered || (question->mode != QuizMode::NameTheNote && question->mode != QuizMode::EarPlayBack)) return;
        const bool ok = proc.trainer.answerPitchClass (*question, pc, secondsSinceAsked());
        showResult (ok, (question->mode == QuizMode::EarPlayBack && ! ok ? "You played " + juce::String (pitchClassName (pc)) + ". " : juce::String())
                        + "It is " + juce::String (pitchClassName (question->targetPc)) + ".");
    }

    void TrainerPanel::answerInterval (int semitones)
    {
        if (! question || answered || (question->mode != QuizMode::IntervalName && question->mode != QuizMode::EarInterval)) return;
        const bool ok = proc.trainer.answerInterval (*question, semitones, secondsSinceAsked());
        showResult (ok, "It is " + juce::String (question->interval == 12 ? "an octave" : "a " + juce::String (intervalName (question->interval))) + ".");
    }

    void TrainerPanel::answerChord (int index)
    {
        const auto& qualities = earChordQualities();
        if (! question || answered || question->mode != QuizMode::EarChord || index < 0 || index >= (int) qualities.size()) return;
        const bool ok = proc.trainer.answerChordQuality (*question, qualities[(size_t) index], secondsSinceAsked());
        showResult (ok, "It is " + juce::String (question->chord.name()) + " (" + juce::String (chordQualityName (question->chord.quality)) + ").");
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
        if (question->mode == QuizMode::EarPlayBack)
        {
            answerName (pitchClassOf (proc.instrument.noteAt (pos)));
            return;
        }
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
        if (question && (question->mode == QuizMode::NameTheNote || question->mode == QuizMode::EarPlayBack))
            answerName (pitchClassOf (note));
    }

    std::vector<FretboardComponent::Mark> TrainerPanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        if (! question) return m;

        // Ear training: nothing on the neck until you answer, then where the notes were.
        if (isEarQuiz (question->mode))
        {
            if (answered)
                for (size_t i = 0; i < question->shown.size(); ++i)
                    m.push_back ({ question->shown[i], i + 1 == question->shown.size() ? theme::root : theme::chordTone, {}, true });
            return m;
        }

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
        if (checkButton.isVisible())  put (a, checkButton, 80);
        if (replayButton.isVisible()) put (a, replayButton, 90);
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
        const bool intervals = mode() == QuizMode::IntervalName || mode() == QuizMode::EarInterval || mode() == QuizMode::EarChord;
        const int w = intervals ? 100 : 66;
        for (auto* btn : answerButtons)
            if (btn->isVisible()) put (c, *btn, w);
    }
}
