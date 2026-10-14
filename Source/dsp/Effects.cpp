#include "Effects.h"
#include "DspUtil.h"
#include <juce_dsp/juce_dsp.h>

// Algorithmes des 47 types d'effets. Ce ne sont pas des copies des algorithmes
// d'origine (inconnus) mais des modèles qui suivent les descriptions du manuel.
namespace fx
{
namespace
{
inline int iround (float v) { return (int) std::lround (v); }

//==============================================================================
// COMP : Compressor / Limiter
class Compressor : public Fx
{
public:
    Compressor (const defs::TypeDef& t, bool isLimiter) : Fx (t), lim (isLimiter)
    {
        iD = ix ("DPT"); iA = ix (lim ? "REL" : "ATK"); iL = ix ("LVL");
        if (! lim) iB = ix ("Bsw");
    }
    void prepare (double s) override { Fx::prepare (s); bright.set (Biquad::HighShelf, 3000.0f, 0.7f, 6.0f, sr); }
    void reset() override { env = 0; bright.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float d = p[iD];
        const float thr = lim ? -1.0f - d * 2.0f : -6.0f - d * 2.4f;
        const float ratio = lim ? 30.0f : 1.6f + d * 0.55f;
        const float atk = lim ? 0.3f : (p[iA] > 0.5f ? 1.5f : 25.0f);
        const float rel = lim ? (p[iA] > 0.5f ? 60.0f : 450.0f) : 220.0f;
        const float ac = onePoleCoef (atk, sr), rc = onePoleCoef (rel, sr);
        const float slope = 1.0f - 1.0f / ratio;
        const float makeup = lim ? 0.0f : -thr * slope * 0.5f;
        const float out = level10Gain (p[iL]);
        const bool br = ! lim && p[iB] > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i], a = std::fabs (x);
            env = a + (a > env ? ac : rc) * (env - a);
            const float over = gainToDb (env) - thr;
            float y = x * dbToGain ((over > 0 ? -over * slope : 0.0f) + makeup);
            if (br) y = bright.process (y);
            L[i] = y * out;
        }
    }
private:
    bool lim; int iD, iA, iB = 0, iL; float env = 0; Biquad bright;
};

// EXT : la boucle d'effet externe n'existe pas dans un plug-in → signal inchangé
class External : public Fx
{
public:
    using Fx::Fx;
    void process (const Ctx&, float*, float*, int, bool&) override {}
};

//==============================================================================
// DIST : Distortion / Over Drive / Crunch (suréchantillonnage x4)
class Distortion : public Fx
{
public:
    Distortion (const defs::TypeDef& t, int k) : Fx (t), kind (k)
    {
        iG = ix (kind == 0 ? "DST" : "DRV"); iE = ix ("EQ"); iL = ix ("LVL");
    }
    void prepare (double s) override
    {
        Fx::prepare (s);
        os.initProcessing (256);
        preHp.set (Biquad::HP, kind == 2 ? 70.0f : 110.0f, 0.7f, 0, sr);
        preMid.set (Biquad::Peak, kind == 0 ? 900.0f : 720.0f, 0.8f, kind == 0 ? 7.0f : 4.0f, sr);
        fizz.set (Biquad::LP, kind == 0 ? 6500.0f : 7500.0f, 0.6f, 0, sr);
        dc.setCutoff (15.0f, sr);
        lastE = -1;
        reset();
    }
    void reset() override { os.reset(); preHp.reset(); preMid.reset(); fizz.reset(); tone.reset(); dc.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        if (p[iE] != lastE)
        {
            lastE = p[iE];
            tone.set (Biquad::LP, 1200.0f * std::pow (2.0f, (lastE - 1.0f) * 0.37f), 0.65f, 0, sr);
        }
        const float g = p[iG];
        const float gainDb = kind == 0 ? 8.0f + g * 3.3f : kind == 1 ? 4.0f + g * 2.7f : 2.0f + g * 2.1f;
        const float drive = dbToGain (gainDb);
        for (int i = 0; i < n; ++i) L[i] = preMid.process (preHp.process (L[i])) * drive;

        float* chans[1] = { L };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
        auto up = os.processSamplesUp (block);
        float* u = up.getChannelPointer (0);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float x = u[i];
            float y;
            if (kind == 0)      y = x / std::pow (1.0f + std::pow (std::fabs (x), 2.5f), 0.4f);
            else if (kind == 1) y = std::tanh (x + 0.25f) - 0.244919f;
            else                y = x >= 0 ? std::tanh (x) : std::tanh (1.4f * x) / 1.4f;
            u[i] = y;
        }
        os.processSamplesDown (block);

        const float out = 0.28f * levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
            L[i] = tone.process (fizz.process (dc.hp (L[i]))) * out;
    }
private:
    int kind, iG, iE, iL; float lastE = -1;
    Biquad preHp, preMid, fizz, tone; OnePole dc;
    juce::dsp::Oversampling<float> os { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
};

//==============================================================================
// EQ
class Eq4 : public Fx
{
public:
    explicit Eq4 (const defs::TypeDef& t) : Fx (t) { iLo = ix ("Low"); iMi = ix ("Mid"); iHi = ix ("Hi"); iPr = ix ("Prs"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); for (auto& l : last) l = -999; }
    void reset() override { for (auto& b : bq) b.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const int idx[4] = { iLo, iMi, iHi, iPr };
        for (int k = 0; k < 4; ++k)
            if (p[idx[k]] != last[k])
            {
                last[k] = p[idx[k]];
                const float db = (last[k] - 50.0f) * 0.24f;
                if (k == 0) bq[0].set (Biquad::LowShelf, 100.0f, 0.7f, db, sr);
                if (k == 1) bq[1].set (Biquad::Peak, 800.0f, 0.7f, db, sr);
                if (k == 2) bq[2].set (Biquad::HighShelf, 3000.0f, 0.7f, db, sr);
                if (k == 3) bq[3].set (Biquad::Peak, 5500.0f, 0.9f, db, sr);
            }
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
            L[i] = bq[3].process (bq[2].process (bq[1].process (bq[0].process (L[i])))) * g;
    }
private:
    int iLo, iMi, iHi, iPr, iL; Biquad bq[4]; float last[4] {};
};

class Eq3 : public Fx
{
public:
    explicit Eq3 (const defs::TypeDef& t) : Fx (t) { iLo = ix ("LoG"); iF = ix ("MdF"); iMi = ix ("MdG"); iHi = ix ("HiG"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); for (auto& l : last) l = -999; }
    void reset() override { for (auto& b : bq) b.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        if (p[iLo] != last[0]) { last[0] = p[iLo]; bq[0].set (Biquad::LowShelf, 150.0f, 0.7f, last[0], sr); }
        if (p[iF] != last[1] || p[iMi] != last[2])
        {
            last[1] = p[iF]; last[2] = p[iMi];
            const int fi = std::max (0, std::min (iround (last[1]), (int) defs::kFreqEq.size() - 1));
            bq[1].set (Biquad::Peak, (float) defs::kFreqEq[(size_t) fi], 1.0f, last[2], sr);
        }
        if (p[iHi] != last[3]) { last[3] = p[iHi]; bq[2].set (Biquad::HighShelf, 4500.0f, 0.7f, last[3], sr); }
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
            L[i] = bq[2].process (bq[1].process (bq[0].process (L[i]))) * g;
    }
private:
    int iLo, iF, iMi, iHi, iL; Biquad bq[3]; float last[4] {};
};

