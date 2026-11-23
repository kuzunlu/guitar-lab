#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

GuitarLabProcessor::GuitarLabProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Guitar In", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Params", createLayout())
{
    for (auto& f : soundingFret) f = -1;
    for (auto& b : soundingBend) b = 0.0f;
    for (auto& n : externalNotes) n = false;

    for (auto* id : listenedParams())
        params.addParameterListener (id, this);

    phraseChanged();
}

GuitarLabProcessor::~GuitarLabProcessor()
{
    for (auto* id : listenedParams())
        params.removeParameterListener (id, this);
    cancelPendingUpdate();
}

const std::vector<const char*>& GuitarLabProcessor::listenedParams()
{
    static const std::vector<const char*> ids {
        ParamIDs::bendRange, ParamIDs::firstChannel, ParamIDs::sendMpe, ParamIDs::syncToHost,
        ParamIDs::internalBpm, ParamIDs::legatoOverlap, ParamIDs::legatoVelocity,
        ParamIDs::trackMode, ParamIDs::trackInput, ParamIDs::trackStability,
        ParamIDs::trackSensitivity, ParamIDs::trackBends, ParamIDs::trackHysteresis };
    return ids;
}

void GuitarLabProcessor::parameterChanged (const juce::String& id, float)
{
    if (id.startsWith ("track") || id == ParamIDs::bendRange || id == ParamIDs::firstChannel)
        trackerDirty = true;
    triggerAsyncUpdate();
}

void GuitarLabProcessor::handleAsyncUpdate()
{
    phraseChanged();
    if (trackerDirty.exchange (false))
        rebuildTracker();
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
                    NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::synthTone, 1 },      "Sound brightness",
                    NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::legatoOverlap, 1 },  "Legato overlap", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::legatoVelocity, 1 }, "Legato velocity",
                    NormalisableRange<float> (0.2f, 1.0f, 0.01f), 0.65f));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { ParamIDs::passThrough, 1 },    "MIDI thru", true));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { ParamIDs::liveVelocity, 1 },   "Click velocity", 1, 127, 100));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::trackMode, 1 },     "Guitar tracking",
                    StringArray { "Off", "Normal guitar: single notes (mono)", "Hex pickup (one input per string)",
                                  "Normal guitar: chords too (poly)" }, 1)); // default Mono. New modes go at the end: saved projects keep the number
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::trackInput, 1 },    "Tracking input",
                    StringArray { "Mix of all inputs", "Input 1", "Input 2" }, 1));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { ParamIDs::trackStability, 1 }, "Tracking safety", 1, 8, 3));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ParamIDs::trackSensitivity, 1 }, "Tracking sensitivity",
                    NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ParamIDs::trackBends, 1 },    "Track bends", true));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ParamIDs::trackMonitor, 1 },  "Hear guitar", false));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ParamIDs::metronome, 1 },     "Metronome", false));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ParamIDs::trackHysteresis, 1 }, "Tracking hysteresis",
                    NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    return layout;
}

bool GuitarLabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Guitar input: off, mono, stereo, or up to 8 separate channels (hex pickup, one per string).
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in.size() <= gl::kMaxStrings;
}

void GuitarLabProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    inputCopy.setSize (gl::kMaxStrings, maxBlock);
    synth.prepare (sampleRate);
    liveCollector.reset (sampleRate);
    needSetup = true;
    wasPlaying = false;
    for (auto& a : active) a.reset();
    rebuildTracker();
}

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------
void GuitarLabProcessor::rebuildTracker()
{
    syncSettings();

    gl::TrackerSettings ts;
    ts.mode         = (gl::TrackMode) (int) params.getRawParameterValue (ParamIDs::trackMode)->load();
    if (ts.mode == gl::TrackMode::Poly && stepInputMono.load())
        ts.mode = gl::TrackMode::Mono; // notes come one at a time: Mono does not take overtones for more notes
    ts.monoInput    = (int) params.getRawParameterValue (ParamIDs::trackInput)->load() - 1; // 0 = mix -> -1
    ts.stableFrames = (int) params.getRawParameterValue (ParamIDs::trackStability)->load();
    ts.sensitivity  = params.getRawParameterValue (ParamIDs::trackSensitivity)->load();
    ts.trackBends   = params.getRawParameterValue (ParamIDs::trackBends)->load() > 0.5f;
    ts.hysteresis   = params.getRawParameterValue (ParamIDs::trackHysteresis)->load();
    ts.bendRange    = midiSettings.bendRange;
    ts.firstChannel = midiSettings.firstChannel;
    ts.features     = calibrating.load(); // tone colour + chord fingerprints, only needed by the wizard

    // Calibration only fits the tuning it was made for (cents per string); otherwise use defaults.
    auto cal = calibration;
    if (cal.valid && cal.tuning != instrument.tuning())
    {
        const float noise = cal.noiseFloor, soft = cal.softPeak, hard = cal.hardPeak;
        cal = gl::Calibration {};
        cal.valid = true;
        cal.noiseFloor = noise; cal.softPeak = soft; cal.hardPeak = hard; // loudness still fits
    }

    auto fresh = std::make_unique<gl::GuitarTracker>();
    fresh->prepare (currentSampleRate, maxBlock, instrument, cal, ts);
    trackerTuning = instrument.tuning();

    {
        const juce::SpinLock::ScopedLockType lock (trackerLock);
        std::swap (tracker, fresh);
    }
    needAllOff = true; // the old tracker may have left notes on
    // "fresh" now holds the old tracker and is freed here, on this (non-audio) thread.
}

