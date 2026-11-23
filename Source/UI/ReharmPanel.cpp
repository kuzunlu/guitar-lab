#include "UI/ReharmPanel.h"

#include <cmath>

namespace gl
{
    namespace
    {
        // Short degree name of a note inside a chord: R, 3, b7, 9 ...
        juce::String degreeLabel (int semitonesFromRoot)
        {
            static const char* const names[12] = { "R", "b9", "9", "b3", "3", "11", "b5", "5", "#5", "6", "b7", "7" };
            return names[wrap12 (semitonesFromRoot)];
        }

        juce::String notesOf (const Chord& c, bool flats)
        {
            juce::StringArray names;
            for (auto pc : c.pitchClasses())
                names.add (juce::String (pitchClassName (pc, flats)));
            return names.joinIntoString (" ");
        }
    }

    // =======================================================================
    // ChordStrip
    // =======================================================================
    juce::Rectangle<float> ChordStrip::cardBounds (int i) const
    {
        auto area = getLocalBounds().toFloat().withTrimmedTop (18.0f).reduced (2.0f);
        double total = 0;
        for (auto& c : chords) total += c.beats;
        if (dragEdge >= 0) total = std::max (total, dragTotal);
        if (total <= 0) return {};

        double before = 0;
        for (int k = 0; k < i; ++k) before += chords[(size_t) k].beats;

        const float x = area.getX() + (float) (before / total) * area.getWidth();
        const float w = (float) (chords[(size_t) i].beats / total) * area.getWidth();
        return { x + 2.0f, area.getY(), w - 4.0f, area.getHeight() };
    }

