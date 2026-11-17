#include "Audio/PluckSynth.h"

namespace gl
{
    void PluckSynth::prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxLen = (int) (sr / 15.0) + 4; // lowest note about 15 Hz (8-string F#0 with low tunings)
        for (auto& v : voices)
            v.line.assign ((size_t) maxLen, 0.0f);
        reset();
    }

    void PluckSynth::reset()
    {
        for (auto& v : voices)
        {
            std::fill (v.line.begin(), v.line.end(), 0.0f);
            v.note = -1;
            v.level = 0.0f;
            v.bendSemis = 0.0;
        }
    }

    void PluckSynth::handle (const juce::MidiMessage& m)
    {
        const int ch = m.getChannel();
        if (ch < 1 || ch > 16)
            return;
        auto& v = voices[(size_t) (ch - 1)];

        if (m.isNoteOn())
        {
            const bool legato = v.note >= 0 && v.level > 0.01f; // a note still ringing: glide, no new pluck
            v.note = m.getNoteNumber();
            v.baseFreq = juce::MidiMessage::getMidiNoteInHertz (v.note);
            v.decay = 0.996f;

            if (! legato || m.getVelocity() > 90)
            {
                // Fill the delay line with a burst of noise. Softer = darker.
                std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
                const float vel = m.getFloatVelocity();
                const float smooth = 1.0f - (0.25f + 0.7f * brightness * vel);
                float prev = 0.0f;
                for (auto& s : v.line)
                {
                    const float x = noise (rng) * vel;
                    prev = prev * smooth + x * (1.0f - smooth);
                    s = prev;
                }
            }
            v.level = 1.0f;
        }
        else if (m.isNoteOff())
        {
            if (m.getNoteNumber() == v.note)
                v.decay = 0.90f; // fingers damp the string
        }
        else if (m.isPitchWheel())
        {
            v.bendSemis = (m.getPitchWheelValue() - 8192) / 8192.0 * v.bendRange;
        }
        else if (m.isController())
        {
            const int num = m.getControllerNumber(), val = m.getControllerValue();
            if (num == 101) v.rpnMsb = val;
            if (num == 100) v.rpnLsb = val;
            if (num == 6 && v.rpnMsb == 0 && v.rpnLsb == 0)
                v.bendRange = juce::jmax (1, val); // pitch bend sensitivity
            if (num == 123 || num == 120)
                for (auto& each : voices) each.decay = 0.90f;
        }
    }

    void PluckSynth::renderVoice (Voice& v, float* left, float* right, int start, int num, float pan)
    {
        if (v.level < 0.0001f || v.line.empty())
            return;

        const int len = (int) v.line.size();
        const double freq = v.baseFreq * std::pow (2.0, v.bendSemis / 12.0);
        const double delay = juce::jlimit (2.0, (double) len - 2.0, sr / freq - 0.5);

        const float l = gain * (1.0f - pan) * 0.6f;
        const float r = gain * pan * 0.6f;

        for (int i = start; i < start + num; ++i)
        {
            double readPos = v.writePos - delay;
            while (readPos < 0) readPos += len;

            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % len;
            const float frac = (float) (readPos - i0);
            const float out = v.line[(size_t) i0] + (v.line[(size_t) i1] - v.line[(size_t) i0]) * frac;

            // Low-pass + decay in the loop = the string loses energy and gets darker.
            const float next = v.decay * 0.5f * (out + v.last);
            v.last = out;
            v.line[(size_t) v.writePos] = next;
            v.writePos = (v.writePos + 1) % len;

            left[i]  += out * l;
            right[i] += out * r;
        }

        v.level *= std::pow (v.decay, (float) num * 0.05f);
    }

    void PluckSynth::render (juce::AudioBuffer<float>& audio, const juce::MidiBuffer& midi)
    {
        const int total = audio.getNumSamples();
        auto* left  = audio.getWritePointer (0);
        auto* right = audio.getNumChannels() > 1 ? audio.getWritePointer (1) : left;

        int pos = 0;
        auto renderTo = [&] (int end)
        {
            if (end <= pos) return;
            for (size_t i = 0; i < voices.size(); ++i)
                renderVoice (voices[i], left, right, pos, end - pos, 0.3f + 0.4f * (float) (i % 8) / 7.0f);
            pos = end;
        };

        for (const auto meta : midi)
        {
            renderTo (juce::jlimit (0, total, meta.samplePosition));
            handle (meta.getMessage());
        }
        renderTo (total);
    }
}
