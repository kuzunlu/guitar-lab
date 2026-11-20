#include "Tracking/Calibration.h"
#include "Theory/ChordFinder.h"
#include "Tracking/PitchDetector.h"
#include "Theory/Voicing.h"

#include <cmath>
#include <sstream>

namespace gl
{
    namespace
    {
        template <typename T>
        T median (std::vector<T> v)
        {
            if (v.empty()) return T {};
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return v[v.size() / 2];
        }

        Timbre meanTimbre (const std::vector<Timbre>& all)
        {
            Timbre t {};
            if (all.empty()) return t;
            for (const auto& x : all)
                for (size_t i = 0; i < t.size(); ++i) t[i] += x[i];
            for (auto& v : t) v /= (float) all.size();
            return t;
        }

        Chroma meanChroma (const std::vector<Chroma>& all)
        {
            Chroma c {};
            for (const auto& x : all)
                for (size_t i = 0; i < c.size(); ++i) c[i] += x[i];
            float len = 0.0f;
            for (float v : c) len += v * v;
            len = std::sqrt (len);
            if (len > 0.0f) for (auto& v : c) v /= len;
            return c;
        }

        constexpr double kSilenceSeconds   = 3.0;
        constexpr double kPitchSeconds     = 0.6;  // pitched sound needed per open string
        constexpr double kSpotSeconds      = 0.3;  // pitched sound needed per fret ("every note")
        constexpr double kSkipAttack       = 0.08; // ignore the pick attack
        constexpr int    kHitsNeeded       = 4;
        constexpr int    kStrumsNeeded     = 3;
        constexpr double kInTuneCents      = 60.0;
        constexpr double kListenTimeout    = 10.0; // seconds without a clear note: stop listening
    }

    // =======================================================================
    // Calibration
    // =======================================================================
    int Calibration::velocityFromPeak (float peak, int string) const
    {
        if (peak <= 0.0f) return 1;
        float soft = softPeak, hard = hardPeak;
        if (string >= 0 && string < kMaxStrings && stringSoft[(size_t) string] > 0.0f && stringHard[(size_t) string] > 0.0f)
        {
            soft = stringSoft[(size_t) string];
            hard = stringHard[(size_t) string];
        }
        const double lo = std::log (std::max (1e-5f, soft));
        const double hi = std::log (std::max (soft * 1.5f, hard));
        const double t = (std::log (peak) - lo) / (hi - lo);
        return std::clamp ((int) std::lround (30.0 + t * 90.0), 1, 127);
    }

    double Calibration::centsFor (int string) const
    {
        if (string >= 0 && string < kMaxStrings && strings[(size_t) string].measured)
            return strings[(size_t) string].centsOffset;
        return globalCents;
    }

    std::string Calibration::serialise() const
    {
        std::ostringstream out;
        out << "C " << (int) valid << ' ' << (int) hexMode << ' ' << globalCents << ' ' << noiseFloor << ' '
            << softPeak << ' ' << hardPeak << ' ' << tuning.size();
        for (int n : tuning) out << ' ' << n;
        out << '\n';
        out << "T " << typicalLevel << '\n';

        for (int s = 0; s < kMaxStrings; ++s)
        {
            const auto& sc = strings[(size_t) s];
            out << "S " << s << ' ' << (int) sc.measured << ' ' << sc.centsOffset << ' ' << sc.intonationCents << ' '
                << sc.inputChannel << ' ' << sc.gain << ' ' << channelNoise[(size_t) s] << '\n';
            out << "F " << s << ' ' << lastFret[(size_t) s] << '\n';
            if (stringSoft[(size_t) s] > 0.0f || stringHard[(size_t) s] > 0.0f)
                out << "D " << s << ' ' << stringSoft[(size_t) s] << ' ' << stringHard[(size_t) s] << '\n';

            for (int c = 0; c < kMaxStrings; ++c)
                if (bleed[(size_t) s][(size_t) c] > 0.0f)
                    out << "B " << s << ' ' << c << ' ' << bleed[(size_t) s][(size_t) c] << '\n';

            for (int f = 0; f <= kNumFrets; ++f)
            {
                const auto& p = positions[(size_t) s][(size_t) f];
                if (! p.measured) continue;
                out << "P " << s << ' ' << f << ' ' << p.cents << ' ' << p.level << ' ' << (int) p.hasTimbre;
                for (float t : p.timbre) out << ' ' << t;
                out << '\n';
                if (p.hasAttack)
                {
                    out << "H " << s << ' ' << f;
                    for (float a : p.attack) out << ' ' << a;
                    out << '\n';
                }
            }
        }

        for (const auto& ch : chords)
        {
            std::string name = ch.name;
            std::replace (name.begin(), name.end(), ' ', '_');
            out << "K " << name << ' ' << ch.frets.size();
            for (int f : ch.frets) out << ' ' << f;
            for (float v : ch.chroma) out << ' ' << v;
            out << '\n';
        }
        return out.str();
    }

