#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>

EventHorizonAudioProcessor::EventHorizonAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
    startTimerHz (20);
}

EventHorizonAudioProcessor::~EventHorizonAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout EventHorizonAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;
    const juce::NormalisableRange<float> gravityRange { 0.25f, 4.0f, 0.01f, 0.5f };
    const juce::NormalisableRange<float> churnRange { 4.0f, 120.0f, 0.5f, 0.55f };
    layout.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gravityParameterId, 1 }, "Gravity",
        gravityRange, gravityRange.snapToLegalValue (gravityRange.convertFrom0to1 (0.45f))));
    layout.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { magnitudeParameterId, 1 }, "Magnitude",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f));
    layout.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixParameterId, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.65f));
    layout.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { dwellParameterId, 1 }, "Churn",
        churnRange, churnRange.snapToLegalValue (churnRange.convertFrom0to1 (0.45f))));
    layout.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { liveParameterId, 1 }, "Live", false));
    return { layout.begin(), layout.end() };
}

void EventHorizonAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    engine.prepare (newSampleRate, samplesPerBlock);
    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, true, false);
    dryBuffer.clear();
}

void EventHorizonAudioProcessor::releaseResources()
{
}

bool EventHorizonAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void EventHorizonAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto samplesToCopy = juce::jmin (buffer.getNumSamples(), dryBuffer.getNumSamples());
    dryBuffer.clear();
    if (totalInputChannels > 0)
    {
        dryBuffer.copyFrom (0, 0, buffer, 0, 0, samplesToCopy);
        dryBuffer.copyFrom (1, 0, buffer, juce::jmin (1, totalInputChannels - 1),
                            0, samplesToCopy);
        const auto live = parameters.getRawParameterValue (liveParameterId)->load() >= 0.5f;
        engine.setLiveCaptureEnabled (live);
        const auto magnitude = parameters.getRawParameterValue (magnitudeParameterId)->load();
        engine.setLiveCaptureAppetite (0.5f + 0.5f * magnitude);
        const auto dwell = parameters.getRawParameterValue (dwellParameterId)->load();
        if (const auto* dwellParameter = parameters.getParameter (dwellParameterId))
            engine.setLiveCaptureTimeScale (dwellParameter->convertTo0to1 (dwell));
        engine.captureInput (buffer.getReadPointer (0), buffer.getNumSamples());
    }

    buffer.clear();
    const auto gravity = parameters.getRawParameterValue (gravityParameterId)->load();
    const auto magnitude = parameters.getRawParameterValue (magnitudeParameterId)->load();
    const auto curvedMagnitude = 0.5f * std::pow (2.0f, magnitude);
    const auto dispersion = curvedMagnitude;
    const auto shimmer = curvedMagnitude;
    const auto field = 0.45f + 0.10f * magnitude;
    constexpr float cycle = 12.0f;
    const auto dwell = parameters.getRawParameterValue (dwellParameterId)->load();
    engine.process (buffer, gravity, dispersion, field, shimmer, cycle, dwell);
    const auto mix = parameters.getRawParameterValue (mixParameterId)->load();
    const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
    const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
    buffer.applyGain (wetGain);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.addFrom (channel, 0, dryBuffer, juce::jmin (channel, 1), 0,
                        samplesToCopy, dryGain);
}

juce::AudioProcessorEditor* EventHorizonAudioProcessor::createEditor()
{
    return new EventHorizonAudioProcessorEditor (*this);
}

void EventHorizonAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EventHorizonAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

bool EventHorizonAudioProcessor::startRecording()
{
    if (! engine.requestRecording())
        return false;

    sourceName = "Live input";
    return true;
}

juce::String EventHorizonAudioProcessor::loadAudioFile (const juce::File& file)
{
    if (! engine.canAcceptSource())
        return "Six generations and two waiting phrases are already in flight";

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return "Could not read that audio file";

    const auto maximumSourceLength = (int64) std::llround (reader->sampleRate * 4.0) + 8;
    const auto sourceLength = juce::jmin (reader->lengthInSamples, maximumSourceLength);
    if (sourceLength <= 0 || sourceLength > (int64) std::numeric_limits<int>::max())
        return "The audio file is empty";

    const auto channels = juce::jlimit (1, 8, (int) reader->numChannels);
    juce::AudioBuffer<float> decoded (channels, (int) sourceLength);
    decoded.clear();
    if (! reader->read (&decoded, 0, (int) sourceLength, 0, true, true))
        return "The audio file could not be decoded";

    if (! engine.submitLoadedAudio (decoded, reader->sampleRate))
        return "No free source slot is available yet";

    sourceName = file.getFileName();
    return {};
}

juce::String EventHorizonAudioProcessor::getStatusText() const
{
    switch (engine.getStatus())
    {
        case EventHorizonEngine::Status::empty:      return "Ready — REC or LOAD a sound";
        case EventHorizonEngine::Status::armed:      return "Recording armed…";
        case EventHorizonEngine::Status::recording:
            if (parameters.getRawParameterValue (liveParameterId)->load() >= 0.5f)
                return "LIVE — capturing phrase…";
            return "Recording " + juce::String (juce::roundToInt (engine.getRecordingProgress() * 100.0f)) + "%";
        case EventHorizonEngine::Status::analysing:  return "Analysing " + sourceName + "…";
        case EventHorizonEngine::Status::ready:
            return engine.getPlayingGenerationCount() >= EventHorizonEngine::maxGenerations
                ? "Phrase waiting at the outer boundary…"
                : "Preparing " + sourceName + "…";
        case EventHorizonEngine::Status::playing:
        {
            const auto count = engine.getPlayingGenerationCount();
            const auto text = count == 1 ? juce::String ("1 generation falling")
                                         : juce::String (count) + " generations falling";
            return parameters.getRawParameterValue (liveParameterId)->load() >= 0.5f
                ? text + " — LIVE"
                : text;
        }
    }

    return {};
}

void EventHorizonAudioProcessor::timerCallback()
{
    engine.servicePendingAnalysis();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EventHorizonAudioProcessor();
}
