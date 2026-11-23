#pragma once

#include "PluginProcessor.h"
#include "Tracking/Calibration.h"
#include "UI/FretboardComponent.h"
#include "UI/IntonationView.h"
#include "UI/LighthouseTuner.h"
#include "UI/Theme.h"
#include "UI/UiMemory.h"

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

    // The notes the tracker sent, newest first (to see what went wrong: a ghost octave, a double note...).
    class NoteLogView : public juce::Component,
                        public juce::TooltipClient
    {
    public:
        struct Entry { juce::int64 start = 0, end = -1; int note = 0, string = -1, fret = -1, velocity = 0; bool samePick = false; };
        void noteOn (juce::int64 sample, int note, int string, int fret, int velocity);
        void noteOff (juce::int64 sample, int note, int string);
        void clear() { entries.clear(); repaint(); }
        double rate = 48000.0;

        void paint (juce::Graphics&) override;
        juce::String getTooltip() override;

    private:
        std::vector<Entry> entries; // oldest first, at most 200
    };

    // One thing to play at a time: the player presses Listen (or Space), plays, sees OK, and the next thing appears.
    // Back goes to the thing before. The session lives in the processor, so closing the window only pauses it.
    class CalibrationWizard : public juce::Component
    {
    public:
        explicit CalibrationWizard (GuitarLabProcessor& p);

        void begin();                           // shows the tuning page
        void resume();                          // the window was opened again during calibration: go on where it was
        bool keyPressed (const juce::KeyPress&) override; // Space / Enter = Listen
        void feed (const AnalysisFrame& f, double hopSeconds);
        void tick();                            // ~30 times a second: refresh texts
        void restoreUi() { memory.restoreAll(); } // after a project was loaded
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

        void startTracker();                    // tracker without calibration, sending no MIDI, while the wizard listens
        void updateButtons();

        GuitarLabProcessor& proc;
        CalibrationSession& session = proc.work.calSession;
        Page page = Page::Tuning;

        juce::Label title, body, headline, instruction, status, stepLabel;
        juce::String headlineText() const;      // the current step in a few big words ("String 6 (E2) · fret 7")
        juce::TextButton startButton { "Quick calibration (1-2 min)" }, fullButton { "Full calibration (15-20 min)" },
                         endFretsButton { "My guitar ends here" }, cancelButton { "Cancel" },
                         listenButton { "Listen" }, backButton { "Back" }, acceptButton { "Use it anyway" }, skipButton { "Skip" },
                         saveButton { "Save calibration" }, discardButton { "Discard" };
        juce::TextButton tryButton { "Try it" };
        juce::Label tryLabel;                   // "Try it": the notes heard right now, with the new calibration
        void startTrying();
        void stopTrying();
        bool trying() const { return proc.work.calTryBackup.has_value(); }
        juce::ToggleButton autoToggle { "Auto listen" }, cuesToggle { "Sound cues" };
        juce::Slider autoDelay;
        void playCues();                        // a cue when listening starts, a step passes, or something is wrong
        int lastCuePhase = -1, lastCueStep = -1;
        double shownProgress = 0.0;
        juce::uint32 readySince = 0;            // auto listen: when the current step became ready
        int autoPausedStep = -1;                // "Stop listening" was pressed on this step: no auto listen until the next

        UiMemory memory { proc.uiState, "Calibration" }; // last member: see UiMemory
    };

    class TrackerPanel : public juce::Component
    {
    public:
        explicit TrackerPanel (GuitarLabProcessor& p);
        ~TrackerPanel() override;

        void refresh();      // instrument or calibration changed
        void restoreUi();    // after a project was loaded (it may hold an unfinished calibration)
        void timerTick();    // ~30 times a second

        // Fretboard marks while calibrating (what to play next).
        std::vector<FretboardComponent::Mark> marks() const;
        std::function<void()> onDisplayChanged;
        std::function<void (const std::vector<MidiNote>&)> onTuningWanted; // a loaded preset was made for this tuning
        // Every note-on the tracker sent. movedFrom >= 0: the same pitch just ended on that string at the same moment
        // (the tracker moved the note to another string), else -1.
        std::function<void (int string, int fret, double seconds, int movedFrom)> onTrackedNote;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void updateStatus();

        // Calibration presets: plain-text files in the user's app data folder, shared by every project.
        static juce::File presetFolder();
        void refreshPresets();
        void loadPreset (const juce::File& file);
        void loadDefaultCalibration();
        static constexpr int kDefaultPresetId = 1; // preset files use ids 2, 3, ...
        void savePreset();
        void deletePreset();
        void showPresetMessage (const juce::String& text);

        GuitarLabProcessor& proc;

        juce::Label modeLabel, inputLabel, safetyLabel, sensLabel, hystLabel, calStatus, warning, help, presetLabel;
        juce::ComboBox presetBox;
        juce::Label styleLabel;
        juce::ComboBox styleBox;                // playing style: sets safety, sensitivity and hysteresis together
        void applyStyle (int id);
        int  matchingStyle() const;             // the style the knobs are set to now (1 = Custom)
        juce::TextEditor presetName;
        juce::TextButton savePresetButton { "Save as preset" }, deletePresetButton { "Delete" };
        juce::Array<juce::File> presetFiles;
        juce::uint32 deleteArmedAt = 0;   // Delete needs a second click within 3 seconds
        juce::String presetMessage;       // shown in the warning line for a few seconds
        juce::uint32 presetMessageAt = 0;
        juce::ComboBox modeBox, inputBox;
        juce::Slider safety, sensitivity, hysteresis;
        juce::ToggleButton bendsToggle { "Track bends / slides / vibrato" }, monitorToggle { "Hear guitar" },
                           soundToggle { "Built-in sound" };
        juce::TextButton calibrateButton { "Calibrate..." }, intonationButton { "Intonation..." }, tunerButton { "Tuner..." };
        IntonationView intonation;
        LighthouseTuner lighthouse { proc };

        TunerView tuner;
        NoteLogView noteLog;
        juce::TextButton clearLogButton { "Clear" };
        std::vector<GuitarLabProcessor::TrackedEvent> trackedBuffer = std::vector<GuitarLabProcessor::TrackedEvent> (1024);
        int lastOffNote = -1, lastOffString = -1;    // the last note-off (to see a note moved to another string)
        juce::int64 lastOffSample = -1;
        CalibrationWizard wizard;

        struct Last { float hz = 0, clarity = 0, rms = 0; };
        std::array<Last, kMaxStrings> last {};
        std::vector<AnalysisFrame> frameBuffer = std::vector<AnalysisFrame> (4096);
        int slowTick = 0;
        int lastDisplayKey = -1;

        using APVTS = juce::AudioProcessorValueTreeState;
        std::unique_ptr<APVTS::ComboBoxAttachment> modeAtt, inputAtt;
        std::unique_ptr<APVTS::SliderAttachment> safetyAtt, sensAtt, hystAtt;
        std::unique_ptr<APVTS::ButtonAttachment> bendsAtt, monitorAtt, soundAtt;
    };
}
