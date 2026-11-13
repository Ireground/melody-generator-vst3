#pragma once
// Melody generation engine (ported from the Simple Melody Generator JSFX).
// Same seed + same settings = same melody.

#include <array>
#include <algorithm>
#include <cmath>

struct MelodyNote
{
    int start  = 0;   // in 16th-note steps
    int len    = 1;   // in 16th-note steps
    int degree = 0;   // scale degree (0 = root)
    int vel    = 100; // 1..127
};

struct MelodyPattern
{
    static constexpr int maxNotes = 256;
    std::array<MelodyNote, maxNotes> notes {};
    int numNotes   = 0;
    int bars       = 1;
    int totalSteps = 16;
};

struct MelodySettings
{
    int scale      = 0;
    int range      = 1;   // octaves
    int complexity = 40;  // 0..100
    int bars       = 1;
    int repetition = 50;  // 0..100
    int seed       = 1;

    bool operator== (const MelodySettings& o) const
    {
        return scale == o.scale && range == o.range && complexity == o.complexity
            && bars == o.bars && repetition == o.repetition && seed == o.seed;
    }
    bool operator!= (const MelodySettings& o) const { return ! (*this == o); }
};

namespace Scales
{
    constexpr int count = 12;

    inline const char* name (int i)
    {
        static const char* names[count] = { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                            "Locrian", "Harmonic Minor", "Melodic Minor",
                                            "Major Pentatonic", "Minor Pentatonic", "Blues" };
        return names[std::clamp (i, 0, count - 1)];
    }

    inline int size (int i)
    {
        static const int sizes[count] = { 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6 };
        return sizes[std::clamp (i, 0, count - 1)];
    }

    inline int interval (int scale, int idx)
    {
        static const int iv[count][7] = {
            { 0, 2, 4, 5, 7, 9, 11 },  // Major
            { 0, 2, 3, 5, 7, 8, 10 },  // Minor
            { 0, 2, 3, 5, 7, 9, 10 },  // Dorian
            { 0, 1, 3, 5, 7, 8, 10 },  // Phrygian
            { 0, 2, 4, 6, 7, 9, 11 },  // Lydian
            { 0, 2, 4, 5, 7, 9, 10 },  // Mixolydian
            { 0, 1, 3, 5, 6, 8, 10 },  // Locrian
            { 0, 2, 3, 5, 7, 8, 11 },  // Harmonic Minor
            { 0, 2, 3, 5, 7, 9, 11 },  // Melodic Minor
            { 0, 2, 4, 7, 9, 0, 0 },   // Major Pentatonic
            { 0, 3, 5, 7, 10, 0, 0 },  // Minor Pentatonic
            { 0, 3, 5, 6, 7, 10, 0 }   // Blues
        };
        scale = std::clamp (scale, 0, count - 1);
        return iv[scale][std::clamp (idx, 0, size (scale) - 1)];
    }

    inline int degreeToPitch (int degree, int scale, int key, int octave)
    {
        const int n   = size (scale);
        const int o   = (int) std::floor ((double) degree / n);
        const int idx = degree - o * n;
        const int p   = 12 * (octave + 1) + key + o * 12 + interval (scale, idx);
        return std::clamp (p, 0, 127);
    }

    inline const char* keyName (int k)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[std::clamp (k, 0, 11)];
    }
}

