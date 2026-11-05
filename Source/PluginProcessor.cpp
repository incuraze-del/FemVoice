#include "PluginProcessor.h"
#include "PluginEditor.h"

FemVoiceProcessor::FemVoiceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout FemVoiceProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<P> (juce::ParameterID { "pitch", 1 },   "Pitch",   R (-12.f, 12.f, 0.01f), 7.f));
    l.add (std::make_unique<P> (juce::ParameterID { "formant", 1 }, "Formant", R (-30.f, 40.f, 0.1f), 17.f));
    l.add (std::make_unique<P> (juce::ParameterID { "breath", 1 },  "Breath",  R (0.f, 1.f, 0.001f), 0.12f));
    l.add (std::make_unique<P> (juce::ParameterID { "bright", 1 },  "Bright",  R (-6.f, 9.f, 0.1f), 2.f));
    l.add (std::make_unique<P> (juce::ParameterID { "mix", 1 },     "Mix",     R (0.f, 1.f, 0.001f), 1.f));
    return l;
}

bool FemVoiceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FemVoiceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    vc.prepare (sampleRate);
    setLatencySamples (vc.getLatency());
    dryBuf.assign ((size_t) vc.getLatency() + 1, 0.f);
    dryPos = 0;
    envW = noiseLp = levelState = 0.f;
    lastBright = 1000.f;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    shelf.prepare (spec);
    shelf.reset();
    lowCut.prepare (spec);
    lowCut.reset();
    lastLowCut = -1.f;
}

void FemVoiceProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = buf.getNumChannels(), ns = buf.getNumSamples();
    if (nCh == 0) return;

    const float pitch   = apvts.getRawParameterValue ("pitch")->load();
    const float formant = apvts.getRawParameterValue ("formant")->load();
    const float breath  = apvts.getRawParameterValue ("breath")->load();
    const float bright  = apvts.getRawParameterValue ("bright")->load();
    const float mix     = apvts.getRawParameterValue ("mix")->load();

    const float ratio = std::pow (2.f, pitch / 12.f);
    const float fRatio = 1.f + formant / 100.f;

    if (std::abs (bright - lastBright) > 1e-3f)
    {
        lastBright = bright;
        *shelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sr, 5000.0, 0.7, juce::Decibels::decibelsToGain (bright));
    }

    // у женского голоса меньше грудного резонанса: срез низов растёт вместе со сдвигом высоты
    const float cutHz = juce::jlimit (60.f, 200.f, 70.f + 7.f * juce::jmax (0.f, pitch));
    if (std::abs (cutHz - lastLowCut) > 0.5f)
    {
        lastLowCut = cutHz;
        *lowCut.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, cutHz, 0.6f);
    }

    auto* l = buf.getWritePointer (0);
    auto* r = nCh > 1 ? buf.getWritePointer (1) : nullptr;
    float peak = 0.f;

    for (int i = 0; i < ns; ++i)
    {
        const float in = r ? 0.5f * (l[i] + r[i]) : l[i];

        // сухой сигнал с компенсацией задержки
        dryBuf[dryPos] = in;
        const float dry = dryBuf[(dryPos + 1) % dryBuf.size()];
        dryPos = (dryPos + 1) % dryBuf.size();

        float wet = vc.process (in, ratio, fRatio);

        // дыхание: высокочастотный шум по огибающей голоса
        const float a = std::abs (wet);
        envW += (a > envW ? 0.05f : 0.002f) * (a - envW);
        const float noise = rng.nextFloat() * 2.f - 1.f;
        noiseLp += 0.35f * (noise - noiseLp);
        wet += breath * 0.6f * envW * (noise - noiseLp);

        wet = shelf.processSample (lowCut.processSample (wet));

        const float out = mix * wet + (1.f - mix) * dry;
        l[i] = out;
        if (r) r[i] = out;
        peak = juce::jmax (peak, std::abs (out));
    }

    for (int ch = 2; ch < nCh; ++ch) buf.clear (ch, 0, ns);

    levelState = juce::jmax (peak, levelState * 0.9f);
    level.store (levelState);
    inputHz.store (vc.getInputHz());
}

juce::AudioProcessorEditor* FemVoiceProcessor::createEditor() { return new FemVoiceEditor (*this); }

void FemVoiceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void FemVoiceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FemVoiceProcessor(); }
