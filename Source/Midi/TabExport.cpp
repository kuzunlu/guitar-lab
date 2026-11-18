#include "Midi/TabExport.h"
#include "Midi/ChordTimeline.h"
#include "Theory/Note.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <vector>

namespace gl
{
    std::string noteTabText (const NoteEvent& n)
    {
        std::string s;
        switch (n.technique)
        {
            case Technique::HammerOn:      s += "h"; break;
            case Technique::PullOff:       s += "p"; break;
            case Technique::Tap:           s += "t"; break;
            case Technique::PalmMute:      s += "PM"; break;
            case Technique::PinchHarmonic: s += "PH"; break;
            default: break;
        }

        if (n.technique == Technique::DeadNote)             s += "x";
        else if (n.technique == Technique::NaturalHarmonic) s += "<" + std::to_string (n.fret) + ">";
        else                                                s += std::to_string (n.fret);

        if (! n.bend.empty())
        {
            // To the nearest half step, like tab. Recorded bends are never exact, so "pre-bend" and "release" need a
            // clear change (the stored curve keeps the exact values).
            double peak = 0;
            for (auto& b : n.bend) peak = std::max (peak, b.semitones);
            const double shown = std::max (0.5, std::round (peak * 2.0) / 2.0);
            const bool release = n.bend.back().semitones < peak - 0.5;
            char amount[16];
            std::snprintf (amount, sizeof amount, shown == std::floor (shown) ? "%.0f" : "%.1f", shown);
            s += (n.bend.front().semitones > 0.3 ? "pb" : "b") + std::string (amount) + (release ? "r" : "");
        }

        switch (n.slide)
        {
            case SlideType::SlideTo:      s += (n.slideToFret >= n.fret ? "/" : "\\") + std::to_string (n.slideToFret); break;
            case SlideType::SlideInBelow: s = "/" + s; break;
            case SlideType::SlideInAbove: s = "\\" + s; break;
            case SlideType::SlideOutDown: s += "\\"; break;
            case SlideType::SlideOutUp:   s += "/"; break;
            case SlideType::None: break;
        }

        if (n.vibrato) s += "~";
        return s;
    }

    namespace
    {
        const char* kLegend = "h = hammer-on, p = pull-off, t = tap, PM = palm mute, x = dead note, <12> = natural harmonic, "
                              "PH = pinch harmonic, b2 = bend 2 half steps, pb = pre-bend, r = release, / \\ = slides, ~ = vibrato";

        // Where a note is written: notes of one strummed chord share the chord's first start (a column in tab).
        double writtenStart (const Phrase& p, const NoteEvent& n)
        {
            double start = n.start;
            if (n.group >= 0)
                for (const auto& m : p.notes)
                    if (m.group == n.group) start = std::min (start, m.start);
            return start;
        }

        // Slots per beat: 16ths, or 12 when a note really sits on a triplet (and not on a 16th).
        int slotsPerBeat (const Phrase& p)
        {
            for (const auto& n : p.notes)
            {
                const double b = writtenStart (p, n);
                const double off16 = std::abs (b * 4.0 - std::round (b * 4.0)) / 4.0;
                const double offTriplet = std::abs (b * 3.0 - std::round (b * 3.0)) / 3.0;
                if (offTriplet < 0.01 && off16 > 0.04) return 12;
            }
            return 4;
        }

        std::vector<std::string> stringNames (const Instrument& inst, bool flats)
        {
            std::vector<std::string> names;
            const int count = inst.numStrings();
            for (int s = 0; s < count; ++s)
                names.push_back (pitchClassName (pitchClassOf (inst.tuning()[(size_t) s]), flats));
            // Usual tab habit: the high E is written as a small "e".
            if (count > 1 && names.front() == names.back())
                for (auto& c : names.front()) c = (char) std::tolower ((unsigned char) c);
            return names;
        }

        std::string tuningText (const Instrument& inst, bool flats)
        {
            std::string t;
            for (int s = inst.numStrings() - 1; s >= 0; --s)
                t += (t.empty() ? "" : " ") + midiNoteName (inst.tuning()[(size_t) s], flats);
            return t;
        }

        // [bar][slot][string] -> text
        struct Grid
        {
            int perBeat = 4, perBar = 16, bars = 1, strings = 6;
            std::vector<std::vector<std::vector<std::string>>> cells;
        };

        Grid buildGrid (const Phrase& p, const Instrument& inst)
        {
            Grid g;
            g.perBeat = slotsPerBeat (p);
            g.perBar = g.perBeat * 4;
            g.strings = inst.numStrings();
            g.bars = std::max (1, (int) std::ceil (std::max (p.lengthBeats, p.endBeat()) / 4.0 - 1e-9));
            g.cells.assign ((size_t) g.bars, std::vector<std::vector<std::string>> ((size_t) g.perBar,
                                                                                      std::vector<std::string> ((size_t) g.strings)));
            for (const auto& n : p.notes)
            {
                if (n.string < 0 || n.string >= g.strings) continue;
                const int slot = (int) std::lround (writtenStart (p, n) * g.perBeat);
                const int bar = slot / g.perBar;
                if (bar < 0 || bar >= g.bars) continue;
                auto& cell = g.cells[(size_t) bar][(size_t) (slot % g.perBar)][(size_t) n.string];
                if (cell.empty()) cell = noteTabText (n);
            }
            return g;
        }

