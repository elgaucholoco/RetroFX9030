#pragma once
#include "Patch.h"
#include "Shared.h"
#include <juce_core/juce_core.h>
#include <array>
#include <functional>

// Caractères spéciaux de l'afficheur (voir Font5x7.h)
namespace vfd
{
constexpr char kArrowRight = '\x7f';   // ▶
constexpr char kArrow      = '\x80';   // →
constexpr char kEdited     = '\x81';   // E inversé : patch modifié
constexpr char kBlock      = '\x84';   // pavé plein
}

/** Logique de la façade : modes Play / Edit / Utility / Store / Compare,
    mémoire des 99 patches, rendu de l'afficheur 20 x 2 et des voyants.
    Ne s'utilise que depuis le thread de messages. */
class Controller
{
public:
    enum class Mode { Play, Edit, Utility, Store, Compare, Learn, Monitor };
    enum class Key { PatchDown, PatchUp, Store, Exit, Utility, Dec, Inc, Bypass, EditLeft, EditRight, Power };
    enum class Led { Off, Red, Green, Amber };

    explicit Controller (SharedState&);

    void press (Key);
    void turn (int knob, int delta);
    float knobPosition (int knob) const;   // 0..1, pour l'aiguille des molettes
    int knobRange (int knob) const;        // nombre de pas du paramètre sous la molette

    void render (std::string& line1, std::string& line2, int& cursorCol, bool blinkOn) const;
    Led indicator (int module, bool blinkOn) const;
    std::string patchDigits (bool blinkOn) const;

    void selectPatch (int index);
    int currentPatch() const { return cur; }
    std::string patchName (int index) const { return user[(size_t) index].trimmedName(); }
    Mode mode() const { return md; }
    bool isPowerOn() const { return power; }
    int dimmer() const { return dim; }

    void midiProgramChange (int program);
    void midiControlChange (int number, int value);

    std::function<void (int)> onPatchSelected;   // index 0-98

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);

private:
    void push (bool newPatch = false);
    void loadPatch (int index);
    void edited() { isEdited = true; }
    void setModuleOn (int module, bool on);
    void showMessage (const std::string& l1, const std::string& l2, double ms = 900);
    const defs::TypeDef& currentType() const;

    SharedState& sh;
    std::vector<Patch> user, rom;
    Patch edit, compareBackup;
    int cur = 0;
    bool isEdited = false;

    Mode md = Mode::Play, storeReturn = Mode::Play;
    int module = defs::COMP, page = 0;
    int utyPage = 0, utyCursor = 0, nameCursor = 0, nameFunc = 0, romSel = 0, storeDest = 0;

    bool bypassAll = false, power = true, outAmp = false, outMono = false, omni = true;
    int midiCh = 1, dim = 4;
    std::array<int, 128> pcTable {};
    int lastPc = -1, lastCcNum = -1, lastCcVal = 0;

    mutable std::array<float, 4> knobPos { 0.8f, 0.5f, 0.5f, 0.5f };
    std::string msg1, msg2;
    double msgUntil = 0;
};
