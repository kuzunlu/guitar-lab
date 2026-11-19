#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gl
{
    // Tooltip window that always waits until the mouse has rested on the same control (and the same tip text)
    // for a while. JUCE's own window shows the next tip at once when one was open a moment ago, and keeps it
    // open over places with a tip of their own. Here every new place starts with "no tip", so the old tip
    // closes as soon as the mouse moves on, and the new one appears only after the wait.
    class DelayedTooltipWindow : public juce::TooltipWindow
    {
    public:
        explicit DelayedTooltipWindow (juce::Component* parent, int waitMs = 1000)
            : juce::TooltipWindow (parent, 0), wait (waitMs) {}

        juce::String getTipFor (juce::Component& c) override
        {
            const auto tip = juce::TooltipWindow::getTipFor (c);
            const auto now = juce::Time::getMillisecondCounter();
            if (&c != restingOn.getComponent() || tip != restingTip)
            {
                restingOn = &c;
                restingTip = tip;
                restingSince = now;
            }
            return now - restingSince >= (juce::uint32) wait ? tip : juce::String();
        }

    private:
        int wait;
        juce::Component::SafePointer<juce::Component> restingOn;
        juce::String restingTip;
        juce::uint32 restingSince = 0;
    };
}
