#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// Piano roll preview of the current melody
class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (MelodyProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;

private:
    MelodyProcessor& processor;
};

// ---------------------------------------------------------------------------
// The box you drag onto a REAPER track to drop the melody as a MIDI item
class DragMidiBox : public juce::Component
{
public:
    explicit DragMidiBox (MelodyProcessor& p) : processor (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    MelodyProcessor& processor;
    bool dragging = false;
};

// ---------------------------------------------------------------------------
// A labelled knob or menu
struct LabelledKnob : public juce::Component
{
    LabelledKnob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text);
    void resized() override;

    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct LabelledMenu : public juce::Component
{
    LabelledMenu (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text);
    void resized() override;

    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// ---------------------------------------------------------------------------
class MelodyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MelodyEditor (MelodyProcessor&);
    ~MelodyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateInfo();
    void saveMidi();

    MelodyProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    juce::TextButton generateButton { "Generate" }, backButton { "Back" }, saveButton { "Save MIDI..." };
    juce::Label info;
    DragMidiBox dragBox;

    LabelledMenu key, scale, length, channel;
    LabelledKnob octave, range, complexity, repetition, gate, seed;
    juce::ToggleButton playToggle { "Play with transport" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAttachment;

    PianoRoll roll;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyEditor)
};
