#pragma once

#include "PluginProcessor.h"

namespace gl
{
    // "Lighthouse": a full-panel strobe tuner. It only works while it is open (the processor copies the guitar input
    // only then). Every screen refresh it finds the pitch of the newest ~85 ms of input (YIN, fine interpolation),
    // smooths it, and moves the picture: the lighthouse beam and three strobe bands turn while the string is out of
    // tune (left = flat, right = sharp) and stand still when it is in tune.
    class LighthouseTuner : public juce::Component
    {
    public:
        explicit LighthouseTuner (GuitarLabProcessor& p);
        ~LighthouseTuner() override;

        void open();
        void close();
        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void frame (double now);               // every screen refresh
        double detectHz (const float* x, int n, double rate, float& clarity) const;

        GuitarLabProcessor& proc;
        juce::TextButton closeButton { "Close" };
        juce::VBlankAttachment vblank;
        std::vector<float> input, work;

        double lastTime = 0.0;
        bool   hasNote = false;                // a clear note was heard recently
        double lastHeard = 0.0;                // time of the last clear pitch
        int    note = -1;                      // nearest MIDI note
        double cents = 0.0;                    // smoothed, -50..50
        double beam = 0.0, strobe = 0.0;       // phases (turns)
        float  level = 0.0f;
        std::array<double, 5> recent {};       // last pitch estimates (cents from the note), for a median
        int    recentCount = 0;
    };
}