class Enhancer : public Fx
{
public:
    explicit Enhancer (const defs::TypeDef& t) : Fx (t) { iLF = ix ("LoF"); iLE = ix ("LoE"); iHF = ix ("HiF"); iHE = ix ("HiE"); iL = ix ("LVL"); }
    void prepare (double s) override
    {
        Fx::prepare (s); lastLF = lastHF = -1;
        fast.set (0.5f, 30.0f, sr); slow.set (30.0f, 300.0f, sr);
    }
    void reset() override { lo.reset(); hi1.reset(); hi2.reset(); fast.reset(); slow.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        if (p[iLF] != lastLF) { lastLF = p[iLF]; lo.set (Biquad::BP, 50.0f * std::pow (1.22f, lastLF - 1.0f), 0.8f, 0, sr); }
        if (p[iHF] != lastHF)
        {
            lastHF = p[iHF];
            const float f = 1500.0f * std::pow (1.2f, lastHF - 1.0f);
            hi1.set (Biquad::HP, f, 0.7f, 0, sr); hi2.set (Biquad::HP, f, 0.7f, 0, sr);
        }
        const float le = p[iLE] / 100.0f * 1.6f, he = p[iHE] / 100.0f * 2.2f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float f = fast.process (x), s = slow.process (x);
            const float transient = clampf ((f - s) * 12.0f, 0.0f, 1.0f);   // dépend de la dynamique
            const float lb = lo.process (x);
            const float hb = hi2.process (hi1.process (x));
            const float hs = hb + 0.4f * std::tanh (hb * 5.0f);
            L[i] = (x + le * std::tanh (lb * 2.0f) + he * hs * (0.35f + 0.65f * transient)) * g;
        }
    }
private:
    int iLF, iLE, iHF, iHE, iL; float lastLF = -1, lastHF = -1;
    Biquad lo, hi1, hi2; EnvFollower fast, slow;
};

//==============================================================================
// AMP : simulateur d'ampli guitare / basse (préampli + voicing + baffle)
class AmpSim : public Fx
{
public:
    AmpSim (const defs::TypeDef& t, bool bassAmp) : Fx (t), bass (bassAmp)
    {
        iC = ix ("COL"); iB = ix ("BOX"); iD = ix ("DPT"); iS = ix ("STR"); iL = ix ("LVL");
    }
    void prepare (double s) override
    {
        Fx::prepare (s); lastC = lastB = -1;
        decor[0].set (Biquad::AP, 420.0f, 0.5f, 0, sr);
        decor[1].set (Biquad::AP, 2600.0f, 0.5f, 0, sr);
    }
    void reset() override { for (auto& b : voice) b.reset(); for (auto& b : cab) b.reset(); decor[0].reset(); decor[1].reset(); }
    void process (const Ctx&, float* L, float* R, int n, bool& st) override
    {
        const int c = iround (p[iC]), b = iround (p[iB]);
        if (c != lastC || b != lastB) { lastC = c; lastB = b; setup (c, b); }
        const float d = p[iD] / 100.0f, g = levelGain (p[iL]);
        const bool stereoOut = p[iS] > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            float y = std::tanh (x * 1.8f) / 1.8f;
            for (auto& v : voice) y = v.process (y);
            for (auto& k : cab) y = k.process (y);
            y *= 0.9f;
            const float out = (x + d * (y - x)) * g;
            L[i] = out;
            if (stereoOut) R[i] = decor[1].process (decor[0].process (out));
        }
        if (stereoOut) st = true;
    }
private:
    void setup (int col, int box)
    {
        for (auto& v : voice) v.setFlat();
        for (auto& k : cab) k.setFlat();
        using B = Biquad;
        if (! bass)
        {
            switch (col)
            {
                case 2: voice[0].set (B::LowShelf, 120, 0.7f, 2, sr); voice[1].set (B::Peak, 650, 0.8f, 3.5f, sr); voice[2].set (B::HighShelf, 4000, 0.7f, -3, sr); break;
                case 3: voice[0].set (B::LowShelf, 200, 0.7f, -4, sr); voice[1].set (B::Peak, 1500, 0.9f, 2, sr); voice[2].set (B::HighShelf, 2500, 0.7f, 5, sr); break;
                case 4: voice[0].set (B::LowShelf, 100, 0.7f, 6, sr); voice[1].set (B::Peak, 700, 0.8f, -7, sr); voice[2].set (B::HighShelf, 3500, 0.7f, 6, sr); break;
                default: break;
            }
            switch (box)
            {
                case 0: cab[0].set (B::HP, 130, 0.7f, 0, sr); cab[1].set (B::Peak, 1600, 1.2f, 4, sr); cab[2].set (B::Peak, 3000, 2.0f, -3, sr); cab[3].set (B::LP, 3200, 0.7f, 0, sr); cab[4].set (B::LP, 4200, 0.6f, 0, sr); break;
                case 1: cab[0].set (B::HP, 85, 0.8f, 0, sr); cab[1].set (B::Peak, 120, 1.2f, 3, sr); cab[2].set (B::Peak, 2200, 1.2f, 3, sr); cab[3].set (B::LP, 4500, 0.7f, 0, sr); cab[4].set (B::LP, 5500, 0.6f, 0, sr); break;
                default: cab[0].set (B::HP, 65, 0.9f, 0, sr); cab[1].set (B::Peak, 100, 1.5f, 5, sr); cab[2].set (B::Peak, 450, 1.0f, -3, sr); cab[3].set (B::Peak, 2600, 1.5f, 4, sr); cab[4].set (B::LP, 5200, 0.7f, 0, sr); break;
            }
        }
        else
        {
            switch (col)
            {
                case 2: voice[0].set (B::LowShelf, 100, 0.7f, 4, sr); voice[1].set (B::Peak, 500, 0.8f, 3, sr); break;
                case 3: voice[0].set (B::Peak, 800, 0.7f, 4, sr); voice[1].set (B::HighShelf, 3000, 0.7f, -4, sr); break;
                case 4: voice[0].set (B::LowShelf, 80, 0.7f, 6, sr); voice[1].set (B::Peak, 500, 0.8f, -6, sr); voice[2].set (B::HighShelf, 2500, 0.7f, 6, sr); break;
                default: break;
            }
            switch (box)
            {
                case 0: cab[0].set (B::HP, 60, 0.7f, 0, sr); cab[1].set (B::Peak, 200, 1.0f, 2, sr); cab[2].set (B::LP, 2500, 0.7f, 0, sr); cab[3].set (B::LP, 3500, 0.6f, 0, sr); break;
                case 1: cab[0].set (B::HP, 45, 0.7f, 0, sr); cab[1].set (B::Peak, 100, 1.2f, 3, sr); cab[2].set (B::Peak, 1500, 1.0f, 2, sr); cab[3].set (B::LP, 3500, 0.7f, 0, sr); cab[4].set (B::LP, 4500, 0.6f, 0, sr); break;
                default: cab[0].set (B::HP, 35, 0.7f, 0, sr); cab[1].set (B::Peak, 80, 1.4f, 5, sr); cab[2].set (B::Peak, 400, 1.0f, -2, sr); cab[3].set (B::LP, 3000, 0.7f, 0, sr); cab[4].set (B::LP, 4000, 0.6f, 0, sr); break;
            }
        }
    }
    bool bass; int iC, iB, iD, iS, iL, lastC = -1, lastB = -1;
    Biquad voice[3], cab[5], decor[2];
};

//==============================================================================
// EFF1 / EFF2
class PitchFx : public Fx
{
public:
    explicit PitchFx (const defs::TypeDef& t) : Fx (t) { iP = ix ("PIT"); iF = ix ("FIN"); iB = ix ("BAL"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); ps.prepare (sr, 50.0f); ratio.setTime (20.0f, sr); }
    void reset() override { ps.reset(); ratio.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float target = std::pow (2.0f, (p[iP] + p[iF] / 100.0f) / 12.0f);
        const float b = p[iB] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float w = ps.process (x, ratio.process (target));
            L[i] = (x * (1.0f - b) + w * b) * g;
        }
    }
private:
    int iP, iF, iB, iL; PitchShifter ps; Smoother ratio;
};

