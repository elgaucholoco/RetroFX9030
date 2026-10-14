#pragma once
#include "Controller.h"
#include "Engine.h"
#include <juce_audio_processors/juce_audio_processors.h>

class RetroFXProcessor : public juce::AudioProcessor,
                         private juce::Timer,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    RetroFXProcessor();
    ~RetroFXProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return kNumPatches; }
    int getCurrentProgram() override { return controller.currentPatch(); }
    void setCurrentProgram (int index) override { controller.selectPatch (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    SharedState shared;
    Controller controller { shared };
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float value) override;

    struct MidiEvent { int kind, a, b; };   // kind 0 = CC, 1 = program change
    juce::AbstractFifo fifo { 256 };
    std::array<MidiEvent, 256> events {};

    Engine engine { shared };
    std::atomic<int> pendingHostPatch { -1 };
    bool settingPatchParam = false;
    float lastPedal = -1.0f;
    std::vector<float> bufL, bufR;
    juce::SmoothedValue<float> inGain, outGain;
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* pedalParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroFXProcessor)
};