    void Calibration::deserialise (const std::string& text)
    {
        *this = Calibration {};
        std::istringstream in (text);
        std::string line;
        while (std::getline (in, line))
        {
            std::istringstream ls (line);
            std::string tag;
            ls >> tag;
            if (tag == "C")
            {
                int v = 0, h = 0;
                size_t count = 0;
                ls >> v >> h >> globalCents >> noiseFloor >> softPeak >> hardPeak >> count;
                valid = v != 0;
                hexMode = h != 0;
                for (size_t i = 0; i < count && i < (size_t) kMaxStrings; ++i)
                {
                    int n = 0;
                    if (ls >> n) tuning.push_back (n);
                }
            }
            else if (tag == "T")
            {
                ls >> typicalLevel;
            }
            else if (tag == "S")
            {
                int s = -1, m = 0;
                StringCalibration sc;
                float noise = 0.0f;
                ls >> s >> m >> sc.centsOffset >> sc.intonationCents >> sc.inputChannel >> sc.gain >> noise;
                if (ls && s >= 0 && s < kMaxStrings)
                {
                    sc.measured = m != 0;
                    strings[(size_t) s] = sc;
                    channelNoise[(size_t) s] = noise;
                }
            }
            else if (tag == "F")
            {
                int s = -1, f = kNumFrets;
                if (ls >> s >> f && s >= 0 && s < kMaxStrings)
                    lastFret[(size_t) s] = std::clamp (f, 0, kNumFrets);
            }
            else if (tag == "D")
            {
                int s = -1;
                float soft = 0.0f, hard = 0.0f;
                if (ls >> s >> soft >> hard && s >= 0 && s < kMaxStrings)
                {
                    stringSoft[(size_t) s] = soft;
                    stringHard[(size_t) s] = hard;
                }
            }
            else if (tag == "B")
            {
                int s = -1, c = -1;
                float v = 0.0f;
                if (ls >> s >> c >> v && s >= 0 && s < kMaxStrings && c >= 0 && c < kMaxStrings)
                    bleed[(size_t) s][(size_t) c] = v;
            }
            else if (tag == "P")
            {
                int s = -1, f = -1, ht = 0;
                PositionCalibration p;
                ls >> s >> f >> p.cents >> p.level >> ht;
                for (auto& t : p.timbre) ls >> t;
                if (ls && s >= 0 && s < kMaxStrings && f >= 0 && f <= kNumFrets)
                {
                    p.measured = true;
                    p.hasTimbre = ht != 0;
                    positions[(size_t) s][(size_t) f] = p;
                }
            }
            else if (tag == "H") // after its "P" line
            {
                int s = -1, f = -1;
                Overtones a {};
                ls >> s >> f;
                for (auto& v : a) ls >> v;
                if (ls && s >= 0 && s < kMaxStrings && f >= 0 && f <= kNumFrets)
                {
                    positions[(size_t) s][(size_t) f].attack = a;
                    positions[(size_t) s][(size_t) f].hasAttack = true;
                }
            }
            else if (tag == "K")
            {
                ChordTemplate ch;
                size_t count = 0;
                ls >> ch.name >> count;
                std::replace (ch.name.begin(), ch.name.end(), '_', ' ');
                for (size_t i = 0; i < count && i < (size_t) kMaxStrings; ++i)
                {
                    int f = -1;
                    ls >> f;
                    ch.frets.push_back (f);
                }
                for (auto& v : ch.chroma) ls >> v;
                if (ls) chords.push_back (ch);
            }
        }
    }

