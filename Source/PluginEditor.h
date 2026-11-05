#pragma once
#include "PluginProcessor.h"

class FemLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
};

// Центральный "шар": тянуть вверх/вниз = макро-регулятор "насколько женский" (pitch + formant вместе)
class OrbComponent : public juce::Component
{
public:
    std::function<void (float)> onMacro;
    float macro = 0.7f, level = 0.f, phase = 0.f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragStart = macro; }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onMacro) onMacro (0.7f); }

private:
    float dragStart = 0.7f;
};

class FemVoiceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FemVoiceEditor (FemVoiceProcessor&);
    ~FemVoiceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setParam (const juce::String& id, float value);

    FemVoiceProcessor& proc;
    FemLookAndFeel laf;
    OrbComponent orb;
    juce::Slider pitch, formant, breath, bright, mix;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    juce::TextButton presetButtons[4];
    juce::Label readout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FemVoiceEditor)
};
