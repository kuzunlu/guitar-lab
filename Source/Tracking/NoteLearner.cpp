#include "Tracking/NoteLearner.h"

#include "Tracking/GuitarTracker.h"
#include "Tracking/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <random>

namespace gl
{
    PickSpan findPick (const float* samples, int count, double rate, double tailSeconds)
    {
        // Loudness in 5 ms blocks; a pick = the block with the biggest jump over the 20 ms before it.
        const int block = std::max (1, (int) (rate * 0.005));
        const int numBlocks = count / block;
        std::vector<float> env ((size_t) std::max (0, numBlocks), 0.0f);
        float loudest = 0.0f;
        for (int b = 0; b < numBlocks; ++b)
        {
            double sq = 0.0;
            for (int i = b * block; i < (b + 1) * block; ++i) sq += (double) samples[i] * samples[i];
            env[(size_t) b] = (float) std::sqrt (sq / block);
            loudest = std::max (loudest, env[(size_t) b]);
        }
        auto jumpAt = [&] (int b)
        {
            float before = 1.0e-6f;
            for (int k = std::max (0, b - 4); k < b; ++k) before = std::max (before, env[(size_t) k]);
            return env[(size_t) b] / before;
        };

        PickSpan span;
        const int lastSearch = numBlocks - (int) (tailSeconds / 0.005);
        int pick = -1;
        float bestJump = 2.0f; // at least twice as loud as just before
        for (int b = 1; b < lastSearch; ++b)
            if (env[(size_t) b] > 0.2f * loudest)
                if (const float j = jumpAt (b); j > bestJump) { bestJump = j; pick = b; }
        if (pick < 0) return span;

        span.start = std::max (0, pick * block - (int) (rate * 0.02));
        span.end = count;
        // The next pick (after the note had 100 ms): the recording ends just before it.
        for (int b = pick + 20; b < numBlocks; ++b)
            if (env[(size_t) b] > 0.2f * loudest && jumpAt (b) > 2.0f)
            {
                span.end = std::max (span.start, (b - 2) * block);
                break;
            }
        return span;
    }

    namespace
    {
        // Keep ~24 kHz: plenty for Poly (it works at ~12 kHz) and a quarter of the size at 96 kHz.
        std::vector<float> downsample (const float* samples, int count, double sampleRate, double& newRate)
        {
            const int factor = std::max (1, (int) (sampleRate / 22000.0));
            newRate = sampleRate / factor;
            Biquad lp1, lp2;
            lp1.setLowPass (sampleRate, newRate * 0.45);
            lp2.setLowPass (sampleRate, newRate * 0.45);
            std::vector<float> down;
            down.reserve ((size_t) (count / factor + 1));
            for (int i = 0; i < count; ++i)
            {
                const float v = lp2.process (lp1.process (samples[i]));
                if (i % factor == 0) down.push_back (v);
            }
            return down;
        }
    }

