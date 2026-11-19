#pragma once

#include "Midi/PhraseRecorder.h"
#include "Midi/ChordTimeline.h"
#include "Midi/TabExport.h"
#include "PluginProcessor.h"
#include "UI/FretboardComponent.h"
#include "UI/Theme.h"
#include "UI/UiMemory.h"

namespace gl
{
    // Tab-style timeline of the phrase. Strings are rows (highest on top), time goes right.
    class TabView : public juce::Component,
                    public juce::TooltipClient
    {
    public:
        explicit TabView (GuitarLabProcessor& p) : proc (p) { setWantsKeyboardFocus (true); }

        double cursor = 0.0;
        int    selected = -1;          // the note the controls show (index into proc.phrase.notes), -1 = none
        std::vector<int> selection;    // every selected note (Ctrl + click, or drag a box)
        std::vector<ChordSpan> chords; // chord timeline, shown under the bar numbers
        double gridStep = 0.5;
        float  pxPerBeat = 52.0f;      // set by PhrasePanel (zoom)

        bool isSelected (int i) const { return std::find (selection.begin(), selection.end(), i) != selection.end(); }

        std::function<void()> onCursorMoved;
        std::function<void (int)> onSelect;
        std::function<void (int)> onDelete;
        std::function<void()> onSelectionChanged;
        std::function<void()> onLoopChanged;
        std::function<bool (const juce::KeyPress&)> onKey;
        std::function<void (float)> onZoom;   // Ctrl + mouse wheel: factor

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress& k) override { return onKey && onKey (k); }
        juce::String getTooltip() override;

    private:
        float beatToX (double beat) const;
        double xToBeat (float x) const;
        double snap (double beat) const { return std::max (0.0, std::round (beat / gridStep) * gridStep); }
        int noteAt (juce::Point<float> p) const;

        GuitarLabProcessor& proc;
        bool boxSelecting = false, loopDragging = false, dragged = false;
        juce::Point<float> boxStart, boxEnd;
        double loopAnchor = 0.0;
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
        void restoreUi();          // after a project was loaded: the controls come back
        std::function<void (const juce::String&)> onSendChords; // "To Reharm Lab": the chords as a progression
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
        void showExportMenu();
        void exportAs (int kind);  // 1 MIDI, 2 text tab, 3 PDF, 4 MusicXML
        TabExportInfo exportInfo (const juce::String& title) const;

        // Working with selected notes
        bool handleKey (const juce::KeyPress& k);
        void copySelection();
        void deleteSelection();
        void pasteClipboard();
        void moveSelection (double beats);
        void transposeSelection (int semitones);
        void moveSelectionString (int direction); // -1 = to the next higher string, +1 = lower; same pitch
        void selectAll();
        void selectMatching (const std::vector<NoteEvent>& notes);
        std::vector<int> selectedIndices() const;

        void setZoom (float factor);    // 0 = fit the whole phrase
        void updateLoopButton();
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
                         exportButton { "Export..." };

        // Practice / editing tools
        juce::ToggleButton loopToggle { "Loop section" }, metronomeToggle { "Metronome" }, countInToggle { "Count-in" };
        juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, zoomFitButton { "Fit" },
                         copyButton { "Copy" }, cutButton { "Cut" }, pasteButton { "Paste" },
                         earlierButton { "<" }, laterButton { ">" }, pitchUpButton { "+1" }, pitchDownButton { "-1" };
        juce::Label zoomLabel, editLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> metronomeAtt;
        std::vector<NoteEvent>& clipboard = proc.work.clipboard; // starts relative to the first copied note (kept in the processor)
        float zoom = 1.0f;
        bool  autoFit = true;               // short phrases fill the width
        bool  playbackForRecording = false; // Play was started by the count-in: stop it with the recording
        DragMidiArea dragArea;
        juce::TextButton toReharmButton { "To Reharm Lab" };

        // Recording the real guitar (tracked notes) into the phrase
        juce::TextButton recordButton { "Record guitar" };
        juce::Label snapLabel;
        juce::ComboBox snapBox;
        PhraseRecorder recorder;
        bool   recordStarted = false;    // first tracked event arrived
        double lastRecordBeat = 0.0;
        std::vector<GuitarLabProcessor::RecordEvent> recordBuffer = std::vector<GuitarLabProcessor::RecordEvent> (4096);

        std::vector<FretPos> chordBuilder;
        std::vector<std::string>& undoStack = proc.work.phraseUndo; // kept in the processor: survives closing the window
        int restoreViewX = -1;                 // scroll position to put back after the first layout
        bool loadingControls = false;
        int  liveString = -1;

        std::unique_ptr<juce::FileChooser> chooser;
        UiMemory memory { proc.uiState, "Phrase" }; // last member: see UiMemory
    };
}
