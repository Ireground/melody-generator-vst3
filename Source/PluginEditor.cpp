#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff16161c };
    const juce::Colour panel      { 0xff202028 };
    const juce::Colour rollBg     { 0xff1a1a21 };
    const juce::Colour text       { 0xffd8d8e0 };
    const juce::Colour dim        { 0xff8a8a96 };
    const juce::Colour accent     { 0xffff8c33 };
    const juce::Colour button     { 0xff3f66bf };
}

// ===========================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    const auto pat = processor.getPattern();
    const auto s = processor.currentSettings();
    const int key = processor.getKey(), octave = processor.getOctave();
    const double gate = processor.getGatePercent() / 100.0;

    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::rollBg);
    g.fillRoundedRectangle (r, 6.0f);

    int pmin = 127, pmax = 0;
    for (int i = 0; i < pat.numNotes; ++i)
    {
        const int p = Scales::degreeToPitch (pat.notes[(size_t) i].degree, s.scale, key, octave);
        pmin = std::min (pmin, p);
        pmax = std::max (pmax, p);
    }
    if (pat.numNotes == 0) { pmin = 60; pmax = 72; }
    pmin = std::max (0, pmin - 2);
    pmax = std::min (127, pmax + 2);

    const int rows = pmax - pmin + 1;
    const float rowH  = r.getHeight() / (float) rows;
    const float stepW = r.getWidth() / (float) pat.totalSteps;

    // rows: root notes highlighted, black keys darker
    for (int p = pmin; p <= pmax; ++p)
    {
        const float y = r.getY() + (float) (pmax - p) * rowH;
        const int pc = p % 12;
        if ((pc - key + 12) % 12 == 0)
        {
            g.setColour (juce::Colour (0xff2a2a3e));
            g.fillRect (r.getX(), y, r.getWidth(), rowH);
        }
        else if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)
        {
            g.setColour (juce::Colour (0xff141419));
            g.fillRect (r.getX(), y, r.getWidth(), rowH);
        }
    }

    // grid lines
    for (int st = 0; st <= pat.totalSteps; ++st)
    {
        const float x = r.getX() + (float) st * stepW;
        if (st % 16 == 0)      g.setColour (juce::Colour (0xff8c8ca6));
        else if (st % 4 == 0)  g.setColour (juce::Colour (0xff4a4a5a));
        else if (stepW >= 6.f) g.setColour (juce::Colour (0xff2c2c36));
        else continue;
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }

    // notes
    for (int i = 0; i < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        const int p = Scales::degreeToPitch (n.degree, s.scale, key, octave);
        const float x = r.getX() + (float) n.start * stepW;
        const float y = r.getY() + (float) (pmax - p) * rowH;
        const float w = std::max (2.0f, (float) (n.len * stepW * gate) - 1.0f);
        g.setColour (Colours::accent.withAlpha (0.45f + 0.55f * (float) n.vel / 127.0f));
        g.fillRoundedRectangle (x + 1.0f, y + 1.0f, w, std::max (2.0f, rowH - 1.0f), 2.0f);
    }

    // playhead
    const double pos = processor.playPositionBeats.load();
    if (pos >= 0.0)
    {
        const float x = r.getX() + (float) (pos * 4.0) * stepW;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }
}

// ===========================================================================
void DragMidiBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = isMouseOver() || dragging;

    g.setColour (hot ? Colours::accent.withAlpha (0.25f) : Colours::panel);
    g.fillRoundedRectangle (r, 6.0f);

    const float dash[] = { 5.0f, 4.0f };
    juce::Path border;
    border.addRoundedRectangle (r, 6.0f);
    juce::PathStrokeType stroke (1.5f);
    juce::Path dashed;
    stroke.createDashedStroke (dashed, border, dash, 2);
    g.setColour (Colours::accent);
    g.fillPath (dashed);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("\xe2\x87\xa9  Drag MIDI"), getLocalBounds(), juce::Justification::centred);
}

void DragMidiBox::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;

    dragging = true;
    repaint();

    const auto file = processor.writeMidiFile();
    if (! file.existsAsFile())
    {
        dragging = false;
        repaint();
        return;
    }

    juce::Component::SafePointer<DragMidiBox> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray { file.getFullPathName() }, false, this,
        [safe]
        {
            if (safe != nullptr)
            {
                safe->dragging = false;
                safe->repaint();
            }
        });
}

// ===========================================================================
LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

void LabelledKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

LabelledMenu::LabelledMenu (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);

    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramId, box);
}

void LabelledMenu::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    box.setBounds (r.removeFromTop (26));
}