    // =======================================================================
    // CalibrationSession
    // =======================================================================
    std::vector<ChordTemplate> CalibrationSession::chordShapesFor (const Instrument& inst)
    {
        // Many chord types, so the tracker knows how YOUR guitar sounds for each kind of chord:
        // open chords, sevenths, suspended chords, barre chords up the neck and power chords.
        std::vector<ChordTemplate> out;
        auto addNamed = [&] (std::initializer_list<const char*> names, int minFret, int maxFret, bool openStrings)
        {
            VoicingOptions opt;
            opt.maxSpan = 4;
            opt.minStrings = std::min (4, inst.numStrings());
            opt.maxStrings = 6;
            opt.rootInBass = true;
            opt.allowOpenStrings = openStrings;
            opt.minFret = minFret;
            opt.maxFret = maxFret;
            for (const char* name : names)
            {
                // The fingering most guitarists use (open chord, barre chord...) when it fits this part of the neck.
                bool added = false;
                if (const auto parsed = parseChordName (name))
                    for (const auto& c : commonShapes (*parsed, inst))
                    {
                        const auto& f = c.voicing.frets;
                        const bool usesOpen = std::find (f.begin(), f.end(), 0) != f.end();
                        bool inArea = true;
                        for (int x : f) if (x > 0 && (x < minFret || x > maxFret)) inArea = false;
                        if (! inArea || (usesOpen && ! openStrings) || c.voicing.numPlayed() < opt.minStrings) continue;
                        out.push_back ({ name, f, {} });
                        added = true;
                        break;
                    }
                if (added) continue;

                const auto chord = parseChordSymbol (name);
                if (! chord) continue;
                const auto found = findVoicings (*chord, inst, opt, 1);
                if (! found.empty())
                    out.push_back ({ name, found.front().frets, {} });
            }
        };

        addNamed ({ "E", "A", "D", "G", "C", "Am", "Em", "Dm" }, 0, 4, true);                            // open
        addNamed ({ "E7", "A7", "D7", "G7", "C7", "B7", "Am7", "Em7", "Dm7" }, 0, 4, true);              // sevenths
        addNamed ({ "Cmaj7", "Fmaj7", "Amaj7", "Dmaj7" }, 0, 4, true);                                   // major sevenths
        addNamed ({ "Asus2", "Asus4", "Dsus2", "Dsus4", "Esus4" }, 0, 4, true);                          // suspended
        addNamed ({ "F", "Bb", "Bm", "Cm", "F#m", "Gm", "C#m", "Bm7b5" }, 1, 9, false);                  // barre chords

        // Power chords (root + 5th + octave) on the two lowest strings, in any tuning.
        const int n = inst.numStrings();
        for (int low : { n - 1, n - 2 })
            for (int fret : { 0, 3, 5, 7 })
            {
                if (low < 2) continue;
                const int root = inst.tuning()[(size_t) low] + fret;
                std::vector<int> frets ((size_t) n, -1);
                frets[(size_t) low] = fret;
                const int f5 = root + 7 - inst.tuning()[(size_t) low - 1];
                const int f8 = root + 12 - inst.tuning()[(size_t) low - 2];
                if (f5 < 0 || f5 > kNumFrets || f8 < 0 || f8 > kNumFrets || std::max (f5, f8) - fret > 4) continue;
                frets[(size_t) low - 1] = f5;
                frets[(size_t) low - 2] = f8;
                out.push_back ({ pitchClassName (root % 12) + "5", frets, {} });
            }
        return out;
    }

    void CalibrationSession::start (const Instrument& i, bool hexMode, int numInputChannels, bool fullCalibration)
    {
        inst = i;
        cal = Calibration {};
        cal.hexMode = hexMode;
        cal.tuning = inst.tuning();
        inputs = std::clamp (numInputChannels, 1, kMaxStrings);
        full = fullCalibration;
        softLevels.clear();
        hardLevels.clear();
        bleedSum.fill (0.0);
        bleedCount.fill (0);
        previousNote = -1;

        const int lowest = inst.numStrings() - 1;
        plan.clear();
        plan.push_back ({ Step::Silence, -1 });

        if (full)
        {
            // Every fret of every string, one item each (this also covers the open strings and the 12th fret).
            for (int s = lowest; s >= 0; --s)
                for (int f = 0; f <= cal.lastFret[(size_t) s]; ++f)
                    plan.push_back ({ Step::EveryNote, s, f });

            // Chords only help in mono mode: hex mode hears every string on its own anyway.
            if (! hexMode)
            {
                chordList = chordShapesFor (inst);
                for (int c = 0; c < (int) chordList.size(); ++c)
                    plan.push_back ({ Step::Chord, c });
            }
        }
        else
        {
            for (int s = lowest; s >= 0; --s)
                plan.push_back ({ Step::OpenString, s });
            plan.push_back ({ Step::TwelfthFret, lowest });
            plan.push_back ({ Step::TwelfthFret, 0 });
        }

        if (full)
        {
            // Soft and hard picking on every string: each string gets its own velocity range.
            for (int s = lowest; s >= 0; --s)
            {
                plan.push_back ({ Step::Soft, s });
                plan.push_back ({ Step::Hard, s });
            }
        }
        else
        {
            plan.push_back ({ Step::Soft, inst.numStrings() / 2 });
            plan.push_back ({ Step::Hard, inst.numStrings() / 2 });
        }
        plan.push_back ({ Step::Done, -1 });

        stepIndex = backLimit = 0;
        spotLevels.clear();
        history.clear();
        say ({}, true);
        beginStep();
    }

    std::string CalibrationSession::saveDraft() const
    {
        std::ostringstream out;
        out << "GLDRAFT 1 " << (int) full << ' ' << (int) cal.hexMode << ' ' << inputs << ' ' << stepIndex << ' '
            << inst.numStrings();
        for (int n : inst.tuning()) out << ' ' << n;
        out << '\n';
        for (const auto& item : plan)
            out << "Q " << (int) item.step << ' ' << item.string << ' ' << item.fret << '\n';
        auto values = [&out] (const char* tag, const std::vector<float>& v)
        {
            out << tag << ' ' << v.size();
            for (float x : v) out << ' ' << x;
            out << '\n';
        };
        values ("L", spotLevels);
        values ("SL", softLevels);
        values ("HL", hardLevels);
        out << "BL";
        for (int s = 0; s < kMaxStrings; ++s) out << ' ' << bleedSum[(size_t) s] << ' ' << bleedCount[(size_t) s];
        out << "\nCAL\n" << cal.serialise();
        return out.str();
    }

