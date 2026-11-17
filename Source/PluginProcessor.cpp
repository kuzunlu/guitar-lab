#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

GuitarLabProcessor::GuitarLabProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Params", createLayout())
{
    for (auto& f : soundingFret) f = -1;
    for (auto& b : soundingBend) b = 0.0f;
    for (auto& n : externalNotes) n = false;

    for (auto* id : { ParamIDs::bendRange, ParamIDs::firstChannel, ParamIDs::sendMpe, ParamIDs::syncToHost,
                      ParamIDs::internalBpm, ParamIDs::legatoOverlap, ParamIDs::legatoVelocity })
        params.addParameterListener (id, this);

    phraseChanged();
}

GuitarLabProcessor::~GuitarLabProcessor()
{
    for (auto* id : { ParamIDs::bendRange, ParamIDs::firstChannel, ParamIDs::sendMpe, ParamIDs::syncToHost,
                      ParamIDs::internalBpm, ParamIDs::legatoOverlap, ParamIDs::legatoVelocity })
        params.removeParameterListener (id, this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarLabProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { ParamIDs::bendRange, 1 },      "Pitch bend range", 1, 48, 12,
                    AudioParameterIntAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { ParamIDs::firstChannel, 1 },   "First string channel", 1, 9, 2));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::sendMpe, 1 },        "Send MPE setup", true));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::syncToHost, 1 },     "Sync to host", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::internalBpm, 1 },    "Internal tempo",
                    NormalisableRange<float> (40.0f, 300.0f, 1.0f), 120.0f, AudioParameterFloatAttributes().withLabel ("bpm")));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::synthOn, 1 },        "Built-in sound", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::synthVolume, 1 },    "Sound volume",
                    NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::synthTone, 1 },      "Sound brightness",
                    NormalisableRange<float> (0.0f, 1.0f), 0.6f));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::legatoOverlap, 1 },  "Legato overlap", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::legatoVelocity, 1 }, "Legato velocity",
                    NormalisableRange<float> (0.2f, 1.0f), 0.65f));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::passThrough, 1 },    "MIDI thru", true));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { ParamIDs::liveVelocity, 1 },   "Click velocity", 1, 127, 100));
    return layout;
}

bool GuitarLabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GuitarLabProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    synth.prepare (sampleRate);
    liveCollector.reset (sampleRate);
    needSetup = true;
    wasPlaying = false;
    for (auto& a : active) a.reset();
}

// ---------------------------------------------------------------------------
double GuitarLabProcessor::renderBpm() const
{
    const double host = hostBpm.load();
    const bool sync = params.getRawParameterValue (ParamIDs::syncToHost)->load() > 0.5f;
    if (sync && host > 0.0)
        return host;
    return params.getRawParameterValue (ParamIDs::internalBpm)->load();
}

void GuitarLabProcessor::syncSettings()
{
    midiSettings.bendRange     = (int) params.getRawParameterValue (ParamIDs::bendRange)->load();
    midiSettings.firstChannel  = (int) params.getRawParameterValue (ParamIDs::firstChannel)->load();
    midiSettings.sendMpeConfig = params.getRawParameterValue (ParamIDs::sendMpe)->load() > 0.5f;
    midiSettings.legatoOverlap = params.getRawParameterValue (ParamIDs::legatoOverlap)->load() > 0.5f;
    midiSettings.legatoVelocityScale = params.getRawParameterValue (ParamIDs::legatoVelocity)->load();

    // MPE only makes sense when strings start on channel 2 (channel 1 = master).
    if (midiSettings.firstChannel != 2)
        midiSettings.sendMpeConfig = false;
}

void GuitarLabProcessor::phraseChanged()
{
    syncSettings();

    renderedBpm = renderBpm();
    auto newEvents = std::make_shared<std::vector<gl::MidiEvent>> (gl::renderPhrase (phrase, instrument, midiSettings, renderedBpm));
    auto newSetup  = std::make_shared<std::vector<gl::MidiEvent>> (gl::setupMessages (midiSettings, instrument.numStrings()));

    for (int s = 0; s < gl::kMaxStrings; ++s)
        tuningCopy[(size_t) s] = s < instrument.numStrings() ? instrument.tuning()[(size_t) s] : 0;
    stringsCopy      = instrument.numStrings();
    firstChannelCopy = midiSettings.firstChannel;
    bendRangeCopy    = midiSettings.bendRange;

    {
        const juce::SpinLock::ScopedLockType lock (eventsLock);
        std::swap (events, newEvents);
        std::swap (setup, newSetup);
        loopLength = std::max (1.0, phrase.lengthBeats);
    }
    // Old vectors are freed here, on the message thread, not on the audio thread.

    needSetup = true;
}

// ---------------------------------------------------------------------------
// Live input from the UI
// ---------------------------------------------------------------------------
void GuitarLabProcessor::liveRaw (const juce::MidiMessage& m)
{
    auto copy = m;
    copy.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    liveCollector.addMessageToQueue (copy);
}

