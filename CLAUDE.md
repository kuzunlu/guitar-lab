# Guitar Lab

A VST3 audio effect plugin with MIDI output (plus a standalone app) for guitarists. Built with C++20 and JUCE.
Five tabs share one 24-fret neck (6, 7 or 8 strings, any tuning), a piano view at the bottom, and a
**chord analyser** at the right of the tab row (names the chord of whatever sounds: "Chord: Cmaj7/E").
Tab order (= the `Tab` enum in `PluginEditor.h`, = the `addTab` order):

1. **Guitar -> MIDI** (opens first) – real-time tracking of a real guitar from the audio input. Modes:
   - *Mono* (normal guitar, one cable, single notes): fastest; bends, slides, vibrato; string guessed from the
     hand position and (after Full calibration) tone colour; only taught chords.
   - *Poly* (normal guitar, one cable, chords too): several notes at once with per-note bends (`PolyTracker`).
   - *Hex* (hexaphonic pickup, one input per string): every string tracked on its own.
   Calibration wizard: tuning first, then Quick (silence, open strings, 12th fret, soft/hard) or Full
   (every fret on every string, ~37 chords in mono/poly, soft and hard picking on every string).
   Calibrations can be saved and loaded as **presets** (files, shared by all projects).
2. **Play & Write** – play the neck live (drag = bend / slide) and write MIDI phrases (up to 512 bars) with
   hammer-ons, pull-offs, taps, palm mute, dead notes, harmonics, bends, slides, vibrato and strummed chords.
   **Record guitar** writes the tracked notes into the phrase as editable tab (`PhraseRecorder`), with an optional
   1-bar count-in. Practice/edit tools: loop a section (drag in the ruler), metronome, zoom, multi-select
   (Ctrl + click, box drag), copy/cut/paste, move/transpose with buttons or keys. Export: MIDI, text tab, PDF,
   MusicXML (`Midi/TabExport`).
3. **Chord Shapes** – type or pick any chord symbol (`Cmaj7`, `F#m7b5`, `G7#9`, `D/F#`, `E7alt`) and see every
   playable shape for this tuning as chord diagrams (`parseChordName` + `findShapes`).
4. **Reharm Lab** – type a progression (`I V vi IV`, `Dm7 G7 Cmaj7`), get substitutions
   (tritone sub, secondary dominant, borrowed chord, passing diminished, related ii-V, diatonic sub,
   backdoor dominant, extensions) with playable, voice-led voicings.
5. **Trainer** – note-name, find-the-note, arpeggio and interval quizzes, focused on chosen strings
   and frets, weighted toward the player's weak spots, with a heat map. Ear training: interval, chord type, and
   "play it back" (answered with the real guitar through tracking, or a click).

## Build

Tools on the dev machine: Visual Studio Build Tools 2026 (MSVC 14.51), Windows SDK 10.0.26100,
CMake 4.3.1 (bundled with Build Tools). JUCE is fetched by CMake, pinned to tag `9.0.5`.

```powershell
cmd /c build.bat      # finds Visual Studio, CMake + Ninja in Release, then runs the tests
```

`build.bat` (used locally and by CI) always reconfigures, so the version from `git describe` stays
current. The first run downloads JUCE (~2 min). It ends with "All tests passed." and these outputs:
- `build/GuitarLab_artefacts/Release/VST3/Guitar Lab.vst3`
- `build/GuitarLab_artefacts/Release/Standalone/Guitar Lab.exe`
- `build/GuitarLabTests.exe`

The `build` folder must be a **Ninja** build. If it was made with another generator, delete it first
(CMake cannot switch generators in one folder).

The C++ runtime is static (`/MT`), so the plugin needs only Windows' own DLLs (no VC++ redistributable).
`COPY_PLUGIN_AFTER_BUILD` is off on purpose. Do not install the plugin into system folders without asking.
Inno Setup is not installed on the dev machine; CI installs it. Do not install it without asking.

### Plugin type and vendor (like MIDI Guitar 3)

- Guitar Lab is an **audio effect** (`IS_SYNTH FALSE`, VST3 category `Fx`) with MIDI in and MIDI out.
  It goes on the audio track the guitar is plugged into. Other tracks play its MIDI by routing
  (Ableton: MIDI track -> MIDI From = the guitar track, second box = Guitar Lab). Do not make it an instrument
  again: then Ableton only allows it on MIDI tracks and the guitar audio cannot reach it.
