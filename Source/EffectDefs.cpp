#include "EffectDefs.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace defs
{
const char* const kModuleNames[NumModules] = { "COMP", "EXT", "DIST", "EQ", "AMP", "EFF1", "EFF2", "SFX", "REV" };

const std::vector<int> kFreqEq { 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                 1000, 1250, 1600, 2000, 2500, 3150, 4000 };
const std::vector<int> kFreqComb { 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250,
                                   315, 400, 500, 630, 800, 1000, 1250, 1600, 2000 };

const std::vector<int>& ctlNumbers()
{
    static const std::vector<int> list = []
    {
        std::vector<int> v;
        for (int i = 1; i <= 31; ++i)
            if (i != 6 && i != 7) v.push_back (i);
        for (int i = 63; i <= 95; ++i)
            if ((i < 70 || i > 79) && i != 91) v.push_back (i);
        return v;
    }();
    return list;
}

int ctlIndexOf (int cc)
{
    const auto& l = ctlNumbers();
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i] == cc) return (int) i;
    return 0;
}

int TypeDef::find (const std::string& label) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].label == label) return (int) i;
    return -1;
}

static std::string freqText (int hz)
{
    char buf[8];
    if (hz < 1000) std::snprintf (buf, sizeof (buf), "%d", hz);
    else if (hz % 1000 == 0) std::snprintf (buf, sizeof (buf), "%dk", hz / 1000);
    else std::snprintf (buf, sizeof (buf), "%dk%d", hz / 1000, (hz % 1000) / 100);
    return buf;
}

std::string formatValue (const ParamDef& p, int v)
{
    char buf[16];
    switch (p.fmt)
    {
        case Fmt::Int:    std::snprintf (buf, sizeof (buf), "%d", v); return buf;
        case Fmt::Signed: std::snprintf (buf, sizeof (buf), v > 0 ? "+%d" : "%d", v); return buf;
        case Fmt::Tenth:  std::snprintf (buf, sizeof (buf), "%d.%d", v / 10, v % 10); return buf;
        case Fmt::Ms:
            if (v >= 1000) return "1S";
            std::snprintf (buf, sizeof (buf), "%d", v); return buf;
        case Fmt::Enum:   return p.enums[(size_t) std::max (0, std::min (v, (int) p.enums.size() - 1))];
        case Fmt::FreqEq:   return freqText (kFreqEq[(size_t) std::max (0, std::min (v, (int) kFreqEq.size() - 1))]);
        case Fmt::FreqComb: return freqText (kFreqComb[(size_t) std::max (0, std::min (v, (int) kFreqComb.size() - 1))]);
        case Fmt::Ratio:
            if (v > 100) return "Tap";
            std::snprintf (buf, sizeof (buf), "%d", v); return buf;
        case Fmt::Ctl:
            std::snprintf (buf, sizeof (buf), "%d", ctlNumbers()[(size_t) std::max (0, std::min (v, (int) ctlNumbers().size() - 1))]);
            return buf;
    }
    return "";
}

