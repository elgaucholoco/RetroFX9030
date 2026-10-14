// Rend l'interface dans des PNG (vérification visuelle sans DAW).
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    RetroFXProcessor proc;
    proc.prepareToPlay (44100, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1300, 260);

    using K = Controller::Key;
    auto shot = [&] (const char* name)
    {
        if (auto* r = dynamic_cast<RetroFXEditor*> (ed.get())) r->refresh();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os (outDir.getChildFile (name));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    auto& c = proc.controller;
    proc.shared.inPeak = 0.5f;
    shot ("1_play.png");
    c.press (K::EditLeft); shot ("2_edit_rev.png");
    c.press (K::Utility); shot ("3_edit_rev_p2.png");
    c.press (K::Exit); c.press (K::PatchUp); c.press (K::PatchUp);
    c.press (K::EditRight); c.press (K::EditRight); c.press (K::EditRight); c.press (K::EditRight); c.press (K::EditRight); shot ("4_edit_amp.png");
    c.press (K::Exit); c.press (K::Utility); c.press (K::Utility); shot ("5_naming.png");
    c.press (K::Exit); c.press (K::Bypass); shot ("6_bypass.png");
    return 0;
}
