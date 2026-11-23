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
   One thing at a time: press Listen (or Space), play, OK, next; Back goes to the step before.
   The whole setup (settings, tuning, calibration) can be saved and loaded as **presets** (files, shared by all projects).
   **Intonation report** (`UI/IntonationView`): from a Full calibration, how far every fret plays from its open string.
2. **Play & Write** – play the neck live (drag = bend / slide) and write MIDI phrases (up to 512 bars) with
   hammer-ons, pull-offs, taps, palm mute, dead notes, harmonics, bends, slides, vibrato and strummed chords.
   **Record guitar** writes the tracked notes into the phrase as editable tab (`PhraseRecorder`), with an optional
   1-bar count-in. **Step input:** in Write mode, fretboard clicks AND real-guitar notes (`TrackerPanel::onTrackedNote`
   -> `PhrasePanel::guitarNote`, only while this tab is open and Record guitar is off) are written at the cursor;
   Horizontal moves the cursor on after each note, Vertical stacks them into a chord until Enter / Next step. Vertical
   never replaces a note: a guitar note whose string is taken on that beat goes to another string with the same pitch
   (`freeSpot`). A note the tracker moves to another string (note-off + note-on of the same pitch at the same sample)
   moves in the phrase too; a real unison keeps both. While Write mode is on (tab open, not recording),
   `proc.stepInputMono` makes `rebuildTracker` swap Poly for Mono (one note at a time: no overtone ghosts);
   `PhrasePanel::timerTick` sets it back. Practice/edit tools: loop a section (drag in the ruler), metronome, zoom, multi-select
   (Ctrl + click, box drag), copy/cut/paste, move/transpose with buttons or keys. Export: MIDI, text tab, PDF,
   MusicXML, Guitar Pro 3 (`Midi/TabExport`). Gold chord names under the bar numbers show the harmony over time
   (`Midi/ChordTimeline`); "To Reharm Lab" sends them there.
3. **Chord Shapes** – type or pick any chord symbol (`Cmaj7`, `F#m7b5`, `G7#9`, `D/F#`, `E7alt`) and see every
   playable shape for this tuning as chord diagrams (`parseChordName` + `findShapes`).
4. **Reharm Lab** – type a progression (`I V vi IV`, `Dm7 G7 Cmaj7`, lengths `C:2 G:2 Am F:8` or `| C G | Am |`,
   or drag the line between two chord cards), get substitutions
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
| `Source/Midi/` | `Phrase` (the note/technique model), `MidiRenderer` (phrase -> MIDI events), `PhraseRecorder` (tracked MIDI -> phrase notes), `TabExport` (tab text for a note; text tab, PDF, MusicXML and GP3 files), `ChordTimeline` (phrase -> chord names over time) | **No** |
| `Source/Tracking/` | `PitchDetector` (YIN on a downsampled copy), `Spectrum` (FFT, tone colour, chroma), `Calibration` + `CalibrationSession` (wizard logic), `GuitarTracker` (audio -> notes/bends/velocity/chords; runs `PolyTracker` in poly mode) | **No** |
| `Source/Audio/` | `PluckSynth`: small built-in plucked-string sound (one voice per MIDI channel) | Yes |
| `Source/UI/` | Fretboard, piano, top bar, the five tab panels (`ShapesPanel` = Chord Shapes), `IntonationView`, theme + tooltip look, `Tooltip.h` (tooltips wait 1 s), `UiMemory.h` (panel state that survives closing the window). The top bar's **Manual** button opens `Manual/index.html` next to the app, else the latest GitHub release | Yes |
| `Source/PluginProcessor.*` | Parameters, state save/load, playback in sync with the host, live notes | Yes |
| `Source/PluginEditor.*` | Lays out the window and routes fretboard/piano clicks to the open tab | Yes |
| `Tests/CoreTests.cpp` | Plain asserts for `Theory/`, `Midi/` and `Tracking/` (no framework) | No |
| `installer/GuitarLab.iss` | Inno Setup 6 installer script | - |
| `docs/manual/` | The user manual (`index.html` + `img/`). Shipped in the release zip and the installer | - |
| `tools/make-manual-images.bat` | Makes `docs/manual/img` from the real plugin window, off-screen (`tools/screenshots`, CMake option `GUITAR_LAB_SCREENSHOTS`, build folder `build-manual`). Run `build.bat` first | - |
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
- Mono legato: a pitch jump of 7+ half steps inside a ringing note must hold for 3 readings (~10 ms) before it becomes a
  new note. Such jumps are mostly an overtone read for a moment; on real licks this removed ~70% of the too-high notes.
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
  semitone), square-rooted, explained by NMF (KL, 15 rounds, warm start) with fixed fingerprints plus a noise
  fingerprint. Without calibration: one fingerprint per pitch (`1/h^1.3`). After a Full calibration: one per string
  and fret, from that spot's **attack fingerprint** (harmonics 2-16 measured 40-70 ms after the pick, `H` lines in
  the calibration file); a pitch's activation is the sum of its fingerprints. The after-attack tone colour is NOT used
  (it made Poly worse). Notes start after `stableFrames - 1` frames (+1 below 105 Hz) and must pass:
  enough share of the energy, not a harmonic "ghost" of a louder note (`isGhost`), own harmonic peaks
  (`hasHarmonicPeaks`), not inside a ringing note's bend (`isClaimed`). **Hysteresis** (`trackHysteresis`, 0..1,
  default 0 = off): a pitch at +12/19/24/28/31 above a LOUDER note picked in the last ~8 frames (a candidate, a note
  whose energy jumped at the attack, or a young note; re-picked notes do not count, or arpeggios break) needs up to
  4x more share. It never delays a note. **Clear-note veto** (always on): voice 0's one-note pitch detector still runs in
  poly mode and hands each reading to Poly (`PolyTracker::hint`, stamped with its input sample, so Poly never uses a
  reading from after its own analysis). When it hears one note clearly (clarity > 0.9, 2 readings in a row) and that note
  was just picked (not ringing for 8+ frames, not re-picked), Poly starts no note at +12/19/24/28/31 above it. On real
  licks this halved the wrong notes on single notes; it costs ~3% of the notes in chord licks (an exception for
  "another note in the same pick" was tried and let most mistakes back in). Strings: a small search
  (`startNotes`) - one note per string, hand span, near the hand, notes of the same strum may move string.
  In a strum of 3+ notes, `completeChord` adds octave notes hidden in a known shape (common shapes + taught chords)
  as soon as a new note starts (and again at ~75 and ~130 ms); when the pitches fit a shape exactly, notes move to
  the shape's strings. Real octaves in chords look like harmonic ghosts in the spectrum, so the shape is the proof.
  Re-picks: a pick attack that no new note explains. Bends: exact pitch from a 512-point FFT.
  The wizard always listens like mono (`GuitarTracker::prepare` swaps Poly for Mono while calibrating), so one
  calibration fits mono and poly. `TrackMode` values are saved in projects: only add new modes at the end.