    void CalibrationAudio::put (int string, int fret, const float* samples, int count, double sampleRate)
    {
        double newRate = 0.0;
        const auto down = downsample (samples, count, sampleRate, newRate);
        if ((! notes.empty() || ! shapes.empty()) && std::abs (newRate - rate) > 1.0)
        {
            notes.clear(); // the audio device changed during the calibration: start again
            shapes.clear();
        }
        rate = newRate;

        const auto span = findPick (down.data(), (int) down.size(), rate, 0.5);
        FretRecording r { string, fret, {} };
        if (span.start >= 0)
            r.samples.assign (down.begin() + span.start, down.begin() + span.end);

        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const FretRecording& n)
                     { return n.string == string && n.fret == fret; }), notes.end());
        if (! r.samples.empty())
            notes.push_back (std::move (r));
    }

    void CalibrationAudio::putShape (const std::vector<int>& frets, const float* samples, int count, double sampleRate)
    {
        double newRate = 0.0;
        auto down = downsample (samples, count, sampleRate, newRate);
        if ((! notes.empty() || ! shapes.empty()) && std::abs (newRate - rate) > 1.0)
        {
            notes.clear();
            shapes.clear();
        }
        rate = newRate;
        shapes.erase (std::remove_if (shapes.begin(), shapes.end(), [&] (const ShapeRecording& r) { return r.frets == frets; }),
                      shapes.end());
        shapes.push_back ({ frets, std::move (down) });
    }

    const FretRecording* CalibrationAudio::find (int string, int fret) const
    {
        for (const auto& n : notes)
            if (n.string == string && n.fret == fret) return &n;
        return nullptr;
    }

    namespace
    {
        constexpr char kMagic[4] = { 'G', 'L', 'C', 'A' };
        template <typename T> void writeValue (std::ofstream& out, T v) { out.write (reinterpret_cast<const char*> (&v), sizeof (T)); }
        template <typename T> bool readValue (std::ifstream& in, T& v) { return (bool) in.read (reinterpret_cast<char*> (&v), sizeof (T)); }
    }

    bool CalibrationAudio::save (const std::filesystem::path& path) const
    {
        std::ofstream out (path, std::ios::binary);
        if (! out) return false;
        out.write (kMagic, 4);
        writeValue<int32_t> (out, 2);
        writeValue<double> (out, rate);
        writeValue<int32_t> (out, (int32_t) notes.size());
        for (const auto& n : notes)
        {
            float peak = 1.0e-9f;
            for (float v : n.samples) peak = std::max (peak, std::abs (v));
            writeValue<int32_t> (out, n.string);
            writeValue<int32_t> (out, n.fret);
            writeValue<int32_t> (out, (int32_t) n.samples.size());
            writeValue<float> (out, peak);
            for (float v : n.samples)
                writeValue<int16_t> (out, (int16_t) std::lround (std::clamp (v / peak, -1.0f, 1.0f) * 32767.0f));
        }
        writeValue<int32_t> (out, (int32_t) shapes.size());
        for (const auto& sh : shapes)
        {
            float peak = 1.0e-9f;
            for (float v : sh.samples) peak = std::max (peak, std::abs (v));
            writeValue<int32_t> (out, (int32_t) sh.frets.size());
            for (int f : sh.frets) writeValue<int32_t> (out, f);
            writeValue<int32_t> (out, (int32_t) sh.samples.size());
            writeValue<float> (out, peak);
            for (float v : sh.samples)
                writeValue<int16_t> (out, (int16_t) std::lround (std::clamp (v / peak, -1.0f, 1.0f) * 32767.0f));
        }
        return (bool) out;
    }

    bool CalibrationAudio::load (const std::filesystem::path& path)
    {
        std::ifstream in (path, std::ios::binary);
        char magic[4] {};
        int32_t version = 0, count = 0;
        if (! in.read (magic, 4) || ! std::equal (magic, magic + 4, kMagic) || ! readValue (in, version) || version < 1 || version > 2)
            return false;
        CalibrationAudio loaded;
        if (! readValue (in, loaded.rate) || ! readValue (in, count) || count < 0 || count > 10000) return false;
        for (int32_t i = 0; i < count; ++i)
        {
            FretRecording n;
            int32_t length = 0;
            float peak = 0.0f;
            if (! readValue (in, n.string) || ! readValue (in, n.fret) || ! readValue (in, length) || ! readValue (in, peak) || length < 0 || length > 10000000)
                return false;
            n.samples.resize ((size_t) length);
            for (auto& v : n.samples)
            {
                int16_t s = 0;
                if (! readValue (in, s)) return false;
                v = s / 32767.0f * peak;
            }
            loaded.notes.push_back (std::move (n));
        }
        int32_t shapeCount = 0;
        if (version >= 2 && (! readValue (in, shapeCount) || shapeCount < 0 || shapeCount > 1000)) return false;
        for (int32_t i = 0; i < shapeCount; ++i)
        {
            ShapeRecording sh;
            int32_t strings = 0, length = 0;
            float peak = 0.0f;
            if (! readValue (in, strings) || strings < 0 || strings > 16) return false;
            sh.frets.resize ((size_t) strings);
            for (auto& f : sh.frets) { int32_t v = 0; if (! readValue (in, v)) return false; f = v; }
            if (! readValue (in, length) || ! readValue (in, peak) || length < 0 || length > 50000000) return false;
            sh.samples.resize ((size_t) length);
            for (auto& v : sh.samples)
            {
                int16_t s = 0;
                if (! readValue (in, s)) return false;
                v = s / 32767.0f * peak;
            }
            loaded.shapes.push_back (std::move (sh));
        }
        *this = std::move (loaded);
        return true;
    }

    // =======================================================================
    // Learning
    // =======================================================================
    LearnMix makeLearnMix (const CalibrationAudio& audio, const Instrument& inst, unsigned seed, double seconds)
    {
        const int numStrings = inst.numStrings();
        std::vector<int> topFret ((size_t) numStrings, -1);
        for (const auto& n : audio.notes)
            if (n.string >= 0 && n.string < numStrings) topFret[(size_t) n.string] = std::max (topFret[(size_t) n.string], n.fret);
        int top = 0;
        for (int f : topFret) top = std::max (top, f);

        // Shapes: the calibration's chord shapes for this tuning, moved up the neck like barre chords.
        const auto shapes = CalibrationSession::chordShapesFor (inst);

        struct Place { int string, fret; double t, gain, cutAt; };
        std::vector<Place> places;
        std::vector<int> last ((size_t) numStrings, -1);
        std::mt19937 rng (seed);
        auto uni = [&] (double a, double b) { return std::uniform_real_distribution<double> (a, b) (rng); };
        auto pick = [&] (int n) { return n <= 0 ? 0 : (int) (rng() % (unsigned) n); };
        auto play = [&] (int s, int fret, double t, double gain)
        {
            if (s < 0 || s >= numStrings || audio.find (s, fret) == nullptr) return;
            if (last[(size_t) s] >= 0) places[(size_t) last[(size_t) s]].cutAt = std::min (places[(size_t) last[(size_t) s]].cutAt, t + 0.015);
            last[(size_t) s] = (int) places.size();
            places.push_back ({ s, fret, t, gain, 1.0e9 });
        };
        auto muteAll = [&] (double t)
        {
            for (int s = 0; s < numStrings; ++s)
                if (last[(size_t) s] >= 0) places[(size_t) last[(size_t) s]].cutAt = std::min (places[(size_t) last[(size_t) s]].cutAt, t);
        };

        double t = 0.5;
        while (t < seconds && ! shapes.empty())
        {
            const double kind = uni (0, 1);
            auto frets = shapes[(size_t) pick ((int) shapes.size())].frets;
            frets.resize ((size_t) numStrings, -1);
            int high = 0;
            for (int f : frets) high = std::max (high, f);
            const int shift = pick (std::max (1, top - high + 1));
            for (auto& f : frets) if (f >= 0) f += shift;
            // Sometimes only two or three neighbouring strings of it (double stops, power chords).
            if (uni (0, 1) < 0.3)
            {
                std::vector<int> used;
                for (int s = 0; s < numStrings; ++s) if (frets[(size_t) s] >= 0) used.push_back (s);
                if (used.size() > 2)
                {
                    const int keep = 2 + pick (2), from = pick ((int) used.size() - keep + 1);
                    for (int k = 0; k < (int) used.size(); ++k)
                        if (k < from || k >= from + keep) frets[(size_t) used[(size_t) k]] = -1;
                }
            }
            const double level = uni (0.3, 1.0);
            if (kind < 0.5) // strum, down (low to high) or up
            {
                if (uni (0, 1) < 0.6) muteAll (t);
                const bool down = uni (0, 1) < 0.7;
                const double gap = uni (0.004, 0.03);
                int k = 0;
                for (int j = 0; j < numStrings; ++j)
                {
                    const int s = down ? numStrings - 1 - j : j; // string index 0 = highest
                    if (frets[(size_t) s] < 0 || uni (0, 1) < 0.1) continue;
                    play (s, frets[(size_t) s], t + gap * k++, level * uni (0.5, 1.0));
                }
                t += gap * k + uni (0.4, 1.2);
            }
            else if (kind < 0.75) // arpeggio, the notes ring on
            {
                if (uni (0, 1) < 0.5) muteAll (t);
                const double gap = uni (0.1, 0.35);
                std::vector<int> order;
                for (int s = numStrings - 1; s >= 0; --s) if (frets[(size_t) s] >= 0) order.push_back (s);
                const int how = pick (3);
                if (how == 1) std::reverse (order.begin(), order.end());
                if (how == 2) std::shuffle (order.begin(), order.end(), rng);
                for (size_t k = 0; k < order.size(); ++k) play (order[k], frets[(size_t) order[k]], t + gap * (double) k, level * uni (0.5, 1.0));
                t += gap * (double) order.size() + uni (0.2, 0.8);
            }
            else // a melody near one hand position; sometimes each note stops the one before
            {
                const bool ring = uni (0, 1) < 0.4;
                const int hand = pick (std::max (1, top - 3));
                const int count = 3 + pick (6);
                int prev = -1;
                for (int k = 0; k < count; ++k)
                {
                    const int s = pick (numStrings);
                    if (! ring && prev >= 0 && prev != s && last[(size_t) prev] >= 0)
                        places[(size_t) last[(size_t) prev]].cutAt = std::min (places[(size_t) last[(size_t) prev]].cutAt, t);
                    play (s, hand + pick (5), t, level * uni (0.6, 1.0));
                    prev = s;
                    t += uni (0.1, 0.4);
                }
                t += uni (0.2, 0.6);
            }
        }

        LearnMix mix;
        mix.rate = audio.rate;
        mix.audio.assign ((size_t) ((t + 2.0) * audio.rate), 0.0f);
        const double pickAt = 0.02; // recordings start ~20 ms before the pick (findPick)
        for (const auto& p : places)
        {
            const auto& g = audio.find (p.string, p.fret)->samples;
            const size_t at = (size_t) (p.t * audio.rate);
            const size_t len = std::min (g.size(), (size_t) std::max (0.0, (p.cutAt - p.t) * audio.rate));
            const size_t fade = (size_t) (0.005 * audio.rate);
            for (size_t k = 0; k < len && at + k < mix.audio.size(); ++k)
            {
                float v = g[k] * (float) p.gain;
                if (len < g.size() && k + fade > len) v *= (float) (len - k) / (float) fade;
                mix.audio[at + k] += v;
            }
            MixNote n;
            n.onset = p.t + pickAt;
            n.offset = std::min (p.t + (double) g.size() / audio.rate, p.cutAt);
            n.pitch = inst.tuning()[(size_t) p.string] + p.fret;
            n.string = p.string;
            n.fret = p.fret;
            if (n.offset > n.onset + 0.03) mix.truth.push_back (n);
        }
        std::sort (mix.truth.begin(), mix.truth.end(), [] (const MixNote& a, const MixNote& b) { return a.onset < b.onset; });
        return mix;
    }

    std::vector<LearnMix> shapeMixes (const CalibrationAudio& audio, const Instrument& inst)
    {
        std::vector<LearnMix> out;
        for (const auto& sh : audio.shapes)
        {
            // Every pick: loudness jumps to twice what it was in the 20 ms before, at least 150 ms after the last one.
            const auto& x = sh.samples;
            const int block = std::max (1, (int) (audio.rate * 0.005));
            const int numBlocks = (int) x.size() / block;
            std::vector<float> env ((size_t) std::max (0, numBlocks), 0.0f);
            float loudest = 0.0f;
            for (int b = 0; b < numBlocks; ++b)
            {
                double sq = 0.0;
                for (int i = b * block; i < (b + 1) * block; ++i) sq += (double) x[(size_t) i] * x[(size_t) i];
                env[(size_t) b] = (float) std::sqrt (sq / block);
                loudest = std::max (loudest, env[(size_t) b]);
            }
            std::vector<double> picks;
            int lastPick = -1000;
            for (int b = 1; b < numBlocks; ++b)
            {
                float before = 1.0e-6f;
                for (int k = std::max (0, b - 4); k < b; ++k) before = std::max (before, env[(size_t) k]);
                if (env[(size_t) b] > 0.2f * loudest && env[(size_t) b] > 2.0f * before && b - lastPick >= 30)
                {
                    picks.push_back ((double) (b * block) / audio.rate);
                    lastPick = b;
                }
            }
            if (picks.empty()) continue;

            LearnMix m;
            m.audio = x;
            m.rate = audio.rate;
            const double end = (double) x.size() / audio.rate;
            for (size_t k = 0; k < picks.size(); ++k)
                for (int s = 0; s < (int) sh.frets.size() && s < inst.numStrings(); ++s)
                    if (sh.frets[(size_t) s] >= 0)
                        m.truth.push_back ({ picks[k], k + 1 < picks.size() ? picks[k + 1] : end,
                                             inst.tuning()[(size_t) s] + sh.frets[(size_t) s], s, sh.frets[(size_t) s] });
            std::sort (m.truth.begin(), m.truth.end(), [] (const MixNote& a, const MixNote& b) { return a.onset < b.onset; });
            out.push_back (std::move (m));
        }
        return out;
    }

    namespace
    {
        struct Example { std::array<float, PolyTracker::kNumNoteFeatures> f {}; bool real = false; float weight = 1.0f; };
        struct Sent { double time; int pitch; };

        // Runs Poly over a mix. With `examples`, keeps what it knew about each possible new note, labelled by the mix.
        std::vector<Sent> listen (const LearnMix& mix, const Instrument& inst, const Calibration& cal,
                                  std::vector<Example>* examples, std::mt19937* rng, double keepFakes,
                                  const std::function<bool (double)>& progress, double from, double to, bool& stopped,
                                  int stableFrames = 3, int waitFrames = 0)
        {
            TrackerSettings ts;
            ts.mode = TrackMode::Poly;
            ts.monoInput = 0;
            ts.stableFrames = stableFrames;
            ts.waitFrames = waitFrames;
            GuitarTracker tracker;
            tracker.prepare (mix.rate, 512, inst, cal, ts);
            double now = 0.0;
            if (examples != nullptr)
                tracker.setNoteFeatureSink ([&] (int midi, const float* f, int count, bool)
                {
                    const double t = now + 0.006;
                    bool fresh = false, sounding = false;
                    for (const auto& note : mix.truth)
                    {
                        if (note.onset > t + 0.2) break;
                        if (note.pitch != midi) continue;
                        if (note.onset >= t - 0.15 && note.onset <= t + 0.01) fresh = true;
                        if (t >= note.onset - 0.03 && t <= note.offset + 0.1) sounding = true;
                    }
                    if (! fresh && sounding) return; // a ringing note: not a decision about a new note
                    if (! fresh && std::uniform_real_distribution<double> (0.0, 1.0) (*rng) > keepFakes) return;
                    Example e;
                    std::copy (f, f + std::min (count, (int) e.f.size()), e.f.begin());
                    e.real = fresh;
                    e.weight = fresh ? 1.0f : (float) (1.0 / keepFakes);
                    examples->push_back (e);
                });

            std::vector<Sent> sent;
            const size_t total = mix.audio.size();
            for (size_t pos = 0; pos < total && ! stopped; pos += 512)
            {
                const int n = (int) std::min<size_t> (512, total - pos);
                const float* ch[1] = { mix.audio.data() + pos };
                tracker.process (ch, 1, n,
                                 [&] (int off, uint8_t st, uint8_t d1, uint8_t d2)
                                 {
                                     if ((st & 0xF0) == 0x90 && d2 > 0) sent.push_back ({ (double) (pos + (size_t) off) / mix.rate, (int) d1 });
                                 },
                                 [] (const AnalysisFrame&) {});
                now = (double) (pos + (size_t) n) / mix.rate;
                if (progress && (pos / 512) % 200 == 0 && ! progress (from + (to - from) * (double) pos / (double) total))
                    stopped = true;
            }
            return sent;
        }

        // Found minus wrong notes, per note played (a note counts when sent 50 ms early to 200 ms late).
        double value (const LearnMix& mix, const std::vector<Sent>& sent)
        {
            std::vector<bool> used (sent.size(), false);
            int found = 0;
            for (const auto& t : mix.truth)
            {
                int best = -1;
                for (size_t k = 0; k < sent.size(); ++k)
                {
                    const double d = sent[k].time - t.onset;
                    if (used[k] || sent[k].pitch != t.pitch || d < -0.05 || d > 0.2) continue;
                    if (best < 0 || std::abs (d) < std::abs (sent[(size_t) best].time - t.onset)) best = (int) k;
                }
                if (best >= 0) { used[(size_t) best] = true; ++found; }
            }
            const int wrong = (int) std::count (used.begin(), used.end(), false);
            return (double) (found - wrong) / (double) std::max<size_t> (1, mix.truth.size());
        }

        // One hidden layer (tanh), sigmoid output, weighted cross-entropy, Adam.
        NoteNet train (const std::vector<Example>& data, const LearnSettings& ls, unsigned seed,
                       const std::function<bool (double)>& progress, double from, double to, bool& stopped)
        {
            const int n = PolyTracker::kNumNoteFeatures, hidden = ls.hidden;
            NoteNet net;
            if (data.empty()) return net;
            net.inputs = n;
            net.hidden = hidden;
            net.mean.assign ((size_t) n, 0.0f);
            net.scale.assign ((size_t) n, 1.0f);
            for (int k = 0; k < n; ++k)
            {
                double sum = 0.0, sq = 0.0;
                for (const auto& d : data) { sum += d.f[(size_t) k]; sq += (double) d.f[(size_t) k] * d.f[(size_t) k]; }
                const double mu = sum / (double) data.size();
                net.mean[(size_t) k] = (float) mu;
                net.scale[(size_t) k] = (float) std::max (1.0e-3, std::sqrt (std::max (0.0, sq / (double) data.size() - mu * mu)));
            }

            std::mt19937 rng (seed);
            std::normal_distribution<double> gauss (0.0, 1.0);
            const size_t nw1 = (size_t) (hidden * n);
            std::vector<double> w (nw1 + (size_t) (2 * hidden) + 1, 0.0); // w1, b1, w2, b2
            for (size_t i = 0; i < nw1; ++i) w[i] = gauss (rng) / std::sqrt ((double) n);
            for (int j = 0; j < hidden; ++j) w[nw1 + (size_t) hidden + (size_t) j] = gauss (rng) / std::sqrt ((double) hidden);
            std::vector<double> m1 (w.size(), 0.0), v1 (w.size(), 0.0), g (w.size(), 0.0), z ((size_t) n), h ((size_t) hidden);
            std::vector<size_t> order (data.size());
            std::iota (order.begin(), order.end(), 0);
            const double lr = 0.003, beta1 = 0.9, beta2 = 0.999;
            long long step = 0;
            constexpr size_t batch = 64;
            for (int e = 0; e < ls.epochs && ! stopped; ++e)
            {
                std::shuffle (order.begin(), order.end(), rng);
                for (size_t start = 0; start < order.size(); start += batch)
                {
                    std::fill (g.begin(), g.end(), 0.0);
                    const size_t end = std::min (order.size(), start + batch);
                    double weights = 0.0;
                    for (size_t oi = start; oi < end; ++oi)
                    {
                        const auto& d = data[order[oi]];
                        for (int k = 0; k < n; ++k) z[(size_t) k] = (d.f[(size_t) k] - net.mean[(size_t) k]) / net.scale[(size_t) k];
                        double out = w.back();
                        for (int j = 0; j < hidden; ++j)
                        {
                            double a = w[nw1 + (size_t) j];
                            for (int k = 0; k < n; ++k) a += w[(size_t) (j * n + k)] * z[(size_t) k];
                            h[(size_t) j] = std::tanh (a);
                            out += w[nw1 + (size_t) hidden + (size_t) j] * h[(size_t) j];
                        }
                        const double pr = 1.0 / (1.0 + std::exp (-out));
                        const double dout = d.weight * (pr - (d.real ? 1.0 : 0.0));
                        weights += d.weight;
                        g.back() += dout;
                        for (int j = 0; j < hidden; ++j)
                        {
                            g[nw1 + (size_t) hidden + (size_t) j] += dout * h[(size_t) j];
                            const double dh = dout * w[nw1 + (size_t) hidden + (size_t) j] * (1.0 - h[(size_t) j] * h[(size_t) j]);
                            g[nw1 + (size_t) j] += dh;
                            for (int k = 0; k < n; ++k) g[(size_t) (j * n + k)] += dh * z[(size_t) k];
                        }
                    }
                    ++step;
                    for (size_t i = 0; i < w.size(); ++i)
                    {
                        const double gi = g[i] / weights + (i < nw1 ? 1.0e-4 * w[i] : 0.0);
                        m1[i] = beta1 * m1[i] + (1 - beta1) * gi;
                        v1[i] = beta2 * v1[i] + (1 - beta2) * gi * gi;
                        const double mh = m1[i] / (1 - std::pow (beta1, (double) step)), vh = v1[i] / (1 - std::pow (beta2, (double) step));
                        w[i] -= lr * mh / (std::sqrt (vh) + 1.0e-8);
                    }
                }
                if (progress && ! progress (from + (to - from) * (e + 1) / ls.epochs)) stopped = true;
            }
            net.w1.assign (w.begin(), w.begin() + (std::ptrdiff_t) nw1);
            net.b1.assign (w.begin() + (std::ptrdiff_t) nw1, w.begin() + (std::ptrdiff_t) (nw1 + (size_t) hidden));
            net.w2.assign (w.begin() + (std::ptrdiff_t) (nw1 + (size_t) hidden), w.begin() + (std::ptrdiff_t) (nw1 + (size_t) (2 * hidden)));
            net.b2 = (float) w.back();
            return net;
        }
    }

    NoteNet learnNoteNet (const CalibrationAudio& audio, const Instrument& inst, const Calibration& cal,
                          const std::function<bool (double)>& progress, const LearnSettings& ls,
                          const std::vector<LearnMix>& extraPlaying)
    {
        auto playing = shapeMixes (audio, inst);
        playing.insert (playing.end(), extraPlaying.begin(), extraPlaying.end());
        // Too few recorded frets to make chords from: no scorer (Poly's rules decide).
        if (audio.rate <= 0.0 || audio.notes.size() < 24)
            return {};

        Calibration rules = cal;
        rules.net = {};
        bool stopped = false;
        std::mt19937 rng (ls.seed);
        std::vector<Example> data;

        // Round 1: what Poly knows while its rules decide. Round 2: while the first scorer decides (its own mistakes).
        const int sf = ls.stableFrames, wf = ls.waitFrames;
        listen (makeLearnMix (audio, inst, ls.seed + 100, ls.mixSeconds), inst, rules, &data, &rng, ls.keepFakes, progress, 0.0, 0.25, stopped, sf, wf);
        for (const auto& p : playing)
            if (! stopped) listen (p, inst, rules, &data, &rng, ls.keepFakes, {}, 0.0, 0.0, stopped, sf, wf);
        auto net = train (data, ls, ls.seed + 7, progress, 0.25, 0.35, stopped);
        Calibration withNet = cal;
        withNet.net = net;
        if (! stopped && net.usable())
        {
            listen (makeLearnMix (audio, inst, ls.seed + 200, ls.mixSeconds), inst, withNet, &data, &rng, ls.keepFakes, progress, 0.35, 0.6, stopped, sf, wf);
            for (const auto& p : playing)
                if (! stopped) listen (p, inst, withNet, &data, &rng, ls.keepFakes, {}, 0.0, 0.0, stopped, sf, wf);
        }
        if (! stopped)
            net = train (data, ls, ls.seed + 17, progress, 0.6, 0.75, stopped);

        // Strictness: the best found-minus-wrong on another mix, then a little less strict ("balanced").
        const auto check = makeLearnMix (audio, inst, ls.seed + 300, ls.checkSeconds);
        const double offsets[] = { -3.0, -2.0, -1.0, 0.0, 1.0, 2.0 };
        double best = -1.0e9, bestOffset = 0.0;
        for (size_t i = 0; i < std::size (offsets) && ! stopped; ++i)
        {
            withNet.net = net;
            withNet.net.offset = (float) offsets[i];
            const double from = 0.75 + 0.25 * (double) i / std::size (offsets), to = 0.75 + 0.25 * (double) (i + 1) / std::size (offsets);
            const double v = value (check, listen (check, inst, withNet, nullptr, nullptr, 0.0, progress, from, to, stopped, sf, wf));
            if (v > best) { best = v; bestOffset = offsets[i]; }
        }
        if (stopped || ! net.usable()) return {};
        net.offset = (float) (bestOffset + ls.balance);
        if (progress) progress (1.0);
        return net;
    }
}
