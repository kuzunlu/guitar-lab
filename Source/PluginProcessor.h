#pragma once

#include "Audio/PluckSynth.h"
#include "Midi/MidiRenderer.h"
#include "Midi/Phrase.h"
#include "Theory/Instrument.h"
#include "Theory/Reharm.h"
#include "Theory/Trainer.h"
#include "Theory/Voicing.h"
#include "Tracking/GuitarTracker.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <memory>
#include <optional>

namespace ParamIDs
{
    inline constexpr auto bendRange      = "bendRange";
    inline constexpr auto firstChannel   = "firstChannel";
    inline constexpr auto sendMpe        = "sendMpe";
    inline constexpr auto syncToHost     = "syncToHost";
    inline constexpr auto internalBpm    = "internalBpm";
    inline constexpr auto synthOn        = "synthOn";
    inline constexpr auto synthVolume    = "synthVolume";
    inline constexpr auto synthTone      = "synthTone";
    inline constexpr auto legatoOverlap  = "legatoOverlap";
    inline constexpr auto legatoVelocity = "legatoVelocity";
    inline constexpr auto passThrough    = "passThrough";
    inline constexpr auto liveVelocity   = "liveVelocity";

    inline constexpr auto trackMode        = "trackMode";
    inline constexpr auto trackInput       = "trackInput";
    inline constexpr auto trackStability   = "trackStability";
    inline constexpr auto trackSensitivity = "trackSensitivity";
    inline constexpr auto trackBends       = "trackBends";
    inline constexpr auto trackMonitor     = "trackMonitor";
    inline constexpr auto trackHysteresis  = "trackHysteresis";
    inline constexpr auto metronome        = "metronome";
}

class GuitarLabProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    GuitarLabProcessor();
    ~GuitarLabProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar Lab"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Shared state (message thread) ---
    juce::AudioProcessorValueTreeState params;

    gl::Instrument instrument;      // edit on the message thread, then call phraseChanged()
    gl::Phrase     phrase;          // edit on the message thread, then call phraseChanged()
    gl::Trainer    trainer;
    gl::MidiSettings midiSettings;  // keyswitches etc; numbers from params are copied in by syncSettings()

    // Text fields that are saved with the project.
    juce::String reharmInput { "I V vi IV" };

    // Everything the window shows that is not a parameter (open tab, window size, the choices in every panel).
    // The window is deleted when a DAW hides it; the panels keep their state here (UI/UiMemory.h). Message thread only.
    juce::ValueTree uiState { "UiState" };

    // Work in progress in the panels (reharm result, trainer question, undo history...). Kept here so it is still
    // there when the window opens again; not saved in the project. The panels use these through references.
    struct PanelWork
    {
        // Reharm Lab
        juce::String reharmFor;                  // input, key and tuning the result below belongs to
        std::vector<gl::TimedChord> reharmResult;
        std::vector<gl::Voicing> reharmVoicings;
        std::vector<int> reharmVoicingChoice;
        gl::Voicing reharmShown;
        bool reharmSelectedInResult = true;
        int  reharmSelected = -1;
        unsigned reharmSeed = 1;

        // Trainer
        std::optional<gl::Question> question;
        std::vector<gl::FretPos> clicked, missed, wrong;
        bool answered = false;
        juce::String trainerFeedback;
        bool trainerFeedbackGood = true;

        // Calibration wizard (paused while the window is closed: it waits for Listen again)
        gl::CalibrationSession calSession;
        int  calPage = 0;                        // CalibrationWizard::Page
        bool calOpen = false;
        std::optional<gl::Calibration> calTryBackup; // "Try it": the calibration in use before the new one was tried

        // Play & Write
        std::vector<std::string> phraseUndo;
        std::vector<gl::NoteEvent> clipboard;
        double phraseCursor = 0.0;
        int phraseSelected = -1;
        std::vector<int> phraseSelection;
        float phraseZoom = 1.0f;
        bool phraseAutoFit = true;
        int phraseViewX = 0;
    } work;
    int  reharmKeyTonic = 0;
    bool reharmKeyMinor = false;

    // Re-renders the phrase into MIDI events (call after any change to phrase, instrument or settings).
    void phraseChanged();
    void syncSettings();

    // Live notes from the fretboard / piano. Safe to call from the message thread.
    void liveNoteOn  (int string, int fret);
    void liveNoteOff (int string);
    void allLiveNotesOff();              // stops every string (before a new chord is played)
    void liveBend    (int string, double semitones);
    void liveRaw     (const juce::MidiMessage& m);

    // Internal transport (used when "sync to host" is off or the host is stopped).
    void setInternalPlaying (bool shouldPlay);
    bool isInternalPlaying() const { return internalPlaying.load(); }

    // Loop one section of the phrase while Guitar Lab's own Play runs (beats). loopEnd <= loopStart = whole phrase.
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };

    // One bar of metronome clicks, then Play starts at startBeat; with thenRecord, Record guitar starts with it.
    void startCountIn (double startBeat, bool thenRecord);
    void cancelCountIn() { countingIn = false; }
    bool isCountingIn() const { return countingIn.load(); }
    std::atomic<int> countInBeat { 0 };  // 1..4 while counting in (for the display)

    // Short sounds for the calibration wizard, mixed into the audio output only (never MIDI). Message thread.
    enum class Cue { None, Start, Ok, Problem };
    void playCue (Cue c) { pendingCue = (int) c; }

    // --- Read by the UI to draw what is sounding ---
    std::atomic<double> playheadBeat { -1.0 };
    std::array<std::atomic<int>,   gl::kMaxStrings> soundingFret {};  // -1 = silent
    std::array<std::atomic<float>, gl::kMaxStrings> soundingBend {};  // semitones
    std::array<std::atomic<bool>, 128> externalNotes {};              // notes coming in from the host

    std::function<void()> onStateLoaded; // the editor refreshes itself when a project is loaded

    // --- Guitar-to-MIDI tracking ---
    gl::Calibration calibration;         // message thread; copied into the tracker by rebuildTracker()
    std::atomic<bool> calibrating { false }; // true while the wizard runs: tracker sends no MIDI
    void rebuildTracker();               // call after changing calibration, instrument or tracking settings
    int  popFrames (gl::AnalysisFrame* dest, int maxFrames); // message thread: analysis frames for tuner + wizard
    int  numGuitarInputs() const { return getTotalNumInputChannels(); }

    // --- Recording tracked notes into the phrase ---
    struct RecordEvent
    {
        double  beat = 0.0;     // recording clock (DAW / Play position, or a free clock at the current tempo)
        bool    aligned = false;// true when the clock was the DAW / Play position (keep the place in the bar)
        uint8_t status = 0, data1 = 0, data2 = 0;
    };
    std::atomic<bool> recordingGuitar { false };
    std::atomic<bool> stepInputMono { false }; // Play & Write, Write mode (step input): Poly listens like Mono meanwhile

    // --- Every note the tracker sends (note log, "Try it", saved clips) ---
    struct TrackedEvent
    {
        juce::int64 sample = 0;   // audio samples since the plugin started
        uint8_t status = 0, data1 = 0, data2 = 0;
    };
    int popTrackedEvents (TrackedEvent* dest, int maxEvents); // message thread

    // --- Lighthouse tuner: the raw guitar input, copied only while the tuner is open ---
    std::atomic<bool> tunerOn { false };
    void readTunerInput (float* dest, int count) const; // the newest `count` samples (count <= 16384), message thread
    juce::int64 samplesSoFar() const { return samplesProcessed.load(); }
    int    popRecordEvents (RecordEvent* dest, int maxEvents); // message thread
    double currentBpm() const { return renderBpm(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;  // host tempo or a setting changed: re-render / rebuild the tracker
    void parameterChanged (const juce::String& id, float) override;
    std::atomic<bool> trackerDirty { false };
    static const std::vector<const char*>& listenedParams();
    std::vector<int> trackerTuning;     // tuning the tracker was built for
    double renderBpm() const;

    void emitPhrase (juce::MidiBuffer& out, double ppqStart, double ppqEnd, double samplesPerBeat, int numSamples);

    // Metronome clicks (audio thread): short beeps, higher on the first beat of a bar.
    struct ClickPlan { int count = 0; std::array<int, 16> at {}; std::array<bool, 16> accent {}; };
    void addClicks (ClickPlan& plan, double fromBeat, double toBeat, double samplesPerBeat, double beatOffset) const;
    void renderClicks (juce::AudioBuffer<float>& audio, const ClickPlan& plan);
    int    clickLeft = 0, clickTotal = 1;
    double clickPhase = 0.0, clickFreq = 1000.0;
    // Calibration cues (audio thread): up to two tones one after the other.
    void renderCue (juce::AudioBuffer<float>& audio);
    std::atomic<int> pendingCue { 0 };
    std::array<double, 2> cueFreq {};
    std::array<int, 2> cueLength {};
    int    cueTone = 2, cuePos = 0;
    double cuePhase = 0.0;
    std::atomic<bool> countingIn { false }, countInReset { false }, countInRecord { false };
    std::atomic<double> countInStart { 0.0 };
    double countInClock = 0.0;
    void allNotesOff (juce::MidiBuffer& out, int samplePos);
    void track (const juce::MidiMessage& m);

    gl::PluckSynth synth;
    juce::MidiMessageCollector liveCollector;

    juce::SpinLock eventsLock;
    std::shared_ptr<std::vector<gl::MidiEvent>> events = std::make_shared<std::vector<gl::MidiEvent>>();
    std::shared_ptr<std::vector<gl::MidiEvent>> setup  = std::make_shared<std::vector<gl::MidiEvent>>();
    double loopLength = 16.0;
    std::array<std::atomic<int>, gl::kMaxStrings> tuningCopy {};
    std::atomic<int> stringsCopy { 6 };
    std::atomic<int> firstChannelCopy { 2 };
    std::atomic<int> bendRangeCopy { 12 };
    std::atomic<double> hostBpm { 0.0 };   // 0 = host has not told us
    double renderedBpm = 120.0;

    std::array<int, gl::kMaxStrings> liveNotes { -1, -1, -1, -1, -1, -1, -1, -1 }; // message thread only

    std::atomic<bool> internalPlaying { false };
    std::atomic<bool> needSetup { true };
    std::atomic<bool> needAllOff { false };
    bool   wasPlaying = false;
    double internalPpq = 0.0;
    double lastPpqEnd = -1.0;
    double currentSampleRate = 44100.0;

    std::array<std::bitset<128>, 16> active; // notes we have sent note-on for (audio thread only)

    // Tracking (swapped under trackerLock; old trackers are freed on the message thread)
    juce::SpinLock trackerLock;
    std::unique_ptr<gl::GuitarTracker> tracker;
    juce::AudioBuffer<float> inputCopy;
    int maxBlock = 512;
    juce::AbstractFifo frameFifo { 8192 };
    std::vector<gl::AnalysisFrame> frameStore = std::vector<gl::AnalysisFrame> (8192);

    // Recording (audio thread)
    juce::AbstractFifo recordFifo { 16384 };
    std::vector<RecordEvent> recordStore = std::vector<RecordEvent> (16384);
    bool   wasRecording = false;
    juce::AbstractFifo trackedFifo { 4096 };
    std::vector<TrackedEvent> trackedStore = std::vector<TrackedEvent> (4096);
    std::atomic<juce::int64> samplesProcessed { 0 };
    static constexpr int kTunerRing = 1 << 15;
    std::vector<float> tunerRing = std::vector<float> ((size_t) kTunerRing);
    std::atomic<int> tunerWrite { 0 };
    bool   recordUsesTransport = false;
    double recordClock = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarLabProcessor)
};
