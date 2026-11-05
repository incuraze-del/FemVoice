#include "PluginEditor.h"

using namespace juce;

// ---------------------------------------------------------------- Look & Feel
void FemLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                       float pos, float startA, float endA, Slider& s)
{
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();

    g.setColour (Colour (0xff16131c));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);

    const float arcR = r * 0.82f, thick = r * 0.12f;
    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startA, endA, true);
    g.setColour (Colour (0xff3a3342));
    g.strokePath (track, PathStrokeType (thick));

    Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startA, startA + pos * (endA - startA), true);
    g.setGradientFill (ColourGradient (Colour (0xffc58bff), c.x - r, c.y, Colour (0xffff7aa8), c.x + r, c.y, false));
    g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (Colours::white);
    g.setFont (Font (FontOptions (r * 0.32f, Font::bold)));
    g.drawText (s.getTextFromValue (s.getValue()),
                Rectangle<float> (c.x - r, c.y - r * 0.38f, 2 * r, r * 0.4f), Justification::centred);
    g.setColour (Colour (0xffb8b0c4));
    g.setFont (Font (FontOptions (r * 0.2f)));
    g.drawText (s.getName(), Rectangle<float> (c.x - r, c.y + r * 0.08f, 2 * r, r * 0.3f), Justification::centred);
}

// ---------------------------------------------------------------- Orb
void OrbComponent::mouseDrag (const MouseEvent& e)
{
    if (onMacro) onMacro (jlimit (0.f, 1.f, dragStart - (float) e.getDistanceFromDragStartY() / 220.f));
}

void OrbComponent::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().withTrimmedBottom (28.f).reduced (4.f);
    const auto c = b.getCentre();
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float twoPi = MathConstants<float>::twoPi;

    g.setGradientFill (ColourGradient (Colour (0x00ffffff), c.x, c.y, Colour (0x55d8d8e0), c.x + R, c.y, true));
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);

    const float ringR = R * 0.72f;
    g.setColour (Colour (0xff7c5cf0));
    g.drawEllipse (c.x - ringR, c.y - ringR, 2 * ringR, 2 * ringR, R * 0.06f);

    const float coreR = R * (0.40f + 0.08f * macro + 0.05f * level);
    g.setGradientFill (ColourGradient (Colour (0xffb44cff).withAlpha (0.9f), c.x - coreR * 0.5f, c.y - coreR * 0.5f,
                                       Colour (0xff2fd6c8).withAlpha (0.8f), c.x + coreR * 0.7f, c.y + coreR * 0.7f, false));
    g.fillEllipse (c.x - coreR, c.y - coreR, 2 * coreR, 2 * coreR);

    Random rnd (7);
    for (int i = 0; i < 320; ++i)
    {
        const float a = rnd.nextFloat() * twoPi, jit = rnd.nextFloat();
        const float rr = coreR * (0.92f + 0.14f * jit + 0.10f * level * std::sin (phase * 3.f + (float) i));
        g.setColour (Colours::white.withAlpha (0.25f + 0.5f * jit));
        g.fillEllipse (c.x + rr * std::cos (a), c.y + rr * std::sin (a), 1.8f, 1.8f);
    }

    const float s = coreR * 0.5f;
    Path bolt;
    bolt.startNewSubPath (c.x + 0.10f * s, c.y - 1.0f * s);
    bolt.lineTo (c.x - 0.55f * s, c.y + 0.1f * s);
    bolt.lineTo (c.x - 0.05f * s, c.y + 0.1f * s);
    bolt.lineTo (c.x - 0.20f * s, c.y + 1.0f * s);
    bolt.lineTo (c.x + 0.55f * s, c.y - 0.2f * s);
    bolt.lineTo (c.x + 0.00f * s, c.y - 0.2f * s);
    bolt.closeSubPath();
    g.setGradientFill (ColourGradient (Colour (0xffff7aa8), c.x, c.y - s, Colour (0xffc58bff), c.x, c.y + s, false));
    g.fillPath (bolt);

    g.setColour (Colour (0xffb8b0c4));
    g.setFont (Font (FontOptions (15.f)));
    g.drawText ("FEMININITY  " + String (roundToInt (macro * 100.f)) + "%   (drag up/down)",
                getLocalBounds().removeFromBottom (24), Justification::centred);
}

