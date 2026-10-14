#pragma once
#include <array>
#include <string>
#include <vector>

// Tables des modules, types d'effets et paramètres, d'après le manuel du 9030
// (section IV « Effect Types and Parameters »).
namespace defs
{
enum Module { COMP, EXT, DIST, EQ, AMP, EFF1, EFF2, SFX, REV, NumModules };
extern const char* const kModuleNames[NumModules];

constexpr int kMaxParams = 16;

enum class Fmt
{
    Int,      // 0..100
    Signed,   // -18..+18 avec signe
    Tenth,    // valeur/10 : "2.4"
    Ms,       // millisecondes, "1S" à 1000
    Enum,     // libellés
    FreqEq,   // index dans kFreqEq
    FreqComb, // index dans kFreqComb
    Ratio,    // -100..100, 101 = "Tap"
    Ctl       // index dans la liste des numéros de CC autorisés
};

struct ParamDef
{
    std::string label;
    int min, max, def;
    Fmt fmt;
    std::vector<std::string> enums;
};

// Cibles spéciales de la modulation temps réel (DES)
enum Special { SP_NONE = -1, SP_INP = -10, SP_HLD = -11, SP_MAN = -12 };

struct DesTarget
{
    std::string label;
    int a = SP_NONE, b = SP_NONE;
};

struct TypeDef
{
    std::string code;   // 3 caractères affichés sous "TYP"
    std::string name;   // nom complet
    Module module;
    bool stereo = false;
    std::vector<ParamDef> params;
    std::vector<std::array<int, 4>> pages; // index de paramètre par molette (-1 = vide)
    std::vector<DesTarget> des;
    int iDES = -1, iCTL = -1, iRAT = -1;

    int find (const std::string& label) const;
};

int numTypes (int module);
const TypeDef& type (int module, int t);

extern const std::vector<int> kFreqEq;
extern const std::vector<int> kFreqComb;
const std::vector<int>& ctlNumbers();   // 1-31, 63-95 sauf 6, 7, 70-79, 91
int ctlIndexOf (int ccNumber);

std::string formatValue (const ParamDef&, int v);   // 4 caractères max
}
