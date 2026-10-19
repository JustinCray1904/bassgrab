#pragma once
// BassAnalyzer.h  -  pure C++17, no JUCE dependency (so it can be unit-tested on its own).
//
//   audio  -->  band-limit + decimate  -->  YIN pitch per 10 ms frame  -->  note segmentation
//          -->  quantise to the beat grid  -->  Standard MIDI File bytes
//
// Designed for MONOPHONIC bass (808s, sub, synth bass, bass guitar). Chords / full mixes will
// give garbage - feed it an isolated bass stem or a bass-only loop.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bassgrab
{
constexpr double kPi = 3.14159265358979323846;

struct Settings
{
    float minHz        = 32.0f;   // lowest fundamental we look for
    float maxHz        = 260.0f;  // highest fundamental we look for
    float yinThreshold = 0.15f;   // lower = stricter (fewer, cleaner notes)
    float minNoteSec   = 0.05f;   // notes shorter than this are discarded
    float floorDb      = -45.0f;  // anything quieter than this (vs the loudest part) is silence
};

struct DetectedNote
{
    double startSec = 0.0, endSec = 0.0;
    int    midi  = 60;      // standard MIDI number (69 = A440)
    float  level = 1.0f;    // 0..1, loudness relative to the loudest note
};

struct Analysis
{
    std::vector<DetectedNote> notes;
    double durationSec  = 0.0;
    double tuningCents  = 0.0;   // how far the audio was detuned from A440 (already compensated)
};

struct ExportNote
{
    double startBeat = 0.0, lengthBeat = 0.0;
    int    midi = 60, velocity = 100;
};

namespace detail
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad make (double fs, double fc, bool highpass)
    {
        const double w0 = 2.0 * kPi * fc / fs, c = std::cos (w0);
        const double al = std::sin (w0) / (2.0 * 0.70710678), a0 = 1.0 + al;
        Biquad f;
        if (! highpass) { f.b0 = (1 - c) * 0.5 / a0; f.b1 = (1 - c) / a0;  f.b2 = f.b0; }
        else            { f.b0 = (1 + c) * 0.5 / a0; f.b1 = -(1 + c) / a0; f.b2 = f.b0; }
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - al) / a0;
        return f;
    }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};
} // namespace detail

