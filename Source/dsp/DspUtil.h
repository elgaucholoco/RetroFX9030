#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Petits blocs DSP réutilisés par tous les effets.
namespace fx
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float x, float lo, float hi) { return std::min (std::max (x, lo), hi); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float expMap (float t, float lo, float hi) { return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f)); }
/** LVL 0-100 : 80 = gain unité */
inline float levelGain (float v) { float g = std::max (v, 0.0f) / 80.0f; return g * g; }
/** LVL 1-10 : 8 = gain unité */
inline float level10Gain (float v) { return dbToGain ((v - 8.0f) * 2.5f); }
inline float onePoleCoef (float ms, float sr) { return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (ms * 0.001f * sr)); }

struct OnePole
{
    float a = 0, z = 0;
    void setCutoff (float fc, float sr) { a = std::exp (-kTwoPi * std::min (fc, 0.49f * sr) / sr); }
    float lp (float x) { z = x + a * (z - x); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

struct Smoother
{
    float a = 0.99f, y = 0; bool init = false;
    void setTime (float ms, float sr) { a = onePoleCoef (ms, sr); }
    float process (float target)
    {
        if (! init) { y = target; init = true; }
        y = target + a * (y - target);
        return y;
    }
    void reset() { init = false; }
};

struct Biquad
{
    enum Type { LP, HP, BP, Peak, LowShelf, HighShelf, Notch, AP };
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setFlat() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void set (Type t, float f, float q, float gainDb, float sr)
    {
        f = clampf (f, 10.0f, 0.45f * sr);
        const float w0 = kTwoPi * f / sr, cs = std::cos (w0), sn = std::sin (w0);
        const float alpha = sn / (2.0f * q), A = std::pow (10.0f, gainDb / 40.0f);
        float B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case LP:   B0 = (1 - cs) * 0.5f; B1 = 1 - cs; B2 = B0; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; break;
            case HP:   B0 = (1 + cs) * 0.5f; B1 = -(1 + cs); B2 = B0; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; break;
            case BP:   B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; break;
            case Notch: B0 = 1; B1 = -2 * cs; B2 = 1; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; break;
            case AP:   B0 = 1 - alpha; B1 = -2 * cs; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; break;
            case Peak: B0 = 1 + alpha * A; B1 = -2 * cs; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cs; A2 = 1 - alpha / A; break;
            case LowShelf:
            {
                const float sq = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cs + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sq);
                A0 = (A + 1) + (A - 1) * cs + sq;       A1 = -2 * ((A - 1) + (A + 1) * cs);    A2 = (A + 1) + (A - 1) * cs - sq;
                break;
            }
            case HighShelf:
            {
                const float sq = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cs + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sq);
                A0 = (A + 1) - (A - 1) * cs + sq;       A1 = 2 * ((A - 1) - (A + 1) * cs);      A2 = (A + 1) - (A - 1) * cs - sq;
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

/** Filtre d'état variable (topologie TPT), stable en modulation rapide. */
struct Svf
{
    float ic1 = 0, ic2 = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float fc, float q, float sr)
    {
        const float g = std::tan (kPi * clampf (fc, 10.0f, 0.45f * sr) / sr);
        k = 1.0f / q; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process (float x, float& lp, float& bp)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1;
    }
    float bandNorm (float x) { float l, b; process (x, l, b); return k * b; }
    void reset() { ic1 = ic2 = 0; }
};

struct DelayLine
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    /** échantillon écrit il y a d pas (0 = le plus récent), interpolation linéaire */
    inline float read (float d) const
    {
        d = clampf (d, 0.0f, (float) (mask - 2));
        const float pos = (float) (w - 1) - d;
        const int i = (int) std::floor (pos);
        const float f = pos - (float) i;
        const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
        return a + f * (b - a);
    }
    inline float readInt (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
};

struct Allpass
{
    DelayLine d; int len = 1; float g = 0.6f;
    void init (int n, float gain) { len = std::max (1, n); g = gain; d.init (len + 2); }
    float process (float x)
    {
        const float z = d.readInt (len - 1);
        const float y = -g * x + z;
        d.push (x + g * z);
        return y;
    }
    void reset() { d.reset(); }
};

struct Lfo
{
    float phase = 0, inc = 0;
    void setRate (float hz, float sr) { inc = hz / sr; }
    void tick() { phase += inc; if (phase >= 1.0f) phase -= 1.0f; }
    static float triAt (float p) { p -= std::floor (p); return 1.0f - 4.0f * std::fabs (p - 0.5f) ; } // [-1,1], 0 → -1
    float tri (float offset = 0) const { return triAt (phase + offset); }
    float sine (float offset = 0) const { return std::sin (kTwoPi * (phase + offset)); }
};

struct EnvFollower
{
    float a = 0, r = 0, y = 0;
    void set (float atkMs, float relMs, float sr) { a = onePoleCoef (atkMs, sr); r = onePoleCoef (relMs, sr); }
    float process (float x)
    {
        x = std::fabs (x);
        const float c = x > y ? a : r;
        y = x + c * (y - x);
        return y;
    }
    void reset() { y = 0; }
};

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s / 4294967296.0f; }
    float bipolar() { return next01() * 2.0f - 1.0f; }
};

/** Détecteur d'attaque (coup de médiator). */
struct Onset
{
    EnvFollower fast, slow; int hold = 0, holdN = 0; float thr = 0.008f;
    void prepare (float sr) { fast.set (0.5f, 40.0f, sr); slow.set (40.0f, 400.0f, sr); holdN = (int) (0.07f * sr); }
    void reset() { fast.reset(); slow.reset(); hold = 0; }
    bool process (float x)
    {
        const float f = fast.process (x), s = slow.process (x);
        if (hold > 0) { --hold; return false; }
        if (f > thr && f > s * 1.6f + 0.002f) { hold = holdN; return true; }
        return false;
    }
    float level() const { return fast.y; }
};

/** Pitch shifter « années 90 » : deux têtes de lecture qui glissent dans une ligne à retard. */
struct PitchShifter
{
    DelayLine d; float win = 2000, phase = 0;
    void prepare (float sr, float winMs) { win = winMs * 0.001f * sr; d.init ((int) (win * 2.0f) + 64); phase = 0; }
    void reset() { d.reset(); phase = 0; }
    float process (float x, float ratio)
    {
        d.push (x);
        phase += (1.0f - ratio) / win;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float s = std::sin (kPi * phase), g1 = s * s;
        return d.read (2.0f + phase * win) * g1 + d.read (2.0f + p2 * win) * (1.0f - g1);
    }
};
}
