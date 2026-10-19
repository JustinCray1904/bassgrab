# BassGrab - bass note finder for FL Studio (VST3)

Load or record a loop -> BassGrab tracks the bass pitch, shows the notes in a piano-roll view,
and lets you **drag them straight into FL Studio's piano roll** as a MIDI clip.

```
audio  ->  pitch tracking (YIN)  ->  note segmentation  ->  quantise to your BPM  ->  .mid  ->  drag into FL
```

## Build with GitHub Actions (no local setup)

1. Create a new GitHub repo and push this folder to it (the `.github/workflows/build.yml` file must be included).
2. Open the repo's **Actions** tab. A run called *Build BassGrab* starts on every push (or press **Run workflow**).
3. When it finishes (about 5-10 min), open the run and download **BassGrab-Windows** from *Artifacts*.
   Unzip it: inside `VST3/` is `BassGrab.vst3` - copy that folder to `C:\Program Files\Common Files\VST3\`.
4. Optional: tag a version (`git tag v1.0.0 && git push --tags`) and the workflow publishes a GitHub **Release**
   with the Windows and macOS zips attached.

If the run fails, open the failed step, copy the red error lines and send them over - the UI code was written without
being able to compile it against JUCE, so a first-run compile error is possible and is a one-line fix.

The engine tests run first on every push, so you will also see if a change breaks the note detection.

## Build locally (Windows)

You need: Visual Studio 2022 ("Desktop development with C++"), CMake 3.22+, Git.

```
cd BassGrab
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

First configure downloads JUCE (a minute or two). The plugin ends up in:

```
build\BassGrab_artefacts\Release\VST3\BassGrab.vst3
```

Copy that whole `BassGrab.vst3` folder to `C:\Program Files\Common Files\VST3\`, then in FL Studio:
**Options > Manage plugins > Find installed plugins** (make sure that folder is in the scan list).
Add **BassGrab** to a mixer insert (it is an effect, so it can listen to audio).

macOS: `cmake -B build -G Xcode && cmake --build build --config Release`, copy the .vst3 to `~/Library/Audio/Plug-Ins/VST3`.

There is also a `Standalone` build next to the VST3 - useful for quickly trying files without FL.

## Use

1. Get audio in, either way:
   - **Drop** a WAV/AIFF/FLAC/MP3/OGG onto the plugin window, or press **Open audio...**
   - or press **Record from input**, play your loop through that mixer track, press **Stop and analyse**
2. Set **BPM** (it follows FL's tempo automatically; untick *Host BPM* to type a different one - set it to
   the tempo of the loop you loaded) and **Quantize** (Off / 1/4 / 1/8 / 1/16 / 1/32).
3. **Click and drag the notes** out of the plugin onto the Piano roll (or a Channel Rack channel / Playlist).
   *Save .mid...* writes the same file to disk instead.

Controls
- **Range**: Sub/808 (30-130 Hz), Bass (32-260 Hz), Low-mid (40-500 Hz). Pick the narrowest that fits - it cuts false hits.
- **Sensitivity**: raise it if notes are missing, lower it if you get junk notes.
- **Octave**: shifts the exported MIDI (handy to drop a sub an octave up so a synth can play it).
- Note names use FL Studio's convention (MIDI 60 = C5).
- The tuning of the audio is detected and compensated, so a loop that is 30 cents sharp still gives the right notes.

## Limits (be aware)

- **Monophonic only.** One note at a time - 808s, sub bass, synth bass, bass guitar. Chords or a full mix will not work;
  run a bass stem / bass-only loop through it. Kicks that overlap the bass range will confuse it.
- Loop tempo is not auto-detected: BPM comes from FL (or what you type).
- Loads up to 2 minutes of audio, records up to 90 s.
- FL's own browser may not forward file drags into plugin windows - if dropping from FL's browser does nothing, use **Open audio...**.

## Files

```
Source/BassAnalyzer.h     pitch tracking, note segmentation, quantise, MIDI writer (no JUCE dependency)
Source/PluginProcessor.*  audio capture, file loading, background analysis thread, state
Source/PluginEditor.*     UI: piano-roll view, drag-out to the host, controls
tests/test_analyzer.cpp   synthetic bass-line tests for the engine
```

Test the engine on its own (any C++17 compiler, no JUCE):

```
g++ -std=c++17 -O2 tests/test_analyzer.cpp -o test && ./test
```

## Licence note

JUCE is dual-licensed (AGPLv3 or a commercial licence). Building this for your own use is fine;
if you distribute the plugin, you must comply with one of those licences.
