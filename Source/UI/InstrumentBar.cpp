#include "UI/InstrumentBar.h"

#include "Theory/Trainer.h"

namespace gl
{
    namespace
    {
        constexpr int kLowestTuningNote = 12;               // C0
        constexpr int kHighestTuningNote = 127 - kNumFrets; // so fret 24 is still a valid MIDI note

        void fillNoteBox (juce::ComboBox& box, bool flats)
        {
            box.clear (juce::dontSendNotification);
            for (int n = kLowestTuningNote; n <= kHighestTuningNote; ++n)
                box.addItem (juce::String (midiNoteName (n, flats)), n + 1); // id = note + 1 (ids must not be 0)
        }

        // ------------------------------------------------------------------
        // MIDI + sound settings, shown in a pop-up box.
        // ------------------------------------------------------------------
        class MidiSettingsComponent : public juce::Component
        {
        public:
            explicit MidiSettingsComponent (GuitarLabProcessor& p) : proc (p)
            {
                using APVTS = juce::AudioProcessorValueTreeState;

                auto addSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, const char* id, const juce::String& tip)
                {
                    l.setText (name, juce::dontSendNotification);
                    l.setTooltip (tip);
                    s.setSliderStyle (juce::Slider::LinearHorizontal);
                    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
                    s.setTooltip (tip);
                    s.onValueChange = [this] { proc.phraseChanged(); };
                    addAndMakeVisible (l);
                    addAndMakeVisible (s);
                    sliderAttachments.add (new APVTS::SliderAttachment (proc.params, id, s));
                    s.setNumDecimalPlacesToDisplay (2); // after the attachment, which sets the range
                };

                auto addToggle = [this] (juce::ToggleButton& b, const juce::String& name, const char* id, const juce::String& tip)
                {
                    b.setButtonText (name);
                    b.setTooltip (tip);
                    b.onClick = [this] { proc.phraseChanged(); };
                    addAndMakeVisible (b);
                    buttonAttachments.add (new APVTS::ButtonAttachment (proc.params, id, b));
                };

                addSlider (bendRange, bendRangeLabel, "Pitch bend range", ParamIDs::bendRange,
                    "Pitch bend range (semitones)\n"
                    "How far a full pitch-bend message moves the pitch, up or down.\n"
                    "This MUST be the same number as the pitch bend range in your synth or sampler, "
                    "or bends will be out of tune (a 'whole step' bend may only move a half step, or go too far).\n"
                    "Guitar Lab also sends this value to the synth (RPN 0) when playback starts, and most synths follow it.\n"
                    "12 is a good choice: big slides fit, and bends are still smooth. Slides longer than this range are cut short.");

                addSlider (firstChannel, firstChannelLabel, "Highest string channel", ParamIDs::firstChannel,
                    "MIDI channel of the highest string\n"
                    "Every string has its own MIDI channel, so a bend on one string does not bend the others.\n"
                    "The highest (thinnest) string uses this channel, the next string uses the next channel, and so on.\n"
                    "Example with 2 on a 6-string: high E = ch 2, B = 3, G = 4, D = 5, A = 6, low E = 7.\n"
                    "Use 2 for MPE synths (channel 1 is the MPE 'master' channel). Use 1 for older 'guitar mode' samplers "
                    "that expect string 1 on channel 1.");

                addToggle (sendMpe, "Send MPE setup", ParamIDs::sendMpe,
                    "Send MPE setup message\n"
                    "MPE (MIDI Polyphonic Expression) is a standard where each note gets its own channel.\n"
                    "When on, Guitar Lab tells the synth: 'use MPE, with one channel per string' (RPN 6 on channel 1).\n"
                    "Turn it on for MPE synths (Vital, Surge XT, Pigments, Equator, Kontakt MPE patches...).\n"
                    "Turn it off if your synth acts strangely or does not know MPE. Only works when the highest string is on channel 2.");

