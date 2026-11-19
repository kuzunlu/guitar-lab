#include "UI/ShapesPanel.h"

namespace gl
{
    namespace
    {
        // Name of a note's job in the chord, from its distance to the root and the other notes in the chord.
        juce::String degreeName (int semis, const std::vector<int>& intervals)
        {
            auto has = [&] (int i) { return std::find (intervals.begin(), intervals.end(), i) != intervals.end(); };
            const bool seventh = has (10) || has (11);
            switch (((semis % 12) + 12) % 12)
            {
                case 0:  return "R";
                case 1:  return "b9";
                case 2:  return has (3) || has (4) ? "9" : "2";
                case 3:  return has (4) ? "#9" : "b3";
                case 4:  return "3";
                case 5:  return has (3) || has (4) ? "11" : "4";
                case 6:  return has (7) || has (11) ? "#11" : "b5";
                case 7:  return "5";
                case 8:  return has (7) ? (seventh ? "b13" : "b6") : "#5";
                case 9:  return has (6) && has (3) && ! seventh ? "bb7" : (seventh ? "13" : "6");
                case 10: return "b7";
                default: return "7";
            }
        }

        struct Area { const char* name; int lo, hi; };
        const Area kAreas[] = {
            { "Whole neck", 0, kNumFrets },         { "Open position (frets 0-4)", 0, 4 }, { "Frets 1-6", 1, 6 },
            { "Frets 5-10", 5, 10 },                { "Frets 7-12", 7, 12 },               { "Frets 10-15", 10, 15 },
            { "Frets 12-17", 12, 17 },              { "Frets 15-24", 15, kNumFrets },
        };

        struct Type { const char* label; const char* suffix; };
        const Type kTypes[] = {
            { "major", "" }, { "minor (m)", "m" }, { "power chord (5)", "5" }, { "dominant 7th (7)", "7" },
            { "major 7th (maj7)", "maj7" }, { "minor 7th (m7)", "m7" }, { "half-diminished (m7b5)", "m7b5" },
            { "diminished (dim)", "dim" }, { "diminished 7th (dim7)", "dim7" }, { "augmented (aug)", "aug" },
            { "sus2", "sus2" }, { "sus4", "sus4" }, { "7sus4", "7sus4" }, { "9sus4", "9sus4" },
            { "6", "6" }, { "m6", "m6" }, { "6/9", "6/9" }, { "add9", "add9" }, { "madd9", "madd9" },
            { "9", "9" }, { "maj9", "maj9" }, { "m9", "m9" }, { "11", "11" }, { "m11", "m11" },
            { "13", "13" }, { "maj13", "maj13" }, { "m13", "m13" }, { "m(maj7)", "m(maj7)" },
            { "7b9", "7b9" }, { "7#9", "7#9" }, { "7#11", "7#11" }, { "7b13", "7b13" }, { "7#5", "7#5" },
            { "7alt", "7alt" }, { "maj7#11", "maj7#11" },
        };
    }

    // =======================================================================
    // ShapeGrid
    // =======================================================================
    int ShapeGrid::heightFor (int width) const
    {
        const int cols = columnsFor (width);
        return std::max (1, (int) ((shapes.size() + (size_t) cols - 1) / (size_t) cols)) * kCardH + 8;
    }

    int ShapeGrid::cardAt (juce::Point<int> p) const
    {
        const int cols = columnsFor (getWidth());
        const int c = p.x / kCardW, r = p.y / kCardH;
        if (c >= cols) return -1;
        const int i = r * cols + c;
        return i >= 0 && i < (int) shapes.size() ? i : -1;
    }

    void ShapeGrid::mouseDown (const juce::MouseEvent& e)
    {
        const int i = cardAt (e.getPosition());
        if (i >= 0 && onClick) onClick (i);
    }

    juce::String ShapeGrid::getTooltip()
    {
        const int i = cardAt (getMouseXYRelative());
        if (i < 0)
            return "Chord shapes\nEvery playable way to play this chord on your guitar, with your tuning.\n"
                   "First (gold title): the fingerings most guitarists use - the open chord, then barre chords "
                   "(E shape, A shape), D shapes and common jazz shapes. After them: all other shapes, lowest on the neck first.\n"
                   "Click a shape to hear it and see it on the big fretboard.\n"
                   "In each diagram: the thickest string is on the left, the number on the left is the fret, "
                   "x = do not play that string, o = play it open. Orange = root, blue = other chord notes.";
        const auto& v = shapes[(size_t) i];
        juce::StringArray names;
        for (int n : v.notes (inst))
            names.add (juce::String (pitchClassName (pitchClassOf (n), flats)));
        return juce::String (voicingToText (v)) + "\nNotes, low string to high: " + names.joinIntoString (" ")
             + "\nClick to hear it and to show it on the fretboard (with each note's job in the chord).";
    }

