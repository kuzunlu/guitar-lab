#include "Tracking/PolyTracker.h"

#include <algorithm>
#include <cmath>

namespace gl
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kInharmonicity = 5.0e-5; // steel strings: upper harmonics are a little sharp
        constexpr int    kNmfIterations = 15;
        constexpr float  kSparsity = 0.04f;        // pushes small, unneeded activations to zero
        // Spectra are compared after a square root. Then weak harmonics still count: a low note whose odd harmonics
        // are quiet (it depends on where the string is picked) is not mistaken for the octave above.
        constexpr float  kCompress = 0.5f;
    }

    void PolyTracker::prepare (double sampleRate, const Instrument& i, const Calibration& c, const Settings& s)
    {
        prepared = false;
        inst = i;
        cal = c;
        settings = s;
        settings.stableFrames = std::clamp (settings.stableFrames, 1, 10);
        settings.sensitivity = std::clamp (settings.sensitivity, 0.25, 4.0);

        inputRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        decimation = std::max (1, (int) std::lround (inputRate / 12000.0));
        rate = inputRate / decimation;
        lp1.setLowPass (inputRate, rate * 0.42);
        lp2.setLowPass (inputRate, rate * 0.42);
        dcCoeff = (float) std::exp (-2.0 * kPi * 30.0 / rate);

        ring.assign ((size_t) kLong, 0.0f);
        frame.assign ((size_t) kLong, 0.0f);
        longSpec.prepare (kLong, kLong / 8);
        shortSpec.prepare (kShort);
        prevShort.assign ((size_t) kShort / 2, 0.0f);
        for (auto& h : shortHist) h.assign ((size_t) kShort / 2, 0.0f);
        shortPos = 0;

        // Pitches this guitar can play: lowest open string to the last fret of each string.
        int lo = 127, hi = 0;
        for (int str = 0; str < inst.numStrings(); ++str)
        {
            const int open = inst.tuning()[(size_t) str];
            lo = std::min (lo, open);
            hi = std::max (hi, open + std::clamp (cal.lastFret[(size_t) str], 0, kNumFrets));
        }
        lowestPitch = lo;
        numPitches = std::max (1, hi - lo + 1);

        // Log-frequency bins, centred on this guitar's tuning (A = 432 etc. after calibration).
        const double tune = cal.valid ? cal.globalCents / 100.0 : 0.0;
        const double binHz = rate / kLong;
        midiLo = lo - 1.0;
        numBins = std::max (1, (int) ((hzToMidi (rate * 0.45) - midiLo) * kBinsPerSemitone));
        binLo.assign ((size_t) numBins, 0);
        binHi.assign ((size_t) numBins, 0);
        binPos.assign ((size_t) numBins, 0.0f);
        for (int b = 0; b < numBins; ++b)
        {
            const double f = midiToHz (midiLo + (double) b / kBinsPerSemitone + tune);
            binPos[(size_t) b] = (float) (f / binHz);
            binLo[(size_t) b] = std::max (1, (int) std::ceil (f * std::pow (2.0, -1.0 / 72.0) / binHz));
            binHi[(size_t) b] = std::min (kLong / 2 - 2, (int) std::floor (f * std::pow (2.0, 1.0 / 72.0) / binHz));
        }
        V.assign ((size_t) numBins, 0.0f);
        WX.assign ((size_t) numBins, 0.0f);
        ratio.assign ((size_t) numBins, 0.0f);

        // How one pure tone spreads over nearby FFT bins with this window (|W(delta)|, delta in bins).
        constexpr int steps = 16, span = 8, centre = span * steps;
        std::vector<float> table ((size_t) (2 * centre + 1), 0.0f);
        const auto& w = longSpec.windowShape();
        for (int j = centre; j <= 2 * centre; ++j)
        {
            const double delta = (double) (j - centre) / steps;
            double re = 0.0, im = 0.0;
            for (int n = 0; n < kLong; ++n)
            {
                const double a = 2.0 * kPi * delta * n / kLong;
                re += w[(size_t) n] * std::cos (a);
                im -= w[(size_t) n] * std::sin (a);
            }
            table[(size_t) j] = table[(size_t) (2 * centre - j)] = (float) std::sqrt (re * re + im * im);
        }

        // Fingerprint: the harmonics of a note, blurred the same way a real note is, in log bins.
        std::vector<float> lin ((size_t) kLong / 2), logv ((size_t) numBins), blurred ((size_t) numBins);
        auto makeTemplate = [&] (double f0, const std::array<float, kMaxHarmonics + 1>& amps)
        {
            std::fill (lin.begin(), lin.end(), 0.0f);
            for (int h = 1; h <= kMaxHarmonics; ++h)
            {
                const double fh = h * f0 * std::sqrt (1.0 + kInharmonicity * h * h);
                if (fh > rate * 0.45) break;
                const double c0 = fh / binHz;
                const int k0 = std::max (0, (int) std::ceil (c0 - span));
                const int k1 = std::min (kLong / 2 - 1, (int) std::floor (c0 + span));
                for (int k = k0; k <= k1; ++k)
                {
                    const double d = (k - c0) * steps + centre;
                    const int j = std::clamp ((int) d, 0, 2 * centre - 1);
                    const double t = d - j;
                    lin[(size_t) k] += amps[(size_t) h] * (float) (table[(size_t) j] * (1.0 - t) + table[(size_t) j + 1] * t);
                }
            }

            toLog (lin, logv);
            for (auto& v : logv) v = std::pow (v, kCompress);
            // A little blur: real notes are a few cents off, and the harmonics of a plucked string drift.
            float sum = 0.0f, peak = 0.0f;
            for (int b = 0; b < numBins; ++b)
            {
                const float l = b > 0 ? logv[(size_t) b - 1] : 0.0f;
                const float r = b + 1 < numBins ? logv[(size_t) b + 1] : 0.0f;
                blurred[(size_t) b] = logv[(size_t) b] + 0.5f * (l + r);
                sum += blurred[(size_t) b];
                peak = std::max (peak, blurred[(size_t) b]);
            }
            std::vector<Entry> t;
            for (int b = 0; b < numBins; ++b)
                if (sum > 0.0f && blurred[(size_t) b] > peak * 1.0e-3f)
                    t.push_back ({ b, blurred[(size_t) b] / sum });
            return t;
        };

        // One fingerprint per pitch. After a full calibration that heard the attacks: one per string and fret instead,
        // with that spot's real sound at the start of a note and its real pitch. Then a note on the low E sounds
        // different from the same note on the G string, and a string's overtones are explained by the string itself.
        templates.clear();
        templatePitch.clear();
        pitchTemplates.assign ((size_t) numPitches, {});
        for (int p = 0; p < numPitches; ++p)
        {
            const int midi = lowestPitch + p;
            for (int str = 0; str < inst.numStrings(); ++str)
            {
                const int fret = midi - inst.tuning()[(size_t) str];
                const auto* pos = fret <= cal.lastFret[(size_t) str] ? cal.position (str, fret) : nullptr;
                if (pos == nullptr || ! pos->hasAttack) continue;

                std::array<float, kMaxHarmonics + 1> amps {};
                amps[1] = 1.0f;
                for (int k = 0; k < kOvertones; ++k)
                    amps[(size_t) k + 2] = std::pow (2.0f, pos->attack[(size_t) k]);
                for (int h = kOvertones + 2; h <= kMaxHarmonics; ++h)
                    amps[(size_t) h] = amps[kOvertones + 1] * std::pow ((float) (kOvertones + 1) / h, 1.2f);

                pitchTemplates[(size_t) p].push_back ((int) templates.size());
                templates.push_back (makeTemplate (midiToHz (midi + pos->cents / 100.0), amps));
                templatePitch.push_back (p);
            }
            if (pitchTemplates[(size_t) p].empty())
            {
                pitchTemplates[(size_t) p].push_back ((int) templates.size());
                templates.push_back (makeTemplate (midiToHz (midi + tune), harmonicProfile (midi)));
                templatePitch.push_back (p);
            }
        }

        dense.assign (templates.size() * (size_t) numBins, 0.0f);
        for (size_t q = 0; q < templates.size(); ++q)
            for (const auto& e : templates[q])
                dense[q * (size_t) numBins + (size_t) e.bin] = e.w;

        // Broadband noise (pick attack, string noise): soaks up energy that is not a note.
        templates.emplace_back();
        templatePitch.push_back (numPitches);
        for (int b = 0; b < numBins; ++b)
            templates.back().push_back ({ b, 1.0f / numBins });

        buildShapes();
        x.assign ((size_t) numPitches + 1, 0.0f);
        act.assign (templates.size(), 0.0f);
        history.assign ((size_t) numPitches, {});
        candidate.assign ((size_t) numPitches, 0);
        onsetX.assign ((size_t) numPitches, 0.0f);
        notes.assign ((size_t) numPitches, {});
        prepared = true;
        reset();
    }

    void PolyTracker::buildShapes()
    {
        // Fret patterns from the lowest string up ('x' = not played). Movable ones are also used at every fret
        // up to 12 (fret 0 = the open chord). In other tunings they simply match less often.
        static const char* movable[] = {
            "022100", "022000", "020100", "020000", "021100", "022200", "022xxx", "02xxxx", // E shapes: maj, m, 7, m7, maj7, sus4, 5
            "x02220", "x02210", "x02020", "x02010", "x02120", "x02200", "x02230", "x022xx", "x02xxx", // A shapes
            "xx0232", "xx0231", "xx0212", "xx0211", "xx0222", "xx0230", "xx0233", "xx023x"            // D shapes
        };
        static const char* open[] = { "x32010", "x32310", "x32000", "320003", "320033", "320001", "x21202",
                                      "022033", "xx3210", "133211", "x24432", "x13321" };

        shapes.clear();
        const int n = inst.numStrings();
        auto add = [&] (const char* pattern, int barre)
        {
            const int len = (int) std::char_traits<char>::length (pattern);
            for (int offset = 0; offset + len <= n; ++offset) // 7/8 strings: every group of adjacent strings
            {
                Shape sh;
                sh.note.fill (-1);
                sh.bass = 999;
                bool ok = true;
                for (int i = 0; i < len && ok; ++i)
                {
                    if (pattern[i] == 'x') continue;
                    const int string = n - 1 - offset - i; // pattern goes low string -> high string
                    const int fret = pattern[i] - '0' + barre;
                    ok = fret <= cal.lastFret[(size_t) string];
                    sh.note[(size_t) string] = inst.tuning()[(size_t) string] + fret;
                    sh.bass = std::min (sh.bass, sh.note[(size_t) string]);
                    ++sh.size;
                }
                if (ok && sh.size >= 2) shapes.push_back (sh);
            }
        };
        for (const char* m : movable)
            for (int barre = 0; barre <= 12; ++barre)
                add (m, barre);
        for (const char* o : open)
            add (o, 0);

        // Chords taught in calibration (any tuning).
        for (const auto& c : cal.chords)
        {
            Shape sh;
            sh.note.fill (-1);
            sh.bass = 999;
            for (int s = 0; s < n && s < (int) c.frets.size(); ++s)
                if (c.frets[(size_t) s] >= 0)
                {
                    sh.note[(size_t) s] = inst.tuning()[(size_t) s] + c.frets[(size_t) s];
                    sh.bass = std::min (sh.bass, sh.note[(size_t) s]);
                    ++sh.size;
                }
            if (sh.size >= 2) shapes.push_back (sh);
        }
    }

    void PolyTracker::completeChord()
    {
        // Notes an octave above a note that already sounds hide inside its harmonics. If the notes heard fit a
        // known shape exactly (same bass, nothing outside it, only such octave copies missing), add the missing
        // strings and put every note on the shape's string.
        std::array<bool, 128> heard {};
        int lowest = 999, numHeard = 0;
        unsigned classes = 0;
        for (int p = 0; p < numPitches; ++p)
            if (notes[(size_t) p].on)
            {
                const int midi = lowestPitch + p;
                heard[(size_t) midi] = true;
                lowest = std::min (lowest, midi);
                classes |= 1u << (midi % 12);
                ++numHeard;
            }
        if (numHeard < 3) return;

        // The fitting shape that needs the fewest changes (notes moved to another string count double).
        const Shape* best = nullptr;
        int bestScore = 1 << 30;
        for (const auto& sh : shapes)
        {
            if (sh.bass != lowest || sh.size < numHeard) continue;
            int inside = 0, added = 0, moved = 0;
            bool fits = true;
            for (int s = 0; s < inst.numStrings() && fits; ++s)
            {
                const int note = sh.note[(size_t) s];
                if (note < 0) continue;
                if (heard[(size_t) note])
                {
                    ++inside;
                    if (notes[(size_t) (note - lowestPitch)].string != s) ++moved;
                }
                else
                {
                    fits = ((classes >> (note % 12)) & 1u) != 0; // missing: must be an octave copy
                    ++added;
                }
            }
            const int score = added + 2 * moved;
            if (fits && inside == numHeard && score < bestScore) { best = &sh; bestScore = score; }
        }
        // Nothing to add when the notes heard already are a whole shape on their strings. When the pitches fit a shape
        // exactly, its strings are a better guess than ours, so notes may move to them.
        if (best == nullptr || bestScore == 0) return;

        // Notes on the wrong string (or on a string the shape does not use) stop; then every shape note starts
        // on its own string.
        for (int q = 0; q < numPitches; ++q)
            if (notes[(size_t) q].on && best->note[(size_t) notes[(size_t) q].string] != lowestPitch + q)
                stop (q);

        float strongest = 0.0f;
        for (int p = 0; p < numPitches; ++p) strongest = std::max (strongest, x[(size_t) p]);
        for (int s = 0; s < inst.numStrings(); ++s)
        {
            const int note = best->note[(size_t) s];
            const int p = note - lowestPitch;
            if (note < 0 || p < 0 || p >= numPitches || notes[(size_t) p].on) continue;
            const int fret = note - inst.tuning()[(size_t) s];
            if (bend[(size_t) s] != 8192) sendBend (s, 8192);
            send ((uint8_t) (0x90 | (channelOf (s) - 1)), (uint8_t) note, (uint8_t) velocityFor (p, s, fret, false));
            // A hidden octave note has little activation of its own: let it live as long as the chord does.
            notes[(size_t) p] = { true, s, fret, std::max (x[(size_t) p], 0.3f * strongest), 0, 0.0, 7, true };
            candidate[(size_t) p] = 0;
        }
    }

    void PolyTracker::reset()
    {
        hintWrite = hintRead = 0;
        samplesIn = 0;
        clearNote = -1;
        clearRun = 0;
        lp1.reset();
        lp2.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (prevShort.begin(), prevShort.end(), 0.0f);
        for (auto& h : shortHist) std::fill (h.begin(), h.end(), 0.0f);
        std::fill (x.begin(), x.end(), 0.0f);
        std::fill (act.begin(), act.end(), 0.0f);
        for (auto& h : history) h.fill (0.0f);
        std::fill (candidate.begin(), candidate.end(), 0);
        for (auto& n : notes) n = {};
        bend.fill (8192);
        decimCounter = hopCounter = writePos = historyPos = 0;
        hopPeak = 0.0f;
        hopSumSq = 0.0;
        dcIn = dcOut = 0.0f;
        envelope = attackPeak = fluxAverage = level = levelPeak = 0.0f;
        sinceOnset = sinceBurst = 100;
        startedInBurst = startedSinceOnset = 0;
        repickPending = false;
        std::fill (onsetX.begin(), onsetX.end(), 0.0f);
        handFret = 5;
        outCount = 0;
    }

    std::array<float, PolyTracker::kMaxHarmonics + 1> PolyTracker::harmonicProfile (int midi) const
    {
        // Typical guitar: harmonics get weaker slowly. (The calibration's tone colour is NOT used here: it is measured
        // after the attack, and made Poly worse on real guitars. Calibrated fingerprints come from the attack instead.)
        std::array<float, kMaxHarmonics + 1> a {};
        for (int h = 1; h <= kMaxHarmonics; ++h)
            a[(size_t) h] = 1.0f / std::pow ((float) h, 1.3f);
        (void) midi;
        return a;
    }

    void PolyTracker::toLog (const std::vector<float>& lin, std::vector<float>& out) const
    {
        const int size = (int) lin.size();
        for (int b = 0; b < numBins; ++b)
        {
            // Low notes: log bins are narrower than FFT bins, so interpolate. High notes: take the strongest FFT bin inside.
            const float pos = binPos[(size_t) b];
            const int k = (int) pos;
            const float t = pos - (float) k;
            float v = k + 1 < size ? lin[(size_t) k] * (1.0f - t) + lin[(size_t) k + 1] * t : 0.0f;
            for (int j = binLo[(size_t) b]; j <= binHi[(size_t) b]; ++j)
                v = std::max (v, lin[(size_t) j]);
            out[(size_t) b] = v;
        }
    }

    // -----------------------------------------------------------------------
    void PolyTracker::analyse()
    {
        while (hintRead < hintWrite && hints[(size_t) (hintRead % (int) hints.size())].sample <= samplesIn)
        {
            const auto& h = hints[(size_t) (hintRead++ % (int) hints.size())];
            const int n = h.midi > 0.0f && h.clarity > 0.9f ? (int) std::lround (h.midi) : -1;
            clearRun = n >= 0 && n == clearNote ? clearRun + 1 : (n >= 0 ? 1 : 0);
            clearNote = n;
        }
        if (hintRead == hintWrite)
            hintRead = hintWrite = 0;

        level = (float) std::sqrt (hopSumSq / kHop);
        levelPeak = hopPeak;
        hopPeak = 0.0f;
        hopSumSq = 0.0;

        for (int i = 0; i < kLong; ++i)
            frame[(size_t) i] = ring[(size_t) ((writePos + i) & (kLong - 1))];
        longSpec.compute (frame.data());
        shortSpec.compute (frame.data() + kLong - kShort);
        toLog (longSpec.magnitudes(), V);
        for (auto& v : V) v = std::pow (v, kCompress);

        // Pick attacks: a jump in loudness, or new energy across the short spectrum ("spectral flux").
        const auto& sm = shortSpec.magnitudes();
        float rise = 0.0f, sum = 0.0f;
        for (size_t k = 1; k < sm.size(); ++k)
        {
            const float now = std::sqrt (sm[k]), before = std::sqrt (prevShort[k]);
            if (now > before) rise += now - before;
            sum += now;
            prevShort[k] = sm[k];
        }
        const float flux = rise / (sum + 1.0e-9f);
        {
            auto& sh = shortHist[(size_t) shortPos]; // for the learned scorer: when did each overtone start?
            for (size_t k = 0; k < sh.size() && k < sm.size(); ++k) sh[k] = std::sqrt (sm[k]);
            shortPos = (shortPos + 1) % 4;
        }

        const float gateOn = cal.gateOn() / (float) settings.sensitivity;
        const bool pluck = levelPeak > gateOn && levelPeak > envelope * 1.6f + gateOn * 0.5f;
        envelope = std::max (levelPeak, envelope * 0.87f);
        const bool fluxHit = level > gateOn && flux > 0.12f && flux > fluxAverage * 2.5f;
        fluxAverage = fluxAverage * 0.9f + flux * 0.1f;

        if (pluck || fluxHit)
        {
            if (sinceOnset > 6) { sinceBurst = 0; startedInBurst = 0; } // first attack of a strum (later strings follow within ~60 ms)
            sinceOnset = 0;
            repickPending = true;
            startedSinceOnset = 0;
            for (int q = 0; q < numPitches; ++q) onsetX[(size_t) q] = x[(size_t) q]; // activations just before the attack
            attackPeak = levelPeak;
        }
        else
        {
            sinceOnset = std::min (sinceOnset + 1, 1000);
            sinceBurst = std::min (sinceBurst + 1, 1000);
            if (sinceOnset < 5) attackPeak = std::max (attackPeak, levelPeak);
        }

        solve();
        updateNotes();
    }

    void PolyTracker::solve()
    {
        // Non-negative matrix factorisation with fixed fingerprints (KL divergence, multiplicative updates).
        // Starts from the last frame's answer, so a few rounds are enough.
        float total = 0.0f;
        for (float v : V) total += v;
        const float floor = 1.0e-3f * total / (float) act.size();
        for (auto& a : act) a = std::max (a * 0.85f, floor);

        for (int it = 0; it < kNmfIterations; ++it)
        {
            std::fill (WX.begin(), WX.end(), 1.0e-9f);
            for (size_t p = 0; p < templates.size(); ++p)
            {
                const float a = act[p];
                if (a <= 0.0f) continue;
                for (const auto& e : templates[p]) WX[(size_t) e.bin] += a * e.w;
            }
            for (int b = 0; b < numBins; ++b)
                ratio[(size_t) b] = V[(size_t) b] / WX[(size_t) b];
            for (size_t p = 0; p < templates.size(); ++p)
            {
                float num = 0.0f;
                for (const auto& e : templates[p]) num += e.w * ratio[(size_t) e.bin];
                act[p] *= num / (1.0f + kSparsity); // every fingerprint sums to 1
            }
        }

        // The model with the final activations (used to see what each note explains).
        std::fill (WX.begin(), WX.end(), 1.0e-9f);
        for (size_t p = 0; p < templates.size(); ++p)
            for (const auto& e : templates[p]) WX[(size_t) e.bin] += act[p] * e.w;

        // Per pitch: its fingerprints added up (without attack fingerprints there is exactly one per pitch).
        std::fill (x.begin(), x.end(), 0.0f);
        for (size_t t = 0; t < templates.size(); ++t)
            x[(size_t) templatePitch[t]] += act[t];
    }

    // -----------------------------------------------------------------------
    bool PolyTracker::isGhost (int p, bool picked) const
    {
        const float xp = x[(size_t) p];
        for (int q = 0; q < numPitches; ++q)
        {
            if (q == p || ! (notes[(size_t) q].on || candidate[(size_t) q] > 0))
                continue;
            const float xq = x[(size_t) q];

            switch (p - q)
            {
                // Above a louder note, on one of its harmonics: only a real note if it adds clearly more.
                case 12: case 19: case 24: case 28: case 31: case 34: case 36:
                    if (! picked && xq > xp && xp < (p - q == 12 ? 0.1f : 0.06f) * xq) return true;
                    break;

                // Next to a louder note: spill while its pitch is still blurred.
                case 1: case -1: if (xq > xp && xp < 0.5f * xq) return true; break;
                case 2: case -2: if (xq > xp && xp < 0.3f * xq) return true; break;

                // Below a real note whose harmonics it would include (an octave, octave + 5th, two octaves...):
                // it fits that note's sound too, even when it looks as strong. Believe it only if its own
                // fundamental or 3rd harmonic is really there.
                case -12: case -19: case -24: case -28: case -31: case -34: case -36:
                    if (xq > 0.3f * xp && ! hasOwnFundamental (p, q)) return true;
                    break;

                default: break;
            }
        }
        return false;
    }

    float PolyTracker::spectrumAt (double midi) const
    {
        const int b = (int) std::lround ((midi - midiLo) * kBinsPerSemitone);
        float v = 0.0f;
        for (int k = b - 1; k <= b + 1; ++k)
            if (k >= 0 && k < numBins) v = std::max (v, V[(size_t) k]);
        return v;
    }

    float PolyTracker::ownEnergyAt (int p, double midi) const
    {
        // Spectrum near this pitch minus what all the OTHER notes explain there.
        const int b0 = (int) std::lround ((midi - midiLo) * kBinsPerSemitone);
        float best = 0.0f;
        for (int b = b0 - 1; b <= b0 + 1; ++b)
            if (b >= 0 && b < numBins)
            {
                float own = 0.0f;
                for (int t : pitchTemplates[(size_t) p])
                    own += act[(size_t) t] * dense[(size_t) t * (size_t) numBins + (size_t) b];
                const float others = WX[(size_t) b] - own;
                best = std::max (best, V[(size_t) b] - others);
            }
        return best;
    }

    bool PolyTracker::hasHarmonicPeaks (int p) const
    {
        // A real note shows sharp peaks at its harmonics. The smeared sound right after a pick attack does not.
        // Only harmonics above 140 Hz count: lower, the spectrum is too blurred to show a peak.
        const double m = lowestPitch + p;
        int peaks = 0, checked = 0;
        for (int h = 1; h <= 8 && checked < 5; ++h)
        {
            const double mh = m + 12.0 * std::log2 ((double) h);
            if (midiToHz (mh) < 140.0) continue;
            if (midiToHz (mh) > rate * 0.4) break;
            ++checked;
            const float side = std::max (spectrumAt (mh - 1.5), spectrumAt (mh + 1.5));
            const float v = spectrumAt (mh);
            // ...and the peak must be this note's own: not a harmonic another note already explains.
            if (v > 1.15f * side && ownEnergyAt (p, mh) > 0.3f * v) ++peaks;
        }
        // High notes have few harmonics in range: one clear peak is enough there.
        if (peaks < std::min (midiToHz (m) > 400.0 ? 1 : 2, checked))
            return false;

        // Very low notes: a pick attack spreads energy down there too, and the harmonics above can belong to
        // other notes. Ask for a real peak at the fundamental or the 3rd harmonic (wider sides: blurrier there).
        if (midiToHz (m) < 105.0)
        {
            auto clearPeak = [this, p] (double mm)
            {
                const float v = spectrumAt (mm);
                return v > 1.15f * std::max (spectrumAt (mm - 2.0), spectrumAt (mm + 2.0)) && ownEnergyAt (p, mm) > 0.3f * v;
            };
            const int third = p + 19; // its 3rd harmonic is no proof when that pitch is a note of its own
            const bool thirdIsNote = third < numPitches && (notes[(size_t) third].on || candidate[(size_t) third] > 0);
            return clearPeak (m) || (! thirdIsNote && clearPeak (m + 19.02));
        }
        return true;
    }

    bool PolyTracker::hasOwnFundamental (int p, int above) const
    {
        // Energy at this pitch that the OTHER notes do not explain (their fingerprints include the spill of a
        // loud note nearby, so that spill does not count as proof).
        const double m = lowestPitch + p;
        auto ownAt = [this, p] (double mm) { return ownEnergyAt (p, mm); };
        // The 3rd harmonic is no proof when it IS the note above (an octave + 5th up).
        const float own = above - p == 19 ? ownAt (m) : std::max (ownAt (m), ownAt (m + 19.02));
        float upper = 0.0f;
        for (int h = 2; h <= 8; ++h)
            if (h != 3) upper = std::max (upper, spectrumAt (m + 12.0 * std::log2 ((double) h)));
        return own > 0.4f * upper;
    }

    bool PolyTracker::isClaimed (int p) const
    {
        // A bent note moves over the neighbouring pitches: they belong to it, they are not new notes.
        // The followed pitch lags a fast bend a little, so claim one semitone further in the bend's direction.
        for (int q = 0; q < numPitches; ++q)
        {
            if (q == p || ! notes[(size_t) q].on) continue;
            const double dev = notes[(size_t) q].dev;
            const double lo = dev < -0.3 ? q + dev - 1.0 : q - 0.6;
            const double hi = dev > 0.3 ? q + dev + 1.0 : q + 0.6;
            if (p > lo && p < hi) return true;
        }
        return false;
    }

    bool PolyTracker::overtoneOfClearNote (int p) const
    {
        // The one-note detector clearly hears one note that was just picked: its overtones are not new notes.
        // A clear note that has rung for a while does not count (a real octave picked over it, in an arpeggio).
        if (clearRun < 2) return false;
        const int m = clearNote - lowestPitch;
        if (m < 0 || m >= numPitches) return false;
        const auto& n = notes[(size_t) m];
        if (n.on && (n.age > 8 || n.repicked)) return false;
        const int d = p - m;
        return d == 12 || d == 19 || d == 24 || d == 28 || d == 31;
    }

    // What the learned scorer sees about a possible new note p: the rules' own measures, plus when p's energy started
    // compared with its "parent" (the louder note below it that has p as an overtone), how much more of it there is
    // than this guitar's calibrated parent makes alone, and what the one-note detector hears clearly.
    void PolyTracker::noteFeatures (int p, float shareRel, bool rising, float before, float strongest, float* f) const
    {
        const float xp = std::max (x[(size_t) p], 1.0e-9f);
        const double m = lowestPitch + p;
        auto lg = [] (float v, float lo, float hi) { return std::clamp (std::log2 (std::max (v, 1.0e-9f)), lo, hi); };
        auto harmonicOf = [] (int d) { return d == 12 ? 2 : d == 19 ? 3 : d == 24 ? 4 : d == 28 ? 5 : d == 31 ? 6 : 0; };

        // Strongest louder note below on a harmonic of it (parent), above it as a harmonic (child), next to it.
        int parent = -1, child = -1;
        float parentX = 0.0f, childX = 0.0f, neighbour = 0.0f;
        for (int q = 0; q < numPitches; ++q)
        {
            if (q == p || ! (notes[(size_t) q].on || candidate[(size_t) q] > 0)) continue;
            const float xq = x[(size_t) q];
            const int d = p - q;
            if (harmonicOf (d) > 0 && xq > xp && xq > parentX) { parent = q; parentX = xq; }
            if ((d == -12 || d == -19 || d == -24) && xq > 0.3f * xp && xq > childX) { child = q; childX = xq; }
            if (std::abs (d) <= 2 && xq > xp) neighbour = std::max (neighbour, lg (xq / xp, 0.0f, 6.0f));
        }
        const int clearP = clearNote - lowestPitch;
        if (parent < 0 && clearRun >= 2 && clearP >= 0 && clearP < numPitches && harmonicOf (p - clearP) > 0)
        {
            parent = clearP;
            parentX = std::max (x[(size_t) clearP], 1.0e-9f);
        }
        int sounding = 0;
        for (const auto& n : notes) if (n.on) ++sounding;
        const float parentRatio = parent >= 0 ? lg (xp / parentX, -8.0f, 3.0f) : 0.0f;
        const bool parentNew = parent >= 0 && onsetX[(size_t) parent] < 0.3f * parentX;
        const float fund = spectrumAt (m);

        int k = 0;
        f[k++] = 1.0f;
        f[k++] = lg (shareRel, -4.0f, 4.0f);
        f[k++] = lg (xp / strongest, -8.0f, 0.0f);
        f[k++] = rising ? 1.0f : 0.0f;
        f[k++] = lg (xp / std::max (before, 1.0e-9f), -3.0f, 6.0f);
        f[k++] = sinceOnset <= 8 ? 1.0f : 0.0f;
        f[k++] = (float) std::min (sinceOnset, 30) / 30.0f;
        f[k++] = (float) std::min (sinceBurst, 30) / 30.0f;
        f[k++] = isGhost (p, rising) ? 1.0f : 0.0f;
        f[k++] = hasHarmonicPeaks (p) ? 1.0f : 0.0f;
        f[k++] = isClaimed (p) ? 1.0f : 0.0f;
        f[k++] = parent >= 0 ? 1.0f : 0.0f;
        f[k++] = parentRatio;
        f[k++] = parentNew ? 1.0f : 0.0f;
        f[k++] = parent >= 0 && p - parent == 12 ? 1.0f : 0.0f;
        f[k++] = child >= 0 ? 1.0f : 0.0f;
        f[k++] = child >= 0 && hasOwnFundamental (p, child) ? 1.0f : 0.0f;
        f[k++] = neighbour;
        f[k++] = (float) (m - 64.0) / 12.0f;
        f[k++] = std::clamp (ownEnergyAt (p, m) / std::max (fund, 1.0e-9f), -1.0f, 1.5f);
        f[k++] = (float) sounding / 6.0f;
        f[k++] = midiToHz (m) < 105.0 ? 1.0f : 0.0f;
        f[k++] = sinceBurst < (m < 52 ? 3 : 2) ? 1.0f : 0.0f;
        f[k++] = rising && parent >= 0 ? 1.0f : 0.0f;
        f[k++] = parentNew ? parentRatio : 0.0f;
        f[k++] = rising ? parentRatio : 0.0f;

        // New: does p's energy start together with its parent's own (odd) harmonics, and is there more of it than
        // this guitar's calibrated parent makes by itself?
        const int hn = parent >= 0 ? harmonicOf (p - parent) : 0;
        float r[4] = {}, a0 = 0.0f, a3 = 0.0f, b0 = 0.0f, b3 = 0.0f, rV = 0.0f, rMax = 0.0f, rMin = 0.0f;
        if (hn > 0)
        {
            const double fq = midiToHz (lowestPitch + parent);
            const double binHz = rate / kShort;
            auto shortAt = [&] (const std::vector<float>& s, double hz)
            {
                const int b = (int) std::lround (hz / binHz);
                float v = 0.0f;
                for (int i = b - 1; i <= b + 1; ++i) if (i >= 1 && i < (int) s.size()) v = std::max (v, s[(size_t) i]);
                return v;
            };
            for (int t = 0; t < 4; ++t)
            {
                const auto& s = shortHist[(size_t) ((shortPos + 3 - t) % 4)];
                const float a = shortAt (s, hn * fq);
                float b = 1.0e-6f;
                for (int h = 1; h <= 6; ++h) if (h % hn != 0) b += shortAt (s, h * fq);
                r[t] = std::log2 ((a + 1.0e-6f) / b);
                if (t == 0) { a0 = a; b0 = b; }
                if (t == 3) { a3 = a; b3 = b; }
            }
            // The same ratio in the long spectrum, and in the parent's own fingerprints (calibrated: per string).
            auto ratioIn = [&] (auto valueAt)
            {
                float b = 1.0e-6f;
                for (int h = 1; h <= 6; ++h) if (h % hn != 0) b += valueAt (12.0 * std::log2 ((double) h));
                return std::log2 ((valueAt (12.0 * std::log2 ((double) hn)) + 1.0e-6f) / b);
            };
            const double mq = lowestPitch + parent;
            rV = ratioIn ([&] (double up) { return spectrumAt (mq + up); });
            rMax = -100.0f; rMin = 100.0f;
            for (int t : pitchTemplates[(size_t) parent])
            {
                const float rt = ratioIn ([&] (double up)
                {
                    const int b = (int) std::lround ((mq + up - midiLo) * kBinsPerSemitone);
                    float v = 0.0f;
                    for (int i = b - 1; i <= b + 1; ++i)
                        if (i >= 0 && i < numBins) v = std::max (v, dense[(size_t) t * (size_t) numBins + (size_t) i]);
                    return v;
                });
                rMax = std::max (rMax, rt);
                rMin = std::min (rMin, rt);
            }
            if (rMax < rMin) rMax = rMin = rV;
        }
        f[k++] = hn > 0 ? 1.0f : 0.0f;
        f[k++] = std::clamp (r[0], -8.0f, 4.0f);
        f[k++] = std::clamp (r[0] - r[1], -6.0f, 6.0f);
        f[k++] = std::clamp (r[0] - r[2], -6.0f, 6.0f);
        f[k++] = std::clamp (r[0] - r[3], -6.0f, 6.0f);
        f[k++] = std::clamp (rV, -8.0f, 4.0f);
        f[k++] = std::clamp (rV - rMax, -8.0f, 8.0f);
        f[k++] = std::clamp (rV - rMin, -8.0f, 8.0f);
        f[k++] = clearRun >= 2 && clearP == p ? 1.0f : 0.0f;
        f[k++] = clearRun >= 2 && clearP >= 0 && harmonicOf (p - clearP) > 0 ? 1.0f : 0.0f;
        f[k++] = (float) std::min (clearRun, 10) / 10.0f;
        f[k++] = (float) hn / 6.0f;
        f[k++] = hn > 0 ? std::clamp (std::log2 ((a0 + 1.0e-6f) / (a3 + 1.0e-6f)), -6.0f, 6.0f) : 0.0f;
        f[k++] = hn > 0 ? std::clamp (std::log2 (b0 / b3), -6.0f, 6.0f) : 0.0f;
    }

    void PolyTracker::updateNotes()
    {
        float total = 1.0e-9f, strongest = 1.0e-9f;
        for (float v : V) total += v;
        for (int p = 0; p < numPitches; ++p) strongest = std::max (strongest, x[(size_t) p]);

        const float gateOn = cal.gateOn() / (float) settings.sensitivity;
        const float gateOff = cal.gateOff() / (float) settings.sensitivity;
        const float onShare = 0.045f / (float) std::sqrt (settings.sensitivity);
        const int confirm = std::clamp (settings.stableFrames - 1, 1, 6);

        // --- Sounding notes: follow the pitch, end the ones that stopped, re-pluck the ones picked again ---
        for (int p = 0; p < numPitches; ++p)
        {
            auto& n = notes[(size_t) p];
            if (! n.on) continue;
            ++n.age;

            if (settings.trackBends)
                followPitch (p);

            float eff = x[(size_t) p];
            if (std::abs (n.dev) > 0.3)
            {
                const int q = (int) std::lround (p + n.dev);
                if (q >= 0 && q < numPitches) eff = std::max (eff, x[(size_t) q]);
            }
            n.peak = std::max (n.peak, eff);

            bool gone = level < gateOff
                      || (eff < 0.025f * total && eff < 0.25f * n.peak)
                      || eff < 0.04f * n.peak;
            if (n.hidden)
            {
                gone = level < gateOff;
                bool lower = false; // a real note with the same name still sounds?
                for (int q = 0; q < numPitches; ++q)
                    if (q != p && notes[(size_t) q].on && ! notes[(size_t) q].hidden && (p - q) % 12 == 0) lower = true;
                if (! lower) gone = true;
            }
            n.low = gone ? n.low + 1 : 0;
            if (n.low >= 3)
            {
                stop (p);
                continue;
            }

        }

        // Hysteresis: a pitch on an overtone of a louder note from the same pick (started in the last ~85 ms, or about
        // to start) needs more energy, so a single note's overtones are not taken for a chord. Other pitches, and the
        // overtones of older ringing notes (an arpeggio), are not touched. 0 = off.
        std::array<bool, 128> young {};
        float pitchSum = 1.0e-9f;
        if (settings.hysteresis > 0.0)
            for (int q = 0; q < std::min (numPitches, 128); ++q)
            {
                const auto& n = notes[(size_t) q];
                // Low notes need longer to start than their overtones: a pitch whose energy jumped at the pick
                // attack counts as just picked even before it is ready to start.
                const bool jumped = sinceOnset <= 8 && x[(size_t) q] > 2.5f * history[(size_t) q][(size_t) historyPos]
                                 && x[(size_t) q] > onShare * total;
                young[(size_t) q] = candidate[(size_t) q] > 0 || jumped || (n.on && ! n.hidden && ! n.repicked && n.age <= 8);
                pitchSum += x[(size_t) q];
            }

        // --- New notes: strong for a few frames, not a harmonic of a louder note, not part of a bend ---
        int count = 0;
        for (int p = 0; p < numPitches; ++p)
        {
            if (notes[(size_t) p].on) { candidate[(size_t) p] = 0; continue; }

            float parent = 0.0f;
            if (settings.hysteresis > 0.0)
                for (int step : { 12, 19, 24, 28, 31 })
                    if (const int q = p - step; q >= 0 && q < 128 && young[(size_t) q] && x[(size_t) q] > x[(size_t) p])
                        parent = std::max (parent, x[(size_t) q]);
            const float startShare = onShare * (1.0f + 3.0f * (float) settings.hysteresis * parent / pitchSum);

            const float share = x[(size_t) p] / total;
            // New energy right after a pick attack: a note picked now, even if it sits on the harmonics of a
            // note that already rings (an arpeggio going up an octave).
            const float before = history[(size_t) p][(size_t) historyPos]; // 4 frames ago
            const bool rising = sinceOnset <= 8 && x[(size_t) p] > 2.5f * before
                             && x[(size_t) p] - before > 0.5f * startShare * total;

            bool ok = level > gateOn && (share > startShare || rising) && x[(size_t) p] > (rising ? 0.04f : 0.12f) * strongest;
            // Without a pick attack (hammer-on, slow swell) only a clearly strong note may start.
            if (ok && sinceOnset > 8 && share < 2.0f * startShare) ok = false;
            if (ok && (isGhost (p, rising) || isClaimed (p) || ! hasHarmonicPeaks (p))) ok = false;
            if (ok && overtoneOfClearNote (p)) ok = false;
            // Right after a pick attack the window holds only a little of the new note: its pitch is still blurred.
            // Low notes need one frame more (their harmonics are closer together).
            if (ok && sinceBurst < (lowestPitch + p < 52 ? 3 : 2)) ok = false;

            // After a Full calibration, the scorer learned from this guitar decides instead of the rules above (it
            // knows its overtones much better). It sees every pitch with a little new energy.
            const bool learned = cal.net.usable() && cal.net.inputs == kNumNoteFeatures;
            if ((learned || featureSink) && level > gateOn && (share > 0.3f * onShare || rising))
            {
                float f[kNumNoteFeatures];
                noteFeatures (p, share / onShare, rising, before, strongest, f);
                if (featureSink) featureSink (lowestPitch + p, f, kNumNoteFeatures, ok);
                if (learned) ok = cal.net.score (f) > 0.0f;
            }
            else if (learned) ok = false;

            candidate[(size_t) p] = ok ? candidate[(size_t) p] + 1 : 0;
            // The lowest notes (below ~105 Hz) are the easiest to fake: one more frame of proof.
            if (candidate[(size_t) p] >= confirm + (midiToHz (lowestPitch + p) < 105.0 ? 1 : 0) && count < (int) fresh.size())
                fresh[(size_t) count++] = p;
        }
        if (count > 0)
            startNotes (count);

        // In a strum of 3 or more strings: fill in octave notes hidden in the harmonics of a known shape, as soon as
        // a new note starts (no waiting), and again ~75 and ~130 ms in.
        if (startedInBurst >= 3 && sinceBurst <= 12 && (count > 0 || sinceBurst == 7 || sinceBurst == 12))
            completeChord();

        // A pick attack that no new note explains (~50 ms later): a ringing note was picked again (tremolo picking,
        // repeated notes). Start again the notes whose energy grew since the attack, or the only note sounding.
        if (repickPending && sinceOnset == 5)
        {
            repickPending = false;
            if (startedSinceOnset == 0)
            {
                int sounding = 0, only = -1;
                for (int p = 0; p < numPitches; ++p)
                    if (notes[(size_t) p].on && notes[(size_t) p].age > 8) { ++sounding; only = p; }
                for (int p = 0; p < numPitches; ++p)
                {
                    auto& n = notes[(size_t) p];
                    if (! n.on || n.age <= 8) continue;
                    const bool grew = x[(size_t) p] > 1.1f * onsetX[(size_t) p] && x[(size_t) p] > onShare * total;
                    if (! grew && ! (sounding == 1 && p == only)) continue;
                    const int s = n.string, f = n.fret;
                    stop (p);
                    if (bend[(size_t) s] != 8192) sendBend (s, 8192);
                    send ((uint8_t) (0x90 | (channelOf (s) - 1)), (uint8_t) (lowestPitch + p), (uint8_t) velocityFor (p, s, f, false));
                    notes[(size_t) p] = { true, s, f, x[(size_t) p], 0, 0.0, 0 };
                    notes[(size_t) p].repicked = true;
                }
            }
        }

        for (int p = 0; p < numPitches; ++p)
            history[(size_t) p][(size_t) historyPos] = x[(size_t) p];
        historyPos = (historyPos + 1) % 4;
    }

    void PolyTracker::startNotes (int count)
    {
        std::sort (fresh.begin(), fresh.begin() + count, [this] (int a, int b) { return x[(size_t) a] > x[(size_t) b]; });

        // Notes from the same strum (started in the last ~60 ms) are placed again together with the new ones:
        // a note that arrived first may have to move to another string when the rest of the chord shows up.
        std::array<int, kMaxStrings> items {};
        std::array<bool, kMaxStrings> moving {};
        int numItems = 0;
        for (int p = 0; p < numPitches && numItems < inst.numStrings(); ++p)
            if (notes[(size_t) p].on && notes[(size_t) p].age <= 6)
            {
                items[(size_t) numItems] = p;
                moving[(size_t) numItems++] = true;
            }
        for (int i = 0; i < count && numItems < inst.numStrings(); ++i) // strongest first, never more notes than strings
        {
            items[(size_t) numItems] = fresh[(size_t) i];
            moving[(size_t) numItems++] = false;
        }

        float total = 1.0e-9f;
        for (float v : V) total += v;

        std::array<int, kMaxStrings> owner;   // older notes still ringing on each string
        owner.fill (-1);
        for (int p = 0; p < numPitches; ++p)
            if (notes[(size_t) p].on && notes[(size_t) p].string >= 0 && notes[(size_t) p].age > 6)
                owner[(size_t) notes[(size_t) p].string] = p;

        struct Option { int string, fret; };
        std::array<std::array<Option, kMaxStrings>, kMaxStrings> options {};
        std::array<int, kMaxStrings> numOptions {};
        for (int i = 0; i < numItems; ++i)
        {
            const int pitch = lowestPitch + items[(size_t) i];
            for (int s = 0; s < inst.numStrings(); ++s)
            {
                const int f = pitch - inst.tuning()[(size_t) s];
                if (f >= 0 && f <= cal.lastFret[(size_t) s])
                    options[(size_t) i][(size_t) numOptions[(size_t) i]++] = { s, f };
            }
        }

        // Try every way to place the notes on strings; keep the easiest to play.
        std::array<int, kMaxStrings> choice {}, best {}, newFret {};
        std::array<bool, kMaxStrings> used {};
        best.fill (-1);
        double bestCost = 1.0e18;

        auto search = [&] (auto& self, int i, double cost) -> void
        {
            if (cost >= bestCost) return;
            if (i == numItems)
            {
                // Hand span over all fretted notes that will sound (open strings do not count).
                int lo = 99, hi = -1;
                for (int s = 0; s < inst.numStrings(); ++s)
                {
                    int f = -1;
                    if (used[(size_t) s]) f = newFret[(size_t) s];
                    else if (owner[(size_t) s] >= 0) f = notes[(size_t) owner[(size_t) s]].fret;
                    if (f > 0) { lo = std::min (lo, f); hi = std::max (hi, f); }
                }
                const int spanFrets = hi >= lo ? hi - lo : 0;
                const double withSpan = cost + (spanFrets > 4 ? 3.0 * (spanFrets - 4) : 0.0);
                if (withSpan < bestCost) { bestCost = withSpan; best = choice; }
                return;
            }

            choice[(size_t) i] = -1; // leave this note out (only when nothing fits)
            self (self, i + 1, cost + (moving[(size_t) i] ? 100.0 : 50.0));

            for (int o = 0; o < numOptions[(size_t) i]; ++o)
            {
                const auto opt = options[(size_t) i][(size_t) o];
                if (used[(size_t) opt.string]) continue;
                // Close to the hand, and a little lower on the neck (open position is the most common).
                double c = opt.fret == 0 ? 0.6 : 0.3 * std::abs (opt.fret - handFret) + 0.08 * opt.fret + (opt.fret > 12 ? 0.5 : 0.0);
                if (const int old = owner[(size_t) opt.string]; old >= 0)
                    c += 1.0 + 8.0 * x[(size_t) old] / total; // stopping a note that still rings is unlikely
                if (moving[(size_t) i] && opt.string != notes[(size_t) items[(size_t) i]].string)
                    c += 0.4; // moving a note that already sounds: only when it really fits better
                used[(size_t) opt.string] = true;
                newFret[(size_t) opt.string] = opt.fret;
                choice[(size_t) i] = o;
                self (self, i + 1, cost + c);
                used[(size_t) opt.string] = false;
            }
        };
        search (search, 0, 0.0);

        // Notes that move (or do not fit any more) stop on their old string first.
        for (int i = 0; i < numItems; ++i)
        {
            if (! moving[(size_t) i]) continue;
            const int p = items[(size_t) i];
            if (best[(size_t) i] < 0 || options[(size_t) i][(size_t) best[(size_t) i]].string != notes[(size_t) p].string)
                stop (p);
        }

        for (int i = 0; i < numItems; ++i)
        {
            const int p = items[(size_t) i];
            candidate[(size_t) p] = 0;
            if (best[(size_t) i] < 0 || notes[(size_t) p].on) continue; // left out, or stays where it is

            const auto opt = options[(size_t) i][(size_t) best[(size_t) i]];
            const int old = owner[(size_t) opt.string];
            // Same string, no new pick attack = hammer-on / pull-off: start the new note before ending the old one.
            const bool legato = old >= 0 && ! moving[(size_t) i] && sinceOnset > 2;
            if (old >= 0 && ! legato) stop (old);
            if (bend[(size_t) opt.string] != 8192) sendBend (opt.string, 8192);
            send ((uint8_t) (0x90 | (channelOf (opt.string) - 1)), (uint8_t) (lowestPitch + p),
                  (uint8_t) velocityFor (p, opt.string, opt.fret, legato));
            if (old >= 0 && legato) stop (old);
            const int age = moving[(size_t) i] ? notes[(size_t) p].age : 0;
            if (! moving[(size_t) i]) { ++startedInBurst; ++startedSinceOnset; }
            notes[(size_t) p] = { true, opt.string, opt.fret, std::max (notes[(size_t) p].peak, x[(size_t) p]), 0, 0.0, age };
        }

        // Where the hand is now: helps place the next notes.
        int sum = 0, fretted = 0;
        for (const auto& n : notes)
            if (n.on && n.fret > 0) { sum += n.fret; ++fretted; }
        if (fretted > 0) handFret = std::clamp ((int) std::lround ((double) sum / fretted), 0, kNumFrets);
    }

    void PolyTracker::stop (int p)
    {
        auto& n = notes[(size_t) p];
        if (! n.on) return;
        send ((uint8_t) (0x80 | (channelOf (n.string) - 1)), (uint8_t) (lowestPitch + p), (uint8_t) 0);
        n.on = false;
        n.low = 0;
        candidate[(size_t) p] = 0;
    }

    void PolyTracker::sendBend (int string, int value)
    {
        value = std::clamp (value, 0, 16383);
        send ((uint8_t) (0xE0 | (channelOf (string) - 1)), (uint8_t) (value & 0x7F), (uint8_t) ((value >> 7) & 0x7F));
        bend[(size_t) string] = value;
    }

    int PolyTracker::velocityFor (int p, int string, int fret, bool legato) const
    {
        // Pick strength right after the attack (a hammer-on has none: use the level now).
        float peak = sinceOnset < 8 ? attackPeak : levelPeak;
        if (auto* pos = cal.position (string, fret); pos != nullptr && pos->level > 0.0f && cal.typicalLevel > 0.0f)
            peak *= std::clamp (cal.typicalLevel / pos->level, 0.5f, 2.0f);

        float strongest = 1.0e-9f;
        for (int q = 0; q < numPitches; ++q) strongest = std::max (strongest, x[(size_t) q]);
        double v = cal.velocityFromPeak (peak, string) * (0.75 + 0.25 * std::sqrt (x[(size_t) p] / strongest));
        if (legato) v *= 2.0 / 3.0;
        return std::clamp ((int) std::lround (v), 1, 127);
    }

    void PolyTracker::followPitch (int p)
    {
        // Exact pitch from the short spectrum: the strongest clean harmonic near where the note should be.
        auto& n = notes[(size_t) p];
        const double spot = cal.centsAt (n.string, n.fret) / 100.0;
        const double expected = midiToHz (lowestPitch + p + n.dev + spot);
        const auto& m = shortSpec.magnitudes();
        const double binHz = rate / kShort;

        float top = 1.0e-9f;
        for (float v : m) top = std::max (top, v);

        int bestK = -1, bestH = 1;
        float bestMag = 0.0f;
        for (int h = 1; h <= 12; ++h)
        {
            const double fh = h * expected;
            if (fh < 250.0) continue;
            if (fh > rate * 0.42) break;

            // Skip harmonics that another sounding note also has (they would pull the pitch).
            bool shared = false;
            for (int q = 0; q < numPitches && ! shared; ++q)
            {
                if (q == p || ! notes[(size_t) q].on) continue;
                const double fq = midiToHz (lowestPitch + q + notes[(size_t) q].dev + spot);
                for (int g = 1; g <= 16 && ! shared; ++g)
                    shared = std::abs (hzToMidi (g * fq) - hzToMidi (fh)) < 1.0;
            }
            if (shared) continue;

            const int k0 = std::max (1, (int) std::floor (fh * std::pow (2.0, -0.6 / 12.0) / binHz));
            const int k1 = std::min ((int) m.size() - 2, (int) std::ceil (fh * std::pow (2.0, 0.6 / 12.0) / binHz));
            for (int k = k0; k <= k1; ++k)
                if (m[(size_t) k] > bestMag && m[(size_t) k] >= m[(size_t) k - 1] && m[(size_t) k] >= m[(size_t) k + 1])
                {
                    bestMag = m[(size_t) k];
                    bestK = k;
                    bestH = h;
                }
        }
        if (bestK < 0 || bestMag < 0.02f * top)
            return;

        // Parabola through the peak and its neighbours (log magnitude) for a fraction of a bin.
        const double a = std::log (m[(size_t) bestK - 1] + 1.0e-9), b = std::log (m[(size_t) bestK] + 1.0e-9),
                     c = std::log (m[(size_t) bestK + 1] + 1.0e-9);
        const double den = a - 2.0 * b + c;
        const double shift = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        const double hz = (bestK + shift) * binHz / bestH;
        const double dev = hzToMidi (hz) - spot - (lowestPitch + p);

        if (std::abs (dev - n.dev) > 0.7)
            return; // jumped to something else: ignore this frame
        n.dev += (dev - n.dev) * 0.6;

        const int target = std::abs (n.dev) < 0.1 ? 8192
                         : (int) std::lround (8192.0 + n.dev / std::max (1, settings.bendRange) * 8192.0);
        if (std::abs (target - bend[(size_t) n.string]) > 16)
            sendBend (n.string, target);
    }
}