                addToggle (syncToHost, "Sync to host", ParamIDs::syncToHost,
                    "Sync to the DAW\n"
                    "On: the phrase plays when the DAW plays, locked to the DAW's tempo and bar position. "
                    "The phrase loops over its length.\n"
                    "Off: the phrase only plays with the Play button in Guitar Lab, at the internal tempo.");

                addSlider (bpm, bpmLabel, "Internal tempo", ParamIDs::internalBpm,
                    "Internal tempo (BPM)\n"
                    "Speed used when the DAW is not playing (or 'Sync to host' is off), and in the standalone app.\n"
                    "Also used to work out strum speed and vibrato speed when there is no DAW tempo.");

                addToggle (synthOn, "Built-in sound", ParamIDs::synthOn,
                    "Built-in plucked-string sound\n"
                    "A simple guitar-like sound so you can hear what you do without loading another plugin.\n"
                    "It follows bends and slides per string, just like the MIDI output.\n"
                    "Turn it off when you send the MIDI to a real guitar library in your DAW.");

                addSlider (volume, volumeLabel, "Sound volume", ParamIDs::synthVolume,
                    "Volume of the built-in sound\nDoes not change the MIDI output.");

                addSlider (tone, toneLabel, "Sound brightness", ParamIDs::synthTone,
                    "Brightness of the built-in sound\nLow = dark, warm (neck pickup). High = bright, sharp (bridge pickup).\n"
                    "Hard picking (high velocity) is always a little brighter.");

                addToggle (legato, "Legato overlap", ParamIDs::legatoOverlap,
                    "Legato overlap\n"
                    "For hammer-ons, pull-offs, taps and slide targets, the new note starts a tiny bit BEFORE the old one stops.\n"
                    "Most guitar libraries and mono synths read this overlap as 'legato' and play the new note "
                    "without a new pick attack.\nTurn off if your instrument plays a double note instead.");

                addSlider (legatoVel, legatoVelLabel, "Legato velocity", ParamIDs::legatoVelocity,
                    "Legato velocity\n"
                    "Hammer-ons and pull-offs are softer than picked notes. Their velocity is multiplied by this value.\n"
                    "0.65 = 65% of the written velocity. Taps are not changed.");

                addToggle (thru, "MIDI thru", ParamIDs::passThrough,
                    "MIDI thru\n"
                    "On: MIDI coming INTO Guitar Lab (from your keyboard or the DAW track) is passed on to the output too.\n"
                    "Off: only Guitar Lab's own notes go out.");

                addSlider (clickVel, clickVelLabel, "Click velocity", ParamIDs::liveVelocity,
                    "Click velocity\nHow hard notes are played when you click the fretboard or the piano (1 = softest, 127 = hardest).");

                // --- Keyswitches ---
                ksTitle.setText ("Keyswitches (for guitar sample libraries)", juce::dontSendNotification);
                ksTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                ksTitle.setTooltip ("Keyswitches\n"
                    "Many guitar libraries change their sound (palm mute, harmonics, hammer-on...) when you play a special "
                    "low note, called a keyswitch.\n"
                    "Pick the keyswitch note for each technique, as written in your library's manual. Guitar Lab plays "
                    "that note just before the real note.\nLeave on 'Off' to send nothing.");
                addAndMakeVisible (ksTitle);

                for (int t = 0; t < (int) Technique::NumTechniques; ++t)
                {
                    auto* label = ksLabels.add (new juce::Label ({}, juce::String (techniqueName ((Technique) t))));
                    auto* box = ksBoxes.add (new juce::ComboBox());
                    box->addItem ("Off", 1);
                    for (int n = 0; n < 128; ++n)
                        box->addItem (juce::String (midiNoteName (n)) + "  (" + juce::String (n) + ")", n + 2);
                    box->setSelectedId (proc.midiSettings.keyswitch[t] + 2, juce::dontSendNotification);

                    const auto tip = "Keyswitch for '" + juce::String (techniqueName ((Technique) t)) + "'\n"
                        "This note is sent right before every note that uses this technique.\n"
                        "Check your guitar library's manual for the right note. 'Off' = no keyswitch.";
                    label->setTooltip (tip);
                    box->setTooltip (tip);
                    box->onChange = [this, t, box]
                    {
                        proc.midiSettings.keyswitch[t] = box->getSelectedId() - 2;
                        proc.phraseChanged();
                    };
                    addAndMakeVisible (label);
                    addAndMakeVisible (box);
                }

