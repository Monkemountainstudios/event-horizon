#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class EventHorizonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EventHorizonLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosition, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isHighlighted, bool isDown) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

private:
    juce::Image knobImage;
};

class HorizonDisplay final : public juce::Component
{
public:
    explicit HorizonDisplay (EventHorizonAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
    }

    void paint (juce::Graphics& graphics) override;

private:
    EventHorizonAudioProcessor& processor;
    float displayedPlasmaActivity = 0.0f;
};

class EventHorizonAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit EventHorizonAudioProcessorEditor (EventHorizonAudioProcessor&);
    ~EventHorizonAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseAudioFile();

    EventHorizonAudioProcessor& processor;
    HorizonDisplay horizonDisplay;
    EventHorizonLookAndFeel lookAndFeel;
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton loadButton { "MANUAL\nINSERT" };
    juce::TextButton clearButton { "CLEAR" };
    juce::TextButton liveButton { "LIVE\nCAPTURE" };
    juce::Slider gravitySlider;
    juce::Slider magnitudeSlider;
    juce::Slider mixSlider;
    juce::Slider dwellSlider;
    juce::Label gravityLabel;
    juce::Label magnitudeLabel;
    juce::Label mixLabel;
    juce::Label dwellLabel;
    juce::Rectangle<int> slotLightBounds;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gravityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> magnitudeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dwellAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> liveAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventHorizonAudioProcessorEditor)
};
