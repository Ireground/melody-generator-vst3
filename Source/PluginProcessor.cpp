#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const int barsForChoice[4] = { 1, 2, 4, 8 };

    juce::StringArray keyNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    juce::StringArray scaleNames()
    {
        juce::StringArray s;
        for (int i = 0; i < Scales::count; ++i)
            s.add (Scales::name (i));
        return s;
    }

    juce::StringArray channelNames()
    {
        juce::StringArray s;
        for (int i = 1; i <= 16; ++i)
            s.add (juce::String (i));
        return s;
    }

    struct Event
    {
        int offset;
        int type;   // 0 = note off, 1 = note on
        int index;
    };
}

MelodyProcessor::MelodyProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MelodyGenerator", createLayout())
{
    onPitch.fill (-1);
    regenerateIfNeeded();
}

juce::AudioProcessorValueTreeState::ParameterLayout MelodyProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 },        "Key",        keyNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 },      "Scale",      scaleNames(), 0));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "octave", 1 },     "Octave",     1, 7, 4));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "range", 1 },      "Range",      1, 3, 1));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "complexity", 1 }, "Complexity", 0, 100, 40));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 },     "Length",
                                                        StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 1));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "repetition", 1 }, "Repetition", 0, 100, 50));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "gate", 1 },       "Gate",       10, 100, 85));
    layout.add (std::make_unique<AudioParameterInt>    (ParameterID { "seed", 1 },       "Seed",       1, 99999, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "channel", 1 },    "MIDI Channel", channelNames(), 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "play", 1 },       "Play With Transport", true));

    return layout;
}

static int rawInt (const juce::AudioProcessorValueTreeState& s, const char* id)
{
    return juce::roundToInt (s.getRawParameterValue (id)->load());
}

MelodySettings MelodyProcessor::currentSettings() const
{
    MelodySettings s;
    s.scale      = rawInt (apvts, "scale");
    s.range      = rawInt (apvts, "range");
    s.complexity = rawInt (apvts, "complexity");
    s.bars       = barsForChoice[juce::jlimit (0, 3, rawInt (apvts, "length"))];
    s.repetition = rawInt (apvts, "repetition");
    s.seed       = rawInt (apvts, "seed");
    return s;
}

int MelodyProcessor::getKey() const         { return rawInt (apvts, "key"); }
int MelodyProcessor::getOctave() const      { return rawInt (apvts, "octave"); }
int MelodyProcessor::getGatePercent() const { return rawInt (apvts, "gate"); }
int MelodyProcessor::getChannel() const     { return rawInt (apvts, "channel") + 1; }

void MelodyProcessor::setSeed (int newSeed)
{
    if (auto* p = apvts.getParameter ("seed"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) newSeed));
        p->endChangeGesture();
    }
}

void MelodyProcessor::regenerateIfNeeded()
{
    const auto s = currentSettings();
    const juce::SpinLock::ScopedLockType sl (patternLock);
    if (hasPattern && s == lastSettings)
        return;
    generator.generate (s, pattern);
    lastSettings = s;
    hasPattern = true;
}

MelodyPattern MelodyProcessor::getPattern()
{
    regenerateIfNeeded();
    const juce::SpinLock::ScopedLockType sl (patternLock);
    return pattern;
}

void MelodyProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    wasPlaying = false;
}

bool MelodyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MelodyProcessor::allNotesOff (juce::MidiBuffer& midi, int sampleOffset, int channel)
{
    for (int p = 0; p < 128; ++p)
    {
        if (active[(size_t) p])
        {
            midi.addEvent (juce::MidiMessage::noteOff (channel, p), sampleOffset);
            active[(size_t) p] = false;
        }
    }
    onPitch.fill (-1);
}

void MelodyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Audio passes through untouched; incoming MIDI passes through too.
    const int numSamples = buffer.getNumSamples();

    regenerateIfNeeded();

    const int channel = getChannel();
    if (channel != sentChannel)
    {
        allNotesOff (midi, 0, sentChannel);
        sentChannel = channel;
    }

    bool playing = false;
    double ppq = 0.0, bpm = 120.0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p; else playing = false;
            if (auto b = pos->getBpm())         bpm = *b;
        }
    }

    const bool enabled = apvts.getRawParameterValue ("play")->load() > 0.5f;

    if (! playing || ! enabled || numSamples <= 0)
    {
        if (wasPlaying)
            allNotesOff (midi, 0, sentChannel);
        wasPlaying = false;
        playPositionBeats.store (-1.0);
        return;
    }

    MelodyPattern pat;
    {
        const juce::SpinLock::ScopedLockType sl (patternLock);
        pat = pattern;
    }

    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const double blockBeats = numSamples * beatsPerSample;

    double pos0;
    if (wasPlaying && std::abs (ppq - expectedPos) < 0.05)
    {
        pos0 = expectedPos;               // continuous playback
    }
    else
    {
        pos0 = ppq;                       // started or jumped: stop hanging notes
        allNotesOff (midi, 0, sentChannel);
    }
    expectedPos = pos0 + blockBeats;
    wasPlaying = true;

    const double loopBeats = pat.totalSteps * 0.25;
    playPositionBeats.store (pos0 - std::floor (pos0 / loopBeats) * loopBeats);

    const double gate = getGatePercent() / 100.0;
    const int key = getKey(), octave = getOctave(), scale = lastSettings.scale;

    std::array<Event, MelodyPattern::maxNotes * 2> events;
    int numEvents = 0;

    auto addEvent = [&] (double beat, int type, int index)
    {
        if (numEvents >= (int) events.size()) return;
        const int o = juce::jlimit (0, numSamples - 1, (int) std::floor ((beat - pos0) / beatsPerSample));
        events[(size_t) numEvents++] = { o, type, index };
    };

    for (int i = 0; i < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        double t = n.start * 0.25;
        double occ = std::ceil ((pos0 - t) / loopBeats) * loopBeats + t;
        if (occ < pos0 + blockBeats) addEvent (occ, 1, i);

        t += std::max (n.len * 0.25 * gate, 0.03);
        occ = std::ceil ((pos0 - t) / loopBeats) * loopBeats + t;
        if (occ < pos0 + blockBeats) addEvent (occ, 0, i);
    }

    std::sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b)
    {
        return a.offset != b.offset ? a.offset < b.offset : a.type < b.type; // offs before ons
    });

    for (int e = 0; e < numEvents; ++e)
    {
        const auto& ev = events[(size_t) e];
        const auto& n = pat.notes[(size_t) ev.index];

        if (ev.type == 1)
        {
            const int p = Scales::degreeToPitch (n.degree, scale, key, octave);
            if (active[(size_t) p])
                midi.addEvent (juce::MidiMessage::noteOff (channel, p), ev.offset);
            midi.addEvent (juce::MidiMessage::noteOn (channel, p, (juce::uint8) n.vel), ev.offset);
            active[(size_t) p] = true;
            onPitch[(size_t) ev.index] = p;
        }
        else
        {
            const int p = onPitch[(size_t) ev.index];
            if (p >= 0)
            {
                midi.addEvent (juce::MidiMessage::noteOff (channel, p), ev.offset);
                active[(size_t) p] = false;
                onPitch[(size_t) ev.index] = -1;
            }
        }
    }
}

juce::File MelodyProcessor::writeMidiFile()
{
    const auto pat = getPattern();
    const auto s   = currentSettings();
    const int key = getKey(), octave = getOctave(), channel = getChannel();
    const double gate = getGatePercent() / 100.0;

    constexpr int ticksPerQuarter = 960;
    constexpr int ticksPerStep    = ticksPerQuarter / 4;

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Melody"), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);

    for (int i = 0; i < pat.numNotes; ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        const int pitch = Scales::degreeToPitch (n.degree, s.scale, key, octave);
        const double on  = n.start * ticksPerStep;
        const double off = on + std::max (30.0, n.len * ticksPerStep * gate);
        seq.addEvent (juce::MidiMessage::noteOn (channel, pitch, (juce::uint8) n.vel), on);
        seq.addEvent (juce::MidiMessage::noteOff (channel, pitch), off);
    }

    // end of track at the end of the last bar, so the item is a whole number of bars
    seq.addEvent (juce::MidiMessage::endOfTrack(), (double) pat.totalSteps * ticksPerStep);
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);
    file.addTrack (seq);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MelodyGenerator");
    dir.createDirectory();

    const juce::String name = juce::String ("Melody ") + Scales::keyName (key) + " "
                            + juce::String (Scales::name (s.scale)).removeCharacters (" ")
                            + " " + juce::String (s.bars) + "bar #" + juce::String (s.seed) + ".mid";
    auto out = dir.getChildFile (juce::File::createLegalFileName (name));
    out.deleteFile();

    juce::FileOutputStream stream (out);
    if (stream.openedOk())
    {
        file.writeTo (stream, 1);
        stream.flush();
    }
    return out;
}

void MelodyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MelodyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MelodyProcessor::createEditor()
{
    return new MelodyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodyProcessor();
}