        std::string number (double v, int decimals = 2)
        {
            char buf[32];
            std::snprintf (buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }
    }

    // =======================================================================
    // Text tab
    // =======================================================================
    std::string phraseToTextTab (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info)
    {
        const auto g = buildGrid (phrase, inst);
        const auto names = stringNames (inst, info.flats);
        size_t nameW = 1;
        for (auto& n : names) nameW = std::max (nameW, n.size());

        const auto chords = info.chordNames ? chordTimeline (phrase, inst, info.flats) : std::vector<ChordSpan> {};
        std::ostringstream out;
        out << info.title << "\n"
            << "Tempo: " << (int) std::lround (info.bpm) << " BPM    Tuning (low to high): " << tuningText (inst, info.flats) << "\n\n";

        const int perLine = std::max (1, info.barsPerLine);
        for (int first = 0; first < g.bars; first += perLine)
        {
            const int last = std::min (g.bars, first + perLine);

            // Column widths: the widest text in each slot plus one dash; empty steps are one dash.
            std::vector<std::vector<size_t>> widths;
            for (int b = first; b < last; ++b)
            {
                std::vector<size_t> w ((size_t) g.perBar, 1);
                for (int k = 0; k < g.perBar; ++k)
                    for (int s = 0; s < g.strings; ++s)
                        w[(size_t) k] = std::max (w[(size_t) k], g.cells[(size_t) b][(size_t) k][(size_t) s].size() + 1);
                widths.push_back (w);
            }

            // Bar numbers above the bars
            std::string numbers (nameW + 1, ' ');
            for (int b = first; b < last; ++b)
            {
                size_t barLen = 0;
                for (auto w : widths[(size_t) (b - first)]) barLen += w;
                std::string label = std::to_string (b + 1);
                label.resize (barLen + 1, ' ');
                numbers += label;
            }
            out << numbers << "\n";

            // Chord names, each above the step where its chord starts
            std::string chordLine (numbers.size() + 8, ' ');
            bool anyChord = false;
            size_t pos = nameW + 1;
            for (int b = first; b < last; ++b)
            {
                for (int k = 0; k < g.perBar; ++k)
                {
                    const double beat = b * 4.0 + (double) k / g.perBeat;
                    for (const auto& c : chords)
                        if (std::abs (c.start - beat) < 1e-6)
                        {
                            if (chordLine.size() < pos + c.name.size()) chordLine.resize (pos + c.name.size(), ' ');
                            chordLine.replace (pos, c.name.size(), c.name);
                            anyChord = true;
                        }
                    pos += widths[(size_t) (b - first)][(size_t) k];
                }
                pos += 1;
            }
            if (anyChord)
            {
                chordLine.erase (chordLine.find_last_not_of (' ') + 1);
                out << chordLine << "\n";
            }

            for (int s = 0; s < g.strings; ++s)
            {
                std::string line = names[(size_t) s];
                line.resize (nameW, ' ');
                line += "|";
                for (int b = first; b < last; ++b)
                {
                    for (int k = 0; k < g.perBar; ++k)
                    {
                        auto cell = g.cells[(size_t) b][(size_t) k][(size_t) s];
                        cell.resize (widths[(size_t) (b - first)][(size_t) k], '-');
                        line += cell;
                    }
                    line += "|";
                }
                out << line << "\n";
            }
            out << "\n";
        }
        out << kLegend << "\n";
        return out.str();
    }

    // =======================================================================
    // PDF
    // =======================================================================
    namespace
    {
        // A tiny PDF writer: standard Helvetica fonts, lines, white boxes and text. Enough for tab pages.
        class PdfWriter
        {
        public:
            static constexpr double W = 595.0, H = 842.0; // A4 in points

            void newPage() { pages.emplace_back(); }

            void text (double x, double y, double size, bool bold, const std::string& s)
            {
                page() += "BT /" + std::string (bold ? "F2 " : "F1 ") + number (size) + " Tf " + number (x) + " " + number (y)
                        + " Td (" + escape (s) + ") Tj ET\n";
            }
            void line (double x1, double y1, double x2, double y2, double width, double grey = 0.0)
            {
                page() += number (grey) + " G " + number (width) + " w " + number (x1) + " " + number (y1) + " m "
                        + number (x2) + " " + number (y2) + " l S\n";
            }
            void whiteBox (double x, double y, double w, double h)
            {
                page() += "1 g " + number (x) + " " + number (y) + " " + number (w) + " " + number (h) + " re f 0 g\n";
            }
            static double textWidth (const std::string& s, double size) { return (double) s.size() * 0.56 * size; }

