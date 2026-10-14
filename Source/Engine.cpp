#include "Engine.h"

using namespace defs;

Engine::Engine (SharedState& s) : sh (s)
{
    for (int m = 0; m < NumModules; ++m)
        for (int t = 0; t < numTypes (m); ++t)
            slots[m].fx.push_back (fx::createFx (type (m, t)));
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& s : slots)
        for (auto& f : s.fx) f->prepare (sr);
    fadeStep = (float) (1.0 / (0.012 * sr));
    ampLp.set (fx::Biquad::LP, 5000.0f, 0.7f, 0, (float) sr);
    ampLpR = ampLp;
    lastSerial = -1;
    patchFade = 0;
    bypassFade = sh.allBypass.load() ? 1.0f : 0.0f;
}

void Engine::process (float* L, float* R, int n)
{
    for (int off = 0; off < n; off += kChunk)
    {
        const int len = std::min (kChunk, n - off);
        processChunk (L + off, R + off, len);
        clock += len;
    }
}

void Engine::computeParams (int m, Slot& s, float* eff)
{
    const auto& td = type (m, s.cur);
    auto& f = *s.fx[(size_t) s.cur];
    // Pendant un changement de type, l'ancien effet garde ses propres valeurs
    // (celles du nouveau type n'ont pas le même sens).
    if (sh.type[m].load() == s.cur || ! s.hasEff)
    {
        for (int i = 0; i < kMaxParams; ++i)
        {
            float v = (float) sh.val[m][i].load (std::memory_order_relaxed);
            if (i < (int) td.params.size())
                v = fx::clampf (v, (float) td.params[(size_t) i].min, (float) td.params[(size_t) i].max);
            s.lastEff[i] = v;
        }
        s.hasEff = true;
    }
    std::copy (s.lastEff, s.lastEff + kMaxParams, eff);
    f.inGain = 1.0f;
    f.hold = false;
    if (td.iDES < 0) return;

    const auto& ctl = ctlNumbers();
    const int des = std::max (0, std::min ((int) eff[td.iDES], (int) td.des.size() - 1));
    const int ccNum = ctl[(size_t) std::max (0, std::min ((int) eff[td.iCTL], (int) ctl.size() - 1))];
    const float c = (float) sh.cc[ccNum].load (std::memory_order_relaxed) / 127.0f;
    const float ratio = eff[td.iRAT];
    const auto& tg = td.des[(size_t) des];
    const bool rising = c >= 0.5f && s.prevCtl < 0.5f;
    s.prevCtl = c;

    if (ratio > 100.5f)   // « Tap » : la pédale donne le tempo du delay
    {
        if (tg.a >= 0 && td.params[(size_t) tg.a].label == "TM")
        {
            if (rising)
            {
                if (s.lastTap >= 0)
                {
                    const float ms = (float) ((double) (clock - s.lastTap) * 1000.0 / sr);
                    const auto& pd = td.params[(size_t) tg.a];
                    if (ms >= (float) pd.min && ms <= (float) pd.max) s.tapMs = ms;
                }
                s.lastTap = clock;
            }
            if (s.tapMs > 0) eff[tg.a] = s.tapMs;
        }
        return;
    }

    const float r = ratio / 100.0f;
    auto apply = [&] (int pi)
    {
        if (pi < 0) return;
        const auto& pd = td.params[(size_t) pi];
        eff[pi] = fx::clampf (eff[pi] + r * c * (float) (pd.max - pd.min), (float) pd.min, (float) pd.max);
    };
    switch (tg.a)
    {
        case SP_INP: f.inGain = r >= 0 ? (1.0f - r) + r * c : 1.0f + r * c; break;
        case SP_HLD:
        {
            const int col = td.find ("COL");
            f.hold = c >= 0.5f && (col < 0 || (int) eff[col] == 5);
            break;
        }
        case SP_MAN: if (rising) f.manTrigger = true; break;
        default: apply (tg.a); apply (tg.b); break;
    }
}