    bool CalibrationSession::loadDraft (const std::string& text)
    {
        std::istringstream in (text);
        std::string line, tag;
        int version = 0, isFull = 0, hex = 0, numIn = 1, index = 0, count = 0;
        if (! std::getline (in, line)) return false;
        std::istringstream head (line);
        head >> tag >> version >> isFull >> hex >> numIn >> index >> count;
        if (! head || tag != "GLDRAFT" || version != 1 || count < 1 || count > kMaxStrings) return false;
        std::vector<MidiNote> tuning ((size_t) count);
        for (auto& n : tuning) head >> n;
        Instrument loaded;
        if (! head || ! loaded.setTuning (tuning)) return false;

        std::vector<PlanItem> items;
        std::vector<float> spots, soft, hard;
        std::array<double, kMaxStrings> bSum {};
        std::array<int, kMaxStrings> bCount {};
        std::string calText;
        auto readValues = [] (std::istringstream& ls, std::vector<float>& v)
        {
            size_t n = 0;
            ls >> n;
            v.assign (std::min<size_t> (n, 10000), 0.0f);
            for (auto& x : v) ls >> x;
        };
        while (std::getline (in, line))
        {
            if (line == "CAL")
            {
                std::ostringstream rest;
                rest << in.rdbuf();
                calText = rest.str();
                break;
            }
            std::istringstream ls (line);
            ls >> tag;
            if (tag == "Q")
            {
                int st = 0, s = -1, f = 0;
                ls >> st >> s >> f;
                if (ls && st >= 0 && st <= (int) Step::Done) items.push_back ({ (Step) st, s, f });
            }
            else if (tag == "L")  readValues (ls, spots);
            else if (tag == "SL") readValues (ls, soft);
            else if (tag == "HL") readValues (ls, hard);
            else if (tag == "BL") for (int s = 0; s < kMaxStrings; ++s) ls >> bSum[(size_t) s] >> bCount[(size_t) s];
        }
        if (items.empty() || items.back().step != Step::Done || index < 0 || index >= (int) items.size() || calText.empty())
            return false;

        inst = loaded;
        cal.deserialise (calText);
        cal.valid = false;
        cal.hexMode = hex != 0;
        cal.tuning = inst.tuning();
        inputs = std::clamp (numIn, 1, kMaxStrings);
        full = isFull != 0;
        plan = items;
        spotLevels = spots;
        softLevels = soft;
        hardLevels = hard;
        bleedSum = bSum;
        bleedCount = bCount;
        chordList = full && ! cal.hexMode ? chordShapesFor (inst) : std::vector<ChordTemplate> {};
        previousNote = -1;
        history.clear();
        stepIndex = backLimit = index;
        beginStep();
        say ("Welcome back: the calibration goes on where you stopped. Press Listen and play.", true);
        return true;
    }

    int CalibrationSession::currentString() const
    {
        if (stepIndex >= (int) plan.size() || current == Step::Chord) return -1;
        return plan[(size_t) stepIndex].string;
    }

    int CalibrationSession::expectedFret() const
    {
        switch (current)
        {
            case Step::OpenString:  return 0;
            case Step::TwelfthFret: return 12;
            case Step::EveryNote:   return fret;
            case Step::Soft:
            case Step::Hard:        return 5;
            default:                return -1;
        }
    }

    int CalibrationSession::expectedNote() const
    {
        const int s = currentString();
        const int f = expectedFret();
        if (s < 0 || s >= inst.numStrings() || f < 0) return -1;
        return inst.tuning()[(size_t) s] + f;
    }

    const ChordTemplate* CalibrationSession::currentChord() const
    {
        if (current != Step::Chord || chordIndex < 0 || chordIndex >= (int) chordList.size()) return nullptr;
        return &chordList[(size_t) chordIndex];
    }

    std::string CalibrationSession::stringName (int s) const
    {
        if (s < 0 || s >= inst.numStrings()) return "?";
        const std::string note = midiNoteName (inst.tuning()[(size_t) s]);
        if (s == inst.numStrings() - 1) return "lowest string (" + note + ")";
        if (s == 0)                     return "highest string (" + note + ")";
        return "string " + std::to_string (s + 1) + " (" + note + ")";
    }

    CalibrationSession::Phase CalibrationSession::phase() const
    {
        if (current == Step::Done) return Phase::Done;
        if (needDecision)          return Phase::Decide;
        if (listening)             return Phase::Listening;
        return Phase::Ready;
    }

