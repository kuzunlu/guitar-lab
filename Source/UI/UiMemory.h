#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace gl
{
    // Remembers controls in a ValueTree that lives in the processor (GuitarLabProcessor::uiState).
    // A DAW deletes the plugin window (and every panel in it) when it closes or hides it. With this, every choice
    // comes back when the window opens again, and it is saved with the project.
    //
    //   remember (control, "key"): call once, after the control is set up and BEFORE the panel uses its value.
    //   It puts back the remembered value (without calling the control's onChange) and then writes every change.
    //
    // Declare it as the LAST member of a panel: it is then destroyed first, while the controls still exist,
    // and stores their final values (also ones changed with dontSendNotification).
    class UiMemory : private juce::Slider::Listener,
                     private juce::ComboBox::Listener,
                     private juce::Button::Listener,
                     private juce::TextEditor::Listener
    {
    public:
        UiMemory (juce::ValueTree parentTree, const juce::Identifier& childName)
            : parent (parentTree), name (childName), tree (parent.getOrCreateChildWithName (name, nullptr)) {}

        ~UiMemory() override
        {
            captureAll();
            for (auto& [c, key] : bound)
            {
                if (auto* s = dynamic_cast<juce::Slider*> (c.getComponent()))           s->removeListener (this);
                else if (auto* b = dynamic_cast<juce::ComboBox*> (c.getComponent()))   b->removeListener (this);
                else if (auto* t = dynamic_cast<juce::TextEditor*> (c.getComponent())) t->removeListener (this);
                else if (auto* x = dynamic_cast<juce::Button*> (c.getComponent()))     x->removeListener (this);
            }
        }

        // The remembered values of this panel. Panels may also keep their own data here.
        juce::ValueTree& data() { return tree; }

        // After a project was loaded: the processor's tree was replaced, so find our part again and put every
        // remembered value back into the controls.
        void restoreAll()
        {
            tree = parent.getOrCreateChildWithName (name, nullptr);
            for (auto& [c, key] : bound)
                if (c != nullptr) restore (*c.getComponent(), key);
        }

        void remember (juce::Slider& s, const juce::Identifier& key)     { bind (s, key); s.addListener (this); }
        void remember (juce::ComboBox& b, const juce::Identifier& key)   { bind (b, key); b.addListener (this); }
        void remember (juce::Button& b, const juce::Identifier& key)     { bind (b, key); b.addListener (this); }
        void remember (juce::TextEditor& t, const juce::Identifier& key) { bind (t, key); t.addListener (this); }

        // Writes the current values of all controls (for changes made with dontSendNotification).
        void captureAll()
        {
            for (auto& [c, key] : bound)
                if (c != nullptr) capture (*c.getComponent(), key);
        }

    private:
        void bind (juce::Component& c, const juce::Identifier& key)
        {
            bound.push_back ({ &c, key });
            if (tree.hasProperty (key)) restore (c, key);
            else capture (c, key);
        }

        void restore (juce::Component& c, const juce::Identifier& key)
        {
            if (! tree.hasProperty (key)) return;
            const auto v = tree[key];
            if (auto* s = dynamic_cast<juce::Slider*> (&c))           s->setValue ((double) v, juce::dontSendNotification);
            else if (auto* b = dynamic_cast<juce::ComboBox*> (&c))   { if (b->indexOfItemId ((int) v) >= 0) b->setSelectedId ((int) v, juce::dontSendNotification); }
            else if (auto* t = dynamic_cast<juce::TextEditor*> (&c)) t->setText (v.toString(), juce::dontSendNotification);
            else if (auto* x = dynamic_cast<juce::Button*> (&c))     x->setToggleState ((bool) v, juce::dontSendNotification);
        }

        void capture (juce::Component& c, const juce::Identifier& key)
        {
            if (auto* s = dynamic_cast<juce::Slider*> (&c))           tree.setProperty (key, s->getValue(), nullptr);
            else if (auto* b = dynamic_cast<juce::ComboBox*> (&c))   { if (b->getSelectedId() != 0) tree.setProperty (key, b->getSelectedId(), nullptr); }
            else if (auto* t = dynamic_cast<juce::TextEditor*> (&c)) tree.setProperty (key, t->getText(), nullptr);
            else if (auto* x = dynamic_cast<juce::Button*> (&c))     tree.setProperty (key, x->getToggleState(), nullptr);
        }

        void captureFrom (juce::Component* c)
        {
            for (auto& [comp, key] : bound)
                if (comp.getComponent() == c) capture (*c, key);
        }

        void sliderValueChanged (juce::Slider* s) override       { captureFrom (s); }
        void comboBoxChanged (juce::ComboBox* b) override        { captureFrom (b); }
        void buttonClicked (juce::Button* b) override            { captureFrom (b); }
        void buttonStateChanged (juce::Button* b) override       { captureFrom (b); }
        void textEditorTextChanged (juce::TextEditor& t) override { captureFrom (&t); }

        juce::ValueTree parent;
        juce::Identifier name;
        juce::ValueTree tree;
        std::vector<std::pair<juce::Component::SafePointer<juce::Component>, juce::Identifier>> bound;
    };
}