//==============================================================================
namespace
{
ParamDef P (const char* l, int mn, int mx, int d, Fmt f = Fmt::Int) { return { l, mn, mx, d, f, {} }; }
ParamDef E (const char* l, std::vector<std::string> e, int d = 0)  { return { l, 0, (int) e.size() - 1, d, Fmt::Enum, std::move (e) }; }

using Page  = std::array<const char*, 4>;
using DesIn = std::pair<std::string, std::vector<std::string>>;

// Paramètres communs
ParamDef LVL()   { return P ("LVL", 0, 100, 80); }
ParamDef LVL10() { return P ("LVL", 1, 10, 8); }
ParamDef SPD (int d = 5) { return P ("SPD", 1, 100, d, Fmt::Tenth); }   // 0.1 - 10 Hz
ParamDef SRC()   { return E ("SRC", { "1", "1+2", "2" }, 1); }

TypeDef T (const char* code, const char* name, Module m, bool stereo,
           std::vector<ParamDef> params, std::vector<Page> pages,
           std::vector<DesIn> des, const char* modSlot4, int ratioMax = 100)
{
    TypeDef t;
    t.code = code; t.name = name; t.module = m; t.stereo = stereo;
    t.params = std::move (params);

    std::vector<std::string> desLabels;
    for (auto& d : des) desLabels.push_back (d.first);

    t.iDES = (int) t.params.size(); t.params.push_back (E ("DES", desLabels, 0));
    t.iCTL = (int) t.params.size(); t.params.push_back ({ "CTL", 0, (int) ctlNumbers().size() - 1, ctlIndexOf (11), Fmt::Ctl, {} });
    t.iRAT = (int) t.params.size(); t.params.push_back ({ "(%)", -100, ratioMax, 0, Fmt::Ratio, {} });
    assert (t.params.size() <= (size_t) kMaxParams);

    auto idx = [&t] (const char* l) -> int
    {
        if (l == nullptr) return -1;
        int i = t.find (l);
        assert (i >= 0);
        return i;
    };

    for (auto& pg : pages)
        t.pages.push_back ({ idx (pg[0]), idx (pg[1]), idx (pg[2]), idx (pg[3]) });
    t.pages.push_back ({ t.iDES, t.iCTL, t.iRAT, idx (modSlot4) });

    for (auto& d : des)
    {
        DesTarget dt; dt.label = d.first;
        if (d.second.empty())
        {
            if (d.first == "INP") dt.a = SP_INP;
            else if (d.first == "HLD") dt.a = SP_HLD;
            else if (d.first == "MAN") dt.a = SP_MAN;
            else dt.a = idx (d.first.c_str());
        }
        else
        {
            dt.a = idx (d.second[0].c_str());
            if (d.second.size() > 1) dt.b = idx (d.second[1].c_str());
        }
        t.des.push_back (dt);
    }
    return t;
}

DesIn D (const char* label) { return { label, {} }; }
DesIn D (const char* label, std::vector<std::string> targets) { return { label, std::move (targets) }; }

// Types partagés entre EFF1 et EFF2
TypeDef pitch (const char* code, const char* name, Module m)
{
    return T (code, name, m, false,
              { P ("PIT", -12, 12, 7, Fmt::Signed), P ("FIN", -50, 50, 0, Fmt::Signed), P ("BAL", 0, 100, 50), LVL() },
              { Page { "PIT", "FIN", "BAL", "LVL" } },
              { D ("PIT"), D ("FIN"), D ("BAL"), D ("LVL") }, "LVL");
}
TypeDef phase (const char* code, const char* name, Module m)
{
    return T (code, name, m, false,
              { P ("DPT", 0, 100, 60), SPD (5), P ("PK", 0, 100, 50), LVL() },
              { Page { "DPT", "SPD", "PK", "LVL" } },
              { D ("DPT"), D ("SPD"), D ("D+S", { "DPT", "SPD" }), D ("LVL") }, "LVL");
}
TypeDef cry (const char* code, const char* name, Module m)
{
    return T (code, name, m, false,
              { P ("SNS", 1, 100, 60), E ("DIR", { "dwn", "up" }, 1), E ("TRG", { "pre", "pos" }, 0), LVL() },
              { Page { "SNS", "DIR", "TRG", "LVL" } },
              { D ("SNS"), D ("DIR"), D ("LVL") }, "LVL");
}
TypeDef tremolo (const char* code, const char* name, Module m)
{
    return T (code, name, m, false,
              { P ("DPT", 0, 100, 70), P ("SPD", 10, 200, 50, Fmt::Tenth), E ("WF", { "Tri", "STd", "STu" }), P ("EDG", 0, 100, 0), LVL() },
              { Page { "DPT", "SPD", "WF", "EDG" } },
              { D ("DPT"), D ("SPD"), D ("EDG"), D ("LVL") }, "LVL");
}
TypeDef chorus2 (const char* code, const char* name, bool stereo)
{
    return T (code, name, EFF2, stereo,
              { P ("DPT", 0, 100, 50), SPD (8), P ("MIX", 0, 100, 50), LVL() },
              { Page { "DPT", "SPD", "MIX", "LVL" } },
              { D ("DPT"), D ("SPD"), D ("MIX"), D ("LVL") }, "LVL");
}
TypeDef reverb (const char* code, const char* name, bool second)
{
    std::vector<DesIn> des = second ? std::vector<DesIn> { D ("RvT"), D ("MIX"), D ("INP"), D ("HLD") }
                                    : std::vector<DesIn> { D ("RvT"), D ("COL"), D ("MIX"), D ("INP") };
    return T (code, name, REV, true,
              { P ("RvT", 5, 100, 24, Fmt::Tenth), P ("PDI", 0, 100, 20), P ("COL", 1, 5, 3), P ("MIX", 0, 100, 35), SRC() },
              { Page { "RvT", "PDI", "COL", "MIX" } }, des, "SRC");
}

std::vector<std::vector<TypeDef>> build()
{
    std::vector<std::vector<TypeDef>> m (NumModules);

    // ---- COMP
    m[COMP].push_back (T ("CMP", "COMPRESSOR", COMP, false,
        { P ("DPT", 1, 13, 7), E ("ATK", { "slw", "fst" }, 1), E ("Bsw", { "off", "on" }), LVL10() },
        { Page { "DPT", "ATK", "Bsw", "LVL" } },
        { D ("DPT"), D ("ATK"), D ("Bsw"), D ("LVL") }, "LVL"));
    m[COMP].push_back (T ("LIM", "LIMITER", COMP, false,
        { P ("DPT", 1, 13, 6), E ("REL", { "slw", "fst" }, 1), LVL10() },
        { Page { "DPT", "REL", "LVL", nullptr } },
        { D ("DPT"), D ("REL"), D ("LVL") }, "LVL"));

    // ---- EXT
    m[EXT].push_back (T ("EXT", "EXTERNAL Fx", EXT, false,
        { LVL10() }, { Page { nullptr, nullptr, nullptr, "LVL" } }, { D ("LVL") }, "LVL"));

    // ---- DIST
    const char* distNames[] = { "DISTORTION", "OVER DRIVE", "CRUNCH" };
    const char* distCodes[] = { "DST", "OD", "CRU" };
    for (int i = 0; i < 3; ++i)
    {
        const char* g = i == 0 ? "DST" : "DRV";
        m[DIST].push_back (T (distCodes[i], distNames[i], DIST, false,
            { P (g, 1, 13, i == 0 ? 9 : 7), P ("EQ", 1, 10, 6), P ("LVL", 0, 100, 70) },
            { Page { g, "EQ", nullptr, "LVL" } },
            { D (g), D ("EQ"), D ("LVL") }, "LVL"));
    }

    // ---- EQ
    m[EQ].push_back (T ("4Gt", "4 BAND GUITAR EQ", EQ, false,
        { P ("Low", 0, 100, 50), P ("Mid", 0, 100, 50), P ("Hi", 0, 100, 50), P ("Prs", 0, 100, 50), LVL() },
        { Page { "Low", "Mid", "Hi", "Prs" } },
        { D ("Low"), D ("Mid"), D ("Hi"), D ("Prs"), D ("LVL") }, "LVL"));
    m[EQ].push_back (T ("3EQ", "3 BAND EQ", EQ, false,
        { P ("LoG", -18, 18, 0, Fmt::Signed), P ("MdF", 0, (int) kFreqEq.size() - 1, 9, Fmt::FreqEq),
          P ("MdG", -18, 18, 0, Fmt::Signed), P ("HiG", -18, 18, 0, Fmt::Signed), LVL() },
        { Page { "LoG", "MdF", "MdG", "HiG" } },
        { D ("LoG"), D ("MdF"), D ("MdG"), D ("HiG"), D ("LVL") }, "LVL"));
    m[EQ].push_back (T ("ENH", "ENHANCER", EQ, false,
        { P ("LoF", 1, 10, 5), P ("LoE", 0, 100, 30), P ("HiF", 1, 10, 5), P ("HiE", 0, 100, 40), LVL() },
        { Page { "LoF", "LoE", "HiF", "HiE" } },
        { D ("LoF"), D ("LoE"), D ("HiF"), D ("HiE"), D ("LVL") }, "LVL"));

    // ---- AMP
    m[AMP].push_back (T ("GtA", "GUITAR AMP. SIM.", AMP, false,
        { P ("COL", 1, 4, 2), E ("BOX", { "COM", "CMB", "STK" }, 1), P ("DPT", 0, 100, 80), E ("STR", { "off", "on" }), LVL() },
        { Page { "COL", "BOX", "DPT", "STR" } },
        { D ("DPT"), D ("LVL") }, "LVL"));
    m[AMP].push_back (T ("BsA", "BASS AMP. SIM.", AMP, false,
        { P ("COL", 1, 4, 2), E ("BOX", { "COM", "CMB", "STK" }, 1), P ("DPT", 0, 100, 80), E ("STR", { "off", "on" }), LVL() },
        { Page { "COL", "BOX", "DPT", "STR" } },
        { D ("DPT"), D ("LVL") }, "LVL"));

    // ---- EFF1
    m[EFF1].push_back (pitch ("PIT", "PITCH SHIFTER", EFF1));
    m[EFF1].push_back (phase ("PHA", "PHASE", EFF1));
    m[EFF1].push_back (T ("FLG", "FLANGER", EFF1, false,
        { P ("DPT", 0, 100, 50), SPD (3), P ("MAN", 0, 100, 3), P ("F.B", -98, 98, 50, Fmt::Signed), LVL() },
        { Page { "DPT", "SPD", "MAN", "F.B" } },
        { D ("DPT"), D ("SPD"), D ("F.B"), D ("S+F", { "SPD", "F.B" }), D ("LVL") }, "LVL"));
    m[EFF1].push_back (T ("CHO", "CHORUS", EFF1, false,
        { P ("DPT", 0, 100, 50), P ("MIX", 0, 100, 50), LVL() },
        { Page { "DPT", nullptr, "MIX", "LVL" } },
        { D ("DPT"), D ("MIX"), D ("LVL") }, "LVL"));
    m[EFF1].push_back (T ("AWA", "AUTO WAH", EFF1, false,
        { P ("SNS", 1, 100, 50), P ("MAN", 1, 100, 30), P ("PK", 1, 10, 6), E ("TRG", { "pre", "pos" }), LVL() },
        { Page { "SNS", "MAN", "PK", "TRG" } },
        { D ("SNS"), D ("LVL") }, "LVL"));
    m[EFF1].push_back (cry ("CRY", "CRY", EFF1));
    m[EFF1].push_back (T ("MET", "METALIC", EFF1, false,
        { P ("FRQ", 1, 100, 40), P ("MOD", 0, 100, 20), SPD (10), LVL() },
        { Page { "FRQ", "MOD", "SPD", "LVL" } },
        { D ("FRQ"), D ("MOD"), D ("SPD"), D ("LVL"), D ("M+S", { "MOD", "SPD" }) }, "LVL"));
    m[EFF1].push_back (T ("DBL", "DOUBLING", EFF1, false,
        { P ("TT1", 1, 100, 18), P ("TL1", 0, 100, 70), P ("TT2", 1, 100, 35), P ("TL2", 0, 100, 50), LVL() },
        { Page { "TT1", "TL1", "TT2", "TL2" } },
        { D ("TL1"), D ("TL2"), D ("L12", { "TL1", "TL2" }), D ("LVL") }, "LVL"));
    m[EFF1].push_back (T ("COF", "COMB FILTER", EFF1, false,
        { P ("FRQ", 0, (int) kFreqComb.size() - 1, 11, Fmt::FreqComb), P ("F.B", -98, 98, 60, Fmt::Signed),
          P ("HiD", 0, 99, 30), P ("MIX", -98, 98, 60, Fmt::Signed), LVL() },
        { Page { "FRQ", "F.B", "HiD", "MIX" } },
        { D ("FRQ"), D ("F.B"), D ("HiD"), D ("MIX"), D ("LVL") }, "LVL"));
    m[EFF1].push_back (tremolo ("TRM", "TREMOLO", EFF1));
    m[EFF1].push_back (T ("OCT", "OCTAVE", EFF1, false,
        { P ("Low", 0, 100, 70), P ("DiL", 0, 100, 80), LVL() },
        { Page { "Low", "DiL", "LVL", nullptr } },
        { D ("Low"), D ("DiL"), D ("LVL") }, "LVL"));
    m[EFF1].push_back (T ("PWA", "PEDAL WAH", EFF1, false,
        { P ("FRQ", 1, 100, 50), LVL() },
        { Page { "FRQ", "LVL", nullptr, nullptr } },
        { D ("FRQ"), D ("LVL") }, "LVL"));

    // ---- EFF2
    m[EFF2].push_back (pitch ("PT2", "PITCH 2", EFF2));
    m[EFF2].push_back (phase ("PH2", "PHASE 2", EFF2));
    m[EFF2].push_back (T ("PH3", "PHASE 3", EFF2, true,
        { P ("DPT", 0, 100, 60), SPD (5), LVL() },
        { Page { "DPT", "SPD", nullptr, "LVL" } },
        { D ("DPT"), D ("SPD"), D ("LVL") }, "LVL"));
    m[EFF2].push_back (chorus2 ("CH2", "CHORUS 2", false));
    m[EFF2].push_back (chorus2 ("CH3", "CHORUS 3", true));
    m[EFF2].push_back (T ("FL2", "FLANGER 2", EFF2, false,
        { P ("DPT", 0, 100, 50), SPD (3), P ("MAN", 1, 900, 5), P ("F.B", -98, 98, 50, Fmt::Signed), LVL() },
        { Page { "DPT", "SPD", "MAN", "F.B" } },
        { D ("DPT"), D ("SPD"), D ("F.B"), D ("S+F", { "SPD", "F.B" }), D ("LVL") }, "LVL"));
    m[EFF2].push_back (cry ("CR2", "CRY 2", EFF2));
    m[EFF2].push_back (tremolo ("TR2", "TREMOLO 2", EFF2));
    m[EFF2].push_back (T ("STP", "STEP", EFF2, false,
        { P ("DPT", 1, 100, 50), P ("FRQ", 10, 100, 40, Fmt::Tenth), E ("SPD", { "slw", "fst" }), LVL() },
        { Page { "DPT", "FRQ", "SPD", "LVL" } },
        { D ("DPT"), D ("FRQ"), D ("SPD"), D ("LVL") }, "LVL"));
    m[EFF2].push_back (T ("PAN", "AUTO PANPOT", EFF2, true,
        { P ("DPT", 0, 100, 80), SPD (10), LVL() },
        { Page { "DPT", "SPD", "LVL", nullptr } },
        { D ("DPT"), D ("SPD"), D ("LVL") }, "LVL"));
    m[EFF2].push_back (T ("DEL", "DELAY", EFF2, false,
        { P ("TM", 1, 1000, 350, Fmt::Ms), P ("F.B", 0, 98, 35), P ("HiD", 0, 99, 30), P ("BAL", 0, 100, 30), LVL() },
        { Page { "TM", "F.B", "HiD", "BAL" } },
        { D ("F.B"), D ("BAL"), D ("LVL"), D ("TM") }, "LVL", 101));
    m[EFF2].push_back (T ("ECH", "ECHO", EFF2, true,
        { P ("LT", 1, 1000, 300, Fmt::Ms), P ("FBT", 1, 1000, 450, Fmt::Ms), P ("RT", 1, 1000, 600, Fmt::Ms),
          P ("F.B", 0, 98, 40), P ("BAL", 0, 100, 30) },
        { Page { "LT", "FBT", "RT", "F.B" } },
        { D ("F.B"), D ("BAL") }, "BAL"));

    // ---- SFX
    m[SFX].push_back (T ("HPS", "HARMONIZED PITCH", SFX, false,
        { E ("KEY", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }),
          E ("INT", { "2nd", "Ma3", "Mi3", "4th", "5th", "6th", "7th" }, 1),
          E ("PIT", { "up", "dwn" }), P ("BAL", 0, 100, 50), E ("TRK", { "1", "2" }), LVL() },
        { Page { "KEY", "INT", "PIT", "BAL" }, Page { "TRK", nullptr, nullptr, "LVL" } },
        { D ("BAL"), D ("LVL") }, "LVL"));
    m[SFX].push_back (T ("AFL", "ADVANCED FLANGER", SFX, false,
        { P ("DPT", 0, 100, 50), SPD (5), P ("EGD", -98, 98, 50, Fmt::Signed), P ("EGS", -98, 98, 0, Fmt::Signed),
          P ("TDl", 0, 1000, 0, Fmt::Ms), P ("EGt", 1, 100, 30), P ("F.B", -98, 98, 60, Fmt::Signed), LVL() },
        { Page { "DPT", "SPD", "EGD", "EGS" }, Page { "TDl", "EGt", "F.B", "LVL" } },
        { D ("DPT"), D ("SPD"), D ("EGt"), D ("F.B"), D ("LVL") }, "LVL"));
    m[SFX].push_back (T ("BND", "BEND CHORUS", SFX, false,
        { P ("DPT", -98, 98, 40, Fmt::Signed), P ("BTm", 1, 100, 30), P ("BAL", 0, 100, 50), LVL() },
        { Page { "DPT", "BTm", "BAL", "LVL" } },
        { D ("DPT"), D ("BTm"), D ("BAL"), D ("LVL") }, "LVL"));
    m[SFX].push_back (T ("PIC", "PICKING MODULATOR", SFX, false,
        { P ("FRQ", 1, 100, 50), P ("DEC", 1, 100, 40), P ("2Dl", 0, 1000, 0, Fmt::Ms), P ("2Rt", 0, 100, 0), LVL() },
        { Page { "FRQ", "DEC", "2Dl", "2Rt" } },
        { D ("FRQ"), D ("DEC"), D ("2Rt"), D ("LVL") }, "LVL"));
    m[SFX].push_back (T ("SCR", "SUPER CRY", SFX, false,
        { P ("PTN", 1, 5, 1), LVL() },
        { Page { "PTN", nullptr, nullptr, "LVL" } },
        { D ("PTN"), D ("LVL") }, "LVL"));
    m[SFX].push_back (T ("BOM", "BOMBER", SFX, false,
        { E ("TRG", { "mut", "in" }, 1), P ("PTN", 1, 3, 1), P ("DEC", 1, 100, 50), P ("BAL", 0, 100, 50), LVL() },
        { Page { "TRG", "PTN", "DEC", "BAL" } },
        { D ("TRG"), D ("PTN"), D ("DEC"), D ("BAL"), D ("MAN") }, "LVL"));
    m[SFX].push_back (T ("ZIT", "ZITAR", SFX, false,
        { P ("DPT", 0, 10, 6), P ("RES", 0, 10, 5), LVL() },
        { Page { "DPT", "RES", "LVL", nullptr } },
        { D ("DPT"), D ("RES"), D ("LVL") }, "LVL"));

