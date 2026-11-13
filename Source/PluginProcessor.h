#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "MelodyEngine.h"

class MelodyProcessor : public juce::AudioProcessor
{
public:
    MelodyProcessor();
    ~MelodyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- used by the editor ---
    MelodyPattern getPattern();               // up-to-date copy of the current melody
    MelodySettings currentSettings() const;
    int  getKey() const;
    int  getOctave() const;
    int  getGatePercent() const;
    int  getChannel() const;                  // 1..16
    void setSeed (int newSeed);
    juce::File writeMidiFile();               // writes the melody to a .mid file in the temp folder

    std::atomic<double> playPositionBeats { -1.0 }; // position inside the loop, -1 when stopped
    std::vector<int> seedHistory;             // for the Back button (message thread only)

private:
    void regenerateIfNeeded();
    void allNotesOff (juce::MidiBuffer& midi, int sampleOffset, int channel);

    juce::SpinLock patternLock;
    MelodyPattern pattern;
    MelodySettings lastSettings;
    bool hasPattern = false;
    MelodyGenerator generator;

    // playback state (audio thread)
    double sampleRate = 44100.0;
    bool wasPlaying = false;
    double expectedPos = 0.0;
    int sentChannel = 1;
    std::array<bool, 128> active {};
    std::array<int, MelodyPattern::maxNotes> onPitch {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyProcessor)
};
