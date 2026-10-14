#include "Controller.h"
#include <algorithm>

using namespace defs;

namespace
{
// Jeu de caractères du nommage (manuel p. 45)
const std::string kCharset = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!?\"#$%&'()*+,-./:;<=>@[]^_";

std::string fit (std::string s, int w)
{
    if ((int) s.size() > w) s.resize ((size_t) w);
    s.resize ((size_t) w, ' ');
    return s;
}
std::string rjust (const std::string& s, int w)
{
    if ((int) s.size() >= w) return s.substr (s.size() - (size_t) w);
    return std::string ((size_t) w - s.size(), ' ') + s;
}
std::string rjust (int v, int w) { return rjust (std::to_string (v), w); }
std::string center (const std::string& s)
{
    const int pad = std::max (0, (20 - (int) s.size()) / 2);
    return fit (std::string ((size_t) pad, ' ') + s, 20);
}
std::string two (int v) { return v < 10 ? "0" + std::to_string (v) : std::to_string (v); }
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

Controller::Controller (SharedState& s) : sh (s)
{
    rom = factoryPatches();
    user = rom;
    for (int i = 0; i < 128; ++i) pcTable[(size_t) i] = i % kNumPatches;
    loadPatch (0);
}

const TypeDef& Controller::currentType() const { return type (module, edit.m[(size_t) module].type); }

//==============================================================================
void Controller::push (bool newPatch)
{
    sh.level.store (edit.level);
    for (int m = 0; m < NumModules; ++m)
    {
        const auto& md = edit.m[(size_t) m];
        sh.type[m].store (md.type);
        sh.on[m].store (md.on ? 1 : 0);
        for (int i = 0; i < kMaxParams; ++i) sh.val[m][i].store (md.v[(size_t) i]);
    }
    sh.allBypass.store (bypassAll ? 1 : 0);
    sh.power.store (power ? 1 : 0);
    sh.outAmp.store (outAmp ? 1 : 0);
    sh.outMono.store (outMono ? 1 : 0);
    sh.midiChannel.store (midiCh);
    sh.omni.store (omni ? 1 : 0);
    if (newPatch) sh.patchSerial.fetch_add (1);
}

void Controller::loadPatch (int index)
{
    cur = (index % kNumPatches + kNumPatches) % kNumPatches;
    edit = user[(size_t) cur];
    isEdited = false;
    push (true);
}

void Controller::selectPatch (int index)
{
    if (index == cur && ! isEdited) return;
    loadPatch (index);
    if (md == Mode::Edit || md == Mode::Compare || md == Mode::Store) md = Mode::Play;
    if (onPatchSelected) onPatchSelected (cur);
}

void Controller::setModuleOn (int m, bool on)
{
    edit.m[(size_t) m].on = on;
    if (on && m == SFX) { edit.m[EFF1].on = false; edit.m[EFF2].on = false; }   // SFX exclut EFF1 / EFF2
    if (on && (m == EFF1 || m == EFF2)) edit.m[SFX].on = false;
}

void Controller::showMessage (const std::string& l1, const std::string& l2, double ms)
{
    msg1 = l1; msg2 = l2; msgUntil = nowMs() + ms;
}

//==============================================================================
void Controller::press (Key k)
{
    if (k == Key::Power)
    {
        power = ! power;
        md = Mode::Play;
        msgUntil = 0;
        push();
        return;
    }
    if (! power) return;
    msgUntil = 0;

    switch (md)
    {
        case Mode::Play:
            switch (k)
            {
                case Key::PatchDown: selectPatch (cur - 1); break;
                case Key::PatchUp:   selectPatch (cur + 1); break;
                case Key::Store:     storeDest = cur; storeReturn = Mode::Play; md = Mode::Store; break;
                case Key::Utility:   if (! bypassAll) { md = Mode::Utility; utyPage = 0; utyCursor = 0; } break;
                case Key::EditLeft:  if (! bypassAll) { md = Mode::Edit; module = REV; page = 0; } break;
                case Key::EditRight: if (! bypassAll) { md = Mode::Edit; module = COMP; page = 0; } break;
                case Key::Bypass:    bypassAll = ! bypassAll; push(); break;
                case Key::Dec:       md = Mode::Learn; lastPc = -1; break;
                case Key::Inc:       md = Mode::Monitor; lastCcNum = -1; break;
                default: break;
            }
            break;

        case Mode::Edit:
        {
            auto& md_ = edit.m[(size_t) module];
            switch (k)
            {
                case Key::EditLeft:
                    if (module == COMP) md = Mode::Play; else { --module; page = 0; }
                    break;
                case Key::EditRight:
                    if (module == REV) md = Mode::Play; else { ++module; page = 0; }
                    break;
                case Key::Utility: page = (page + 1) % (int) currentType().pages.size(); break;
                case Key::Dec:
                case Key::Inc:
                    if (page == 0 && md_.on)
                    {
                        const int n = numTypes (module);
                        const int t = (md_.type + (k == Key::Inc ? 1 : n - 1)) % n;
                        edit.setType (module, t);
                        edited(); push();
                        showMessage (kModuleNames[module], type (module, t).name, 700);
                    }
                    break;
                case Key::Bypass: setModuleOn (module, ! md_.on); edited(); push(); break;
                case Key::PatchUp:   // COMPARE
                    compareBackup = edit;
                    edit = user[(size_t) cur];
                    push();
                    edit = compareBackup;
                    md = Mode::Compare;
                    break;
                case Key::Store: storeDest = cur; storeReturn = Mode::Edit; md = Mode::Store; break;
                case Key::Exit:  md = Mode::Play; break;
                default: break;
            }
            break;
        }

        case Mode::Compare:
            if (k == Key::EditLeft || k == Key::EditRight || k == Key::Exit || k == Key::Utility || k == Key::PatchUp)
            {
                push();
                md = Mode::Edit;
            }
            break;

        case Mode::Store:
            switch (k)
            {
                case Key::PatchDown: storeDest = (storeDest + kNumPatches - 1) % kNumPatches; break;
                case Key::PatchUp:   storeDest = (storeDest + 1) % kNumPatches; break;
                case Key::Store:
                    user[(size_t) storeDest] = edit;
                    cur = storeDest;
                    isEdited = false;
                    md = Mode::Play;
                    if (onPatchSelected) onPatchSelected (cur);
                    showMessage ("STORE", "COMPLETED", 700);
                    break;
                case Key::Exit: md = storeReturn; break;
                default: break;
            }
            break;

        case Mode::Utility:
            switch (k)
            {
                case Key::Utility: utyPage = (utyPage + 1) % 5; utyCursor = 0; break;
                case Key::Exit:    md = Mode::Play; break;
                case Key::EditLeft:
                case Key::EditRight:
                {
                    const int d = k == Key::EditRight ? 1 : -1;
                    if (utyPage == 1) nameCursor = std::max (0, std::min (Patch::kNameLength - 1, nameCursor + d));
                    else if (utyPage == 0 || utyPage == 2) utyCursor = d > 0 ? 1 : 0;
                    break;
                }
                case Key::Dec:
                case Key::Inc:
                    turn (utyPage == 1 ? 1 : utyCursor, k == Key::Inc ? 1 : -1);
                    break;
                case Key::Store:
                    if (utyPage == 1)   // BS / INS / DEL
                    {
                        std::string nm = edit.name;
                        if (nameFunc == 0 && nameCursor > 0) { nm.erase ((size_t) nameCursor - 1, 1); --nameCursor; }
                        else if (nameFunc == 1) nm.insert ((size_t) nameCursor, " ");
                        else if (nameFunc == 2) nm.erase ((size_t) nameCursor, 1);
                        edit.setName (nm); edited();
                    }
                    else if (utyPage == 3)   // ROM PATCH CALL
                    {
                        edit = rom[(size_t) romSel];
                        isEdited = true;
                        push (true);
                        md = Mode::Play;
                        showMessage ("ROM PATCH CALL", "COMPLETED", 800);
                    }
                    break;
                default: break;
            }
            break;

        case Mode::Learn:
        case Mode::Monitor:
            if (k == Key::Exit) md = Mode::Play;
            else if (md == Mode::Learn && k == Key::PatchDown) selectPatch (cur - 1);
            else if (md == Mode::Learn && k == Key::PatchUp) selectPatch (cur + 1);
            break;
    }
}

void Controller::turn (int knob, int delta)
{
    if (! power || delta == 0) return;
    msgUntil = 0;

    if (md == Mode::Play && knob == 0)
    {
        edit.level = std::max (0, std::min (100, edit.level + delta));
        edited(); push();
    }
    else if (md == Mode::Edit)
    {
        auto& m = edit.m[(size_t) module];
        if (! m.on) return;
        const auto& td = currentType();
        const int pi = td.pages[(size_t) page][(size_t) knob];
        if (pi < 0) return;
        const auto& pd = td.params[(size_t) pi];
        const int v = std::max (pd.min, std::min (pd.max, m.v[(size_t) pi] + delta));
        if (v != m.v[(size_t) pi]) { m.v[(size_t) pi] = v; edited(); push(); }
    }
    else if (md == Mode::Store && knob == 0)
    {
        storeDest = ((storeDest + delta) % kNumPatches + kNumPatches) % kNumPatches;
    }
    else if (md == Mode::Utility)
    {
        const int d = delta > 0 ? 1 : -1;
        switch (utyPage)
        {
            case 0:
                if (knob == 0) outAmp = d > 0;
                if (knob == 1) outMono = d > 0;
                push();
                break;
            case 1:
                if (knob == 0) nameFunc = std::max (0, std::min (2, nameFunc + d));
                if (knob == 1)
                {
                    std::string nm = edit.name;
                    auto pos = kCharset.find (nm[(size_t) nameCursor]);
                    if (pos == std::string::npos) pos = 0;
                    const int n = (int) kCharset.size();
                    nm[(size_t) nameCursor] = kCharset[(size_t) (((int) pos + delta) % n + n) % n];
                    edit.setName (nm); edited();
                }
                break;
            case 2:
                if (knob == 0) midiCh = std::max (1, std::min (16, midiCh + delta));
                if (knob == 1) omni = d > 0;
                push();
                break;
            case 3: romSel = ((romSel + delta) % kNumPatches + kNumPatches) % kNumPatches; break;
            case 4: if (knob == 0) dim = std::max (1, std::min (4, dim + d)); break;
            default: break;
        }
    }
}

int Controller::knobRange (int knob) const
{
    if (md == Mode::Edit)
    {
        const auto& td = currentType();
        const int pi = td.pages[(size_t) page][(size_t) knob];
        if (pi >= 0) return td.params[(size_t) pi].max - td.params[(size_t) pi].min;
        return 0;
    }
    if (md == Mode::Play) return knob == 0 ? 100 : 0;
    if (md == Mode::Utility)
    {
        if (utyPage == 1 && knob == 1) return (int) kCharset.size();
        if (utyPage == 2 && knob == 0) return 16;
        if (utyPage == 3 && knob == 0) return kNumPatches;
        return knob <= 1 ? 4 : 0;
    }
    if (md == Mode::Store && knob == 0) return kNumPatches;
    return 0;
}

float Controller::knobPosition (int knob) const
{
    if (md == Mode::Play && knob == 0) knobPos[0] = (float) edit.level / 100.0f;
    else if (md == Mode::Edit)
    {
        const auto& td = currentType();
        const int pi = td.pages[(size_t) page][(size_t) knob];
        if (pi >= 0)
        {
            const auto& pd = td.params[(size_t) pi];
            if (pd.max > pd.min)
                knobPos[(size_t) knob] = (float) (edit.m[(size_t) module].v[(size_t) pi] - pd.min) / (float) (pd.max - pd.min);
        }
    }
    return knobPos[(size_t) knob];
}

//==============================================================================
void Controller::render (std::string& l1, std::string& l2, int& cursorCol, bool blink) const
{
    cursorCol = -1;
    if (! power) { l1 = l2 = std::string (20, ' '); return; }
    if (nowMs() < msgUntil) { l1 = center (msg1); l2 = center (msg2); return; }

    const char mark = isEdited ? vfd::kEdited : ' ';
    switch (md)
    {
        case Mode::Play:
            l1 = center (edit.trimmedName());
            l2 = std::string (1, mark) + " " + (outAmp ? "AMP" : "LIN") + "/" + (outMono ? "MO" : "ST")
               + " LVL " + rjust (edit.level, 3) + " " + ((bypassAll && blink) ? "BYP" : "   ");
            break;

        case Mode::Edit:
        {
            const auto& m = edit.m[(size_t) module];
            const auto& td = currentType();
            if (! m.on)
            {
                l1 = fit (" TYP", 20);
                l2 = fit (std::string (1, mark) + fit (td.code, 3) + "   MODULE OFF", 20);
                break;
            }
            const auto& pg = td.pages[(size_t) page];
            l1 = std::string (" ") + (page == 0 ? "TYP" : "typ");
            l2 = std::string (1, mark) + fit (td.code, 3);
            for (int k = 0; k < 4; ++k)
            {
                const int pi = pg[(size_t) k];
                l1 += " " + (pi >= 0 ? fit (td.params[(size_t) pi].label, 3) : std::string ("   "));
                l2 += pi >= 0 ? rjust (formatValue (td.params[(size_t) pi], m.v[(size_t) pi]), 4) : std::string ("    ");
            }
            l1 = fit (l1, 20); l2 = fit (l2, 20);
            break;
        }

        case Mode::Compare:
            l1 = center (edit.trimmedName());
            l2 = center ("COMPARE");
            break;

        case Mode::Store:
            l1 = fit (two (cur + 1) + " " + edit.trimmedName(), 20);
            l2 = fit (std::string (1, vfd::kEdited) + vfd::kArrow + " " + user[(size_t) storeDest].trimmedName(), 20);
            break;

        case Mode::Utility:
        {
            const auto arrow = [] (bool on) { return std::string (1, on ? vfd::kArrowRight : ' '); };
            switch (utyPage)
            {
                case 0:
                    l1 = fit ("UTY1 OUTPUT MODE", 20);
                    l2 = fit (arrow (utyCursor == 0) + (outAmp ? "AMP " : "LINE") + "      " + arrow (utyCursor == 1) + (outMono ? "MONO" : "STEREO"), 20);
                    break;
                case 1:
                {
                    static const char* funcs[] = { "BS ", "INS", "DEL" };
                    l1 = fit ("UTY2 PATCH NAMING", 20);
                    l2 = fit (std::string (funcs[nameFunc]) + "[" + edit.name + "]", 20);
                    cursorCol = 4 + nameCursor;
                    break;
                }
                case 2:
                    l1 = fit ("UTY3 MIDI CH SEL", 20);
                    l2 = fit (" CH" + arrow (utyCursor == 0) + rjust (midiCh, 2) + "   OMNI" + arrow (utyCursor == 1) + (omni ? "ON" : "OFF"), 20);
                    break;
                case 3:
                    l1 = fit ("UTY4 ROM PATCH CALL", 20);
                    l2 = fit (std::string ("ROM") + vfd::kArrowRight + two (romSel + 1) + " " + rom[(size_t) romSel].trimmedName(), 20);
                    break;
                default:
                    l1 = fit ("UTY5 DISPLAY DIMMER", 20);
                    l2 = fit ("LEVEL " + std::to_string (dim) + "  " + std::string ((size_t) dim * 3, vfd::kBlock), 20);
                    break;
            }
            break;
        }

        case Mode::Learn:
            l1 = fit ("PRG-CHG LEARN", 20);
            l2 = fit (std::string ("PRG") + vfd::kArrowRight + (lastPc < 0 ? "---" : rjust (lastPc, 3)) + " = PATCH " + two (cur + 1), 20);
            break;

        case Mode::Monitor:
            l1 = fit ("CTL-CHG MONITOR", 20);
            l2 = lastCcNum < 0 ? fit ("  WAITING ...", 20)
                               : fit ("CTL#" + rjust (lastCcNum, 3) + "   VAL " + rjust (lastCcVal, 3), 20);
            break;
    }
}

Controller::Led Controller::indicator (int m, bool blink) const
{
    if (! power) return Led::Off;
    const bool on = edit.m[(size_t) m].on;
    const Led base = on ? (bypassAll ? Led::Amber : Led::Red) : Led::Green;
    if ((md == Mode::Edit || md == Mode::Compare) && m == module) return blink ? base : Led::Off;
    return base;
}

std::string Controller::patchDigits (bool blink) const
{
    if (! power) return "";
    if (md == Mode::Store) return blink ? two (storeDest + 1) : "";
    return two (cur + 1);
}

//==============================================================================
void Controller::midiProgramChange (int program)
{
    program = std::max (0, std::min (127, program));
    lastPc = program;
    if (md == Mode::Learn) { pcTable[(size_t) program] = cur; return; }
    selectPatch (pcTable[(size_t) program]);
}

void Controller::midiControlChange (int number, int value)
{
    lastCcNum = number; lastCcVal = value;
    if (! power) return;
    if (number == 7) { edit.level = (value * 100 + 63) / 127; edited(); push(); }
    else if (number >= 70 && number <= 78) { setModuleOn (number - 70, value >= 64); edited(); push(); }
    else if (number == 91) { bypassAll = value >= 64; push(); }
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Controller::toXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("RETROFX9030");
    x->setAttribute ("version", 1);
    x->setAttribute ("current", cur);
    x->setAttribute ("edited", isEdited);
    x->setAttribute ("outAmp", outAmp);
    x->setAttribute ("outMono", outMono);
    x->setAttribute ("midiCh", midiCh);
    x->setAttribute ("omni", omni);
    x->setAttribute ("dimmer", dim);
    x->setAttribute ("power", power);
    x->setAttribute ("bypass", bypassAll);
    x->createNewChildElement ("EDIT")->setAttribute ("data", juce::String (edit.serialize()));
    for (int i = 0; i < kNumPatches; ++i)
    {
        auto* e = x->createNewChildElement ("PATCH");
        e->setAttribute ("index", i);
        e->setAttribute ("data", juce::String (user[(size_t) i].serialize()));
    }
    juce::StringArray pcs;
    for (int v : pcTable) pcs.add (juce::String (v));
    x->createNewChildElement ("PCTABLE")->setAttribute ("data", pcs.joinIntoString (","));
    return x;
}

void Controller::fromXml (const juce::XmlElement& x)
{
    if (! x.hasTagName ("RETROFX9030")) return;
    for (auto* e : x.getChildWithTagNameIterator ("PATCH"))
    {
        const int i = e->getIntAttribute ("index", -1);
        Patch p;
        if (i >= 0 && i < kNumPatches && p.deserialize (e->getStringAttribute ("data").toStdString()))
            user[(size_t) i] = p;
    }
    if (auto* pc = x.getChildByName ("PCTABLE"))
    {
        juce::StringArray pcs;
        pcs.addTokens (pc->getStringAttribute ("data"), ",", "");
        for (int i = 0; i < std::min (128, pcs.size()); ++i)
            pcTable[(size_t) i] = std::max (0, std::min (kNumPatches - 1, pcs[i].getIntValue()));
    }
    outAmp  = x.getBoolAttribute ("outAmp", false);
    outMono = x.getBoolAttribute ("outMono", false);
    midiCh  = std::max (1, std::min (16, x.getIntAttribute ("midiCh", 1)));
    omni    = x.getBoolAttribute ("omni", true);
    dim     = std::max (1, std::min (4, x.getIntAttribute ("dimmer", 4)));
    power   = x.getBoolAttribute ("power", true);
    bypassAll = x.getBoolAttribute ("bypass", false);

    cur = std::max (0, std::min (kNumPatches - 1, x.getIntAttribute ("current", 0)));
    edit = user[(size_t) cur];
    isEdited = false;
    if (auto* e = x.getChildByName ("EDIT"))
    {
        Patch p;
        if (p.deserialize (e->getStringAttribute ("data").toStdString()))
        {
            edit = p;
            isEdited = x.getBoolAttribute ("edited", false);
        }
    }
    md = Mode::Play;
    push (true);
}
