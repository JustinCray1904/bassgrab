#include "../Source/BassAnalyzer.h"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace bassgrab;

struct Truth { double start, len; int midi; int kind; };

static std::vector<float> render (const std::vector<Truth>& seq, double sr, double seconds,
                                  double detuneCents, bool hats, double noiseAmp)
{
    std::vector<float> y ((size_t) (sr * seconds), 0.0f);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> u (-1.f, 1.f);

    for (auto& t : seq)
    {
        const double f = 440.0 * std::pow (2.0, (t.midi - 69 + detuneCents / 100.0) / 12.0);
        const size_t i0 = (size_t) (t.start * sr), n = (size_t) (t.len * sr);
        double ph = 0.0;
        for (size_t i = 0; i < n && i0 + i < y.size(); ++i)
        {
            const double tt = (double) i / sr;
            float v = 0.f;
            if (t.kind == 0)   // 808: sine, tiny pitch drop at the start, exponential decay, soft drive
            {
                const double fi = f * (1.0 + 0.04 * std::exp (-tt * 60.0));
                ph += 2 * kPi * fi / sr;
                v = (float) std::tanh (1.8 * std::sin (ph)) * (float) std::exp (-tt * 3.0);
                if (tt < 0.003) v *= (float) (tt / 0.003);
            }
            else               // gated saw-ish bass (8 harmonics), 5 ms attack, 30 ms release
            {
                ph += 2 * kPi * f / sr;
                for (int h = 1; h <= 8; ++h) v += (float) (std::sin (h * ph) / h);
                v *= 0.5f;
                float env = 1.f;
                if (tt < 0.005) env = (float) (tt / 0.005);
                if (tt > t.len - 0.03) env = (float) std::max (0.0, (t.len - tt) / 0.03);
                v *= env;
            }
            y[i0 + i] += 0.8f * v;
        }
    }
    if (hats)  // noisy hi-hats on every 8th note
        for (double h = 0.25; h < seconds - 0.1; h += 0.25)
        {
            const size_t i0 = (size_t) (h * sr);
            float prev = 0.f;
            for (size_t i = 0; i < (size_t) (0.05 * sr) && i0 + i < y.size(); ++i)
            {
                const float n = u (rng); const float hp = n - prev; prev = n;   // crude high-pass
                y[i0 + i] += 0.25f * hp * (float) std::exp (-(double) i / (0.012 * sr));
            }
        }
    if (noiseAmp > 0)
        for (auto& s : y) s += (float) noiseAmp * u (rng);
    return y;
}

static int check (const char* name, const std::vector<Truth>& seq, double sr, double detune, bool hats, double noise)
{
    auto y = render (seq, sr, 4.5, detune, hats, noise);
    Settings s;
    auto a = analyse (y.data(), y.size(), sr, s);

    std::printf ("\n== %s  (sr %.0f, detune %+.0f cents, hats %d, noise %.3f)  tuning detected %.1f c ==\n",
                 name, sr, detune, hats, noise, a.tuningCents);
    int pitchOk = 0, startOk = 0;
    for (auto& n : a.notes)
        std::printf ("  %-4s  %.3f -> %.3f  lvl %.2f\n", noteName (n.midi).c_str(), n.startSec, n.endSec, n.level);

    for (auto& t : seq)
    {
        const DetectedNote* best = nullptr; double bd = 1e9;
        for (auto& n : a.notes) { const double dd = std::abs (n.startSec - t.start); if (dd < bd) { bd = dd; best = &n; } }
        const bool p = best && best->midi == t.midi && bd < 0.08;
        pitchOk += p; startOk += (best && bd < 0.04);
        std::printf ("  want %-4s @%.2f  -> %s (start err %s ms)\n", noteName (t.midi).c_str(), t.start,
                     p ? "OK  " : "MISS", best ? std::to_string ((int) std::lround ((best->startSec - t.start) * 1000)).c_str() : "-");
    }
    std::printf ("  ==> %d/%zu notes correct, %d/%zu starts within 40 ms, %zu detected total\n",
                 pitchOk, seq.size(), startOk, seq.size(), a.notes.size());
    return (int) seq.size() - pitchOk;
}

