#pragma once
#include "EffectDefs.h"
#include <array>
#include <string>
#include <vector>

struct ModuleData
{
    int type = 0;
    bool on = false;
    std::array<int, defs::kMaxParams> v {};
    bool operator== (const ModuleData& o) const { return type == o.type && on == o.on && v == o.v; }
};

struct Patch
{
    static constexpr int kNameLength = 15;

    Patch();
    std::string name;       // toujours 15 caractères (complété par des espaces)
    int level = 80;         // niveau master 0-100
    std::array<ModuleData, defs::NumModules> m;

    void setName (const std::string&);
    std::string trimmedName() const;
    void setType (int module, int t);   // charge les valeurs par défaut du type

    std::string serialize() const;
    bool deserialize (const std::string&);
    bool operator== (const Patch& o) const { return name == o.name && level == o.level && m == o.m; }
};

constexpr int kNumPatches = 99;
std::vector<Patch> factoryPatches();
int factoryPresetErrors();   // libellés inconnus rencontrés en construisant les presets (pour SelfTest)