class Phaser : public Fx
{
public:
    Phaser (const defs::TypeDef& t, bool st) : Fx (t), stereo (st)
    {
        iD = ix ("DPT"); iS = ix ("SPD"); iL = ix ("LVL"); iK = stereo ? -1 : ix ("PK");
    }
    void reset() override { for (auto& c : z) for (auto& v : c) v = 0; fb[0] = fb[1] = 0; lfo.phase = 0; }
    void process (const Ctx&, float* L, float* R, int n, bool& st) override
    {
        lfo.setRate (p[iS] / 10.0f, sr);
        const float depth = p[iD] / 100.0f;
        const float fbAmt = stereo ? 0.45f : p[iK] / 100.0f * 0.85f;
        const float fmin = 180.0f, fmax = fmin * std::pow (2.0f, 1.0f + 4.0f * depth);
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float x = L[i];
            L[i] = runChannel (0, x, fmin, fmax, 0.5f + 0.5f * lfo.tri(), fbAmt) * g;
            if (stereo) R[i] = runChannel (1, x, fmin, fmax, 0.5f + 0.5f * lfo.tri (0.25f), fbAmt) * g;
        }
        if (stereo) st = true;
    }
private:
    float runChannel (int c, float x, float fmin, float fmax, float m, float fbAmt)
    {
        const float f = fmin * std::pow (fmax / fmin, m);
        const float t = std::tan (kPi * std::min (f, 0.45f * sr) / sr);
        const float a = (t - 1.0f) / (t + 1.0f);
        float y = x + fb[c] * fbAmt;
        for (int s = 0; s < 4; ++s)
        {
            const float o = a * y + z[c][s];
            z[c][s] = y - a * o;
            y = o;
        }
        fb[c] = y;
        return 0.5f * (x + y);
    }
    bool stereo; int iD, iS, iL, iK; Lfo lfo; float z[2][4] {}; float fb[2] {};
};

class Flanger : public Fx
{
public:
    Flanger (const defs::TypeDef& t, float maxMs) : Fx (t), maxDelayMs (maxMs)
    {
        iD = ix ("DPT"); iS = ix ("SPD"); iM = ix ("MAN"); iF = ix ("F.B"); iL = ix ("LVL");
    }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) ((maxDelayMs + 20.0f) * 0.001f * sr)); man.setTime (60.0f, sr); }
    void reset() override { dl.reset(); man.reset(); lfo.phase = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        lfo.setRate (p[iS] / 10.0f, sr);
        const float fb = p[iF] / 100.0f, depth = p[iD] / 100.0f, g = levelGain (p[iL]);
        const float msToS = 0.001f * sr;
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float m = man.process (p[iM]);
            const float sweep = depth * (2.5f + 0.04f * m) * (0.5f + 0.5f * lfo.tri());
            const float y = dl.read ((m + 0.25f + sweep) * msToS);
            const float x = L[i];
            dl.push (x + fb * y);
            L[i] = 0.5f * (x + y) * g;
        }
    }
private:
    float maxDelayMs; int iD, iS, iM, iF, iL; DelayLine dl; Lfo lfo; Smoother man;
};

class Chorus : public Fx
{
public:
    // variant 0 = CHO (vitesse fixe), 1 = CH2, 2 = CH3 (stéréo)
    Chorus (const defs::TypeDef& t, int v) : Fx (t), variant (v)
    {
        iD = ix ("DPT"); iM = ix ("MIX"); iL = ix ("LVL"); iS = variant == 0 ? -1 : ix ("SPD");
    }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (0.05f * sr)); }
    void reset() override { dl.reset(); lfo1.phase = 0; lfo2.phase = 0.37f; }
    void process (const Ctx&, float* L, float* R, int n, bool& st) override
    {
        const float rate = variant == 0 ? 0.65f : p[iS] / 10.0f;
        lfo1.setRate (rate, sr); lfo2.setRate (rate * 1.13f, sr);
        const float depth = p[iD] / 100.0f * 5.0f * 0.001f * sr;
        const float base1 = 0.009f * sr, base2 = 0.0135f * sr;
        const float mix = p[iM] / 100.0f, norm = 1.0f / (1.0f + 0.5f * mix), g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            lfo1.tick(); lfo2.tick();
            const float x = L[i];
            dl.push (x);
            const float v1 = dl.read (base1 + depth * (0.5f + 0.5f * lfo1.sine()));
            const float v2 = dl.read (base2 + depth * (0.5f + 0.5f * lfo2.sine (0.5f)));
            if (variant == 2)
            {
                L[i] = (x + mix * v1) * norm * g;
                R[i] = (x + mix * v2) * norm * g;
            }
            else
                L[i] = (x + mix * 0.5f * (v1 + v2)) * norm * g;
        }
        if (variant == 2) st = true;
    }
private:
    int variant, iD, iM, iL, iS; DelayLine dl; Lfo lfo1, lfo2;
};

class AutoWah : public Fx
{
public:
    explicit AutoWah (const defs::TypeDef& t) : Fx (t) { iS = ix ("SNS"); iM = ix ("MAN"); iK = ix ("PK"); iT = ix ("TRG"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); env.set (3.0f, 140.0f, sr); }
    void reset() override { svf.reset(); env.reset(); }
    void process (const Ctx& c, float* L, float*, int n, bool&) override
    {
        const bool pre = p[iT] < 0.5f && c.dryIn != nullptr;
        const float sens = p[iS] / 100.0f * 25.0f;
        const float base = 200.0f * std::pow (2.0f, p[iM] / 100.0f * 2.3f);
        const float q = 1.5f + p[iK] * 1.3f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float e = clampf (env.process (pre ? c.dryIn[i] : x) * sens, 0.0f, 1.0f);
            if ((i & 7) == 0) svf.set (base * std::pow (2.0f, 3.3f * e), q, sr);
            float lp, bp; svf.process (x, lp, bp);
            L[i] = (svf.k * bp * 3.0f + lp * 0.6f) * g;
        }
    }
private:
    int iS, iM, iK, iT, iL; Svf svf; EnvFollower env;
};

class Cry : public Fx
{
public:
    explicit Cry (const defs::TypeDef& t) : Fx (t) { iS = ix ("SNS"); iD = ix ("DIR"); iT = ix ("TRG"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); env.set (4.0f, 160.0f, sr); }
    void reset() override { f1.reset(); f2.reset(); env.reset(); }
    void process (const Ctx& c, float* L, float*, int n, bool&) override
    {
        const bool pre = p[iT] < 0.5f && c.dryIn != nullptr;
        const bool up = p[iD] > 0.5f;
        const float sens = p[iS] / 100.0f * 22.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            float m = clampf (env.process (pre ? c.dryIn[i] : x) * sens, 0.0f, 1.0f);
            if (! up) m = 1.0f - m;
            if ((i & 7) == 0)
            {
                f1.set (lerp (320.0f, 760.0f, m), 7.0f, sr);    // « ou » → « a » → « è »
                f2.set (lerp (800.0f, 1900.0f, m), 9.0f, sr);
            }
            L[i] = (f1.bandNorm (x) * 3.5f + f2.bandNorm (x) * 2.5f + 0.2f * x) * g;
        }
    }
private:
    int iS, iD, iT, iL; Svf f1, f2; EnvFollower env;
};

class Metallic : public Fx
{
public:
    explicit Metallic (const defs::TypeDef& t) : Fx (t) { iF = ix ("FRQ"); iM = ix ("MOD"); iS = ix ("SPD"); iL = ix ("LVL"); }
    void reset() override { ph = 0; lfo.phase = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        lfo.setRate (p[iS] / 10.0f, sr);
        const float f0 = 20.0f * std::pow (150.0f, p[iF] / 100.0f);
        const float mod = p[iM] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            ph += f0 * std::pow (2.0f, mod * lfo.sine()) / sr;
            ph -= std::floor (ph);
            L[i] = L[i] * std::sin (kTwoPi * ph) * 1.4f * g;
        }
    }
private:
    int iF, iM, iS, iL; float ph = 0; Lfo lfo;
};

class Doubling : public Fx
{
public:
    explicit Doubling (const defs::TypeDef& t) : Fx (t) { i1 = ix ("TT1"); i2 = ix ("TL1"); i3 = ix ("TT2"); i4 = ix ("TL2"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (0.11f * sr)); t1.setTime (40, sr); t2.setTime (40, sr); }
    void reset() override { dl.reset(); t1.reset(); t2.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float l1 = p[i2] / 100.0f, l2 = p[i4] / 100.0f;
        const float norm = 1.0f / (1.0f + 0.5f * (l1 + l2)), g = levelGain (p[iL]);
        const float ms = 0.001f * sr;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            dl.push (x);
            const float y = x + l1 * dl.read (t1.process (p[i1]) * ms) + l2 * dl.read (t2.process (p[i3]) * ms);
            L[i] = y * norm * g;
        }
    }
