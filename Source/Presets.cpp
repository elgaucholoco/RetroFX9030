#include "Patch.h"
#include <cstdio>
#include <initializer_list>
#include <utility>

// Patches d'usine (mémoire ROM). Les noms et réglages sont originaux :
// la liste d'usine du vrai 9030 n'est pas publiée dans le manuel.
namespace
{
using namespace defs;
int errors = 0;

struct PB
{
    Patch p;
    PB (const char* name, int level = 80) { p.setName (name); p.level = level; }

    PB& fx (int module, const char* code, std::initializer_list<std::pair<const char*, int>> values = {})
    {
        int t = -1;
        for (int i = 0; i < numTypes (module); ++i)
            if (type (module, i).code == code) t = i;
        if (t < 0) { std::fprintf (stderr, "preset %s: type %s inconnu\n", p.name.c_str(), code); ++errors; return *this; }
        p.setType (module, t);
        p.m[(size_t) module].on = true;
        const auto& td = type (module, t);
        for (auto& kv : values)
        {
            const int i = td.find (kv.first);
            if (i < 0) { std::fprintf (stderr, "preset %s: %s.%s inconnu\n", p.name.c_str(), code, kv.first); ++errors; continue; }
            int v = std::string (kv.first) == "CTL" ? ctlIndexOf (kv.second) : kv.second;
            const auto& pd = td.params[(size_t) i];
            if (v < pd.min || v > pd.max) { std::fprintf (stderr, "preset %s: %s.%s hors plage\n", p.name.c_str(), code, kv.first); ++errors; }
            p.m[(size_t) module].v[(size_t) i] = v;
        }
        return *this;
    }

    /** modulation temps réel : destination, numéro de CC, ratio */
    PB& mod (int module, const char* des, int cc, int ratio)
    {
        auto& md = p.m[(size_t) module];
        const auto& td = type (module, md.type);
        int d = -1;
        for (size_t i = 0; i < td.des.size(); ++i)
            if (td.des[i].label == des) d = (int) i;
        if (d < 0) { std::fprintf (stderr, "preset %s: DES %s inconnu\n", p.name.c_str(), des); ++errors; return *this; }
        md.v[(size_t) td.iDES] = d;
        md.v[(size_t) td.iCTL] = ctlIndexOf (cc);
        md.v[(size_t) td.iRAT] = ratio;
        return *this;
    }
};
}

int factoryPresetErrors() { return errors; }