// ------------------------------------------------------------------------------------------
inline Analysis analyse (const float* x, size_t n, double sr, const Settings& s)
{
    Analysis out;
    if (x == nullptr || n < 4096 || sr < 8000.0)
        return out;

    out.durationSec = (double) n / sr;

    // ---- 1. band-limit and decimate to ~6 kHz (bass lives well below 500 Hz) -----------------
    const int    factor = std::max (1, (int) std::lround (sr / 6000.0));
    const double fs     = sr / factor;
    const double lpHz   = std::min ((double) s.maxHz * 3.0, fs * 0.4);

    auto lp1 = detail::Biquad::make (sr, lpHz, false);
    auto lp2 = detail::Biquad::make (sr, lpHz, false);
    auto hp  = detail::Biquad::make (sr, std::max (15.0, s.minHz * 0.6), true);

    std::vector<float> d;
    d.reserve (n / (size_t) factor + 1);
    for (size_t i = 0; i < n; ++i)
    {
        const float v = hp.process (lp2.process (lp1.process (x[i])));
        if (i % (size_t) factor == 0)
            d.push_back (v);
    }

    // ---- 2. frame layout ---------------------------------------------------------------------
    const int W      = std::clamp ((int) (2.5 * fs / s.minHz), 256, 1024);       // YIN window
    const int hop    = std::max (16, (int) std::lround (fs * 0.010));            // ~10 ms
    const int tauMin = std::max (2, (int) (fs / s.maxHz));
    const int tauMax = (int) std::ceil (fs / s.minHz) + 1;
    const int span   = W + tauMax + 2;
    const int N0     = (int) d.size();
    d.insert (d.end(), (size_t) span + 8, 0.0f);                                  // zero pad the tail
    const int nFrames = N0 / hop;
    if (nFrames < 8)
        return out;

    // ---- 3. loudness envelope (window >= one period of the lowest note => no ripple) ---------
    const int Wenv = (int) std::ceil (fs / s.minHz);
    const int lag  = std::max (1, (int) std::lround ((double) Wenv / hop));
    std::vector<float> env ((size_t) nFrames, 0.0f);
    float peakEnv = 0.0f;
    for (int i = 0; i < nFrames; ++i)
    {
        double e = 0.0;
        const float* p = &d[(size_t) i * hop];
        for (int j = 0; j < Wenv; ++j)
            e += (double) p[j] * p[j];
        env[(size_t) i] = (float) std::sqrt (e / Wenv);
        peakEnv = std::max (peakEnv, env[(size_t) i]);
    }
    if (peakEnv < 1e-5f)
        return out;
    const float floorLin = peakEnv * std::pow (10.0f, s.floorDb / 20.0f);

    // onsets: a sudden jump in level. Index is shifted by `lag` so it lines up with note starts.
    std::vector<char> onset ((size_t) nFrames + 1, 0);
    {
        int lastOn = -100;
        for (int k = 1; k < nFrames; ++k)
        {
            float mn = env[(size_t) k - 1];
            for (int q = 2; q <= 4 && k - q >= 0; ++q)
                mn = std::min (mn, env[(size_t) (k - q)]);
            if (env[(size_t) k] > std::max (mn * 2.0f, floorLin * 3.0f) && k - lastOn >= 6)
            {
                const int idx = std::min (nFrames - 1, k + lag);
                onset[(size_t) idx] = 1;
                lastOn = k;
            }
        }
    }

    // ---- 4. YIN per frame --------------------------------------------------------------------
    std::vector<float> diff ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    std::vector<float> midiF ((size_t) nFrames, 0.0f), conf ((size_t) nFrames, 0.0f);
    std::vector<char>  voiced ((size_t) nFrames, 0);

    for (int i = 0; i < nFrames; ++i)
    {
        if (env[(size_t) i] < floorLin)
            continue;

        const float* p = &d[(size_t) i * hop];
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const float* q = p + tau;
            float sum = 0.0f;
            for (int j = 0; j < W; ++j) { const float t = p[j] - q[j]; sum += t * t; }
            diff[(size_t) tau] = sum;
        }

        cm[0] = 1.0f;
        float run = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += diff[(size_t) tau];
            cm[(size_t) tau] = run > 1e-9f ? diff[(size_t) tau] * (float) tau / run : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < s.yinThreshold)
            {
                while (tau + 1 <= tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0 || best >= tauMax)
            continue;

        double tauF = best;
        {
            const double a0 = cm[(size_t) best - 1], b0 = cm[(size_t) best], c0 = cm[(size_t) best + 1];
            const double den = a0 - 2.0 * b0 + c0;
            if (std::abs (den) > 1e-9)
                tauF += std::clamp (0.5 * (a0 - c0) / den, -1.0, 1.0);
        }
        const double f0 = fs / tauF;
        if (f0 < s.minHz * 0.9 || f0 > s.maxHz * 1.1)
            continue;

        voiced[(size_t) i] = 1;
        midiF[(size_t) i]  = (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
        conf[(size_t) i]   = 1.0f - cm[(size_t) best];
    }

    // ---- 5. global tuning offset (loops are often a few cents / a quarter-tone off) ----------
    double tune = 0.0;
    {
        double sc = 0, ss = 0; int cnt = 0;
        for (int i = 0; i < nFrames; ++i)
            if (voiced[(size_t) i] && conf[(size_t) i] > 0.9f)
            {
                const double dev = midiF[(size_t) i] - std::round (midiF[(size_t) i]);
                sc += std::cos (2.0 * kPi * dev);
                ss += std::sin (2.0 * kPi * dev);
                ++cnt;
            }
        if (cnt >= 8 && std::hypot (sc, ss) / cnt > 0.7)
            tune = std::atan2 (ss, sc) / (2.0 * kPi);
    }
    out.tuningCents = tune * 100.0;

    // ---- 6. median-smooth the pitch track into integer note labels ---------------------------
    std::vector<int> label ((size_t) nFrames, -1);
    for (int i = 0; i < nFrames; ++i)
    {
        if (! voiced[(size_t) i]) continue;
        float v[5]; int c = 0;
        for (int j = std::max (0, i - 2); j <= std::min (nFrames - 1, i + 2); ++j)
            if (voiced[(size_t) j]) v[c++] = midiF[(size_t) j];
        if (c < 3) continue;
        for (int a = 1; a < c; ++a)                         // tiny insertion sort
            for (int b = a; b > 0 && v[b - 1] > v[b]; --b) std::swap (v[b - 1], v[b]);
        label[(size_t) i] = (int) std::lround ((double) v[c / 2] - tune);
    }

    // ---- 7. group frames into notes ----------------------------------------------------------
    struct Run { int note, start, last; };
    std::vector<Run> runs;
    {
        const int maxGap = 3;
        bool active = false, pendingSplit = false;
        Run cur { 0, 0, 0 };
        for (int i = 0; i < nFrames; ++i)
        {
            if (onset[(size_t) i]) pendingSplit = true;
            const int nt = label[(size_t) i];
            if (nt < 0)
            {
                if (active && i - cur.last > maxGap) { runs.push_back (cur); active = false; }
                continue;
            }
            const bool split = pendingSplit;
            pendingSplit = false;
            if (active && nt == cur.note && ! split) { cur.last = i; continue; }
            if (active) runs.push_back (cur);
            cur = { nt, i, i };
            active = true;
        }
        if (active) runs.push_back (cur);
    }

    const double secPerFrame = (double) hop / fs;
    for (size_t r = 0; r < runs.size(); ++r)
    {
        int startF = runs[r].start;
        for (int k = std::max (0, startF - 4); k <= startF; ++k)       // snap to a just-before onset
            if (onset[(size_t) k]) { startF = k; break; }

        const int limit = (r + 1 < runs.size()) ? runs[r + 1].start : nFrames;

        float pk = 0.0f;
        for (int k = std::max (0, startF - lag); k < std::min (nFrames, startF + 10); ++k)
            pk = std::max (pk, env[(size_t) k]);

        int endF = runs[r].last + 1;
        while (endF < limit && endF < nFrames && env[(size_t) endF] > std::max (pk * 0.12f, floorLin))
            ++endF;

        DetectedNote nt;
        nt.midi     = runs[r].note;
        nt.startSec = startF * secPerFrame;
        nt.endSec   = endF * secPerFrame;
        nt.level    = std::clamp (pk / peakEnv, 0.0f, 1.0f);
        out.notes.push_back (nt);
    }

    std::sort (out.notes.begin(), out.notes.end(),
               [] (const DetectedNote& a, const DetectedNote& b) { return a.startSec < b.startSec; });

    for (size_t i = 0; i + 1 < out.notes.size(); ++i)
        out.notes[i].endSec = std::min (out.notes[i].endSec, out.notes[i + 1].startSec);

    out.notes.erase (std::remove_if (out.notes.begin(), out.notes.end(),
                     [&] (const DetectedNote& nt) { return nt.endSec - nt.startSec < s.minNoteSec; }),
                     out.notes.end());
    return out;
}