// ===========================================================================
MelodyEditor::MelodyEditor (MelodyProcessor& p)
    : AudioProcessorEditor (&p), processor (p), dragBox (p),
      key        (p.apvts, "key",        "Key"),
      scale      (p.apvts, "scale",      "Scale"),
      length     (p.apvts, "length",     "Length"),
      channel    (p.apvts, "channel",    "MIDI Channel"),
      octave     (p.apvts, "octave",     "Octave"),
      range      (p.apvts, "range",      "Range (oct)"),
      complexity (p.apvts, "complexity", "Complexity %"),
      repetition (p.apvts, "repetition", "Repetition %"),
      gate       (p.apvts, "gate",       "Note Length %"),
      seed       (p.apvts, "seed",       "Melody #"),
      roll (p)
{
    lnf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    lnf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lnf.setColour (juce::Slider::thumbColourId, Colours::accent);
    lnf.setColour (juce::TextButton::buttonColourId, Colours::button);
    lnf.setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    lnf.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3a46));
    lnf.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setLookAndFeel (&lnf);

    seed.slider.setSliderStyle (juce::Slider::IncDecButtons);
    seed.slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 26);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &generateButton, &backButton, &saveButton, &info, &dragBox,
             &key, &scale, &length, &channel,
             &octave, &range, &complexity, &repetition, &gate, &seed,
             &playToggle, &roll })
        addAndMakeVisible (c);

    playAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "play", playToggle);

    info.setColour (juce::Label::textColourId, Colours::text);
    info.setFont (juce::FontOptions (15.0f));

    generateButton.onClick = [this]
    {
        auto& hist = processor.seedHistory;
        hist.push_back (processor.currentSettings().seed);
        if (hist.size() > 64)
            hist.erase (hist.begin());
        processor.setSeed (juce::Random::getSystemRandom().nextInt ({ 1, 100000 }));
    };

    backButton.onClick = [this]
    {
        auto& hist = processor.seedHistory;
        if (! hist.empty())
        {
            processor.setSeed (hist.back());
            hist.pop_back();
        }
    };

    saveButton.onClick = [this] { saveMidi(); };

    setResizable (true, true);
    setResizeLimits (640, 420, 1600, 1000);
    setSize (processor.editorW.load(), processor.editorH.load());   // remembered size
    sizeReady = true;
    startTimerHz (30);
}

MelodyEditor::~MelodyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MelodyEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void MelodyEditor::resized()
{
    if (sizeReady) { processor.editorW = getWidth(); processor.editorH = getHeight(); }
    auto r = getLocalBounds().reduced (12);

    auto top = r.removeFromTop (40);
    generateButton.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    backButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (12);
    dragBox.setBounds (top.removeFromRight (150));
    top.removeFromRight (8);
    saveButton.setBounds (top.removeFromRight (100).reduced (0, 4));
    top.removeFromRight (8);
    info.setBounds (top);

    r.removeFromTop (10);
    auto menus = r.removeFromTop (46);
    const int mw = (menus.getWidth() - 4 * 10) / 5;
    key.setBounds (menus.removeFromLeft (mw / 2 + 20));     menus.removeFromLeft (10);
    scale.setBounds (menus.removeFromLeft (mw + 30));       menus.removeFromLeft (10);
    length.setBounds (menus.removeFromLeft (mw));           menus.removeFromLeft (10);
    channel.setBounds (menus.removeFromLeft (mw / 2 + 30)); menus.removeFromLeft (10);
    playToggle.setBounds (menus.withTrimmedTop (18).withHeight (26));

    r.removeFromTop (8);
    auto knobs = r.removeFromTop (100);
    const int kw = knobs.getWidth() / 6;
    for (auto* k : { &octave, &range, &complexity, &repetition, &gate })
        k->setBounds (knobs.removeFromLeft (kw).reduced (4, 0));
    seed.setBounds (knobs.reduced (4, 0).withTrimmedBottom (40));

    r.removeFromTop (10);
    roll.setBounds (r);
}

void MelodyEditor::updateInfo()
{
    const auto s = processor.currentSettings();
    const auto pat = processor.getPattern();
    const juce::String text = juce::String (Scales::keyName (processor.getKey())) + " " + Scales::name (s.scale)
                            + "   |   " + juce::String (s.bars) + (s.bars > 1 ? " bars" : " bar")
                            + "   |   Melody #" + juce::String (s.seed)
                            + "   |   " + juce::String (pat.numNotes) + " notes";
    if (text != lastInfo)
    {
        lastInfo = text;
        info.setText (text, juce::dontSendNotification);
    }
}

void MelodyEditor::timerCallback()
{
    updateInfo();
    roll.repaint();
}

void MelodyEditor::saveMidi()
{
    const auto temp = processor.writeMidiFile();
    chooser = std::make_unique<juce::FileChooser> (
        "Save melody as MIDI",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (temp.getFileName()),
        "*.mid");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [temp] (const juce::FileChooser& fc)
                          {
                              auto dest = fc.getResult();
                              if (dest != juce::File())
                                  temp.copyFileTo (dest.withFileExtension ("mid"));
                          });
}
