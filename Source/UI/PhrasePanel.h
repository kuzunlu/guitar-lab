#pragma once

#include "Midi/PhraseRecorder.h"
#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"

namespace gl
{
    // Tab-style timeline of the phrase. Strings are rows (highest on top), time goes right.
    class TabView : public juce::Component,
                    public juce::TooltipClient
    {
    public:
        explicit TabView (GuitarLabProcessor& p) : proc (p) {}

        double cursor = 0.0;
        int    selected = -1;   // index into proc.phrase.notes, -1 = none
        double gridStep = 0.5;
        float  pxPerBeat = 52.0f;  // set by PhrasePanel to fill the width

        std::function<void()> onCursorMoved;
        std::function<void (int)> onSelect;
        std::function<void (int)> onDelete;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        float beatToX (double beat) const;
        double xToBeat (float x) const;
        int noteAt (juce::Point<float> p) const;

        GuitarLabProcessor& proc;
    };

    // Grab this with the mouse and drop it on a DAW track: the phrase arrives as a MIDI clip.
    class DragMidiArea : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        std::function<juce::File()> makeFile;

        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    private:
        bool dragging = false;
    };

    class PhrasePanel : public juce::Component
    {
    public:
        PhrasePanel (GuitarLabProcessor& p, FretboardComponent& board);
        ~PhrasePanel() override;

        // Fretboard events while this tab is shown.
        void fretPressed (FretPos pos, const juce::ModifierKeys& mods);
        void fretDragged (FretPos start, double bend, double slide);
        void fretReleased (FretPos pos);

        // Marks this panel wants on the fretboard (chord builder notes, selected note).
        std::vector<FretboardComponent::Mark> marks() const;

        void refresh();            // after the phrase was changed elsewhere
        void timerTick();          // called by the editor ~30 times a second

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void pushUndo();
        void undo();
        void commit();             // after any phrase change
        void writeNote (FretPos pos);
        void addChordFromBuilder();
        void applyControlsToSelected();
        void loadControlsFrom (const NoteEvent& n);
        NoteEvent noteFromControls (FretPos pos) const;
        double stepBeats() const;
        void exportMidi();
        void startRecording();
        void stopRecording();
        void drainRecording();
        juce::File writeTempMidi();
        juce::MidiFile buildMidiFile();

        GuitarLabProcessor& proc;
        FretboardComponent& board;

        TabView tab;
        juce::Viewport tabViewport;

        juce::ToggleButton writeToggle { "Write mode" };
        juce::ComboBox stepBox, barsBox, techniqueBox, bendBox, bendAmountBox, slideBox;
        juce::Slider   slideDistance, velocity, vibDepth, vibRate, strumMs;
        juce::ToggleButton vibratoToggle { "Vibrato" }, letRingToggle { "Let ring" }, strumDownToggle { "Down strum" };
        juce::Label stepLabel, barsLabel, techniqueLabel, bendLabel, slideLabel, velLabel, vibLabel, strumLabel, statusLabel;

        juce::TextButton playButton { "Play" }, clearButton { "Clear" }, undoButton { "Undo" },
                         chordButton { "Add chord" }, deleteButton { "Delete note" },
                         exportButton { "Export .mid" };
        DragMidiArea dragArea;

        // Recording the real guitar (tracked notes) into the phrase
        juce::TextButton recordButton { "Record guitar" };
        juce::Label snapLabel;
        juce::ComboBox snapBox;
        PhraseRecorder recorder;
        bool   recordStarted = false;    // first tracked event arrived
        double lastRecordBeat = 0.0;
        std::vector<GuitarLabProcessor::RecordEvent> recordBuffer = std::vector<GuitarLabProcessor::RecordEvent> (4096);

        std::vector<FretPos> chordBuilder;
        std::vector<std::string> undoStack;
        bool loadingControls = false;
        int  liveString = -1;

        std::unique_ptr<juce::FileChooser> chooser;
    };
}