std::vector<Patch> factoryPatches()
{
    errors = 0;
    std::vector<PB> b;
    b.push_back (PB ("CLEAN CHORUS").fx (COMP, "CMP", { { "DPT", 5 }, { "Bsw", 1 } })
                    .fx (EQ, "3EQ", { { "LoG", 2 }, { "MdG", -2 }, { "HiG", 3 } })
                    .fx (EFF2, "CH3", { { "DPT", 45 }, { "SPD", 7 }, { "MIX", 60 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 30 } }));
    b.push_back (PB ("TubeAmp Sound").fx (DIST, "CRU", { { "DRV", 7 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 1 }, { "DPT", 90 } })
                    .fx (REV, "Rv2", { { "RvT", 14 }, { "PDI", 10 }, { "COL", 2 }, { "MIX", 25 } }));
    b.push_back (PB ("HEAVY STACK").fx (COMP, "CMP", { { "DPT", 4 } })
                    .fx (DIST, "DST", { { "DST", 12 }, { "LVL", 75 } })
                    .fx (EQ, "4Gt", { { "Low", 65 }, { "Mid", 35 }, { "Hi", 60 }, { "Prs", 60 } })
                    .fx (AMP, "GtA", { { "COL", 4 }, { "BOX", 2 }, { "DPT", 100 } })
                    .fx (REV, "ER1", { { "RmS", 30 }, { "PDI", 5 }, { "MIX", 25 } }));
    b.push_back (PB ("BLUES DRIVE").fx (DIST, "OD", { { "DRV", 6 }, { "EQ", 5 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 0 }, { "DPT", 85 } })
                    .fx (REV, "Rv1", { { "RvT", 18 }, { "COL", 2 }, { "MIX", 28 } }));
    b.push_back (PB ("ARENA LEAD").fx (COMP, "CMP", { { "DPT", 6 } })
                    .fx (DIST, "DST", { { "DST", 10 }, { "EQ", 7 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 2 } })
                    .fx (EFF1, "CHO", { { "DPT", 35 }, { "MIX", 40 } })
                    .fx (EFF2, "DEL", { { "TM", 420 }, { "F.B", 35 }, { "HiD", 40 }, { "BAL", 30 } })
                    .fx (REV, "Rv1", { { "RvT", 30 }, { "MIX", 30 } }));
    b.push_back (PB ("JAZZ BOX", 66).fx (COMP, "CMP", { { "DPT", 4 }, { "ATK", 0 } })
                    .fx (EQ, "3EQ", { { "LoG", 4 }, { "MdF", 6 }, { "MdG", 2 }, { "HiG", -6 } })
                    .fx (AMP, "GtA", { { "COL", 1 }, { "BOX", 1 }, { "DPT", 60 } })
                    .fx (REV, "Rv2", { { "RvT", 16 }, { "MIX", 22 } }));
    b.push_back (PB ("FUNK WAH", 100).fx (COMP, "CMP", { { "DPT", 9 } })
                    .fx (EFF1, "AWA", { { "SNS", 70 }, { "MAN", 25 }, { "PK", 7 } })
                    .fx (EFF2, "CH2", { { "DPT", 30 }, { "SPD", 9 }, { "MIX", 30 } })
                    .fx (REV, "ER1", { { "RmS", 25 }, { "MIX", 20 } }));
    b.push_back (PB ("TALKING GUITAR").fx (DIST, "DST", { { "DST", 8 } })
                    .fx (AMP, "GtA", { { "BOX", 1 } })
                    .fx (EFF1, "CRY", { { "SNS", 70 } })
                    .fx (REV, "Rv1", { { "RvT", 20 }, { "MIX", 25 } }));
    b.push_back (PB ("SPACE ECHO").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (EFF1, "CHO", { { "DPT", 40 }, { "MIX", 35 } })
                    .fx (EFF2, "ECH", { { "LT", 375 }, { "FBT", 500 }, { "RT", 250 }, { "F.B", 55 }, { "BAL", 40 } })
                    .fx (REV, "Rv1", { { "RvT", 45 }, { "PDI", 40 }, { "COL", 2 }, { "MIX", 35 } }));
    b.push_back (PB ("JET FLANGER").fx (DIST, "OD", { { "DRV", 8 } })
                    .fx (AMP, "GtA", { { "COL", 3 }, { "BOX", 2 } })
                    .fx (EFF1, "FLG", { { "DPT", 80 }, { "SPD", 2 }, { "MAN", 2 }, { "F.B", 80 } })
                    .fx (REV, "Rv1", { { "RvT", 20 }, { "MIX", 25 } }));
    b.push_back (PB ("FAT OCTAVE").fx (COMP, "CMP", { { "DPT", 6 } })
                    .fx (AMP, "GtA", { { "COL", 1 }, { "BOX", 2 } })
                    .fx (EFF1, "OCT", { { "Low", 80 }, { "DiL", 70 } })
                    .fx (REV, "Rv2", { { "RvT", 12 }, { "MIX", 20 } }));
    b.push_back (PB ("RING METAL").fx (EFF1, "MET", { { "FRQ", 45 }, { "MOD", 15 }, { "SPD", 20 } })
                    .fx (REV, "Rv1", { { "RvT", 25 }, { "MIX", 30 } }));
    b.push_back (PB ("HARMONY 3RD").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (SFX, "HPS", { { "KEY", 9 }, { "INT", 1 }, { "BAL", 45 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 28 } }));
    b.push_back (PB ("SITAR DRONE").fx (COMP, "CMP", { { "DPT", 6 } })
                    .fx (SFX, "ZIT", { { "DPT", 7 }, { "RES", 6 } })
                    .fx (REV, "Rv2", { { "RvT", 25 }, { "COL", 4 }, { "MIX", 30 } }));
    b.push_back (PB ("BOMBER").fx (SFX, "BOM", { { "PTN", 1 }, { "DEC", 60 }, { "BAL", 50 } })
                    .fx (REV, "Rv1", { { "RvT", 30 }, { "MIX", 25 } }));
    b.push_back (PB ("SUPER CRY", 70).fx (DIST, "DST", { { "DST", 9 } })
                    .fx (SFX, "SCR", { { "PTN", 3 } })
                    .fx (REV, "Rv1", { { "RvT", 20 }, { "MIX", 25 } }));
    b.push_back (PB ("SURF TREMOLO").fx (AMP, "GtA", { { "COL", 3 }, { "BOX", 1 } })
                    .fx (EFF1, "TRM", { { "DPT", 70 }, { "SPD", 60 }, { "EDG", 20 } })
                    .fx (REV, "Rv2", { { "RvT", 30 }, { "COL", 5 }, { "MIX", 45 } }));
    b.push_back (PB ("STEREO PHASE").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (EFF2, "PH3", { { "DPT", 70 }, { "SPD", 6 } })
                    .fx (REV, "Rv1", { { "RvT", 20 }, { "MIX", 22 } }));
    b.push_back (PB ("STEP ARPEGGIO").fx (COMP, "CMP", { { "DPT", 7 } })
                    .fx (EFF2, "STP", { { "DPT", 100 }, { "FRQ", 60 } })
                    .fx (REV, "Rv1", { { "RvT", 25 }, { "MIX", 30 } }));
    b.push_back (PB ("PING PONG").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (EFF2, "CH2", { { "DPT", 35 }, { "SPD", 8 }, { "MIX", 40 } })
                    .fx (REV, "PPD", { { "TM", 330 }, { "F.B", 45 }, { "HiD", 4 }, { "MIX", 40 } }));
    b.push_back (PB ("MULTI TAP").fx (DIST, "CRU", { { "DRV", 5 } })
                    .fx (AMP, "GtA", { { "COL", 2 } })
                    .fx (REV, "MTD", { { "TM", 600 }, { "INT", 5 }, { "PTN", 3 }, { "SLO", -5 }, { "MIX", 40 } }));
    b.push_back (PB ("GATED VERB").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (REV, "ER2", { { "RmS", 70 }, { "PDI", 5 }, { "ENV", 0 }, { "MIX", 50 } }));
    b.push_back (PB ("DETUNE DOUBLE", 92).fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (EFF1, "PIT", { { "PIT", 0 }, { "FIN", 10 }, { "BAL", 45 } })
                    .fx (EFF2, "PT2", { { "PIT", 0 }, { "FIN", -10 }, { "BAL", 45 } })
                    .fx (REV, "Rv1", { { "RvT", 20 }, { "MIX", 25 } }));
    b.push_back (PB ("BASS CLEAN").fx (COMP, "CMP", { { "DPT", 7 }, { "ATK", 0 } })
                    .fx (EQ, "3EQ", { { "LoG", 4 }, { "MdF", 5 }, { "MdG", -3 }, { "HiG", 2 } })
                    .fx (AMP, "BsA", { { "COL", 3 }, { "BOX", 2 }, { "DPT", 80 } }));
    b.push_back (PB ("BASS FUZZ").fx (DIST, "DST", { { "DST", 10 }, { "EQ", 4 } })
                    .fx (AMP, "BsA", { { "COL", 4 }, { "BOX", 2 } })
                    .fx (EFF1, "OCT", { { "Low", 50 }, { "DiL", 80 } }));
    b.push_back (PB ("PEDAL WAH", 70).fx (DIST, "DST", { { "DST", 8 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 2 } })
                    .fx (EFF1, "PWA", { { "FRQ", 1 } }).mod (EFF1, "FRQ", 11, 100)
                    .fx (REV, "Rv1", { { "RvT", 18 }, { "MIX", 22 } }));
    b.push_back (PB ("AUTO PAN").fx (EFF1, "CHO", { { "DPT", 40 }, { "MIX", 40 } })
                    .fx (EFF2, "PAN", { { "DPT", 90 }, { "SPD", 15 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 25 } }));
    b.push_back (PB ("ACOUSTIC SIM").fx (COMP, "CMP", { { "DPT", 8 } })
                    .fx (EQ, "ENH", { { "LoF", 4 }, { "LoE", 30 }, { "HiF", 6 }, { "HiE", 60 } })
                    .fx (EFF2, "CH3", { { "DPT", 20 }, { "SPD", 5 }, { "MIX", 25 } })
                    .fx (REV, "Rv1", { { "RvT", 18 }, { "MIX", 30 } }));
    b.push_back (PB ("BEND CHORUS").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (SFX, "BND", { { "DPT", 50 }, { "BTm", 25 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 25 } }));
    b.push_back (PB ("PICKING MOD").fx (SFX, "PIC", { { "FRQ", 55 }, { "DEC", 45 }, { "2Dl", 150 }, { "2Rt", 40 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 25 } }));
    b.push_back (PB ("ADVANCED FLANGE").fx (SFX, "AFL", { { "DPT", 40 }, { "SPD", 3 }, { "EGD", 60 }, { "EGS", 40 }, { "EGt", 40 }, { "F.B", 70 } })
                    .fx (REV, "Rv1", { { "RvT", 22 }, { "MIX", 25 } }));
    b.push_back (PB ("COMB PHASE").fx (EFF1, "COF", { { "FRQ", 12 }, { "F.B", 70 }, { "MIX", 70 } })
                    .fx (REV, "Rv2", { { "RvT", 18 }, { "MIX", 25 } }));
    b.push_back (PB ("LIMITED CLEAN").fx (COMP, "LIM", { { "DPT", 8 } })
                    .fx (EQ, "ENH", { { "HiE", 50 } })
                    .fx (REV, "ER1", { { "RmS", 35 }, { "MIX", 25 } }));
    b.push_back (PB ("SLAPBACK", 95).fx (DIST, "CRU", { { "DRV", 6 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 1 } })
                    .fx (EFF2, "DEL", { { "TM", 110 }, { "F.B", 10 }, { "HiD", 50 }, { "BAL", 35 } }));
    b.push_back (PB ("SHIMMER OCT").fx (COMP, "CMP", { { "DPT", 6 } })
                    .fx (EFF1, "PIT", { { "PIT", 12 }, { "BAL", 30 } })
                    .fx (REV, "Rv1", { { "RvT", 70 }, { "PDI", 50 }, { "COL", 4 }, { "MIX", 50 } }));
    b.push_back (PB ("DROP TUNE").fx (DIST, "DST", { { "DST", 10 } })
                    .fx (AMP, "GtA", { { "COL", 4 }, { "BOX", 2 } })
                    .fx (EFF1, "PIT", { { "PIT", -2 }, { "BAL", 100 } }));
    b.push_back (PB ("INFINITE VERB", 60).fx (COMP, "CMP", { { "DPT", 4 } })
                    .fx (REV, "Rv2", { { "RvT", 80 }, { "COL", 5 }, { "MIX", 60 } }).mod (REV, "HLD", 11, 100));
    b.push_back (PB ("LEAD DELAY").fx (DIST, "OD", { { "DRV", 9 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 2 } })
                    .fx (EFF2, "DEL", { { "TM", 480 }, { "F.B", 40 }, { "HiD", 45 }, { "BAL", 30 } })
                    .fx (REV, "Rv1", { { "RvT", 25 }, { "MIX", 25 } }));
    b.push_back (PB ("PHASE 90", 95).fx (DIST, "OD", { { "DRV", 5 } })
                    .fx (AMP, "GtA", { { "COL", 2 } })
                    .fx (EFF1, "PHA", { { "DPT", 50 }, { "SPD", 5 }, { "PK", 40 } }));
    b.push_back (PB ("THICK CHORUS").fx (COMP, "CMP", { { "DPT", 5 } })
                    .fx (EFF2, "CH2", { { "DPT", 60 }, { "SPD", 5 }, { "MIX", 55 } })
                    .fx (REV, "Rv1", { { "RvT", 25 }, { "MIX", 28 } }));
    b.push_back (PB ("CRUNCH RHYTHM").fx (COMP, "CMP", { { "DPT", 3 } })
                    .fx (DIST, "CRU", { { "DRV", 8 } })
                    .fx (EQ, "4Gt", { { "Low", 55 }, { "Mid", 60 }, { "Hi", 55 }, { "Prs", 55 } })
                    .fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 1 } })
                    .fx (REV, "ER1", { { "RmS", 30 }, { "MIX", 20 } }));
    b.push_back (PB ("ULTRA SUPER CH").fx (EFF1, "CHO", { { "DPT", 70 }, { "MIX", 60 } })
                    .fx (EFF2, "CH3", { { "DPT", 50 }, { "SPD", 9 }, { "MIX", 50 } })
                    .fx (REV, "Rv1", { { "RvT", 28 }, { "MIX", 30 } }));
    b.push_back (PB ("TAPE ECHO", 100).fx (DIST, "CRU", { { "DRV", 4 } })
                    .fx (EFF2, "DEL", { { "TM", 380 }, { "F.B", 60 }, { "HiD", 75 }, { "BAL", 40 } })
                    .fx (REV, "Rv2", { { "RvT", 12 }, { "MIX", 20 } }));
    b.push_back (PB ("DIVE BOMB").fx (DIST, "DST", { { "DST", 10 } })
                    .fx (AMP, "GtA", { { "COL", 4 }, { "BOX", 2 } })
                    .fx (EFF1, "PIT", { { "PIT", 0 }, { "BAL", 100 } }).mod (EFF1, "PIT", 11, -100));
    b.push_back (PB ("ROOM AMBIENCE").fx (REV, "ER1", { { "RmS", 50 }, { "PDI", 8 }, { "HiC", 3 }, { "MIX", 40 } }));
    b.push_back (PB ("CONCERT HALL").fx (REV, "Rv1", { { "RvT", 35 }, { "PDI", 30 }, { "COL", 3 }, { "MIX", 40 } }));
    b.push_back (PB ("LO-FI RADIO", 100).fx (DIST, "CRU", { { "DRV", 4 } })
                    .fx (EQ, "3EQ", { { "LoG", -18 }, { "MdF", 11 }, { "MdG", 12 }, { "HiG", -18 } })
                    .fx (AMP, "GtA", { { "COL", 1 }, { "BOX", 0 } }));
    b.push_back (PB ("SYNTH LEAD").fx (DIST, "DST", { { "DST", 11 } })
                    .fx (EFF1, "OCT", { { "Low", 60 }, { "DiL", 60 } })
                    .fx (EFF2, "PH3", { { "DPT", 50 }, { "SPD", 3 } })
                    .fx (REV, "Rv1", { { "RvT", 25 }, { "MIX", 25 } }));
    b.push_back (PB ("ROTARY SPEAKER", 90).fx (AMP, "GtA", { { "COL", 2 }, { "BOX", 1 } })
                    .fx (EFF1, "TRM", { { "DPT", 40 }, { "SPD", 60 } })
                    .fx (EFF2, "CH3", { { "DPT", 60 }, { "SPD", 60 }, { "MIX", 50 } }));
    b.push_back (PB ("DIRECT SOUND"));

    std::vector<Patch> out;
    for (auto& x : b) out.push_back (x.p);
    while (out.size() < (size_t) kNumPatches) out.push_back (Patch());
    out.resize ((size_t) kNumPatches);
    return out;
}