- **Guitar -> MIDI is the first tab and opens by default.** The `Tab` enum in `PluginEditor.h` must match
  the `addTab` order (Tracker, Play, Shapes, Reharm, Trainer). That tab also has the **Built-in sound** switch (same `synthOn` parameter as in
  MIDI / Sound settings): on to practise with Guitar Lab's own sound, off when the MIDI plays another instrument.
- `COMPANY_NAME` is `Kursat` (the vendor name DAWs show), the same as the owner's stream-bridge plugin.
- Keep `PLUGIN_MANUFACTURER_CODE Kuzu` and `PLUGIN_CODE GtLb` unchanged: together they are the plugin's ID.
  Changing them makes DAWs see a new plugin, and saved projects lose their Guitar Lab.

## Code layout

| Folder | What | Depends on JUCE? |
|---|---|---|
| `Source/Theory/` | Notes, instrument + tunings, chords + Roman numerals, reharm engine, voicing search (`findVoicings`, `findShapes` for any note set), trainer logic, `ChordFinder` (notes -> chord names, chord symbol -> notes) | **No** |
| `Source/Midi/` | `Phrase` (the note/technique model), `MidiRenderer` (phrase -> MIDI events), `PhraseRecorder` (tracked MIDI -> phrase notes), `TabExport` (tab text for a note; text tab, PDF and MusicXML files) | **No** |
| `Source/Tracking/` | `PitchDetector` (YIN on a downsampled copy), `Spectrum` (FFT, tone colour, chroma), `Calibration` + `CalibrationSession` (wizard logic), `GuitarTracker` (audio -> notes/bends/velocity/chords; runs `PolyTracker` in poly mode) | **No** |
| `Source/Audio/` | `PluckSynth`: small built-in plucked-string sound (one voice per MIDI channel) | Yes |
| `Source/UI/` | Fretboard, piano, top bar, the five tab panels (`ShapesPanel` = Chord Shapes), theme + tooltip look | Yes |
| `Source/PluginProcessor.*` | Parameters, state save/load, playback in sync with the host, live notes | Yes |
| `Source/PluginEditor.*` | Lays out the window and routes fretboard/piano clicks to the open tab | Yes |
| `Tests/CoreTests.cpp` | Plain asserts for `Theory/`, `Midi/` and `Tracking/` (no framework) | No |
| `installer/GuitarLab.iss` | Inno Setup 6 installer script | - |
| `build.bat` | Local + CI build (CMake + Ninja with Visual Studio's toolchain), then runs the tests | - |
| `.github/workflows/build.yml` | CI on every push; a `v*` tag also publishes a GitHub Release | - |

Keep `Theory/`, `Midi/` and `Tracking/` free of JUCE, so they stay testable and could move to another plugin format.

## Rules that are easy to break

- **String index 0 is the HIGHEST string** (like tab). The UI shows "string 1" for index 0.
  Tuning boxes in the top bar are shown low-to-high, so they map `box i -> string (count - 1 - i)`.
- **One MIDI channel per string.** Highest string = `firstChannel` (default 2), next string = next channel.
  With MPE on, channel 1 is the master channel. Never put two strings on one channel: pitch bend is per channel.
- Bends, slides and vibrato are **pitch bend**, limited by the bend range setting (default 12).
  `setupMessages()` sends the MPE zone (RPN 6) and bend range (RPN 0) at playback start.
- Inside `MidiRenderer`, events on the same beat are ordered: note-off, then controls/bends, then note-on.
  Legato notes (hammer/pull/tap, slide target) overlap the previous note by `legatoOverlapBeats`.
- **Audio thread safety:** the UI edits `proc.phrase` / `proc.instrument` on the message thread and then
  calls `proc.phraseChanged()`, which renders new events and swaps them in under a `SpinLock`.
  The audio thread only uses `ScopedTryLock` and never allocates or frees the event vectors.
  Live notes from the UI go through `juce::MidiMessageCollector`.
- After changing anything that affects MIDI output, call `proc.phraseChanged()`.
- **Tracking:** `GuitarTracker::prepare` allocates, so the tracker is built on the message thread in
  `proc.rebuildTracker()` and swapped in under `trackerLock` (audio thread: `ScopedTryLock` only).
  Call `rebuildTracker()` after changing the calibration, the instrument, or any `track*` parameter
  (parameter changes do it by themselves through `parameterChanged`).
- The input buffer shares channels with the output, so `processBlock` copies the guitar input to
  `inputCopy` BEFORE clearing the buffer.
- Analysis frames go to the UI through `frameFifo` (lock-free). The editor timer must drain it every tick,
  even when the Guitar -> MIDI tab is hidden.
- While `proc.calibrating` is true the tracker runs but sends no MIDI. The wizard rebuilds the tracker with an
  empty calibration first, so in hex mode frame channels are raw input numbers (needed to find the channel map).
- A calibration only fits the tuning it was made for (`cal.tuning`). With another tuning, `rebuildTracker()`
  keeps only the loudness values and drops the per-string cents and channel map.
- Spectrum work (tone colour, chroma) runs on the audio thread only every 4th hop while calibrating
  (`TrackerSettings::features`), and otherwise once per note start (string guess) or at ~60 ms (chord check).
  Keep it that way: an FFT on every hop for 8 strings is too expensive.
- Mono chords: the single note starts at once (low latency); about 60 ms later, if the chroma matches a
  taught chord, the note is swapped for the whole shape. Do not delay single notes to wait for chords.
- **Record guitar:** while `proc.recordingGuitar` is true, every tracker MIDI message is stamped with a beat
  and sent through `recordFifo` to `PhrasePanel`, which feeds `PhraseRecorder`. The clock is chosen once when
  recording starts: DAW / Play position if running (notes keep their place in the bar), else a free clock at
  the current tempo. Notes are added on Stop with one undo step; recording starts at the first note.
- `PhraseRecorder` depends on the tracker's message order: on a legato change the tracker sends
  *bend (new note) -> note-on (new) -> note-off (old)* at the same time, so a bend with the same beat as a
  legato note-on belongs to the new note. A short single note that ends exactly where a chord with the same
  pitch starts is the tracker's chord swap and is dropped. Keep both in sync if the tracker changes.
- Recorded bends keep their exact curve (so the sound is faithful); the tab text rounds them to half steps.
- **Poly mode (`PolyTracker`)**, ~90 analyses a second: low-pass + downsample to ~12 kHz, 30 Hz low cut,
  2048-point FFT with a low-latency window (fast fall over the last 256 samples) on a log scale (3 bins per
  semitone), square-rooted, explained by NMF (KL, 15 rounds, warm start) with one fixed fingerprint per pitch
  plus a noise fingerprint. Fingerprints come from the Full calibration's tone colour (harmonics 2-9) when there
  is one, else `1/h^1.3`. Notes start after `stableFrames - 1` frames (+1 below 105 Hz) and must pass:
  enough share of the energy, not a harmonic "ghost" of a louder note (`isGhost`), own harmonic peaks
  (`hasHarmonicPeaks`), not inside a ringing note's bend (`isClaimed`). Strings: a small search
  (`startNotes`) - one note per string, hand span, near the hand, notes of the same strum may move string.
  ~75 ms into a strum of 3+ notes, `completeChord` adds octave notes hidden in a known shape (common shapes +
  taught chords). Re-picks: a pick attack that no new note explains. Bends: exact pitch from a 512-point FFT.
  The wizard always listens like mono (`GuitarTracker::prepare` swaps Poly for Mono while calibrating), so one
  calibration fits mono and poly. `TrackMode` values are saved in projects: only add new modes at the end.
- The poly rules were tuned on synthetic plucks (Karplus-Strong with a triangle pick shape at many pick
  positions). Before changing a threshold, run the chord tests in `testPolyTracking`; real-guitar recordings
  (WAV) are the best test when the owner has them.
- Tracking tests use synthetic decaying sawtooth "plucks" in `Tests/CoreTests.cpp`. Add a test there for any
  change to note start/stop, bend vs. legato decisions, or the calibration steps.
- **Every control needs a detailed tooltip.** First line = short title (shown bold), then plain-English lines
  that explain what it does and when to use it. Components that change by hover (fretboard, piano, tab view,
  chord cards) implement `getTooltip()` to describe what is under the mouse.
- In the Trainer tab, never show the scale layer or note names on the neck: it would give answers away.
  The chord analyser at the right of the tab row is empty there for the same reason.
- **Chord names (`nameChord`)** analyse the intervals above every possible root (3rd, 5th, 7th, 9/11/13 and
  alterations), spell each note by its degree (C7#9 has D#, Cdim7 has Bbb), and rank: root in the bass +25,
  plain triad/seventh +15, rootless -30, each alteration -8. Inversions of plain triads must beat odd altered
  names (C Eb Ab = Ab/C). `testChordNames` has ~50 cases and checks all 4095 note sets; add a case for any
  change. `parseChordName` is the reverse (symbol -> intervals + optional notes + slash bass); the round-trip
  test keeps both in step.
- Voicing search: the **bass is the lowest pitch**, not the lowest string (an open A is below fret 14 on the
  low E). `findShapes` takes any pitch-class set; `findVoicings` is a wrapper for the Reharm Lab.
- **Calibration presets:** `%APPDATA%\Guitar Lab\Calibrations\<name>.glcal`, plain `Calibration::serialise()`
  text. Loading one also switches the instrument to `cal.tuning` (through `InstrumentBar::applyTuning`).
  No pop-up windows in the plugin (some hosts handle them badly): the name box and the two-click Delete are
  in the tab itself.
- **Tab export (`Midi/TabExport`)**: `noteTabText` is THE tab notation (the tab view uses it too). Exports lay notes
  on a grid of 16ths (or triplets, only when notes really sit on triplets); the notes of one strummed chord (same
  `group`) are written in one column. The PDF is written by our own tiny writer (standard Helvetica fonts, lines,
  white boxes, text; no library): bars get the width their notes need and lines are stretched to the page; palm
  mute is "P.M. - - -" above the staff. Guitar Pro (.gp) is not written: Guitar Pro imports the MusicXML.
- **Practice tools in the processor:** `loopStart` / `loopEnd` (beats, atomics) loop Guitar Lab's own Play;
  `startCountIn()` plays one bar of clicks, then starts Play at the given bar and (for Record guitar) sets
  `recordingGuitar` in the same block, so the recording uses the transport clock. The metronome is the
  `metronome` parameter; clicks are mixed into the audio output only (never MIDI).
- Ear quizzes (`QuizMode::Ear*`) hide every dot until answered. "Play it back" is answered by
  `TrainerPanel::heard()` (the editor passes the sounding notes each tick while the Trainer tab is open);
  `ignoreUntil` skips the question's own notes.
- Full calibration measures soft/hard per string (`stringSoft` / `stringHard`, saved as `D` lines);
  `velocityFromPeak (peak, string)` uses them when present. Old files without `D` lines still load.
- Plugin state (tuning, phrase, trainer stats, reharm input, keyswitches) is saved as plain text inside the
  `GuitarLabData` child of the parameter tree. Keep old fields readable when adding new ones.

## Versioning, CI and releases

- Version = `git describe --tags --match "v[0-9]*"`, e.g. `v0.1.0` or `v0.1.0-2-gabc1234` (no tag: `dev`).
  CMake passes the number to `juce_add_plugin VERSION` and the full text as `GUITAR_LAB_VERSION`,
  shown at the right of the top bar. CI checks out with `fetch-depth: 0` so it has the tags.
- CI (`windows-latest`): `build.bat` (build + tests) -> upload **Guitar-Lab-VST3** and
  **Guitar-Lab-Standalone** -> build the installer with Inno Setup (installed via choco if missing)
  -> upload **Guitar-Lab-Installer**. Actions are pinned by commit SHA with a version comment
  (same pins as the owner's stream-bridge repo).
- Release: `git tag vX.Y.Z && git push origin vX.Y.Z` -> CI publishes a GitHub Release with
  `Guitar-Lab-vX.Y.Z.zip` (VST3 folder + standalone exe) and `Guitar-Lab-Setup-X.Y.Z.exe`.
  **Never push a tag unless the owner asks.**
- This machine cannot reach the GitHub API (no `gh`, `api.github.com` blocked), so CI results must be
  checked by the owner on the Actions page.

## Installer (installer/GuitarLab.iss)

Admin install. VST3 -> `{commoncf64}\VST3\Guitar Lab.vst3`; standalone app -> `{autopf}\Guitar Lab\`
with a Start menu shortcut (desktop shortcut optional, unchecked). The finish page can start the app
(unchecked by default). The uninstaller removes both. Keep `AppId` (`EC7CA7EC-AD0F-4735-B147-70B70766C9E9`)
unchanged forever: Windows uses it to recognise updates. Unsigned, so Windows SmartScreen will warn.

## Working with the owner

- Plain, simple English in replies and comments. Short sentences.
- Ask before: committing, pushing, running the app, or anything hard to undo.
- Commit messages and PR descriptions: one short title line, nothing else.
- Pin exact versions (JUCE tag, any new dependency). Never use floating versions.
- Bug fixes: change as few lines as possible. Suggest clean-ups separately.
