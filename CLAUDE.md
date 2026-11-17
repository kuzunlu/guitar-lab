# Guitar Lab

A VST3 instrument plugin (plus a standalone app) for guitarists. Built with C++20 and JUCE.
Three tools share one 24-fret neck (6, 7 or 8 strings, any tuning) and a piano view at the bottom:

1. **Play & Write** – play the neck live (drag = bend / slide) and write MIDI phrases with
   hammer-ons, pull-offs, taps, palm mute, dead notes, harmonics, bends, slides, vibrato and strummed chords.
2. **Reharm Lab** – type a progression (`I V vi IV`, `Dm7 G7 Cmaj7`), get substitutions
   (tritone sub, secondary dominant, borrowed chord, passing diminished, related ii-V, diatonic sub,
   backdoor dominant, extensions) with playable, voice-led voicings.
3. **Trainer** – note-name, find-the-note, arpeggio and interval quizzes, focused on chosen strings
   and frets, weighted toward the player's weak spots, with a heat map.

4. **Guitar -> MIDI** – real-time tracking of a real guitar from the audio input.
   *Mono* mode (normal guitar, one cable): single notes with bends, slides, vibrato; string/fret is guessed
   from the hand position; chords are not reliable. *Hex* mode (hexaphonic pickup, one input per string):
   every string tracked on its own. A calibration wizard asks for the tuning first, then listens to silence,
   each open string, the 12th fret, and soft/hard picking (Quick). *Full* calibration instead asks for every
   fret on every string (real pitch, level and tone colour per spot; hex: string-to-string leak) and, in mono
   mode, ~8 chords (chroma fingerprints). Mono mode then guesses the string from tone colour and recognises
   taught chords.

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

## Code layout

| Folder | What | Depends on JUCE? |
|---|---|---|
| `Source/Theory/` | Notes, instrument + tunings, chords + Roman numerals, reharm engine, voicing search, trainer logic | **No** |
| `Source/Midi/` | `Phrase` (the note/technique model) and `MidiRenderer` (phrase -> MIDI events) | **No** |
| `Source/Tracking/` | `PitchDetector` (YIN on a downsampled copy), `Spectrum` (FFT, tone colour, chroma), `Calibration` + `CalibrationSession` (wizard logic), `GuitarTracker` (audio -> notes/bends/velocity/chords) | **No** |
| `Source/Audio/` | `PluckSynth`: small built-in plucked-string sound (one voice per MIDI channel) | Yes |
| `Source/UI/` | Fretboard, piano, top bar, the four tab panels, theme + tooltip look | Yes |
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
- Tracking tests use synthetic decaying sawtooth "plucks" in `Tests/CoreTests.cpp`. Add a test there for any
  change to note start/stop, bend vs. legato decisions, or the calibration steps.
- **Every control needs a detailed tooltip.** First line = short title (shown bold), then plain-English lines
  that explain what it does and when to use it. Components that change by hover (fretboard, piano, tab view,
  chord cards) implement `getTooltip()` to describe what is under the mouse.
- In the Trainer tab, never show the scale layer or note names on the neck: it would give answers away.
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