    // ---- REV
    m[REV].push_back (reverb ("Rv1", "REVERB 1", false));
    m[REV].push_back (reverb ("Rv2", "REVERB 2", true));
    m[REV].push_back (T ("ER1", "EARLY REFLECTION 1", REV, true,
        { P ("RmS", 1, 100, 40), P ("PDI", 0, 100, 10), P ("HiC", 0, 10, 4), P ("MIX", 0, 100, 35), SRC() },
        { Page { "RmS", "PDI", "HiC", "MIX" } },
        { D ("RmS"), D ("MIX"), D ("INP") }, "SRC"));
    m[REV].push_back (T ("ER2", "EARLY REFLECTION 2", REV, true,
        { P ("RmS", 1, 100, 40), P ("PDI", 0, 100, 10), P ("ENV", -10, 10, 0, Fmt::Signed), P ("MIX", 0, 100, 35), SRC() },
        { Page { "RmS", "PDI", "ENV", "MIX" } },
        { D ("RmS"), D ("ENV"), D ("MIX"), D ("INP") }, "SRC"));
    m[REV].push_back (T ("MTD", "MULTI TAP DELAY", REV, true,
        { P ("TM", 10, 900, 400), P ("INT", -10, 10, 0, Fmt::Signed), E ("PTN", { "L>R", "L<R", "L-R", "P-P" }),
          P ("SLO", -10, 10, 0, Fmt::Signed), P ("HiC", 0, 10, 4), P ("MIX", 0, 100, 35), SRC() },
        { Page { "TM", "INT", "PTN", "SLO" }, Page { "HiC", nullptr, nullptr, "MIX" } },
        { D ("ITV", { "INT" }), D ("PTN"), D ("SLO"), D ("MIX"), D ("INP") }, "SRC"));
    m[REV].push_back (T ("PPD", "PING PONG DELAY", REV, true,
        { P ("TM", 10, 900, 350), P ("F.B", 0, 98, 40), P ("HiD", 0, 10, 3), P ("MIX", 0, 100, 35), SRC() },
        { Page { "TM", "F.B", "HiD", "MIX" } },
        { D ("F.B"), D ("MIX"), D ("INP"), D ("TM") }, "SRC", 101));

    return m;
}

const std::vector<std::vector<TypeDef>>& all()
{
    static const auto tables = build();
    return tables;
}
} // namespace

int numTypes (int module)            { return (int) all()[(size_t) module].size(); }
const TypeDef& type (int module, int t) { return all()[(size_t) module][(size_t) t]; }
} // namespace defs