                ksChannelLabel.setText ("Keyswitch channel", juce::dontSendNotification);
                ksChannel.addItem ("Same as note", 1);
                for (int c = 1; c <= 16; ++c) ksChannel.addItem ("Channel " + juce::String (c), c + 1);
                ksChannel.setSelectedId (proc.midiSettings.keyswitchChannel + 1, juce::dontSendNotification);
                const juce::String ksChTip = "Keyswitch channel\n"
                    "'Same as note' sends each keyswitch on the channel of the string that plays.\n"
                    "Pick a fixed channel if your library listens for keyswitches on one channel only.";
                ksChannelLabel.setTooltip (ksChTip);
                ksChannel.setTooltip (ksChTip);
                ksChannel.onChange = [this] { proc.midiSettings.keyswitchChannel = ksChannel.getSelectedId() - 1; proc.phraseChanged(); };
                addAndMakeVisible (ksChannelLabel);
                addAndMakeVisible (ksChannel);

                pmLabel.setText ("Palm mute CC", juce::dontSendNotification);
                pmCC.addItem ("Off", 1);
                for (int c = 0; c < 128; ++c) pmCC.addItem ("CC " + juce::String (c), c + 2);
                pmCC.setSelectedId (proc.midiSettings.palmMuteCC + 2, juce::dontSendNotification);
                const juce::String pmTip = "Palm mute controller (CC)\n"
                    "Some libraries switch palm mute on with a controller instead of a keyswitch.\n"
                    "Guitar Lab sends value 127 when a palm-muted note starts and 0 when it ends.\n'Off' = not sent.";
                pmLabel.setTooltip (pmTip);
                pmCC.setTooltip (pmTip);
                pmCC.onChange = [this] { proc.midiSettings.palmMuteCC = pmCC.getSelectedId() - 2; proc.phraseChanged(); };
                addAndMakeVisible (pmLabel);
                addAndMakeVisible (pmCC);

                setSize (520, 640);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (10);
                auto row = [&r] (int h = 24) { auto x = r.removeFromTop (h); r.removeFromTop (4); return x; };

                auto sliderRow = [&] (juce::Label& l, juce::Slider& s) { auto x = row(); l.setBounds (x.removeFromLeft (170)); s.setBounds (x); };
                auto toggleRow = [&] (juce::ToggleButton& a, juce::ToggleButton& b) { auto x = row(); a.setBounds (x.removeFromLeft (x.getWidth() / 2)); b.setBounds (x); };

                sliderRow (bendRangeLabel, bendRange);
                sliderRow (firstChannelLabel, firstChannel);
                toggleRow (sendMpe, syncToHost);
                sliderRow (bpmLabel, bpm);
                toggleRow (legato, thru);
                sliderRow (legatoVelLabel, legatoVel);
                sliderRow (clickVelLabel, clickVel);
                { auto x = row(); synthOn.setBounds (x); }
                sliderRow (volumeLabel, volume);
                sliderRow (toneLabel, tone);

                r.removeFromTop (6);
                ksTitle.setBounds (row());
                for (int i = 0; i < ksBoxes.size(); ++i)
                {
                    auto x = row (22);
                    ksLabels[i]->setBounds (x.removeFromLeft (170));
                    ksBoxes[i]->setBounds (x.removeFromLeft (180));
                }
                { auto x = row (22); ksChannelLabel.setBounds (x.removeFromLeft (170)); ksChannel.setBounds (x.removeFromLeft (180)); }
                { auto x = row (22); pmLabel.setBounds (x.removeFromLeft (170)); pmCC.setBounds (x.removeFromLeft (180)); }
            }

