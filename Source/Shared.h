#pragma once
#include "EffectDefs.h"
#include <atomic>

// État partagé entre le thread de messages (Controller) et le thread audio (Engine).
// Le Controller écrit le patch courant ici ; le moteur le lit à chaque bloc.
struct SharedState
{
    SharedState()
    {
        for (int m = 0; m < defs::NumModules; ++m)
        {
            type[m].store (0);
            on[m].store (0);
            for (auto& v : val[m]) v.store (0);
        }
        for (auto& c : cc) c.store (0);
    }

    std::atomic<int> level { 80 };
    std::atomic<int> type[defs::NumModules];
    std::atomic<int> on[defs::NumModules];
    std::atomic<int> val[defs::NumModules][defs::kMaxParams];

    std::atomic<int> allBypass { 0 };
    std::atomic<int> power { 1 };
    std::atomic<int> outAmp { 0 };     // UTY1 : LINE / AMP
    std::atomic<int> outMono { 0 };    // UTY1 : STEREO / MONO
    std::atomic<int> midiChannel { 1 };
    std::atomic<int> omni { 1 };
    std::atomic<int> patchSerial { 0 }; // incrémenté à chaque changement de patch

    std::atomic<int> cc[128];
    std::atomic<float> inPeak { 0.0f };
};
