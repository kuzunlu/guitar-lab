#include "PluginEditor.h"
#include "Theory/ChordFinder.h"

using namespace gl;

GuitarLabEditor::GuitarLabEditor (GuitarLabProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      instrumentBar (p),
      phrasePanel (p, fretboard),
      shapesPanel (p),
      reharmPanel (p, fretboard, piano),
      trainerPanel (p),
      trackerPanel (p)
{
    setLookAndFeel (&lnf);
    tooltipWindow.setLookAndFeel (&lnf);

    addAndMakeVisible (instrumentBar);
    addAndMakeVisible (fretboard);
    addAndMakeVisible (piano);

    // Same order as the Tab enum. The first tab is the one that opens.
    tabs.addTab ("Guitar -> MIDI", theme::panel, &trackerPanel, false);
    tabs.addTab ("Play & Write",  theme::panel, &phrasePanel,  false);
    tabs.addTab ("Chord Shapes",  theme::panel, &shapesPanel,  false);
    tabs.addTab ("Reharm Lab",    theme::panel, &reharmPanel,  false);
    tabs.addTab ("Trainer",       theme::panel, &trainerPanel, false);
    tabs.getTabbedButtonBar().getTabButton (TrackerTab)->setTooltip (
        "Guitar -> MIDI\nPlay your real guitar and turn it into MIDI in real time: notes, bends, slides, vibrato and velocity. "
        "Works with a normal guitar (single notes) or a hex pickup (one input per string, chords too). "
        "Calibrate it to your guitar first.");
    tabs.getTabbedButtonBar().getTabButton (PlayTab)->setTooltip (
        "Play & Write\nPlay the fretboard live (bends and slides with the mouse) and write MIDI phrases with "
        "hammer-ons, pull-offs, bends, slides, vibrato, harmonics and strummed chords.");
    tabs.getTabbedButtonBar().getTabButton (ShapesTab)->setTooltip (
        "Chord Shapes\nType or pick any chord (Cmaj7, F#m7b5, G7#9, D/F#...) and see every way to play it on your guitar "
        "and tuning, as chord diagrams. Click one to hear it and see it on the fretboard.");
    tabs.getTabbedButtonBar().getTabButton (ReharmTab)->setTooltip (
        "Reharm Lab\nType a progression (like I V vi IV) and get tritone subs, borrowed chords, passing diminished chords "
        "and more, with voicings you can actually play.");
    tabs.getTabbedButtonBar().getTabButton (TrainerTab)->setTooltip (
        "Trainer\nQuizzes on note names, finding notes, arpeggios and intervals. Focus on any strings and frets "
        "(like only the 4th string, or past the 12th fret). It learns your weak spots.");
    tabs.setTabBarDepth (28);
    tabs.onChange = [this] { updateDisplay(); lastChordNotes.clear(); updateChordName ({}); };
    addAndMakeVisible (tabs);

    chordName.setJustificationType (juce::Justification::centredRight);
    chordName.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    chordName.setColour (juce::Label::textColourId, theme::accent);
    addAndMakeVisible (chordName); // after the tabs, so it sits on top of the empty end of the tab row

    // --- Wiring ---
    instrumentBar.onChange = [this] { instrumentChanged(); };

    fretboard.onPress = [this] (FretPos pos, const juce::ModifierKeys& mods)
    {
        pianoSpots.clear();
        switch (currentTab())
        {
            case PlayTab:    phrasePanel.fretPressed (pos, mods); break;
            case ReharmTab:  reharmPanel.fretPressed (pos); break;
            case TrainerTab: trainerPanel.fretPressed (pos); break;
            default: break;
        }
        updateDisplay();
    };

    fretboard.onDrag = [this] (FretPos start, double bend, double slide)
    {
        // Live bends and slides work in every tab.
        proc.liveBend (start.string, bend + slide);
    };

    fretboard.onRelease = [this] (FretPos pos)
    {
        switch (currentTab())
        {
            case PlayTab:    phrasePanel.fretReleased (pos); break;
            case ReharmTab:  reharmPanel.fretReleased(); break;
            case TrainerTab: trainerPanel.fretReleased(); break;
            default: break;
        }
    };

    piano.onKeyPress = [this] (MidiNote note)
    {
        // Play it on the first string that has it, and show every spot on the neck.
        pianoSpots = proc.instrument.positionsOf (note);
        if (! pianoSpots.empty())
        {
            proc.liveNoteOn (pianoSpots.front().string, pianoSpots.front().fret);
            pianoLiveString = pianoSpots.front().string;
        }
        if (currentTab() == TrainerTab)
            trainerPanel.pianoPressed (note);
        updateDisplay();
    };

    piano.onKeyRelease = [this] (MidiNote)
    {
        proc.liveNoteOff (pianoLiveString);
        pianoLiveString = -1;
    };

    reharmPanel.onDisplayChanged  = [this] { updateDisplay(); };
    reharmPanel.onPhraseReplaced  = [this] { phrasePanel.refresh(); tabs.setCurrentTabIndex (PlayTab); };
    trainerPanel.onDisplayChanged = [this] { updateDisplay(); };
    trackerPanel.onDisplayChanged = [this] { if (currentTab() == TrackerTab) updateDisplay(); };
    shapesPanel.onDisplayChanged  = [this] { if (currentTab() == ShapesTab) updateDisplay(); };
    trackerPanel.onTuningWanted   = [this] (const std::vector<MidiNote>& t) { instrumentBar.applyTuning (t); };

    proc.onStateLoaded = [this] { reloadAll(); };

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1500);
    setSize (1320, 860);

    instrumentChanged();
    startTimerHz (30);
}

