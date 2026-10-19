// Bass inside a full "melody loop": lead melody + chords + hi-hats on top of a bass line.
// The engine must return ONLY the bass notes.
#ifndef BASS_HEADER
#define BASS_HEADER "../Source/BassAnalyzer.h"
#endif
#include BASS_HEADER
#ifndef BASS_AMP
#define BASS_AMP 0.9f
#endif
#ifndef BASS_HARM
#define BASS_HARM 3
#endif
#ifndef LOW_CHORD
#define LOW_CHORD 58
#endif
#ifndef HAT_AMP
#define HAT_AMP 0.12f
#endif
#ifndef SR
#define SR 44100
#endif
#ifndef DETUNE
#define DETUNE 0.0
#endif
#include <cstdio>
#include <random>

using namespace bassgrab;

struct Note { double start, len; int midi; };

static void addTone (std::vector<float>& y, double sr, const Note& n, int harmonics, float amp, double decay, bool pluck)
{
    const double f = 440.0 * std::pow (2.0, (n.midi - 69 + DETUNE / 100.0) / 12.0);
    const size_t i0 = (size_t) (n.start * sr), len = (size_t) (n.len * sr);
    double ph = 0;
    for (size_t i = 0; i < len && i0 + i < y.size(); ++i)
    {
        const double t = (double) i / sr;
        ph += 2 * kPi * f / sr;
        float v = 0;
        for (int h = 1; h <= harmonics; ++h) v += (float) std::sin (h * ph) / (float) std::pow (h, 1.1);
        float env = (float) (t < 0.004 ? t / 0.004 : 1.0);
        if (pluck) env *= (float) std::exp (-t * decay);
        if (t > n.len - 0.02) env *= (float) std::max (0.0, (n.len - t) / 0.02);
        y[i0 + i] += amp * v * env;
    }
}

int main()
{
    const double sr = SR, bpm = 150, beat = 60.0 / bpm;
    const double total = 4 * 4 * beat + 0.5;                  // 4 bars + tail
    std::vector<float> y ((size_t) (sr * total), 0.0f);
    std::mt19937 rng (7);

    // --- bass line (what we want back): A#m-ish, 808 + saw mix, MIDI 22..37 -------------------
    std::vector<Note> bass;
    const int bassNotes[] = { 34, 34, 29, 29, 37, 34, 30, 30, 32, 32, 29, 25, 34, 34, 37, 29 };
    for (int i = 0; i < 16; ++i) bass.push_back ({ i * beat, beat * 0.9, bassNotes[i] });
    for (auto& b : bass) addTone (y, sr, b, BASS_HARM, BASS_AMP, 2.5, true);

    // --- melody lead: fast notes MIDI 58..84, plucky --------------------------------------------
    const int scale[] = { 58, 61, 63, 65, 68, 70, 73, 75, 77, 80, 82 };
    for (double t = 0; t < total - 0.3; t += beat / 2)
        addTone (y, sr, { t, beat * 0.45, scale[rng() % 11] }, 7, 0.45f, 6.0, true);

    // --- chords every bar: 4 notes MIDI 58..70 (pad-like, sustained) -------------------------------
    for (int bar = 0; bar < 4; ++bar)
        for (int m : { LOW_CHORD, 61, 65, 70 })
            addTone (y, sr, { bar * 4 * beat, 4 * beat * 0.98, m + (bar % 2) * 2 }, 5, 0.22f, 0.8, false);

    // --- hi-hats -----------------------------------------------------------------------------------
    std::uniform_real_distribution<float> u (-1, 1);
    for (double h = 0; h < total - 0.1; h += beat / 4)
    {
        float prev = 0; const size_t i0 = (size_t) (h * sr);
        for (size_t i = 0; i < (size_t) (0.04 * sr) && i0 + i < y.size(); ++i)
        { const float n = u (rng); y[i0 + i] += HAT_AMP * (n - prev) * (float) std::exp (-(double) i / (0.01 * sr)); prev = n; }
    }

    Settings s;
    auto a = analyse (y.data(), y.size(), sr, s);

    int good = 0, extra = 0, missed = 0;
    for (auto& n : a.notes)
    {
        bool ok = false;
        for (auto& b : bass)
            if (n.midi == b.midi && n.startSec < b.start + b.len && n.endSec > b.start) ok = true;
        ok ? ++good : ++extra;
    }
    for (auto& b : bass)
    {
        bool found = false;
        for (auto& n : a.notes) if (n.midi == b.midi && std::abs (n.startSec - b.start) < 0.07) found = true;
        if (! found) ++missed;
    }
    std::printf ("detected %zu notes | bass hits found %d/16 | FALSE (non-bass) notes: %d | above MIDI 45: ",
                 a.notes.size(), 16 - missed, extra);
    int hi = 0; for (auto& n : a.notes) if (n.midi > 45) ++hi;
    std::printf ("%d\n", hi);
    return (missed > 1 || extra > 2) ? 1 : 0;
}
