#pragma once

#include "Midi/Phrase.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gl
{
    struct MidiSettings
    {
        // MIDI channel of the HIGHEST string (1..16). Lower strings use the next channels.
        // With MPE (lower zone) channel 1 is the "master" channel, so strings start on channel 2.
        int  firstChannel = 2;

        // Sends the MPE setup message on channel 1 when playback starts.
        bool sendMpeConfig = true;

        // Pitch bend range in semitones. MUST match the setting in your synth / sampler.
        int  bendRange = 12;

        // How often pitch bend is updated during bends, slides and vibrato (in beats). 1/64 = smooth.
        double bendStepBeats = 1.0 / 64.0;

        // Legato (hammer-on, pull-off, tap, slide target): new note starts a little before the old one ends,
        // so mono/legato patches glide instead of re-picking.
        bool   legatoOverlap = true;
        double legatoOverlapBeats = 0.03;

        double legatoVelocityScale = 0.65; // hammer-ons and pull-offs are softer than picked notes
        double palmMuteLengthScale = 0.5;
        double palmMuteVelocityScale = 0.85;
        double deadNoteLengthBeats = 1.0 / 16.0;
        double deadNoteVelocityScale = 0.45;

        // Optional keyswitch note per technique (-1 = off). Many guitar libraries change sound this way.
        int keyswitch[(int) Technique::NumTechniques] = { -1, -1, -1, -1, -1, -1, -1, -1 };

        // Channel for keyswitches: 0 = same channel as the note, 1..16 = fixed channel.
        int keyswitchChannel = 0;

        // Optional CC sent while a palm-muted note plays (127 on, 0 off). -1 = off.
        int palmMuteCC = -1;

        int channelForString (int string) const { return std::min (16, firstChannel + string); }
    };

    struct MidiEvent
    {
        double  beat = 0.0;
        int     order = 0;     // same beat: lower order goes first (note-off before bend before note-on)
        uint8_t data[3] = { 0, 0, 0 };
        int     size = 3;
    };

    // Messages to send once before playback (MPE zone setup + pitch bend range for every string channel).
    std::vector<MidiEvent> setupMessages (const MidiSettings& settings, int numStrings);

    // Renders a phrase into sorted MIDI events. bpm is needed for strums and vibrato speed.
    std::vector<MidiEvent> renderPhrase (const Phrase& phrase, const Instrument& inst,
                                         const MidiSettings& settings, double bpm);

    // Pitch bend value (0..16383) for a number of semitones.
    int pitchBendValue (double semitones, int bendRange);
}