private:
    int i1, i2, i3, i4, iL; DelayLine dl; Smoother t1, t2;
};

class CombFilter : public Fx
{
public:
    explicit CombFilter (const defs::TypeDef& t) : Fx (t) { iF = ix ("FRQ"); iB = ix ("F.B"); iH = ix ("HiD"); iM = ix ("MIX"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (sr / 18.0f)); dly.setTime (30, sr); }
    void reset() override { dl.reset(); damp.reset(); dly.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const int fi = std::max (0, std::min (iround (p[iF]), (int) defs::kFreqComb.size() - 1));
        const float target = sr / (float) defs::kFreqComb[(size_t) fi];
        const float fb = p[iB] / 100.0f, mix = p[iM] / 100.0f, g = levelGain (p[iL]);
        damp.a = p[iH] / 100.0f * 0.95f;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float y = dl.read (dly.process (target));
            const float d = damp.lp (y);
            dl.push (x + fb * d);
            L[i] = (x + mix * y) * 0.7f * g;
        }
    }
private:
    int iF, iB, iH, iM, iL; DelayLine dl; OnePole damp; Smoother dly;
};

class Tremolo : public Fx
{
public:
    explicit Tremolo (const defs::TypeDef& t) : Fx (t) { iD = ix ("DPT"); iS = ix ("SPD"); iW = ix ("WF"); iE = ix ("EDG"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); smooth.setTime (1.0f, sr); }
    void reset() override { lfo.phase = 0; smooth.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        lfo.setRate (p[iS] / 10.0f, sr);
        const int wf = iround (p[iW]);
        const float depth = p[iD] / 100.0f, edge = 1.0f + p[iE] / 12.5f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            float w = wf == 0 ? 0.5f + 0.5f * lfo.tri() : wf == 1 ? 1.0f - lfo.phase : lfo.phase;
            w = clampf ((w - 0.5f) * edge + 0.5f, 0.0f, 1.0f);
            L[i] *= (1.0f - depth * (1.0f - smooth.process (w))) * g;
        }
    }
private:
    int iD, iS, iW, iE, iL; Lfo lfo; Smoother smooth;
};

class Octave : public Fx
{
public:
    explicit Octave (const defs::TypeDef& t) : Fx (t) { iLo = ix ("Low"); iDi = ix ("DiL"); iL = ix ("LVL"); }
    void prepare (double s) override
    {
        Fx::prepare (s);
        t1.set (Biquad::LP, 260.0f, 0.7f, 0, sr); t2.set (Biquad::LP, 260.0f, 0.7f, 0, sr);
        s1.set (Biquad::LP, 320.0f, 0.7f, 0, sr); s2.set (Biquad::LP, 320.0f, 0.7f, 0, sr);
        env.set (5.0f, 90.0f, sr);
    }
    void reset() override { t1.reset(); t2.reset(); s1.reset(); s2.reset(); env.reset(); armed = ff1 = ff2 = false; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float lo = p[iLo] / 100.0f, di = p[iDi] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float e = env.process (x);
            const float s = t2.process (t1.process (x));
            const float thr = e * 0.12f + 1.0e-4f;
            if (! armed && s < -thr) armed = true;
            if (armed && s > thr) { armed = false; ff1 = ! ff1; if (ff1) ff2 = ! ff2; }   // diviseurs /2 puis /4
            const float sub = s2.process (s1.process ((ff2 ? 1.0f : -1.0f) * e * 2.5f));
            L[i] = (di * x + lo * sub * 0.45f) * g;
        }
    }
private:
    int iLo, iDi, iL; Biquad t1, t2, s1, s2; EnvFollower env; bool armed = false, ff1 = false, ff2 = false;
};

class PedalWah : public Fx
{
public:
    explicit PedalWah (const defs::TypeDef& t) : Fx (t) { iF = ix ("FRQ"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); pos.setTime (15.0f, sr); }
    void reset() override { svf.reset(); pos.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float m = pos.process (p[iF] / 100.0f);
            if ((i & 7) == 0) svf.set (350.0f * std::pow (2.0f, m * 2.65f), 5.0f, sr);
            float lp, bp; svf.process (L[i], lp, bp);
            L[i] = (svf.k * bp * 3.5f + lp * 0.35f) * g;
        }
    }
private:
    int iF, iL; Svf svf; Smoother pos;
};

class Step : public Fx
{
public:
    explicit Step (const defs::TypeDef& t) : Fx (t) { iD = ix ("DPT"); iF = ix ("FRQ"); iS = ix ("SPD"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); ps.prepare (sr, 30.0f); ratio.setTime (2.0f, sr); }
    void reset() override { ps.reset(); ratio.reset(); counter = 0; semis = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        static const int steps[] = { 0, 12, 7, 5, 3, 4, 9, 10, 2, -5, -7, -12, -3 };
        const float rate = p[iF] / 10.0f * (p[iS] > 0.5f ? 10.0f : 1.0f);
        const int period = std::max (1, (int) (sr / rate));
        const float range = p[iD] / 100.0f * 12.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            if (--counter <= 0)       // échantillonneur-bloqueur
            {
                counter = period;
                int s = 0;
                for (int tries = 0; tries < 8; ++tries)
                {
                    s = steps[(int) (rng.next01() * 12.999f)];
                    if (std::abs (s) <= range + 0.01f) break;
                    s = 0;
                }
                semis = (float) s;
            }
            L[i] = ps.process (L[i], ratio.process (std::pow (2.0f, semis / 12.0f))) * g;
        }
    }
private:
    int iD, iF, iS, iL, counter = 0; float semis = 0; PitchShifter ps; Smoother ratio; Rng rng;
};

class AutoPan : public Fx
{
public:
    explicit AutoPan (const defs::TypeDef& t) : Fx (t) { iD = ix ("DPT"); iS = ix ("SPD"); iL = ix ("LVL"); }
    void reset() override { lfo.phase = 0.25f; }
    void process (const Ctx&, float* L, float* R, int n, bool& st) override
    {
        lfo.setRate (p[iS] / 10.0f, sr);
        const float depth = p[iD] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            lfo.tick();
            const float pan = depth * lfo.tri();
            const float a = (pan + 1.0f) * kPi * 0.25f;
            const float x = L[i] * g * 1.4142f;
            L[i] = x * std::cos (a);
            R[i] = x * std::sin (a);
        }
        st = true;
    }
private:
    int iD, iS, iL; Lfo lfo;
};

class Delay : public Fx
{
public:
    explicit Delay (const defs::TypeDef& t) : Fx (t) { iT = ix ("TM"); iF = ix ("F.B"); iH = ix ("HiD"); iB = ix ("BAL"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (1.05f * sr)); tm.setTime (120.0f, sr); }
    void reset() override { dl.reset(); damp.reset(); tm.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float fb = p[iF] / 100.0f, b = p[iB] / 100.0f, g = levelGain (p[iL]);
        damp.setCutoff (18000.0f * std::pow (1500.0f / 18000.0f, p[iH] / 99.0f), sr);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float y = dl.read (tm.process (p[iT]) * 0.001f * sr);
            dl.push (x + fb * damp.lp (y));
            L[i] = (x * (1.0f - b) + y * b) * g;
        }
    }
private:
    int iT, iF, iH, iB, iL; DelayLine dl; OnePole damp; Smoother tm;
};