GuitarLabEditor::~GuitarLabEditor()
{
    proc.onStateLoaded = nullptr;
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void GuitarLabEditor::reloadAll()
{
    instrumentBar.refresh();
    reharmPanel.refresh();
    shapesPanel.refresh();
    trainerPanel.refresh();
    phrasePanel.refresh();
    instrumentChanged();
}

void GuitarLabEditor::instrumentChanged()
{
    fretboard.setInstrument (proc.instrument);
    piano.setInstrument (proc.instrument);
    fretboard.setPreferFlats (instrumentBar.preferFlats());
    piano.setPreferFlats (instrumentBar.preferFlats());
    shapesPanel.setPreferFlats (instrumentBar.preferFlats());

    // Only rebuild panels when the string count or tuning really changed.
    if (lastTuning != proc.instrument.tuning())
    {
        lastTuning = proc.instrument.tuning();
        trainerPanel.refresh();
        reharmPanel.refresh();
        shapesPanel.refresh();
        phrasePanel.refresh();
        trackerPanel.refresh();
        resized(); // the fretboard height depends on the string count
    }
    updateDisplay();
}

void GuitarLabEditor::updateDisplay()
{
    std::vector<FretboardComponent::Mark> marks;
    std::map<PitchClass, juce::Colour> boardLayer, pianoLayer;
    const auto scale = instrumentBar.scaleLayer();

    switch (currentTab())
    {
        case PlayTab:
            marks = phrasePanel.marks();
            boardLayer = scale;
            pianoLayer = scale;
            fretboard.setHeatMap ({});
            fretboard.setFretRangeHighlight (-1, -1);
            fretboard.setShowAllNoteNames (instrumentBar.showAllNames());
            break;

        case ShapesTab:
            marks = shapesPanel.marks();
            boardLayer = scale;
            pianoLayer = shapesPanel.chordLayer();
            fretboard.setHeatMap ({});
            fretboard.setFretRangeHighlight (-1, -1);
            fretboard.setShowAllNoteNames (instrumentBar.showAllNames());
            break;

        case ReharmTab:
            marks = reharmPanel.marks();
            boardLayer = scale;
            pianoLayer = reharmPanel.chordLayer().empty() ? scale : reharmPanel.chordLayer();
            fretboard.setHeatMap ({});
            fretboard.setFretRangeHighlight (-1, -1);
            fretboard.setShowAllNoteNames (instrumentBar.showAllNames());
            break;

        case TrainerTab:
        {
            // No scale layer and no note names here: they would give the answers away.
            marks = trainerPanel.marks();
            const auto [lo, hi] = trainerPanel.fretRange();
            fretboard.setFretRangeHighlight (lo, hi);
            fretboard.setShowAllNoteNames (false);
            if (trainerPanel.showHeatMap())
                fretboard.setHeatMap ([this] (FretPos p) { return proc.trainer.accuracyAt (p); });
            else
                fretboard.setHeatMap ({});
            break;
        }

        case TrackerTab:
            // Tracked notes show as green "sounding" dots; the scale overlay helps to see what you play.
            // While calibrating, the fret or chord shape to play next is shown in yellow.
            marks = trackerPanel.marks();
            boardLayer = scale;
            pianoLayer = scale;
            fretboard.setHeatMap ({});
            fretboard.setFretRangeHighlight (-1, -1);
            fretboard.setShowAllNoteNames (instrumentBar.showAllNames());
            break;

        default: break;
    }

    for (auto p : pianoSpots)
        marks.push_back ({ p, theme::accent, {}, false });

    std::map<MidiNote, juce::Colour> pianoMarks;
    for (const auto& m : marks)
        if (proc.instrument.isValid (m.pos) && currentTab() != TrainerTab)
            pianoMarks[proc.instrument.noteAt (m.pos)] = m.colour;

    fretboard.setMarks (marks);
    fretboard.setPitchClassLayer (boardLayer);
    fretboard.setPreferFlats (instrumentBar.preferFlats());
    piano.setPitchClassLayer (pianoLayer);
    piano.setNoteMarks (pianoMarks);
    piano.setPreferFlats (instrumentBar.preferFlats());
    shapesPanel.setPreferFlats (instrumentBar.preferFlats());
}

void GuitarLabEditor::timerCallback()
{
    const int strings = proc.instrument.numStrings();
    std::vector<int> frets ((size_t) strings);
    std::vector<float> bends ((size_t) strings);
    std::set<MidiNote> sounding;

    for (int s = 0; s < strings; ++s)
    {
        frets[(size_t) s] = proc.soundingFret[(size_t) s].load();
        bends[(size_t) s] = proc.soundingBend[(size_t) s].load();
        if (frets[(size_t) s] >= 0)
            sounding.insert (proc.instrument.noteAt ({ s, frets[(size_t) s] }));
    }
    for (int n = 0; n < 128; ++n)
        if (proc.externalNotes[(size_t) n].load())
            sounding.insert (n);

    fretboard.setSounding (frets, bends);
    piano.setSounding (sounding);
    if (sounding != lastChordNotes)
    {
        lastChordNotes = sounding;
        updateChordName (sounding);
    }
    phrasePanel.timerTick();
    trackerPanel.timerTick(); // always: the frame queue must be emptied even when the tab is hidden
}

void GuitarLabEditor::updateChordName (const std::set<MidiNote>& sounding)
{
    // Hidden in the Trainer: it would give answers away.
    const std::vector<int> notes (sounding.begin(), sounding.end());
    const auto matches = currentTab() == TrainerTab ? std::vector<gl::ChordMatch> {}
                                                    : gl::nameChord (notes, instrumentBar.preferFlats());

    juce::String tip ("Chord analyser\n"
                      "Names the chord made by the notes sounding right now: your real guitar (Guitar -> MIDI), "
                      "the fretboard or piano, the phrase while it plays, or MIDI coming in.\n"
                      "A name after '/' is the bass note (C/E = C major with E as the lowest note). "
                      "The same notes can often be read more than one way; the other readings are listed below.");

    if (matches.empty())
    {
        chordName.setText (currentTab() == TrainerTab || notes.empty() ? juce::String()
                               : juce::String (gl::midiNoteName (notes.front(), instrumentBar.preferFlats())),
                           juce::dontSendNotification);
        chordName.setTooltip (tip);
        return;
    }

    chordName.setText ("Chord: " + juce::String (matches.front().name), juce::dontSendNotification);
    tip << "\n\nNow: " << matches.front().name << " = " << matches.front().meaning;
    for (size_t i = 1; i < matches.size() && i < 5; ++i)
        tip << "\nOr: " << matches[i].name << " = " << matches[i].meaning;
    chordName.setTooltip (tip);
}

void GuitarLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void GuitarLabEditor::resized()
{
    auto r = getLocalBounds();
    instrumentBar.setBounds (r.removeFromTop (64));
    piano.setBounds (r.removeFromBottom (juce::jlimit (70, 120, getHeight() / 9)));

    const int strings = proc.instrument.numStrings();
    const int boardHeight = juce::jlimit (180, 340, 30 * strings + 40);
    fretboard.setBounds (r.removeFromTop (boardHeight));
    tabs.setBounds (r);
    chordName.setBounds (r.getX() + r.getWidth() - 420, r.getY(), 412, 28); // empty end of the tab row
}
