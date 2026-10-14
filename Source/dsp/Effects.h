#pragma once
#include "../EffectDefs.h"
#include <memory>

namespace fx
{
struct Ctx
{
    const float* dryIn = nullptr;  // signal d'entrée brut (avant COMP), pour TRG=pre
    bool parallel = false;         // routage parallèle EFF1/EFF2 (choix SRC de la réverb)
};

/** Un type d'effet. Les paramètres arrivent en unités de l'appareil (ex. DPT 1-13). */
class Fx
{
public:
    explicit Fx (const defs::TypeDef& t) : td (t) {}
    virtual ~Fx() = default;

    virtual void prepare (double sampleRate) { sr = (float) sampleRate; }
    virtual void reset() {}
    void setParams (const float* v) { for (int i = 0; i < defs::kMaxParams; ++i) p[i] = v[i]; }

    /** Traite n échantillons sur place. Si stereo == false, seul L est valide en entrée.
        Un effet stéréo écrit L et R et passe stereo à true. */
    virtual void process (const Ctx&, float* L, float* R, int n, bool& stereo) = 0;

    // Cibles spéciales de la modulation temps réel
    float inGain = 1.0f;     // INP
    bool hold = false;       // HLD
    bool manTrigger = false; // MAN (front montant)

protected:
    int ix (const char* label) const { int i = td.find (label); return i < 0 ? 0 : i; }
    const defs::TypeDef& td;
    float sr = 44100.0f;
    float p[defs::kMaxParams] {};
};

std::unique_ptr<Fx> createFx (const defs::TypeDef&);
}