int main()
{
    // 120 bpm, beat = 0.5 s.  E1 retriggered twice, a short G1, legato A1->D2, a gap, B0.
    const std::vector<Truth> sub = {
        { 0.0, 0.45, 28, 0 }, { 0.5, 0.45, 28, 0 }, { 1.0, 0.30, 31, 0 }, { 1.5, 0.48, 33, 0 },
        { 2.0, 0.48, 38, 0 }, { 3.0, 0.45, 35, 0 }, { 3.5, 0.9, 40, 0 } };
    const std::vector<Truth> saw = {
        { 0.0, 0.22, 36, 1 }, { 0.25, 0.22, 36, 1 }, { 0.5, 0.45, 39, 1 }, { 1.0, 0.22, 43, 1 },
        { 1.5, 0.48, 41, 1 }, { 2.0, 0.23, 48, 1 }, { 2.5, 0.45, 46, 1 }, { 3.0, 0.9, 34, 1 } };

    int miss = 0;
    miss += check ("808 sub line",          sub, 44100, 0,   false, 0.0);
    miss += check ("808 sub, 48 kHz",       sub, 48000, 0,   false, 0.0);
    miss += check ("808 sub, +30c detune",  sub, 44100, 30,  false, 0.0);
    miss += check ("saw bass + hats",       saw, 44100, 0,   true,  0.003);
    miss += check ("saw bass, -35c, hats",  saw, 48000, -35, true,  0.003);

    // MIDI writer smoke test
    Analysis a; auto y = render (sub, 44100, 4.5, 0, false, 0);
    a = analyse (y.data(), y.size(), 44100, Settings{});
    auto ex = toExportNotes (a, 120.0, 0.25, 0);
    std::printf ("\nQuantised to 1/16 @120bpm:\n");
    for (auto& n : ex) std::printf ("  %-4s beat %.2f len %.2f vel %d\n", noteName (n.midi).c_str(), n.startBeat, n.lengthBeat, n.velocity);
    auto bytes = makeMidiFile (ex, 120.0, "BassGrab");
    FILE* f = std::fopen ("/tmp/test_bass.mid", "wb");
    std::fwrite (bytes.data(), 1, bytes.size(), f); std::fclose (f);
    std::printf ("MIDI bytes: %zu\n", bytes.size());

    // loop window: bars 2-3 only (beats 4..12 at 120 bpm) must come back re-based to beat 0
    auto win = toExportNotes (a, 120.0, 0.25, 0, 4.0, 4.0);
    std::printf ("window beats 4..8 -> %zu notes, first at beat %.2f\n", win.size(), win.empty() ? -1.0 : win[0].startBeat);
    if (win.empty() || win[0].startBeat != 0.0 || win.back().startBeat + win.back().lengthBeat > 4.0 + 1e-9) { std::printf ("WINDOW FAIL\n"); ++miss; }

    const char* names[] = { "her fragrance - 150 bpm - A#m (A#m - F#) @1prodprathxm.mp3", "Loop_BPM140_Cm.wav", "melody 93BPM.wav",
                            "no tempo here.wav", "synth 2024 take 3.wav", "bpm: 128 bass.wav" };
    const double want[] = { 150, 140, 93, 0, 0, 128 };
    for (int i = 0; i < 6; ++i)
    {
        const double got = parseBpmFromName (names[i]);
        std::printf ("  bpm from \"%s\" -> %.0f %s\n", names[i], got, got == want[i] ? "OK" : "WRONG");
        if (got != want[i]) ++miss;
    }
    std::printf ("\nTOTAL MISSES: %d\n", miss);
    return miss ? 1 : 0;
}