    std::string CalibrationSession::instruction() const
    {
        const auto name = stringName (currentString());
        switch (current)
        {
            case Step::Silence:     return "Stay quiet for 3 seconds. Do not touch the strings (rest your hand on them to mute them).";
            case Step::OpenString:  return "Play the OPEN " + name + " and let it ring.";
            case Step::TwelfthFret: return "Play the 12th FRET on the " + name + " and let it ring.";
            case Step::EveryNote:
                return (fret == 0 ? std::string ("Play the OPEN ") : "Play FRET " + std::to_string (fret) + " on the ") + name
                     + ". Let it ring for half a second. (Fret " + std::to_string (fret) + " of "
                     + std::to_string (cal.lastFret[(size_t) currentString()]) + ")";
            case Step::Chord:
                if (auto* ch = currentChord())
                {
                    std::string shape;
                    for (int s = (int) ch->frets.size() - 1; s >= 0; --s)
                        shape += (ch->frets[(size_t) s] < 0 ? std::string ("x") : std::to_string (ch->frets[(size_t) s])) + (s > 0 ? " " : "");
                    return "Strum " + ch->name + " 3 times (shape, low to high: " + shape
                         + " - shown on the fretboard). Let it ring a moment each time.";
                }
                return {};
            case Step::Soft:        return "Play the 5th fret on the " + name + " VERY SOFTLY, 4 times. Let each note fade a little.";
            case Step::Hard:        return "Play the 5th fret on the " + name + " HARD (your loudest normal picking), 4 times.";
            case Step::Done:        return "Calibration finished. The tracker now uses your guitar's tuning, noise level and picking strength.";
        }
        return {};
    }

    void CalibrationSession::clearMeasurement()
    {
        elapsed = progress = noteTime = 0.0;
        noteActive = false;
        pitches.clear();
        peaks.clear();
        timbres.clear();
        attacks.clear();
        chromas.clear();
        channelEnergy.fill (0.0f);
        strums = 0;
        chromasAtStrumStart = 0;
        for (auto& v : silence) v.clear();
    }

    void CalibrationSession::beginStep()
    {
        // Remember everything measured before this item, so back() can return to it exactly.
        history.resize ((size_t) stepIndex);
        history.push_back ({ cal, plan, spotLevels, softLevels, hardLevels, bleedSum, bleedCount });

        const auto& item = plan[(size_t) stepIndex];
        current = item.step;
        fret = item.step == Step::EveryNote ? item.fret : 0;
        listening = needDecision = false;
        clearMeasurement();

        // "Every note" in hex mode: how much a string leaks into the other inputs is averaged over its frets.
        if (current == Step::EveryNote && fret == 0)
        {
            bleedSum.fill (0.0);
            bleedCount.fill (0);
        }
        if (current == Step::Chord)
            chordIndex = item.string;
        if (current == Step::Done)
            finalise();
    }

    void CalibrationSession::finalise()
    {
        double sum = 0.0;
        int count = 0;
        for (int s = 0; s < inst.numStrings(); ++s)
            if (cal.strings[(size_t) s].measured) { sum += cal.strings[(size_t) s].centsOffset; ++count; }
        cal.globalCents = count > 0 ? sum / count : 0.0;
        cal.typicalLevel = spotLevels.empty() ? 0.0f : median (spotLevels);
        cal.valid = true;
        progress = 1.0;
    }

    void CalibrationSession::finishStep()
    {
        listening = false;
        if (stepIndex + 1 < (int) plan.size())
            ++stepIndex;
        beginStep();
    }

    void CalibrationSession::listen()
    {
        if (current == Step::Done) return;
        needDecision = false;
        clearMeasurement();
        listening = true;
        listenTime = 0.0;
        armed = false;
        quietSeen.fill (false);
        heardBefore.fill (false);
        attackEnvelope.fill (0.0f);
        say (current == Step::Silence ? "Listening to the silence..." : "Listening... play now.", true);
    }

    void CalibrationSession::stop()
    {
        if (! listening) return;
        listening = false;
        clearMeasurement();
        say ("Stopped. Press Listen when you are ready.", true);
    }

    void CalibrationSession::back()
    {
        if (stepIndex <= backLimit) return;
        --stepIndex;
        const auto h = history[(size_t) stepIndex];
        cal = h.cal;
        plan = h.plan;
        spotLevels = h.spotLevels;
        softLevels = h.softLevels;
        hardLevels = h.hardLevels;
        bleedSum = h.bleedSum;
        bleedCount = h.bleedCount;
        previousNote = -1;
        beginStep();
        say ("Back one step. Its result was forgotten: press Listen and play it again.", true);
    }

    void CalibrationSession::noMoreFrets()
    {
        if (current != Step::EveryNote) return;
        const int last = std::max (0, fret - 1);
        for (auto& lf : cal.lastFret) lf = last; // guitars have the same number of frets on every string

        // The frets past the end are not asked for on any string.
        for (int k = (int) plan.size() - 1; k >= stepIndex; --k)
            if (plan[(size_t) k].step == Step::EveryNote && plan[(size_t) k].fret > last)
                plan.erase (plan.begin() + k);
        listening = false;
        history.resize ((size_t) stepIndex); // beginStep() takes the snapshot of this item again
        beginStep();
        say ("OK: this guitar has " + std::to_string (last) + " frets.", true);
    }