    int ChordStrip::cardAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) chords.size(); ++i)
            if (cardBounds (i).contains (p))
                return i;
        return -1;
    }

    void ChordStrip::paint (juce::Graphics& g)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centredLeft);

        for (int i = 0; i < (int) chords.size(); ++i)
        {
            const auto& tc = chords[(size_t) i];
            auto r = cardBounds (i);

            g.setColour (tc.substituted ? theme::accent.withAlpha (0.16f) : theme::panelLight);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (i == selected ? theme::sounding : (tc.substituted ? theme::accent : theme::panelLight.brighter (0.3f)));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, i == selected ? 2.5f : 1.0f);

            const bool narrow = r.getWidth() < 64;
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (narrow ? 13.0f : 18.0f, juce::Font::bold));
            g.drawText (juce::String (tc.chord.name (key.prefersFlats())), r.withHeight (r.getHeight() * 0.5f).translated (0, 4),
                        juce::Justification::centred, true);

            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (narrow ? 10.0f : 12.0f));
            g.drawText (juce::String (romanLabel (tc.chord, key)) + (narrow ? "" : "   " + juce::String (tc.beats, tc.beats == std::floor (tc.beats) ? 0 : 1) + " beats"),
                        r.withTrimmedTop (r.getHeight() * 0.5f).withHeight (r.getHeight() * 0.25f), juce::Justification::centred, true);

            if (tc.substituted && ! narrow)
            {
                g.setColour (theme::accent.withAlpha (0.9f));
                g.setFont (juce::FontOptions (10.5f));
                g.drawText (juce::String (tc.reason), r.withTrimmedTop (r.getHeight() * 0.72f).reduced (3, 0),
                            juce::Justification::centred, true);
            }
        }
    }

    int ChordStrip::edgeAt (juce::Point<float> p) const
    {
        if (onLengthsChanged == nullptr) return -1;
        for (int i = 0; i < (int) chords.size(); ++i)
        {
            const auto r = cardBounds (i);
            if (p.y >= r.getY() && p.y <= r.getBottom() && std::abs (p.x - (r.getRight() + 2.0f)) <= 5.0f)
                return i;
        }
        return -1;
    }

    void ChordStrip::mouseMove (const juce::MouseEvent& e)
    {
        const bool onEdge = edgeAt (e.position) >= 0;
        setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        if (const int card = onEdge ? -1 : cardAt (e.position); card != hovered)
        {
            hovered = card;
            if (onHover) onHover (card);
        }
    }

    void ChordStrip::mouseExit (const juce::MouseEvent&)
    {
        if (hovered < 0) return;
        hovered = -1;
        if (onHover) onHover (-1);
    }

    void ChordStrip::mouseDown (const juce::MouseEvent& e)
    {
        dragEdge = edgeAt (e.position);
        if (dragEdge >= 0)
        {
            dragStartBeats.clear();
            dragTotal = 0.0;
            for (auto& c : chords) { dragStartBeats.push_back (c.beats); dragTotal += c.beats; }
            const auto area = getLocalBounds().toFloat().withTrimmedTop (18.0f).reduced (2.0f);
            dragPxPerBeat = (float) (area.getWidth() / std::max (0.5, dragTotal));
            return;
        }
        if (onClick)
            onClick (cardAt (e.position));
    }

    void ChordStrip::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragEdge < 0 || dragEdge >= (int) chords.size()) return;

        // Half-beat steps (Shift: quarter beats). Every chord keeps at least one step.
        const double step = e.mods.isShiftDown() ? 0.25 : 0.5;
        double delta = std::round (e.getDistanceFromDragStartX() / dragPxPerBeat / step) * step;
        const int i = dragEdge;
        const bool last = i + 1 >= (int) chords.size();
        delta = std::max (delta, step - dragStartBeats[(size_t) i]);
        if (! last) delta = std::min (delta, dragStartBeats[(size_t) i + 1] - step);

        chords[(size_t) i].beats = dragStartBeats[(size_t) i] + delta;
        if (! last) chords[(size_t) i + 1].beats = dragStartBeats[(size_t) i + 1] - delta;
        repaint();
    }

    void ChordStrip::mouseUp (const juce::MouseEvent&)
    {
        if (dragEdge < 0) return;
        dragEdge = -1;
        std::vector<double> beats;
        for (auto& c : chords) beats.push_back (c.beats);
        if (beats != dragStartBeats && onLengthsChanged)
            onLengthsChanged (beats);
        repaint();
    }

    juce::String ChordStrip::getTooltip()
    {
        const int i = cardAt (getMouseXYRelative().toFloat());
        if (edgeAt (getMouseXYRelative().toFloat()) >= 0)
            return "Change the length\nDrag this line left or right to make the chord before it shorter or longer "
                   "(the next chord gets the rest). Half-beat steps; hold Shift for quarter beats.";
        if (i < 0)
            return title + "\nClick a chord to hear it, see its shape and list only its suggestions. "
                           "Click empty space to list all suggestions again.\n"
                           "Drag the line between two chords to change their lengths.";

        const auto& tc = chords[(size_t) i];
        juce::String tip = juce::String (tc.chord.name (key.prefersFlats())) + "   (" + juce::String (romanLabel (tc.chord, key))
                         + " in " + juce::String (key.name()) + ")\n"
                         + "Notes: " + notesOf (tc.chord, key.prefersFlats()) + "\n"
                         + "Length: " + juce::String (tc.beats, 2) + " beats";
        if (tc.substituted)
            tip << "\nWhy: " << juce::String (tc.reason);
        tip << "\nPoint at it to see its shape. Click to hear it, show its voicing on the fretboard and list only its suggestions.";
        return tip;
    }

    // =======================================================================
    // ReharmPanel
    // =======================================================================
    ReharmPanel::ReharmPanel (GuitarLabProcessor& p, FretboardComponent& b, PianoComponent& pi)
        : proc (p), board (b), piano (pi)
    {
        auto label = [this] (juce::Label& l, const juce::String& text, const juce::String& tip)
        {
            l.setText (text, juce::dontSendNotification);
            l.setTooltip (tip);
            addAndMakeVisible (l);
        };

        label (inputLabel, "Progression", "Progression\n"
            "Type chords split by spaces, dashes, commas or bar lines.\n"
            "Roman numerals (in the key on the right): I V vi IV, ii7 V7 Imaj7, bVII, iv, V/V, viio7, iiø7\n"
            "Or chord names: C G Am F, Dm7 G7 Cmaj7, F#m7b5, Bb7#9, Esus4\n"
            "UPPER case numeral = major, lower case = minor.\n"
            "Lengths: each chord gets one bar (4 beats). Write C:2 for 2 beats (any length: G:1.5, F:8).\n"
            "Bar lines share a bar: | C G | Am | = C and G 2 beats each, Am 4 beats.\n"
            "You can also drag the line between two chord cards below.");
        input.setText (proc.reharmInput, juce::dontSendNotification);
        input.setTooltip (inputLabel.getTooltip());
        input.onReturnKey = [this] { parseInput(); };
        input.onFocusLost = [this] { if (input.getText() != proc.reharmInput) parseInput(); }; // leaving the tab must not reset the result
        addAndMakeVisible (input);

        label (keyLabel, "Key", "Key\nThe key of the song. Roman numerals are read in this key, and the engine uses it to decide "
                                "which chords are 'borrowed' or 'diatonic'.\nMinor keys use the natural minor scale.");
        for (int pc = 0; pc < 12; ++pc)
            keyRoot.addItem (juce::String (pitchClassName (pc, keyPrefersFlats (pc, false))), pc + 1);
        keyRoot.setSelectedId (proc.reharmKeyTonic + 1, juce::dontSendNotification);
        keyRoot.setTooltip (keyLabel.getTooltip());
        keyRoot.onChange = [this] { parseInput(); };
        addAndMakeVisible (keyRoot);

        keyMode.addItem ("Major", 1);
        keyMode.addItem ("Minor", 2);
        keyMode.setSelectedId (proc.reharmKeyMinor ? 2 : 1, juce::dontSendNotification);
        keyMode.setTooltip (keyLabel.getTooltip());
        keyMode.onChange = [this] { parseInput(); };
        addAndMakeVisible (keyMode);

        for (int k = 0; k < (int) SubKind::NumKinds; ++k)
        {
            auto* t = kindToggles.add (new juce::ToggleButton (juce::String (subKindName ((SubKind) k))));
            t->setToggleState (true, juce::dontSendNotification);
            t->setTooltip (juce::String (subKindDescription ((SubKind) k))
                           + "\n\nTick = 'Reharmonize' may use this kind of change. The suggestion list below is filtered the same way.");
            t->onClick = [this] { suggestionList.updateContent(); suggestionList.repaint(); if (onHeightChanged) onHeightChanged(); };
            addAndMakeVisible (t);
        }

        label (amountLabel, "Amount", "Amount of change\n0% = keep every chord. 100% = try to change every chord.\n"
                                      "Each chord gets at most one change, chosen at random from the ticked kinds.");
        amount.setRange (0, 100, 1);
        amount.setValue (50, juce::dontSendNotification);
        amount.setTextValueSuffix (" %");
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        amount.setTooltip (amountLabel.getTooltip());
        addAndMakeVisible (amount);

        theme::primary (reharmButton);
        reharmButton.setTooltip ("Reharmonize\nBuilds a new version of your progression using the ticked substitution kinds and the amount.\n"
                                 "Changed chords have a yellow border and show why they were chosen.");
        reharmButton.onClick = [this] { runReharm(); };
        addAndMakeVisible (reharmButton);

        newIdeaButton.setTooltip ("New idea\nSame settings, different random choices. Press it again and again until you like one.");
        newIdeaButton.onClick = [this] { ++seed; runReharm(); };
        addAndMakeVisible (newIdeaButton);

        resetButton.setTooltip ("Reset\nGoes back to your original progression.");
        resetButton.onClick = [this] { result = original; revoice(); };
        addAndMakeVisible (resetButton);

        // Voicing options
        label (spanLabel, "Hand stretch", "Hand stretch (frets)\nThe largest number of frets the voicing may cover. "
                                          "4 = normal hand. 5 = wide stretch. 3 = very compact shapes.");
        span.setRange (3, 6, 1);
        span.setValue (4, juce::dontSendNotification);
        span.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
        span.setTooltip (spanLabel.getTooltip());
        span.onValueChange = [this] { revoice(); };
        addAndMakeVisible (span);

        label (areaLabel, "Neck area", "Neck area (fret)\nWhere on the neck the voicings should stay. 0-3 = open position, "
                                       "5-9 = middle of the neck, 12+ = high up.\nThe engine then picks shapes that move as little as possible "
                                       "from chord to chord (voice leading).");
        area.setRange (0, 20, 1);
        area.setValue (3, juce::dontSendNotification);
        area.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 20);
        area.setTooltip (areaLabel.getTooltip());
        area.onValueChange = [this] { revoice(); };
        addAndMakeVisible (area);

        rootBass.setToggleState (true, juce::dontSendNotification);
        rootBass.setTooltip ("Root in bass\nOn: the lowest note of every voicing is the chord's root (clear, solid sound).\n"
                             "Off: inversions are allowed too (smoother bass lines, more shapes).");
        rootBass.onClick = [this] { revoice(); };
        addAndMakeVisible (rootBass);

        openStrings.setToggleState (true, juce::dontSendNotification);
        openStrings.setTooltip ("Open strings\nOn: voicings may use open strings (only when the neck area includes the open position).\n"
                                "Off: only fretted notes, so every shape can be moved to other keys.");
        openStrings.onClick = [this] { revoice(); };
        addAndMakeVisible (openStrings);

        nextVoicingButton.setTooltip ("Next voicing\nShows another playable shape for the selected chord (in the bottom row). "
                                      "The new shape is used when you send the progression to the phrase.");
        nextVoicingButton.onClick = [this] { nextVoicing(); };
        addAndMakeVisible (nextVoicingButton);

        sendButton.setTooltip ("Send to phrase\nWrites the reharmonized progression into the phrase as strummed chords "
                               "(one chord per card, using the voicings shown), so it plays in your DAW and can be exported as MIDI.\n"
                               "Warning: this REPLACES the notes in the current phrase.");
        sendButton.onClick = [this] { sendToPhrase(); };
        addAndMakeVisible (sendButton);

        errorLabel.setColour (juce::Label::textColourId, theme::bad);
        addAndMakeVisible (errorLabel);

        originalStrip.title = "Your progression";
        originalStrip.onClick = [this] (int i) { selectCard (false, i); };
        originalStrip.onLengthsChanged = [this] (const std::vector<double>& beats)
        {
            // Same as typing the new lengths: the text is rewritten and read again.
            for (size_t i = 0; i < beats.size() && i < original.size(); ++i) original[i].beats = beats[i];
            input.setText (progressionToText (original, currentKey().prefersFlats()), juce::dontSendNotification);
            parseInput();
        };
        originalStrip.onHover = [this] (int i) { showHoverShape (false, i); };
        addAndMakeVisible (originalStrip);

        resultStrip.title = "Reharmonized (yellow = changed)";
        resultStrip.onClick = [this] (int i) { selectCard (true, i); };
        resultStrip.onLengthsChanged = [this] (const std::vector<double>& beats)
        {
            for (size_t i = 0; i < beats.size() && i < result.size(); ++i) result[i].beats = beats[i];
            revoice();
        };
        resultStrip.onHover = [this] (int i) { showHoverShape (true, i); };
        addAndMakeVisible (resultStrip);

        hoverShape.setInterceptsMouseClicks (false, false); // the mouse keeps moving over the cards below it
        addChildComponent (hoverShape);

        label (suggestionsLabel, "Single suggestions (double-click to apply to the bottom row)",
               "Suggestion list\nEvery single change the engine found for the bottom row, with a short reason.\n"
               "Double-click one (or select it and press 'Apply suggestion') to apply just that change.\n"
               "Only ticked kinds are listed.");
        suggestionList.setRowHeight (22);
        suggestionList.setColour (juce::ListBox::backgroundColourId, theme::panel);
        suggestionList.setTooltip (suggestionsLabel.getTooltip());
        addAndMakeVisible (suggestionList);

        showAllButton.setTooltip ("Show all\nThe list shows only the suggestions for the selected chord. "
                                  "Click to list the suggestions for every chord again.");
        showAllButton.onClick = [this] { selectCard (selectedInResult, -1); };
        addChildComponent (showAllButton);

        applyButton.setTooltip ("Apply suggestion\nApplies the selected suggestion to the bottom row.");
        applyButton.onClick = [this]
        {
            const int row = suggestionList.getSelectedRow();
            if (row >= 0) applyRow (row);
        };
        addAndMakeVisible (applyButton);

        memory.remember (amount, "amount");
        memory.remember (span, "stretch");
        memory.remember (area, "area");
        memory.remember (rootBass, "rootBass");
        memory.remember (openStrings, "openStrings");
        for (int k = 0; k < kindToggles.size(); ++k)
            memory.remember (*kindToggles[k], juce::Identifier ("kind" + juce::String (k)));
        refresh();
    }

    juce::String ReharmPanel::workKey() const
    {
        juce::StringArray tuning;
        for (auto n : proc.instrument.tuning()) tuning.add (juce::String (n));
        return input.getText() + "|" + juce::String (keyRoot.getSelectedId()) + "|" + juce::String (keyMode.getSelectedId())
             + "|" + tuning.joinIntoString (",");
    }

    // -----------------------------------------------------------------------
    Key ReharmPanel::currentKey() const
    {
        return { keyRoot.getSelectedId() - 1, keyMode.getSelectedId() == 2 };
    }

    VoicingOptions ReharmPanel::voicingOptions() const
    {
        VoicingOptions o;
        o.maxSpan = (int) span.getValue();
        o.rootInBass = rootBass.getToggleState();
        o.allowOpenStrings = openStrings.getToggleState();
        o.minFret = std::max (0, (int) area.getValue() - 3);
        o.maxFret = std::min (kNumFrets, (int) area.getValue() + 7);
        return o;
    }

    void ReharmPanel::parseInput()
    {
        readOriginal();
        result = original;
        selectedIndex = -1;
        revoice();
    }

    void ReharmPanel::readOriginal()
    {
        proc.reharmInput    = input.getText();
        proc.reharmKeyTonic = keyRoot.getSelectedId() - 1;
        proc.reharmKeyMinor = keyMode.getSelectedId() == 2;

        std::string err;
        const auto chords = parseTimedProgression (input.getText().toStdString(), currentKey(), &err);
        errorLabel.setText (chords.empty() ? juce::String (err) : juce::String(), juce::dontSendNotification);

        original = chords;
    }

    void ReharmPanel::runReharm()
    {
        ReharmOptions o;
        for (int k = 0; k < (int) SubKind::NumKinds; ++k)
            o.enabled[k] = kindToggles[k]->getToggleState();
        o.amount = amount.getValue() / 100.0;
        o.seed = seed;

        result = reharmonize (original, currentKey(), o);
        selectedIndex = -1;
        revoice();
    }

    void ReharmPanel::revoice()
    {
        std::vector<Chord> chords;
        for (auto& tc : result) chords.push_back (tc.chord);

        voicings = voiceLead (chords, proc.instrument, voicingOptions(), (int) area.getValue());
        voicingChoice.assign (result.size(), -1);
        updateView();
    }

    void ReharmPanel::showHoverShape (bool resultRow, int index)
    {
        const auto& chords = resultRow ? result : original;
        Voicing v;
        if (index >= 0 && index < (int) chords.size())
        {
            if (resultRow && index < (int) voicings.size())
                v = voicings[(size_t) index];  // the voicing the bottom row really uses
            else if (auto found = findVoicings (chords[(size_t) index].chord, proc.instrument, voicingOptions(), 1); ! found.empty())
                v = found.front();
        }
        if (v.frets.empty())
        {
            hoverShape.setVisible (false);
            return;
        }

        hoverShape.shapes = { v };
        hoverShape.inst = proc.instrument;
        hoverShape.root = chords[(size_t) index].chord.root;
        hoverShape.flats = currentKey().prefersFlats();

        // Next to the card: below the bottom row, above the top row (or the other way when there is no room).
        auto& strip = resultRow ? resultStrip : originalStrip;
        const auto card = getLocalArea (&strip, strip.cardArea (index));
        const int w = ShapeGrid::kCardW, h = ShapeGrid::kCardH;
        int y = resultRow ? card.getBottom() + 4 : card.getY() - h - 4;
        if (y < 0) y = card.getBottom() + 4;
        if (y + h > getHeight()) y = std::max (0, card.getY() - h - 4);
        hoverShape.setBounds (juce::jlimit (0, std::max (0, getWidth() - w), card.getCentreX() - w / 2), y, w, h);
        hoverShape.setVisible (true);
        hoverShape.toFront (false);
        hoverShape.repaint();
    }

    void ReharmPanel::updateView()
    {
        proc.work.reharmFor = workKey();
        hoverShape.setVisible (false);

        // Suggestions for the current bottom row.
        suggestions = findSuggestions (result, currentKey());

        originalStrip.chords = original;
        originalStrip.key = currentKey();
        resultStrip.chords = result;
        resultStrip.key = currentKey();
        originalStrip.selected = selectedInResult ? -1 : selectedIndex;
        resultStrip.selected   = selectedInResult ? selectedIndex : -1;

        if (selectedInResult && selectedIndex >= 0 && selectedIndex < (int) voicings.size())
            shownVoicing = voicings[(size_t) selectedIndex];
        else if (selectedIndex < 0)
            shownVoicing = {};

        suggestionList.updateContent();
        updateSuggestionHeader();
        repaint();
        if (onDisplayChanged) onDisplayChanged();
        if (onHeightChanged) onHeightChanged();
    }

    void ReharmPanel::selectCard (bool resultRow, int index)
    {
        selectedInResult = resultRow;
        selectedIndex = index;
        suggestionList.deselectAllRows();
        suggestionList.updateContent();
        updateSuggestionHeader();
        if (onHeightChanged) onHeightChanged();

        if (index < 0)
        {
            shownVoicing = {};
            originalStrip.selected = resultStrip.selected = -1;
            originalStrip.repaint();
            resultStrip.repaint();
            if (onDisplayChanged) onDisplayChanged();
            return;
        }

        if (resultRow && index < (int) voicings.size())
            shownVoicing = voicings[(size_t) index];
        else if (! resultRow && index < (int) original.size())
        {
            auto found = findVoicings (original[(size_t) index].chord, proc.instrument, voicingOptions(), 1);
            shownVoicing = found.empty() ? Voicing{} : found.front();
        }

        originalStrip.selected = resultRow ? -1 : index;
        resultStrip.selected   = resultRow ? index : -1;
        originalStrip.repaint();
        resultStrip.repaint();

        playVoicing (shownVoicing);
        if (onDisplayChanged) onDisplayChanged();
    }

    void ReharmPanel::nextVoicing()
    {
        if (! selectedInResult || selectedIndex < 0 || selectedIndex >= (int) result.size())
            return;

        const auto alts = findVoicings (result[(size_t) selectedIndex].chord, proc.instrument, voicingOptions(), 16);
        if (alts.empty()) return;

        auto& choice = voicingChoice[(size_t) selectedIndex];
        choice = (choice + 1) % (int) alts.size();
        voicings[(size_t) selectedIndex] = alts[(size_t) choice];
        shownVoicing = voicings[(size_t) selectedIndex];

        playVoicing (shownVoicing);
        if (onDisplayChanged) onDisplayChanged();
    }

    void ReharmPanel::playVoicing (const Voicing& v)
    {
        proc.allLiveNotesOff(); // a new chord cuts off the one before
        for (int s = 0; s < (int) v.frets.size(); ++s)
            if (v.frets[(size_t) s] != kMuted)
                proc.liveNoteOn (s, v.frets[(size_t) s]);

        const int token = ++playToken;
        juce::Component::SafePointer<ReharmPanel> safe (this);
        const int strings = (int) v.frets.size();
        juce::Timer::callAfterDelay (1400, [safe, token, strings]
        {
            if (safe == nullptr || safe->playToken != token) return;
            for (int s = 0; s < strings; ++s)
                safe->proc.liveNoteOff (s);
        });
    }

    void ReharmPanel::sendToPhrase()
    {
        if (result.empty()) return;

        const double bpm = proc.params.getRawParameterValue (ParamIDs::internalBpm)->load();
        proc.phrase.clear();

        double beat = 0;
        for (size_t i = 0; i < result.size() && i < voicings.size(); ++i)
        {
            proc.phrase.addChord (voicings[i], beat, result[i].beats, 96, true, 20.0, bpm);
            beat += result[i].beats;
        }
        proc.phrase.lengthBeats = std::max (4.0, std::ceil (beat / 4.0) * 4.0);
        proc.phraseChanged();

        if (onPhraseReplaced) onPhraseReplaced();
    }

    std::vector<FretboardComponent::Mark> ReharmPanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        const auto& chords = selectedInResult ? result : original;
        if (selectedIndex < 0 || selectedIndex >= (int) chords.size())
            return m;

        const auto root = chords[(size_t) selectedIndex].chord.root;
        for (auto p : shownVoicing.positions())
        {
            if (! proc.instrument.isValid (p)) continue;
            const int interval = wrap12 (pitchClassOf (proc.instrument.noteAt (p)) - root);
            m.push_back ({ p, interval == 0 ? theme::root : theme::chordTone, degreeLabel (interval), true });
        }
        return m;
    }

    std::map<PitchClass, juce::Colour> ReharmPanel::chordLayer() const
    {
        std::map<PitchClass, juce::Colour> layer;
        const auto& chords = selectedInResult ? result : original;
        if (selectedIndex < 0 || selectedIndex >= (int) chords.size())
            return layer;

        const auto& c = chords[(size_t) selectedIndex].chord;
        for (auto pc : c.pitchClasses())
            layer[pc] = pc == c.root ? theme::root : theme::chordTone;
        return layer;
    }

    void ReharmPanel::fretPressed (FretPos pos)
    {
        proc.liveNoteOn (pos.string, pos.fret);
        liveString = pos.string;
    }

    void ReharmPanel::fretReleased()
    {
        proc.liveNoteOff (liveString);
        liveString = -1;
    }

    void ReharmPanel::refresh()
    {
        input.setText (proc.reharmInput, juce::dontSendNotification);
        keyRoot.setSelectedId (proc.reharmKeyTonic + 1, juce::dontSendNotification);
        keyMode.setSelectedId (proc.reharmKeyMinor ? 2 : 1, juce::dontSendNotification);

        // The window was closed and opened again (same input, key and tuning): keep the result, the voicings
        // and the selection as they were. Otherwise start again from the typed progression.
        if (proc.work.reharmFor == workKey() && ! result.empty() && voicings.size() == result.size())
        {
            readOriginal();
            updateView();
        }
        else
            parseInput();
    }

    void ReharmPanel::restoreUi()
    {
        memory.restoreAll();
        refresh();
    }

    // -----------------------------------------------------------------------
    // Suggestion list (only ticked kinds)
    // -----------------------------------------------------------------------
    int ReharmPanel::filterIndex() const
    {
        if (selectedIndex < 0) return -1;
        if (selectedInResult) return selectedIndex < (int) result.size() ? selectedIndex : -1;
        if (selectedIndex >= (int) original.size()) return -1;

        // A chord in the top row: the bottom-row chord that sounds at the same beat.
        double start = 0.0;
        for (int k = 0; k < selectedIndex; ++k) start += original[(size_t) k].beats;
        double at = 0.0;
        for (int k = 0; k < (int) result.size(); ++k)
        {
            if (start < at + result[(size_t) k].beats - 1.0e-9) return k;
            at += result[(size_t) k].beats;
        }
        return -1;
    }

    bool ReharmPanel::isListed (const Suggestion& s) const
    {
        const int only = filterIndex();
        return kindToggles[(int) s.kind]->getToggleState() && (only < 0 || s.index == only);
    }

    const Suggestion* ReharmPanel::listedSuggestion (int row) const
    {
        for (auto& s : suggestions)
            if (isListed (s) && row-- == 0)
                return &s;
        return nullptr;
    }

    void ReharmPanel::updateSuggestionHeader()
    {
        const int only = filterIndex();
        showAllButton.setVisible (only >= 0);
        suggestionsLabel.setText (only < 0 ? juce::String ("Single suggestions (double-click to apply to the bottom row)")
                                           : "Suggestions for chord " + juce::String (only + 1) + " ("
                                             + juce::String (result[(size_t) only].chord.name (currentKey().prefersFlats()))
                                             + ") only - double-click to apply",
                                  juce::dontSendNotification);
    }

    int ReharmPanel::getNumRows()
    {
        int count = 0;
        for (auto& s : suggestions)
            if (isListed (s)) ++count;
        return count;
    }

    void ReharmPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
    {
        const auto* s = listedSuggestion (row);
        if (s == nullptr) return;

        if (isSelected)
            g.fillAll (theme::accent.withAlpha (0.2f));

        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("Chord " + juce::String (s->index + 1) + "  " + juce::String (subKindName (s->kind)),
                    4, 0, 190, height, juce::Justification::centredLeft);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (juce::String (s->explanation), 196, 0, width - 200, height, juce::Justification::centredLeft, true);
    }

    void ReharmPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        applyRow (row);
    }

    void ReharmPanel::applyRow (int row)
    {
        const auto* s = listedSuggestion (row);
        if (s == nullptr) return;

        result = applySuggestion (result, *s);
        selectedIndex = -1;
        revoice();
    }

    juce::String ReharmPanel::getTooltipForRow (int row)
    {
        const auto* s = listedSuggestion (row);
        if (s == nullptr) return {};
        return juce::String (subKindName (s->kind)) + " on chord " + juce::String (s->index + 1) + "\n"
             + juce::String (s->explanation) + "\n\n" + juce::String (subKindDescription (s->kind))
             + "\n\nDouble-click to apply.";
    }

    int ReharmPanel::heightFor (int)
    {
        // Same rows as resized(): margin, three rows of controls, the two chord rows, the list header, then every
        // suggestion (at least a few rows, so the empty list still looks like a list).
        const int rows = std::max (6, getNumRows());
        return 6 + 30 + 26 + 30 + kStripsHeight + 4 + 24 + rows * suggestionList.getRowHeight() + 4 + 6;
    }

    // -----------------------------------------------------------------------
    void ReharmPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
    }

    void ReharmPanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (4); return x; };
        auto put = [] (juce::Rectangle<int>& x, juce::Component& c, int w) { c.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4); };

        auto a = row();
        put (a, inputLabel, 80);
        put (a, input, 330);
        put (a, keyLabel, 30);
        put (a, keyRoot, 70);
        put (a, keyMode, 80);
        put (a, amountLabel, 56);
        put (a, amount, 170);
        put (a, reharmButton, 100);
        put (a, newIdeaButton, 80);
        put (a, resetButton, 60);
        errorLabel.setBounds (a);

        auto b = row (22);
        for (auto* t : kindToggles)
            put (b, *t, 150);

        auto c = row();
        put (c, spanLabel, 86);   put (c, span, 120);
        put (c, areaLabel, 70);   put (c, area, 160);
        put (c, rootBass, 110);
        put (c, openStrings, 110);
        put (c, nextVoicingButton, 100);
        put (c, sendButton, 120);

        auto strips = r.removeFromTop (kStripsHeight);
        originalStrip.setBounds (strips.removeFromTop (strips.getHeight() / 2));
        resultStrip.setBounds (strips);
        r.removeFromTop (4);

        auto listHeader = row (20);
        applyButton.setBounds (listHeader.removeFromRight (130));
        listHeader.removeFromRight (4);
        showAllButton.setBounds (listHeader.removeFromRight (80));
        suggestionsLabel.setBounds (listHeader);
        suggestionList.setBounds (r);
    }
}