void Engine::runModule (int m, bool allowed, const fx::Ctx& ctx, float* L, float* R, int n, bool& st)
{
    Slot& s = slots[m];
    const int t = std::max (0, std::min (sh.type[m].load(), numTypes (m) - 1));
    const bool want = allowed && sh.on[m].load() != 0;

    if (s.fade <= 0.0f && t != s.cur) { s.cur = t; s.hasEff = false; s.fx[(size_t) t]->reset(); }
    const float target = (want && t == s.cur) ? 1.0f : 0.0f;
    if (s.fade <= 0.0f && target <= 0.0f) return;
    if (s.fade <= 0.0f) s.fx[(size_t) s.cur]->reset();   // redémarre proprement

    float eff[kMaxParams];
    computeParams (m, s, eff);
    if (m == AMP && sh.on[SFX].load())
    {
        const int str = type (m, s.cur).find ("STR");   // stéréo indisponible avec SFX
        if (str >= 0) eff[str] = 0;
    }
    auto& f = *s.fx[(size_t) s.cur];
    f.setParams (eff);

    std::copy (L, L + n, dL);
    if (st) std::copy (R, R + n, dR);
    bool st2 = st;
    f.process (ctx, L, R, n, st2);

    for (int i = 0; i < n; ++i)
    {
        s.fade = target > s.fade ? std::min (target, s.fade + fadeStep) : std::max (target, s.fade - fadeStep);
        const float g = s.fade;
        L[i] = dL[i] + g * (L[i] - dL[i]);
        if (st2)
        {
            const float d = st ? dR[i] : dL[i];
            R[i] = d + g * (R[i] - d);
        }
    }
    st = st2;
}

void Engine::processChunk (float* L, float* R, int n)
{
    std::copy (L, L + n, dry);

    if (sh.power.load() == 0)
    {
        std::copy (L, L + n, R);
        return;
    }

    // Changement de patch : fondu de sortie, remise à zéro des effets, fondu d'entrée
    const int serial = sh.patchSerial.load();
    if (serial != lastSerial && patchFade <= 0.0f)
    {
        lastSerial = serial;
        for (int m = 0; m < NumModules; ++m)
        {
            auto& s = slots[m];
            s.cur = std::max (0, std::min (sh.type[m].load(), numTypes (m) - 1));
            s.fx[(size_t) s.cur]->reset();
            s.hasEff = false;
            s.fade = 0;
            s.tapMs = -1; s.lastTap = -1;
        }
    }
    const float patchTarget = serial == lastSerial ? 1.0f : 0.0f;
    const float bypassTarget = sh.allBypass.load() ? 1.0f : 0.0f;

    fx::Ctx ctx;
    ctx.dryIn = dry;
    bool st = false;

    runModule (COMP, true, ctx, L, R, n, st);
    runModule (EXT,  true, ctx, L, R, n, st);
    runModule (DIST, true, ctx, L, R, n, st);
    runModule (EQ,   true, ctx, L, R, n, st);
    runModule (AMP,  true, ctx, L, R, n, st);

    const bool sfxOn = sh.on[SFX].load() != 0;
    if (sfxOn)
    {
        runModule (EFF1, false, ctx, L, R, n, st);
        runModule (EFF2, false, ctx, L, R, n, st);
        runModule (SFX, true, ctx, L, R, n, st);
    }
    else if (st)
    {
        // Routage parallèle : sortie gauche de l'ampli → EFF1, droite → EFF2
        ctx.parallel = true;
        std::copy (R, R + n, aR);
        bool s1 = false, s2 = false;
        runModule (EFF1, true, ctx, L, aL, n, s1);
        runModule (EFF2, true, ctx, aR, aL, n, s2);
        for (int i = 0; i < n; ++i) R[i] = s2 ? 0.5f * (aR[i] + aL[i]) : aR[i];
        runModule (SFX, false, ctx, L, R, n, st);
    }
    else
    {
        runModule (EFF1, true, ctx, L, R, n, st);
        runModule (EFF2, true, ctx, L, R, n, st);
        runModule (SFX, false, ctx, L, R, n, st);
    }
    runModule (REV, true, ctx, L, R, n, st);
    if (! st) std::copy (L, L + n, R);

    const float level = fx::levelGain ((float) sh.level.load());
    const bool amp = sh.outAmp.load() != 0, mono = sh.outMono.load() != 0;
    for (int i = 0; i < n; ++i)
    {
        patchFade = patchTarget > patchFade ? std::min (1.0f, patchFade + fadeStep) : std::max (0.0f, patchFade - fadeStep);
        bypassFade = bypassTarget > bypassFade ? std::min (1.0f, bypassFade + fadeStep) : std::max (0.0f, bypassFade - fadeStep);
        float l = L[i] * level, r = R[i] * level;
        if (amp) { l = ampLp.process (l) * 0.5f; r = ampLpR.process (r) * 0.5f; }   // sortie pour ampli guitare
        if (mono) l = r = 0.5f * (l + r);
        const float wet = (1.0f - bypassFade) * patchFade;
        L[i] = l * wet + dry[i] * bypassFade;
        R[i] = r * wet + dry[i] * bypassFade;
    }
}