    void ShapeGrid::paint (juce::Graphics& g)
    {
        const int cols = columnsFor (getWidth());
        for (int i = 0; i < (int) shapes.size(); ++i)
        {
            const auto r = juce::Rectangle<float> ((float) ((i % cols) * kCardW), (float) ((i / cols) * kCardH),
                                                   (float) kCardW, (float) kCardH).reduced (4.0f);
            drawCard (g, r, shapes[(size_t) i], i == selected,
                      i < (int) kinds.size() ? kinds[(size_t) i] : juce::String());
        }
        if (shapes.empty())
        {
            g.setColour (theme::textDim);
            g.drawText ("No shapes. Try another chord, a wider hand stretch, or another part of the neck.",
                        getLocalBounds().reduced (10), juce::Justification::centredTop);
        }
    }

    void ShapeGrid::drawCard (juce::Graphics& g, juce::Rectangle<float> r, const Voicing& v, bool isSelected,
                              const juce::String& kind) const
    {
        g.setColour (isSelected ? theme::panelLight.brighter (0.1f) : theme::panelLight);
        g.fillRoundedRectangle (r, 6.0f);
        if (isSelected)
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (r, 6.0f, 2.0f);
        }

        const int n = (int) v.frets.size();
        int lo = 99, hi = 0;
        for (int f : v.frets) if (f > 0) { lo = std::min (lo, f); hi = std::max (hi, f); }
        if (lo == 99) lo = hi = 1;
        const int first = hi <= 5 ? 1 : lo;          // show the nut when the shape fits in the first 5 frets
        const int rows = std::max (5, hi - first + 1);

        auto area = r.reduced (16.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (24.0f);
        const float gap = area.getWidth() / (float) std::max (1, n - 1);
        const float fretH = area.getHeight() / (float) rows;

        // Title: the usual name of this fingering (gold), or where on the neck it is
        const bool hasOpen = std::find (v.frets.begin(), v.frets.end(), 0) != v.frets.end();
        if (kind.isNotEmpty())
        {
            g.setColour (theme::accent);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (kind, r.withHeight (16.0f).translated (0, 2), juce::Justification::centred);
        }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (first == 1 && lo <= 4 && hasOpen ? "Open" : "Fret " + juce::String (lo),
                        r.withHeight (16.0f).translated (0, 2), juce::Justification::centred);
        }

