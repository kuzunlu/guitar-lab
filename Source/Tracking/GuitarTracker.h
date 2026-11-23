#pragma once

#include "Theory/Instrument.h"
#include "Tracking/Calibration.h"
#include "Tracking/PitchDetector.h"
#include "Tracking/PolyTracker.h"
#include "Tracking/Spectrum.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gl
{
    // Poly = normal guitar on one cable, several notes at once (PolyTracker). Values match the saved parameter.
    enum class TrackMode { Off = 0, Mono = 1, Hex = 2, Poly = 3 };

    struct TrackerSettings
    {
        TrackMode mode = TrackMode::Off;
        int    monoInput = -1;       // mono mode: input channel to listen to (0 = left/1, 1 = right/2), -1 = mix all
        int    stableFrames = 3;     // how many ~5 ms frames the pitch must hold before a note starts (2 = fast, 5 = safe)
        double sensitivity = 1.0;    // > 1 = reacts to softer playing, < 1 = ignores more noise
        bool   trackBends = true;    // send pitch bend for bends, slides and vibrato
        int    bendRange = 12;       // must match the MIDI settings
        int    firstChannel = 2;     // MIDI channel of the highest string
        bool   features = false;     // calibration: send tone colour + chord fingerprints with the frames
        double hysteresis = 0.0;     // poly: 0..1, an overtone of a louder, just-picked note needs more energy (0 = off)
    };

    // Turns guitar audio into MIDI: notes on per-string channels, pitch bend, velocity.
    class GuitarTracker
    {
    public:
        // Allocates: call from prepareToPlay or the message thread while audio is stopped.
        void prepare (double sampleRate, int maxBlockSize, const Instrument& inst,
                      const Calibration& cal, const TrackerSettings& settings);

        void reset();

        // emit (sampleOffset, status, data1, data2) for each MIDI message.
        // frame (AnalysisFrame) for each analysis hop per channel (used by the tuner and calibration).
        template <typename Emit, typename Frame>
        void process (const float* const* inputs, int numInputs, int numSamples, Emit&& emit, Frame&& frame);

        // Note-offs for everything sounding (when tracking is switched off or the block is bypassed).
        template <typename Emit>
        void allOff (Emit&& emit);

        TrackMode mode() const { return settings.mode; }

    private:
        struct Voice
        {
            PitchDetector detector;
            Spectrum timbreSpec, chromaSpec, attackSpec;
            std::vector<float> scratch;
            int   hopCount = 0;

            int   fixedString = -1;     // hex mode: the string this voice listens to
            int   inputChannel = 0;
            float gain = 1.0f;
            float rawRms = 0.0f;        // last level before gain (for leak checks between strings)

            enum class State { Idle, Pending, On, Chord } state = State::Idle;
            int    candidate = -1;      // note being confirmed
            int    stableCount = 0;
            int    pendingHops = 0;
            int    onHops = 0;          // hops since the single note started
            float  attackPeak = 0.0f;
            float  envelope = 0.0f;
            int    quietFrames = 0;
            int    unclearFrames = 0;
            int    jumpNote = -1;       // a big legato jump waiting to be confirmed
            int    jumpCount = 0;

            int    note = -1;           // sounding MIDI note
            int    string = -1;
            int    channel = 1;
            int    lastBend = 8192;
            double lastMidi = 0.0;      // last pitch (with fraction)
            double smoothedDev = 0.0;
            double spotCents = 0.0;     // calibrated pitch error of the fret being played

            std::array<int, kMaxStrings> chordNotes {}; // Chord state: note per string, -1 = silent
        };

        template <typename Emit>
        void hop (Voice& v, const PitchDetector::Result& r, int offset, Emit&& emit);

        template <typename Emit>
        void startNote (Voice& v, int note, float peak, double dev, int offset, Emit&& emit, bool legato = false);

        template <typename Emit>
        void stopNote (Voice& v, int offset, Emit&& emit);

        template <typename Emit>
        bool tryChord (Voice& v, double minSimilarity, int offset, Emit&& emit);

        int    chooseString (int note, const Timbre* timbre) const; // mono mode: guess the string
        double centsCorrection (const Voice& v) const;
        int    velocityFor (int string, int fret, float peak) const;
        Timbre timbreNow (Voice& v, double hz);
        Overtones overtonesNow (Voice& v, double hz);
        Chroma chromaNow (Voice& v);
        float  leakLevel (const Voice& v) const; // hex: level that may be leak from other strings

        Instrument inst;
        Calibration cal;
        TrackerSettings settings;
        std::vector<Voice> voices;
        std::vector<float> mix;
        int handFret = 5;
        PolyTracker poly;     // Poly mode: makes the notes (voice 0 feeds the tuner display and gives Poly its readings)
        int64_t samplesIn = 0; // input samples since reset (to put voice 0's readings at the right time for Poly)
    };

    // =======================================================================
    // Template code (kept in the header so the audio thread does no extra calls)
    // =======================================================================
    template <typename Emit, typename Frame>
    void GuitarTracker::process (const float* const* inputs, int numInputs, int numSamples, Emit&& emit, Frame&& frame)
    {
        if (settings.mode == TrackMode::Off || numInputs <= 0)
            return;

        for (int vi = 0; vi < (int) voices.size(); ++vi)
        {
            auto& v = voices[(size_t) vi];
            const float* src = nullptr;

            if (settings.mode != TrackMode::Hex && settings.monoInput < 0 && numInputs > 1)
            {
                // Mix all inputs into one signal.
                const int n = std::min (numSamples, (int) mix.size());
                for (int i = 0; i < n; ++i)
                {
                    float s = 0.0f;
                    for (int c = 0; c < numInputs; ++c) s += inputs[c][i];
                    mix[(size_t) i] = s / (float) numInputs;
                }
                src = mix.data();
                numSamples = n;
            }
            else
            {
                const int ch = std::clamp (v.inputChannel, 0, numInputs - 1);
                src = inputs[ch];
            }

            v.detector.process (src, numSamples, [&] (int offset, const PitchDetector::Result& r)
            {
                PitchDetector::Result scaled = r;
                scaled.rms  *= v.gain;
                scaled.peak *= v.gain;
                v.rawRms = (float) r.rms;

                AnalysisFrame f { settings.mode == TrackMode::Hex ? v.inputChannel : 0,
                                  (float) r.hz, (float) r.clarity, (float) scaled.rms, (float) scaled.peak };

                // Calibration wants tone colour and chord fingerprints. Every 4th hop is enough.
                if (settings.features && (++v.hopCount % 4) == 0 && r.rms > 1.0e-4)
                {
                    f.hasFeatures = true;
                    f.timbre = timbreNow (v, r.hz);
                    f.chroma = chromaNow (v);
                }
                // ...and (normal guitar) the overtones at the attack, every 2nd hop: Poly's fingerprints.
                if (settings.features && settings.mode != TrackMode::Hex && (v.hopCount % 2) == 0
                    && r.hz > 0.0 && r.clarity > 0.8 && r.rms > 1.0e-4)
                {
                    f.hasOvertones = true;
                    f.overtones = overtonesNow (v, r.hz);
                }

                frame (f);
                if (settings.mode != TrackMode::Poly)
                    hop (v, scaled, offset, emit);
                else if (vi == 0)
                    poly.hint (samplesIn + offset + 1, r.hz > 0.0 ? (float) (hzToMidi (r.hz) - centsCorrection (v) / 100.0) : 0.0f,
                               (float) r.clarity);
            });

            if (settings.mode == TrackMode::Poly)
                poly.process (src, numSamples, emit);
        }
        samplesIn += numSamples;
    }

    template <typename Emit>
    void GuitarTracker::startNote (Voice& v, int note, float peak, double dev, int offset, Emit&& emit, bool legato)
    {
        if (v.fixedString >= 0)
            v.string = v.fixedString;
        else if (cal.hasPositions())
        {
            const Timbre t = timbreNow (v, midiToHz (note + dev));
            v.string = chooseString (note, &t);
        }
        else
            v.string = chooseString (note, nullptr);

        v.channel = std::clamp (settings.firstChannel + std::max (0, v.string), 1, 16);
        v.note = std::clamp (note, 0, 127);

        const int fret = (v.string >= 0 && v.string < inst.numStrings()) ? v.note - inst.tuning()[(size_t) v.string] : -1;

        // Bends are measured from the real pitch of this fret (full calibration), not the perfect note.
        if (fret >= 0 && cal.position (v.string, fret) != nullptr)
        {
            const double before = centsCorrection (v);
            v.spotCents = cal.centsAt (v.string, fret);
            dev += (before - v.spotCents) / 100.0;
        }
        else
            v.spotCents = centsCorrection (v);

        int velocity = velocityFor (v.string, fret, peak);
        if (legato) velocity = std::max (1, velocity * 2 / 3);

        // Pitch bend first, so the note starts at the right pitch.
        const int bend = settings.trackBends
            ? std::clamp ((int) std::lround (8192.0 + dev / std::max (1, settings.bendRange) * 8192.0), 0, 16383) : 8192;
        if (bend != v.lastBend)
        {
            emit (offset, (uint8_t) (0xE0 | (v.channel - 1)), (uint8_t) (bend & 0x7F), (uint8_t) ((bend >> 7) & 0x7F));
            v.lastBend = bend;
        }
        emit (offset, (uint8_t) (0x90 | (v.channel - 1)), (uint8_t) v.note, (uint8_t) std::clamp (velocity, 1, 127));

        if (fret >= 0)
            handFret = std::clamp (fret, 0, kNumFrets);
        v.state = Voice::State::On;
        v.smoothedDev = dev;
        v.lastMidi = note + dev;
        v.quietFrames = v.unclearFrames = 0;
        v.onHops = 0;
    }

    template <typename Emit>
    void GuitarTracker::stopNote (Voice& v, int offset, Emit&& emit)
    {
        if (v.state == Voice::State::Chord)
        {
            for (int s = 0; s < kMaxStrings; ++s)
                if (v.chordNotes[(size_t) s] >= 0)
                {
                    const int ch = std::clamp (settings.firstChannel + s, 1, 16);
                    emit (offset, (uint8_t) (0x80 | (ch - 1)), (uint8_t) v.chordNotes[(size_t) s], (uint8_t) 0);
                    v.chordNotes[(size_t) s] = -1;
                }
        }
        else if (v.note >= 0)
            emit (offset, (uint8_t) (0x80 | (v.channel - 1)), (uint8_t) v.note, (uint8_t) 0);

        v.note = -1;
        v.state = Voice::State::Idle;
        v.candidate = -1;
        v.stableCount = 0;
        v.pendingHops = 0;
    }

    template <typename Emit>
    bool GuitarTracker::tryChord (Voice& v, double minSimilarity, int offset, Emit&& emit)
    {
        // Mono mode only: compare the sound with the chords taught during calibration.
        if (cal.chords.empty() || v.fixedString >= 0)
            return false;

        const Chroma now = chromaNow (v);
        const ChordTemplate* best = nullptr;
        float bestSim = 0.0f;
        for (const auto& t : cal.chords)
        {
            const float sim = chromaSimilarity (now, t.chroma);
            if (sim > bestSim) { bestSim = sim; best = &t; }
        }
        if (best == nullptr || bestSim < minSimilarity)
            return false;

        // A single note was already sent for this pluck: replace it with the chord.
        if (v.state == Voice::State::On && v.note >= 0)
        {
            emit (offset, (uint8_t) (0x80 | (v.channel - 1)), (uint8_t) v.note, (uint8_t) 0);
            v.note = -1;
        }

        const int velocity = cal.velocityFromPeak (v.attackPeak);
        v.chordNotes.fill (-1);
        for (int s = 0; s < (int) best->frets.size() && s < inst.numStrings(); ++s)
        {
            const int f = best->frets[(size_t) s];
            if (f < 0) continue;
            const int ch = std::clamp (settings.firstChannel + s, 1, 16);
            const int note = std::clamp (inst.tuning()[(size_t) s] + f, 0, 127);
            emit (offset, (uint8_t) (0xE0 | (ch - 1)), (uint8_t) 0, (uint8_t) 64); // no bend on chord notes
            emit (offset, (uint8_t) (0x90 | (ch - 1)), (uint8_t) note, (uint8_t) std::clamp (velocity, 1, 127));
            v.chordNotes[(size_t) s] = note;
        }
        v.lastBend = 8192;
        v.state = Voice::State::Chord;
        v.quietFrames = 0;
        return true;
    }

    template <typename Emit>
    void GuitarTracker::allOff (Emit&& emit)
    {
        if (settings.mode == TrackMode::Poly)
            poly.allOff (emit);
        for (auto& v : voices)
        {
            stopNote (v, 0, emit);
            if (v.lastBend != 8192)
            {
                emit (0, (uint8_t) (0xE0 | (v.channel - 1)), (uint8_t) 0, (uint8_t) 64);
                v.lastBend = 8192;
            }
        }
    }

    template <typename Emit>
    void GuitarTracker::hop (Voice& v, const PitchDetector::Result& r, int offset, Emit&& emit)
    {
        const float gateOn  = cal.gateOn() / (float) settings.sensitivity;
        const float gateOff = cal.gateOff() / (float) settings.sensitivity;

        // A sudden jump in loudness = a new pluck.
        const bool pluck = r.peak > gateOn && r.peak > v.envelope * 1.8f + gateOn * 0.5f;
        v.envelope = std::max ((float) r.peak, v.envelope * 0.93f);

        // Hex: sound that is only leak from a louder neighbour string must not start a note here.
        const bool onlyLeak = v.fixedString >= 0 && v.rawRms < leakLevel (v);

        const bool pitched = r.hz > 0.0 && r.clarity > 0.8;
        const double raw = pitched ? hzToMidi (r.hz) : 0.0;
        const double midi = raw - centsCorrection (v) / 100.0;
        const int nearest = pitched ? (int) std::lround (midi) : -1;

        switch (v.state)
        {
            case Voice::State::Idle:
                if (r.rms > gateOn && ! onlyLeak)
                {
                    v.state = Voice::State::Pending;
                    v.candidate = nearest;
                    v.stableCount = pitched ? 1 : 0;
                    v.pendingHops = 1;
                    v.attackPeak = (float) r.peak;
                }
                break;

            case Voice::State::Pending:
                ++v.pendingHops;
                v.attackPeak = std::max (v.attackPeak, (float) r.peak);
                if (r.rms < gateOff) { v.state = Voice::State::Idle; break; }

                // Loud but no single clear pitch: maybe a chord (mono, taught chords).
                // A chord fingerprint needs ~60 ms of sound, so check at 60 and 100 ms.
                if (! pitched)
                {
                    if (v.pendingHops == 12 || v.pendingHops == 20)
                        tryChord (v, 0.85, offset, emit);
                    break;
                }

                if (nearest == v.candidate) ++v.stableCount;
                else { v.candidate = nearest; v.stableCount = 1; }

                if (v.stableCount >= settings.stableFrames)
                    startNote (v, v.candidate, v.attackPeak, midi - v.candidate, offset, emit);
                break;

            case Voice::State::Chord:
                v.quietFrames = r.rms < gateOff ? v.quietFrames + 1 : 0;
                if (v.quietFrames >= 4)
                    stopNote (v, offset, emit);
                else if (pluck)
                {
                    stopNote (v, offset, emit);
                    v.state = Voice::State::Pending;
                    v.candidate = nearest;
                    v.stableCount = pitched ? 1 : 0;
                    v.pendingHops = 1;
                    v.attackPeak = (float) r.peak;
                }
                break;

            case Voice::State::On:
            {
                // End of note: quiet, or no clear pitch for a while.
                v.quietFrames   = r.rms < gateOff ? v.quietFrames + 1 : 0;
                v.unclearFrames = pitched ? 0 : v.unclearFrames + 1;
                if (v.quietFrames >= 4 || v.unclearFrames >= 14)
                {
                    stopNote (v, offset, emit);
                    break;
                }

                // New pluck: start again (pitch will be confirmed in Pending).
                if (pluck)
                {
                    stopNote (v, offset, emit);
                    v.state = Voice::State::Pending;
                    v.candidate = nearest;
                    v.stableCount = pitched ? 1 : 0;
                    v.pendingHops = 1;
                    v.attackPeak = (float) r.peak;
                    break;
                }

                // YIN often hears only the root of a strummed chord. ~60 ms in, the chord fingerprint is
                // clear enough: if it matches a taught chord, swap the single note for the whole shape.
                if (++v.onHops == 12 && tryChord (v, 0.88, offset, emit))
                    break;

                if (! pitched) break;

                // Deviation from the real pitch of the fret being played.
                const double spotMidi = raw - v.spotCents / 100.0;
                const double dev = spotMidi - v.note;
                const double jump = std::abs (spotMidi - v.lastMidi);
                v.lastMidi = spotMidi;

                // Sudden jump of nearly a semitone or more = hammer-on / pull-off: legato new note.
                // Drift past the bend range = long slide: also a new legato note.
                const bool legatoJump = jump > 0.75 && std::abs (dev) > 0.75;
                const bool pastRange = std::abs (dev) > settings.bendRange - 0.5;
                // A jump of 7+ half steps is often an overtone read for a moment (a real hammer-on that big is rare):
                // it must hold for 3 readings (~10 ms) before the new note starts.
                const bool wantChange = legatoJump || pastRange || (! settings.trackBends && std::abs (dev) > 0.6);
                if (wantChange && std::abs (nearest - v.note) >= 7)
                {
                    v.jumpCount = nearest == v.jumpNote ? v.jumpCount + 1 : 1;
                    v.jumpNote = nearest;
                    if (v.jumpCount < 3) { v.lastMidi = v.note + v.smoothedDev; break; }
                }
                else
                    v.jumpNote = -1;
                if (wantChange)
                {
                    const int oldNote = v.note, oldChannel = v.channel;
                    startNote (v, nearest, (float) r.peak, midi - nearest, offset, emit, true); // new note first (legato overlap)
                    if (oldNote >= 0 && ! (oldChannel == v.channel && oldNote == v.note))
                        emit (offset, (uint8_t) (0x80 | (oldChannel - 1)), (uint8_t) oldNote, (uint8_t) 0);
                    break;
                }

                // Slow change = bend / vibrato / short slide: pitch bend, smoothed a little.
                if (settings.trackBends)
                {
                    v.smoothedDev += (dev - v.smoothedDev) * 0.6;
                    const int bend = std::clamp ((int) std::lround (8192.0 + v.smoothedDev / std::max (1, settings.bendRange) * 8192.0), 0, 16383);
                    if (std::abs (bend - v.lastBend) > 8) // ignore tiny changes (less than ~1 cent at range 12)
                    {
                        emit (offset, (uint8_t) (0xE0 | (v.channel - 1)), (uint8_t) (bend & 0x7F), (uint8_t) ((bend >> 7) & 0x7F));
                        v.lastBend = bend;
                    }
                }
                break;
            }
        }
    }
}
