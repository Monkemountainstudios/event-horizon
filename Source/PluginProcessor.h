#pragma once

#include <JuceHeader.h>
#include "EventHorizonEngine.h"

class EventHorizonAudioProcessor final : public juce::AudioProcessor,
                                         private juce::Timer
{
public:
    EventHorizonAudioProcessor();
    ~EventHorizonAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool startRecording();
    juce::String loadAudioFile (const juce::File& file);
    void clearUniverse() noexcept { engine.requestClear(); }
    [[nodiscard]] bool canAcceptSource() const noexcept { return engine.canAcceptSource(); }
    [[nodiscard]] int getGenerationCount() const noexcept { return engine.getGenerationCount(); }
    [[nodiscard]] int getPlayingGenerationCount() const noexcept
    {
        return engine.getPlayingGenerationCount();
    }
    [[nodiscard]] juce::String getStatusText() const;
    [[nodiscard]] EventHorizonEngine::Snapshot getSnapshot (int index) const noexcept
    {
        return engine.getSnapshot (index);
    }
    [[nodiscard]] EventHorizonEngine::RadiationSnapshot getRadiationSnapshot (
        int index) const noexcept
    {
        return engine.getRadiationSnapshot (index);
    }

    juce::AudioProcessorValueTreeState parameters;

    static constexpr const char* gravityParameterId = "gravity";
    static constexpr const char* magnitudeParameterId = "magnitude";
    static constexpr const char* mixParameterId = "mix";
    static constexpr const char* dwellParameterId = "dwell";
    static constexpr const char* liveParameterId = "live";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;

    EventHorizonEngine engine;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioFormatManager formatManager;
    juce::String sourceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventHorizonAudioProcessor)
};
