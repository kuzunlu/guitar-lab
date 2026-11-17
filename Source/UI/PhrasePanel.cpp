#include "UI/PhrasePanel.h"

#include <cmath>

namespace gl
{
    namespace
    {
        constexpr float kPxPerBeat = 52.0f;
        constexpr float kTabLeft   = 6.0f;
        constexpr float kTabTop    = 16.0f;

        // Short tab text for a note: "h7", "PM5", "<12>", "7b2", "5/9", "7~" ...
        juce::String tabText (const NoteEvent& n)
        {
            juce::String s;
            switch (n.technique)
            {
                case Technique::HammerOn:        s << "h"; break;
                case Technique::PullOff:         s << "p"; break;
                case Technique::Tap:             s << "t"; break;
                case Technique::PalmMute:        s << "PM"; break;
                case Technique::PinchHarmonic:   s << "PH"; break;
                default: break;
            }

            if (n.technique == Technique::DeadNote)             s << "x";
            else if (n.technique == Technique::NaturalHarmonic) s << "<" << n.fret << ">";
            else                                                s << n.fret;

            if (! n.bend.empty())
            {
                double peak = 0;
                for (auto& b : n.bend) peak = std::max (peak, b.semitones);
                const bool release = n.bend.back().semitones < peak - 0.01;
                s << (n.bend.front().semitones > 0.01 ? "pb" : "b") << juce::String (peak, peak == std::floor (peak) ? 0 : 1)
                  << (release ? "r" : "");
            }

            switch (n.slide)
            {
                case SlideType::SlideTo:      s << (n.slideToFret >= n.fret ? "/" : "\\") << n.slideToFret; break;
                case SlideType::SlideInBelow: s = "/" + s; break;
                case SlideType::SlideInAbove: s = "\\" + s; break;
                case SlideType::SlideOutDown: s << "\\"; break;
                case SlideType::SlideOutUp:   s << "/"; break;
                case SlideType::None: break;
            }

            if (n.vibrato) s << "~";
            return s;
        }

        const double kStepBeats[] = { 0.25, 0.5, 1.0 / 3.0, 1.0, 2.0, 4.0 };
    }

    // =======================================================================
    // TabView
    // =======================================================================
    float TabView::beatToX (double beat) const { return kTabLeft + (float) beat * kPxPerBeat; }
    double TabView::xToBeat (float x) const    { return (x - kTabLeft) / kPxPerBeat; }

    int TabView::noteAt (juce::Point<float> p) const
    {
        const auto& notes = proc.phrase.notes;
        const float rowH = (getHeight() - kTabTop) / (float) proc.instrument.numStrings();

        for (int i = (int) notes.size() - 1; i >= 0; --i)
        {
            const auto& n = notes[(size_t) i];
            const float x = beatToX (n.start);
            const float y = kTabTop + rowH * n.string;
            const float w = std::max (22.0f, (float) n.length * kPxPerBeat);
            if (juce::Rectangle<float> (x, y, w, rowH).contains (p))
                return i;
        }
        return -1;
    }

