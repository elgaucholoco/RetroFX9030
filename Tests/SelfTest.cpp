// Banc d'essai : presets, rendu de l'afficheur, stabilité numérique de chaque effet.
#include "../Source/Controller.h"
#include "../Source/Engine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace defs;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("ÉCHEC: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static void loadSingle (SharedState& sh, int module, int t, std::function<void (std::array<int, kMaxParams>&)> tweak = {})
{
    Patch p;
    p.setType (module, t);
    p.m[(size_t) module].on = true;
    if (tweak) tweak (p.m[(size_t) module].v);
    sh.level = p.level;
    for (int m = 0; m < NumModules; ++m)
    {
        sh.type[m] = p.m[(size_t) m].type;
        sh.on[m] = p.m[(size_t) m].on;
        for (int i = 0; i < kMaxParams; ++i) sh.val[m][i] = p.m[(size_t) m].v[(size_t) i];
    }
    sh.patchSerial.fetch_add (1);
}

/** 3 s de signal guitare simulé (attaques + bruit) ; renvoie le pic de sortie */
static float runSignal (Engine& e, double sr, float& rmsOut)
{
    const int block = 512, total = (int) (sr * 3);
    std::vector<float> L (block), R (block);
    float peak = 0; double sum = 0; uint32_t seed = 1;
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const double t = (pos + i) / sr;
            const double noteT = std::fmod (t, 0.5);
            const double f = 110.0 * std::pow (2.0, std::floor (t / 0.5) * 5 / 12.0);
            seed = seed * 1664525u + 1013904223u;
            L[(size_t) i] = (float) (0.3 * std::exp (-noteT * 4) * (std::sin (2 * M_PI * f * t) + 0.3 * std::sin (6 * M_PI * f * t)))
                          + ((float) seed / 4294967296.0f - 0.5f) * 0.002f;
        }
        e.process (L.data(), R.data(), block);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) return INFINITY;
            peak = std::max (peak, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i])));
            sum += L[(size_t) i] * L[(size_t) i];
        }
    }
    rmsOut = (float) std::sqrt (sum / total);
    return peak;
}

static void impulseDebug (const char* code)
{
    for (int m = 0; m < NumModules; ++m)
        for (int t = 0; t < numTypes (m); ++t)
            if (type (m, t).code == code)
            {
                SharedState sh; Engine e (sh); e.prepare (44100);
                loadSingle (sh, m, t);
                std::vector<float> L (4410), R (4410);
                for (int blk = 0; blk < 40; ++blk)
                {
                    std::fill (L.begin(), L.end(), 0.0f);
                    if (blk == 1) L[0] = 1.0f;
                    e.process (L.data(), R.data(), 4410);
                    double en = 0; float pk = 0;
                    for (int i = 0; i < 4410; ++i) { en += L[(size_t) i] * L[(size_t) i]; pk = std::max (pk, std::fabs (L[(size_t) i])); }
                    std::printf ("%s %4d ms  energie %.5f  pic %.4f\n", code, blk * 100, en, pk);
                }
            }
}

