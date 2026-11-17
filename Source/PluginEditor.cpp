#include "PluginEditor.h"

using namespace gl;

GuitarLabEditor::GuitarLabEditor (GuitarLabProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      instrumentBar (p),
      phrasePanel (p, fretboard),
      reharmPanel (p, fretboard, piano),
      trainerPanel (p),
      trackerPanel (p)
{
    setLookAndFeel (&lnf);
    tooltipWindow.setLookAndFeel (&lnf);

    addAndMakeVisible (instrumentBar);
    addAndMakeVisible (fretboard);
    addAndMakeVisible (piano);

    tabs.addTab ("Play & Write",  theme::panel, &phrasePanel,  false);
    tabs.addTab ("Reharm Lab",    theme::panel, &reharmPanel,  false);
    tabs.addTab ("Trainer",       theme::panel, &trainerPanel, false);
    tabs.addTab ("Guitar -> MIDI", theme::panel, &trackerPanel, false);
    tabs.getTabbedButtonBar().getTabButton (TrackerTab)->setTooltip (
        "Guitar -> MIDI\nPlay your real guitar and turn it into MIDI in real time: notes, bends, slides, vibrato and velocity. "
        "Works with a normal guitar (single notes) or a hex pickup (one input per string, chords too). "
        "Calibrate it to your guitar first.");
    tabs.getTabbedButtonBar().getTabButton (PlayTab)->setTooltip (
        "Play & Write\nPlay the fretboard live (bends and slides with the mouse) and write MIDI phrases with "
        "hammer-ons, pull-offs, bends, slides, vibrato, harmonics and strummed chords.");
    tabs.getTabbedButtonBar().getTabButton (ReharmTab)->setTooltip (
        "Reharm Lab\nType a progression (like I V vi IV) and get tritone subs, borrowed chords, passing diminished chords "
        "and more, with voicings you can actually play.");
    tabs.getTabbedButtonBar().getTabButton (TrainerTab)->setTooltip (
        "Trainer\nQuizzes on note names, finding notes, arpeggios and intervals. Focus on any strings and frets "
        "(like only the 4th string, or past the 12th fret). It learns your weak spots.");
    tabs.setTabBarDepth (28);
    tabs.onChange = [this] { updateDisplay(); };
    addAndMakeVisible (tabs);

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

    // Only rebuild panels when the string count or tuning really changed.
    if (lastTuning != proc.instrument.tuning())
    {
        lastTuning = proc.instrument.tuning();
        trainerPanel.refresh();
        reharmPanel.refresh();
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
    phrasePanel.timerTick();
    trackerPanel.timerTick(); // always: the frame queue must be emptied even when the tab is hidden
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
}