int GuitarLabProcessor::popRecordEvents (RecordEvent* dest, int maxEvents)
{
    const auto scope = recordFifo.read (std::min (maxEvents, recordFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = recordStore[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = recordStore[(size_t) (scope.startIndex2 + i)];
    return n;
}

int GuitarLabProcessor::popTrackedEvents (TrackedEvent* dest, int maxEvents)
{
    const auto scope = trackedFifo.read (std::min (maxEvents, trackedFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = trackedStore[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = trackedStore[(size_t) (scope.startIndex2 + i)];
    return n;
}

void GuitarLabProcessor::readTunerInput (float* dest, int count) const
{
    const int w = tunerWrite.load();
    for (int i = 0; i < count; ++i)
        dest[i] = tunerRing[(size_t) ((w - count + i) & (kTunerRing - 1))];
}

int GuitarLabProcessor::popFrames (gl::AnalysisFrame* dest, int maxFrames)
{
    const auto scope = frameFifo.read (std::min (maxFrames, frameFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = frameStore[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = frameStore[(size_t) (scope.startIndex2 + i)];
    return n;
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

    if (trackerTuning != instrument.tuning())
        rebuildTracker();
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

void GuitarLabProcessor::allLiveNotesOff()
{
    for (int s = 0; s < gl::kMaxStrings; ++s)
        liveNoteOff (s);
}

void GuitarLabProcessor::liveBend (int string, double semitones)
{
    if (string < 0 || string >= gl::kMaxStrings)
        return;
    const int ch = midiSettings.channelForString (string);
    liveRaw (juce::MidiMessage::pitchWheel (ch, gl::pitchBendValue (semitones, midiSettings.bendRange)));
}

void GuitarLabProcessor::startCountIn (double startBeat, bool thenRecord)
{
    countInStart = startBeat;
    countInRecord = thenRecord;
    countInReset = true; // the audio thread restarts its count-in clock
    countingIn = true;
}

void GuitarLabProcessor::addClicks (ClickPlan& plan, double fromBeat, double toBeat, double samplesPerBeat, double beatOffset) const
{
    // One click on every whole beat in [fromBeat, toBeat). beatOffset: beat 0 of the bar (for the accent).
    for (double b = std::ceil (fromBeat - 1e-9); b < toBeat && plan.count < (int) plan.at.size(); b += 1.0)
    {
        plan.at[(size_t) plan.count] = (int) ((b - fromBeat) * samplesPerBeat);
        plan.accent[(size_t) plan.count] = std::abs (std::fmod (b - beatOffset, 4.0)) < 1e-6;
        ++plan.count;
    }
}

void GuitarLabProcessor::renderClicks (juce::AudioBuffer<float>& audio, const ClickPlan& plan)
{
    const int numSamples = audio.getNumSamples();
    int next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < plan.count && plan.at[(size_t) next] <= i)
        {
            clickTotal = clickLeft = (int) (currentSampleRate * 0.03);
            clickFreq = plan.accent[(size_t) next] ? 1760.0 : 1100.0;
            clickPhase = 0.0;
            ++next;
        }
        if (clickLeft <= 0) continue;
        const float env = (float) clickLeft / (float) clickTotal;
        const float v = (float) std::sin (clickPhase) * 0.3f * env * env;
        clickPhase += 2.0 * juce::MathConstants<double>::pi * clickFreq / currentSampleRate;
        --clickLeft;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            audio.addSample (ch, i, v);
    }
}

void GuitarLabProcessor::renderCue (juce::AudioBuffer<float>& audio)
{
    // Start = one short tick, Ok = a rising "ding-ding", Problem = one low, longer tone.
    if (const int c = pendingCue.exchange (0); c != 0)
    {
        const auto ms = [this] (double m) { return (int) (currentSampleRate * m / 1000.0); };
        if (c == (int) Cue::Start)   { cueFreq = { 1320.0, 0.0 };  cueLength = { ms (45), 0 }; }
        if (c == (int) Cue::Ok)      { cueFreq = { 880.0, 1320.0 }; cueLength = { ms (80), ms (110) }; }
        if (c == (int) Cue::Problem) { cueFreq = { 262.0, 0.0 };   cueLength = { ms (220), 0 }; }
        cueTone = 0;
        cuePos = 0;
        cuePhase = 0.0;
    }

    for (int i = 0; i < audio.getNumSamples() && cueTone < 2; ++i)
    {
        const int len = cueLength[(size_t) cueTone];
        if (len <= 0 || cuePos >= len)
        {
            ++cueTone;
            cuePos = 0;
            --i; // this sample belongs to the next tone
            continue;
        }
        const float t = (float) cuePos / (float) len;
        const float env = std::min (1.0f, t * 40.0f) * (1.0f - t) * (1.0f - t); // quick attack, soft fall
        const float v = (float) std::sin (cuePhase) * 0.25f * env;
        cuePhase += 2.0 * juce::MathConstants<double>::pi * cueFreq[(size_t) cueTone] / currentSampleRate;
        ++cuePos;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            audio.addSample (ch, i, v);
    }
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
    const int numSamples = audio.getNumSamples();

    // Keep the guitar input: the buffer is shared with the output and gets cleared next.
    const int numIn = std::min (getTotalNumInputChannels(), gl::kMaxStrings);
    const int numCopied = std::min (numSamples, inputCopy.getNumSamples());
    for (int c = 0; c < numIn; ++c)
        inputCopy.copyFrom (c, 0, audio, c, 0, numCopied);

    audio.clear();

    // Lighthouse tuner (only while it is open) and calibration: the chosen input, or all inputs mixed.
    if ((tunerOn.load() || calibrating.load()) && numIn > 0)
    {
        const int chosen = (int) params.getRawParameterValue (ParamIDs::trackInput)->load() - 1; // -1 = mix
        int w = tunerWrite.load();
        for (int i = 0; i < numCopied; ++i)
        {
            float v = 0.0f;
            if (chosen >= 0 && chosen < numIn) v = inputCopy.getSample (chosen, i);
            else { for (int c = 0; c < numIn; ++c) v += inputCopy.getSample (c, i); v /= (float) numIn; }
            tunerRing[(size_t) (w & (kTunerRing - 1))] = v;
            ++w;
        }
        tunerWrite.store (w & 0x3fffffff);
    }

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

    // --- Count-in: one bar of clicks, then Guitar Lab's own Play starts (and recording, if asked) ---
    ClickPlan clicks;
    if (countingIn.load())
    {
        if (countInReset.exchange (false)) countInClock = 0.0;
        const double spb = currentSampleRate * 60.0 / std::max (1.0, bpm);
        const double from = countInClock, to = countInClock + numSamples / spb;
        addClicks (clicks, from, std::min (to, 4.0), spb, 0.0);
        countInClock = to;
        countInBeat = std::clamp ((int) std::floor (from) + 1, 1, 4);
        if (to >= 4.0)
        {
            countingIn = false;
            countInBeat = 0;
            internalPpq = countInStart.load();
            internalPlaying = true;
            if (countInRecord.load()) recordingGuitar = true;
        }
    }

    // --- Loop a section (Guitar Lab's own Play only; the DAW has its own loop) ---
    if (internalPlaying.load() && ! (sync && hostPlaying))
    {
        const double ls = loopStart.load(), le = loopEnd.load();
        if (le > ls + 1e-6 && (internalPpq >= le - 1e-9 || internalPpq < ls - 1e-9))
            internalPpq = ls;
    }

    bool playing = false;
    double ppqStart = 0.0;
    if (sync && hostPlaying)       { playing = true; ppqStart = hostPpq; }
    else if (internalPlaying.load()) { playing = true; ppqStart = internalPpq; }

    const double samplesPerBeat = currentSampleRate * 60.0 / std::max (1.0, bpm);
    const double ppqEnd = ppqStart + numSamples / samplesPerBeat;

    if (playing && params.getRawParameterValue (ParamIDs::metronome)->load() > 0.5f)
        addClicks (clicks, ppqStart, ppqEnd, samplesPerBeat, 0.0);

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

    // --- Guitar-to-MIDI tracking ---
    {
        // Recording clock: chosen once when recording starts. DAW / Play position if running, else a free clock.
        const bool recording = recordingGuitar.load();
        if (recording && ! wasRecording)
        {
            recordUsesTransport = playing;
            recordClock = 0.0;
        }
        wasRecording = recording;
        const double recordBase = recordUsesTransport ? ppqStart : recordClock;
        recordClock += numSamples / samplesPerBeat;

        const juce::SpinLock::ScopedTryLockType trackLock (trackerLock);
        if (trackLock.isLocked() && tracker != nullptr && tracker->mode() != gl::TrackMode::Off && numIn > 0)
        {
            const float* ptrs[gl::kMaxStrings] {};
            for (int c = 0; c < numIn; ++c)
                ptrs[c] = inputCopy.getReadPointer (c);

            const bool quiet = calibrating.load(); // the wizard listens, but nothing is sent
            tracker->process (ptrs, numIn, numCopied,
                [&] (int offset, uint8_t status, uint8_t d1, uint8_t d2)
                {
                    if (quiet)
                        return;
                    out.addEvent (juce::MidiMessage (status, d1, d2), juce::jlimit (0, numSamples - 1, offset));

                    if ((status & 0xE0) == 0x80) // note-on or note-off: for the note log
                    {
                        const auto scope = trackedFifo.write (1);
                        const TrackedEvent e { samplesProcessed.load() + offset, status, d1, d2 };
                        if (scope.blockSize1 > 0)      trackedStore[(size_t) scope.startIndex1] = e;
                        else if (scope.blockSize2 > 0) trackedStore[(size_t) scope.startIndex2] = e;
                    }

                    if (recording)
                    {
                        const auto scope = recordFifo.write (1);
                        const RecordEvent e { recordBase + offset / samplesPerBeat, recordUsesTransport, status, d1, d2 };
                        if (scope.blockSize1 > 0)      recordStore[(size_t) scope.startIndex1] = e;
                        else if (scope.blockSize2 > 0) recordStore[(size_t) scope.startIndex2] = e;
                    }
                },
                [this] (const gl::AnalysisFrame& f)
                {
                    const auto scope = frameFifo.write (1);
                    if (scope.blockSize1 > 0)      frameStore[(size_t) scope.startIndex1] = f;
                    else if (scope.blockSize2 > 0) frameStore[(size_t) scope.startIndex2] = f;
                });
        }
    }

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

    // --- Metronome / count-in clicks ---
    renderClicks (audio, clicks);
    renderCue (audio);

    // --- Hear the guitar itself (mixed to mono, sent to every output channel) ---
    if (numIn > 0 && params.getRawParameterValue (ParamIDs::trackMonitor)->load() > 0.5f)
        for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
            for (int c = 0; c < numIn; ++c)
                audio.addFrom (ch, 0, inputCopy, c, 0, numCopied, 1.0f / (float) numIn);

    samplesProcessed += numSamples;
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
    // While a new calibration is only being tried, the project keeps the one from before.
    extra.setProperty ("calibration", juce::String ((work.calTryBackup ? *work.calTryBackup : calibration).serialise()), nullptr);
    if (work.calOpen && work.calPage == 1) // a calibration in progress (wizard page "Running"): go on with it later
        extra.setProperty ("calibrationDraft", juce::String (work.calSession.saveDraft()), nullptr);
    extra.appendChild (uiState.createCopy(), nullptr);

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
        calibration.deserialise (extra.getProperty ("calibration").toString().toStdString());
        if (extra.hasProperty ("calibrationDraft")
            && work.calSession.loadDraft (extra.getProperty ("calibrationDraft").toString().toStdString()))
        {
            work.calOpen = true;
            work.calPage = 1; // the wizard opens at the same step
        }

        // Copied into the same tree object: the panels look their part up again when the state is loaded.
        const auto ui = extra.getChildWithName ("UiState");
        uiState.copyPropertiesAndChildrenFrom (ui.isValid() ? ui : juce::ValueTree ("UiState"), nullptr);
    }

    phraseChanged();
    rebuildTracker();

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