- The poly rules were tuned on synthetic plucks (Karplus-Strong with a triangle pick shape at many pick
  positions). Before changing a threshold, run the chord tests in `testPolyTracking`; real-guitar recordings
  (WAV) are the best test when the owner has them.
- Tracking tests use synthetic decaying sawtooth "plucks" in `Tests/CoreTests.cpp`. Add a test there for any
  change to note start/stop, bend vs. legato decisions, or the calibration steps.
- **The window can be deleted at any time** (DAWs delete the editor when they hide it). Nothing may live only in a
  panel: control settings are remembered with `UiMemory` (`memory.remember (control, "key")`, saved in
  `proc.uiState` and in the project); work in progress (reharm result, trainer question, undo history, the
  calibration session) lives in `proc.work` and panels use it through references. `UiMemory` must be the LAST
  member of a panel. Open tab and window size are in `proc.uiState` too. Add new controls to the memory.
- **One scroll bar for the whole page** (`GuitarLabEditor::layoutPage`). Panels do not scroll up/down by themselves:
  a panel with a growing list reports `heightFor (width)` and calls `onHeightChanged`; the editor makes the page
  taller. Only the Play & Write tab view scrolls sideways.
- **Calibration flow (`CalibrationSession`):** Ready -> `listen()` -> Listening -> OK (next item, Ready) or Decide
  (Again / Use it anyway / Skip). Frames only count while listening, and only after a NEW pick attack (or sound after
  a quiet moment), so a note still ringing from the step before is ignored. Each fret of a Full calibration is its own
  item. `back()` restores the snapshot taken before the previous item. 10 s of nothing: back to Ready.
  **Auto listen** (wizard switch + delay, remembered in `UiMemory` "Calibration") presses Listen by itself a set time
  after a step is Ready; never in Decide, and "Stop listening" pauses it until the next step.
  **Sound cues** (`proc.playCue`, audio output only): tick = listening started, ding-ding = OK, low tone = problem.
  The wizard finds them by comparing phase and step number with the last tick (`playCues`).
  **Unfinished calibration:** while the wizard is on its Running page, the project saves `calibrationDraft`
  (`CalibrationSession::saveDraft`: plan, step, tuning, levels, the calibration so far). Loading it opens the wizard
  at the same step; Back cannot go to steps from before the load (`backLimit`).
- **Lighthouse tuner** (`UI/LighthouseTuner`, "Tuner..." on the Guitar -> MIDI tab): the processor copies the raw input
  into `tunerRing` ONLY while `proc.tunerOn` is true (tuner open). The tuner runs its own YIN on every screen refresh
  (`VBlankAttachment`) and smooths the cents; closed, it costs nothing. **Note log** (`NoteLogView`): every tracker
  note-on/off goes through `trackedFifo` (`popTrackedEvents`), drained by `TrackerPanel::timerTick`.
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
- **Presets (the whole setup):** `%APPDATA%\Guitar Lab\Calibrations\<name>.glcal` (old folder and extension kept, so
  old presets still show), `SetupPreset` text: `GLPRESET 1`, the tuning, the parameters in `kPresetParams`
  (`TrackerPanel.cpp`: all but metronome, tempo and host sync) and the calibration. Old files are a bare
  `Calibration::serialise()` and load as calibration only. Loading switches the instrument to the preset's tuning
  (through `InstrumentBar::applyTuning`). Add new setup parameters to `kPresetParams`.
  No pop-up windows in the plugin (some hosts handle them badly): the name box and the two-click Delete are
  in the tab itself.
- **Tab export (`Midi/TabExport`)**: `noteTabText` is THE tab notation (the tab view uses it too). Exports lay notes
  on a grid of 16ths (or triplets, only when notes really sit on triplets); the notes of one strummed chord (same
  `group`) are written in one column. The PDF is written by our own tiny writer (standard Helvetica fonts, lines,
  white boxes, text; no library): bars get the width their notes need and lines are stretched to the page; palm
  mute is "P.M. - - -" above the staff. Guitar Pro 3 (.gp3) is written by hand too (up to 7 strings, no palm mute in
  that format); the newer .gp format is not written.
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
- **Every release ships an updated user manual.** Before tagging: update `docs/manual/index.html` for every
  feature change, run `tools\make-manual-images.bat` so the pictures match, and check that every `<img>` exists.
  `.gitattributes` keeps `*.pdf` binary (else Windows checkout breaks the example PDF in the manual).
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
