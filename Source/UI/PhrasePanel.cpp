#include "UI/PhrasePanel.h"

#include <cmath>
#include <map>

namespace gl
{
    namespace
    {
        constexpr float kPxPerBeat = 52.0f;
        constexpr float kTabLeft   = 6.0f;
        constexpr float kRulerH    = 16.0f;   // bar numbers (drag here to loop a section)
        constexpr float kTabTop    = 34.0f;   // ruler + the chord names row

        const double kStepBeats[] = { 0.25, 0.5, 1.0 / 3.0, 1.0, 2.0, 4.0 };
    }

    // =======================================================================
    // TabView
    // =======================================================================
    float TabView::beatToX (double beat) const { return kTabLeft + (float) beat * pxPerBeat; }
    double TabView::xToBeat (float x) const    { return (x - kTabLeft) / pxPerBeat; }

    int TabView::noteAt (juce::Point<float> p) const
    {
        const auto& notes = proc.phrase.notes;
        const float rowH = (getHeight() - kTabTop) / (float) proc.instrument.numStrings();

        for (int i = (int) notes.size() - 1; i >= 0; --i)
        {
            const auto& n = notes[(size_t) i];
            const float x = beatToX (n.start);
            const float y = kTabTop + rowH * n.string;
            const float w = std::max (22.0f, (float) n.length * pxPerBeat);
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
        const auto clip = g.getClipBounds();
        const double fromBeat = std::max (0.0, xToBeat ((float) clip.getX()) - 4.0);
        const double toBeat = std::min (length, xToBeat ((float) clip.getRight()) + 4.0);

        // Loop section: shaded, with a gold strip in the ruler
        const double ls = proc.loopStart.load(), le = proc.loopEnd.load();
        if (le > ls + 1e-6)
        {
            g.setColour (theme::accent.withAlpha (0.07f));
            g.fillRect (beatToX (ls), kTabTop, beatToX (le) - beatToX (ls), (float) getHeight() - kTabTop);
            g.setColour (theme::accent.withAlpha (0.45f));
            g.fillRect (beatToX (ls), 0.0f, beatToX (le) - beatToX (ls), kRulerH);
        }

        // Grid: fewer lines when zoomed out
        double step = gridStep;
        if (step * pxPerBeat < 5.0f) step = 1.0;
        if (step * pxPerBeat < 5.0f) step = 4.0;
        const int labelEvery = std::max (1, (int) std::ceil (44.0 / (4.0 * pxPerBeat)));
        for (double b = std::floor (fromBeat / step) * step; b <= toBeat + 1e-9; b += step)
        {
            const bool bar = std::fmod (b, 4.0) < 1e-6;
            const bool beat = std::fmod (b, 1.0) < 1e-6;
            g.setColour (bar ? theme::textDim.withAlpha (0.7f) : theme::textDim.withAlpha (beat ? 0.25f : 0.1f));
            g.drawVerticalLine ((int) beatToX (b), kTabTop, (float) getHeight());

            const int barNo = (int) std::lround (b / 4.0);
            if (bar && b < length && barNo % labelEvery == 0)
            {
                g.setColour (theme::textDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ((labelEvery == 1 ? "Bar " : "") + juce::String (barNo + 1),
                            juce::Rectangle<float> (beatToX (b) + 3, 0, 60, kRulerH), juce::Justification::centredLeft);
            }
        }

        // Chord names (chord timeline)
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        for (const auto& c : chords)
        {
            if (c.end < fromBeat || c.start > toBeat) continue;
            const float x0 = beatToX (c.start), x1 = beatToX (c.end);
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRoundedRectangle (x0 + 1.0f, kRulerH + 1.0f, std::max (2.0f, x1 - x0 - 2.0f), kTabTop - kRulerH - 3.0f, 3.0f);
            g.setColour (theme::accent);
            g.drawText (juce::String (c.name), juce::Rectangle<float> (x0 + 4.0f, kRulerH, std::max (20.0f, x1 - x0 - 4.0f), kTabTop - kRulerH - 1.0f),
                        juce::Justification::centredLeft, true);
        }

        // String lines
        for (int s = 0; s < strings; ++s)
        {
            g.setColour (theme::stringCol.withAlpha (0.35f));
            g.drawHorizontalLine ((int) (kTabTop + rowH * (s + 0.5f)), beatToX (0), beatToX (length));
        }

        // Notes (zoomed far out: bars only, no text)
        const bool showText = pxPerBeat >= 14.0f;
        const auto& notes = proc.phrase.notes;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            if (n.string >= strings || n.start + n.length < fromBeat || n.start > toBeat) continue;

            const float x = beatToX (n.start);
            const float y = kTabTop + rowH * n.string;
            const float w = (float) n.length * pxPerBeat;
            const bool marked = i == selected || isSelected (i);

            g.setColour ((marked ? theme::accent : theme::chordTone).withAlpha (marked ? 0.45f : 0.25f));
            g.fillRoundedRectangle (x, y + rowH * 0.3f, std::max (3.0f, w - 2.0f), rowH * 0.4f, 3.0f);
            if (! showText) continue;

            const auto text = juce::String (noteTabText (n));
            const juce::FontOptions font (juce::jmin (14.0f, rowH * 0.8f), juce::Font::bold);
            const float tw = juce::GlyphArrangement::getStringWidth (font, text) + 6.0f;

            g.setColour (theme::panel);
            g.fillRect (x, y + 2, tw, rowH - 4);
            g.setColour (marked ? theme::accent : theme::text);
            g.setFont (font);
            g.drawText (text, juce::Rectangle<float> (x, y, tw, rowH), juce::Justification::centred, false);
        }

        // Box selection
        if (boxSelecting)
        {
            const auto box = juce::Rectangle<float> (boxStart, boxEnd);
            g.setColour (theme::accent.withAlpha (0.12f));
            g.fillRect (box);
            g.setColour (theme::accent);
            g.drawRect (box, 1.0f);
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
        grabKeyboardFocus();
        dragged = false;

        // The ruler (bar numbers): drag to mark a section to loop.
        if (e.position.y < kRulerH && ! e.mods.isPopupMenu())
        {
            loopDragging = true;
            loopAnchor = snap (xToBeat (e.position.x));
            return;
        }

        const int hit = noteAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (hit >= 0 && onDelete) onDelete (hit);
            return;
        }

        if (hit >= 0)
        {
            if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            {
                // Ctrl + click: add to / take out of the selection
                if (isSelected (hit)) selection.erase (std::remove (selection.begin(), selection.end(), hit), selection.end());
                else                  selection.push_back (hit);
                selected = isSelected (hit) ? hit : (selection.empty() ? -1 : selection.back());
            }
            else
            {
                if (! isSelected (hit)) selection = { hit };
                selected = hit;
            }
            if (onSelect) onSelect (selected);
            if (onSelectionChanged) onSelectionChanged();
        }
        else
        {
            // Empty place: a click moves the cursor, a drag selects with a box.
            boxStart = boxEnd = e.position;
            boxSelecting = true;
        }
        repaint();
    }

    void TabView::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() > 3) dragged = true;
        if (loopDragging)
        {
            if (! dragged) return;
            const double at = snap (xToBeat (e.position.x));
            proc.loopStart = std::min (loopAnchor, at);
            proc.loopEnd = std::max (std::min (loopAnchor, at) + gridStep, std::max (loopAnchor, at));
            if (onLoopChanged) onLoopChanged();
            repaint();
        }
        else if (boxSelecting)
        {
            boxEnd = e.position;
            repaint();
        }
    }

    void TabView::mouseUp (const juce::MouseEvent& e)
    {
        if (loopDragging)
        {
            loopDragging = false;
            if (! dragged)
            {
                // A click on the ruler (no drag) ends the loop.
                proc.loopStart = 0.0;
                proc.loopEnd = 0.0;
                if (onLoopChanged) onLoopChanged();
            }
            repaint();
            return;
        }
        if (! boxSelecting) return;
        boxSelecting = false;

        if (! dragged)
        {
            selection.clear();
            selected = -1;
            if (onSelect) onSelect (-1);
            if (onSelectionChanged) onSelectionChanged();
            cursor = juce::jlimit (0.0, std::max (0.0, proc.phrase.lengthBeats - gridStep), snap (xToBeat (e.position.x)));
            if (onCursorMoved) onCursorMoved();
            repaint();
            return;
        }

        // Every note touched by the box
        const auto box = juce::Rectangle<float> (boxStart, boxEnd);
        const float rowH = (getHeight() - kTabTop) / (float) proc.instrument.numStrings();
        if (! (e.mods.isCommandDown() || e.mods.isCtrlDown())) selection.clear();
        for (int i = 0; i < (int) proc.phrase.notes.size(); ++i)
        {
            const auto& n = proc.phrase.notes[(size_t) i];
            const juce::Rectangle<float> r (beatToX (n.start), kTabTop + rowH * n.string, std::max (8.0f, (float) n.length * pxPerBeat), rowH);
            if (r.intersects (box) && ! isSelected (i)) selection.push_back (i);
        }
        selected = selection.empty() ? -1 : selection.front();
        if (onSelect) onSelect (selected);
        if (onSelectionChanged) onSelectionChanged();
        repaint();
    }

    void TabView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // Ctrl + wheel = zoom; otherwise let the viewport scroll.
        if ((e.mods.isCommandDown() || e.mods.isCtrlDown()) && onZoom)
            onZoom (w.deltaY > 0 ? 1.25f : 0.8f);
        else
            Component::mouseWheelMove (e, w);
    }

    juce::String TabView::getTooltip()
    {
        const auto mouse = getMouseXYRelative().toFloat();
        if (mouse.y >= kRulerH && mouse.y < kTabTop)
        {
            const double beat = xToBeat (mouse.x);
            for (const auto& c : chords)
                if (beat >= c.start && beat < c.end)
                    return "Chord: " + juce::String (c.name) + "\n" + juce::String (c.meaning)
                         + "\n\nThe chord names are found from the notes (strummed chords, and arpeggios over 3+ strings). "
                           "'To Reharm Lab' sends them there, to try other chords.";
            return "Chords\nThe chords of the phrase, found from its notes. Single-note melodies have no chord name.";
        }
        if (mouse.y < kRulerH)
            return "Ruler\nDrag here to mark a section (gold). With 'Loop section' on, Play repeats only that section, "
                   "so you can practise one part again and again. Click the ruler once to end the loop.";

        const int hit = noteAt (mouse);
        if (hit >= 0)
        {
            const auto& n = proc.phrase.notes[(size_t) hit];
            return "Note: string " + juce::String (n.string + 1) + ", fret " + juce::String (n.fret)
                 + " (" + juce::String (midiNoteName (soundingNote (n, proc.instrument))) + ")\n"
                 + "Starts at beat " + juce::String (n.start + 1.0, 2) + ", length " + juce::String (n.length, 2) + " beats\n"
                 + "Technique: " + juce::String (techniqueName (n.technique)) + "   Velocity: " + juce::String (n.velocity) + "\n"
                 + "Click to select it (then change the controls above to edit it). Ctrl + click adds it to the selection. "
                   "Right-click to delete it.";
        }

        return "Phrase (tab view)\n"
               "Each row is a string, highest string on top, exactly like guitar tab. Time goes to the right.\n"
               "Yellow line = write cursor (new notes go here). Green line = playback position. Gold = loop section.\n"
               "Click an empty place to move the cursor. Click a note to select and edit it; Ctrl + click to select more; "
               "drag on an empty place to select every note in a box. Right-click a note to delete it.\n"
               "Keys: Delete = delete, Ctrl+C / Ctrl+X / Ctrl+V = copy / cut / paste (at the cursor), Ctrl+A = select all, "
               "Left / Right = move earlier / later by one step, Up / Down = one half step higher / lower, "
               "Shift + Up / Down = same pitch on the next string, Ctrl+Z = undo. Ctrl + mouse wheel = zoom.\n"
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
                                    "when it plays in the DAW. Notes past the end are not played.\n"
                                    "Up to 512 bars (about 17 minutes at 120 BPM). Recording your guitar makes it longer by itself "
                                    "when the take needs it.");
        for (int bars : { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 })
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

        deleteButton.setTooltip ("Delete\nRemoves the selected notes (also: the Delete key, or right-click a note in the tab view).");
        deleteButton.onClick = [this] { deleteSelection(); };
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

        exportButton.setTooltip ("Export\nSaves the phrase as a file:\n"
                                 "  - MIDI (.mid): every pitch bend, slide, vibrato and keyswitch, for your DAW.\n"
                                 "  - Text tab (.txt): classic e|--3--5--| tab to paste in a message, forum or notes.\n"
                                 "  - PDF: a printable tab page, with title, tempo, tuning, bar numbers and all symbols.\n"
                                 "  - MusicXML: opens in MuseScore, Guitar Pro, Sibelius and Finale, as tab (and notes).\n"
                                 "  - Guitar Pro 3 (.gp3): opens directly in Guitar Pro (3 to 8), TuxGuitar and Songsterr. Up to 7 strings; "
                                 "GP3 has no palm mute. New: if a file does not open, use MusicXML and tell us.\n"
                                 "Tip: record your guitar, fix the tab by hand, then export.");
        exportButton.onClick = [this] { showExportMenu(); };
        addAndMakeVisible (exportButton);

        dragArea.setTooltip ("Drag MIDI to DAW\nPress here and drag onto a track in your DAW. The phrase is dropped in as a MIDI clip, "
                             "with all bends and slides.\nTip: put the clip on a track with an MPE-capable synth, or a guitar library "
                             "set to one channel per string.");
        dragArea.makeFile = [this] { return writeTempMidi(); };
        addAndMakeVisible (dragArea);

        toReharmButton.setTooltip ("To Reharm Lab\nSends the chords of the phrase (the gold names under the bar numbers) to the Reharm Lab "
                                   "as a progression, one chord per bar, and opens it - to try substitutions and other voicings.\n"
                                   "Tip: record yourself strumming a song, then press this.");
        toReharmButton.onClick = [this]
        {
            const auto text = juce::String (chordsForReharm (tab.chords));
            if (text.isEmpty()) { statusLabel.setText ("No chords found in the phrase yet.", juce::dontSendNotification); return; }
            if (onSendChords) onSendChords (text);
        };
        addAndMakeVisible (toReharmButton);

        statusLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (statusLabel);

        // --- Record the real guitar ---
        recordButton.setClickingTogglesState (true);
        recordButton.setColour (juce::TextButton::buttonOnColourId, theme::bad);
        recordButton.setTooltip ("Record guitar\n"
            "Writes what you play on your REAL guitar into this phrase, as tab you can edit.\n"
            "Needs guitar tracking: turn it on (and calibrate) in the Guitar -> MIDI tab first.\n"
            "Recording starts with your first note and goes to the yellow cursor. If the DAW (or Play) is running, "
            "notes keep their place in the bar instead.\n"
            "Press again to stop. The notes then appear in the tab view: hammer-ons (h), pull-offs (p), bends, slides, "
            "vibrato (~) and chords are recognised. One Undo removes the whole take.");
        recordButton.onClick = [this]
        {
            if (recordButton.getToggleState()) startRecording();
            else if (proc.isCountingIn())
            {
                // Stopped during the count-in: nothing was recorded yet.
                proc.cancelCountIn();
                recordButton.setToggleState (false, juce::dontSendNotification);
                statusLabel.setText ("Count-in stopped.", juce::dontSendNotification);
            }
            else stopRecording();
        };
        addAndMakeVisible (recordButton);

        snapLabel.setText ("Snap", juce::dontSendNotification);
        snapLabel.setJustificationType (juce::Justification::centredRight);
        snapLabel.setTooltip ("Snap recorded notes\n"
            "Moves the start and end of each recorded note to the nearest grid line.\n"
            "Off = keep your exact timing (good for feel). 1/16 or 1/8 = tidy tab that lines up with the beat.\n"
            "Bends, slides and vibrato are not changed by snapping.");
        addAndMakeVisible (snapLabel);
        for (auto [name, id] : { std::pair { "Off", 1 }, { "1/16", 2 }, { "1/8", 3 }, { "1/8T", 4 }, { "1/4", 5 } })
            snapBox.addItem (name, id);
        snapBox.setSelectedId (2, juce::dontSendNotification);
        snapBox.setTooltip (snapLabel.getTooltip());
        addAndMakeVisible (snapBox);

        // --- Practice tools ---
        loopToggle.setTooltip ("Loop section\nPlay repeats only the gold section of the tab, so you can practise one part.\n"
                               "Drag in the ruler (bar numbers above the tab) to mark the section. Turning this on with no "
                               "section marked loops the bar where the yellow cursor is. Turn it off to play everything again.\n"
                               "Works with Guitar Lab's own Play button (the DAW has its own loop).");
        loopToggle.onClick = [this]
        {
            if (loopToggle.getToggleState())
            {
                if (proc.loopEnd.load() <= proc.loopStart.load())
                {
                    const double bar = std::floor (tab.cursor / 4.0) * 4.0;
                    proc.loopStart = bar;
                    proc.loopEnd = std::min (proc.phrase.lengthBeats, bar + 4.0);
                }
            }
            else
            {
                proc.loopStart = 0.0;
                proc.loopEnd = 0.0;
            }
            tab.repaint();
        };
        addAndMakeVisible (loopToggle);

        metronomeToggle.setTooltip ("Metronome\nA click on every beat (higher on the first beat of each bar) while the phrase plays, "
                                    "with Guitar Lab's Play button or with the DAW. Only in Guitar Lab's audio output, never in the MIDI.");
        addAndMakeVisible (metronomeToggle);
        metronomeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, ParamIDs::metronome, metronomeToggle);

        countInToggle.setTooltip ("Count-in\nWhen you press Record guitar and nothing is playing: one bar of clicks (1 2 3 4) first, "
                                  "then Play and the recording start together at the bar of the yellow cursor. So your notes land "
                                  "in time with the bars. Turn on the Metronome too to keep hearing the beat.");
        countInToggle.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (countInToggle);

        zoomLabel.setText ("Zoom", juce::dontSendNotification);
        zoomLabel.setJustificationType (juce::Justification::centredRight);
        zoomLabel.setTooltip ("Zoom\nMake the tab view wider (+) or narrower (-). Fit shows the whole phrase. Also: Ctrl + mouse wheel.");
        addAndMakeVisible (zoomLabel);
        zoomOutButton.setTooltip ("Zoom out\nSee more bars at once.");
        zoomInButton.setTooltip ("Zoom in\nWider notes, easier to read and click.");
        zoomFitButton.setTooltip ("Fit\nShow the whole phrase in the window.");
        zoomOutButton.onClick = [this] { setZoom (zoom / 1.5f); };
        zoomInButton.onClick  = [this] { setZoom (zoom * 1.5f); };
        zoomFitButton.onClick = [this] { setZoom (0.0f); };
        for (auto* zb : { &zoomOutButton, &zoomInButton, &zoomFitButton }) addAndMakeVisible (zb);

        editLabel.setText ("Selection", juce::dontSendNotification);
        editLabel.setJustificationType (juce::Justification::centredRight);
        editLabel.setTooltip ("Selected notes\nSelect notes in the tab view: click one, Ctrl + click more, or drag a box on an empty "
                              "place. Then copy, cut, paste or move them. Ctrl + A selects everything.");
        addAndMakeVisible (editLabel);
        copyButton.setTooltip ("Copy (Ctrl + C)\nRemembers the selected notes, so you can paste them somewhere else.");
        cutButton.setTooltip ("Cut (Ctrl + X)\nCopies the selected notes and removes them.");
        pasteButton.setTooltip ("Paste (Ctrl + V)\nWrites the copied notes at the yellow cursor (the first copied note lands on the cursor). "
                                "Paste again to repeat them.");
        earlierButton.setTooltip ("Move earlier (Left key)\nMoves the selected notes one step earlier (the Step length).");
        laterButton.setTooltip ("Move later (Right key)\nMoves the selected notes one step later.");
        pitchUpButton.setTooltip ("One half step higher (Up key)\nMoves the selected notes one fret up, on the same string.\n"
                                  "Shift + Up: the same pitch on the next higher string (another fingering).");
        pitchDownButton.setTooltip ("One half step lower (Down key)\nMoves the selected notes one fret down, on the same string.\n"
                                    "Shift + Down: the same pitch on the next lower string.");
        copyButton.onClick      = [this] { copySelection(); };
        cutButton.onClick       = [this] { copySelection(); deleteSelection(); };
        pasteButton.onClick     = [this] { pasteClipboard(); };
        earlierButton.onClick   = [this] { moveSelection (-stepBeats()); };
        laterButton.onClick     = [this] { moveSelection (stepBeats()); };
        pitchUpButton.onClick   = [this] { transposeSelection (1); };
        pitchDownButton.onClick = [this] { transposeSelection (-1); };
        for (auto* eb : { &copyButton, &cutButton, &pasteButton, &earlierButton, &laterButton, &pitchUpButton, &pitchDownButton })
            addAndMakeVisible (eb);

        // --- Tab view ---
        tab.onKey = [this] (const juce::KeyPress& k) { return handleKey (k); };
        tab.onZoom = [this] (float f) { setZoom ((autoFit ? 1.0f : zoom) * f); };
        tab.onLoopChanged = [this] { updateLoopButton(); };
        tab.onSelectionChanged = [this] { board.setMarks (marks()); };
        tab.onSelect = [this] (int i)
        {
            if (i >= 0 && i < (int) proc.phrase.notes.size())
                loadControlsFrom (proc.phrase.notes[(size_t) i]);
            board.repaint();
            if (auto* parent = getParentComponent()) parent->repaint();
        };
        tab.onDelete = [this] (int i)
        {
            if (! tab.isSelected (i)) { tab.selection = { i }; tab.selected = i; }
            deleteSelection();
        };
        tab.onCursorMoved = [this] { refresh(); };
        tabViewport.setViewedComponent (&tab, false);
        tabViewport.setScrollBarsShown (false, true);
        addAndMakeVisible (tabViewport);

        refresh();
    }

    PhrasePanel::~PhrasePanel()
    {
        proc.recordingGuitar = false;
    }

    // -----------------------------------------------------------------------
    // Recording the real guitar
    // -----------------------------------------------------------------------
    void PhrasePanel::startRecording()
    {
        if ((int) proc.params.getRawParameterValue (ParamIDs::trackMode)->load() == 0)
        {
            recordButton.setToggleState (false, juce::dontSendNotification);
            statusLabel.setText ("Turn on guitar tracking first (Guitar -> MIDI tab).", juce::dontSendNotification);
            return;
        }

        static const double snapBeats[] = { 0.0, 0.25, 0.5, 1.0 / 3.0, 1.0 };
        PhraseRecorder::Settings s;
        s.firstChannel = proc.midiSettings.firstChannel;
        s.bendRange    = proc.midiSettings.bendRange;
        s.bpm          = proc.currentBpm();
        s.snapBeats    = snapBeats[juce::jlimit (1, 5, snapBox.getSelectedId()) - 1];
        s.startAt      = tab.cursor;
        recorder.start (proc.instrument, s);

        // Throw away anything left from before, then start.
        while (proc.popRecordEvents (recordBuffer.data(), (int) recordBuffer.size()) > 0) {}
        recordStarted = false;
        lastRecordBeat = 0.0;
        playbackForRecording = false;

        // Count-in: only when nothing plays. One bar of clicks, then Play and the recording start at the cursor's bar.
        if (countInToggle.getToggleState() && ! proc.isInternalPlaying() && proc.playheadBeat.load() < 0)
        {
            playbackForRecording = true;
            proc.startCountIn (std::floor (tab.cursor / 4.0) * 4.0, true);
            statusLabel.setText ("Count-in: get ready...", juce::dontSendNotification);
            return;
        }
        proc.recordingGuitar = true;
        statusLabel.setText ("Recording... play your guitar. Press the red button to stop.", juce::dontSendNotification);
    }

    void PhrasePanel::drainRecording()
    {
        int n = 0;
        while ((n = proc.popRecordEvents (recordBuffer.data(), (int) recordBuffer.size())) > 0)
            for (int i = 0; i < n; ++i)
            {
                const auto& e = recordBuffer[(size_t) i];
                if (! recordStarted)
                {
                    recordStarted = true;
                    recorder.setAlignToBars (e.aligned);
                }
                recorder.add (e.beat, e.status, e.data1, e.data2);
                lastRecordBeat = std::max (lastRecordBeat, e.beat);
            }
    }

    void PhrasePanel::stopRecording()
    {
        proc.recordingGuitar = false;
        drainRecording();
        recordButton.setToggleState (false, juce::dontSendNotification);
        if (playbackForRecording)
        {
            proc.setInternalPlaying (false); // Play was started by the count-in
            playbackForRecording = false;
        }

        auto notes = recorder.finish (lastRecordBeat + 0.25); // notes still ringing end here
        if (notes.empty())
        {
            statusLabel.setText ("Nothing was recorded. Is the guitar tracked (green notes on the fretboard)?", juce::dontSendNotification);
            return;
        }

        pushUndo();

        // Chord groups from the recorder start at 0: move them after the phrase's own groups.
        int groups = 0;
        double end = 0.0;
        for (auto& n : notes)
        {
            if (n.group >= 0) { groups = std::max (groups, n.group + 1); n.group += proc.phrase.nextGroup; }
            end = std::max (end, n.start + n.length);
            proc.phrase.notes.push_back (n);
        }
        proc.phrase.nextGroup += groups;

        // Make the phrase long enough (1, 2, 4 ... 512 bars, as in the Length box).
        for (int bars : { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 })
            if (bars * 4.0 >= end || bars == 512)
            {
                proc.phrase.lengthBeats = std::max (proc.phrase.lengthBeats, bars * 4.0);
                break;
            }

        tab.cursor = std::min (end, proc.phrase.lengthBeats - stepBeats());
        tab.selected = -1;
        commit();
        statusLabel.setText ("Recorded " + juce::String ((int) notes.size()) + " notes. Undo removes the whole take.",
                             juce::dontSendNotification);
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
        tab.selection.clear();
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

        tab.selection = { tab.selected };
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
        barsBox.setSelectedId (juce::jlimit (1, 512, (int) std::round (proc.phrase.lengthBeats / 4.0)), juce::dontSendNotification);
        loadingControls = false;

        tab.gridStep = stepBeats();
        tab.chords = chordTimeline (proc.phrase, proc.instrument);
        // Zoom. "Auto" (the start): short phrases fill the width, long ones scroll.
        const float fit = (tabViewport.getWidth() - kTabLeft * 2) / (float) std::max (1.0, proc.phrase.lengthBeats);
        tab.pxPerBeat = autoFit ? std::max (kPxPerBeat, fit) : juce::jlimit (1.0f, 400.0f, kPxPerBeat * zoom);
        tab.setSize ((int) (kTabLeft * 2 + proc.phrase.lengthBeats * tab.pxPerBeat), juce::jmax (60, tabViewport.getHeight() - 12));
        tab.repaint();

        statusLabel.setText (juce::String ((int) proc.phrase.notes.size()) + " notes   cursor: bar "
                             + juce::String ((int) (tab.cursor / 4) + 1) + ", beat " + juce::String (std::fmod (tab.cursor, 4.0) + 1.0, 2),
                             juce::dontSendNotification);

        board.setMarks (marks());
    }

    void PhrasePanel::timerTick()
    {
        playButton.setButtonText (proc.isInternalPlaying() ? "Stop" : "Play");

        if (proc.isCountingIn())
        {
            static const char* counts[] = { "", "1", "1  2", "1  2  3", "1  2  3  4" };
            statusLabel.setText ("Count-in:  " + juce::String (counts[std::clamp (proc.countInBeat.load(), 0, 4)]) + "  ...",
                                 juce::dontSendNotification);
        }
        else if (proc.recordingGuitar.load())
        {
            drainRecording();
            statusLabel.setText (recordStarted ? "Recording... " + juce::String (recorder.notesSoFar()) + " notes so far."
                                               : "Recording... waiting for your first note.", juce::dontSendNotification);
        }
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

    // -----------------------------------------------------------------------
    // Export
    // -----------------------------------------------------------------------
    TabExportInfo PhrasePanel::exportInfo (const juce::String& title) const
    {
        TabExportInfo info;
        info.title = title.toStdString();
        info.bpm = proc.currentBpm();
        return info;
    }

    void PhrasePanel::showExportMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "MIDI file (.mid) - for your DAW");
        m.addItem (2, "Text tab (.txt) - e|--3--5--|");
        m.addItem (3, "PDF tab (.pdf) - to print or share");
        m.addItem (4, "MusicXML (.musicxml) - MuseScore, Guitar Pro, Sibelius, Finale");
        m.addItem (6, "Guitar Pro 3 (.gp3) - Guitar Pro, TuxGuitar, Songsterr", proc.instrument.numStrings() <= 7);
        m.addSeparator();
        m.addItem (5, "Copy text tab to the clipboard");
        juce::Component::SafePointer<PhrasePanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [safe] (int kind)
        {
            if (safe == nullptr || kind == 0) return;
            if (kind == 5)
            {
                juce::SystemClipboard::copyTextToClipboard (juce::String (phraseToTextTab (safe->proc.phrase, safe->proc.instrument,
                                                                                         safe->exportInfo ("Guitar Lab phrase"))));
                safe->statusLabel.setText ("Text tab copied. Paste it anywhere with Ctrl + V.", juce::dontSendNotification);
                return;
            }
            safe->exportAs (kind);
        });
    }

    void PhrasePanel::exportAs (int kind)
    {
        static const char* extensions[] = { "", "mid", "txt", "pdf", "musicxml", "", "gp3" };
        static const char* what[] = { "", "a MIDI file", "text tab", "a PDF", "MusicXML", "", "a Guitar Pro 3 file" };
        const juce::String ext (extensions[kind]);
        chooser = std::make_unique<juce::FileChooser> ("Save the phrase as " + juce::String (what[kind]),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                           .getChildFile ("GuitarLab phrase." + ext),
                                                       "*." + ext);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, kind, ext] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            file = file.withFileExtension (ext);
            const auto info = exportInfo (file.getFileNameWithoutExtension());
            bool ok = false;
            file.deleteFile();
            if (kind == 1)
            {
                juce::FileOutputStream out (file);
                ok = out.openedOk() && buildMidiFile().writeTo (out);
            }
            else
            {
                const std::string data = kind == 2 ? phraseToTextTab (proc.phrase, proc.instrument, info)
                                       : kind == 3 ? phraseToPdf (proc.phrase, proc.instrument, info)
                                       : kind == 6 ? phraseToGuitarPro3 (proc.phrase, proc.instrument, info)
                                                   : phraseToMusicXml (proc.phrase, proc.instrument, info);
                juce::FileOutputStream out (file);
                ok = out.openedOk() && out.write (data.data(), data.size());
            }
            statusLabel.setText (ok ? "Saved " + file.getFileName() : juce::String ("Could not save the file."), juce::dontSendNotification);
        });
    }

    // -----------------------------------------------------------------------
    // Selected notes
    // -----------------------------------------------------------------------
    std::vector<int> PhrasePanel::selectedIndices() const
    {
        std::vector<int> out;
        for (int i : tab.selection)
            if (i >= 0 && i < (int) proc.phrase.notes.size()) out.push_back (i);
        if (out.empty() && tab.selected >= 0 && tab.selected < (int) proc.phrase.notes.size())
            out.push_back (tab.selected);
        std::sort (out.begin(), out.end());
        out.erase (std::unique (out.begin(), out.end()), out.end());
        return out;
    }

    void PhrasePanel::selectMatching (const std::vector<NoteEvent>& wanted)
    {
        // Indices change when the phrase is sorted: find the notes again by string, fret and start.
        tab.selection.clear();
        for (const auto& w : wanted)
            for (int i = 0; i < (int) proc.phrase.notes.size(); ++i)
            {
                const auto& n = proc.phrase.notes[(size_t) i];
                if (n.string == w.string && n.fret == w.fret && std::abs (n.start - w.start) < 1e-6 && ! tab.isSelected (i))
                {
                    tab.selection.push_back (i);
                    break;
                }
            }
        tab.selected = tab.selection.empty() ? -1 : tab.selection.front();
        if (tab.selected >= 0) loadControlsFrom (proc.phrase.notes[(size_t) tab.selected]);
        board.setMarks (marks());
        tab.repaint();
    }

    void PhrasePanel::selectAll()
    {
        tab.selection.clear();
        for (int i = 0; i < (int) proc.phrase.notes.size(); ++i) tab.selection.push_back (i);
        tab.selected = tab.selection.empty() ? -1 : 0;
        tab.repaint();
        statusLabel.setText (juce::String ((int) tab.selection.size()) + " notes selected.", juce::dontSendNotification);
    }

    void PhrasePanel::copySelection()
    {
        const auto idx = selectedIndices();
        if (idx.empty()) { statusLabel.setText ("Select notes first (click, Ctrl + click, or drag a box).", juce::dontSendNotification); return; }
        clipboard.clear();
        double first = 1e9;
        for (int i : idx) first = std::min (first, proc.phrase.notes[(size_t) i].start);
        for (int i : idx)
        {
            auto n = proc.phrase.notes[(size_t) i];
            n.start -= first;
            clipboard.push_back (n);
        }
        statusLabel.setText ("Copied " + juce::String ((int) clipboard.size()) + " notes. Move the cursor and press Paste.",
                             juce::dontSendNotification);
    }

    void PhrasePanel::deleteSelection()
    {
        auto idx = selectedIndices();
        if (idx.empty()) return;
        pushUndo();
        for (auto it = idx.rbegin(); it != idx.rend(); ++it)
            proc.phrase.notes.erase (proc.phrase.notes.begin() + *it);
        tab.selection.clear();
        tab.selected = -1;
        commit();
    }

    void PhrasePanel::pasteClipboard()
    {
        if (clipboard.empty()) { statusLabel.setText ("Nothing copied yet.", juce::dontSendNotification); return; }
        pushUndo();

        // Chords keep their own (new) group numbers.
        std::map<int, int> groups;
        std::vector<NoteEvent> pasted;
        double end = tab.cursor;
        for (auto n : clipboard)
        {
            n.start += tab.cursor;
            if (n.group >= 0)
            {
                if (! groups.count (n.group)) groups[n.group] = proc.phrase.nextGroup++;
                n.group = groups[n.group];
            }
            end = std::max (end, n.start + n.length);
            proc.phrase.notes.push_back (n);
            pasted.push_back (n);
        }
        // Make the phrase long enough.
        for (int bars : { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 })
            if (bars * 4.0 >= end || bars == 512) { proc.phrase.lengthBeats = std::max (proc.phrase.lengthBeats, bars * 4.0); break; }

        tab.cursor = std::min (end, proc.phrase.lengthBeats - stepBeats());
        commit();
        selectMatching (pasted);
        statusLabel.setText ("Pasted " + juce::String ((int) pasted.size()) + " notes.", juce::dontSendNotification);
    }

    void PhrasePanel::moveSelection (double beats)
    {
        const auto idx = selectedIndices();
        if (idx.empty()) return;
        double first = 1e9;
        for (int i : idx) first = std::min (first, proc.phrase.notes[(size_t) i].start);
        beats = std::max (beats, -first); // not before the start
        if (std::abs (beats) < 1e-9) return;

        pushUndo();
        std::vector<NoteEvent> moved;
        for (int i : idx)
        {
            auto& n = proc.phrase.notes[(size_t) i];
            n.start = std::min (n.start + beats, proc.phrase.lengthBeats - 0.05);
            moved.push_back (n);
        }
        commit();
        selectMatching (moved);
    }

    void PhrasePanel::transposeSelection (int semitones)
    {
        const auto idx = selectedIndices();
        if (idx.empty()) return;
        for (int i : idx)
        {
            const int f = proc.phrase.notes[(size_t) i].fret + semitones;
            if (f < 0 || f > kNumFrets) { statusLabel.setText ("Cannot move past the nut or fret 24.", juce::dontSendNotification); return; }
        }
        pushUndo();
        std::vector<NoteEvent> moved;
        for (int i : idx)
        {
            auto& n = proc.phrase.notes[(size_t) i];
            n.fret += semitones;
            n.slideToFret = juce::jlimit (0, kNumFrets, n.slideToFret + semitones);
            moved.push_back (n);
        }
        commit();
        selectMatching (moved);
    }

    void PhrasePanel::moveSelectionString (int direction)
    {
        const auto idx = selectedIndices();
        if (idx.empty()) return;
        const auto& tuning = proc.instrument.tuning();
        for (int i : idx)
        {
            const auto& n = proc.phrase.notes[(size_t) i];
            const int s = n.string + direction;
            const int f = s >= 0 && s < proc.instrument.numStrings() ? n.fret + tuning[(size_t) n.string] - tuning[(size_t) s] : -1;
            if (f < 0 || f > kNumFrets) { statusLabel.setText ("That note cannot be played on the next string.", juce::dontSendNotification); return; }
        }
        pushUndo();
        std::vector<NoteEvent> moved;
        for (int i : idx)
        {
            auto& n = proc.phrase.notes[(size_t) i];
            const int s = n.string + direction;
            const int shift = tuning[(size_t) n.string] - tuning[(size_t) s];
            n.fret += shift;
            n.slideToFret = juce::jlimit (0, kNumFrets, n.slideToFret + shift);
            n.string = s;
            moved.push_back (n);
        }
        commit();
        selectMatching (moved);
    }

    bool PhrasePanel::handleKey (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        const bool ctrl = mods.isCommandDown() || mods.isCtrlDown();
        const int code = k.getKeyCode();
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
        if (ctrl && (code == 'C' || code == 'c')) { copySelection(); return true; }
        if (ctrl && (code == 'X' || code == 'x')) { copySelection(); deleteSelection(); return true; }
        if (ctrl && (code == 'V' || code == 'v')) { pasteClipboard(); return true; }
        if (ctrl && (code == 'A' || code == 'a')) { selectAll(); return true; }
        if (ctrl && (code == 'Z' || code == 'z')) { undo(); return true; }
        if (code == juce::KeyPress::leftKey)  { moveSelection (-stepBeats()); return true; }
        if (code == juce::KeyPress::rightKey) { moveSelection (stepBeats()); return true; }
        if (code == juce::KeyPress::upKey)    { if (mods.isShiftDown()) moveSelectionString (-1); else transposeSelection (1); return true; }
        if (code == juce::KeyPress::downKey)  { if (mods.isShiftDown()) moveSelectionString (1); else transposeSelection (-1); return true; }
        return false;
    }

    void PhrasePanel::setZoom (float factor)
    {
        if (factor <= 0.0f)
        {
            // Fit: the whole phrase in the window
            const float fit = (tabViewport.getWidth() - kTabLeft * 2) / (float) std::max (1.0, proc.phrase.lengthBeats);
            zoom = std::max (0.02f, fit / kPxPerBeat);
        }
        else
            zoom = juce::jlimit (0.02f, 8.0f, factor);
        autoFit = false;

        // Keep the cursor in view.
        const double cursorBeat = tab.cursor;
        refresh();
        const int x = (int) (kTabLeft + cursorBeat * tab.pxPerBeat) - tabViewport.getWidth() / 2;
        tabViewport.setViewPosition (std::max (0, x), 0);
    }

    void PhrasePanel::updateLoopButton()
    {
        loopToggle.setToggleState (proc.loopEnd.load() > proc.loopStart.load(), juce::dontSendNotification);
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
        put (a, recordButton, 120);
        put (a, snapLabel, 40);        put (a, snapBox, 80);

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
        put (c, toReharmButton, 120);

        auto d = row();
        put (d, loopToggle, 110);      put (d, metronomeToggle, 100);  put (d, countInToggle, 90);
        put (d, zoomLabel, 44);        put (d, zoomOutButton, 28);     put (d, zoomInButton, 28);  put (d, zoomFitButton, 40);
        put (d, editLabel, 66);
        put (d, copyButton, 50);       put (d, cutButton, 44);         put (d, pasteButton, 52);
        put (d, earlierButton, 28);    put (d, laterButton, 28);       put (d, pitchUpButton, 32); put (d, pitchDownButton, 32);
        statusLabel.setBounds (d);

        tabViewport.setBounds (r);
        refresh();
    }
}
