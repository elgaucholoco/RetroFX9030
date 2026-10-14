#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Molette Hi-Z INPUT (0-10) : 5 = 0 dB
float inputGainFor (float v)  { return v <= 0.01f ? 0.0f : juce::Decibels::decibelsToGain ((v - 5.0f) * 4.0f); }
// Molette PHONES / sortie (0-10) : 7 = 0 dB
float outputGainFor (float v) { return v <= 0.01f ? 0.0f : juce::Decibels::decibelsToGain ((v - 7.0f) * 3.0f); }
}

RetroFXProcessor::RetroFXProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    inputParam  = apvts.getRawParameterValue ("input");
    outputParam = apvts.getRawParameterValue ("output");
    pedalParam  = apvts.getRawParameterValue ("pedal");
    apvts.addParameterListener ("patch", this);

    controller.onPatchSelected = [this] (int index)
    {
        if (auto* p = apvts.getParameter ("patch"))
        {
            const juce::ScopedValueSetter<bool> svs (settingPatchParam, true);
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (index + 1)));
        }
    };
    startTimerHz (30);
}

RetroFXProcessor::~RetroFXProcessor()
{
    apvts.removeParameterListener ("patch", this);
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RetroFXProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input Gain",
                                                  NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output Volume",
                                                  NormalisableRange<float> (0.0f, 10.0f, 0.01f), 7.0f));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "patch", 1 }, "Patch", 1, kNumPatches, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pedal", 1 }, "Pedal (CC 11)",
                                                  NormalisableRange<float> (0.0f, 127.0f, 1.0f), 0.0f));
    return l;
}

bool RetroFXProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void RetroFXProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    bufL.assign ((size_t) std::max (samplesPerBlock, 32), 0.0f);
    bufR.assign (bufL.size(), 0.0f);
    inGain.reset (sampleRate, 0.03);
    outGain.reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (inputGainFor (inputParam->load()));
    outGain.setCurrentAndTargetValue (outputGainFor (outputParam->load()));
}

void RetroFXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();

    // MIDI : CC → modulation temps réel (lue directement par le moteur), le reste → thread de messages
    const bool omni = shared.omni.load() != 0;
    const int channel = shared.midiChannel.load();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! omni && msg.getChannel() != channel) continue;
        MidiEvent ev { -1, 0, 0 };
        if (msg.isController())
        {
            shared.cc[msg.getControllerNumber()].store (msg.getControllerValue());
            ev = { 0, msg.getControllerNumber(), msg.getControllerValue() };
        }
        else if (msg.isProgramChange())
            ev = { 1, msg.getProgramChangeNumber(), 0 };
        if (ev.kind >= 0)
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0) events[(size_t) scope.startIndex1] = ev;
        }
    }
    midi.clear();

    // Paramètre « Pedal » du DAW = pédale d'expression sur le CC 11
    const float pedal = pedalParam->load();
    if (pedal != lastPedal) { lastPedal = pedal; shared.cc[11].store ((int) pedal); }

    if ((int) bufL.size() < n) { bufL.resize ((size_t) n); bufR.resize ((size_t) n); }

    // Entrée mono (comme l'entrée Hi-Z de l'appareil)
    inGain.setTargetValue (inputGainFor (inputParam->load()));
    float peak = 0;
    for (int i = 0; i < n; ++i)
    {
        float x = nIn > 0 ? buffer.getSample (0, i) : 0.0f;
        if (nIn > 1) x = 0.5f * (x + buffer.getSample (1, i));
        x *= inGain.getNextValue();
        bufL[(size_t) i] = x;
        peak = std::max (peak, std::abs (x));
    }
    if (peak > shared.inPeak.load()) shared.inPeak.store (peak);

    engine.process (bufL.data(), bufR.data(), n);

    outGain.setTargetValue (outputGainFor (outputParam->load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        bufL[(size_t) i] *= g; bufR[(size_t) i] *= g;
    }
    if (nOut == 1)
        for (int i = 0; i < n; ++i) buffer.setSample (0, i, 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]));
    else
    {
        buffer.copyFrom (0, 0, bufL.data(), n);
        buffer.copyFrom (1, 0, bufR.data(), n);
        for (int ch = 2; ch < nOut; ++ch) buffer.clear (ch, 0, n);
    }
}

void RetroFXProcessor::timerCallback()
{
    const auto scope = fifo.read (fifo.getNumReady());
    auto handle = [this] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& ev = events[(size_t) (start + i)];
            if (ev.kind == 0) controller.midiControlChange (ev.a, ev.b);
            else controller.midiProgramChange (ev.a);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);

    const int hostPatch = pendingHostPatch.exchange (-1);
    if (hostPatch >= 0 && hostPatch != controller.currentPatch())
        controller.selectPatch (hostPatch);
}

void RetroFXProcessor::parameterChanged (const juce::String& id, float value)
{
    if (id == "patch" && ! settingPatchParam)
        pendingHostPatch.store (juce::jlimit (0, kNumPatches - 1, (int) std::lround (value) - 1));
}

const juce::String RetroFXProcessor::getProgramName (int index)
{
    index = juce::jlimit (0, kNumPatches - 1, index);
    return juce::String (index + 1).paddedLeft ('0', 2) + " " + juce::String (controller.patchName (index));
}

void RetroFXProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root ("RETROFX_STATE");
    if (auto params = apvts.copyState().createXml()) root.addChildElement (params.release());
    root.addChildElement (controller.toXml().release());
    copyXmlToBinary (root, dest);
}

void RetroFXProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("RETROFX_STATE")) return;
    if (auto* params = xml->getChildByName (apvts.state.getType()))
    {
        const juce::ScopedValueSetter<bool> svs (settingPatchParam, true);
        apvts.replaceState (juce::ValueTree::fromXml (*params));
    }
    if (auto* dev = xml->getChildByName ("RETROFX9030")) controller.fromXml (*dev);
}

juce::AudioProcessorEditor* RetroFXProcessor::createEditor() { return new RetroFXEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RetroFXProcessor(); }
