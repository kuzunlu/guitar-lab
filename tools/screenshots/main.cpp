// Makes the pictures for the user manual (docs/manual/img). Builds the real editor off-screen, sets up scenes and
// saves JPEG files. No window is shown and no audio device is opened.
// Build and run it with tools\make-manual-images.bat (it needs the CMake option GUITAR_LAB_SCREENSHOTS=ON).

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Midi/TabExport.h"

#include <cmath>
#include <random>
#include <sstream>

using namespace gl;

namespace
{
    juce::File outDir;

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    template <typename T, typename Pred>
    T* findComp (juce::Component* root, Pred pred)
    {
        for (auto* c : root->getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                if (pred (*t)) return t;
            if (auto* r = findComp<T> (c, pred)) return r;
        }
        return nullptr;
    }

    juce::ComboBox* comboWithItem (juce::Component* root, const juce::String& text)
    {
        return findComp<juce::ComboBox> (root, [&] (juce::ComboBox& b)
        {
            for (int i = 0; i < b.getNumItems(); ++i)
                if (b.getItemText (i) == text) return true;
            return false;
        });
    }

    juce::TextButton* button (juce::Component* root, const juce::String& text)
    {
        return findComp<juce::TextButton> (root, [&] (juce::TextButton& b) { return b.getButtonText() == text; });
    }

    juce::ToggleButton* toggle (juce::Component* root, const juce::String& text)
    {
        return findComp<juce::ToggleButton> (root, [&] (juce::ToggleButton& b) { return b.getButtonText() == text; });
    }

    void selectItem (juce::ComboBox* box, const juce::String& text)
    {
        if (box == nullptr) { std::printf ("  (combo for '%s' not found)\n", text.toRawUTF8()); return; }
        for (int i = 0; i < box->getNumItems(); ++i)
            if (box->getItemText (i) == text)
                box->setSelectedId (box->getItemId (i), juce::sendNotificationSync);
    }

    void click (juce::Button* b, const char* what)
    {
        if (b == nullptr) { std::printf ("  (button '%s' not found)\n", what); return; }
        if (b->onClick) b->onClick();
    }

