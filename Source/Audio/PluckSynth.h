#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <random>
#include <vector>

namespace gl
{
    // A simple plucked-string sound so you can hear the MIDI without a DAW.
    // One voice per MIDI channel, so per-string pitch bend works like the real output.
    class PluckSynth
    {
    public:
        void prepare (double sampleRate);
        void reset();

        // Renders audio and reacts to the MIDI in the buffer (note on/off, pitch bend, RPN bend range).
        void render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi);

        void setGain (float g)         { gain = g; }
        void setBrightness (float b)   { brightness = juce::jlimit (0.0f, 1.0f, b); }

    private:
        struct Voice
        {
            std::vector<float> line;
            int    writePos = 0;
            double baseFreq = 110.0;
            double bendSemis = 0.0;
            int    note = -1;
            float  decay = 0.996f;
            float  last = 0.0f;
            float  level = 0.0f;
            int    bendRange = 2;
            int    rpnMsb = 127, rpnLsb = 127;
        };

        void handle (const juce::MidiMessage& m);
        void renderVoice (Voice& v, float* left, float* right, int start, int num, float pan);

        std::array<Voice, 16> voices; // one per MIDI channel
        double sr = 44100.0;
        float gain = 0.5f;
        float brightness = 0.6f;
        std::mt19937 rng { 1234 };
    };
}