class MelodyGenerator
{
public:
    void generate (const MelodySettings& s, MelodyPattern& out)
    {
        rs = (double) s.seed * 7919.0 + 12345.0;
        rnd(); rnd();

        const double c = s.complexity / 100.0;
        out.bars       = s.bars;
        out.totalSteps = s.bars * 16;
        out.numNotes   = 0;

        nsc    = Scales::size (s.scale);
        lo     = 0;
        hi     = s.range * nsc;
        center = hi * 0.5;

        const double restP = 0.25 - 0.18 * c;
        int cur = (nsc == 7) ? (rnd() < 0.5 ? 0 : (rnd() < 0.5 ? 2 : 4))
                             : (rnd() < 0.6 ? 0 : 2);

        for (int b = 0; b < s.bars; ++b)
        {
            const int base = b * 16;

            if (b > 0 && rnd() < s.repetition / 100.0)
            {
                // repeat an earlier bar (sometimes shifted by one scale step)
                const int src   = b - ((b % 2) == 1 ? 1 : 2);
                const int shift = rnd() < 0.1 + 0.3 * c ? (rnd() < 0.5 ? -1 : 1) : 0;
                const int cnt   = out.numNotes;

                for (int i = 0; i < cnt; ++i)
                {
                    const auto n = out.notes[(size_t) i];
                    if (n.start >= src * 16 && n.start < src * 16 + 16)
                    {
                        const int nd = std::clamp (n.degree + shift, lo, hi);
                        add (out, n.start - src * 16 + base, n.len, nd, n.vel);
                        cur = nd;
                    }
                }
            }
            else
            {
                // fresh bar
                int st = 0;
                while (st < 16)
                {
                    if (base + st > 0 && rnd() < restP)
                    {
                        const int rd = rnd() < 0.5 ? 2 : (c < 0.4 ? 4 : 1);
                        st += std::min (rd, 16 - st);
                    }
                    else
                    {
                        const int d = pickDuration (c, 16 - st);
                        if (out.numNotes > 0)
                            cur = nextDegree (cur, st, c);

                        int v = 82 + (st % 4 == 0 ? 14 : 0) + (st % 8 == 0 ? 6 : 0)
                                + (int) std::floor (rnd() * 12.0) - 6;
                        add (out, base + st, d, cur, std::clamp (v, 1, 127));
                        st += d;
                    }
                }
            }
        }

        // end the phrase on the root note
        if (out.numNotes > 0)
        {
            auto& last = out.notes[(size_t) out.numNotes - 1];
            int nd = (int) std::floor ((double) last.degree / nsc + 0.5) * nsc;
            if (nd > hi) nd -= nsc;
            if (nd < lo) nd += nsc;
            last.degree = nd;
        }
    }

private:
    double rs = 1.0;
    int nsc = 7, lo = 0, hi = 7;
    double center = 3.5;

    double rnd()
    {
        const double x = rs * 16807.0;
        rs = x - std::floor (x / 2147483647.0) * 2147483647.0;
        return rs / 2147483647.0;
    }

    static void add (MelodyPattern& p, int start, int len, int degree, int vel)
    {
        if (p.numNotes >= MelodyPattern::maxNotes)
            return;
        p.notes[(size_t) p.numNotes++] = { start, len, degree, vel };
    }

    // low complexity favours long notes, high complexity favours short ones
    int pickDuration (double c, int maxd)
    {
        const double w1 = 0.05 + 1.5 * c * c;          // 16th
        const double w2 = 0.5 + 0.7 * c;               // 8th
        const double w3 = 0.05 + 0.25 * c;             // dotted 8th
        const double w4 = 1.0 - 0.6 * c;               // quarter
        const double w6 = 0.35 * (1.0 - c);            // dotted quarter
        const double w8 = 0.6 * (1.0 - c) * (1.0 - c); // half

        double r = rnd() * (w1 + w2 + w3 + w4 + w6 + w8);
        int d = 8;
        if (r < w1) d = 1;
        else { r -= w1;
            if (r < w2) d = 2;
            else { r -= w2;
                if (r < w3) d = 3;
                else { r -= w3;
                    if (r < w4) d = 4;
                    else { r -= w4; d = (r < w6) ? 6 : 8; }
                }
            }
        }
        return std::min (d, maxd);
    }

    // mostly steps, some skips, leaps more likely at high complexity
    int nextDegree (int d, int stepInBar, double c)
    {
        const double r = rnd();
        int mv;
        if (r < 0.06 + 0.14 * (1.0 - c)) mv = 0;
        else if (r < 0.70)               mv = 1;
        else if (r < 0.88)               mv = 2;
        else                             mv = 3 + (int) std::floor (rnd() * (1.0 + 3.0 * c));

        int dir = rnd() < 0.5 ? -1 : 1;
        if (d > center + nsc * 0.4 && rnd() < 0.7) dir = -1;
        if (d < center - nsc * 0.4 && rnd() < 0.7) dir = 1;

        int nd = d + dir * mv;
        if (nd > hi) nd = d - mv;
        if (nd < lo) nd = d + mv;
        nd = std::clamp (nd, lo, hi);

        // on strong beats, lean towards chord tones (1st, 3rd, 5th)
        if (stepInBar % 8 == 0 && nsc == 7 && rnd() < 0.65)
        {
            const int m = nd % 7;
            if (m == 1 || m == 3 || m == 5) nd -= 1;
            if (m == 6) nd = (nd + 1 <= hi) ? nd + 1 : nd - 2;
        }
        return nd;
    }
};