    // Saves a picture for the manual as JPEG (name.png -> name.jpg). crop: part of the window only.
    void writePng (const juce::Image& img, const juce::String& name, juce::Rectangle<int> crop = {})
    {
        auto file = outDir.getChildFile (name.upToLastOccurrenceOf (".", false, false) + ".jpg");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.86f);
        jpeg.writeImageToStream (crop.isEmpty() ? img : img.getClippedImage (crop), out);
        std::printf ("saved %s\n", file.getFullPathName().toRawUTF8());
    }

    juce::Image snap (juce::Component& ed)
    {
        pump (250); // let timers update the fretboard, piano and panels
        return ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    }

    void drawTooltip (juce::Image& img, juce::Component& ed, juce::Component& target)
    {
        auto* client = dynamic_cast<juce::TooltipClient*> (&target);
        if (client == nullptr) return;
        const auto tip = client->getTooltip();
        auto& lnf = ed.getLookAndFeel();
        const auto pos = ed.getLocalPoint (&target, juce::Point<int> (target.getWidth() / 2, target.getHeight() / 2));
        const auto bounds = lnf.getTooltipBounds (tip, pos, ed.getLocalBounds());

        juce::Graphics g (img);
        g.setOrigin (bounds.getPosition());
        lnf.drawTooltip (g, tip, bounds.getWidth(), bounds.getHeight());

        // A small mouse pointer where the mouse "is".
        g.setOrigin (pos - bounds.getPosition());
        juce::Path arrow;
        arrow.startNewSubPath (0, 0); arrow.lineTo (0, 16); arrow.lineTo (4, 12); arrow.lineTo (8, 19);
        arrow.lineTo (10, 18); arrow.lineTo (6, 11); arrow.lineTo (11, 11); arrow.closeSubPath();
        g.setColour (juce::Colours::white); g.fillPath (arrow);
        g.setColour (juce::Colours::black); g.strokePath (arrow, juce::PathStrokeType (1.0f));
    }

    void setParam (GuitarLabProcessor& p, const char* id, float value)
    {
        auto* param = p.params.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Plays audio into the plugin's guitar input, block by block, letting the UI timers run.
    void feed (GuitarLabProcessor& p, const std::vector<float>& audio)
    {
        juce::AudioBuffer<float> buf (2, 512);
        for (size_t pos = 0; pos < audio.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, audio.size() - pos);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            buf.copyFrom (0, 0, audio.data() + pos, n);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            pump (2);
        }
    }

    template <typename FreqFn>
    std::vector<float> pluck (double seconds, FreqFn freqAt, double amp = 0.4)
    {
        std::vector<float> out ((size_t) (seconds * 48000.0));
        double phase = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = i / 48000.0;
            phase += freqAt (t) / 48000.0;
            phase -= std::floor (phase);
            out[i] = (float) ((2.0 * phase - 1.0) * amp * std::exp (-t * 1.2));
        }
        return out;
    }

    std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * 48000.0), 0.0f); }

    NoteEvent note (int string, int fret, double start, double length, Technique t = Technique::Normal)
    {
        NoteEvent n;
        n.string = string; n.fret = fret; n.start = start; n.length = length; n.technique = t;
        return n;
    }

    void buildLick (GuitarLabProcessor& p)
    {
        auto& ph = p.phrase;
        ph.clear();
        ph.lengthBeats = 16.0;

        auto bend = note (2, 7, 0.0, 1.0);          bend.bend = makeBend (BendPreset::Bend, 2.0); bend.vibrato = true; ph.notes.push_back (bend);
        ph.notes.push_back (note (1, 5, 1.0, 0.5));
        ph.notes.push_back (note (1, 8, 1.5, 0.5));
        ph.notes.push_back (note (1, 5, 2.0, 0.5, Technique::PullOff));
        ph.notes.push_back (note (2, 5, 3.0, 0.5));
        ph.notes.push_back (note (2, 7, 3.5, 0.5, Technique::HammerOn));
        auto slide = note (3, 7, 4.0, 1.0);         slide.slide = SlideType::SlideTo; slide.slideToFret = 9; ph.notes.push_back (slide);
        auto br = note (1, 8, 5.0, 1.5);            br.bend = makeBend (BendPreset::BendAndRelease, 2.0); br.vibrato = true; ph.notes.push_back (br);
        auto in = note (0, 5, 6.5, 1.5);            in.slide = SlideType::SlideInBelow; in.slideDistance = 3; ph.notes.push_back (in);
        for (int i = 0; i < 4; ++i) ph.notes.push_back (note (5, 0, 8.0 + i * 0.5, 0.5, Technique::PalmMute));

        Voicing a5 { { -1, -1, -1, 7, 7, 5 } };
        ph.addChord (a5, 10.0, 2.0, 110, true, 15.0, 120.0);

        ph.notes.push_back (note (2, 12, 12.0, 0.5, Technique::NaturalHarmonic));
        ph.notes.push_back (note (1, 12, 12.5, 0.5, Technique::NaturalHarmonic));
        auto ring = note (0, 12, 13.0, 1.0, Technique::NaturalHarmonic); ring.letRing = true; ph.notes.push_back (ring);
        ph.notes.push_back (note (3, 7, 14.0, 0.25, Technique::DeadNote));
        ph.notes.push_back (note (3, 7, 14.25, 0.25, Technique::DeadNote));
        auto pinch = note (3, 7, 14.5, 1.5, Technique::PinchHarmonic); pinch.vibrato = true; pinch.vibratoDepth = 0.6; ph.notes.push_back (pinch);

        ph.sort();
        p.phraseChanged();
    }

    void practiceStats (GuitarLabProcessor& p)
    {
        QuizFocus f;
        f.strings.assign ((size_t) p.instrument.numStrings(), true);
        f.minFret = 0; f.maxFret = 12;
        p.trainer.setFocus (f);

        std::mt19937 rng (3);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        for (int i = 0; i < 600; ++i)
        {
            auto q = p.trainer.next (QuizMode::NameTheNote);
            const auto spot = q.shown[0];
            const double chance = spot.string == 3 ? 0.35 : (spot.fret > 7 ? 0.6 : 0.93); // weak on the 4th string
            const bool ok = u (rng) < chance;
            p.trainer.answerPitchClass (q, ok ? q.targetPc : q.targetPc + 1, ok ? 1.5 : 4.0);
        }
    }


    // Draws the first page of a Guitar Lab PDF (our own simple drawing commands) into an image, to check the layout.
    juce::Image renderPdfPage (const std::string& pdf)
    {
        juce::Image img (juce::Image::RGB, 1190, 1684, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colours::white);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        const auto start = pdf.find ("stream\n", pdf.find ("/Contents"));
        if (start == std::string::npos) return img;
        const auto end = pdf.find ("endstream", start);
        std::istringstream in (pdf.substr (start + 7, end - start - 7));
        std::string line;
        auto Y = [] (double y) { return (float) (842.0 - y); };
        while (std::getline (in, line))
        {
            std::istringstream ls (line);
            std::vector<std::string> t;
            std::string w;
            if (line.rfind ("BT", 0) == 0)
            {
                const auto a = line.find ('('), b = line.rfind (')');
                std::string text = line.substr (a + 1, b - a - 1), clean;
                for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '\\') ++i; if (i < text.size()) clean += text[i]; }
                std::istringstream h (line);
                std::string bt, font, tf, td; double size, x, y;
                h >> bt >> font >> size >> tf >> x >> y;
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions ((float) size * 1.0f, font == "/F2" ? juce::Font::bold : juce::Font::plain));
                g.drawSingleLineText (clean, (int) x, (int) Y (y));
                continue;
            }
            while (ls >> w) t.push_back (w);
            if (t.size() == 11 && t[10] == "S") // grey G width w x1 y1 m x2 y2 l S
            {
                const float grey = std::stof (t[0]);
                g.setColour (juce::Colour::greyLevel (grey));
                g.drawLine (std::stof (t[4]), Y (std::stod (t[5])), std::stof (t[7]), Y (std::stod (t[8])), std::stof (t[2]));
            }
            else if (t.size() == 9 && t[6] == "f") // 1 g x y w h re f 0 g
            {
                const float x = std::stof (t[2]), y = std::stof (t[3]), wd = std::stof (t[4]), ht = std::stof (t[5]);
                g.setColour (juce::Colours::white);
                g.fillRect (x, Y (y + ht), wd, ht);
            }
        }
        return img;
    }

    void clearSounding (GuitarLabProcessor& p)
    {
        for (auto& f : p.soundingFret) f = -1;
        for (auto& b : p.soundingBend) b = 0.0f;
    }
}


