#include "Patch.h"
#include <algorithm>
#include <sstream>

Patch::Patch()
{
    setName ("INITIAL DATA");
    for (int i = 0; i < defs::NumModules; ++i) setType (i, 0);
}

void Patch::setName (const std::string& s)
{
    name = s.substr (0, kNameLength);
    name.resize (kNameLength, ' ');
}

std::string Patch::trimmedName() const
{
    auto e = name.find_last_not_of (' ');
    return e == std::string::npos ? std::string() : name.substr (0, e + 1);
}

void Patch::setType (int module, int t)
{
    const auto& td = defs::type (module, t);
    auto& md = m[(size_t) module];
    md.type = t;
    md.v.fill (0);
    for (size_t i = 0; i < td.params.size(); ++i) md.v[i] = td.params[i].def;
}

std::string Patch::serialize() const
{
    std::ostringstream os;
    os << name << '\t' << level;
    for (auto& md : m)
    {
        os << '\t' << md.type << ',' << (md.on ? 1 : 0);
        for (int v : md.v) os << ',' << v;
    }
    return os.str();
}

bool Patch::deserialize (const std::string& s)
{
    std::vector<std::string> fields;
    std::string cur;
    for (char c : s)
    {
        if (c == '\t') { fields.push_back (cur); cur.clear(); }
        else cur += c;
    }
    fields.push_back (cur);
    if (fields.size() != 2 + (size_t) defs::NumModules) return false;

    Patch p;
    p.setName (fields[0]);
    p.level = std::max (0, std::min (100, std::atoi (fields[1].c_str())));
    for (int i = 0; i < defs::NumModules; ++i)
    {
        std::vector<int> nums;
        std::stringstream ss (fields[(size_t) i + 2]);
        std::string item;
        while (std::getline (ss, item, ',')) nums.push_back (std::atoi (item.c_str()));
        if (nums.size() != 2 + (size_t) defs::kMaxParams) return false;
        const int t = std::max (0, std::min (nums[0], defs::numTypes (i) - 1));
        p.setType (i, t);
        auto& md = p.m[(size_t) i];
        md.on = nums[1] != 0;
        const auto& td = defs::type (i, t);
        for (size_t k = 0; k < td.params.size(); ++k)
            md.v[k] = std::max (td.params[k].min, std::min (td.params[k].max, nums[k + 2]));
    }
    *this = p;
    return true;
}