class Echo : public Fx
{
public:
    explicit Echo (const defs::TypeDef& t) : Fx (t) { iLT = ix ("LT"); iFT = ix ("FBT"); iRT = ix ("RT"); iF = ix ("F.B"); iB = ix ("BAL"); }
    void prepare (double s) override
    {
        Fx::prepare (s); dl.init ((int) (1.05f * sr));
        for (auto& t : tm) t.setTime (120.0f, sr);
        damp.setCutoff (6000.0f, sr);
    }
    void reset() override { dl.reset(); damp.reset(); for (auto& t : tm) t.reset(); }
    void process (const Ctx&, float* L, float* R, int n, bool& st) override
    {
        const float fb = p[iF] / 100.0f, b = p[iB] / 100.0f, ms = 0.001f * sr;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float yl = dl.read (tm[0].process (p[iLT]) * ms);
            const float yr = dl.read (tm[2].process (p[iRT]) * ms);
            const float yf = dl.read (tm[1].process (p[iFT]) * ms);
            dl.push (x + fb * damp.lp (yf));
            L[i] = x * (1.0f - b) + yl * b;
            R[i] = x * (1.0f - b) + yr * b;
        }
        st = true;
    }
private:
    int iLT, iFT, iRT, iF, iB; DelayLine dl; OnePole damp; Smoother tm[3];
};

//==============================================================================
// SFX
class Harmonizer : public Fx
{
public:
    explicit Harmonizer (const defs::TypeDef& t) : Fx (t)
    {
        iK = ix ("KEY"); iI = ix ("INT"); iP = ix ("PIT"); iB = ix ("BAL"); iT = ix ("TRK"); iL = ix ("LVL");
    }
    void prepare (double s) override
    {
        Fx::prepare (s);
        ps.prepare (sr, 40.0f);
        decim = std::max (1, (int) std::lround (sr / 11025.0f));
        srD = sr / (float) decim;
        aa1.set (Biquad::LP, srD * 0.4f, 0.7f, 0, sr); aa2.set (Biquad::LP, srD * 0.4f, 0.7f, 0, sr);
        ring.assign (kN, 0.0f); frame.assign (kN, 0.0f); diff.assign (kN, 0.0f);
        reset();
    }
    void reset() override
    {
        ps.reset(); aa1.reset(); aa2.reset(); shift.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        ringPos = decCount = hopCount = 0; note = -1; semis = 0;
    }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        shift.setTime (p[iT] > 0.5f ? 60.0f : 12.0f, sr);
        const float b = p[iB] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float a = aa2.process (aa1.process (x));
            if (++decCount >= decim)
            {
                decCount = 0;
                ring[(size_t) ringPos] = a; ringPos = (ringPos + 1) % kN;
                if (++hopCount >= kHop) { hopCount = 0; detect(); }
            }
            const float r = std::pow (2.0f, shift.process (semis) / 12.0f);
            const float w = ps.process (x, r);
            L[i] = (x * (1.0f - b) + w * b) * g;
        }
    }
private:
    static constexpr int kN = 512, kW = 256, kHop = 96;

    void detect()
    {
        float energy = 0;
        for (int j = 0; j < kN; ++j) { frame[(size_t) j] = ring[(size_t) ((ringPos + j) % kN)]; energy += frame[(size_t) j] * frame[(size_t) j]; }
        if (energy / kN < 1.0e-6f) return;   // silence : garde la dernière note

        const int maxTau = std::min (kN - kW - 1, (int) (srD / 65.0f));
        const int minTau = std::max (2, (int) (srD / 1100.0f));
        float running = 0;
        diff[0] = 1.0f;
        int found = -1;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float d = 0;
            for (int j = 0; j < kW; ++j) { const float e = frame[(size_t) j] - frame[(size_t) (j + tau)]; d += e * e; }
            running += d;
            diff[(size_t) tau] = running > 0 ? d * (float) tau / running : 1.0f;
        }
        for (int tau = minTau; tau < maxTau; ++tau)
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxTau && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
                found = tau; break;
            }
        if (found < 0) return;
        const float a = diff[(size_t) (found - 1)], b = diff[(size_t) found], c = diff[(size_t) (found + 1)];
        const float den = a - 2 * b + c;
        const float tau = (float) found + (std::fabs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
        const float f = srD / tau;
        const int midi = (int) std::lround (69.0f + 12.0f * std::log2 (f / 440.0f));
        if (midi != note) { note = midi; semis = (float) intervalFor (midi); }
    }

    int intervalFor (int midi) const
    {
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        static const int steps[7] = { 1, 2, 2, 3, 4, 5, 6 };   // 2nd, Ma3, Mi3, 4th, 5th, 6th, 7th
        const int key = iround (p[iK]), in = std::max (0, std::min (6, iround (p[iI])));
        const int* sc = in == 2 ? minor : major;
        const int rel = ((midi - key) % 12 + 12) % 12;
        int deg = 0;
        for (int d = 0; d < 7; ++d) if (sc[d] <= rel) deg = d;
        int tgt = p[iP] < 0.5f ? deg + steps[in] : deg - steps[in], oct = 0;
        while (tgt >= 7) { tgt -= 7; ++oct; }
        while (tgt < 0)  { tgt += 7; --oct; }
        return sc[tgt] + 12 * oct - sc[deg];
    }

    int iK, iI, iP, iB, iT, iL;
    PitchShifter ps; Biquad aa1, aa2; Smoother shift;
    std::vector<float> ring, frame, diff;
    int decim = 4, ringPos = 0, decCount = 0, hopCount = 0, note = -1;
    float srD = 11025, semis = 0;
};

class AdvFlanger : public Fx
{
public:
    explicit AdvFlanger (const defs::TypeDef& t) : Fx (t)
    {
        iD = ix ("DPT"); iS = ix ("SPD"); iED = ix ("EGD"); iES = ix ("EGS"); iTD = ix ("TDl"); iEt = ix ("EGt"); iF = ix ("F.B"); iL = ix ("LVL");
    }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (0.03f * sr)); onset.prepare (sr); }
    void reset() override { dl.reset(); onset.reset(); eg = 0; wait = -1; ph = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float egTime = 0.01f * std::pow (200.0f, (p[iEt] - 1.0f) / 99.0f);  // 10 ms - 2 s
        const float egInc = 1.0f / (egTime * sr), egDec = 1.0f / (0.25f * sr);
        const float fb = p[iF] / 100.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            if (onset.process (x)) { wait = (int) (p[iTD] * 0.001f * sr); eg = 0; }
            if (wait >= 0) { if (wait-- == 0) rising = true; }
            if (rising) { eg += egInc; if (eg >= 1.0f) { eg = 1.0f; rising = false; } }
            else if (onset.level() < 0.004f) eg = std::max (0.0f, eg - egDec);

            const float depth = clampf (p[iD] / 100.0f + p[iED] / 98.0f * eg, 0.0f, 1.0f);
            const float speed = p[iS] / 10.0f * std::pow (2.0f, p[iES] / 98.0f * 3.0f * eg);
            ph += speed / sr; ph -= std::floor (ph);
            const float d = (0.3f + depth * 6.0f * (0.5f + 0.5f * Lfo::triAt (ph))) * 0.001f * sr;
            const float y = dl.read (d);
            dl.push (x + fb * y);
            L[i] = 0.5f * (x + y) * g;
        }
    }
private:
    int iD, iS, iED, iES, iTD, iEt, iF, iL, wait = -1; bool rising = false;
    float eg = 0, ph = 0; DelayLine dl; Onset onset;
};

class BendChorus : public Fx
{
public:
    explicit BendChorus (const defs::TypeDef& t) : Fx (t) { iD = ix ("DPT"); iT = ix ("BTm"); iB = ix ("BAL"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); ps.prepare (sr, 30.0f); env.set (2.0f, 200.0f, sr); }
    void reset() override { ps.reset(); env.reset(); bend.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        bend.setTime (5.0f + p[iT] * 15.0f, sr);
        const float b = p[iB] / 100.0f, g = levelGain (p[iL]), amount = p[iD] / 98.0f * 1.5f;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            const float level = clampf (std::sqrt (env.process (x) * 6.0f), 0.0f, 1.0f);   // bend proportionnel au niveau
            const float w = ps.process (x, std::pow (2.0f, bend.process (amount * level) / 12.0f));
            L[i] = (x * (1.0f - b) + w * b) * g;
        }
    }
