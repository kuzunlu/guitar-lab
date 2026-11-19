#pragma once

#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/InstrumentBar.h"
#include "UI/PhrasePanel.h"
#include "UI/PianoComponent.h"
#include "UI/ReharmPanel.h"
#include "UI/ShapesPanel.h"
#include "UI/Theme.h"
#include "UI/Tooltip.h"
#include "UI/TrackerPanel.h"
#include "UI/TrainerPanel.h"

#include <set>

class GuitarLabEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit GuitarLabEditor (GuitarLabProcessor&);
    ~GuitarLabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Tab { TrackerTab = 0, PlayTab = 1, ShapesTab = 2, ReharmTab = 3, TrainerTab = 4 };

    // TabbedComponent only tells subclasses when the tab changes.
    struct Tabs : public juce::TabbedComponent
    {
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void()> onChange;
        void currentTabChanged (int, const juce::String&) override { if (onChange) onChange(); }
    };

    void timerCallback() override;
    void updateDisplay();          // marks + layers on fretboard and piano for the current tab
    void instrumentChanged();
    void reloadAll();              // after a project/preset was loaded
    void updateChordName (const std::set<gl::MidiNote>& sounding);
    int  currentTab() const { return tabs.getCurrentTabIndex(); }
    void layoutPage();             // sizes the page; it scrolls when the open tab needs more height than the window
    int  panelHeight (int width);  // height the open tab's panel needs

    GuitarLabProcessor& proc;
    gl::theme::LookAndFeel lnf;
    gl::DelayedTooltipWindow tooltipWindow { this }; // waits 1 s on every control

    gl::InstrumentBar      instrumentBar;
    gl::FretboardComponent fretboard;
    gl::PianoComponent     piano;
    gl::PhrasePanel        phrasePanel;
    gl::ShapesPanel        shapesPanel;
    gl::ReharmPanel        reharmPanel;
    gl::TrainerPanel       trainerPanel;
    gl::TrackerPanel       trackerPanel;
    Tabs tabs;
    juce::Label chordName;                // chord analyser, at the right of the tab row
    juce::Component content;              // everything above; the page scrolls it up and down
    juce::Viewport page;
    std::set<gl::MidiNote> lastChordNotes;

    std::vector<int> lastTuning;
    std::vector<gl::FretPos> pianoSpots;  // spots shown after clicking a piano key
    int pianoLiveString = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarLabEditor)
};