// ------------------------------------------------------------------------------------------
// seconds -> beats, quantise, transpose, make sure the line stays monophonic
inline std::vector<ExportNote> toExportNotes (const Analysis& a, double bpm, double gridBeats, int transpose)
{
    std::vector<ExportNote> out;
    if (bpm <= 0.0) return out;
    const double bps = bpm / 60.0;

    struct Tmp { double s, e, origLen; int midi, vel; };
    std::vector<Tmp> t;
    for (const auto& n : a.notes)
    {
        double s = n.startSec * bps, e = n.endSec * bps;
        const double origLen = e - s;
        if (gridBeats > 0.0)
        {
            s = std::round (s / gridBeats) * gridBeats;
            e = std::round (e / gridBeats) * gridBeats;
            if (e < s + gridBeats) e = s + gridBeats;
        }
        const int vel = (int) std::lround (30.0 + 97.0 * std::pow (std::clamp (n.level, 0.0f, 1.0f), 0.6f));
        t.push_back ({ s, e, origLen, std::clamp (n.midi + transpose, 0, 127), vel });
    }

    std::vector<Tmp> k;
    for (const auto& n : t)
    {
        if (! k.empty() && n.s < k.back().s + 1e-6)        // two notes landed on the same step
        {
            if (n.origLen > k.back().origLen) k.back() = n;
            continue;
        }
        if (! k.empty() && k.back().e > n.s) k.back().e = n.s;
        k.push_back (n);
    }
    for (const auto& n : k)
        if (n.e - n.s > 1e-3)
            out.push_back ({ n.s, n.e - n.s, n.midi, n.vel });
    return out;
}