private:
    int iD, iT, iB, iL; PitchShifter ps; EnvFollower env; Smoother bend;
};

class PickingMod : public Fx
{
public:
    explicit PickingMod (const defs::TypeDef& t) : Fx (t) { iF = ix ("FRQ"); iD = ix ("DEC"); i2D = ix ("2Dl"); i2R = ix ("2Rt"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); onset.prepare (sr); }
    void reset() override { onset.reset(); amt = 0; ph = 0; t = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float f0 = 30.0f * std::pow (100.0f, p[iF] / 100.0f);
        const float decay = onePoleCoef (10.0f * std::pow (200.0f, p[iD] / 100.0f), sr);
        const float delayS = p[i2D] * 0.001f, riseS = p[i2R] / 100.0f;
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            if (onset.process (x)) { amt = 1.0f; t = 0; }
            t += 1.0f / sr;
            float f = f0;
            if (riseS > 0 && t > delayS) f *= std::pow (2.0f, std::min (1.0f, (t - delayS) / riseS));   // 2e transposition
            ph += f / sr; ph -= std::floor (ph);
            L[i] = (x * (1.0f - amt) + x * std::sin (kTwoPi * ph) * amt * 1.3f) * g;
            amt *= decay;
        }
    }
private:
    int iF, iD, i2D, i2R, iL; float amt = 0, ph = 0, t = 0; Onset onset;
};

class SuperCry : public Fx
{
public:
    explicit SuperCry (const defs::TypeDef& t) : Fx (t) { iP = ix ("PTN"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); env.set (3.0f, 180.0f, sr); }
    void reset() override { for (auto& f : fm) f.reset(); env.reset(); lfo.phase = 0; }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        // voyelles (F1, F2, F3) : u, o, a, e, i
        static const float V[5][3] = { { 300, 870, 2240 }, { 500, 900, 2500 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
        static const int seq[5][5] = { { 0, 2, -1 }, { 1, 3, -1 }, { 0, 1, 2, 3, 4 }, { 4, 2, -1 }, { 2, 0, -1 } };
        const int ptn = std::max (1, std::min (5, iround (p[iP]))) - 1;
        int len = 0; while (len < 5 && seq[ptn][len] >= 0) ++len;
        lfo.setRate (4.0f, sr);
        const float g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            float m = clampf (std::pow (env.process (x) * 14.0f, 0.7f), 0.0f, 1.0f);
            if (ptn == 4) m *= 0.5f + 0.5f * lfo.sine();   // motif 5 : « oua-oua » rythmé
            lfo.tick();
            if ((i & 7) == 0)
            {
                const float pos = m * (float) (len - 1);
                const int a = std::min ((int) pos, len - 2), b = a + 1;
                const float f = pos - (float) a;
                for (int k = 0; k < 3; ++k)
                    fm[k].set (lerp (V[seq[ptn][a]][k], V[seq[ptn][b]][k], f), 8.0f + 3.0f * (float) k, sr);
            }
            L[i] = (fm[0].bandNorm (x) * 4.0f + fm[1].bandNorm (x) * 2.8f + fm[2].bandNorm (x) * 1.4f) * g;
        }
    }
private:
    int iP, iL; Svf fm[3]; EnvFollower env; Lfo lfo;
};

class Bomber : public Fx
{
public:
    explicit Bomber (const defs::TypeDef& t) : Fx (t) { iT = ix ("TRG"); iP = ix ("PTN"); iD = ix ("DEC"); iB = ix ("BAL"); iL = ix ("LVL"); }
    void prepare (double s) override { Fx::prepare (s); onset.prepare (sr); onset.thr = 0.02f; }
    void reset() override { onset.reset(); amp = 0; svf.reset(); crackle.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const int ptn = std::max (1, std::min (3, iround (p[iP])));
        const float decS = 0.1f * std::pow (40.0f, p[iD] / 100.0f);
        const float ampDec = onePoleCoef (decS * 1000.0f, sr);
        const float cutDec = onePoleCoef (decS * 350.0f, sr);
        const float b = p[iB] / 100.0f, g = levelGain (p[iL]);
        const bool armed = p[iT] > 0.5f;
        bool trig = manTrigger;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            if ((onset.process (x) && armed) || trig) { trig = false; amp = 1.0f; cut = 1.0f; subPh = 0; }
            float boom = 0;
            if (amp > 1.0e-4f)
            {
                const float noise = rng.bipolar();
                const float fc = 60.0f + (ptn == 3 ? 6000.0f : 3500.0f) * cut;
                if ((i & 7) == 0) svf.set (fc, ptn == 3 ? 2.5f : 0.9f, sr);
                float lp, bp; svf.process (noise, lp, bp);
                const float subF = 32.0f + (ptn == 2 ? 140.0f : 90.0f) * cut;
                subPh += subF / sr; subPh -= std::floor (subPh);
                const float sub = std::sin (kTwoPi * subPh);
                const float crack = ptn == 3 && rng.next01() < 0.002f * cut ? rng.bipolar() * 3.0f : 0.0f;
                boom = (ptn == 2 ? lp * 0.6f + sub * 1.2f : ptn == 3 ? bp * 1.2f + sub * 0.5f + crackle.process (crack) : lp * 1.0f + sub * 0.8f) * amp;
                amp *= ampDec; cut *= cutDec;
            }
            L[i] = (x * (1.0f - b) + boom * b) * g;
        }
        manTrigger = false;
    }
private:
    struct Crackle { float z = 0; float process (float x) { z = x + 0.97f * z; return z * 0.2f; } void reset() { z = 0; } };
    int iT, iP, iD, iB, iL; float amp = 0, cut = 0, subPh = 0; Svf svf; Rng rng; Onset onset; Crackle crackle;
};

class Zitar : public Fx
{
public:
    explicit Zitar (const defs::TypeDef& t) : Fx (t) { iD = ix ("DPT"); iR = ix ("RES"); iL = ix ("LVL"); }
    void prepare (double s) override
    {
        Fx::prepare (s);
        buzz.init ((int) (0.01f * sr));
        static const float drone[6] = { 138.6f, 207.7f, 277.2f, 311.1f, 349.2f, 415.3f };   // cordes sympathiques
        for (int k = 0; k < 6; ++k) { len[k] = sr / drone[k]; comb[k].init ((int) len[k] + 4); damp[k].setCutoff (3500.0f, sr); }
        hp.set (Biquad::HP, 300.0f, 0.7f, 0, sr);
        shape.setTime (0.3f, sr);
    }
    void reset() override { buzz.reset(); for (auto& c : comb) c.reset(); for (auto& d : damp) d.reset(); hp.reset(); shape.reset(); }
    void process (const Ctx&, float* L, float*, int n, bool&) override
    {
        const float d = p[iD] / 10.0f, r = p[iR] / 10.0f, g = levelGain (p[iL]);
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            buzz.push (x);
            // « jawari » : retard qui dépend de l'amplitude → bourdonnement du chevalet
            const float dly = shape.process ((0.2f + d * 1.6f * clampf (std::fabs (x) * 5.0f, 0.0f, 1.0f)) * 0.001f * sr);
            float y = 0.5f * (x + buzz.read (dly)) + d * 0.25f * std::tanh (hp.process (x) * 8.0f) * std::fabs (x) * 4.0f;
            float sym = 0;
            const float exc = y * 0.04f;
            for (int k = 0; k < 6; ++k)
            {
                const float o = comb[k].read (len[k] - 1.0f);
                comb[k].push (exc + 0.985f * damp[k].lp (o));
                sym += o;
            }
            L[i] = (y + r * sym * 0.6f) * g;
        }
    }
private:
    int iD, iR, iL; DelayLine buzz, comb[6]; OnePole damp[6]; float len[6] {}; Biquad hp; Smoother shape;
};

//==============================================================================
// REV
inline float reverbInput (const Ctx& c, int src, const float* L, const float* R, bool st, int i)
{
    if (! st) return L[i];
    if (c.parallel && src == 0) return L[i];
    if (c.parallel && src == 2) return R[i];
    return 0.5f * (L[i] + R[i]);
}

