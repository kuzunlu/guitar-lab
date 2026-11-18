#include "Tracking/GuitarTracker.h"

namespace gl
{
    void GuitarTracker::prepare (double sampleRate, int maxBlockSize, const Instrument& i,
                                 const Calibration& c, const TrackerSettings& s)
    {
        inst = i;
        cal = c;
        settings = s;
        settings.stableFrames = std::clamp (settings.stableFrames, 1, 10);
        settings.sensitivity = std::clamp (settings.sensitivity, 0.25, 4.0);
        mix.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
        voices.clear();

        if (settings.mode == TrackMode::Off)
            return;

        // Calibration listens like mono mode (one pitch at a time), so the same calibration fits both.
        if (settings.mode == TrackMode::Poly && settings.features)
            settings.mode = TrackMode::Mono;

        if (settings.mode == TrackMode::Poly)
            poly.prepare (sampleRate, inst, cal, { settings.stableFrames, settings.sensitivity, settings.trackBends,
                                                   settings.bendRange, settings.firstChannel });

        if (settings.mode != TrackMode::Hex)
        {
            // One detector for the whole guitar range (lowest open string to fret 24, plus room for bends).
            int lo = 127, hi = 0;
            for (int n : inst.tuning()) { lo = std::min (lo, n); hi = std::max (hi, n + kNumFrets); }
            voices.resize (1);
            voices[0].inputChannel = std::max (0, settings.monoInput);
            voices[0].detector.prepare (sampleRate, midiToHz (lo - 1.5), midiToHz (hi + 3.0));
        }
        else
        {
            // One detector per string, each only listening to its own range: faster and fewer octave mistakes.
            voices.resize ((size_t) inst.numStrings());
            for (int str = 0; str < inst.numStrings(); ++str)
            {
                auto& v = voices[(size_t) str];
                v.fixedString = str;
                const auto& sc = cal.strings[(size_t) str];
                v.inputChannel = (cal.valid && sc.inputChannel >= 0) ? sc.inputChannel : str;
                v.gain = cal.valid ? sc.gain : 1.0f;
                const int open = inst.tuning()[(size_t) str];
                v.detector.prepare (sampleRate, midiToHz (open - 1.5), midiToHz (open + kNumFrets + 4.0));
            }
        }

        for (auto& v : voices)
        {
            v.timbreSpec.prepare (2048);
            v.chromaSpec.prepare (PitchDetector::kHistory);
            v.scratch.assign ((size_t) PitchDetector::kHistory, 0.0f);
        }
        reset();
    }

    void GuitarTracker::reset()
    {
        for (auto& v : voices)
        {
            v.detector.reset();
            v.state = Voice::State::Idle;
            v.note = -1;
            v.candidate = -1;
            v.stableCount = v.quietFrames = v.unclearFrames = v.pendingHops = v.hopCount = 0;
            v.envelope = v.attackPeak = v.rawRms = 0.0f;
            v.lastBend = 8192;
            v.chordNotes.fill (-1);
        }
        handFret = 5;
        poly.reset();
    }

    double GuitarTracker::centsCorrection (const Voice& v) const
    {
        if (! cal.valid) return 0.0;
        return v.fixedString >= 0 ? cal.centsFor (v.fixedString) : cal.globalCents;
    }

    Timbre GuitarTracker::timbreNow (Voice& v, double hz)
    {
        const int n = v.timbreSpec.size();
        v.detector.copyHistory (v.scratch.data(), n);
        v.timbreSpec.compute (v.scratch.data());
        return v.timbreSpec.timbre (hz, v.detector.analysisRate());
    }

    Chroma GuitarTracker::chromaNow (Voice& v)
    {
        const int n = v.chromaSpec.size();
        v.detector.copyHistory (v.scratch.data(), n);
        v.chromaSpec.compute (v.scratch.data());
        return v.chromaSpec.chroma (v.detector.analysisRate(), cal.valid ? cal.globalCents : 0.0);
    }

    int GuitarTracker::velocityFor (int string, int fret, float peak) const
    {
        // Even out dead spots and hot spots measured in a full calibration.
        if (auto* p = cal.position (string, fret); p != nullptr && p->level > 0.0f && cal.typicalLevel > 0.0f)
            peak *= std::clamp (cal.typicalLevel / p->level, 0.5f, 2.0f);
        return cal.velocityFromPeak (peak, string);
    }

    float GuitarTracker::leakLevel (const Voice& v) const
    {
        float leak = 0.0f;
        for (const auto& o : voices)
            if (&o != &v && o.fixedString >= 0 && v.inputChannel >= 0 && v.inputChannel < kMaxStrings)
                leak = std::max (leak, o.rawRms * cal.bleed[(size_t) o.fixedString][(size_t) v.inputChannel] * 1.5f);
        return leak;
    }

    int GuitarTracker::chooseString (int note, const Timbre* timbre) const
    {
        // Mono mode cannot hear the string directly. Score every spot where the note can be played:
        //  - close to where the hand was (most playing stays in one area),
        //  - a small penalty above the 12th fret,
        //  - after a full calibration: how much the tone colour matches the tone colour of that spot.
        const auto spots = inst.positionsOf (note);
        if (spots.empty())
            return inst.numStrings() - 1;

        int best = spots.front().string;
        double bestCost = 1e9;
        for (auto p : spots)
        {
            if (p.fret > cal.lastFret[(size_t) p.string])
                continue; // this guitar has no such fret

            double cost = 0.35 * std::abs (p.fret - handFret) + (p.fret > 12 ? 1.0 : 0.0);
            if (timbre != nullptr)
                if (auto* pos = cal.position (p.string, p.fret); pos != nullptr && pos->hasTimbre)
                    cost += 2.0 * timbreDistance (*timbre, pos->timbre);

            if (cost < bestCost) { bestCost = cost; best = p.string; }
        }
        return best;
    }
}
