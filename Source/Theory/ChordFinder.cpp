#include "Theory/ChordFinder.h"
#include "Theory/Note.h"
#include "Theory/Trainer.h" // intervalName

#include <algorithm>
#include <array>
#include <cctype>

namespace gl
{
    namespace
    {
        // ------------------------------------------------------------------
        // Spelling: every chord tone gets the letter of its degree, so C7#9 has a D# (not Eb) and Cdim7 a Bbb.
        // ------------------------------------------------------------------
        constexpr std::array<int, 7> kNaturalPc { 0, 2, 4, 5, 7, 9, 11 }; // C D E F G A B

        int letterIndex (char c) { return std::string ("CDEFGAB").find (c) == std::string::npos ? 0 : (int) std::string ("CDEFGAB").find (c); }

        // Usual root names for chord symbols: Eb, Ab, Bb but C#, F# (and G# in minor), unless flats are wanted.
        std::string rootName (int pc, bool minor, bool flats)
        {
            static const char* sharpish[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
            static const char* minorish[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
            static const char* flat[]     = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
            return flats ? flat[pc] : (minor ? minorish[pc] : sharpish[pc]);
        }

        // degree: 1..13 (2 = 9, 4 = 11, 6 = 13 for the letter). semis: distance above the root.
        std::string spell (const std::string& root, int rootPc, int degree, int semis)
        {
            const int li = (letterIndex (root[0]) + (degree - 1)) % 7;
            const int natural = kNaturalPc[(size_t) li];
            int diff = ((rootPc + semis - natural) % 12 + 12) % 12;
            if (diff > 6) diff -= 12;
            std::string acc = diff == 1 ? "#" : diff == 2 ? "##" : diff == -1 ? "b" : diff == -2 ? "bb" : "";
            return std::string (1, "CDEFGAB"[li]) + acc;
        }

        struct Role { int semis; int degree; const char* label; };

        struct Reading
        {
            std::string suffix;     // "m7b5", "13(#11)", "6/9"...
            std::string words;      // "minor 7th, flat 5"
            std::vector<Role> roles;
            int  penalty = 0;
            bool plain = false;     // ordinary triad or seventh chord, nothing added or changed
            bool minor = false;
        };

        // Explains a set of intervals above a root (bit i = i semitones above the root; bit 0 = the root).
        Reading analyse (unsigned rel)
        {
            auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
            Reading r;
            int notes = 0;
            for (int i = 0; i < 12; ++i) if (has (i)) ++notes;

            // --- 3rd, 5th, 7th ---
            const int third = has (4) ? 4 : (has (3) ? 3 : -1);
            const bool sus4 = third < 0 && has (5);
            const bool sus2 = third < 0 && has (2) && ! sus4;
            const bool fifth = has (7);
            const int seventh = has (10) ? 10 : (has (11) ? 11 : -1);
            const bool alteredNinth = has (1) || (third == 4 && has (3));
            const bool dimTriad = third == 3 && ! fifth && has (6);
            // Jazz habit: next to an altered 9th, the raised 5th is called b13 (C7alt), otherwise #5 (C7#5, C9#5).
            const bool augTriad = third == 4 && ! fifth && has (8) && ! alteredNinth;
            // With a major 7th, or a 9th / 13th on top, the tritone above the root is called #11; otherwise b5 (C7b5).
            const bool flat5 = third == 4 && ! fifth && has (6) && ! augTriad && seventh != 11 && ! has (2) && ! has (9);
            const bool bothSevenths = has (10) && has (11);
            const bool dim7 = dimTriad && has (9) && seventh < 0;

            // --- what the other notes are ---
            const bool sharp9 = third == 4 && has (3);
            const bool flat9 = has (1);
            const bool nine = has (2) && ! sus2;
            const bool eleven = has (5) && ! sus4;
            const bool sharp11 = has (6) && ! dimTriad && ! flat5;
            const bool flat13 = has (8) && ! augTriad;
            const bool six = has (9) && ! dim7;

            r.minor = third == 3;
            r.roles.push_back ({ 0, 1, "root" });
            if (third == 4) r.roles.push_back ({ 4, 3, "3rd" });
            if (third == 3) r.roles.push_back ({ 3, 3, "minor 3rd" });
            if (sus4)       r.roles.push_back ({ 5, 4, "4th (instead of the 3rd)" });
            if (sus2)       r.roles.push_back ({ 2, 2, "2nd (instead of the 3rd)" });
            if (fifth)      r.roles.push_back ({ 7, 5, "5th" });
            if (dimTriad)   r.roles.push_back ({ 6, 5, "flat 5th" });
            if (flat5)      r.roles.push_back ({ 6, 5, "flat 5th" });
            if (augTriad)   r.roles.push_back ({ 8, 5, "sharp 5th" });
            if (dim7)       r.roles.push_back ({ 9, 7, "diminished 7th" });
            if (seventh == 10) r.roles.push_back ({ 10, 7, "minor 7th" });
            if (seventh == 11 && ! bothSevenths) r.roles.push_back ({ 11, 7, "major 7th" });
            if (bothSevenths)  r.roles.push_back ({ 11, 7, "major 7th (as well)" });
            if (flat9)   r.roles.push_back ({ 1, 9, "flat 9th" });
            if (nine)    r.roles.push_back ({ 2, 9, "9th" });
            if (sharp9)  r.roles.push_back ({ 3, 9, "sharp 9th" });
            if (eleven)  r.roles.push_back ({ 5, 11, "11th" });
            if (sharp11) r.roles.push_back ({ 6, 11, "sharp 11th" });
            if (flat13)  r.roles.push_back ({ 8, 13, seventh >= 0 ? "flat 13th" : "flat 6th" });
            if (six)     r.roles.push_back ({ 9, seventh >= 0 ? 13 : 6, seventh >= 0 ? "13th" : "6th" });

            // --- the base symbol ---
            std::string q, words;
            bool extendable = false; // the "7" may become 9 / 11 / 13
            if (dim7)            { q = "dim7"; words = "diminished 7th"; }
            else if (dimTriad)   { q = seventh == 10 ? "m7b5" : seventh == 11 ? "dim(maj7)" : "dim";
                                   words = seventh == 10 ? "half-diminished (minor 7th, flat 5)" : seventh == 11 ? "diminished with a major 7th" : "diminished"; }
            else if (augTriad)   { q = seventh == 10 ? "7#5" : seventh == 11 ? "maj7#5" : "aug";
                                   words = seventh == 10 ? "augmented 7th (dominant 7th, sharp 5)" : seventh == 11 ? "augmented major 7th" : "augmented"; }
            else if (third == 4) { q = seventh == 10 ? "7" : seventh == 11 ? "maj7" : "";
                                   words = seventh == 10 ? "dominant 7th" : seventh == 11 ? "major 7th" : "major"; extendable = seventh >= 0; }
            else if (third == 3) { q = seventh == 10 ? "m7" : seventh == 11 ? "m(maj7)" : "m";
                                   words = seventh == 10 ? "minor 7th" : seventh == 11 ? "minor with a major 7th" : "minor"; extendable = seventh >= 0; }
            else if (sus4)       { q = seventh == 10 ? "7sus4" : seventh == 11 ? "maj7sus4" : "sus4";
                                   words = std::string (seventh == 10 ? "dominant 7th, " : seventh == 11 ? "major 7th, " : "") + "suspended 4th"; extendable = seventh >= 0; }
            else if (sus2)       { q = seventh == 10 ? "7sus2" : seventh == 11 ? "maj7sus2" : "sus2";
                                   words = std::string (seventh == 10 ? "dominant 7th, " : seventh == 11 ? "major 7th, " : "") + "suspended 2nd"; }
            else if (seventh >= 0) { q = seventh == 10 ? "7" : "maj7"; words = seventh == 10 ? "dominant 7th without a 3rd" : "major 7th without a 3rd";
                                     r.penalty += 15; extendable = true; }
            else if (fifth && notes == 2) { q = "5"; words = "power chord (root and 5th)"; }
            else                 { q = ""; words = "chord without a 3rd"; r.penalty += 15; }

            // --- 9 / 11 / 13: the highest natural extension names the chord; the lower ones are implied ---
            bool used9 = false, used11 = false, used13 = false;
            if (extendable)
            {
                const char* ext = six ? "13" : eleven ? "11" : nine ? "9" : nullptr;
                if (ext != nullptr)
                {
                    const auto pos = q.find ('7');
                    q = q.substr (0, pos) + ext + q.substr (pos + 1);
                    used13 = six; used11 = eleven; used9 = nine;
                    words += std::string (" with ") + (six ? "13th" : eleven ? "11th" : "9th") + " on top";
                    r.penalty += 1;
                }
            }
            else if (seventh < 0 && ! dim7)
            {
                // No 7th: 6 chords and "add" chords.
                if (six && (third >= 0 || sus4 || sus2))
                {
                    if (nine && (third >= 0)) { q += "6/9"; used9 = true; words += " with 6th and 9th"; }
                    else                      { q = (q == "m" ? "m6" : q == "" ? "6" : q + "(add6)"); words += " with added 6th"; }
                    used13 = true;
                    if (! fifth) r.penalty += 12;             // G B E is Em/G rather than G6 without a 5th
                    if (third < 0) r.penalty += 12;
                }
                if (nine && ! used9)
                {
                    q = q == "" ? "add9" : q == "m" ? "madd9" : q + "(add9)";
                    used9 = true;
                    words += ", added 9th";
                    r.penalty += 2;
                }
                if (eleven && ! used11)
                {
                    q = q == "" ? "add11" : q == "m" ? "madd11" : q + "(add11)";
                    used11 = true;
                    words += ", added 11th";
                    r.penalty += 4;
                }
            }

            // --- alterations and anything left ---
            std::vector<std::string> alt;
            if (flat5)   alt.push_back ("b5");
            if (flat9)   alt.push_back ("b9");
            if (sharp9)  alt.push_back ("#9");
            if (sharp11) alt.push_back ("#11");
            if (flat13)  alt.push_back ("b13");
            if (nine && ! used9)     alt.push_back ("9");
            if (eleven && ! used11)  alt.push_back ("11");
            if (six && ! used13)     alt.push_back ("13");
            if (bothSevenths)        alt.push_back ("add maj7");
            r.penalty += 8 * (int) alt.size();
            if (seventh < 0 && (flat9 || sharp9 || sharp11 || flat13)) r.penalty += 10; // altered notes without a 7th are rare
            if (bothSevenths) r.penalty += 15;
            if (sus4 || sus2) r.penalty += sus2 ? 3 : 2;
            if (third < 0 && ! sus4 && ! sus2 && q != "5") { alt.push_back ("no3"); }

            const bool dominantFamily = q == "7" || q == "9" || q == "11" || q == "13";
            if (alt.size() == 1 && dominantFamily && alt[0] != "no3" && alt[0] != "add maj7")
                q += alt[0];                                   // C7b9, C13#11
            else if (! alt.empty())
            {
                q += "(";
                for (size_t i = 0; i < alt.size(); ++i) q += (i ? "," : "") + alt[i];
                q += ")";
            }
            if (seventh == 10 && third == 4 && alteredNinth && (has (6) || has (8)))
                words += " - an 'altered' dominant (also written 7alt)";
            for (const auto& a : alt)
                if (a != "no3") words += (a == "add maj7" ? ", with " : ", ") + std::string (a == "b5" ? "flat 5" : a == "b9" ? "flat 9"
                                    : a == "#9" ? "sharp 9" : a == "#11" ? "sharp 11" : a == "b13" ? "flat 13" : a == "add maj7" ? "a major 7th too" : a);

            r.suffix = q;
            r.words = words;
            r.plain = alt.empty() && ! used9 && ! used11 && ! used13 && ! sus2 && ! sus4 && third >= 0
                      && (fifth || dimTriad || augTriad);
            return r;
        }

        std::string roleList (const Reading& rd, const std::string& root, int rootPc)
        {
            std::string s;
            auto roles = rd.roles;
            std::sort (roles.begin(), roles.end(), [] (const Role& a, const Role& b) { return a.semis < b.semis; });
            for (const auto& ro : roles)
                s += (s.empty() ? "" : ", ") + spell (root, rootPc, ro.degree, ro.semis) + " = " + ro.label;
            return s;
        }
    }

    std::vector<ChordMatch> nameChord (const std::vector<int>& midiNotes, bool preferFlats)
    {
        std::vector<ChordMatch> out;
        if (midiNotes.empty()) return out;

        unsigned present = 0;
        for (int n : midiNotes) present |= 1u << (((n % 12) + 12) % 12);
        const int bass = ((*std::min_element (midiNotes.begin(), midiNotes.end()) % 12) + 12) % 12;
        int count = 0;
        for (int pc = 0; pc < 12; ++pc) if (present & (1u << pc)) ++count;
        if (count < 2) return out;

        auto relTo = [present] (int root)
        {
            unsigned rel = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (present & (1u << pc)) rel |= 1u << ((pc - root + 12) % 12);
            return rel;
        };

        // Two notes: a power chord, or just the interval.
        if (count == 2)
        {
            const int other = [&] { for (int pc = 0; pc < 12; ++pc) if ((present & (1u << pc)) && pc != bass) return pc; return bass; }();
            const int up = (other - bass + 12) % 12;
            ChordMatch m;
            m.root = bass; m.bass = bass;
            if (up == 7 || up == 5)
            {
                const int root = up == 7 ? bass : other;
                const auto rn = rootName (root, false, preferFlats);
                m.root = root;
                m.name = rn + "5" + (root != bass ? "/" + spell (rn, root, 5, 7) : "");
                m.meaning = rn + " power chord (root and 5th, no 3rd: neither major nor minor)"
                          + std::string (root != bass ? ", 5th in the bass" : "");
                m.score = 100;
            }
            else
            {
                const auto bn = rootName (bass, false, preferFlats);
                m.name = bn + " + " + rootName (other, false, preferFlats);
                m.meaning = intervalName (up) + " (two notes: not a full chord yet)";
            }
            out.push_back (m);
            return out;
        }

        for (int root = 0; root < 12; ++root)
        {
            const bool rootSounds = (present & (1u << root)) != 0;
            if (! rootSounds && count < 4) continue; // a missing root only makes sense in fuller (jazz) voicings

            unsigned rel = relTo (root);
            rel |= 1u; // the root, played or not
            const Reading rd = analyse (rel);

            const auto rn = rootName (root, rd.minor, preferFlats);
            ChordMatch m;
            m.root = root;
            m.bass = bass;
            m.name = rn + rd.suffix;
            m.meaning = rn + " " + rd.words + ". Notes: " + roleList (rd, rn, root);
            if (bass != root)
            {
                // The bass note spelled as the chord tone it is.
                std::string bn = rootName (bass, false, preferFlats);
                for (const auto& ro : rd.roles)
                    if ((root + ro.semis) % 12 == bass) bn = spell (rn, root, ro.degree, ro.semis);
                m.name += "/" + bn;
                m.meaning += ". " + bn + " in the bass";
            }
            if (! rootSounds)
                m.meaning += ". The root " + rn + " itself is not played (a rootless voicing; a bass player would play it)";

            m.score = 100 - rd.penalty + (bass == root ? 25 : 0) + (rd.plain ? 15 : 0) - (rootSounds ? 0 : 30);
            out.push_back (m);
        }

        // A chord over a different bass note (F/G, D/C): the notes above the bass make a plain chord of their own.
        if (count >= 4)
        {
            const unsigned upper = present & ~(1u << bass);
            for (int root = 0; root < 12; ++root)
            {
                if (! (upper & (1u << root)) || root == bass) continue;
                unsigned rel = 0;
                for (int pc = 0; pc < 12; ++pc)
                    if (upper & (1u << pc)) rel |= 1u << ((pc - root + 12) % 12);
                const Reading rd = analyse (rel);
                if (! rd.plain) continue;
                const auto rn = rootName (root, rd.minor, preferFlats);
                const auto bn = rootName (bass, false, preferFlats);
                ChordMatch m;
                m.root = root;
                m.bass = bass;
                m.name = rn + rd.suffix + "/" + bn;
                m.meaning = rn + " " + rd.words + " over a " + bn + " bass note that is not part of it (a 'slash' or "
                            "'upper structure' chord). Notes above the bass: " + roleList (rd, rn, root);
                m.score = 112;
                out.push_back (m);
            }
        }

        std::sort (out.begin(), out.end(), [] (const ChordMatch& a, const ChordMatch& b) { return a.score > b.score; });
        // Same name twice (from the two ways above): keep the better one.
        std::vector<ChordMatch> unique;
        for (const auto& m : out)
            if (std::none_of (unique.begin(), unique.end(), [&] (const ChordMatch& u) { return u.name == m.name; }))
                unique.push_back (m);
        return unique;
    }
    // =======================================================================
    // Chord symbol -> notes
    // =======================================================================
    namespace
    {
        // A note name at position i ("C", "F#", "Bb", "Ebb"). Moves i past it. -1 if there is none.
        int readNote (const std::string& s, size_t& i)
        {
            if (i >= s.size()) return -1;
            const auto li = std::string ("CDEFGAB").find ((char) std::toupper ((unsigned char) s[i]));
            if (li == std::string::npos) return -1;
            int pc = kNaturalPc[li];
            ++i;
            while (i < s.size() && (s[i] == '#' || s[i] == 'b'))
                pc += s[i++] == '#' ? 1 : -1;
            return ((pc % 12) + 12) % 12;
        }
    }

    std::optional<ParsedChord> parseChordName (const std::string& text)
    {
        std::string t;
        for (char c : text) if (c != ' ' && c != '\t') t += c;
        size_t i = 0;
        const int root = readNote (t, i);
        if (root < 0) return std::nullopt;

        ParsedChord out;
        out.root = root;

        // Slash bass: "/" followed by a note name at the very end ("D/F#"). "6/9" is not a bass.
        std::string rest = t.substr (i);
        if (const auto slash = t.rfind ('/'); slash != std::string::npos && slash >= i)
        {
            size_t j = slash + 1;
            const int b = readNote (t, j);
            if (b >= 0 && j == t.size())
            {
                out.bass = b;
                rest = t.substr (i, slash - i);
            }
        }

        int third = 4, fifth = 7, seventh = -1;
        bool majorSeventh = false, dim = false, eleventh = false, thirteenth = false, alt = false;
        std::vector<int> extra, optional;
        size_t k = 0;
        auto take = [&] (const char* token)
        {
            const size_t n = std::char_traits<char>::length (token);
            if (rest.compare (k, n, token) != 0) return false;
            k += n;
            return true;
        };
        auto readNumber = [&] () -> int
        {
            int v = 0;
            const size_t start = k;
            while (k < rest.size() && std::isdigit ((unsigned char) rest[k])) v = v * 10 + (rest[k++] - '0');
            return k > start ? v : -1;
        };
        auto setSeventh = [&] { seventh = dim && ! majorSeventh ? 9 : (majorSeventh ? 11 : 10); };
        // Degree number -> semitones (natural: 2/9 = 2, 4/11 = 5, 6/13 = 9).
        auto degreeSemis = [] (int d) { return d == 2 || d == 9 ? 2 : d == 4 || d == 11 ? 5 : d == 6 || d == 13 ? 9 : d == 5 ? 7 : d == 3 ? 4 : d == 7 ? 10 : -1; };

        while (k < rest.size())
        {
            const char c = rest[k];
            if (c == '(' || c == ')' || c == ',') { ++k; continue; }

            if (take ("maj") || take ("Maj") || take ("MAJ") || take ("ma") || take ("\xCE\x94") || take ("^")
                || (rest[k] == 'M' && (++k, true)))
            {
                majorSeventh = true;
                if (k >= rest.size() || ! std::isdigit ((unsigned char) rest[k])) seventh = 11; // "CΔ", "Cmaj"
                continue;
            }
            if (take ("min") || take ("mi") || take ("m") || (k == 0 && take ("-"))) { third = 3; continue; }
            if (take ("dim") || take ("o") || take ("\xC2\xB0")) { third = 3; fifth = 6; dim = true; continue; }
            if (take ("\xC3\xB8")) { third = 3; fifth = 6; seventh = 10; continue; }                 // ø = half-diminished
            if (take ("aug") || (rest[k] == '+' && (k == 0 || k + 1 >= rest.size() || ! std::isdigit ((unsigned char) rest[k + 1])) && (++k, true)))
            { fifth = 8; continue; }
            if (take ("sus2")) { third = -1; extra.push_back (2); continue; }
            if (take ("sus4") || take ("sus")) { third = -1; extra.push_back (5); continue; }
            if (take ("alt")) { alt = true; if (seventh < 0) setSeventh(); continue; }
            if (take ("no") || take ("omit"))
            {
                const int d = readNumber();
                if (d == 3) third = -1;
                else if (d == 5) fifth = -1;
                else return std::nullopt;
                continue;
            }
            if (take ("add"))
            {
                int shift = 0;
                if (k < rest.size() && (rest[k] == '#' || rest[k] == 'b')) shift = rest[k++] == '#' ? 1 : -1;
                const int d = readNumber();
                const int semis = degreeSemis (d);
                if (semis < 0) return std::nullopt;
                extra.push_back ((semis + shift + 12) % 12);
                continue;
            }
            if (take ("6/9") || take ("69")) { extra.push_back (9); extra.push_back (2); continue; }

            // Alterations: b5 #5 b9 #9 #11 b13 (also written -9, +5).
            if (c == 'b' || c == '#' || (k > 0 && (c == '-' || c == '+')))
            {
                const int shift = (c == '#' || c == '+') ? 1 : -1;
                ++k;
                const int d = readNumber();
                if (d == 5)        fifth = 7 + shift;
                else if (d == 9 || d == 11 || d == 13 || d == 6) extra.push_back ((degreeSemis (d) + shift + 12) % 12);
                else return std::nullopt;
                if (d != 5 && seventh < 0 && d != 6) setSeventh(); // C7b9 written as Cb9 still means a 7th chord
                continue;
            }

            if (std::isdigit ((unsigned char) c))
            {
                const int d = readNumber();
                switch (d)
                {
                    case 5:  if (k == 1) third = -1; break; // C5 = power chord (root + 5th)
                    case 6:  extra.push_back (9); break;
                    case 7:  setSeventh(); break;
                    case 9:  setSeventh(); extra.push_back (2); break;
                    case 11: setSeventh(); extra.push_back (2); extra.push_back (5); eleventh = true; break;
                    case 13: setSeventh(); extra.push_back (2); extra.push_back (9); thirteenth = true; break;
                    case 2:  extra.push_back (2); break;               // C2 = Cadd9
                    case 4:  third = -1; extra.push_back (5); break;   // C4 = Csus4
                    default: return std::nullopt;
                }
                continue;
            }
            return std::nullopt; // something we do not know
        }

        if (alt)
        {
            // Altered dominant: 3rd + b7, with b9 / #9 and b5 / b13 instead of the 5th and 9th.
            fifth = -1;
            extra.push_back (3);
            for (int o : { 1, 6, 8 }) { extra.push_back (o); optional.push_back (o); }
        }

        // Natural 9 / 11 / 13 are replaced by their altered versions when both are written ("C7b9" has no 9).
        auto has = [&] (int v) { return std::find (extra.begin(), extra.end(), v) != extra.end(); };
        if (has (1) || has (3)) extra.erase (std::remove (extra.begin(), extra.end(), 2), extra.end());
        if (has (6)) extra.erase (std::remove (extra.begin(), extra.end(), 5), extra.end());
        if (has (8) && seventh >= 0) extra.erase (std::remove (extra.begin(), extra.end(), 9), extra.end());

        out.intervals.push_back (0);
        if (third >= 0) out.intervals.push_back (third);
        if (fifth >= 0) out.intervals.push_back (fifth);
        if (seventh >= 0) out.intervals.push_back (seventh);
        for (int e : extra)
            if (std::find (out.intervals.begin(), out.intervals.end(), e) == out.intervals.end())
                out.intervals.push_back (e);

        // What a guitarist may leave out: the 5th (when there is more than a triad), the 9th in 11th and 13th chords,
        // the 11th in 13th chords, and the 3rd in a dominant 11th (it clashes with the 11th).
        if (fifth == 7 && out.intervals.size() >= 4) optional.push_back (7);
        if (eleventh || thirteenth) optional.push_back (2);
        if (thirteenth) optional.push_back (5);
        if (eleventh && third == 4) optional.push_back (4);
        for (int o : optional)
            if (std::find (out.intervals.begin(), out.intervals.end(), o) != out.intervals.end()
                && std::find (out.optional.begin(), out.optional.end(), o) == out.optional.end())
                out.optional.push_back (o);
        return out;
    }

    ShapeRequest shapeRequestFor (const ParsedChord& c)
    {
        ShapeRequest r;
        r.root = c.root;
        r.bass = c.bass;
        for (int iv : c.intervals)
        {
            const int pc = (c.root + iv) % 12;
            r.pitchClasses.push_back (pc);
            if (std::find (c.optional.begin(), c.optional.end(), iv) == c.optional.end())
                r.required.push_back (pc);
        }
        // A slash bass that is not a chord tone (C/D) is an extra note, and it must be there.
        if (c.bass >= 0 && std::find (r.pitchClasses.begin(), r.pitchClasses.end(), c.bass) == r.pitchClasses.end())
        {
            r.pitchClasses.push_back (c.bass);
            r.required.push_back (c.bass);
        }
        return r;
    }
    // =======================================================================
    // The fingerings guitarists actually play
    // =======================================================================
    namespace
    {
        constexpr int X = -100; // string not played

        // Open chords as everybody learns them (lowest string first). Fixed frets.
        const char* const kOpenChords[] = {
            "x32010", "x02220", "320003", "320033", "022100", "xx0232", "133211", "xx3211",          // C A G G E D F F
            "x02210", "022000", "xx0231",                                                              // Am Em Dm
            "x32310", "x02020", "320001", "020100", "xx0212", "x21202",                                // C7 A7 G7 E7 D7 B7
            "x02010", "020000", "022030", "xx0211",                                                    // Am7 Em7 Em7 Dm7
            "x32000", "x02120", "320002", "021100", "xx0222", "xx3210",                                // maj7: C A G E D F
            "x02200", "x02230", "xx0230", "xx0233", "022200", "x02030", "020200",                      // sus
            "x32030", "x02222", "022120", "xx0202", "320000", "x32210",                                // Cadd9, 6 chords
            "x02212", "022020", "xx0201",                                                              // m6
            "022xxx", "x022xx", "xx023x", "02xxxx", "x02xxx",                                          // power chords
            "x24432", "244222", "x13331", "x35543", "x2323x", "xx0101",                                // Bm F#m Bb Cm Bm7b5 Ddim7
        };

        // Movable shapes: frets above the root's fret on the root string (lowest string first), X = not played.
        struct Movable { const char* kind; int weight; std::array<int, 6> offsets; };
        const Movable kMovable[] = {
            // E shape: root on the lowest string
            { "Barre, E shape", 1, { 0, 2, 2, 1, 0, 0 } }, { "Barre, E shape", 1, { 0, 2, 2, 0, 0, 0 } },
            { "Barre, E shape", 1, { 0, 2, 0, 1, 0, 0 } }, { "Barre, E shape", 1, { 0, 2, 0, 0, 0, 0 } },
            { "Barre, E shape", 1, { 0, 2, 2, 2, 0, 0 } }, { "Barre, E shape", 1, { 0, 2, 0, 2, 0, 0 } },
            { "Barre, E shape", 1, { 0, 2, 1, 1, 0, 0 } },                                                   // maj7
            { "Power chord",    1, { 0, 2, 2, X, X, X } }, { "Power chord",    1, { 0, 2, X, X, X, X } },
            // A shape: root on the second string from the bottom
            { "Barre, A shape", 1, { X, 0, 2, 2, 2, 0 } }, { "Barre, A shape", 1, { X, 0, 2, 2, 1, 0 } },
            { "Barre, A shape", 1, { X, 0, 2, 0, 2, 0 } }, { "Barre, A shape", 1, { X, 0, 2, 0, 1, 0 } },
            { "Barre, A shape", 1, { X, 0, 2, 1, 2, 0 } }, { "Barre, A shape", 1, { X, 0, 2, 2, 0, 0 } },
            { "Barre, A shape", 1, { X, 0, 2, 2, 3, 0 } }, { "Barre, A shape", 1, { X, 0, 2, 0, 3, 0 } },
            { "Barre, A shape", 1, { X, 0, 2, 2, 2, 2 } }, { "Barre, A shape", 1, { X, 0, 2, 2, 1, 2 } },
            { "Power chord",    1, { X, 0, 2, 2, X, X } }, { "Power chord",    1, { X, 0, 2, X, X, X } },
            // Jazz shapes (root on one of the two lowest strings, one string skipped or the top left out)
            { "Jazz shape", 2, { 0, X, 1, 1, 0, X } },  { "Jazz shape", 2, { 0, X, 0, 1, 0, X } },  // maj7, 7
            { "Jazz shape", 2, { 0, X, 0, 0, 0, X } },  { "Jazz shape", 2, { 0, X, 0, 0, -1, X } }, // m7, m7b5
            { "Jazz shape", 2, { 0, X, -1, 0, -1, X } }, { "Jazz shape", 2, { 0, X, -1, 1, 0, X } }, // dim7, 6
            { "Jazz shape", 2, { 0, X, -1, 0, 0, X } }, { "Jazz shape", 2, { 0, X, 0, 1, 2, 2 } },  // m6, 13
            { "Jazz shape", 2, { X, 0, -1, 0, 0, 0 } }, { "Jazz shape", 2, { X, 0, -2, 0, 0, 0 } }, // 9, m9
            { "Jazz shape", 2, { X, 0, -1, 1, 0, 0 } }, { "Jazz shape", 2, { X, 0, -1, 0, 1, X } }, // maj9, 7#9
            { "Jazz shape", 2, { X, 0, -1, 0, -1, X } }, { "Jazz shape", 2, { X, 0, 1, -1, 1, X } }, // 7b9, dim7
            { "Jazz shape", 2, { X, 0, 1, 0, 1, X } },  { "Jazz shape", 2, { X, 0, -1, -1, 0, 0 } }, // m7b5, 6/9
            // D shape: root on the third string from the bottom
            { "D shape", 3, { X, X, 0, 2, 3, 2 } }, { "D shape", 3, { X, X, 0, 2, 3, 1 } },
            { "D shape", 3, { X, X, 0, 2, 1, 2 } }, { "D shape", 3, { X, X, 0, 2, 1, 1 } },
            { "D shape", 3, { X, X, 0, 2, 2, 2 } }, { "D shape", 3, { X, X, 0, 2, 3, 0 } },
            { "D shape", 3, { X, X, 0, 2, 3, 3 } }, { "Power chord", 3, { X, X, 0, 2, 3, X } },
        };
    }

    std::vector<CommonShape> commonShapes (const ParsedChord& chord, const Instrument& inst)
    {
        std::vector<CommonShape> out;
        const auto req = shapeRequestFor (chord);
        const int n = inst.numStrings();
        if (n < 6) return out;

        auto allowed = [&] (int pc) { return std::find (req.pitchClasses.begin(), req.pitchClasses.end(), pc) != req.pitchClasses.end(); };
        const int wantBass = chord.bass >= 0 ? chord.bass : chord.root;

        // frets: lowest string of the group first. offset = strings below the group (7/8 strings); the shapes are
        // made for the top six strings of a standard tuning, so that group counts as the most usual.
        auto consider = [&] (const std::array<int, 6>& lowToHigh, int offset, int rank, const std::string& kind) -> bool
        {
            Voicing v;
            v.frets.assign ((size_t) n, kMuted);
            MidiNote lowest = 999;
            unsigned present = 0;
            for (int i = 0; i < 6; ++i)
            {
                const int f = lowToHigh[(size_t) i];
                if (f == X) continue;
                if (f < 0 || f > kNumFrets) return false;
                const int string = n - 1 - offset - i;
                v.frets[(size_t) string] = f;
                const MidiNote note = inst.noteAt ({ string, f });
                lowest = std::min (lowest, note);
                if (! allowed (pitchClassOf (note))) return false; // a note that is not in the chord
                present |= 1u << pitchClassOf (note);
            }
            if (lowest == 999 || pitchClassOf (lowest) != wantBass) return false;
            for (int r : req.required)
                if (! (present & (1u << r))) return false;  // a needed note is missing

            for (auto& o : out)
                if (o.voicing == v) { if (rank < o.rank) { o.rank = rank; o.kind = kind; } return true; }
            out.push_back ({ v, rank, kind });
            return true;
        };

        for (int offset = 0; offset + 6 <= n; ++offset)
        {
            // Open chords: in the list order (the most common version of each chord comes first).
            int index = 0;
            for (const char* pattern : kOpenChords)
            {
                std::array<int, 6> frets {};
                for (int i = 0; i < 6; ++i) frets[(size_t) i] = pattern[i] == 'x' ? X : pattern[i] - '0';
                const bool barre = std::none_of (frets.begin(), frets.end(), [] (int f) { return f == 0; });
                consider (frets, offset, (n - 6 - offset) * 1000 + index++, barre ? "Barre chord" : "Open chord");
            }

            // Movable shapes: once each, at the lowest fret where the root falls on the root string.
            // (The same shape an octave higher is still listed with all the other shapes.)
            for (const auto& m : kMovable)
            {
                int rootString = 0;
                while (rootString < 6 && m.offsets[(size_t) rootString] == X) ++rootString;
                const MidiNote open = inst.tuning()[(size_t) (n - 1 - offset - rootString)];
                for (int r = ((chord.root - open) % 12 + 12) % 12; r <= kNumFrets; r += 12)
                {
                    std::array<int, 6> frets {};
                    for (int i = 0; i < 6; ++i)
                        frets[(size_t) i] = m.offsets[(size_t) i] == X ? X : r + m.offsets[(size_t) i];
                    if (consider (frets, offset, (n - 6 - offset) * 1000 + 100 * m.weight + r, m.kind))
                        break;
                }
            }
        }

        std::sort (out.begin(), out.end(), [] (const CommonShape& a, const CommonShape& b) { return a.rank < b.rank; });
        return out;
    }
}