class Reverb : public Fx
{
public:
    Reverb (const defs::TypeDef& t, bool v2) : Fx (t), second (v2)
    {
        iT = ix ("RvT"); iP = ix ("PDI"); iC = ix ("COL"); iM = ix ("MIX"); iS = ix ("SRC");
    }
    void prepare (double s) override
    {
        Fx::prepare (s);
        static const float base1[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        static const float base2[8] = { 19.1f, 23.3f, 26.9f, 29.3f, 33.7f, 37.9f, 41.3f, 45.1f };
        static const float diffMs[6] = { 4.7f, 3.6f, 12.7f, 9.3f, 6.1f, 2.9f };
        for (int k = 0; k < 8; ++k)
        {
            baseMs[k] = second ? base2[k] : base1[k];
            line[k].init ((int) (baseMs[k] * 1.6f * 0.001f * sr) + 64);
        }
        nDiff = second ? 6 : 4;
        for (int k = 0; k < nDiff; ++k) diff[k].init ((int) (diffMs[k] * 0.001f * sr), second ? 0.72f : 0.65f);
        pre.init ((int) (0.11f * sr));
        lfo.setRate (0.6f, sr);
        inHp.set (Biquad::HP, 110.0f, 0.6f, 0, sr);
    }
    void reset() override
    {
        pre.reset(); inHp.reset(); for (auto& l : line) l.reset(); for (auto& d : diff) d.reset(); for (auto& d : damp) d.reset();
    }
    void process (const Ctx& c, float* L, float* R, int n, bool& st) override
    {
        const int col = std::max (1, std::min (5, iround (p[iC])));
        static const float sizes[5] = { 0.6f, 0.8f, 1.0f, 1.25f, 1.55f };
        static const float cuts[5]  = { 2500.0f, 4000.0f, 6500.0f, 9500.0f, 14000.0f };
        const float size = second ? sizes[col - 1] : 1.0f;
        const float cut = second ? 6500.0f : cuts[col - 1];
        const float rt = std::max (0.3f, p[iT] / 10.0f);
        float len[8], g[8];
        for (int k = 0; k < 8; ++k)
        {
            len[k] = baseMs[k] * size * 0.001f * sr;
            g[k] = hold ? 1.0f : std::pow (10.0f, -3.0f * len[k] / (rt * sr));
            damp[k].setCutoff (cut, sr);
        }
        // injection normalisée : le gain moyen de la boucle reste ~1 quel que soit RvT
        float gMean = 0;
        for (float gk : g) gMean += gk * gk;
        const float inject = 0.5f * std::sqrt (std::max (0.002f, 1.0f - gMean / 8.0f));
        const float pd = p[iP] * 0.001f * sr, mix = p[iM] / 100.0f * 1.1f;
        const float outScale = second ? 0.3f : 0.55f;
        const int src = iround (p[iS]);
        const float modDepth = 6.0f * sr / 44100.0f;
        static const float sL[8] = { 1, 1, -1, 1, -1, -1, 1, -1 }, sR[8] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        for (int i = 0; i < n; ++i)
        {
            float in = inHp.process (reverbInput (c, src, L, R, st, i)) * inGain * (hold ? 0.0f : 1.0f);
            pre.push (in);
            float d = pre.read (pd);
            for (int k = 0; k < nDiff; ++k) d = diff[k].process (d);

            lfo.tick();
            float o[8];
            for (int k = 0; k < 8; ++k)
            {
                const float m = k == 0 ? modDepth * (1.0f + lfo.sine()) : k == 5 ? modDepth * (1.0f + lfo.sine (0.33f)) : 0.0f;
                o[k] = line[k].read (len[k] - 1.0f + m);
                if (! hold) o[k] = damp[k].lp (o[k]);
            }
            float mx[8];
            for (int k = 0; k < 8; ++k) mx[k] = o[k];
            hadamard (mx);
            for (int k = 0; k < 8; ++k) line[k].push (d * ((k & 1) ? -inject : inject) + mx[k] * g[k]);

            float wl = 0, wr = 0;
            for (int k = 0; k < 8; ++k) { wl += o[k] * sL[k]; wr += o[k] * sR[k]; }
            const float dl = L[i], dr = st ? R[i] : L[i];
            L[i] = dl + wl * outScale * mix;
            R[i] = dr + wr * outScale * mix;
        }
        st = true;
    }
private:
    static void hadamard (float* x)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h * 2)
                for (int j = i; j < i + h; ++j) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }
        for (int i = 0; i < 8; ++i) x[i] *= 0.35355339f;
    }
    bool second; int iT, iP, iC, iM, iS, nDiff = 4;
    DelayLine pre, line[8]; Allpass diff[6]; OnePole damp[8]; float baseMs[8] {}; Lfo lfo; Biquad inHp;
};

class EarlyRef : public Fx
{
public:
    EarlyRef (const defs::TypeDef& t, bool v2) : Fx (t), second (v2)
    {
        iR = ix ("RmS"); iP = ix ("PDI"); iX = ix (second ? "ENV" : "HiC"); iM = ix ("MIX"); iS = ix ("SRC");
        Rng r; r.s = second ? 0xBADC0DEu : 0x1234567u;
        for (int k = 0; k < kTaps; ++k)
        {
            posL[k] = clampf (((float) k + 0.5f + 0.8f * (r.next01() - 0.5f)) / kTaps, 0.0f, 1.0f);
            posR[k] = clampf (((float) k + 0.5f + 0.8f * (r.next01() - 0.5f)) / kTaps, 0.0f, 1.0f);
            rnd[k] = 0.55f + 0.45f * r.next01();
        }
    }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (0.5f * sr)); apL.init ((int) (0.0031f * sr), 0.5f); apR.init ((int) (0.0043f * sr), 0.5f); lastX = -999; }
    void reset() override { dl.reset(); apL.reset(); apR.reset(); lpL.reset(); lpR.reset(); }
    void process (const Ctx& c, float* L, float* R, int n, bool& st) override
    {
        const float lenS = (5.0f + p[iR] * 1.6f) * 0.001f * sr, pd = p[iP] * 0.001f * sr;
        if (p[iX] != lastX)
        {
            lastX = p[iX];
            const float fc = second ? 9000.0f : 20000.0f * std::pow (1500.0f / 20000.0f, lastX / 10.0f);
            lpL.set (Biquad::LP, fc, 0.7f, 0, sr); lpR.set (Biquad::LP, fc, 0.7f, 0, sr);
        }
        float tl[kTaps], tr[kTaps], al[kTaps], ar[kTaps];
        const float env = second ? p[iX] / 10.0f : 0.0f;
        for (int k = 0; k < kTaps; ++k)
        {
            tl[k] = pd + posL[k] * lenS; tr[k] = pd + posR[k] * lenS;
            al[k] = shape (posL[k], env) * rnd[k]; ar[k] = shape (posR[k], env) * rnd[kTaps - 1 - k];
        }
        const float mix = p[iM] / 100.0f * 1.2f / std::sqrt ((float) kTaps) * 1.5f;
        const int src = iround (p[iS]);
        for (int i = 0; i < n; ++i)
        {
            dl.push (reverbInput (c, src, L, R, st, i) * inGain);
            float wl = 0, wr = 0;
            for (int k = 0; k < kTaps; ++k) { wl += dl.read (tl[k]) * al[k]; wr += dl.read (tr[k]) * ar[k]; }
            wl = apL.process (lpL.process (wl)); wr = apR.process (lpR.process (wr));
            const float dl0 = L[i], dr0 = st ? R[i] : L[i];
            L[i] = dl0 + wl * mix;
            R[i] = dr0 + wr * mix;
        }
        st = true;
    }
private:
    float shape (float u, float env) const
    {
        if (! second) return std::exp (-2.5f * u);
        if (env > 0) return lerp (1.0f, 1.0f - u, env);       // décroissant
        if (env < 0) return lerp (1.0f, u, -env);             // inversé
        return 1.0f;                                          // porte
    }
    static constexpr int kTaps = 20;
    bool second; int iR, iP, iX, iM, iS; float lastX = -999;
    float posL[kTaps] {}, posR[kTaps] {}, rnd[kTaps] {};
    DelayLine dl; Allpass apL, apR; Biquad lpL, lpR;
};