    void CalibrationSession::accept()
    {
        if (! needDecision) return;

        if (current == Step::Chord)
        {
            if (auto* ch = currentChord())
            {
                auto t = *ch;
                t.chroma = meanChroma (chromas);
                cal.chords.push_back (t);
                say ("OK: " + ch->name + " kept.", true);
            }
            finishStep();
            return;
        }

        if (current == Step::EveryNote)
        {
            needDecision = false;
            forceAccept = true;
            evaluateEveryNote(); // stores the measurement even though it is far off
            forceAccept = false;
            return;
        }

        const int s = currentString();
        auto& sc = cal.strings[(size_t) s];
        if (current == Step::OpenString) { sc.centsOffset = pendingCents; sc.measured = true; }
        else                             { sc.intonationCents = pendingCents - (sc.measured ? sc.centsOffset : 0.0); }
        say ("OK: kept as it is.", true);
        finishStep();
    }

    void CalibrationSession::skip()
    {
        say (current == Step::EveryNote ? "Skipped fret " + std::to_string (fret) + "." : std::string ("Skipped."), true);
        finishStep();
    }

    // -----------------------------------------------------------------------
    void CalibrationSession::evaluatePitch()
    {
        listening = false;
        const int s = currentString();
        const double heard = hzToMidi (median (pitches));
        const int expected = expectedNote();
        const double cents = (heard - expected) * 100.0;
        const std::string heardName = midiNoteName ((int) std::lround (heard));

        auto& sc = cal.strings[(size_t) s];
        std::string warning;

        // In hex mode, the loudest input during this note is this string's channel.
        if (cal.hexMode)
        {
            int loudest = 0;
            for (int c = 1; c < inputs; ++c)
                if (channelEnergy[(size_t) c] > channelEnergy[(size_t) loudest]) loudest = c;
            sc.inputChannel = loudest;

            const float avgPeak = peaks.empty() ? 0.3f : median (peaks);
            sc.gain = std::clamp (0.3f / std::max (1e-4f, avgPeak), 0.25f, 8.0f);

            for (int other = 0; other < inst.numStrings(); ++other)
                if (other != s && cal.strings[(size_t) other].inputChannel == loudest && cal.strings[(size_t) other].measured)
                    warning = "Warning: input " + std::to_string (loudest + 1) + " was also loudest for another string. "
                              "Check the pickup cable / channel order. ";
        }

        if (std::abs (cents) <= kInTuneCents)
        {
            if (current == Step::OpenString) { sc.centsOffset = cents; sc.measured = true; }
            else                             sc.intonationCents = cents - (sc.measured ? sc.centsOffset : 0.0);

            std::ostringstream msg;
            msg.precision (0);
            msg << std::fixed << warning << "OK: heard " << heardName << ", " << std::abs (cents) << " cents "
                << (cents >= 0 ? "sharp" : "flat") << ".";
            say (msg.str(), warning.empty());
            finishStep();
            return;
        }

        // Maybe the player hit the wrong string?
        if (current == Step::OpenString)
            for (int other = 0; other < inst.numStrings(); ++other)
                if (other != s && std::abs ((heard - inst.tuning()[(size_t) other]) * 100.0) <= kInTuneCents)
                {
                    say ("That sounded like the " + stringName (other) + ". Press Listen and play the " + stringName (s) + ".", false);
                    clearMeasurement();
                    return;
                }

        pendingCents = cents;
        needDecision = true;
        std::ostringstream msg;
        msg.precision (0);
        msg << std::fixed << warning << "Heard " << heardName << ": " << std::abs (cents) << " cents " << (cents >= 0 ? "too high" : "too low")
            << ". Tune the string and press Again, or press 'Use it anyway' to calibrate to the way it is now.";
        say (msg.str(), false);
    }

