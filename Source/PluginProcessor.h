#pragma once

#include "Audio/PluckSynth.h"
#include "Midi/MidiRenderer.h"
#include "Theory/Instrument.h"
#include "Theory/Trainer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <memory>

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
    int  reharmKeyTonic = 0;
    bool reharmKeyMinor = false;

    // Re-renders the phrase into MIDI events (call after any change to phrase, instrument or settings).
    void phraseChanged();
    void syncSettings();

    // Live notes from the fretboard / piano. Safe to call from the message thread.
    void liveNoteOn  (int string, int fret);
    void liveNoteOff (int string);
    void liveBend    (int string, double semitones);
    void liveRaw     (const juce::MidiMessage& m);

    // Internal transport (used when "sync to host" is off or the host is stopped).
    void setInternalPlaying (bool shouldPlay);
    bool isInternalPlaying() const { return internalPlaying.load(); }

    // --- Read by the UI to draw what is sounding ---
    std::atomic<double> playheadBeat { -1.0 };
    std::array<std::atomic<int>,   gl::kMaxStrings> soundingFret {};  // -1 = silent
    std::array<std::atomic<float>, gl::kMaxStrings> soundingBend {};  // semitones
    std::array<std::atomic<bool>, 128> externalNotes {};              // notes coming in from the host

    std::function<void()> onStateLoaded; // the editor refreshes itself when a project is loaded

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override { phraseChanged(); } // host tempo or a MIDI setting changed: re-render
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    double renderBpm() const;

    void emitPhrase (juce::MidiBuffer& out, double ppqStart, double ppqEnd, double samplesPerBeat, int numSamples);
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarLabProcessor)
};