int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    outDir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    auto proc = std::make_unique<GuitarLabProcessor>();
    proc->prepareToPlay (48000.0, 512);
    buildLick (*proc);
    practiceStats (*proc);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setVisible (true);
    pump (300);

    auto* root = ed.get();
    writePng (snap (*root), "00-opens-on.png");
    auto* tabs = findComp<juce::TabbedComponent> (root, [] (auto&) { return true; });

    // Scale overlay: A minor pentatonic
    selectItem (comboWithItem (root, "Minor pentatonic"), "Minor pentatonic");
    selectItem (comboWithItem (root, "A# / Bb"), "A");

    // ---------------------------------------------------------------- 1. Play & Write
    tabs->setCurrentTabIndex (1);
    proc->soundingFret[2] = 7;  proc->soundingBend[2] = 1.6f;   // G string, 7th fret, bent up
    proc->soundingFret[1] = 5;
    if (auto* tv = findComp<TabView> (root, [] (auto&) { return true; }))
    {
        tv->selected = 7;
        if (tv->onSelect) tv->onSelect (7);
    }
    writePng (snap (*root), "01-play-and-write.png");
    writePng (snap (*root), "m-play-write.png", { 0, 284, 1320, 476 });
    {
        // Export the lick as PDF and text, and draw the PDF page.
        gl::TabExportInfo info;
        info.title = "Lick in A minor";
        const auto pdf = gl::phraseToPdf (proc->phrase, proc->instrument, info);
        juce::File (outDir.getChildFile ("15-tab-export.pdf")).replaceWithData (pdf.data(), pdf.size());
        writePng (renderPdfPage (pdf), "15-tab-export-pdf-page.png");
        writePng (renderPdfPage (pdf), "m-pdf.png", { 0, 0, 1190, 760 });
        outDir.getChildFile ("15-tab-export.txt").replaceWithText (gl::phraseToTextTab (proc->phrase, proc->instrument, info));
    }

    // ---------------------------------------------------------------- 2. Tooltip
    if (auto* bendBox = comboWithItem (root, "Bend + release"))
    {
        auto img = snap (*root);
        drawTooltip (img, *root, *bendBox);
        writePng (img, "02-tooltip-bend-control.png");
    }
    if (auto* tv = findComp<TabView> (root, [] (auto&) { return true; }))
    {
        auto img = snap (*root);
        // Tooltip of the selected note in the tab view (it describes what is under the mouse).
        drawTooltip (img, *root, *tv);
        writePng (img, "03-tooltip-tab-view.png");
    }

    // ---------------------------------------------------------------- 3. MIDI settings
    click (button (root, "MIDI / Sound..."), "MIDI / Sound...");
    pump (500);
    writePng (snap (*root), "04-midi-and-sound-settings.png");
    writePng (snap (*root), "m-midi-sound.png", { 776, 72, 530, 650 });
    if (auto* box = findComp<juce::CallOutBox> (root, [] (auto&) { return true; })) box->dismiss();
    pump (500);
    clearSounding (*proc);

    // ---------------------------------------------------------------- 4. Reharm Lab
    tabs->setCurrentTabIndex (3);
    pump (100);
    if (auto* amount = findComp<juce::Slider> (root, [] (juce::Slider& s) { return s.getMaximum() == 100.0 && s.getTextValueSuffix() == " %"; }))
        amount->setValue (85, juce::sendNotificationSync);
    click (button (root, "Reharmonize"), "Reharmonize");
    if (auto* strip = findComp<ChordStrip> (root, [] (ChordStrip& s) { return s.title.startsWith ("Reharmonized"); }))
        if (strip->onClick) strip->onClick (2);
    writePng (snap (*root), "05-reharm-lab.png");
    writePng (snap (*root), "m-reharm.png", { 0, 284, 1320, 478 });

    // ---------------------------------------------------------------- 5. Trainer
    tabs->setCurrentTabIndex (4);
    pump (100);
    if (auto* heat = toggle (root, "Show heat map")) heat->setToggleState (true, juce::sendNotificationSync);
    click (button (root, "Start"), "Start");
    writePng (snap (*root), "06-trainer-heat-map.png");
    writePng (snap (*root), "m-trainer.png", { 0, 0, 1320, 480 });
    selectItem (comboWithItem (root, "Ear: chord type"), "Ear: chord type");
    pump (300);
    writePng (snap (*root), "16-ear-training-chord-type.png");
    writePng (snap (*root), "m-ear.png", { 0, 284, 1320, 200 });
    selectItem (comboWithItem (root, "Name the note"), "Name the note");
    pump (100);

    // ---------------------------------------------------------------- 5b. Chord Shapes: Cmaj7
    tabs->setCurrentTabIndex (2);
    pump (100);
    if (auto* input = findComp<juce::TextEditor> (root, [] (juce::TextEditor& t) { return t.getText() == "C"; }))
        input->setText ("Cmaj7", true);
    pump (100);
    if (auto* grid = findComp<gl::ShapeGrid> (root, [] (auto&) { return true; }))
        if (grid->onClick) grid->onClick (2);
    pump (200);
    writePng (snap (*root), "14-chord-shapes-cmaj7.png");
    clearSounding (*proc);

    // ---------------------------------------------------------------- 6. Guitar -> MIDI (a tracked note with a bend)
    proc->calibration = Calibration {};
    proc->calibration.valid = true;
    proc->calibration.tuning = proc->instrument.tuning();
    proc->calibration.globalCents = -6.0;
    proc->calibration.noiseFloor = 0.001f; proc->calibration.softPeak = 0.05f; proc->calibration.hardPeak = 0.5f;
    setParam (*proc, ParamIDs::trackMode, 1.0f);
    setParam (*proc, ParamIDs::trackInput, 1.0f);
    pump (300);
    proc->rebuildTracker();
    tabs->setCurrentTabIndex (0);
    pump (100);

    // A2 picked, then bent up a little more than a half step.
    auto a2 = pluck (0.55, [] (double t) { return 110.0 * std::pow (2.0, std::clamp ((t - 0.2) / 0.25, 0.0, 1.0) * 1.2 / 12.0); });
    feed (*proc, a2);
    writePng (snap (*root), "07-guitar-to-midi-tracking.png");
    writePng (snap (*root), "m-topbar.png", { 0, 0, 1320, 64 });
    writePng (snap (*root), "m-tracking.png", { 0, 284, 1320, 300 });
    feed (*proc, silence (0.5));

    // ---------------------------------------------------------------- 6b. Poly mode: a strummed open C chord
    setParam (*proc, ParamIDs::trackMode, 3.0f);
    pump (300);
    proc->rebuildTracker();
    {
        std::vector<float> chord ((size_t) (0.45 * 48000.0), 0.0f);
        const int notesC[] = { 48, 52, 55, 60, 64 };
        for (int i = 0; i < 5; ++i)
        {
            const int n = notesC[i];
            auto s = pluck (0.45, [n] (double) { return midiToHz (n); }, 0.12);
            const size_t off = (size_t) (i * 0.012 * 48000.0);
            for (size_t k = 0; k < s.size() && k + off < chord.size(); ++k) chord[k + off] += s[k];
        }
        feed (*proc, chord);
    }
    writePng (snap (*root), "13-poly-chord-and-chord-name.png");

    // ---------------------------------------------------------------- 6c. Intonation report (made-up full calibration)
    {
        auto& c = proc->calibration;
        c.typicalLevel = 0.2f;
        for (int s = 0; s < proc->instrument.numStrings(); ++s)
            for (int f = 0; f <= 22; ++f)
            {
                c.positions[(size_t) s][(size_t) f].measured = true;
                // The low strings play more and more sharp up the neck (saddles too far forward); a little noise.
                const double drift = (s >= 4 ? 0.7 : (s == 0 ? -0.35 : 0.15)) * f;
                c.positions[(size_t) s][(size_t) f].cents = (float) (drift + std::sin (s * 7.0 + f * 1.7) * 2.0);
            }
        if (auto* b = button (root, "Intonation...")) b->onClick();
        pump (200);
        writePng (snap (*root), "17-intonation-report.png");
        if (auto* b = button (root, "Close")) b->onClick();
        c.typicalLevel = 0.0f;
        for (auto& row : c.positions) for (auto& pos : row) pos = {};
        pump (100);
    }
    feed (*proc, silence (0.6));
    setParam (*proc, ParamIDs::trackMode, 1.0f);
    pump (300);
    proc->rebuildTracker();

    // ---------------------------------------------------------------- 7. Calibration wizard: tuning first
    click (button (root, "Calibrate..."), "Calibrate...");
    writePng (snap (*root), "08-calibration-step-1-tuning.png");
    writePng (snap (*root), "m-cal-start.png", { 20, 320, 1280, 435 });

    // ---------------------------------------------------------------- 8. Full calibration: every note
    click (button (root, "Full calibration (15-20 min)"), "Full calibration");
    feed (*proc, silence (3.4));                                   // silence step
    feed (*proc, pluck (0.7, [] (double) { return 82.41 * std::pow (2.0, -8.0 / 1200.0); })); // open low E, 8 cents flat
    feed (*proc, silence (0.15));
    writePng (snap (*root), "09-full-calibration-every-note.png");
    click (button (root, "Cancel"), "Cancel");
    pump (200);

    // ---------------------------------------------------------------- 8b. Record guitar into the phrase
    tabs->setCurrentTabIndex (1);
    pump (100);
    click (button (root, "Clear"), "Clear");
    if (auto* rec = button (root, "Record guitar"))
    {
        rec->setToggleState (true, juce::dontSendNotification);
        click (rec, "Record guitar (start)");

        feed (*proc, silence (0.2));
        // A2, hammer-on to B2
        feed (*proc, pluck (0.7, [] (double t) { return t < 0.3 ? 110.0 : 123.47; }));
        feed (*proc, silence (0.25));
        // E3, bent up a whole step, then vibrato on top
        feed (*proc, pluck (1.4, [] (double t)
        {
            const double bend = std::clamp ((t - 0.15) / 0.25, 0.0, 1.0) * 2.0;
            const double vib = t > 0.6 ? 0.35 * std::sin (2.0 * 3.14159265 * 5.5 * (t - 0.6)) : 0.0;
            return 164.81 * std::pow (2.0, (bend + vib) / 12.0);
        }));
        feed (*proc, silence (0.4));

        rec->setToggleState (false, juce::dontSendNotification);
        click (rec, "Record guitar (stop)");
    }
    writePng (snap (*root), "12-record-guitar-into-tab.png");
    writePng (snap (*root), "m-record.png", { 0, 284, 1320, 476 });

    // ---------------------------------------------------------------- 9. 8-string, Drop E, Reharm Lab
    selectItem (comboWithItem (root, "8 strings"), "8 strings");
    pump (100);
    selectItem (comboWithItem (root, "Drop E (E B E A D G B E)"), "Drop E (E B E A D G B E)");
    tabs->setCurrentTabIndex (3);
    pump (100);
    if (auto* input = findComp<juce::TextEditor> (root, [] (auto&) { return true; }))
    {
        input->setText ("Imaj7 vi7 ii7 V7", false);
        if (input->onReturnKey) input->onReturnKey();
    }
    click (button (root, "New idea"), "New idea");
    if (auto* strip = findComp<ChordStrip> (root, [] (ChordStrip& s) { return s.title.startsWith ("Reharmonized"); }))
        if (strip->onClick) strip->onClick (1);
    writePng (snap (*root), "10-8-string-drop-e-reharm.png");

    // ---------------------------------------------------------------- 10. 7-string, all note names, Dorian
    selectItem (comboWithItem (root, "7 strings"), "7 strings");
    pump (100);
    selectItem (comboWithItem (root, "Minor pentatonic"), "Dorian");
    selectItem (comboWithItem (root, "A# / Bb"), "D");
    if (auto* names = toggle (root, "All note names")) names->setToggleState (true, juce::sendNotificationSync);
    tabs->setCurrentTabIndex (1);
    writePng (snap (*root), "11-7-string-dorian-note-names.png");

    ed.reset();
    proc.reset();
    return 0;
}