void GuitarLabProcessor::liveNoteOn (int string, int fret)
{
    if (! instrument.isValid ({ string, fret }))
        return;

    liveNoteOff (string); // one note per string, like a real guitar

    const int ch = midiSettings.channelForString (string);
    const int note = juce::jlimit (0, 127, instrument.noteAt ({ string, fret }));
    const int vel = (int) params.getRawParameterValue (ParamIDs::liveVelocity)->load();

    liveRaw (juce::MidiMessage::pitchWheel (ch, 8192));
    liveRaw (juce::MidiMessage::noteOn (ch, note, (juce::uint8) vel));
    liveNotes[(size_t) string] = note;
}

void GuitarLabProcessor::liveNoteOff (int string)
{
    if (string < 0 || string >= gl::kMaxStrings || liveNotes[(size_t) string] < 0)
        return;

    const int ch = midiSettings.channelForString (string);
    liveRaw (juce::MidiMessage::noteOff (ch, liveNotes[(size_t) string]));
    liveNotes[(size_t) string] = -1;
}

void GuitarLabProcessor::liveBend (int string, double semitones)
{
    if (string < 0 || string >= gl::kMaxStrings)
        return;
    const int ch = midiSettings.channelForString (string);
    liveRaw (juce::MidiMessage::pitchWheel (ch, gl::pitchBendValue (semitones, midiSettings.bendRange)));
}

void GuitarLabProcessor::setInternalPlaying (bool shouldPlay)
{
    if (shouldPlay && ! internalPlaying.load())
        internalPpq = 0.0; // set before the flag, so the audio thread sees the reset
    internalPlaying = shouldPlay;
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------
void GuitarLabProcessor::track (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    if (ch < 1)
        return;

    if (m.isNoteOn())       active[(size_t) (ch - 1)].set ((size_t) m.getNoteNumber());
    else if (m.isNoteOff()) active[(size_t) (ch - 1)].reset ((size_t) m.getNoteNumber());

    const int s = ch - firstChannelCopy.load();
    if (s < 0 || s >= stringsCopy.load())
        return;

    if (m.isNoteOn())
    {
        const int fret = m.getNoteNumber() - tuningCopy[(size_t) s].load();
        soundingFret[(size_t) s] = (fret >= 0 && fret <= gl::kNumFrets) ? fret : -1;
    }
    else if (m.isNoteOff())
    {
        const int fret = m.getNoteNumber() - tuningCopy[(size_t) s].load();
        if (soundingFret[(size_t) s].load() == fret)
            soundingFret[(size_t) s] = -1;
    }
    else if (m.isPitchWheel())
    {
        soundingBend[(size_t) s] = (float) ((m.getPitchWheelValue() - 8192) / 8192.0 * bendRangeCopy.load());
    }
}

void GuitarLabProcessor::allNotesOff (juce::MidiBuffer& out, int samplePos)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        auto& bits = active[(size_t) (ch - 1)];
        for (int n = 0; n < 128; ++n)
            if (bits.test ((size_t) n))
                out.addEvent (juce::MidiMessage::noteOff (ch, n), samplePos);
        bits.reset();
    }

    for (int s = 0; s < stringsCopy.load(); ++s)
        out.addEvent (juce::MidiMessage::pitchWheel (firstChannelCopy.load() + s, 8192), samplePos);
}

void GuitarLabProcessor::emitPhrase (juce::MidiBuffer& out, double ppqStart, double ppqEnd, double samplesPerBeat, int numSamples)
{
    const auto& list = *events;
    if (list.empty())
        return;

    // The phrase loops forever: find every loop pass that touches this block.
    double base = std::floor (ppqStart / loopLength) * loopLength;

    for (; base < ppqEnd; base += loopLength)
    {
        const double from = ppqStart - base;
        const double to   = ppqEnd - base;

        auto it = std::lower_bound (list.begin(), list.end(), from,
                                    [] (const gl::MidiEvent& e, double b) { return e.beat < b; });

        for (; it != list.end() && it->beat < to; ++it)
        {
            const int pos = juce::jlimit (0, numSamples - 1, (int) ((base + it->beat - ppqStart) * samplesPerBeat));
            out.addEvent (juce::MidiMessage (it->data, it->size), pos);
        }
    }
}

void GuitarLabProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    const int numSamples = audio.getNumSamples();

    // --- Where are we in time? ---
    const bool sync = params.getRawParameterValue (ParamIDs::syncToHost)->load() > 0.5f;
    double bpm = params.getRawParameterValue (ParamIDs::internalBpm)->load();
    bool hostPlaying = false;
    double hostPpq = 0.0;

    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm())
            {
                if (sync && std::abs (*b - hostBpm.load()) > 0.5)
                {
                    hostBpm = *b;
                    triggerAsyncUpdate(); // re-render strums/vibrato at the new tempo
                }
                if (sync) bpm = *b;
            }
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
        }
    }

    bool playing = false;
    double ppqStart = 0.0;
    if (sync && hostPlaying)       { playing = true; ppqStart = hostPpq; }
    else if (internalPlaying.load()) { playing = true; ppqStart = internalPpq; }

    const double samplesPerBeat = currentSampleRate * 60.0 / std::max (1.0, bpm);
    const double ppqEnd = ppqStart + numSamples / samplesPerBeat;

    juce::MidiBuffer out;

    const juce::SpinLock::ScopedTryLockType lock (eventsLock);

    // --- Setup messages first (MPE zone + bend range) ---
    if (playing && ! wasPlaying)
        needSetup = true;

    if (lock.isLocked() && needSetup.exchange (false))
        for (const auto& e : *setup)
            out.addEvent (juce::MidiMessage (e.data, e.size), 0);

    // --- Stop / jump: never leave notes hanging ---
    const bool jumped = playing && wasPlaying && std::abs (ppqStart - lastPpqEnd) > 0.05;
    if ((! playing && wasPlaying) || jumped || needAllOff.exchange (false))
        allNotesOff (out, 0);

    // --- Host input ---
    const bool thru = params.getRawParameterValue (ParamIDs::passThrough)->load() > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       externalNotes[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff()) externalNotes[(size_t) m.getNoteNumber()] = false;
        if (thru)
            out.addEvent (m, meta.samplePosition);
    }

    // --- Live notes from the UI ---
    juce::MidiBuffer live;
    liveCollector.removeNextBlockOfMessages (live, numSamples);
    out.addEvents (live, 0, numSamples, 0);

    // --- The phrase ---
    if (playing && lock.isLocked())
    {
        emitPhrase (out, ppqStart, ppqEnd, samplesPerBeat, numSamples);
        playheadBeat = std::fmod (ppqStart, loopLength);
    }
    else if (! playing)
    {
        playheadBeat = -1.0;
    }

    if (internalPlaying.load() && ! (sync && hostPlaying))
        internalPpq = ppqEnd;

    wasPlaying = playing;
    lastPpqEnd = ppqEnd;

    for (const auto meta : out)
        track (meta.getMessage());

    // --- Built-in sound ---
    if (params.getRawParameterValue (ParamIDs::synthOn)->load() > 0.5f)
    {
        synth.setGain (params.getRawParameterValue (ParamIDs::synthVolume)->load());
        synth.setBrightness (params.getRawParameterValue (ParamIDs::synthTone)->load());
        synth.render (audio, out);
    }

    midi.swapWith (out);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void GuitarLabProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    juce::ValueTree extra ("GuitarLabData");

    juce::StringArray tuning;
    for (auto n : instrument.tuning()) tuning.add (juce::String (n));
    extra.setProperty ("tuning", tuning.joinIntoString (","), nullptr);

    extra.setProperty ("phrase",  juce::String (gl::phraseToText (phrase)), nullptr);
    extra.setProperty ("trainer", juce::String (trainer.serialise()), nullptr);
    extra.setProperty ("reharmInput", reharmInput, nullptr);
    extra.setProperty ("reharmTonic", reharmKeyTonic, nullptr);
    extra.setProperty ("reharmMinor", reharmKeyMinor, nullptr);

    juce::StringArray ks;
    for (int k : midiSettings.keyswitch) ks.add (juce::String (k));
    extra.setProperty ("keyswitches", ks.joinIntoString (","), nullptr);
    extra.setProperty ("keyswitchChannel", midiSettings.keyswitchChannel, nullptr);
    extra.setProperty ("palmMuteCC", midiSettings.palmMuteCC, nullptr);

    state.removeChild (state.getChildWithName ("GuitarLabData"), nullptr);
    state.appendChild (extra, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GuitarLabProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    auto extra = state.getChildWithName ("GuitarLabData");
    state.removeChild (extra, nullptr);
    params.replaceState (state);

    if (extra.isValid())
    {
        std::vector<int> tuning;
        for (auto& t : juce::StringArray::fromTokens (extra.getProperty ("tuning").toString(), ",", ""))
            tuning.push_back (t.getIntValue());
        instrument.setTuning (tuning);

        phrase = gl::phraseFromText (extra.getProperty ("phrase").toString().toStdString());
        trainer.setInstrument (instrument);
        trainer.deserialise (extra.getProperty ("trainer").toString().toStdString());
        reharmInput    = extra.getProperty ("reharmInput", reharmInput).toString();
        reharmKeyTonic = extra.getProperty ("reharmTonic", 0);
        reharmKeyMinor = extra.getProperty ("reharmMinor", false);

        auto ks = juce::StringArray::fromTokens (extra.getProperty ("keyswitches").toString(), ",", "");
        for (int i = 0; i < ks.size() && i < (int) gl::Technique::NumTechniques; ++i)
            midiSettings.keyswitch[i] = ks[i].getIntValue();
        midiSettings.keyswitchChannel = extra.getProperty ("keyswitchChannel", 0);
        midiSettings.palmMuteCC       = extra.getProperty ("palmMuteCC", -1);
    }

    phraseChanged();

    if (onStateLoaded)
        juce::MessageManager::callAsync ([this] { if (onStateLoaded) onStateLoaded(); });
}

juce::AudioProcessorEditor* GuitarLabProcessor::createEditor()
{
    return new GuitarLabEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarLabProcessor();
}
