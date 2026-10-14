#pragma once
#include "Shared.h"
#include "dsp/DspUtil.h"
#include "dsp/Effects.h"
#include <memory>
#include <vector>

/** Chaîne COMP → EXT → DIST → EQ → AMP → EFF1/EFF2 ou SFX → REV,
    avec les quatre routages du manuel (Series 1, Series 2, Parallel, SFX). */
class Engine
{
public:
    explicit Engine (SharedState& s);
    void prepare (double sampleRate);
    /** L contient l'entrée mono ; L et R reçoivent la sortie stéréo. */
    void process (float* L, float* R, int n);

private:
    static constexpr int kChunk = 32;

    struct Slot
    {
        std::vector<std::unique_ptr<fx::Fx>> fx;
        int cur = 0;
        float fade = 0;
        float prevCtl = 0;
        long long lastTap = -1;
        float tapMs = -1;
        float lastEff[defs::kMaxParams] {};
        bool hasEff = false;
    };

    void processChunk (float* L, float* R, int n);
    void runModule (int m, bool allowed, const fx::Ctx&, float* L, float* R, int n, bool& st);
    void computeParams (int m, Slot&, float* eff);

    SharedState& sh;
    Slot slots[defs::NumModules];
    double sr = 44100;
    float fadeStep = 0.01f;
    float patchFade = 0, bypassFade = 0;
    int lastSerial = -1;
    long long clock = 0;
    float dry[kChunk] {}, dL[kChunk] {}, dR[kChunk] {}, aL[kChunk] {}, aR[kChunk] {};
    fx::Biquad ampLp, ampLpR;
};
