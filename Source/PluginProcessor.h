#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "VoiceChanger.h"

class FemVoiceProcessor : public juce::AudioProcessor
{
public:
    FemVoiceProcessor();
    ~FemVoiceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FemVoice"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inputHz { 0.f }, level { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    VoiceChanger vc;
    juce::dsp::IIR::Filter<float> shelf, lowCut;
    float lastLowCut = -1.f;
    std::vector<float> dryBuf;
    size_t dryPos = 0;
    float lastBright = 1000.f, envW = 0.f, noiseLp = 0.f, levelState = 0.f;
    double sr = 44100.0;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FemVoiceProcessor)
};
