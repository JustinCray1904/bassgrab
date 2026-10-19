# BassGrab - bass note finder for FL Studio (VST3)

Load or record a loop -> BassGrab isolates the bass, tracks its pitch, shows the notes in a piano-roll view,
and lets you **drag them straight into FL Studio's piano roll** as a MIDI clip.

```
audio  ->  isolate the bass band  ->  pitch tracking (YIN)  ->  note segmentation  ->  quantise to your BPM  ->  .mid  ->  drag into FL
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
2. BassGrab picks out the **bass line only** - melodies, chords and hats in the loop are ignored.
3. Check the **BPM**:
   - A loaded file uses **its own** tempo. If the file name contains it ("150 bpm", "BPM140") it is read automatically;
     otherwise type the loop's tempo yourself. (Notes are placed by beats, so a wrong BPM = wrong timing.)
   - A recorded take follows FL's tempo (**Host BPM** ticked).
4. Pick the part you want with **Loop** (1 / 2 / 4 / 8 / 16 / 32 bars or whole file) and **Start bar**.
   A long file opens on its first 8 bars; a short loop opens whole.
5. **Click and drag** the green **Drag MIDI into FL Studio** pad (or the notes themselves) onto a piano roll,
   a Channel Rack channel or the Playlist. **Save .mid...** writes the same file to disk.

Controls
- **Quantize**: snap notes to 1/4, 1/8, 1/16, 1/32 or leave them as played.
- **Range**: Sub/808 (28-100 Hz), Bass (30-180 Hz, default), Wide (30-300 Hz). Use the narrowest that covers your bass -
  it keeps higher instruments out. Use Wide only for high bass guitar / acid lines.
- **Sensitivity**: raise it if bass notes are missing, lower it if stray notes appear.
- **Octave**: shifts the exported MIDI (e.g. play a sub an octave up on a synth).
- Note names use FL Studio's convention (MIDI 60 = C5).
- Audio that is a few cents off A440 is detected and compensated automatically.

## Limits (be aware)

- Finds **one bass note at a time** (808s, sub, synth bass, bass guitar). It cannot pull a bass out of a track that has
  no bass, and it cannot split chords.
- It looks only at the low band, so it needs the bass to be audible there. If the bass is much quieter than the
  melody (roughly 5x) you can get a few stray notes or a missed hit - nudge Sensitivity / Range.
- Kick drums sit in the same frequency range as bass and will confuse it.
- Loads up to 2 minutes of audio, records up to 90 s.
- FL's own browser may not forward file drags into plugin windows - if dropping from FL's browser does nothing, use **Open audio...**.

## Files

```
Source/BassAnalyzer.h     pitch tracking, note segmentation, quantise, MIDI writer (no JUCE dependency)
Source/PluginProcessor.*  audio capture, file loading, background analysis thread, state
Source/PluginEditor.*     UI: piano-roll view, drag-out to the host, controls
tests/test_analyzer.cpp   bass-only tests (808 / saw, detune, sample rates, BPM-from-filename, loop window)
tests/test_mix.cpp        bass inside a melody loop (lead + chords + hats) - must return only the bass
```

Test the engine on its own (any C++17 compiler, no JUCE):

```
g++ -std=c++17 -O2 tests/test_analyzer.cpp -o test && ./test
g++ -std=c++17 -O2 tests/test_mix.cpp -o test_mix && ./test_mix
```

## Licence note

JUCE is dual-licensed (AGPLv3 or a commercial licence). Building this for your own use is fine;
if you distribute the plugin, you must comply with one of those licences.