int main()
{
    if (auto* dbg = std::getenv ("RFX_IMPULSE")) { impulseDebug (dbg); return 0; }
    // 1. Presets d'usine
    auto rom = factoryPatches();
    CHECK (factoryPresetErrors() == 0, "%d erreurs dans les presets", factoryPresetErrors());
    CHECK (rom.size() == 99, "nombre de patches %zu", rom.size());

    // 2. Sérialisation aller-retour
    for (auto& p : rom)
    {
        Patch q;
        CHECK (q.deserialize (p.serialize()) && q == p, "sérialisation de %s", p.name.c_str());
    }

    // 3. Tables : chaque page, chaque cible DES valide ; afficheur toujours sur 20 caractères
    int nTypes = 0;
    for (int m = 0; m < NumModules; ++m)
        for (int t = 0; t < numTypes (m); ++t)
        {
            ++nTypes;
            const auto& td = type (m, t);
            CHECK (td.params.size() <= (size_t) kMaxParams, "%s : trop de paramètres", td.code.c_str());
            for (auto& d : td.des) CHECK (d.a != SP_NONE, "%s : DES %s sans cible", td.code.c_str(), d.label.c_str());
            for (auto& pg : td.pages)
                for (int pi : pg)
                    if (pi >= 0)
                    {
                        const auto& pd = td.params[(size_t) pi];
                        for (int v : { pd.min, pd.def, pd.max })
                            CHECK (formatValue (pd, v).size() <= 4, "%s.%s : valeur trop large '%s'", td.code.c_str(), pd.label.c_str(), formatValue (pd, v).c_str());
                    }
        }
    std::printf ("%d types d'effets\n", nTypes);

    SharedState ctlState;
    Controller c (ctlState);
    std::string l1, l2; int cursor;
    using K = Controller::Key;
    c.press (K::EditRight);
    for (int m = 0; m < NumModules; ++m)
    {
        for (int t = 0; t < numTypes (m) + 1; ++t)
        {
            if (! ctlState.on[m]) c.press (K::Bypass);
            for (int pgi = 0; pgi < 4; ++pgi)
            {
                c.render (l1, l2, cursor, true);
                CHECK (l1.size() == 20 && l2.size() == 20, "afficheur %s / %s", l1.c_str(), l2.c_str());
                c.press (K::Utility);
            }
            c.press (K::Inc);
        }
        c.press (K::EditRight);
    }
    c.render (l1, l2, cursor, true);
    std::printf ("Play : [%s] [%s]\n", l1.c_str(), l2.c_str());
    CHECK (c.mode() == Controller::Mode::Play, "retour en mode Play après REV");

    // Utility, Store, Compare
    for (int i = 0; i < 6; ++i) { c.press (i == 0 ? K::Utility : K::Utility); c.render (l1, l2, cursor, true); std::printf ("UTY  : [%s] [%s]\n", l1.c_str(), l2.c_str()); }
    c.press (K::Exit);
    c.press (K::EditLeft);
    c.render (l1, l2, cursor, true);
    std::printf ("Edit : [%s] [%s]\n", l1.c_str(), l2.c_str());
    c.press (K::Store);
    c.render (l1, l2, cursor, true);
    std::printf ("Store: [%s] [%s]\n", l1.c_str(), l2.c_str());
    c.press (K::Exit); c.press (K::Exit);

    // État XML aller-retour
    auto xml = c.toXml();
    SharedState s2; Controller c2 (s2);
    c2.fromXml (*xml);
    CHECK (c2.toXml()->isEquivalentTo (xml.get(), false), "état XML");

    // 4. Chaque type d'effet seul, paramètres par défaut puis extrêmes
    for (double sr : { 44100.0, 96000.0 })
    {
        SharedState sh0;
        Engine e0 (sh0);
        e0.prepare (sr);
        auto& sh = sh0;
        auto& e = e0;
        for (int m = 0; m < NumModules; ++m)
            for (int t = 0; t < numTypes (m); ++t)
            {
                const auto& td = type (m, t);
                for (int variant = 0; variant < 3; ++variant)
                {
                    loadSingle (sh, m, t, [&] (std::array<int, kMaxParams>& v)
                    {
                        if (variant == 0) return;
                        for (size_t i = 0; i < td.params.size(); ++i)
                            if ((int) i != td.iDES && (int) i != td.iCTL && (int) i != td.iRAT)
                                v[i] = variant == 1 ? td.params[i].max : td.params[i].min;
                    });
                    float rms = 0;
                    const float peak = runSignal (e, sr, rms);
                    CHECK (std::isfinite (peak) && peak < 8.0f, "%s %s variante %d @%.0f : pic %.2f", kModuleNames[m], td.code.c_str(), variant, sr, peak);
                    if (variant == 0 && sr == 44100.0)
                        std::printf ("  %-4s %-3s  pic %5.2f  rms %.3f\n", kModuleNames[m], td.code.c_str(), peak, rms);
                }
            }
        // Tous les presets : enchaînés sur le même moteur (comme dans le plug-in), puis moteur neuf
        for (int i = 0; i < 100; ++i)
        {
            const bool fresh = i >= 50;
            SharedState shFresh;
            Engine eFresh (shFresh);
            if (fresh) eFresh.prepare (sr);
            auto& sh = fresh ? shFresh : sh0;
            auto& e = fresh ? eFresh : e0;
            const auto& p = rom[(size_t) (i % 50)];
            sh.level = p.level;
            for (int m = 0; m < NumModules; ++m)
            {
                sh.type[m] = p.m[(size_t) m].type; sh.on[m] = p.m[(size_t) m].on;
                for (int k = 0; k < kMaxParams; ++k) sh.val[m][k] = p.m[(size_t) m].v[(size_t) k];
            }
            sh.patchSerial.fetch_add (1);
            float rms = 0;
            const float peak = runSignal (e, sr, rms);
            CHECK (std::isfinite (peak) && peak < 8.0f, "preset %s @%.0f %s : pic %.2f", p.name.c_str(), sr, fresh ? "neuf" : "enchaine", peak);
            if (peak >= 8.0f && sr == 44100.0 && fresh)
                for (int solo = 0; solo < NumModules; ++solo)
                    if (p.m[(size_t) solo].on)
                    {
                        SharedState s3; Engine e3 (s3); e3.prepare (sr);
                        s3.level = p.level;
                        for (int m = 0; m < NumModules; ++m)
                        {
                            s3.type[m] = p.m[(size_t) m].type; s3.on[m] = p.m[(size_t) m].on && m <= solo;
                            for (int k = 0; k < kMaxParams; ++k) s3.val[m][k] = p.m[(size_t) m].v[(size_t) k];
                        }
                        float r3 = 0; const float pk3 = runSignal (e3, sr, r3);
                        std::printf ("     chaîne jusqu'à %s : pic %.2f\n", kModuleNames[solo], pk3);
                    }
            CHECK (rms > 0.005f || p.trimmedName() == "BOMBER", "preset %s @%.0f : silencieux (rms %.4f)", p.name.c_str(), sr, rms);
            if (fresh && sr == 44100.0) std::printf ("  preset %-16s pic %5.2f  rms %.3f\n", p.name.c_str(), peak, rms);
        }
    }

    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ÉCHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
