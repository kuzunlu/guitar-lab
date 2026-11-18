#pragma once

#include "PluginProcessor.h"
#include "Tracking/Calibration.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"

namespace gl
{
    // Live view: heard note, cents off, and input level, per input (hex) or one row (mono).
    class TunerView : public juce::Component,
                      public juce::TooltipClient
    {
    public:
        struct Row
        {
            juce::String name;
            float hz = 0, clarity = 0, rms = 0;
            int expected = -1;      // calibration: the note we want (-1 = none)
        };

        std::vector<Row> rows;
        float gate = 0.01f;         // level needed to start a note
        double centsCorrection = 0; // whole-guitar calibration offset

        void paint (juce::Graphics&) override;
        juce::String getTooltip() override;
    };

    class CalibrationWizard : public juce::Component
    {
    public:
        explicit CalibrationWizard (GuitarLabProcessor& p);

        void begin();                           // shows the tuning page
        void feed (const AnalysisFrame& f, double hopSeconds);
        void tick();                            // ~30 times a second: refresh texts
        bool isListening() const { return page == Page::Running; }
        int  expectedNote() const { return session.expectedNote(); }
        int  expectedString() const { return session.currentString(); }

        // What to show on the fretboard: the fret to play, or the chord shape.
        std::vector<FretPos> spotsToPlay() const;
        int displayKey() const; // changes whenever spotsToPlay() changes

        std::function<void()> onClose;          // wizard finished or cancelled

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        enum class Page { Tuning, Running, Done };
        void showPage (Page p);
        void startListening (bool full);
        void finish (bool save);
        juce::String summary() const;

        GuitarLabProcessor& proc;
        CalibrationSession session;
        Page page = Page::Tuning;

        juce::Label title, body, instruction, status, stepLabel;
        juce::TextButton startButton { "Quick calibration (1-2 min)" }, fullButton { "Full calibration (15-20 min)" },
                         endFretsButton { "My guitar ends here" }, cancelButton { "Cancel" },
                         retryButton { "Retry" }, acceptButton { "Accept" }, skipButton { "Skip" },
                         saveButton { "Save calibration" }, discardButton { "Discard" };
        double shownProgress = 0.0;
    };

    class TrackerPanel : public juce::Component
    {
    public:
        explicit TrackerPanel (GuitarLabProcessor& p);
        ~TrackerPanel() override;

        void refresh();      // instrument or calibration changed
        void timerTick();    // ~30 times a second

        // Fretboard marks while calibrating (what to play next).
        std::vector<FretboardComponent::Mark> marks() const;
        std::function<void()> onDisplayChanged;
        std::function<void (const std::vector<MidiNote>&)> onTuningWanted; // a loaded preset was made for this tuning

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void updateStatus();

        // Calibration presets: plain-text files in the user's app data folder, shared by every project.
        static juce::File presetFolder();
        void refreshPresets();
        void loadPreset (const juce::File& file);
        void savePreset();
        void deletePreset();
        void showPresetMessage (const juce::String& text);

        GuitarLabProcessor& proc;

        juce::Label modeLabel, inputLabel, safetyLabel, sensLabel, calStatus, warning, help, presetLabel;
        juce::ComboBox presetBox;
        juce::TextEditor presetName;
        juce::TextButton savePresetButton { "Save as preset" }, deletePresetButton { "Delete" };
        juce::Array<juce::File> presetFiles;
        juce::uint32 deleteArmedAt = 0;   // Delete needs a second click within 3 seconds
        juce::String presetMessage;       // shown in the warning line for a few seconds
        juce::uint32 presetMessageAt = 0;
        juce::ComboBox modeBox, inputBox;
        juce::Slider safety, sensitivity;
        juce::ToggleButton bendsToggle { "Track bends / slides / vibrato" }, monitorToggle { "Hear guitar" },
                           soundToggle { "Built-in sound" };
        juce::TextButton calibrateButton { "Calibrate..." };

        TunerView tuner;
        CalibrationWizard wizard;

        struct Last { float hz = 0, clarity = 0, rms = 0; };
        std::array<Last, kMaxStrings> last {};
        std::vector<AnalysisFrame> frameBuffer = std::vector<AnalysisFrame> (4096);
        int slowTick = 0;
        int lastDisplayKey = -1;

        using APVTS = juce::AudioProcessorValueTreeState;
        std::unique_ptr<APVTS::ComboBoxAttachment> modeAtt, inputAtt;
        std::unique_ptr<APVTS::SliderAttachment> safetyAtt, sensAtt;
        std::unique_ptr<APVTS::ButtonAttachment> bendsAtt, monitorAtt, soundAtt;
    };
}
