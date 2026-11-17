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

Planned next: **real-time guitar-to-MIDI tracking** with a calibration stage (asks for the tuning first).

## Build

Tools on the dev machine: Visual Studio Build Tools 2026 (MSVC 14.51), Windows SDK 10.0.26100,
CMake 4.3.1 (bundled with Build Tools). JUCE is fetched by CMake, pinned to tag `9.0.5`.

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build -G "Visual Studio 18 2026" -A x64     # first time: downloads JUCE (~2 min)
& $cmake --build build --config Release --parallel
.\build\Release\GuitarLabTests.exe                            # must print "All tests passed."
```

Outputs:
- `build/GuitarLab_artefacts/Release/VST3/Guitar Lab.vst3`
- `build/GuitarLab_artefacts/Release/Standalone/Guitar Lab.exe`

`COPY_PLUGIN_AFTER_BUILD` is off on purpose. Do not install the plugin into system folders without asking.

## Code layout

| Folder | What | Depends on JUCE? |
|---|---|---|
| `Source/Theory/` | Notes, instrument + tunings, chords + Roman numerals, reharm engine, voicing search, trainer logic | **No** |
| `Source/Midi/` | `Phrase` (the note/technique model) and `MidiRenderer` (phrase -> MIDI events) | **No** |
| `Source/Audio/` | `PluckSynth`: small built-in plucked-string sound (one voice per MIDI channel) | Yes |
| `Source/UI/` | Fretboard, piano, top bar, the three tab panels, theme + tooltip look | Yes |
| `Source/PluginProcessor.*` | Parameters, state save/load, playback in sync with the host, live notes | Yes |
| `Source/PluginEditor.*` | Lays out the window and routes fretboard/piano clicks to the open tab | Yes |
| `Tests/CoreTests.cpp` | Plain asserts for `Theory/` and `Midi/` (no framework) | No |

Keep `Theory/` and `Midi/` free of JUCE, so they stay testable and could move to another plugin format.

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
- **Every control needs a detailed tooltip.** First line = short title (shown bold), then plain-English lines
  that explain what it does and when to use it. Components that change by hover (fretboard, piano, tab view,
  chord cards) implement `getTooltip()` to describe what is under the mouse.
- In the Trainer tab, never show the scale layer or note names on the neck: it would give answers away.
- Plugin state (tuning, phrase, trainer stats, reharm input, keyswitches) is saved as plain text inside the
  `GuitarLabData` child of the parameter tree. Keep old fields readable when adding new ones.

## Working with the owner

- Plain, simple English in replies and comments. Short sentences.
- Ask before: committing, pushing, running the app, or anything hard to undo.
- Commit messages and PR descriptions: one short title line, nothing else.
- Pin exact versions (JUCE tag, any new dependency). Never use floating versions.
- Bug fixes: change as few lines as possible. Suggest clean-ups separately.