        // Nut / frets / strings
        g.setColour (theme::fretWire.withAlpha (0.6f));
        for (int k = 0; k <= rows; ++k)
            g.drawHorizontalLine ((int) (area.getY() + k * fretH), area.getX(), area.getRight());
        if (first == 1)
        {
            g.setColour (theme::nut);
            g.fillRect (area.getX(), area.getY() - 2.0f, area.getWidth(), 3.0f);
        }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (first), juce::Rectangle<float> (r.getX(), area.getY(), 14.0f, fretH), juce::Justification::centred);
        }
        g.setColour (theme::stringCol.withAlpha (0.7f));
        for (int s = 0; s < n; ++s)
        {
            const float x = area.getX() + (float) (n - 1 - s) * gap; // lowest string (highest index) on the left
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }

        // Dots, open strings and muted strings
        const float dot = std::min (gap, fretH) * 0.78f;
        for (int s = 0; s < n; ++s)
        {
            const float x = area.getX() + (float) (n - 1 - s) * gap;
            const int f = v.frets[(size_t) s];
            if (f == kMuted || f == 0)
            {
                g.setColour (theme::textDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (f == kMuted ? "x" : "o", juce::Rectangle<float> (x - 6.0f, area.getY() - 15.0f, 12.0f, 12.0f),
                            juce::Justification::centred);
                continue;
            }
            const int note = inst.noteAt ({ s, f });
            const bool isRoot = pitchClassOf (note) == root;
            const float y = area.getY() + ((float) (f - first) + 0.5f) * fretH;
            g.setColour (isRoot ? theme::root : theme::chordTone);
            g.fillEllipse (x - dot / 2, y - dot / 2, dot, dot);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (std::max (7.0f, dot * 0.55f), juce::Font::bold));
            g.drawText (juce::String (pitchClassName (pitchClassOf (note), flats)),
                        juce::Rectangle<float> (x - dot / 2, y - dot / 2, dot, dot), juce::Justification::centred);
        }

        // Fret text under the diagram
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (voicingToText (v), r.removeFromBottom (20.0f), juce::Justification::centred);
    }

    // =======================================================================
    // ShapesPanel
    // =======================================================================
    ShapesPanel::ShapesPanel (GuitarLabProcessor& p) : proc (p)
    {
        auto label = [this] (juce::Label& l, const juce::String& text, const juce::String& tip)
        {
            l.setText (text, juce::dontSendNotification);
            l.setTooltip (tip);
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        };

        const juce::String chordTip ("Chord\nType any chord symbol, then press Enter (or just keep typing). Examples:\n"
            "C, Am, F#m7b5, Bbmaj9, G7#9, E7alt, Dadd9, C6/9, Asus2, G13(#11), Cm(maj7), D5, Bdim7, Cø, C-7, CM7.\n"
            "Slash chords put a note in the bass: D/F#, C/E, Am7/G.\n"
            "Or pick a root and a chord type in the two boxes on the right.");
        label (chordLabel, "Chord", chordTip);
        chordInput.setTooltip (chordTip);
        chordInput.setText ("C", juce::dontSendNotification);
        chordInput.setTextToShowWhenEmpty ("e.g. Cmaj7, F#m7b5, D/F#", theme::textDim);
        chordInput.onReturnKey = [this] { search(); };
        chordInput.onTextChange = [this] { if (! updating) search(); };
        addAndMakeVisible (chordInput);

        label (rootLabel, "Root", "Root\nThe note the chord is built on. Picking one here writes the chord in the Chord box.");
        rootBox.setTooltip (rootLabel.getTooltip());
        for (int pc = 0; pc < 12; ++pc)
            rootBox.addItem (juce::String (pitchClassName (pc, pc == 3 || pc == 8 || pc == 10)), pc + 1);
        rootBox.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (rootBox);

        label (typeLabel, "Type", "Chord type\nThe kind of chord. Picking one here writes the chord in the Chord box. "
                                  "You can type many more kinds yourself (any extension, alteration or slash bass).");
        typeBox.setTooltip (typeLabel.getTooltip());
        for (int i = 0; i < (int) std::size (kTypes); ++i)
            typeBox.addItem (kTypes[i].label, i + 1);
        typeBox.setSelectedId (1, juce::dontSendNotification);
        typeBox.setTextWhenNothingSelected ("(your own, typed)");
        addAndMakeVisible (typeBox);

        auto fromBoxes = [this]
        {
            const int pc = rootBox.getSelectedId() - 1, t = typeBox.getSelectedId() - 1;
            if (pc < 0 || t < 0) return;
            updating = true;
            chordInput.setText (juce::String (pitchClassName (pc, pc == 3 || pc == 8 || pc == 10)) + kTypes[t].suffix,
                                juce::dontSendNotification);
            updating = false;
            search();
        };
        rootBox.onChange = fromBoxes;
        typeBox.onChange = fromBoxes;

        label (stretchLabel, "Hand stretch", "Hand stretch (frets)\nThe most frets one shape may cover. 4 = a normal hand, "
                                             "5 = a wide stretch, 3 = only compact shapes.");
        stretch.setSliderStyle (juce::Slider::LinearHorizontal);
        stretch.setTextBoxStyle (juce::Slider::TextBoxRight, false, 30, 20);
        stretch.setRange (2, 6, 1);
        stretch.setValue (4, juce::dontSendNotification);
        stretch.setTooltip (stretchLabel.getTooltip());
        stretch.onValueChange = [this] { search(); };
        addAndMakeVisible (stretch);

        label (areaLabel, "Neck", "Part of the neck\nOnly show shapes in this part of the neck (the frets the hand presses).");
        areaBox.setTooltip (areaLabel.getTooltip());
        for (int i = 0; i < (int) std::size (kAreas); ++i)
            areaBox.addItem (kAreas[i].name, i + 1);
        areaBox.setSelectedId (1, juce::dontSendNotification);
        areaBox.onChange = [this] { search(); };
        addAndMakeVisible (areaBox);

        label (notesLabel, "Strings", "Strings played\nHow many strings a shape uses: from the left number to the right number. "
                                      "More strings = a fuller sound to strum. Fewer = small shapes for jazz, funk and higher voicings.\n"
                                      "Example: 3 to 4 = only small shapes. 7 to 7 = shapes that use every string of a 7-string.");
        label (notesToLabel, "to", notesLabel.getTooltip());
        notesToLabel.setJustificationType (juce::Justification::centred);
        for (auto* b : { &notesBox, &maxNotesBox })
        {
            b->setTooltip (notesLabel.getTooltip());
            b->onChange = [this, b]
            {
                // Keep from <= to: moving one past the other moves the other along.
                if (b == &notesBox && maxNotesBox.getSelectedId() < notesBox.getSelectedId())
                    maxNotesBox.setSelectedId (notesBox.getSelectedId(), juce::dontSendNotification);
                if (b == &maxNotesBox && notesBox.getSelectedId() > maxNotesBox.getSelectedId())
                    notesBox.setSelectedId (maxNotesBox.getSelectedId(), juce::dontSendNotification);
                search();
            };
            addAndMakeVisible (b);
        }
        fillStringBoxes();

        rootBass.setTooltip ("Root lowest\nOn: the lowest note of every shape is the root (or the note after '/' in a slash chord), "
                             "so the chord sounds solid and clear.\nOff: inversions too (another chord note lowest).");
        rootBass.setToggleState (true, juce::dontSendNotification);
        openStrings.setTooltip ("Open strings\nOn: shapes may use open strings. Off: only fretted notes, so a shape can be "
                                "moved up and down the neck to other keys.");
        openStrings.setToggleState (true, juce::dontSendNotification);
        innerMutes.setTooltip ("Skip a string inside\nOn: shapes may leave out one string in the middle (common in jazz "
                               "voicings; play them finger-style or mute that string with a finger).");
        for (auto* b : { &rootBass, &openStrings, &innerMutes })
        {
            b->onClick = [this] { search(); };
            addAndMakeVisible (b);
        }

        status.setColour (juce::Label::textColourId, theme::textDim);
        status.setTooltip ("What the chord is made of, and how many shapes were found.");
        addAndMakeVisible (status);

        grid.onClick = [this] (int i) { choose (i); };
        addAndMakeVisible (grid);

        memory.remember (chordInput, "chord");
        memory.remember (stretch, "stretch");
        memory.remember (areaBox, "area");
        memory.remember (notesBox, "minStrings");
        memory.remember (maxNotesBox, "maxStrings");
        memory.remember (rootBass, "rootBass");
        memory.remember (openStrings, "openStrings");
        memory.remember (innerMutes, "innerMutes");
        restoreSelection();
    }

    void ShapesPanel::restoreSelection()
    {
        // search() forgets the chosen shape; it comes back without playing.
        const int shape = memory.data().getProperty ("shape", -1);
        search();
        if (shape >= 0 && shape < (int) grid.shapes.size())
        {
            grid.selected = shape;
            memory.data().setProperty ("shape", shape, nullptr);
            grid.repaint();
            if (onDisplayChanged) onDisplayChanged();
        }
    }

    void ShapesPanel::restoreUi()
    {
        memory.restoreAll();
        fillStringBoxes();
        restoreSelection();
    }

    VoicingOptions ShapesPanel::options() const
    {
        VoicingOptions o;
        o.maxSpan = (int) stretch.getValue(); // same meaning as Hand stretch in the Reharm Lab
        o.minStrings = std::max (2, notesBox.getSelectedId());
        o.maxStrings = std::max (o.minStrings, maxNotesBox.getSelectedId());
        o.rootInBass = rootBass.getToggleState();
        o.allowOpenStrings = openStrings.getToggleState();
        o.allowInnerMutes = innerMutes.getToggleState();
        const auto& a = kAreas[(size_t) std::max (0, areaBox.getSelectedId() - 1)];
        o.minFret = a.lo;
        o.maxFret = a.hi;
        return o;
    }

    void ShapesPanel::search()
    {
        parsed = parseChordName (chordInput.getText().toStdString());

        // The Root and Type boxes follow the typed chord (empty when the chord is not one of the listed types).
        {
            const auto typed = chordInput.getText().trim();
            int rootId = 0, typeId = 0;
            for (int pc = 0; pc < 12 && typeId == 0; ++pc)
                for (int t = 0; t < (int) std::size (kTypes); ++t)
                    if (typed == juce::String (pitchClassName (pc, pc == 3 || pc == 8 || pc == 10)) + kTypes[t].suffix
                        || typed == juce::String (pitchClassName (pc, ! (pc == 3 || pc == 8 || pc == 10))) + kTypes[t].suffix)
                    { rootId = pc + 1; typeId = t + 1; break; }
            if (rootId == 0 && parsed) rootId = parsed->root + 1;
            rootBox.setSelectedId (rootId, juce::dontSendNotification);
            typeBox.setSelectedId (typeId, juce::dontSendNotification);
        }
        grid.inst = proc.instrument;
        grid.selected = -1;
        memory.data().setProperty ("shape", -1, nullptr);
        grid.shapes.clear();

        if (! parsed)
        {
            status.setText (chordInput.getText().isEmpty() ? juce::String ("Type a chord, or pick a root and a type.")
                                : "Cannot read '" + chordInput.getText() + "'. Try: C, Am, F#m7b5, Bb13, G7#9, D/F#, Cadd9, E7alt.",
                            juce::dontSendNotification);
        }
        else
        {
            grid.root = parsed->root;
            auto found = findShapes (shapeRequestFor (*parsed), proc.instrument, options(), 400);

            // A chord book page: lowest position on the neck first (open shapes first), best shapes first inside it.
            // Shapes with open strings near the nut count as "open position" (0).
            auto position = [] (const Voicing& v)
            {
                const bool open = std::find (v.frets.begin(), v.frets.end(), 0) != v.frets.end();
                return open && v.highestFret() <= 5 ? 0 : v.lowestFrettedFret();
            };
            std::stable_sort (found.begin(), found.end(), [&] (const Voicing& a, const Voicing& b) { return position (a) < position (b); });
            // The fingerings everybody plays come first (open chords, barre chords...), then all the others.
            const auto opt = options();
            grid.shapes.clear();
            grid.kinds.clear();
            for (const auto& c : commonShapes (*parsed, proc.instrument))
            {
                const auto& f = c.voicing.frets;
                const bool usesOpen = std::find (f.begin(), f.end(), 0) != f.end();
                bool inArea = true;
                for (int x : f) if (x > 0 && (x < opt.minFret || x > opt.maxFret)) inArea = false;
                if (! inArea || (usesOpen && (! opt.allowOpenStrings || opt.minFret > 0)) || c.voicing.numPlayed() < opt.minStrings || c.voicing.numPlayed() > opt.maxStrings)
                    continue;
                grid.shapes.push_back (c.voicing);
                grid.kinds.push_back (juce::String (c.kind));
            }
            const auto common = (long) grid.shapes.size();
            for (const auto& v : found)
                if (std::find (grid.shapes.begin(), grid.shapes.begin() + common, v) == grid.shapes.begin() + common)
                {
                    grid.shapes.push_back (v);
                    grid.kinds.push_back ({});
                }

            juce::StringArray names;
            for (int iv : parsed->intervals)
                names.add (juce::String (pitchClassName ((parsed->root + iv) % 12, grid.flats))
                           + " (" + degreeName (iv, parsed->intervals) + ")");
            juce::String text = chordInput.getText().trim() + " = " + names.joinIntoString ("  ");
            if (parsed->bass >= 0) text << ", " << pitchClassName (parsed->bass, grid.flats) << " in the bass";
            text << ".   " << (int) grid.shapes.size() << " shape" << (grid.shapes.size() == 1 ? "" : "s")
                 << (common > 0 ? ": the usual fingerings first (gold), then all the others" : "") << ". Click one to hear it.";
            status.setText (text, juce::dontSendNotification);
        }

        grid.setSize (std::max (100, getWidth() - 12), grid.heightFor (std::max (100, getWidth() - 12)));
        grid.repaint();
        if (onHeightChanged) onHeightChanged();
        if (onDisplayChanged) onDisplayChanged();
    }

    void ShapesPanel::choose (int index)
    {
        if (index < 0 || index >= (int) grid.shapes.size()) return;
        grid.selected = index;
        memory.data().setProperty ("shape", index, nullptr);
        grid.repaint();
        playShape (grid.shapes[(size_t) index]);
        if (onDisplayChanged) onDisplayChanged();
    }

    void ShapesPanel::playShape (const Voicing& v)
    {
        // Strum low to high (25 ms apart), let it ring, then stop. A new chord cuts off the one before.
        const int token = ++playToken;
        proc.allLiveNotesOff();
        juce::Component::SafePointer<ShapesPanel> safe (this);
        int delay = 0;
        for (int s = (int) v.frets.size() - 1; s >= 0; --s)
        {
            const int f = v.frets[(size_t) s];
            if (f == kMuted) continue;
            juce::Timer::callAfterDelay (delay, [safe, token, s, f]
            {
                if (safe != nullptr && safe->playToken == token) safe->proc.liveNoteOn (s, f);
            });
            delay += 25;
        }
        const int strings = (int) v.frets.size();
        juce::Timer::callAfterDelay (1600, [safe, token, strings]
        {
            if (safe == nullptr || safe->playToken != token) return;
            for (int s = 0; s < strings; ++s) safe->proc.liveNoteOff (s);
        });
    }

    std::vector<FretboardComponent::Mark> ShapesPanel::marks() const
    {
        std::vector<FretboardComponent::Mark> m;
        if (! parsed || grid.selected < 0 || grid.selected >= (int) grid.shapes.size()) return m;
        for (auto p : grid.shapes[(size_t) grid.selected].positions())
        {
            if (! proc.instrument.isValid (p)) continue;
            const int iv = ((pitchClassOf (proc.instrument.noteAt (p)) - parsed->root) % 12 + 12) % 12;
            m.push_back ({ p, iv == 0 ? theme::root : theme::chordTone, degreeName (iv, parsed->intervals), true });
        }
        return m;
    }

    std::map<PitchClass, juce::Colour> ShapesPanel::chordLayer() const
    {
        std::map<PitchClass, juce::Colour> layer;
        if (! parsed) return layer;
        for (int iv : parsed->intervals)
            layer[(parsed->root + iv) % 12] = iv == 0 ? theme::root : theme::chordTone;
        return layer;
    }

    void ShapesPanel::fillStringBoxes()
    {
        // Item id = number of strings (2 .. all). "To" stays at "all strings" when it was there before.
        const int n = proc.instrument.numStrings();
        const int oldCount = maxNotesBox.getNumItems() + 1;
        const int from = notesBox.getSelectedId() > 0 ? notesBox.getSelectedId() : 4;
        const int to = maxNotesBox.getSelectedId() > 0 && maxNotesBox.getSelectedId() < oldCount ? maxNotesBox.getSelectedId() : n;
        for (auto* b : { &notesBox, &maxNotesBox })
        {
            b->clear (juce::dontSendNotification);
            for (int k = 2; k <= n; ++k)
                b->addItem (juce::String (k), k);
        }
        notesBox.setSelectedId (juce::jlimit (2, n, from), juce::dontSendNotification);
        maxNotesBox.setSelectedId (juce::jlimit (notesBox.getSelectedId(), n, to), juce::dontSendNotification);
    }

    void ShapesPanel::refresh()
    {
        fillStringBoxes();
        search();
    }

    int ShapesPanel::heightFor (int width) const
    {
        // Same rows as resized(): margin, two rows of controls, the status line, then every card.
        return 6 + 30 + 30 + 24 + grid.heightFor (std::max (100, width - 12)) + 6;
    }

    void ShapesPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
    }

    void ShapesPanel::resized()
    {
        auto r = getLocalBounds().reduced (6);
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (4); return x; };
        auto put = [] (juce::Rectangle<int>& x, juce::Component& c, int w) { c.setBounds (x.removeFromLeft (w)); x.removeFromLeft (4); };

        auto a = row();
        put (a, chordLabel, 46);  put (a, chordInput, 170);
        put (a, rootLabel, 40);   put (a, rootBox, 70);
        put (a, typeLabel, 40);   put (a, typeBox, 200);

        auto b = row();
        put (b, stretchLabel, 86); put (b, stretch, 130);
        put (b, areaLabel, 40);    put (b, areaBox, 200);
        put (b, notesLabel, 54);   put (b, notesBox, 50);
        put (b, notesToLabel, 26); put (b, maxNotesBox, 50);
        put (b, rootBass, 110);    put (b, openStrings, 110);
        put (b, innerMutes, 170);

        status.setBounds (row (20));
        const int w = std::max (100, r.getWidth());
        grid.setBounds (r.getX(), r.getY(), w, grid.heightFor (w));
    }
}