inline const char* pitchClassName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

// FL Studio labels MIDI 60 as C5, so by default octave = midi / 12.
inline std::string noteName (int midi, bool flStudioOctaves = true)
{
    const int oct = flStudioOctaves ? midi / 12 : midi / 12 - 1;
    return std::string (pitchClassName (midi)) + std::to_string (oct);
}

inline int dominantPitchClass (const Analysis& a)
{
    double h[12] = {};
    for (const auto& n : a.notes) h[((n.midi % 12) + 12) % 12] += n.endSec - n.startSec;
    int best = -1; double bv = 0.0;
    for (int i = 0; i < 12; ++i) if (h[i] > bv) { bv = h[i]; best = i; }
    return best;
}

// ------------------------------------------------------------------------------------------
// Standard MIDI File, format 0, 960 PPQ, 4/4, with tempo
inline std::vector<uint8_t> makeMidiFile (const std::vector<ExportNote>& notes, double bpm, const std::string& trackName)
{
    constexpr int ppq = 960;
    struct Ev { uint32_t tick; int order; uint8_t st, d1, d2; };
    std::vector<Ev> ev;
    for (const auto& n : notes)
    {
        const uint32_t on  = (uint32_t) std::llround (n.startBeat * ppq);
        const uint32_t off = std::max (on + 1, (uint32_t) std::llround ((n.startBeat + n.lengthBeat) * ppq));
        ev.push_back ({ on,  1, 0x90, (uint8_t) n.midi, (uint8_t) std::clamp (n.velocity, 1, 127) });
        ev.push_back ({ off, 0, 0x80, (uint8_t) n.midi, 0 });
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b)
                      { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    std::vector<uint8_t> trk;
    auto vlq = [&] (uint32_t v)
    {
        uint8_t b[5]; int c = 0;
        b[c++] = (uint8_t) (v & 0x7f);
        while (v >>= 7) b[c++] = (uint8_t) ((v & 0x7f) | 0x80);
        while (c--) trk.push_back (b[c]);
    };

    vlq (0); trk.insert (trk.end(), { 0xFF, 0x03, (uint8_t) std::min<size_t> (trackName.size(), 100) });
    trk.insert (trk.end(), trackName.begin(), trackName.begin() + (long) std::min<size_t> (trackName.size(), 100));

    const uint32_t us = (uint32_t) std::llround (60000000.0 / std::max (1.0, bpm));
    vlq (0); trk.insert (trk.end(), { 0xFF, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us });
    vlq (0); trk.insert (trk.end(), { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 });

    uint32_t last = 0;
    for (const auto& e : ev)
    {
        vlq (e.tick - last); last = e.tick;
        trk.push_back (e.st); trk.push_back (e.d1); trk.push_back (e.d2);
    }
    vlq (0); trk.insert (trk.end(), { 0xFF, 0x2F, 0x00 });

    std::vector<uint8_t> f { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, (uint8_t) (ppq >> 8), (uint8_t) (ppq & 0xff),
                             'M', 'T', 'r', 'k',
                             (uint8_t) (trk.size() >> 24), (uint8_t) (trk.size() >> 16),
                             (uint8_t) (trk.size() >> 8),  (uint8_t) trk.size() };
    f.insert (f.end(), trk.begin(), trk.end());
    return f;
}
} // namespace bassgrab