    void TabView::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);

        const int strings = proc.instrument.numStrings();
        const float rowH = (getHeight() - kTabTop) / (float) strings;
        const double length = proc.phrase.lengthBeats;

        // Grid
        for (double b = 0; b <= length + 1e-9; b += gridStep)
        {
            const bool bar = std::fmod (b, 4.0) < 1e-6;
            const bool beat = std::fmod (b, 1.0) < 1e-6;
            g.setColour (bar ? theme::textDim.withAlpha (0.7f) : theme::textDim.withAlpha (beat ? 0.25f : 0.1f));
            g.drawVerticalLine ((int) beatToX (b), kTabTop, (float) getHeight());

            if (bar && b < length)
            {
                g.setColour (theme::textDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("Bar " + juce::String ((int) (b / 4) + 1), juce::Rectangle<float> (beatToX (b) + 3, 0, 60, kTabTop),
                            juce::Justification::centredLeft);
            }
        }

        // String lines
        for (int s = 0; s < strings; ++s)
        {
            g.setColour (theme::stringCol.withAlpha (0.35f));
            g.drawHorizontalLine ((int) (kTabTop + rowH * (s + 0.5f)), beatToX (0), beatToX (length));
        }

        // Notes
        const auto& notes = proc.phrase.notes;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            if (n.string >= strings) continue;

            const float x = beatToX (n.start);
            const float y = kTabTop + rowH * n.string;
            const float w = (float) n.length * kPxPerBeat;

            g.setColour ((i == selected ? theme::accent : theme::chordTone).withAlpha (0.25f));
            g.fillRoundedRectangle (x, y + rowH * 0.3f, std::max (4.0f, w - 2.0f), rowH * 0.4f, 3.0f);

            const auto text = tabText (n);
            const juce::FontOptions font (juce::jmin (14.0f, rowH * 0.8f), juce::Font::bold);
            const float tw = juce::GlyphArrangement::getStringWidth (font, text) + 6.0f;

            g.setColour (theme::panel);
            g.fillRect (x, y + 2, tw, rowH - 4);
            g.setColour (i == selected ? theme::accent : theme::text);
            g.setFont (font);
            g.drawText (text, juce::Rectangle<float> (x, y, tw, rowH), juce::Justification::centred, false);
        }

        // Cursor and playhead
        g.setColour (theme::accent);
        g.fillRect (beatToX (cursor) - 1.0f, 0.0f, 2.0f, (float) getHeight());

        const double play = proc.playheadBeat.load();
        if (play >= 0)
        {
            g.setColour (theme::sounding);
            g.fillRect (beatToX (play) - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }
    }

    void TabView::mouseDown (const juce::MouseEvent& e)
    {
        const int hit = noteAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0 && onDelete) onDelete (hit);
            return;
        }

        if (hit >= 0)
        {
            selected = hit;
            if (onSelect) onSelect (hit);
        }
        else
        {
            selected = -1;
            if (onSelect) onSelect (-1);
            const double beat = std::round (xToBeat (e.position.x) / gridStep) * gridStep;
            cursor = juce::jlimit (0.0, std::max (0.0, proc.phrase.lengthBeats - gridStep), beat);
            if (onCursorMoved) onCursorMoved();
        }
        repaint();
    }

    juce::String TabView::getTooltip()
    {
        const auto mouse = getMouseXYRelative().toFloat();
        const int hit = noteAt (mouse);
        if (hit >= 0)
        {
            const auto& n = proc.phrase.notes[(size_t) hit];
            return "Note: string " + juce::String (n.string + 1) + ", fret " + juce::String (n.fret)
                 + " (" + juce::String (midiNoteName (soundingNote (n, proc.instrument))) + ")\n"
                 + "Starts at beat " + juce::String (n.start + 1.0, 2) + ", length " + juce::String (n.length, 2) + " beats\n"
                 + "Technique: " + juce::String (techniqueName (n.technique)) + "   Velocity: " + juce::String (n.velocity) + "\n"
                 + "Click to select it (then change the controls above to edit it). Right-click to delete it.";
        }

        return "Phrase (tab view)\n"
               "Each row is a string, highest string on top, exactly like guitar tab. Time goes to the right.\n"
               "Yellow line = write cursor (new notes go here). Green line = playback position.\n"
               "Click an empty place to move the cursor. Click a note to select and edit it. Right-click a note to delete it.\n"
               "Symbols: h = hammer-on, p = pull-off, t = tap, PM = palm mute, x = dead note, <12> = natural harmonic, "
               "PH = pinch harmonic, b2 = bend 2 semitones, pb = pre-bend, r = release, / and \\ = slides, ~ = vibrato.";
    }

    // =======================================================================
    // DragMidiArea
    // =======================================================================
    void DragMidiArea::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::panelLight);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Drag MIDI to DAW", r, juce::Justification::centred);
    }

    void DragMidiArea::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFile)
            return;

        dragging = true;
        const auto file = makeFile();
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    // =======================================================================
    // PhrasePanel
    // =======================================================================
    PhrasePanel::PhrasePanel (GuitarLabProcessor& p, FretboardComponent& b)
        : proc (p), board (b), tab (p)
    {
        auto label = [this] (juce::Label& l, const juce::String& text, const juce::String& tip)
        {
            l.setText (text, juce::dontSendNotification);
            l.setTooltip (tip);
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        };

        auto slider = [this] (juce::Slider& s, double lo, double hi, double step, double value, const juce::String& suffix, const juce::String& tip)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
            s.setRange (lo, hi, step);
            s.setValue (value, juce::dontSendNotification);
            s.setTextValueSuffix (suffix);
            s.setTooltip (tip);
            s.onValueChange = [this] { applyControlsToSelected(); };
            addAndMakeVisible (s);
        };

        // --- Write mode ---
        writeToggle.setTooltip ("Write mode\n"
            "Off: clicking the fretboard only plays the note (and sends it to MIDI out right away).\n"
            "On: each click ALSO writes the note into the phrase at the yellow cursor, using the technique, bend, slide "
            "and vibrato settings below. The cursor then moves forward by one step.\n"
            "Shift + click collects notes for a chord (blue rings) in both modes.");
        addAndMakeVisible (writeToggle);

        // --- Step ---
        label (stepLabel, "Step", "Step length\nHow long each written note is, and how far the cursor moves after each note.\n"
                                  "1/8 = half a beat. 1/8T = eighth-note triplet (three per beat). 1 bar = four beats.");
        for (auto [name, id] : { std::pair { "1/16", 1 }, { "1/8", 2 }, { "1/8T", 3 }, { "1/4", 4 }, { "1/2", 5 }, { "1 bar", 6 } })
            stepBox.addItem (name, id);
        stepBox.setSelectedId (2, juce::dontSendNotification);
        stepBox.setTooltip (stepLabel.getTooltip());
        stepBox.onChange = [this] { tab.gridStep = stepBeats(); tab.repaint(); };
        addAndMakeVisible (stepBox);

        // --- Bars ---
        label (barsLabel, "Length", "Phrase length\nHow many bars (of 4 beats) the phrase has. The phrase loops over this length "
                                    "when it plays in the DAW. Notes past the end are not played.");
        for (int bars : { 1, 2, 4, 8, 16, 32 })
            barsBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
        barsBox.setTooltip (barsLabel.getTooltip());
        barsBox.onChange = [this]
        {
            if (loadingControls) return;
            pushUndo();
            proc.phrase.lengthBeats = barsBox.getSelectedId() * 4.0;
            commit();
        };
        addAndMakeVisible (barsBox);

        // --- Technique ---
        label (techniqueLabel, "Technique", "Technique\nHow the note is played. This changes the MIDI:\n"
            "Hammer-on / Pull-off: softer note that overlaps the one before (legato).\n"
            "Tap: legato, but full strength.\n"
            "Palm mute: shorter and a bit softer (plus a keyswitch/CC if set in MIDI settings).\n"
            "Dead note: very short and quiet 'chk'.\n"
            "Natural harmonic: the bell-like note of the harmonic at frets 12, 7, 19, 5, 24, 4, 9, 16, 3.\n"
            "Pinch harmonic: the fretted note plus an octave and a fifth (the 'squeal').");
        for (int t = 0; t < (int) Technique::NumTechniques; ++t)
            techniqueBox.addItem (juce::String (techniqueName ((Technique) t)), t + 1);
        techniqueBox.setSelectedId (1, juce::dontSendNotification);
        techniqueBox.setTooltip (techniqueLabel.getTooltip());
        techniqueBox.onChange = [this] { applyControlsToSelected(); };
        addAndMakeVisible (techniqueBox);

        // --- Velocity ---
        label (velLabel, "Velocity", "Velocity\nHow hard the note is picked: 1 = softest, 127 = hardest.\n"
                                     "In a chord, each later string is slightly softer, like a real strum.");
        slider (velocity, 1, 127, 1, 100, "", velLabel.getTooltip());

        // --- Bend ---
        label (bendLabel, "Bend", "Bend shape\n"
            "Bend: push the string up and hold.\n"
            "Bend + release: up, hold, then back down.\n"
            "Pre-bend: the string is already bent when you pick it.\n"
            "Pre-bend + release: starts bent, then comes down.\n"
            "Slow bend + hold: a slow, singing bend.\n"
            "Quarter-tone bend: a small blues 'curl' (half a semitone).\n"
            "Bends are sent as pitch bend on the string's own channel, so other strings stay in tune.");
        for (int i = 0; i <= (int) BendPreset::QuarterBend; ++i)
            bendBox.addItem (juce::String (bendPresetName ((BendPreset) i)), i + 1);
        bendBox.setSelectedId (1, juce::dontSendNotification);
        bendBox.setTooltip (bendLabel.getTooltip());
        bendBox.onChange = [this] { applyControlsToSelected(); };
        addAndMakeVisible (bendBox);

        for (auto [name, id] : { std::pair { "1/2 step", 1 }, { "Whole step", 2 }, { "1 1/2 steps", 3 }, { "2 steps", 4 } })
            bendAmountBox.addItem (name, id);
        bendAmountBox.setSelectedId (2, juce::dontSendNotification);
        bendAmountBox.setTooltip ("Bend amount\nHow far the bend goes. Whole step = 2 semitones (the most common bend).\n"
                                  "Make sure the pitch bend range in MIDI settings is at least this big.");
        bendAmountBox.onChange = [this] { applyControlsToSelected(); };
        addAndMakeVisible (bendAmountBox);

        // --- Slide ---
        label (slideLabel, "Slide", "Slide\n"
            "Slide to fret: glide from this note to another fret on the same string (distance below). "
            "If the next note is on that fret, it is joined smoothly (legato).\n"
            "Slide in from below / above: start a few frets away and glide into the note.\n"
            "Slide out down / up: glide away at the end of the note.\n"
            "Slides are pitch bend, so they only reach as far as the pitch bend range.");
        for (int i = 0; i <= (int) SlideType::SlideOutUp; ++i)
            slideBox.addItem (juce::String (slideTypeName ((SlideType) i)), i + 1);
        slideBox.setSelectedId (1, juce::dontSendNotification);
        slideBox.setTooltip (slideLabel.getTooltip());
        slideBox.onChange = [this] { applyControlsToSelected(); };
        addAndMakeVisible (slideBox);

        slider (slideDistance, -12, 12, 1, 2, " fr",
            "Slide distance (frets)\nFor 'Slide to fret': how many frets to move (+ = up the neck, - = down).\n"
            "For slide in / out: how many frets away the slide starts or ends (the sign is ignored).");

        // --- Vibrato ---
        vibratoToggle.setTooltip ("Vibrato\nShakes the pitch up and down while the note rings. It starts after a short delay, "
                                  "like a real player. Sent as pitch bend on the string's channel.");
        vibratoToggle.onClick = [this] { applyControlsToSelected(); };
        addAndMakeVisible (vibratoToggle);

        label (vibLabel, "Depth / speed", "Vibrato depth and speed\nDepth = how far the pitch moves (semitones). 0.2 = gentle, 0.5 = wide rock vibrato.\n"
                                          "Speed = shakes per second (Hz). 5-6 Hz is normal.");
        slider (vibDepth, 0.05, 1.0, 0.05, 0.4, " st", vibLabel.getTooltip());
        slider (vibRate, 2.0, 9.0, 0.1, 5.5, " Hz", vibLabel.getTooltip());

        letRingToggle.setTooltip ("Let ring\nThe note keeps sounding until the next note on the same string, "
                                  "no matter how long it is written. Good for arpeggios and open chords.");
        letRingToggle.onClick = [this] { applyControlsToSelected(); };
        addAndMakeVisible (letRingToggle);

        // --- Chord / strum ---
        strumDownToggle.setToggleState (true, juce::dontSendNotification);
        strumDownToggle.setTooltip ("Strum direction\nOn = down strum (low string first). Off = up strum (high string first).");
        addAndMakeVisible (strumDownToggle);

        label (strumLabel, "Strum", "Strum time\nHow long the pick takes to cross all the strings, in milliseconds.\n"
                                    "0 = all notes at once (like a piano). 10-30 ms = normal strum. 80+ ms = slow, harp-like rake.");
        slider (strumMs, 0, 200, 1, 18, " ms", strumLabel.getTooltip());

        chordButton.setTooltip ("Add chord\nWrites the notes you collected with Shift + click (blue rings) as one strummed chord at the cursor.\n"
                                "Uses the strum direction, strum time, velocity and technique settings.\n"
                                "Tip: in the Reharm Lab tab you can send a whole voiced progression to the phrase at once.");
        chordButton.onClick = [this] { addChordFromBuilder(); };
        addAndMakeVisible (chordButton);

        deleteButton.setTooltip ("Delete note\nRemoves the selected note (you can also right-click a note in the tab view).");
        deleteButton.onClick = [this]
        {
            if (tab.selected < 0 || tab.selected >= (int) proc.phrase.notes.size()) return;
            pushUndo();
            proc.phrase.notes.erase (proc.phrase.notes.begin() + tab.selected);
            tab.selected = -1;
            commit();
        };
        addAndMakeVisible (deleteButton);

        undoButton.setTooltip ("Undo\nGoes back one change in the phrase (up to 100 steps).");
        undoButton.onClick = [this] { undo(); };
        addAndMakeVisible (undoButton);

        clearButton.setTooltip ("Clear phrase\nRemoves every note. You can get them back with Undo.");
        clearButton.onClick = [this] { pushUndo(); proc.phrase.clear(); tab.selected = -1; tab.cursor = 0; commit(); };
        addAndMakeVisible (clearButton);

        playButton.setTooltip ("Play / Stop\nPlays the phrase in a loop with Guitar Lab's own clock, at the internal tempo "
                               "(set in MIDI / Sound settings).\nWhen 'Sync to host' is on, the phrase ALSO plays whenever the DAW plays.");
        playButton.onClick = [this] { proc.setInternalPlaying (! proc.isInternalPlaying()); };
        addAndMakeVisible (playButton);

        exportButton.setTooltip ("Export MIDI file\nSaves the phrase as a standard .mid file (one track, 960 ticks per beat), "
                                 "with every pitch bend, slide, vibrato and keyswitch, and the MPE / bend range setup at the start.");
        exportButton.onClick = [this] { exportMidi(); };
        addAndMakeVisible (exportButton);

        dragArea.setTooltip ("Drag MIDI to DAW\nPress here and drag onto a track in your DAW. The phrase is dropped in as a MIDI clip, "
                             "with all bends and slides.\nTip: put the clip on a track with an MPE-capable synth, or a guitar library "
                             "set to one channel per string.");
        dragArea.makeFile = [this] { return writeTempMidi(); };
        addAndMakeVisible (dragArea);

        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (statusLabel);

        // --- Tab view ---
        tab.onSelect = [this] (int i)
        {
            if (i >= 0 && i < (int) proc.phrase.notes.size())
                loadControlsFrom (proc.phrase.notes[(size_t) i]);
            board.repaint();
            if (auto* parent = getParentComponent()) parent->repaint();
        };
        tab.onDelete = [this] (int i)
        {
            pushUndo();
            proc.phrase.notes.erase (proc.phrase.notes.begin() + i);
            tab.selected = -1;
            commit();
        };
        tab.onCursorMoved = [this] { refresh(); };
        tabViewport.setViewedComponent (&tab, false);
        tabViewport.setScrollBarsShown (false, true);
        addAndMakeVisible (tabViewport);

        refresh();
    }

    // -----------------------------------------------------------------------
    double PhrasePanel::stepBeats() const
    {
        const int id = juce::jlimit (1, 6, stepBox.getSelectedId());
        return kStepBeats[id - 1];
    }

    void PhrasePanel::pushUndo()
    {
        undoStack.push_back (phraseToText (proc.phrase));
        if (undoStack.size() > 100)
            undoStack.erase (undoStack.begin());
    }

    void PhrasePanel::undo()
    {
        if (undoStack.empty()) return;
        proc.phrase = phraseFromText (undoStack.back());
        undoStack.pop_back();
        tab.selected = -1;
        commit();
    }

    void PhrasePanel::commit()
    {
        proc.phrase.sort();
        proc.phraseChanged();
        refresh();
    }

    NoteEvent PhrasePanel::noteFromControls (FretPos pos) const
    {
        NoteEvent n;
        n.string    = pos.string;
        n.fret      = pos.fret;
        n.start     = tab.cursor;
        n.length    = stepBeats();
        n.velocity  = (int) velocity.getValue();
        n.technique = (Technique) (techniqueBox.getSelectedId() - 1);

        const auto preset = (BendPreset) (bendBox.getSelectedId() - 1);
        n.bend = makeBend (preset, preset == BendPreset::QuarterBend ? 0.5 : bendAmountBox.getSelectedId());

        n.slide = (SlideType) (slideBox.getSelectedId() - 1);
        const int dist = (int) slideDistance.getValue();
        n.slideDistance = std::max (1, std::abs (dist));
        n.slideToFret   = juce::jlimit (0, kNumFrets, pos.fret + dist);

        n.vibrato       = vibratoToggle.getToggleState();
        n.vibratoDepth  = vibDepth.getValue();
        n.vibratoRateHz = vibRate.getValue();
        n.letRing       = letRingToggle.getToggleState();
        return n;
    }

    void PhrasePanel::loadControlsFrom (const NoteEvent& n)
    {
        loadingControls = true;
        techniqueBox.setSelectedId ((int) n.technique + 1, juce::dontSendNotification);
        velocity.setValue (n.velocity, juce::dontSendNotification);

        // Find the bend preset that matches the note (or "No bend").
        bendBox.setSelectedId (1, juce::dontSendNotification);
        for (int p = 1; p <= (int) BendPreset::QuarterBend; ++p)
            for (int amount = 1; amount <= 4; ++amount)
            {
                const auto shape = makeBend ((BendPreset) p, (BendPreset) p == BendPreset::QuarterBend ? 0.5 : amount);
                bool same = shape.size() == n.bend.size();
                for (size_t i = 0; same && i < shape.size(); ++i)
                    same = std::abs (shape[i].position - n.bend[i].position) < 1e-6 && std::abs (shape[i].semitones - n.bend[i].semitones) < 1e-6;
                if (same)
                {
                    bendBox.setSelectedId (p + 1, juce::dontSendNotification);
                    if ((BendPreset) p != BendPreset::QuarterBend)
                        bendAmountBox.setSelectedId (amount, juce::dontSendNotification);
                }
            }

        slideBox.setSelectedId ((int) n.slide + 1, juce::dontSendNotification);
        slideDistance.setValue (n.slide == SlideType::SlideTo ? n.slideToFret - n.fret : n.slideDistance, juce::dontSendNotification);
        vibratoToggle.setToggleState (n.vibrato, juce::dontSendNotification);
        vibDepth.setValue (n.vibratoDepth, juce::dontSendNotification);
        vibRate.setValue (n.vibratoRateHz, juce::dontSendNotification);
        letRingToggle.setToggleState (n.letRing, juce::dontSendNotification);
        loadingControls = false;
    }

    void PhrasePanel::applyControlsToSelected()
    {
        if (loadingControls || tab.selected < 0 || tab.selected >= (int) proc.phrase.notes.size())
            return;

        pushUndo();
        auto& n = proc.phrase.notes[(size_t) tab.selected];
        auto updated = noteFromControls ({ n.string, n.fret });
        updated.start  = n.start;
        updated.length = n.length;
        updated.group  = n.group;
        n = updated;
        proc.phraseChanged();
        tab.repaint();
    }

    void PhrasePanel::writeNote (FretPos pos)
    {
        pushUndo();
        auto n = noteFromControls (pos);
        proc.phrase.notes.push_back (n);
        proc.phrase.sort();

        // Select the new note so it can be edited right away.
        for (int i = 0; i < (int) proc.phrase.notes.size(); ++i)
        {
            const auto& m = proc.phrase.notes[(size_t) i];
            if (m.string == n.string && m.fret == n.fret && std::abs (m.start - n.start) < 1e-9)
                tab.selected = i;
        }

        tab.cursor += stepBeats();
        if (tab.cursor >= proc.phrase.lengthBeats - 1e-9)
            tab.cursor = 0.0;
        commit();
    }

    void PhrasePanel::addChordFromBuilder()
    {
        if (chordBuilder.empty())
        {
            statusLabel.setText ("Shift + click notes on the fretboard first.", juce::dontSendNotification);
            return;
        }

        pushUndo();
        Voicing v { std::vector<int> ((size_t) proc.instrument.numStrings(), kMuted) };
        for (auto p : chordBuilder)
            if (p.string < proc.instrument.numStrings())
                v.frets[(size_t) p.string] = p.fret;

        const double bpm = proc.params.getRawParameterValue (ParamIDs::internalBpm)->load();
        proc.phrase.addChord (v, tab.cursor, stepBeats(), (int) velocity.getValue(), strumDownToggle.getToggleState(),
                              strumMs.getValue(), bpm, (Technique) (techniqueBox.getSelectedId() - 1));

        tab.cursor += stepBeats();
        if (tab.cursor >= proc.phrase.lengthBeats - 1e-9)
            tab.cursor = 0.0;

        chordBuilder.clear();
        commit();
    }

    // -----------------------------------------------------------------------
    void PhrasePanel::fretPressed (FretPos pos, const juce::ModifierKeys& mods)
    {
        if (mods.isShiftDown())
        {
            // Toggle in the chord builder (one note per string).
            auto same = std::find (chordBuilder.begin(), chordBuilder.end(), pos);
            if (same != chordBuilder.end())
                chordBuilder.erase (same);
            else
            {
                chordBuilder.erase (std::remove_if (chordBuilder.begin(), chordBuilder.end(),
                                                    [&] (FretPos p) { return p.string == pos.string; }), chordBuilder.end());
                chordBuilder.push_back (pos);
            }
            board.setMarks (marks());
        }
        else if (writeToggle.getToggleState())
        {
            writeNote (pos);
        }

        proc.liveNoteOn (pos.string, pos.fret);
        liveString = pos.string;
    }

    void PhrasePanel::fretDragged (FretPos start, double bend, double slide)
    {
        proc.liveBend (start.string, bend + slide);
    }

    void PhrasePanel::fretReleased (FretPos)
    {
        proc.liveNoteOff (liveString);
        liveString = -1;
    }

    std::vector<FretboardComponent::Mark> PhrasePanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        for (auto p : chordBuilder)
            m.push_back ({ p, theme::chordTone, {}, false });

        if (tab.selected >= 0 && tab.selected < (int) proc.phrase.notes.size())
        {
            const auto& n = proc.phrase.notes[(size_t) tab.selected];
            m.push_back ({ { n.string, n.fret }, theme::accent, {}, true });
        }
        return m;
    }

    void PhrasePanel::refresh()
    {
        loadingControls = true;
        barsBox.setSelectedId (juce::jlimit (1, 32, (int) std::round (proc.phrase.lengthBeats / 4.0)), juce::dontSendNotification);
        loadingControls = false;

        tab.gridStep = stepBeats();
        tab.setSize ((int) (kTabLeft * 2 + proc.phrase.lengthBeats * kPxPerBeat), juce::jmax (60, tabViewport.getHeight() - 12));
        tab.repaint();

        statusLabel.setText (juce::String ((int) proc.phrase.notes.size()) + " notes   cursor: bar "
                             + juce::String ((int) (tab.cursor / 4) + 1) + ", beat " + juce::String (std::fmod (tab.cursor, 4.0) + 1.0, 2),
                             juce::dontSendNotification);

        board.setMarks (marks());
    }

    void PhrasePanel::timerTick()
    {
        playButton.setButtonText (proc.isInternalPlaying() ? "Stop" : "Play");
        if (proc.playheadBeat.load() >= 0)
            tab.repaint();
    }

    // -----------------------------------------------------------------------
    juce::MidiFile PhrasePanel::buildMidiFile()
    {
        constexpr int ppq = 960;
        const double bpm = proc.params.getRawParameterValue (ParamIDs::internalBpm)->load();

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);

        proc.syncSettings();
        for (const auto& e : setupMessages (proc.midiSettings, proc.instrument.numStrings()))
            seq.addEvent (juce::MidiMessage (e.data, e.size), 0);

        for (const auto& e : renderPhrase (proc.phrase, proc.instrument, proc.midiSettings, bpm))
            seq.addEvent (juce::MidiMessage (e.data, e.size), std::round (e.beat * ppq));

        seq.addEvent (juce::MidiMessage::endOfTrack(), proc.phrase.lengthBeats * ppq);
        seq.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (seq);
        return file;
    }

    juce::File PhrasePanel::writeTempMidi()
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GuitarLab-phrase.mid");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
            buildMidiFile().writeTo (out);
        return file;
    }

    void PhrasePanel::exportMidi()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save the phrase as a MIDI file",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GuitarLab phrase.mid"),
                                                       "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            file = file.withFileExtension ("mid");
            file.deleteFile();
            juce::FileOutputStream out (file);
            if (out.openedOk() && buildMidiFile().writeTo (out))
                statusLabel.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
            else
                statusLabel.setText ("Could not save the file.", juce::dontSendNotification);
        });
    }

    // -----------------------------------------------------------------------
    void PhrasePanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);
    }

    void PhrasePanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        auto row = [&r] { auto x = r.removeFromTop (26); r.removeFromTop (4); return x; };
        auto put = [] (juce::Rectangle<int>& x, juce::Component& c, int w) { c.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4); };

        auto a = row();
        put (a, writeToggle, 110);
        put (a, stepLabel, 40);        put (a, stepBox, 80);
        put (a, barsLabel, 52);        put (a, barsBox, 90);
        put (a, techniqueLabel, 72);   put (a, techniqueBox, 150);
        put (a, velLabel, 60);         put (a, velocity, 170);

        auto b = row();
        put (b, bendLabel, 40);        put (b, bendBox, 150);   put (b, bendAmountBox, 110);
        put (b, slideLabel, 40);       put (b, slideBox, 160);  put (b, slideDistance, 150);
        put (b, vibratoToggle, 80);
        put (b, vibLabel, 92);         put (b, vibDepth, 130);  put (b, vibRate, 130);
        put (b, letRingToggle, 80);

        auto c = row();
        put (c, strumDownToggle, 100);
        put (c, strumLabel, 44);       put (c, strumMs, 150);
        put (c, chordButton, 90);
        put (c, deleteButton, 90);
        put (c, undoButton, 60);
        put (c, clearButton, 60);
        put (c, playButton, 60);
        put (c, exportButton, 100);
        put (c, dragArea, 140);
        statusLabel.setBounds (c);

        tabViewport.setBounds (r);
        refresh();
    }
}
