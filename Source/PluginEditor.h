#pragma once

#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/InstrumentBar.h"
#include "UI/PhrasePanel.h"
#include "UI/PianoComponent.h"
#include "UI/ReharmPanel.h"
#include "UI/Theme.h"
#include "UI/TrainerPanel.h"

class GuitarLabEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit GuitarLabEditor (GuitarLabProcessor&);
    ~GuitarLabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Tab { PlayTab = 0, ReharmTab = 1, TrainerTab = 2 };

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
    int  currentTab() const { return tabs.getCurrentTabIndex(); }

    GuitarLabProcessor& proc;
    gl::theme::LookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 450 };

    gl::InstrumentBar      instrumentBar;
    gl::FretboardComponent fretboard;
    gl::PianoComponent     piano;
    gl::PhrasePanel        phrasePanel;
    gl::ReharmPanel        reharmPanel;
    gl::TrainerPanel       trainerPanel;
    Tabs tabs;

    std::vector<int> lastTuning;
    std::vector<gl::FretPos> pianoSpots;  // spots shown after clicking a piano key
    int pianoLiveString = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarLabEditor)
};