            std::string finish() const
            {
                std::string out = "%PDF-1.4\n";
                std::vector<size_t> offsets;
                auto object = [&] (const std::string& body)
                {
                    offsets.push_back (out.size());
                    out += std::to_string (offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
                };

                const int count = (int) pages.size();
                std::string kids;
                for (int i = 0; i < count; ++i) kids += std::to_string (5 + 2 * i) + " 0 R ";

                object ("<< /Type /Catalog /Pages 2 0 R >>");
                object ("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string (count) + " >>");
                object ("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
                object ("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
                for (int i = 0; i < count; ++i)
                {
                    object ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> "
                            "/Contents " + std::to_string (6 + 2 * i) + " 0 R >>");
                    const auto& c = pages[(size_t) i];
                    object ("<< /Length " + std::to_string (c.size()) + " >>\nstream\n" + c + "endstream");
                }

                const size_t xref = out.size();
                out += "xref\n0 " + std::to_string (offsets.size() + 1) + "\n0000000000 65535 f \n";
                for (auto o : offsets)
                {
                    char buf[24];
                    std::snprintf (buf, sizeof buf, "%010zu 00000 n \n", o);
                    out += buf;
                }
                out += "trailer\n<< /Size " + std::to_string (offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
                     + std::to_string (xref) + "\n%%EOF\n";
                return out;
            }

        private:
            std::string& page() { if (pages.empty()) newPage(); return pages.back(); }
            static std::string escape (const std::string& s)
            {
                std::string e;
                for (char c : s)
                {
                    if (c == '(' || c == ')' || c == '\\') e += '\\';
                    e += c;
                }
                return e;
            }
            std::vector<std::string> pages;
        };
    }

    std::string phraseToPdf (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info)
    {
        const auto g = buildGrid (phrase, inst);
        const auto names = stringNames (inst, info.flats);
        PdfWriter pdf;

        const double left = 50.0, right = PdfWriter::W - 40.0, bottom = 60.0;
        const double lineGap = 8.0;                         // between tab lines
        const double tabX = left + 16.0;
        const double avail = right - tabX;
        const double systemH = 36.0 + (g.strings - 1) * lineGap + 22.0;
        const auto chords = info.chordNames ? chordTimeline (phrase, inst, info.flats) : std::vector<ChordSpan> {};
        const double fontSize = 7.5;
        const double minSlot = g.perBar > 16 ? 3.6 : 5.5;   // an empty 16th (or triplet) step

        // Text in the PDF: palm mute is written above the staff ("P.M. - - -"), so not in the note itself.
        auto cellText = [&] (int b, int k, int s)
        {
            std::string t = g.cells[(size_t) b][(size_t) k][(size_t) s];
            if (t.rfind ("PM", 0) == 0) t = t.substr (2);
            return t;
        };
        auto isPalmMuted = [&] (int b, int k, int s) { return g.cells[(size_t) b][(size_t) k][(size_t) s].rfind ("PM", 0) == 0; };

        // Every step of a bar is as wide as its widest note (busy bars get more room), like a printed tab book.
        std::vector<std::vector<double>> colW ((size_t) g.bars);
        std::vector<double> barNatural ((size_t) g.bars, 0.0);
        for (int b = 0; b < g.bars; ++b)
        {
            colW[(size_t) b].assign ((size_t) g.perBar, minSlot);
            for (int k = 0; k < g.perBar; ++k)
                for (int s = 0; s < g.strings; ++s)
                {
                    const auto t = cellText (b, k, s);
                    if (! t.empty())
                        colW[(size_t) b][(size_t) k] = std::max (colW[(size_t) b][(size_t) k], PdfWriter::textWidth (t, fontSize) + 3.0);
                }
            barNatural[(size_t) b] = 6.0;
            for (double w : colW[(size_t) b]) barNatural[(size_t) b] += w;
        }

        int pageNo = 0;
        double y = 0.0;
        auto startPage = [&]
        {
            pdf.newPage();
            ++pageNo;
            y = PdfWriter::H - 56.0;
            if (pageNo == 1)
            {
                pdf.text (left, y, 18.0, true, info.title);
                y -= 18.0;
                pdf.text (left, y, 9.5, false, "Tempo: " + std::to_string ((int) std::lround (info.bpm)) + " BPM     Tuning (low to high): "
                                               + tuningText (inst, info.flats) + "     Made with Guitar Lab");
                y -= 16.0;
            }
            const auto label = "Page " + std::to_string (pageNo);
            pdf.text (PdfWriter::W / 2 - PdfWriter::textWidth (label, 8.0) / 2, 30.0, 8.0, false, label);
        };
        startPage();

        for (int first = 0; first < g.bars;)
        {
            // As many bars as fit (at most barsPerLine), then stretch them to fill the line.
            int last = first;
            double natural = 0.0;
            while (last < g.bars && last - first < std::max (1, info.barsPerLine)
                   && (last == first || natural + barNatural[(size_t) last] <= avail))
                natural += barNatural[(size_t) last++];
            const bool finalLine = last == g.bars;
            const double scale = std::max (finalLine ? std::min (1.6, avail / natural) : avail / natural, 0.2);

            if (y - systemH < bottom) startPage();
            const double top = y - 36.0;                     // highest string's line
            const double bottomLine = top - (g.strings - 1) * lineGap;
            const double end = tabX + natural * scale;

            for (int s = 0; s < g.strings; ++s)
            {
                const double ly = top - s * lineGap;
                pdf.line (tabX, ly, end, ly, 0.5, 0.45);
                pdf.text (left, ly - 2.5, 7.0, true, names[(size_t) s]);
            }
            pdf.line (tabX, top, tabX, bottomLine, 0.8);

            struct Mark { double x, endX; double beat; };
            std::vector<Mark> palmMutes;

            double bx = tabX;
            for (int b = first; b < last; ++b)
            {
                const double bw = barNatural[(size_t) b] * scale;
                pdf.line (bx + bw, top, bx + bw, bottomLine, b == g.bars - 1 ? 2.0 : 0.8);
                pdf.text (bx + 2.0, top + 4.0, 6.5, false, std::to_string (b + 1));

                double x = bx + 3.0 * scale;
                for (int k = 0; k < g.perBar; ++k)
                {
                    const double beat = b * 4.0 + (double) k / g.perBeat;
                    for (const auto& c : chords)
                        if (std::abs (c.start - beat) < 1e-6)
                            pdf.text (x, top + 22.0, 9.0, true, c.name);   // chord name, above the bar numbers

                    bool pm = false;
                    double widest = 0.0;
                    for (int s = 0; s < g.strings; ++s)
                    {
                        const auto t = cellText (b, k, s);
                        if (t.empty()) continue;
                        const double ly = top - s * lineGap;
                        const double w = PdfWriter::textWidth (t, fontSize) + 1.5;
                        pdf.whiteBox (x - 0.75, ly - fontSize * 0.45, w, fontSize * 0.9);
                        pdf.text (x, ly - fontSize * 0.35, fontSize, true, t);
                        pm = pm || isPalmMuted (b, k, s);
                        widest = std::max (widest, w);
                    }
                    if (pm) palmMutes.push_back ({ x, x + widest, b * 4.0 + (double) k / g.perBeat });
                    x += colW[(size_t) b][(size_t) k] * scale;
                }
                bx += bw;
            }

            // "P.M. - - -" above the staff over each run of palm-muted notes (gaps of a beat or less).
            for (size_t i = 0; i < palmMutes.size();)
            {
                size_t j = i + 1;
                while (j < palmMutes.size() && palmMutes[j].beat - palmMutes[j - 1].beat <= 1.0 + 1e-6) ++j;
                const double x0 = palmMutes[i].x, x1 = palmMutes[j - 1].endX;
                const double my = top + 12.0;
                pdf.text (x0, my, 6.5, true, "P.M.");
                for (double dx = x0 + PdfWriter::textWidth ("P.M.", 6.5) + 3.0; dx + 2.0 <= x1; dx += 5.0)
                    pdf.line (dx, my + 2.0, dx + 2.5, my + 2.0, 0.6);
                i = j;
            }

            y -= systemH;
            first = last;
        }

        // Legend, wrapped to the page width.
        std::vector<std::string> lines;
        std::string current;
        std::istringstream words (kLegend);
        std::string word;
        while (words >> word)
        {
            const auto test = current.empty() ? word : current + " " + word;
            if (PdfWriter::textWidth (test, 6.5) > right - left && ! current.empty()) { lines.push_back (current); current = word; }
            else current = test;
        }
        if (! current.empty()) lines.push_back (current);
        if (y - 10.0 * (double) lines.size() - 10.0 < bottom) startPage();
        for (const auto& l : lines)
        {
            pdf.text (left, y - 6.0, 6.5, false, l);
            y -= 9.0;
        }
        return pdf.finish();
    }

    // =======================================================================
    // MusicXML
    // =======================================================================
    namespace
    {
        constexpr int kDivisions = 12; // per beat: fits 16ths and triplets

        struct Piece { int ticks; const char* type; bool dot; bool triplet; };
        const Piece kPieces[] = {
            { 48, "whole", false, false }, { 36, "half", true, false }, { 24, "half", false, false },
            { 18, "quarter", true, false }, { 12, "quarter", false, false }, { 9, "eighth", true, false },
            { 6, "eighth", false, false }, { 4, "eighth", false, true }, { 3, "16th", false, false },
            { 2, "16th", false, true }, { 1, "32nd", false, true },
        };

        std::vector<Piece> split (int ticks)
        {
            std::vector<Piece> out;
            while (ticks > 0)
                for (const auto& p : kPieces)
                    if (p.ticks <= ticks) { out.push_back (p); ticks -= p.ticks; break; }
            return out;
        }

        std::string xmlEscape (const std::string& s)
        {
            std::string e;
            for (char c : s)
                e += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string (1, c);
            return e;
        }

        std::string pitchXml (int midi, bool flats)
        {
            static const char* steps[] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B" };
            static const int sharpAlter[] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
            static const char* flatSteps[] = { "C", "D", "D", "E", "E", "F", "G", "G", "A", "A", "B", "B" };
            const int pc = ((midi % 12) + 12) % 12;
            const int octave = midi / 12 - 1;
            const std::string step = flats ? flatSteps[pc] : steps[pc];
            const int alter = sharpAlter[pc] ? (flats ? -1 : 1) : 0;
            return "<pitch><step>" + step + "</step>" + (alter ? "<alter>" + std::to_string (alter) + "</alter>" : "")
                 + "<octave>" + std::to_string (octave) + "</octave></pitch>";
        }

        // A chord symbol for MuseScore / Guitar Pro: <harmony> with root, kind and bass.
        std::string harmonyXml (const std::string& name, int offset)
        {
            size_t i = 0;
            auto readStep = [&] (std::string& step, int& alter)
            {
                step = name.substr (i, 1);
                ++i;
                alter = 0;
                while (i < name.size() && (name[i] == '#' || name[i] == 'b')) alter += name[i++] == '#' ? 1 : -1;
            };
            std::string step, bassStep;
            int alter = 0, bassAlter = 0;
            readStep (step, alter);
            std::string rest = name.substr (i);
            const auto slash = rest.rfind ('/');
            bool hasBass = false;
            if (slash != std::string::npos && slash + 1 < rest.size() && std::isupper ((unsigned char) rest[slash + 1]))
            {
                const std::string b = rest.substr (slash + 1);
                rest = rest.substr (0, slash);
                size_t j = 0;
                bassStep = b.substr (0, 1);
                for (j = 1; j < b.size(); ++j) bassAlter += b[j] == '#' ? 1 : (b[j] == 'b' ? -1 : 0);
                hasBass = true;
            }
            static const std::pair<const char*, const char*> kinds[] = {
                { "", "major" }, { "m", "minor" }, { "7", "dominant" }, { "maj7", "major-seventh" }, { "m7", "minor-seventh" },
                { "dim", "diminished" }, { "dim7", "diminished-seventh" }, { "m7b5", "half-diminished" }, { "aug", "augmented" },
                { "sus2", "suspended-second" }, { "sus4", "suspended-fourth" }, { "6", "major-sixth" }, { "m6", "minor-sixth" },
                { "9", "dominant-ninth" }, { "maj9", "major-ninth" }, { "m9", "minor-ninth" }, { "11", "dominant-11th" },
                { "m11", "minor-11th" }, { "13", "dominant-13th" }, { "maj13", "major-13th" }, { "m13", "minor-13th" },
                { "5", "power" }, { "m(maj7)", "major-minor" }, { "7#5", "augmented-seventh" },
            };
            std::string kind = "other";
            for (const auto& k : kinds) if (rest == k.first) kind = k.second;

            std::string x = "<harmony><root><root-step>" + step + "</root-step>"
                          + (alter ? "<root-alter>" + std::to_string (alter) + "</root-alter>" : "") + "</root>"
                          + "<kind text=\"" + xmlEscape (rest) + "\">" + kind + "</kind>";
            if (hasBass)
                x += "<bass><bass-step>" + bassStep + "</bass-step>" + (bassAlter ? "<bass-alter>" + std::to_string (bassAlter) + "</bass-alter>" : "") + "</bass>";
            if (offset > 0) x += "<offset>" + std::to_string (offset) + "</offset>";
            return x + "</harmony>";
        }

        struct XmlNote
        {
            bool rest = false, chord = false, tieStart = false, tieStop = false;
            int midi = 0, string = 1, fret = 0;
            Piece piece {};
            const NoteEvent* source = nullptr;
            std::string hpStart, hpStop;  // "hammer-on" / "pull-off"
            bool first = true;            // first piece of its note (techniques go here)
        };
    }

    namespace
    {
        // The phrase as bars of notes and rests (shared by MusicXML and Guitar Pro): one "chord" per onset, each with a
        // standard length; long notes are split into tied pieces; hammer-ons / pull-offs marked on both notes.
        std::vector<std::vector<XmlNote>> layoutBars (const Phrase& phrase, const Instrument& inst, int& barsOut)
        {
            const int strings = inst.numStrings();
            const int measureTicks = 4 * kDivisions;
            const int bars = std::max (1, (int) std::ceil (std::max (phrase.lengthBeats, phrase.endBeat()) / 4.0 - 1e-9));
            barsOut = bars;

            const int perBeat = slotsPerBeat (phrase);
            std::vector<std::vector<XmlNote>> measures ((size_t) bars);

            for (int m = 0; m < bars; ++m)
            {
                std::vector<std::pair<int, const NoteEvent*>> starts;
                for (const auto& n : phrase.notes)
                {
                    if (n.string < 0 || n.string >= strings) continue;
                    // Same grid as the tab (16ths, or triplets): small timing differences do not make tiny notes.
                    const int tick = (int) std::lround (writtenStart (phrase, n) * perBeat) * (kDivisions / perBeat);
                    if (tick >= m * measureTicks && tick < (m + 1) * measureTicks)
                        starts.push_back ({ tick - m * measureTicks, &n });
                }
                std::sort (starts.begin(), starts.end(), [] (auto& a, auto& b) { return a.first < b.first || (a.first == b.first && a.second->string > b.second->string); });

                auto& out = measures[(size_t) m];
                int at = 0;
                for (size_t i = 0; i < starts.size();)
                {
                    const int onset = starts[i].first;
                    size_t j = i;
                    while (j < starts.size() && starts[j].first == onset) ++j;
                    if (onset > at)
                        for (auto pc : split (onset - at)) { XmlNote r; r.rest = true; r.piece = pc; out.push_back (r); }
                    const int next = j < starts.size() ? starts[j].first : measureTicks;
                    const auto pieces = split (std::max (1, next - onset));

                    for (size_t k = 0; k < pieces.size(); ++k)
                    {
                        bool firstInChord = true;
                        for (size_t q = i; q < j; ++q)
                        {
                            const auto* n = starts[q].second;
                            if (q > i && n->string == starts[q - 1].second->string) continue; // one note per string
                            XmlNote x;
                            x.chord = ! firstInChord;
                            firstInChord = false;
                            x.midi = soundingNote (*n, inst);
                            x.string = n->string + 1;
                            x.fret = n->fret;
                            x.piece = pieces[k];
                            x.source = n;
                            x.first = k == 0;
                            x.tieStart = k + 1 < pieces.size();
                            x.tieStop = k > 0;
                            out.push_back (x);
                        }
                    }
                    at = next;
                    i = j;
                }
                if (starts.empty())
                {
                    XmlNote r;
                    r.rest = true;
                    r.piece = { measureTicks, "whole", false, false };
                    out.push_back (r);
                }
            }

            // Hammer-ons / pull-offs: the slur starts on the note before, on the same string.
            std::vector<XmlNote*> lastOnString ((size_t) strings, nullptr);
            for (auto& m : measures)
                for (auto& x : m)
                {
                    if (x.rest || x.source == nullptr) continue;
                    if (x.first && (x.source->technique == Technique::HammerOn || x.source->technique == Technique::PullOff))
                        if (auto* before = lastOnString[(size_t) (x.string - 1)])
                        {
                            const std::string kind = x.source->technique == Technique::HammerOn ? "hammer-on" : "pull-off";
                            before->hpStart = kind;
                            x.hpStop = kind;
                        }
                    lastOnString[(size_t) (x.string - 1)] = &x;
                }
            return measures;
        }
    }

    std::string phraseToMusicXml (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info)
    {
        const int strings = inst.numStrings();
        int bars = 1;
        const auto measures = layoutBars (phrase, inst, bars);
        const auto chords = info.chordNames ? chordTimeline (phrase, inst, info.flats) : std::vector<ChordSpan> {};

        std::ostringstream o;
        o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
             "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n"
             "<score-partwise version=\"3.1\">\n"
             "  <work><work-title>" << xmlEscape (info.title) << "</work-title></work>\n"
             "  <identification><encoding><software>Guitar Lab</software></encoding></identification>\n"
             "  <part-list><score-part id=\"P1\"><part-name>Guitar</part-name></score-part></part-list>\n"
             "  <part id=\"P1\">\n";

        for (int m = 0; m < bars; ++m)
        {
            o << "    <measure number=\"" << (m + 1) << "\">\n";
            if (m == 0)
            {
                o << "      <attributes><divisions>" << kDivisions << "</divisions><key><fifths>0</fifths></key>"
                     "<time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>TAB</sign><line>5</line></clef>"
                     "<staff-details><staff-lines>" << strings << "</staff-lines>";
                for (int line = 1; line <= strings; ++line) // line 1 = lowest string
                {
                    const int open = inst.tuning()[(size_t) (strings - line)];
                    const auto p = pitchXml (open, info.flats);
                    std::string step = p.substr (p.find ("<step>") + 6, 1);
                    o << "<staff-tuning line=\"" << line << "\"><tuning-step>" << step << "</tuning-step>";
                    if (p.find ("<alter>") != std::string::npos)
                        o << "<tuning-alter>" << (info.flats ? -1 : 1) << "</tuning-alter>";
                    o << "<tuning-octave>" << (open / 12 - 1) << "</tuning-octave></staff-tuning>";
                }
                o << "</staff-details></attributes>\n"
                     "      <direction placement=\"above\"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>"
                  << (int) std::lround (info.bpm) << "</per-minute></metronome></direction-type><sound tempo=\""
                  << (int) std::lround (info.bpm) << "\"/></direction>\n";
            }

            for (const auto& c : chords)
                if (c.start >= m * 4.0 - 1e-9 && c.start < (m + 1) * 4.0 - 1e-9)
                    o << "      " << harmonyXml (c.name, (int) std::lround ((c.start - m * 4.0) * kDivisions)) << "\n";

            for (const auto& x : measures[(size_t) m])
            {
                o << "      <note>";
                if (x.chord) o << "<chord/>";
                if (x.rest) o << "<rest/>";
                else        o << pitchXml (x.midi, info.flats);
                o << "<duration>" << x.piece.ticks << "</duration>";
                if (x.tieStop)  o << "<tie type=\"stop\"/>";
                if (x.tieStart) o << "<tie type=\"start\"/>";
                o << "<voice>1</voice><type>" << x.piece.type << "</type>";
                if (x.piece.dot) o << "<dot/>";
                if (x.piece.triplet) o << "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>";
                if (! x.rest && x.source->technique == Technique::DeadNote) o << "<notehead>x</notehead>";
                if (! x.rest)
                {
                    o << "<notations>";
                    if (x.tieStop)  o << "<tied type=\"stop\"/>";
                    if (x.tieStart) o << "<tied type=\"start\"/>";
                    o << "<technical><string>" << x.string << "</string><fret>" << x.fret << "</fret>";
                    if (! x.hpStop.empty())  o << "<" << x.hpStop << " type=\"stop\"/>";
                    if (! x.hpStart.empty()) o << "<" << x.hpStart << " type=\"start\">" << (x.hpStart == "hammer-on" ? "H" : "P") << "</" << x.hpStart << ">";
                    if (x.first)
                    {
                        const auto& n = *x.source;
                        if (n.technique == Technique::NaturalHarmonic) o << "<harmonic><natural/></harmonic>";
                        if (n.technique == Technique::Tap) o << "<tap/>";
                        if (n.technique == Technique::PalmMute) o << "<other-technical>P.M.</other-technical>";
                        if (! n.bend.empty())
                        {
                            double peak = 0;
                            for (auto& b : n.bend) peak = std::max (peak, b.semitones);
                            o << "<bend><bend-alter>" << number (std::round (peak * 2.0) / 2.0, 1) << "</bend-alter>"
                              << (n.bend.back().semitones < peak - 0.5 ? "<release/>" : "") << "</bend>";
                        }
                    }
                    o << "</technical>";
                    if (x.first && x.source->vibrato) o << "<ornaments><other-ornament>vibrato</other-ornament></ornaments>";
                    o << "</notations>";
                }
                o << "</note>\n";
            }
            o << "    </measure>\n";
        }
        o << "  </part>\n</score-partwise>\n";
        return o.str();
    }
    // =======================================================================
    // Guitar Pro 3 (.gp3)
    // =======================================================================
    namespace
    {
        // Little-endian writer for the Guitar Pro file format.
        struct GpWriter
        {
            std::string out;
            void u8 (int v)  { out += (char) (unsigned char) (v & 0xFF); }
            void i8 (int v)  { u8 (v); }
            void boolean (bool b) { u8 (b ? 1 : 0); }
            void i32 (int v)
            {
                const auto u = (uint32_t) v;
                for (int k = 0; k < 4; ++k) u8 ((int) ((u >> (8 * k)) & 0xFF));
            }
            void zeros (int n) { for (int k = 0; k < n; ++k) u8 (0); }
            // 1 byte length, then exactly `size` bytes (padded with 0).
            void byteSizeString (std::string text, int size)
            {
                if ((int) text.size() > size) text.resize ((size_t) size);
                u8 ((int) text.size());
                out += text;
                zeros (size - (int) text.size());
            }
            // int (length + 1), byte (length), text.
            void intByteSizeString (std::string text)
            {
                if (text.size() > 255) text.resize (255);
                i32 ((int) text.size() + 1);
                u8 ((int) text.size());
                out += text;
            }
        };

        int gpDurationCode (const Piece& p)
        {
            const std::string t = p.type;
            return t == "whole" ? -2 : t == "half" ? -1 : t == "quarter" ? 0 : t == "eighth" ? 1 : t == "16th" ? 2 : 3;
        }
    }

    std::string phraseToGuitarPro3 (const Phrase& phrase, const Instrument& inst, const TabExportInfo& info)
    {
        const int strings = std::min (inst.numStrings(), 7); // GP3 knows up to 7 strings
        int bars = 1;
        const auto measures = layoutBars (phrase, inst, bars);
        GpWriter w;

        w.byteSizeString ("FICHIER GUITAR PRO v3.00", 30);

        // Song information: title, subtitle, artist, album, author, copyright, tab by, instructions; no notice lines.
        for (const auto& text : { info.title, std::string(), std::string(), std::string(), std::string(), std::string(),
                                  std::string ("Guitar Lab"), std::string() })
            w.intByteSizeString (text);
        w.i32 (0);

        w.boolean (false);                          // triplet feel
        w.i32 ((int) std::lround (info.bpm));       // tempo
        w.i32 (0);                                  // key: C

        // 64 MIDI channels: program, then volume, balance, chorus, reverb, phaser, tremolo (in GP's short form).
        for (int c = 0; c < 64; ++c)
        {
            w.i32 (c < 2 ? 27 : 0);                 // channel 1/2: clean electric guitar (GM program 28)
            const int volume = 104, balance = 64;
            auto shortForm = [] (int v) { return std::min (std::max ((v >> 3) - 1, -128), 127) + 1; };
            w.i8 (shortForm (volume));
            w.i8 (shortForm (balance));
            w.i8 (0); w.i8 (0); w.i8 (0); w.i8 (0);
            w.zeros (2);
        }

        w.i32 (bars);                               // measures
        w.i32 (1);                                  // tracks

        // Measure headers: 4/4 on the first bar, nothing changes after.
        for (int m = 0; m < bars; ++m)
        {
            if (m == 0) { w.u8 (0x01 | 0x02); w.i8 (4); w.i8 (4); }
            else          w.u8 (0);
        }

        // The track
        w.u8 (0);
        w.byteSizeString ("Guitar", 40);
        w.i32 (strings);
        for (int s = 0; s < 7; ++s)                 // string 1 (the highest) first, MIDI note numbers
            w.i32 (s < strings ? inst.tuning()[(size_t) s] : 0);
        w.i32 (1);                                  // MIDI port
        w.i32 (1);                                  // channel 1
        w.i32 (2);                                  // effects channel 2
        w.i32 (kNumFrets);
        w.i32 (0);                                  // capo
        w.u8 (255); w.u8 (0); w.u8 (0); w.zeros (1); // colour

        // Measures: beats (one per onset; tied pieces for long notes)
        for (int m = 0; m < bars; ++m)
        {
            const auto& notes = measures[(size_t) m];
            // Group the notes into beats: a beat starts at every note that is not "chord".
            std::vector<std::pair<size_t, size_t>> beats;
            for (size_t i = 0; i < notes.size();)
            {
                size_t j = i + 1;
                while (j < notes.size() && notes[j].chord) ++j;
                beats.push_back ({ i, j });
                i = j;
            }
            w.i32 ((int) beats.size());

            for (auto [a, b] : beats)
            {
                const auto& head = notes[a];
                bool vibrato = false, harmonic = false;
                for (size_t k = a; k < b; ++k)
                    if (! notes[k].rest && notes[k].first)
                    {
                        vibrato = vibrato || notes[k].source->vibrato;
                        harmonic = harmonic || notes[k].source->technique == Technique::NaturalHarmonic;
                    }

                int flags = 0;
                if (head.piece.dot) flags |= 0x01;
                if (vibrato || harmonic) flags |= 0x08;
                if (head.piece.triplet) flags |= 0x20;
                if (head.rest) flags |= 0x40;
                w.u8 (flags);
                if (head.rest) w.u8 (2);            // rest
                w.i8 (gpDurationCode (head.piece));
                if (head.piece.triplet) w.i32 (3);
                if (flags & 0x08) w.u8 ((vibrato ? 0x01 : 0) | (harmonic ? 0x04 : 0));

                // Notes: which strings sound (bit 7 - string number), then each note, string 1 first.
                std::vector<const XmlNote*> chord;
                if (! head.rest)
                    for (size_t k = a; k < b; ++k)
                        if (notes[k].string <= strings) chord.push_back (&notes[k]);
                std::sort (chord.begin(), chord.end(), [] (auto* x, auto* y) { return x->string < y->string; });
                int stringFlags = 0;
                for (auto* x : chord) stringFlags |= 1 << (7 - x->string);
                w.u8 (stringFlags);

                for (auto* x : chord)
                {
                    const auto& n = *x->source;
                    const bool tied = x->tieStop;
                    const bool dead = n.technique == Technique::DeadNote;
                    const bool bend = x->first && ! n.bend.empty();
                    const bool hammer = ! x->hpStart.empty();
                    const bool slide = x->first && n.slide != SlideType::None;
                    const bool ring = n.letRing;
                    const bool effects = bend || hammer || slide || ring;

                    w.u8 (0x20 | (effects ? 0x08 : 0));
                    w.u8 (tied ? 2 : dead ? 3 : 1);  // normal, tied, dead
                    w.i8 (tied ? 0 : n.fret);
                    if (effects)
                    {
                        w.u8 ((bend ? 0x01 : 0) | (hammer ? 0x02 : 0) | (slide ? 0x04 : 0) | (ring ? 0x08 : 0));
                        if (bend)
                        {
                            double peak = 0.0;
                            for (const auto& bp : n.bend) peak = std::max (peak, bp.semitones);
                            w.i8 (1);                                     // bend
                            w.i32 ((int) std::lround (peak * 50.0));      // 100 = a whole step
                            w.i32 ((int) n.bend.size() + (n.bend.front().position > 0.0 ? 1 : 0));
                            if (n.bend.front().position > 0.0) { w.i32 (0); w.i32 (0); w.boolean (false); }
                            for (const auto& bp : n.bend)
                            {
                                w.i32 ((int) std::lround (bp.position * 60.0));   // 0..60 over the note
                                w.i32 ((int) std::lround (bp.semitones * 25.0));  // 25 = a half step
                                w.boolean (false);
                            }
                        }
                    }
                }
            }
        }
        w.i32 (0);
        return w.out;
    }
}
