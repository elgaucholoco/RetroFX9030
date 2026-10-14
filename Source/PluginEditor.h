#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
/** Afficheur fluorescent 20 caractères x 2 lignes, matrice 5 x 7. */
class Vfd : public juce::Component
{
public:
    void setState (const std::string& l1, const std::string& l2, int cursor, bool cursorOn, int dimmer);
    void paint (juce::Graphics&) override;
private:
    std::string line1, line2;
    int cursorCol = -1, dim = 4;
    bool cursorVisible = false;
};

/** Afficheur LED 7 segments rouge « PATCH NO. ». */
class SevenSeg : public juce::Component
{
public:
    void setText (const std::string&);
    void paint (juce::Graphics&) override;
private:
    std::string text;
};

/** Les 9 voyants bicolores des modules. */
class Indicators : public juce::Component
{
public:
    void setState (const std::array<Controller::Led, defs::NumModules>&);
    void paint (juce::Graphics&) override;
private:
    std::array<Controller::Led, defs::NumModules> leds {};
};

class SignalLeds : public juce::Component
{
public:
    void setLevel (float);
    void paint (juce::Graphics&) override;
private:
    float level = 0;
};

class PanelButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum class Shape { Pill, Round, RockerLeft, RockerRight, Power };
    PanelButton (Shape s, bool repeat) : shape (s), autoRepeat (repeat) {}
    std::function<void()> onPress;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    Shape shape; bool autoRepeat; bool down = false;
};

/** Molette de saisie (Data Entry Control) : sans butée, agit sur le paramètre affiché. */
class DataKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onTurn;
    std::function<int()> getRange;
    void setPosition (float);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void emit (float steps);
    float pos = 0.5f, accum = 0, lastY = 0;
};

class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

void drawKnob (juce::Graphics&, juce::Point<float> centre, float radius, float pos);
}

//==============================================================================
class RetroFXEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RetroFXEditor (RetroFXProcessor&);
    ~RetroFXEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();   // relit l'état du Controller (appelé 30 fois par seconde)

private:
    static constexpr int kW = 1300, kH = 260;

    struct Panel : public juce::Component
    {
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    ui::PanelButton& addButton (ui::PanelButton::Shape, bool repeat, Controller::Key, juce::Rectangle<int>, const char* tipUtf8);

    RetroFXProcessor& proc;
    Controller& ctl;
    Panel panel;
    ui::KnobLookAndFeel knobLnf;
    ui::Vfd vfd;
    ui::SevenSeg seg;
    ui::Indicators indicators;
    ui::SignalLeds signal;
    juce::Slider inputKnob, phonesKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAtt, phonesAtt;
    ui::DataKnob dataKnobs[4];
    std::vector<std::unique_ptr<ui::PanelButton>> buttons;
    juce::TooltipWindow tooltips { this, 600 };
    float peakHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroFXEditor)
};