    void CalibrationSession::evaluateEveryNote()
    {
        listening = false;
        const int s = currentString();
        const int expected = expectedNote();
        const double heard = pitches.empty() ? expected + pendingCents / 100.0 : hzToMidi (median (pitches));
        const double cents = (heard - expected) * 100.0;
        const bool accepted = forceAccept || pitches.empty() || std::abs (cents) <= kInTuneCents;

        if (! accepted)
        {
            // Wrong fret? (one or two frets away)
            const int heardNote = (int) std::lround (heard);
            const int heardFret = heardNote - inst.tuning()[(size_t) s];
            if (heardFret != fret && std::abs (heardNote - expected) <= 2 && std::abs (heard - heardNote) < 0.4)
            {
                say ("That was fret " + std::to_string (heardFret) + ". Press Listen and play fret " + std::to_string (fret) + ".", false);
                previousNote = heardNote;
                clearMeasurement();
                return;
            }

            pendingCents = cents;
            needDecision = true;
            std::ostringstream msg;
            msg.precision (0);
            msg << std::fixed << "Fret " << fret << " sounds " << std::abs (cents) << " cents " << (cents >= 0 ? "too high" : "too low")
                << ". Again, 'Use it anyway' (calibrate to it), or Skip this fret.";
            say (msg.str(), false);
            return;
        }

        // --- Store this spot ---
        auto& p = cal.positions[(size_t) s][(size_t) fret];
        p.measured = true;
        p.cents = (float) std::clamp (cents, -200.0, 200.0);
        p.level = peaks.empty() ? 0.0f : median (peaks);
        p.hasTimbre = ! timbres.empty();
        p.timbre = meanTimbre (timbres);
        p.hasAttack = ! attacks.empty();
        p.attack = {};
        for (const auto& a : attacks)
            for (int k = 0; k < kOvertones; ++k) p.attack[(size_t) k] += a[(size_t) k] / (float) attacks.size();
        if (p.level > 0.0f) spotLevels.push_back (p.level);

        auto& sc = cal.strings[(size_t) s];
        if (fret == 0)  { sc.centsOffset = cents; sc.measured = true; }
        if (fret == 12) sc.intonationCents = cents - (sc.measured ? sc.centsOffset : 0.0);

        if (cal.hexMode)
        {
            int loudest = 0;
            for (int c = 1; c < inputs; ++c)
                if (channelEnergy[(size_t) c] > channelEnergy[(size_t) loudest]) loudest = c;

            if (fret == 0)
            {
                sc.inputChannel = loudest;
                sc.gain = std::clamp (0.3f / std::max (1e-4f, p.level), 0.25f, 8.0f);
            }

            if (channelEnergy[(size_t) loudest] > 0.0f)
                for (int c = 0; c < inputs; ++c)
                {
                    bleedSum[(size_t) c] += channelEnergy[(size_t) c] / channelEnergy[(size_t) loudest];
                    ++bleedCount[(size_t) c];
                }
            // How much this string leaks into every input (average of its frets so far).
            for (int c = 0; c < inputs; ++c)
                if (bleedCount[(size_t) c] > 0)
                    cal.bleed[(size_t) s][(size_t) c] = (float) (bleedSum[(size_t) c] / bleedCount[(size_t) c]);
        }

        std::ostringstream msg;
        msg.precision (0);
        msg << std::fixed << "OK: fret " << fret << ", " << std::abs (cents) << " cents " << (cents >= 0 ? "sharp" : "flat") << ".";
        say (msg.str(), true);

        previousNote = expected;
        finishStep();
    }

    void CalibrationSession::feedChord (const AnalysisFrame& f, double hop)
    {
        const float gate = std::max (cal.noiseFloor * 6.0f, 0.003f);

        if (! noteActive && f.peak > gate)
        {
            noteActive = true;
            noteTime = 0.0;
            chromasAtStrumStart = chromas.size();
        }
        else if (noteActive)
        {
            noteTime += hop;
            if (f.hasFeatures && noteTime > kSkipAttack && noteTime < 0.6)
                chromas.push_back (f.chroma);

            if (f.rms < gate * 0.7f || noteTime > 1.2)
            {
                noteActive = false;
                if (chromas.size() > chromasAtStrumStart) // this strum gave fingerprints
                    ++strums;
            }
        }

        progress = (double) strums / kStrumsNeeded;
        if (strums < kStrumsNeeded || noteActive)
            return;

        listening = false;
        auto* ch = currentChord();
        if (ch == nullptr) { finishStep(); return; }

        // Does it sound like the chord? Compare with the notes of the shape.
        Chroma ideal {};
        for (int s = 0; s < (int) ch->frets.size(); ++s)
            if (ch->frets[(size_t) s] >= 0)
                ideal[(size_t) pitchClassOf (inst.tuning()[(size_t) s] + ch->frets[(size_t) s])] += 1.0f;
        std::vector<Chroma> one { ideal };
        ideal = meanChroma (one);

        const Chroma heard = meanChroma (chromas);
        if (chromaSimilarity (heard, ideal) < 0.6f)
        {
            needDecision = true;
            say ("This does not sound much like " + ch->name + ". Check the shape on the fretboard and press Again, "
                 "or press 'Use it anyway' to keep it.", false);
            return;
        }

        auto t = *ch;
        t.chroma = heard;
        cal.chords.push_back (t);
        say ("OK: " + ch->name + " learned.", true);
        finishStep();
    }

    bool CalibrationSession::newAttack (const AnalysisFrame& f)
    {
        // Per input: a pick attack = the peak jumps well above what was ringing, or sound comes after a quiet moment.
        // The first frame after Listen only learns what is ringing already.
        const auto c = (size_t) f.channel;
        const float gate = std::max (cal.noiseFloor * 6.0f, 0.003f);
        if (f.rms < gate * 0.7f) quietSeen[c] = true;
        const bool attack = f.peak > gate && (quietSeen[c] || (heardBefore[c] && f.peak > attackEnvelope[c] * 1.8f));
        attackEnvelope[c] = std::max (f.peak, attackEnvelope[c] * 0.93f);
        heardBefore[c] = true;
        return attack;
    }