class MultiTap : public Fx
{
public:
    explicit MultiTap (const defs::TypeDef& t) : Fx (t)
    {
        iT = ix ("TM"); iI = ix ("INT"); iP = ix ("PTN"); iSl = ix ("SLO"); iH = ix ("HiC"); iM = ix ("MIX"); iS = ix ("SRC");
    }
    void prepare (double s) override { Fx::prepare (s); dl.init ((int) (0.95f * sr)); tm.setTime (120.0f, sr); }
    void reset() override { dl.reset(); lpL.reset(); lpR.reset(); tm.reset(); }
    void process (const Ctx& c, float* L, float* R, int n, bool& st) override
    {
        const float q = std::pow (2.0f, -p[iI] / 10.0f);
        const int ptn = iround (p[iP]);
        const float slo = p[iSl] / 10.0f;
        float u[8], gl[8], gr[8];
        for (int k = 0; k < 8; ++k)
        {
            const float x = (float) (k + 1) / 8.0f;
            u[k] = std::pow (x, q);
            const float a = clampf (1.0f + slo * (x - 0.5f) * 1.8f, 0.05f, 1.6f) * 0.55f;
            float pan;
            switch (ptn)
            {
                case 0:  pan = -1.0f + 2.0f * (float) k / 7.0f; break;
                case 1:  pan = 1.0f - 2.0f * (float) k / 7.0f; break;
                case 2:  pan = Lfo::triAt ((float) k / 4.0f); break;
                default: pan = (k & 1) ? 1.0f : -1.0f; break;
            }
            const float ang = (pan + 1.0f) * kPi * 0.25f;
            gl[k] = a * std::cos (ang) * 1.4142f; gr[k] = a * std::sin (ang) * 1.4142f;
        }
        const float fc = 20000.0f * std::pow (1500.0f / 20000.0f, p[iH] / 10.0f);
        lpL.setCutoff (fc, sr); lpR.setCutoff (fc, sr);
        const float mix = p[iM] / 100.0f * 1.2f;
        const int src = iround (p[iS]);
        for (int i = 0; i < n; ++i)
        {
            const float total = tm.process (p[iT]) * 0.001f * sr;
            dl.push (reverbInput (c, src, L, R, st, i) * inGain);
            float wl = 0, wr = 0;
            for (int k = 0; k < 8; ++k) { const float y = dl.read (u[k] * total); wl += y * gl[k]; wr += y * gr[k]; }
            const float dl0 = L[i], dr0 = st ? R[i] : L[i];
            L[i] = dl0 + lpL.lp (wl) * mix;
            R[i] = dr0 + lpR.lp (wr) * mix;
        }
        st = true;
    }
private:
    int iT, iI, iP, iSl, iH, iM, iS; DelayLine dl; OnePole lpL, lpR; Smoother tm;
};

class PingPong : public Fx
{
public:
    explicit PingPong (const defs::TypeDef& t) : Fx (t) { iT = ix ("TM"); iF = ix ("F.B"); iH = ix ("HiD"); iM = ix ("MIX"); iS = ix ("SRC"); }
    void prepare (double s) override { Fx::prepare (s); dL.init ((int) (0.95f * sr)); dR.init ((int) (0.95f * sr)); tm.setTime (120.0f, sr); }
    void reset() override { dL.reset(); dR.reset(); lpL.reset(); lpR.reset(); tm.reset(); }
    void process (const Ctx& c, float* L, float* R, int n, bool& st) override
    {
        const float fb = p[iF] / 100.0f, mix = p[iM] / 100.0f * 1.2f;
        const float fc = 18000.0f * std::pow (1500.0f / 18000.0f, p[iH] / 10.0f);
        lpL.setCutoff (fc, sr); lpR.setCutoff (fc, sr);
        const int src = iround (p[iS]);
        for (int i = 0; i < n; ++i)
        {
            const float d = tm.process (p[iT]) * 0.001f * sr;
            const float yl = dL.read (d), yr = dR.read (d);
            dL.push (reverbInput (c, src, L, R, st, i) * inGain + fb * lpR.lp (yr));
            dR.push (fb * lpL.lp (yl));
            const float dl0 = L[i], dr0 = st ? R[i] : L[i];
            L[i] = dl0 + yl * mix;
            R[i] = dr0 + yr * mix;
        }
        st = true;
    }
private:
    int iT, iF, iH, iM, iS; DelayLine dL, dR; OnePole lpL, lpR; Smoother tm;
};
} // namespace

//==============================================================================
std::unique_ptr<Fx> createFx (const defs::TypeDef& t)
{
    const std::string& c = t.code;
    if (c == "CMP") return std::make_unique<Compressor> (t, false);
    if (c == "LIM") return std::make_unique<Compressor> (t, true);
    if (c == "EXT") return std::make_unique<External> (t);
    if (c == "DST") return std::make_unique<Distortion> (t, 0);
    if (c == "OD")  return std::make_unique<Distortion> (t, 1);
    if (c == "CRU") return std::make_unique<Distortion> (t, 2);
    if (c == "4Gt") return std::make_unique<Eq4> (t);
    if (c == "3EQ") return std::make_unique<Eq3> (t);
    if (c == "ENH") return std::make_unique<Enhancer> (t);
    if (c == "GtA") return std::make_unique<AmpSim> (t, false);
    if (c == "BsA") return std::make_unique<AmpSim> (t, true);
    if (c == "PIT" || c == "PT2") return std::make_unique<PitchFx> (t);
    if (c == "PHA" || c == "PH2") return std::make_unique<Phaser> (t, false);
    if (c == "PH3") return std::make_unique<Phaser> (t, true);
    if (c == "FLG") return std::make_unique<Flanger> (t, 100.0f);
    if (c == "FL2") return std::make_unique<Flanger> (t, 900.0f);
    if (c == "CHO") return std::make_unique<Chorus> (t, 0);
    if (c == "CH2") return std::make_unique<Chorus> (t, 1);
    if (c == "CH3") return std::make_unique<Chorus> (t, 2);
    if (c == "AWA") return std::make_unique<AutoWah> (t);
    if (c == "CRY" || c == "CR2") return std::make_unique<Cry> (t);
    if (c == "MET") return std::make_unique<Metallic> (t);
    if (c == "DBL") return std::make_unique<Doubling> (t);
    if (c == "COF") return std::make_unique<CombFilter> (t);
    if (c == "TRM" || c == "TR2") return std::make_unique<Tremolo> (t);
    if (c == "OCT") return std::make_unique<Octave> (t);
    if (c == "PWA") return std::make_unique<PedalWah> (t);
    if (c == "STP") return std::make_unique<Step> (t);
    if (c == "PAN") return std::make_unique<AutoPan> (t);
    if (c == "DEL") return std::make_unique<Delay> (t);
    if (c == "ECH") return std::make_unique<Echo> (t);
    if (c == "HPS") return std::make_unique<Harmonizer> (t);
    if (c == "AFL") return std::make_unique<AdvFlanger> (t);
    if (c == "BND") return std::make_unique<BendChorus> (t);
    if (c == "PIC") return std::make_unique<PickingMod> (t);
    if (c == "SCR") return std::make_unique<SuperCry> (t);
    if (c == "BOM") return std::make_unique<Bomber> (t);
    if (c == "ZIT") return std::make_unique<Zitar> (t);
    if (c == "Rv1") return std::make_unique<Reverb> (t, false);
    if (c == "Rv2") return std::make_unique<Reverb> (t, true);
    if (c == "ER1") return std::make_unique<EarlyRef> (t, false);
    if (c == "ER2") return std::make_unique<EarlyRef> (t, true);
    if (c == "MTD") return std::make_unique<MultiTap> (t);
    if (c == "PPD") return std::make_unique<PingPong> (t);
    return std::make_unique<External> (t);
}
}