        private:
            GuitarLabProcessor& proc;

            juce::Slider bendRange, firstChannel, bpm, volume, tone, legatoVel, clickVel;
            juce::Label  bendRangeLabel, firstChannelLabel, bpmLabel, volumeLabel, toneLabel, legatoVelLabel, clickVelLabel;
            juce::ToggleButton sendMpe, syncToHost, synthOn, legato, thru;

            juce::Label ksTitle;
            juce::OwnedArray<juce::Label> ksLabels;
            juce::OwnedArray<juce::ComboBox> ksBoxes;
            juce::Label ksChannelLabel, pmLabel;
            juce::ComboBox ksChannel, pmCC;

            juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
            juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
        };
    }

    // -----------------------------------------------------------------------
    InstrumentBar::InstrumentBar (GuitarLabProcessor& p) : proc (p)
    {
        addAndMakeVisible (stringsLabel);
        stringsBox.addItem ("6 strings", 6);
        stringsBox.addItem ("7 strings", 7);
        stringsBox.addItem ("8 strings", 8);
        stringsBox.onChange = [this] { stringsChanged(); };
        const juce::String stringsTip = "Number of strings\n"
            "6 = normal guitar. 7 = adds a low B string. 8 = adds a low F# and a low B.\n"
            "Changing this loads the standard tuning for that guitar. Your phrase notes on strings that no longer exist are removed.";
        stringsBox.setTooltip (stringsTip);
        stringsLabel.setTooltip (stringsTip);
        addAndMakeVisible (stringsBox);

        addAndMakeVisible (tuningLabel);
        presetBox.onChange = [this] { presetChanged(); };
        const juce::String presetTip = "Tuning preset\n"
            "Pick a common tuning for this number of strings (Standard, Drop D, Open G, DADGAD...).\n"
            "You can then change any single string with the note boxes on the right. The preset name shows "
            "'Custom' when your tuning does not match a preset.";
        presetBox.setTooltip (presetTip);
        tuningLabel.setTooltip (presetTip);
        addAndMakeVisible (presetBox);

        downButton.setTooltip ("Tune everything down a half step\nAll strings go down by one semitone (E standard -> Eb standard).");
        upButton.setTooltip ("Tune everything up a half step\nAll strings go up by one semitone.");
        downButton.onClick = [this] { transpose (-1); };
        upButton.onClick   = [this] { transpose (+1); };
        addAndMakeVisible (downButton);
        addAndMakeVisible (upButton);

        for (int i = 0; i < kMaxStrings; ++i)
        {
            auto* box = stringTuning.add (new juce::ComboBox());
            box->onChange = [this, i] { tuningComboChanged (i); };
            addChildComponent (box);
        }

        addAndMakeVisible (scaleLabel);
        scaleRootBox.addItem ("C", 1);
        for (int pc = 1; pc < 12; ++pc)
        {
            const juce::String sharp (pitchClassName (pc, false)), flat (pitchClassName (pc, true));
            scaleRootBox.addItem (sharp == flat ? sharp : sharp + " / " + flat, pc + 1); // "D", but "C# / Db"
        }
        scaleRootBox.setSelectedId (1, juce::dontSendNotification);
        scaleRootBox.onChange = [this] { if (onChange) onChange(); };
        const juce::String rootTip = "Scale root (key)\nThe note the scale starts on. Root notes are shown in orange on the fretboard and piano.";
        scaleRootBox.setTooltip (rootTip);
        addAndMakeVisible (scaleRootBox);

        scaleTypeBox.addItem ("Off", 1);
        int id = 2;
        for (const auto& s : scaleTypes())
            scaleTypeBox.addItem (juce::String (s.name), id++);
        scaleTypeBox.setSelectedId (1, juce::dontSendNotification);
        scaleTypeBox.onChange = [this] { if (onChange) onChange(); };
        const juce::String scaleTip = "Scale overlay\n"
            "Shows every note of a scale across the whole neck (all 24 frets) and on the piano.\n"
            "Orange = root note, grey = other scale notes. Pick 'Off' to hide it.\n"
            "Tip: use it with the Play tab to find shapes, or to see where a key 'lives' on a 7- or 8-string.";
        scaleTypeBox.setTooltip (scaleTip);
        scaleLabel.setTooltip (scaleTip);
        addAndMakeVisible (scaleTypeBox);

        namesToggle.setTooltip ("Show all note names\nWrites the name of every note on every fret, faintly. Good for learning the neck. "
                                "Turn it off when you want to test yourself.");
        namesToggle.onClick = [this] { if (onChange) onChange(); };
        addAndMakeVisible (namesToggle);

        flatsToggle.setTooltip ("Use flat names\nOn: Bb, Eb, Ab... Off: A#, D#, G#...\nThe notes are the same, only the names change.");
        flatsToggle.onClick = [this] { refresh(); if (onChange) onChange(); };
        addAndMakeVisible (flatsToggle);

        midiButton.setTooltip ("MIDI and sound settings\n"
            "Pitch bend range, MIDI channels per string, MPE, DAW sync, tempo, legato, keyswitches for guitar libraries, "
            "and the built-in sound.\nOpen this first when you connect Guitar Lab to a guitar library in your DAW.");
        midiButton.onClick = [this] { showMidiSettings(); };
        addAndMakeVisible (midiButton);

       #ifndef GUITAR_LAB_VERSION
        #define GUITAR_LAB_VERSION "dev"
       #endif
        versionLabel.setText (GUITAR_LAB_VERSION, juce::dontSendNotification);
        versionLabel.setJustificationType (juce::Justification::centredRight);
        versionLabel.setColour (juce::Label::textColourId, theme::textDim);
        versionLabel.setTooltip ("Guitar Lab version\nThe version of this plugin, from the release it was built from. "
                                 "A longer text like v0.1.0-3-gabc1234 means a test build, 3 changes after v0.1.0.");
        addAndMakeVisible (versionLabel);

        refresh();
    }

    InstrumentBar::~InstrumentBar() = default;

    void InstrumentBar::fillPresetBox()
    {
        presetBox.clear (juce::dontSendNotification);
        const auto presets = tuningPresets (proc.instrument.numStrings());
        int match = 0;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            presetBox.addItem (juce::String (presets[(size_t) i].name), i + 1);
            if (presets[(size_t) i].openNotes == proc.instrument.tuning())
                match = i + 1;
        }
        presetBox.addItem ("Custom", 1000);
        presetBox.setSelectedId (match > 0 ? match : 1000, juce::dontSendNotification);
    }

    void InstrumentBar::refresh()
    {
        updating = true;
        const int count = proc.instrument.numStrings();
        stringsBox.setSelectedId (count, juce::dontSendNotification);
        fillPresetBox();

        for (int i = 0; i < kMaxStrings; ++i)
        {
            auto* box = stringTuning[i];
            box->setVisible (i < count);
            if (i >= count) continue;

            // Box i shows string (count - 1 - i): low string on the left.
            const int string = count - 1 - i;
            fillNoteBox (*box, flatsToggle.getToggleState());
            box->setSelectedId (proc.instrument.tuning()[(size_t) string] + 1, juce::dontSendNotification);
            box->setTooltip ("String " + juce::String (string + 1) + " open note\n"
                             "The note this string plays with no finger on it. Box order is low string (left) to high string (right).\n"
                             "Change it to make any custom tuning. String 1 is the highest string.");
        }
        updating = false;
        resized();
    }

    void InstrumentBar::instrumentEdited()
    {
        // Remove phrase notes on strings that no longer exist.
        auto& notes = proc.phrase.notes;
        notes.erase (std::remove_if (notes.begin(), notes.end(),
                                     [this] (const NoteEvent& n) { return n.string >= proc.instrument.numStrings(); }), notes.end());

        proc.trainer.setInstrument (proc.instrument);
        proc.phraseChanged();
        refresh();
        if (onChange) onChange();
    }

    void InstrumentBar::applyTuning (const std::vector<MidiNote>& openNotes)
    {
        if (openNotes == proc.instrument.tuning() || ! proc.instrument.setTuning (openNotes))
            return;
        instrumentEdited();
    }

    void InstrumentBar::stringsChanged()
    {
        if (updating) return;
        proc.instrument.setNumStrings (stringsBox.getSelectedId());
        instrumentEdited();
    }

    void InstrumentBar::presetChanged()
    {
        if (updating) return;
        const int id = presetBox.getSelectedId();
        const auto presets = tuningPresets (proc.instrument.numStrings());
        if (id >= 1 && id <= (int) presets.size())
        {
            proc.instrument.setTuning (presets[(size_t) (id - 1)].openNotes);
            instrumentEdited();
        }
    }

    void InstrumentBar::tuningComboChanged (int boxIndex)
    {
        if (updating) return;
        const int string = proc.instrument.numStrings() - 1 - boxIndex;
        proc.instrument.setStringOpenNote (string, stringTuning[boxIndex]->getSelectedId() - 1);
        instrumentEdited();
    }

    void InstrumentBar::transpose (int semitones)
    {
        proc.instrument.transposeAll (semitones);
        instrumentEdited();
    }

    void InstrumentBar::showMidiSettings()
    {
        // Keep the pop-up inside the plugin window (floating desktop windows behave badly in some DAWs).
        auto* top = getTopLevelComponent();
        juce::CallOutBox::launchAsynchronously (std::make_unique<MidiSettingsComponent> (proc),
                                                top->getLocalArea (this, midiButton.getBounds()), top);
    }

    std::map<PitchClass, juce::Colour> InstrumentBar::scaleLayer() const
    {
        std::map<PitchClass, juce::Colour> layer;
        const int typeIndex = scaleTypeBox.getSelectedId() - 2;
        if (typeIndex < 0 || typeIndex >= (int) scaleTypes().size())
            return layer;

        const int root = scaleRootBox.getSelectedId() - 1;
        for (int interval : scaleTypes()[(size_t) typeIndex].intervals)
            layer[wrap12 (root + interval)] = interval == 0 ? theme::root : theme::scaleTone;
        return layer;
    }

    void InstrumentBar::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
    }

    void InstrumentBar::resized()
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto top = r.removeFromTop (26);
        r.removeFromTop (4);
        auto bottom = r.removeFromTop (26);

        stringsLabel.setBounds (top.removeFromLeft (52));
        stringsBox.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (10);
        tuningLabel.setBounds (top.removeFromLeft (50));
        presetBox.setBounds (top.removeFromLeft (250));
        top.removeFromLeft (4);
        downButton.setBounds (top.removeFromLeft (32));
        upButton.setBounds (top.removeFromLeft (32));
        top.removeFromLeft (10);
        for (auto* box : stringTuning)
            if (box->isVisible())
            {
                box->setBounds (top.removeFromLeft (62));
                top.removeFromLeft (2);
            }

        midiButton.setBounds (bottom.removeFromRight (130));
        versionLabel.setBounds (bottom.removeFromRight (150));
        scaleLabel.setBounds (bottom.removeFromLeft (76));
        scaleRootBox.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (4);
        scaleTypeBox.setBounds (bottom.removeFromLeft (180));
        bottom.removeFromLeft (10);
        namesToggle.setBounds (bottom.removeFromLeft (130));
        flatsToggle.setBounds (bottom.removeFromLeft (70));
    }
}