// ---------------------------------------------------------------- Editor
static String noteName (float hz)
{
    if (hz < 20.f) return "--";
    const int midi = roundToInt (69.f + 12.f * std::log2 (hz / 440.f));
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (names[((midi % 12) + 12) % 12]) + String (midi / 12 - 1);
}

void FemVoiceEditor::setParam (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

FemVoiceEditor::FemVoiceEditor (FemVoiceProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (860, 600);

    auto setup = [this] (Slider& s, const String& name, const String& id, const String& suffix, int decimals)
    {
        s.setName (name);
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s.setNumDecimalPlacesToDisplay (decimals);
        s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);
        attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s));
    };
    setup (pitch,   "Pitch",   "pitch",   " st", 1);
    setup (formant, "Formant", "formant", " %",  0);
    setup (breath,  "Breath",  "breath",  "",    0);
    setup (bright,  "Air",     "bright",  " dB", 1);
    setup (mix,     "Mix",     "mix",     "",    0);
    for (auto* s : { &breath, &mix })
        s->textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };

    orb.onMacro = [this] (float m) { setParam ("pitch", m * 10.f); setParam ("formant", m * 24.f); };
    addAndMakeVisible (orb);

    struct Preset { const char* name; float pitch, formant, breath, bright; };
    static const Preset presets[4] = {
        { "Natural", 7.f, 17.f, 0.12f, 2.f },
        { "Soft",    6.f, 14.f, 0.30f, 1.f },
        { "Bright",  9.f, 24.f, 0.15f, 4.f },
        { "Subtle",  4.f, 10.f, 0.08f, 1.f } };
    for (int i = 0; i < 4; ++i)
    {
        auto& btn = presetButtons[i];
        btn.setButtonText (presets[i].name);
        btn.setColour (TextButton::buttonColourId, Colour (0xff2a2433));
        btn.setColour (TextButton::textColourOffId, Colour (0xffe6dcff));
        btn.onClick = [this, i]
        {
            const auto& pr = presets[i];
            setParam ("pitch", pr.pitch); setParam ("formant", pr.formant);
            setParam ("breath", pr.breath); setParam ("bright", pr.bright);
        };
        addAndMakeVisible (btn);
    }

    readout.setJustificationType (Justification::centredLeft);
    readout.setColour (Label::textColourId, Colour (0xffb8b0c4));
    addAndMakeVisible (readout);

    startTimerHz (30);
}

FemVoiceEditor::~FemVoiceEditor() { setLookAndFeel (nullptr); }

void FemVoiceEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0d0c11));
    g.setColour (Colour (0xff1c1a22));
    g.fillRoundedRectangle (520.f, 20.f, 330.f, 470.f, 40.f);
    g.setColour (Colour (0xff16141b));
    g.fillRect (0, 500, getWidth(), getHeight() - 500);

    g.setColour (Colour (0xffe6dcff));
    g.setFont (Font (FontOptions (22.f, Font::bold)));
    g.drawText ("Fem", 24, 14, 60, 30, Justification::centredLeft);
    g.setColour (Colour (0xffc58bff));
    g.drawText ("Voice", 62, 14, 100, 30, Justification::centredLeft);
}

void FemVoiceEditor::resized()
{
    orb.setBounds (30, 50, 480, 440);
    pitch.setBounds (545, 40, 250, 250);
    formant.setBounds (640, 290, 190, 190);
    for (int i = 0; i < 4; ++i) presetButtons[i].setBounds (24 + i * 100, 514, 92, 34);
    readout.setBounds (24, 560, 520, 28);
    breath.setBounds (560, 508, 82, 82);
    bright.setBounds (650, 508, 82, 82);
    mix.setBounds (740, 508, 82, 82);
}

void FemVoiceEditor::timerCallback()
{
    const float p = proc.apvts.getRawParameterValue ("pitch")->load();
    orb.macro = jlimit (0.f, 1.f, p / 10.f);
    orb.level = proc.level.load();
    orb.phase += 0.08f;
    orb.repaint();

    const float hz = proc.inputHz.load();
    const float outHz = hz * std::pow (2.f, p / 12.f);
    readout.setText (hz > 20.f ? "IN " + String (roundToInt (hz)) + " Hz (" + noteName (hz) + ")   ->   OUT "
                                     + String (roundToInt (outHz)) + " Hz (" + noteName (outHz) + ")"
                               : "IN --   (no voiced signal)",
                     dontSendNotification);
}
