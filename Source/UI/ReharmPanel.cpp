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

    void ChordStrip::mouseDown (const juce::MouseEvent& e)
    {
        const int i = cardAt (e.position);
        if (i >= 0 && onClick)
            onClick (i);
    }

    juce::String ChordStrip::getTooltip()
    {
        const int i = cardAt (getMouseXYRelative().toFloat());
        if (i < 0)
            return title + "\nClick a chord to hear it and see its shape on the fretboard and piano.";

        const auto& tc = chords[(size_t) i];
        juce::String tip = juce::String (tc.chord.name (key.prefersFlats())) + "   (" + juce::String (romanLabel (tc.chord, key))
                         + " in " + juce::String (key.name()) + ")\n"
                         + "Notes: " + notesOf (tc.chord, key.prefersFlats()) + "\n"
                         + "Length: " + juce::String (tc.beats, 2) + " beats";
        if (tc.substituted)
            tip << "\nWhy: " << juce::String (tc.reason);
        tip << "\nClick to hear it and show its voicing on the fretboard.";
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
            "UPPER case numeral = major, lower case = minor. Each chord gets one bar (4 beats).");
        input.setText (proc.reharmInput, juce::dontSendNotification);
        input.setTooltip (inputLabel.getTooltip());
        input.onReturnKey = [this] { parseInput(); };
        input.onFocusLost = [this] { parseInput(); };
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
            t->onClick = [this] { suggestionList.updateContent(); suggestionList.repaint(); };
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
        addAndMakeVisible (originalStrip);

        resultStrip.title = "Reharmonized (yellow = changed)";
        resultStrip.onClick = [this] (int i) { selectCard (true, i); };
        addAndMakeVisible (resultStrip);

        label (suggestionsLabel, "Single suggestions (double-click to apply to the bottom row)",
               "Suggestion list\nEvery single change the engine found for the bottom row, with a short reason.\n"
               "Double-click one (or select it and press 'Apply suggestion') to apply just that change.\n"
               "Only ticked kinds are listed.");
        suggestionList.setRowHeight (22);
        suggestionList.setColour (juce::ListBox::backgroundColourId, theme::panel);
        suggestionList.setTooltip (suggestionsLabel.getTooltip());
        addAndMakeVisible (suggestionList);

        applyButton.setTooltip ("Apply suggestion\nApplies the selected suggestion to the bottom row.");
        applyButton.onClick = [this]
        {
            const int row = suggestionList.getSelectedRow();
            if (row >= 0) applyRow (row);
        };
        addAndMakeVisible (applyButton);

        parseInput();
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
        proc.reharmInput    = input.getText();
        proc.reharmKeyTonic = keyRoot.getSelectedId() - 1;
        proc.reharmKeyMinor = keyMode.getSelectedId() == 2;

        std::string err;
        const auto chords = parseProgression (input.getText().toStdString(), currentKey(), &err);
        errorLabel.setText (chords.empty() ? juce::String (err) : juce::String(), juce::dontSendNotification);

        original = toTimed (chords);
        result = original;
        selectedIndex = -1;
        revoice();
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
        repaint();
        if (onDisplayChanged) onDisplayChanged();
    }

    void ReharmPanel::selectCard (bool resultRow, int index)
    {
        selectedInResult = resultRow;
        selectedIndex = index;

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
        parseInput();
    }

    // -----------------------------------------------------------------------
    // Suggestion list (only ticked kinds)
    // -----------------------------------------------------------------------
    int ReharmPanel::getNumRows()
    {
        int count = 0;
        for (auto& s : suggestions)
            if (kindToggles[(int) s.kind]->getToggleState()) ++count;
        return count;
    }

    namespace
    {
        const Suggestion* nthVisible (const std::vector<Suggestion>& all, const juce::OwnedArray<juce::ToggleButton>& toggles, int row)
        {
            for (auto& s : all)
                if (toggles[(int) s.kind]->getToggleState() && row-- == 0)
                    return &s;
            return nullptr;
        }
    }

    void ReharmPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
    {
        const auto* s = nthVisible (suggestions, kindToggles, row);
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
        const auto* s = nthVisible (suggestions, kindToggles, row);
        if (s == nullptr) return;

        result = applySuggestion (result, *s);
        selectedIndex = -1;
        revoice();
    }

    juce::String ReharmPanel::getTooltipForRow (int row)
    {
        const auto* s = nthVisible (suggestions, kindToggles, row);
        if (s == nullptr) return {};
        return juce::String (subKindName (s->kind)) + " on chord " + juce::String (s->index + 1) + "\n"
             + juce::String (s->explanation) + "\n\n" + juce::String (subKindDescription (s->kind))
             + "\n\nDouble-click to apply.";
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

        auto strips = r.removeFromTop (juce::jmin (170, r.getHeight() / 2));
        originalStrip.setBounds (strips.removeFromTop (strips.getHeight() / 2));
        resultStrip.setBounds (strips);
        r.removeFromTop (4);

        auto listHeader = row (20);
        applyButton.setBounds (listHeader.removeFromRight (130));
        suggestionsLabel.setBounds (listHeader);
        suggestionList.setBounds (r);
    }
}