    void CalibrationSession::feed (const AnalysisFrame& f, double hop)
    {
        if (! listening || current == Step::Done || needDecision || f.channel < 0 || f.channel >= inputs)
            return;

        // --- Silence: learn the noise level of every input ---
        if (current == Step::Silence)
        {
            silence[f.channel].push_back (f.rms);
            if (f.channel == 0) elapsed += hop;
            progress = elapsed / kSilenceSeconds;

            if (elapsed >= kSilenceSeconds)
            {
                cal.noiseFloor = 0.0f;
                for (int c = 0; c < inputs; ++c)
                {
                    auto v = silence[c];
                    std::sort (v.begin(), v.end());
                    const float p90 = v.empty() ? 0.001f : v[(size_t) ((double) v.size() * 0.9)];
                    cal.channelNoise[(size_t) c] = p90;
                    cal.noiseFloor = std::max (cal.noiseFloor, p90);
                }
                cal.noiseFloor = std::max (cal.noiseFloor, 1e-4f);
                say ("OK: noise level measured.", true);
                finishStep();
            }
            return;
        }

        // Nothing clear for a long time: stop, so the player can press Listen again when ready.
        if (f.channel == 0) listenTime += hop;
        if (listenTime > kListenTimeout && ! noteActive)
        {
            listening = false;
            clearMeasurement();
            say ("Nothing clear was heard. Press Listen and play again.", false);
            return;
        }

        // In mono mode only input 0 matters.
        if (! cal.hexMode && f.channel != 0)
            return;

        // A note still ringing from before does not count: wait for a new pick attack.
        if (! armed)
        {
            if (! newAttack (f)) return;
            armed = true;
        }

        // In hex mode, sum the energy per input to find the string's channel.
        if (cal.hexMode)
            channelEnergy[(size_t) f.channel] += f.rms;

        if (current == Step::Chord)
        {
            feedChord (f, hop);
            return;
        }

        // In hex mode, judge the note on the input that is loudest right now.
        if (cal.hexMode && f.rms < cal.gateOn())
            return;

        const float gate = std::max (cal.noiseFloor * 6.0f, 0.003f);

        // --- Open string / 12th fret / every note: collect stable pitch ---
        if (current == Step::OpenString || current == Step::TwelfthFret || current == Step::EveryNote)
        {
            const bool everyNote = current == Step::EveryNote;

            // "Every note": the last fret may still ring. Ignore it until a new note comes.
            if (everyNote && f.hz > 0.0f && f.clarity > 0.85f && (int) std::lround (hzToMidi (f.hz)) == previousNote)
                return;

            if (! noteActive)
            {
                if (f.rms > gate) { noteActive = true; noteTime = 0.0; }
                return;
            }

            noteTime += hop;
            if (f.rms < gate * 0.5f) { noteActive = false; return; }

            // The sound of the attack itself (what Poly hears when it decides), on the right note only.
            if (everyNote && f.hasOvertones && noteTime >= 0.04 && noteTime <= 0.07
                && std::abs (hzToMidi (f.hz) - expectedNote()) < 0.5)
                attacks.push_back (f.overtones);

            if (noteTime > kSkipAttack && f.clarity > 0.85f && f.hz > 0.0f)
            {
                pitches.push_back (f.hz);
                peaks.push_back (f.peak);
                if (f.hasFeatures) timbres.push_back (f.timbre);
            }

            const double needed = everyNote ? kSpotSeconds : kPitchSeconds;
            progress = std::min (1.0, (double) pitches.size() * hop / needed);
            if (progress >= 1.0)
            {
                if (everyNote) evaluateEveryNote();
                else           evaluatePitch();
            }
            return;
        }

        // --- Soft / hard: count plucks and keep each pluck's peak ---
        if (current == Step::Soft || current == Step::Hard)
        {
            if (! noteActive && f.peak > gate)
            {
                noteActive = true;
                noteTime = 0.0;
                peaks.push_back (f.peak);
            }
            else if (noteActive)
            {
                noteTime += hop;
                if (noteTime < 0.06 && ! peaks.empty())
                    peaks.back() = std::max (peaks.back(), f.peak); // the peak comes a few ms after the attack
                if (f.rms < gate * 0.7f || (noteTime > 0.25 && f.peak > peaks.back() * 1.6f))
                    noteActive = false; // ready for the next pluck
            }

            progress = (double) peaks.size() / kHitsNeeded;
            if ((int) peaks.size() >= kHitsNeeded && ! noteActive)
            {
                listening = false;
                const float level = median (peaks);
                const int s = currentString();
                if (current == Step::Soft)
                {
                    softLevels.push_back (level);
                    cal.softPeak = median (softLevels);
                    if (full && s >= 0 && s < kMaxStrings) cal.stringSoft[(size_t) s] = level;
                    say ("OK: soft picking measured.", true);
                }
                else
                {
                    const float soft = (full && s >= 0 && s < kMaxStrings && cal.stringSoft[(size_t) s] > 0.0f)
                                     ? cal.stringSoft[(size_t) s] : cal.softPeak;
                    hardLevels.push_back (std::max (level, soft * 1.5f));
                    cal.hardPeak = std::max (median (hardLevels), cal.softPeak * 1.5f);
                    if (full && s >= 0 && s < kMaxStrings) cal.stringHard[(size_t) s] = std::max (level, soft * 1.5f);
                    if (level < soft * 1.5f)
                        say ("Hard and soft sounded almost the same, so the velocity range will be small. "
                             "Press Back to try again.", false);
                    else
                        say ("OK: hard picking measured.", true);
                }
                finishStep();
            }
        }
    }
}
