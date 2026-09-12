#include "PluginEditor.h"
#include <EventHorizonAssets.h>

#include <cmath>

EventHorizonLookAndFeel::EventHorizonLookAndFeel()
    : knobImage (juce::ImageFileFormat::loadFrom (
          EventHorizonAssets::knob_png, EventHorizonAssets::knob_pngSize))
{
}

void EventHorizonLookAndFeel::drawRotarySlider (
    juce::Graphics& graphics, int x, int y, int width, int height,
    float sliderPosition, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    if (! knobImage.isValid())
    {
        juce::LookAndFeel_V4::drawRotarySlider (
            graphics, x, y, width, height, sliderPosition,
            rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }

    const auto diameter = 0.50f * (float) juce::jmin (width, height);
    const auto centreX = (float) x + 0.5f * (float) width;
    const auto centreY = (float) y + 0.5f * (float) height;
    const auto angle = rotaryStartAngle
                     + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto destination = juce::Rectangle<float> (
        centreX - 0.5f * diameter, centreY - 0.5f * diameter,
        diameter, diameter);
    graphics.setColour (juce::Colours::black.withAlpha (0.72f));
    graphics.fillEllipse (destination.expanded (2.0f));
    graphics.saveState();
    graphics.setOpacity (0.74f);
    graphics.addTransform (juce::AffineTransform::rotation (angle, centreX, centreY));
    graphics.drawImage (knobImage, destination);
    graphics.restoreState();
    graphics.setColour (juce::Colours::black.withAlpha (0.16f));
    graphics.fillEllipse (destination);
}

void EventHorizonLookAndFeel::drawButtonBackground (
    juce::Graphics& graphics, juce::Button& button,
    const juce::Colour&, bool isHighlighted, bool isDown)
{
    auto outer = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto enabledAlpha = button.isEnabled() ? 1.0f : 0.42f;
    graphics.setColour (juce::Colour::fromRGB (2, 3, 4).withAlpha (enabledAlpha));
    graphics.fillRoundedRectangle (outer, 3.5f);
    graphics.setColour (juce::Colour::fromRGB (80, 83, 86).withAlpha (0.75f * enabledAlpha));
    graphics.drawRoundedRectangle (outer, 3.5f, 1.0f);

    auto face = outer.reduced (5.0f, 4.5f);
    const auto latched = button.getToggleState();
    const auto top = latched ? juce::Colour::fromRGB (255, 225, 174)
                             : juce::Colour::fromRGB (247, 232, 210);
    const auto bottom = latched ? juce::Colour::fromRGB (212, 130, 62)
                                : juce::Colour::fromRGB (199, 176, 148);
    juce::ColourGradient lamp (
        top.brighter (isHighlighted ? 0.08f : 0.0f), face.getCentreX(), face.getY(),
        bottom.darker (isDown ? 0.18f : 0.0f), face.getCentreX(), face.getBottom(), false);
    lamp.addColour (0.22, top);
    graphics.setGradientFill (lamp);
    graphics.fillRoundedRectangle (face.translated (0.0f, isDown ? 1.0f : 0.0f), 4.0f);
    graphics.setColour (juce::Colours::white.withAlpha (
        (latched ? 0.44f : 0.26f) * enabledAlpha));
    graphics.drawHorizontalLine (juce::roundToInt (face.getY() + 3.0f),
                                 face.getX() + 5.0f, face.getRight() - 5.0f);
}

void EventHorizonLookAndFeel::drawButtonText (
    juce::Graphics& graphics, juce::TextButton& button,
    bool, bool isDown)
{
    auto face = button.getLocalBounds().reduced (7, 6);
    if (isDown)
        face.translate (0, 1);
    graphics.setColour (juce::Colour::fromRGB (43, 25, 21)
        .withAlpha (button.isEnabled() ? 0.96f : 0.40f));
    graphics.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    graphics.drawFittedText (button.getButtonText(), face,
                             juce::Justification::centred, 2, 0.86f);
}

void EventHorizonLookAndFeel::drawLabel (juce::Graphics& graphics, juce::Label& label)
{
    const juce::Font font (juce::FontOptions (12.5f, juce::Font::bold));
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, label.getText());
    const auto plateWidth = juce::jmin ((float) label.getWidth() - 2.0f, textWidth + 18.0f);
    const auto plate = juce::Rectangle<float> (
        0.5f * ((float) label.getWidth() - plateWidth), 0.5f,
        plateWidth, (float) label.getHeight() - 2.5f);

    graphics.setColour (juce::Colours::black.withAlpha (0.76f));
    graphics.fillRoundedRectangle (plate.translated (0.0f, 1.5f), 1.5f);
    juce::ColourGradient metal (
        juce::Colour::fromRGB (42, 45, 47), plate.getX(), plate.getY(),
        juce::Colour::fromRGB (14, 16, 18), plate.getX(), plate.getBottom(), false);
    graphics.setGradientFill (metal);
    graphics.fillRoundedRectangle (plate, 1.5f);
    graphics.setColour (juce::Colours::white.withAlpha (0.13f));
    graphics.drawHorizontalLine (juce::roundToInt (plate.getY() + 1.0f),
                                 plate.getX() + 2.0f, plate.getRight() - 2.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.72f));
    graphics.drawRoundedRectangle (plate, 1.5f, 0.8f);
    graphics.setColour (juce::Colour::fromRGB (184, 180, 168)
        .withAlpha (label.isEnabled() ? 0.94f : 0.42f));
    graphics.setFont (font);
    graphics.drawFittedText (label.getText(), plate.toNearestInt(),
                             juce::Justification::centred, 1, 0.9f);
}

void HorizonDisplay::paint (juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat().reduced (10.0f);
    const auto centre = bounds.getCentre();
    const auto outerRadius = 0.46f * juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto horizonRadius = outerRadius * 0.16f;

    juce::Path eyeClip;
    eyeClip.addEllipse (centre.x - outerRadius, centre.y - outerRadius,
                        outerRadius * 2.0f, outerRadius * 2.0f);
    graphics.saveState();
    graphics.reduceClipRegion (eyeClip, juce::AffineTransform());

    juce::ColourGradient space (juce::Colour::fromRGB (15, 20, 36), centre.x, centre.y,
                                juce::Colours::black, centre.x + outerRadius, centre.y, true);
    graphics.setGradientFill (space);
    graphics.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                          outerRadius * 2.0f, outerRadius * 2.0f);

    float accumulatedPlasma = 0.0f;
    for (int i = 0; i < EventHorizonEngine::maxChunks; ++i)
    {
        const auto snapshot = processor.getSnapshot (i);
        if (snapshot.active)
            accumulatedPlasma += juce::jlimit (0.0f, 1.0f,
                (snapshot.proximity - 0.82f) / (0.965f - 0.82f));
    }
    const auto targetPlasmaActivity = 1.0f - std::exp (-0.38f * accumulatedPlasma);
    const auto plasmaSmoothing = targetPlasmaActivity > displayedPlasmaActivity ? 0.16f : 0.045f;
    displayedPlasmaActivity += plasmaSmoothing
                             * (targetPlasmaActivity - displayedPlasmaActivity);
    const auto plasmaActivity = juce::jlimit (0.0f, 1.0f, displayedPlasmaActivity);
    const auto plasmaBandWidth = 1.4f + 15.0f * plasmaActivity;

    // Plasma lives outside the black disc.  Its hot arcs are driven by actual
    // inner matter and impact energy, so the apparent grinder motion follows
    // the same orbital state as the sound.
    graphics.setColour (juce::Colour::fromRGB (255, 66, 8)
        .withAlpha (0.10f + 0.18f * plasmaActivity));
    graphics.drawEllipse (centre.x - horizonRadius - 2.0f,
                          centre.y - horizonRadius - 2.0f,
                          (horizonRadius + 2.0f) * 2.0f,
                          (horizonRadius + 2.0f) * 2.0f, plasmaBandWidth);
    for (int i = 0; i < EventHorizonEngine::maxChunks; ++i)
    {
        const auto snapshot = processor.getSnapshot (i);
        if (! snapshot.active || snapshot.proximity < 0.82f)
            continue;

        const auto energy = juce::jlimit (0.0f, 1.0f,
            (snapshot.proximity - 0.82f) / (0.965f - 0.82f));
        const auto flare = juce::jlimit (0.0f, 1.0f, snapshot.impactGlow);
        const auto radius = horizonRadius + 1.5f
                          + 0.18f * plasmaBandWidth * (1.0f - energy);
        const auto halfSpan = 0.035f + 0.09f * energy + 0.08f * flare;
        juce::Path hotArc;
        constexpr int arcSteps = 14;
        for (int step = 0; step <= arcSteps; ++step)
        {
            const auto proportion = (float) step / (float) arcSteps;
            const auto angle = snapshot.angle + juce::jmap (proportion, -halfSpan, halfSpan);
            const auto x = centre.x + std::sin (angle) * radius;
            const auto y = centre.y - std::cos (angle) * radius;
            if (step == 0)
                hotArc.startNewSubPath (x, y);
            else
                hotArc.lineTo (x, y);
        }

        const auto hotColour = juce::Colour::fromRGB (255, 103, 22)
            .interpolatedWith (juce::Colour::fromRGB (255, 232, 142), flare);
        graphics.setColour (hotColour.withAlpha (0.08f + 0.22f * energy + 0.35f * flare));
        graphics.strokePath (hotArc, juce::PathStrokeType (
            3.0f + 0.55f * plasmaBandWidth + 5.0f * flare,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        graphics.setColour (hotColour.withAlpha (0.22f + 0.48f * flare));
        graphics.strokePath (hotArc, juce::PathStrokeType (1.1f + 1.8f * flare,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (flare > 0.08f)
        {
            const auto outwardX = std::sin (snapshot.angle);
            const auto outwardY = -std::cos (snapshot.angle);
            const auto tangentX = std::cos (snapshot.angle);
            const auto tangentY = std::sin (snapshot.angle);
            const auto originRadius = horizonRadius + 0.40f * plasmaBandWidth;
            const auto originX = centre.x + outwardX * originRadius;
            const auto originY = centre.y + outwardY * originRadius;
            constexpr int sparkCount = 9;
            for (int spark = 0; spark < sparkCount; ++spark)
            {
                const auto hash = std::fmod (
                    std::sin ((float) ((i + 1) * 37 + (spark + 3) * 91)) * 43758.5453f,
                    1.0f);
                const auto variation = hash < 0.0f ? hash + 1.0f : hash;
                const auto spread = ((float) spark / (float) (sparkCount - 1) - 0.5f) * 0.85f;
                auto directionX = tangentX * (0.78f + 0.24f * variation)
                                + outwardX * (0.30f + spread);
                auto directionY = tangentY * (0.78f + 0.24f * variation)
                                + outwardY * (0.30f + spread);
                const auto directionLength = std::sqrt (
                    directionX * directionX + directionY * directionY);
                directionX /= juce::jmax (0.001f, directionLength);
                directionY /= juce::jmax (0.001f, directionLength);
                const auto sparkLength = flare * (7.0f + 24.0f * variation);
                const auto endX = originX + directionX * sparkLength;
                const auto endY = originY + directionY * sparkLength;
                graphics.setColour (juce::Colour::fromRGB (255, 116, 22)
                    .withAlpha (0.12f + 0.30f * flare));
                graphics.drawLine (originX, originY, endX, endY, 2.2f);
                graphics.setColour (juce::Colour::fromRGB (255, 238, 176)
                    .withAlpha (0.32f + 0.50f * flare));
                graphics.drawLine (originX, originY, endX, endY, 0.75f);
            }
        }
    }

    graphics.setColour (juce::Colours::black);
    graphics.fillEllipse (centre.x - horizonRadius, centre.y - horizonRadius,
                          horizonRadius * 2.0f, horizonRadius * 2.0f);
    graphics.setColour (juce::Colour::fromRGB (255, 126, 47).withAlpha (0.75f));
    graphics.drawEllipse (centre.x - horizonRadius, centre.y - horizonRadius,
                          horizonRadius * 2.0f, horizonRadius * 2.0f, 2.0f);

    for (int i = 0; i < EventHorizonEngine::maxChunks; ++i)
    {
        const auto snapshot = processor.getSnapshot (i);
        if (! snapshot.active)
            continue;

        const auto radius = horizonRadius
                          + (outerRadius - horizonRadius) * (1.0f - snapshot.proximity);
        // Horizontal screen position now uses the same sine phase as audio pan:
        // left on screen is left in the stereo field, right is right.
        const auto x = centre.x + std::sin (snapshot.angle) * radius;
        const auto y = centre.y - std::cos (snapshot.angle) * radius;
        const auto massPosition = juce::jlimit (0.0f, 1.0f,
            (snapshot.gravity - 0.85f) / 0.30f);
        const auto shrinkPosition = juce::jlimit (0.0f, 1.0f,
            (snapshot.proximity - 0.25f) / (0.965f - 0.25f));
        const auto shrinkCurve = shrinkPosition * shrinkPosition
                               * (3.0f - 2.0f * shrinkPosition);
        const auto size = (8.0f + massPosition * 10.0f)
                        * (1.0f - 0.88f * shrinkCurve)
                        * snapshot.visualScale;
        const std::array<float, 8> generationHues {
            0.27f, 0.35f, 0.43f, 0.50f, 0.58f, 0.66f, 0.76f, 0.88f
        };
        const auto generationIndex = juce::jlimit (0, 7, snapshot.generation);
        const auto colour = snapshot.collisionShard
            ? juce::Colour::fromRGB (244, 43, 24)
            : juce::Colour::fromHSV (generationHues[(size_t) generationIndex],
                                     0.58f + 0.18f * snapshot.proximity,
                                     1.0f, 0.9f);
        graphics.setColour (colour.withAlpha (0.18f));
        graphics.fillEllipse (x - size, y - size, size * 2.0f, size * 2.0f);
        const auto bodyRadius = size * 0.5f;
        juce::ColourGradient body (
            colour.brighter (0.42f), x - bodyRadius * 0.30f, y - bodyRadius * 0.34f,
            colour.darker (0.82f), x + bodyRadius * 0.62f, y + bodyRadius * 0.70f, true);
        body.addColour (0.42, colour.withMultipliedBrightness (0.82f));
        body.addColour (0.78, colour.darker (0.42f));
        graphics.setGradientFill (body);
        graphics.fillEllipse (x - bodyRadius, y - bodyRadius,
                              bodyRadius * 2.0f, bodyRadius * 2.0f);
        graphics.setColour (juce::Colours::black.withAlpha (0.38f));
        graphics.drawEllipse (x - bodyRadius, y - bodyRadius,
                              bodyRadius * 2.0f, bodyRadius * 2.0f, 0.8f);
    }

    // Smoked convex-glass cover: edge vignette, upper-left reflection and a
    // substantial recessed bezel.  The central disc is repainted last so it
    // remains optically absolute rather than becoming a glossy red eye.
    juce::ColourGradient glassVignette (
        juce::Colours::transparentBlack, centre.x - outerRadius * 0.18f,
        centre.y - outerRadius * 0.20f,
        juce::Colours::black.withAlpha (0.62f), centre.x + outerRadius,
        centre.y + outerRadius, true);
    glassVignette.addColour (0.68, juce::Colours::transparentBlack);
    graphics.setGradientFill (glassVignette);
    graphics.fillEllipse (centre.x - outerRadius, centre.y - outerRadius,
                          outerRadius * 2.0f, outerRadius * 2.0f);

    graphics.saveState();
    graphics.reduceClipRegion (eyeClip, juce::AffineTransform());
    juce::ColourGradient reflection (
        juce::Colours::white.withAlpha (0.065f), centre.x - outerRadius * 0.50f,
        centre.y - outerRadius * 0.58f,
        juce::Colours::white.withAlpha (0.0f), centre.x - outerRadius * 0.05f,
        centre.y - outerRadius * 0.08f, true);
    graphics.setGradientFill (reflection);
    graphics.fillEllipse (centre.x - outerRadius * 0.83f,
                          centre.y - outerRadius * 0.88f,
                          outerRadius * 0.92f, outerRadius * 0.54f);
    graphics.restoreState();

    graphics.setColour (juce::Colours::black.withAlpha (0.96f));
    graphics.fillEllipse (centre.x - horizonRadius, centre.y - horizonRadius,
                          horizonRadius * 2.0f, horizonRadius * 2.0f);
    // The absolute black disc is edged by a thin, population-sensitive plasma
    // core.  It remains a dull ember when starved and approaches white heat as
    // material accumulates, with a soft outer bloom beneath the sharp rim.
    const auto rimHeat = std::pow (plasmaActivity, 0.68f);
    const auto rimColour = juce::Colour::fromRGB (178, 43, 7)
        .interpolatedWith (juce::Colour::fromRGB (255, 241, 186), rimHeat);
    graphics.setColour (rimColour.withAlpha (0.07f + 0.24f * plasmaActivity));
    graphics.drawEllipse (centre.x - horizonRadius, centre.y - horizonRadius,
                          horizonRadius * 2.0f, horizonRadius * 2.0f,
                          3.0f + 2.4f * plasmaActivity);
    graphics.setColour (rimColour.withAlpha (0.34f + 0.62f * plasmaActivity));
    graphics.drawEllipse (centre.x - horizonRadius, centre.y - horizonRadius,
                          horizonRadius * 2.0f, horizonRadius * 2.0f,
                          0.85f + 0.75f * plasmaActivity);

    // Hawking radiation is drawn from the actual audio voices.  Its pre-delay,
    // outward progress, strength and horizontal destination are therefore the
    // same event the listener hears rather than an unrelated visual flourish.
    for (int radiationIndex = 0;
         radiationIndex < EventHorizonEngine::maxRadiationVoices;
         ++radiationIndex)
    {
        const auto radiation = processor.getRadiationSnapshot (radiationIndex);
        if (! radiation.active)
            continue;

        const auto progress = juce::jlimit (0.0f, 1.0f, radiation.progress);
        const auto outward = progress * progress * (3.0f - 2.0f * progress);
        const auto targetX = radiation.targetPan * outerRadius * 0.94f;
        const auto remainingRadius = std::sqrt (juce::jmax (
            0.0f, outerRadius * outerRadius - targetX * targetX));
        const auto verticalSign = (radiationIndex & 1) == 0 ? -1.0f : 1.0f;
        const auto targetY = verticalSign * remainingRadius * 0.90f;
        const auto endX = centre.x + targetX * outward;
        const auto endY = centre.y + targetY * outward;
        auto directionX = endX - centre.x;
        auto directionY = endY - centre.y;
        const auto directionLength = std::sqrt (
            directionX * directionX + directionY * directionY);
        if (directionLength < 0.5f)
            continue;
        directionX /= directionLength;
        directionY /= directionLength;
        const auto perpendicularX = -directionY;
        const auto perpendicularY = directionX;

        juce::Path bolt;
        constexpr int boltSegments = 11;
        for (int segment = 0; segment <= boltSegments; ++segment)
        {
            const auto t = (float) segment / (float) boltSegments;
            const auto temporalStep = juce::roundToInt (progress * 37.0f);
            const auto hashInput = (float) ((radiationIndex + 3) * 79
                + (segment + 5) * 131 + temporalStep * 43);
            auto hash = std::fmod (std::sin (hashInput) * 43758.5453f, 1.0f);
            if (hash < 0.0f)
                hash += 1.0f;
            const auto centralJitter = std::sin (juce::MathConstants<float>::pi * t);
            const auto jitter = (hash * 2.0f - 1.0f) * centralJitter
                              * (3.0f + 7.0f * progress);
            const auto x = centre.x + directionX * directionLength * t
                         + perpendicularX * jitter;
            const auto y = centre.y + directionY * directionLength * t
                         + perpendicularY * jitter;
            if (segment == 0)
                bolt.startNewSubPath (x, y);
            else
                bolt.lineTo (x, y);
        }

        const auto life = std::sin (juce::MathConstants<float>::pi
                                  * juce::jlimit (0.02f, 0.98f, progress));
        const auto energy = juce::jlimit (0.0f, 1.0f,
            radiation.intensity * 1.5f) * (0.38f + 0.62f * life);
        graphics.setColour (juce::Colour::fromRGB (80, 166, 255)
            .withAlpha (0.12f + 0.20f * energy));
        graphics.strokePath (bolt, juce::PathStrokeType (
            5.0f + 4.0f * energy, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        graphics.setColour (juce::Colour::fromRGB (190, 226, 255)
            .withAlpha (0.40f + 0.46f * energy));
        graphics.strokePath (bolt, juce::PathStrokeType (
            1.15f + 1.25f * energy, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        graphics.setColour (juce::Colours::white.withAlpha (0.58f + 0.32f * energy));
        graphics.drawLine (endX - directionX * 3.0f, endY - directionY * 3.0f,
                           endX, endY, 1.0f);
    }

    graphics.restoreState();

    juce::ColourGradient bezel (
        juce::Colour::fromRGB (47, 52, 60), centre.x, centre.y - outerRadius,
        juce::Colour::fromRGB (3, 5, 8), centre.x, centre.y + outerRadius, false);
    bezel.addColour (0.48, juce::Colour::fromRGB (15, 19, 25));
    graphics.setGradientFill (bezel);
    graphics.drawEllipse (centre.x - outerRadius, centre.y - outerRadius,
                          outerRadius * 2.0f, outerRadius * 2.0f, 15.0f);
    graphics.setColour (juce::Colour::fromRGB (108, 113, 122).withAlpha (0.66f));
    graphics.drawEllipse (centre.x - outerRadius - 1.0f,
                          centre.y - outerRadius - 1.0f,
                          (outerRadius + 1.0f) * 2.0f,
                          (outerRadius + 1.0f) * 2.0f, 1.4f);
    graphics.setColour (juce::Colours::white.withAlpha (0.10f));
    graphics.drawEllipse (centre.x - outerRadius + 5.0f,
                          centre.y - outerRadius + 5.0f,
                          (outerRadius - 5.0f) * 2.0f,
                          (outerRadius - 5.0f) * 2.0f, 1.0f);
}

EventHorizonAudioProcessorEditor::EventHorizonAudioProcessorEditor (
    EventHorizonAudioProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      horizonDisplay (processorToUse)
{
    setSize (540, 780);

    const std::array<juce::Component*, 13> components {
        &horizonDisplay, &recordButton, &loadButton, &clearButton, &liveButton,
        &gravitySlider, &magnitudeSlider, &mixSlider, &dwellSlider,
        &gravityLabel, &magnitudeLabel, &mixLabel, &dwellLabel
    };
    for (auto* component : components)
        addAndMakeVisible (component);

    for (auto* slider : { &gravitySlider, &dwellSlider, &magnitudeSlider, &mixSlider })
        slider->setLookAndFeel (&lookAndFeel);
    for (auto* button : { &recordButton, &loadButton, &clearButton, &liveButton })
        button->setLookAndFeel (&lookAndFeel);
    for (auto* label : { &gravityLabel, &dwellLabel, &magnitudeLabel, &mixLabel })
        label->setLookAndFeel (&lookAndFeel);

    gravitySlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gravitySlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    magnitudeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    magnitudeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dwellSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dwellSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gravityLabel.setText ("GRAVITY", juce::dontSendNotification);
    magnitudeLabel.setText ("MAGNITUDE", juce::dontSendNotification);
    mixLabel.setText ("MIX", juce::dontSendNotification);
    dwellLabel.setText ("CHURN", juce::dontSendNotification);
    gravityLabel.setJustificationType (juce::Justification::centred);
    magnitudeLabel.setJustificationType (juce::Justification::centred);
    mixLabel.setJustificationType (juce::Justification::centred);
    dwellLabel.setJustificationType (juce::Justification::centred);
    gravityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, EventHorizonAudioProcessor::gravityParameterId, gravitySlider);
    magnitudeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, EventHorizonAudioProcessor::magnitudeParameterId, magnitudeSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, EventHorizonAudioProcessor::mixParameterId, mixSlider);
    dwellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, EventHorizonAudioProcessor::dwellParameterId, dwellSlider);
    liveButton.setClickingTogglesState (true);
    liveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, EventHorizonAudioProcessor::liveParameterId, liveButton);

    recordButton.onClick = [this]
    {
        processor.startRecording();
    };
    loadButton.onClick = [this] { chooseAudioFile(); };
    clearButton.onClick = [this] { processor.clearUniverse(); };

    startTimerHz (30);
}

EventHorizonAudioProcessorEditor::~EventHorizonAudioProcessorEditor()
{
    stopTimer();
    for (auto* slider : { &gravitySlider, &dwellSlider, &magnitudeSlider, &mixSlider })
        slider->setLookAndFeel (nullptr);
    for (auto* button : { &recordButton, &loadButton, &clearButton, &liveButton })
        button->setLookAndFeel (nullptr);
    for (auto* label : { &gravityLabel, &dwellLabel, &magnitudeLabel, &mixLabel })
        label->setLookAndFeel (nullptr);
}

void EventHorizonAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    juce::ColourGradient panel (
        juce::Colour::fromRGB (30, 32, 35), 0.0f, 0.0f,
        juce::Colour::fromRGB (5, 6, 8), 0.0f, (float) getHeight(), false);
    graphics.setGradientFill (panel);
    graphics.fillAll();
    for (int y = 2; y < getHeight(); y += 4)
    {
        graphics.setColour (juce::Colours::white.withAlpha (0.012f));
        graphics.drawHorizontalLine (y, 8.0f, (float) getWidth() - 8.0f);
    }
    graphics.setColour (juce::Colour::fromRGB (74, 78, 84).withAlpha (0.70f));
    graphics.drawRoundedRectangle (getLocalBounds().toFloat().reduced (5.0f),
                                   5.0f, 1.2f);
    const auto nameplate = juce::Rectangle<float> (
        0.5f * (float) getWidth() - 151.0f, 6.0f, 302.0f, 38.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.78f));
    graphics.fillRoundedRectangle (nameplate.translated (0.0f, 2.5f), 2.0f);
    juce::ColourGradient plate (
        juce::Colour::fromRGB (70, 73, 75), nameplate.getX(), nameplate.getY(),
        juce::Colour::fromRGB (25, 27, 29), nameplate.getX(), nameplate.getBottom(), false);
    plate.addColour (0.42, juce::Colour::fromRGB (47, 50, 52));
    graphics.setGradientFill (plate);
    graphics.fillRoundedRectangle (nameplate, 2.0f);
    graphics.setColour (juce::Colours::white.withAlpha (0.24f));
    graphics.drawHorizontalLine (juce::roundToInt (nameplate.getY() + 1.0f),
                                 nameplate.getX() + 3.0f, nameplate.getRight() - 3.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.86f));
    graphics.drawRoundedRectangle (nameplate, 2.0f, 1.0f);
    for (const auto screwX : { nameplate.getX() + 10.0f, nameplate.getRight() - 10.0f })
    {
        graphics.setColour (juce::Colour::fromRGB (18, 20, 22));
        graphics.fillEllipse (screwX - 2.2f, nameplate.getCentreY() - 2.2f, 4.4f, 4.4f);
        graphics.setColour (juce::Colours::white.withAlpha (0.18f));
        graphics.drawLine (screwX - 1.2f, nameplate.getCentreY() - 1.2f,
                           screwX + 1.2f, nameplate.getCentreY() + 1.2f, 0.7f);
    }

    const juce::Font titleFont (juce::FontOptions (25.0f, juce::Font::bold));
    const juce::String titlePrefix { "EVENT H" };
    const juce::String titleSuffix { "RIZON" };
    const auto prefixWidth = juce::GlyphArrangement::getStringWidth (titleFont, titlePrefix);
    const auto suffixWidth = juce::GlyphArrangement::getStringWidth (titleFont, titleSuffix);
    const auto ringWidth = 20.0f;
    const auto titleWidth = prefixWidth + ringWidth + suffixWidth;
    const auto titleX = 0.5f * ((float) getWidth() - titleWidth);
    const auto titleAreaY = 8.0f;
    graphics.setColour (juce::Colours::black.withAlpha (0.66f));
    graphics.setFont (titleFont);
    graphics.drawText (titlePrefix, juce::Rectangle<float> (
        titleX + 1.0f, titleAreaY + 1.5f, prefixWidth, 34.0f),
        juce::Justification::centredLeft);
    graphics.drawText (titleSuffix, juce::Rectangle<float> (
        titleX + prefixWidth + ringWidth + 1.0f, titleAreaY + 1.5f,
        suffixWidth, 34.0f), juce::Justification::centredLeft);
    graphics.setColour (juce::Colour::fromRGB (235, 236, 231));
    graphics.setFont (titleFont);
    graphics.drawText (titlePrefix, juce::Rectangle<float> (
        titleX, titleAreaY, prefixWidth, 34.0f), juce::Justification::centredLeft);
    graphics.drawText (titleSuffix, juce::Rectangle<float> (
        titleX + prefixWidth + ringWidth, titleAreaY, suffixWidth, 34.0f),
        juce::Justification::centredLeft);
    const auto titleRingCentreX = titleX + prefixWidth + 0.5f * ringWidth;
    const auto titleRingCentreY = titleAreaY + 17.0f;
    graphics.setColour (juce::Colour::fromRGB (255, 92, 18).withAlpha (0.22f));
    graphics.fillEllipse (titleRingCentreX - 9.5f, titleRingCentreY - 9.5f, 19.0f, 19.0f);
    graphics.setColour (juce::Colours::black);
    graphics.fillEllipse (titleRingCentreX - 6.7f, titleRingCentreY - 6.7f, 13.4f, 13.4f);
    graphics.setColour (juce::Colour::fromRGB (255, 125, 40));
    graphics.drawEllipse (titleRingCentreX - 7.2f, titleRingCentreY - 7.2f,
                          14.4f, 14.4f, 1.3f);

    const auto playing = processor.getPlayingGenerationCount();
    const auto waiting = juce::jlimit (0, 2, processor.getGenerationCount() - playing);
    const auto lampDiameter = 10.0f;
    const auto lampGap = 9.0f;
    const auto lampsWidth = 8.0f * lampDiameter + 7.0f * lampGap;
    const auto lampStartX = (float) slotLightBounds.getCentreX() - 0.5f * lampsWidth;
    const auto lampY = (float) slotLightBounds.getCentreY() - 0.5f * lampDiameter;
    for (int slot = 0; slot < 8; ++slot)
    {
        const auto isWaitingSlot = slot >= 6;
        const auto lit = isWaitingSlot ? slot - 6 < waiting : slot < playing;
        const auto litColour = isWaitingSlot ? juce::Colour::fromRGB (218, 66, 38)
                                             : juce::Colour::fromRGB (226, 174, 46);
        const auto darkColour = isWaitingSlot ? juce::Colour::fromRGB (49, 20, 17)
                                              : juce::Colour::fromRGB (48, 40, 19);
        const auto x = lampStartX + (float) slot * (lampDiameter + lampGap);
        if (lit)
        {
            graphics.setColour (litColour.withAlpha (0.20f));
            graphics.fillEllipse (x - 3.0f, lampY - 3.0f,
                                  lampDiameter + 6.0f, lampDiameter + 6.0f);
        }
        graphics.setColour (juce::Colours::black.withAlpha (0.72f));
        graphics.fillEllipse (x + 0.8f, lampY + 1.8f,
                              lampDiameter + 2.2f, lampDiameter + 2.2f);
        graphics.setColour (juce::Colour::fromRGB (9, 10, 11));
        graphics.fillEllipse (x - 1.5f, lampY - 1.5f,
                              lampDiameter + 3.0f, lampDiameter + 3.0f);
        graphics.setColour (juce::Colour::fromRGB (70, 72, 73).withAlpha (0.48f));
        graphics.drawEllipse (x - 1.2f, lampY - 1.2f,
                              lampDiameter + 2.4f, lampDiameter + 2.4f, 0.7f);
        juce::ColourGradient lamp (lit ? litColour.brighter (0.35f) : darkColour,
                                   x + 3.0f, lampY + 2.0f,
                                   lit ? litColour.darker (0.45f) : juce::Colours::black,
                                   x + lampDiameter, lampY + lampDiameter, true);
        graphics.setGradientFill (lamp);
        graphics.fillEllipse (x, lampY, lampDiameter, lampDiameter);
        graphics.setColour (juce::Colours::black.withAlpha (0.82f));
        graphics.drawEllipse (x, lampY, lampDiameter, lampDiameter, 1.0f);
        graphics.setColour (juce::Colours::white.withAlpha (lit ? 0.58f : 0.14f));
        graphics.fillEllipse (x + 2.0f, lampY + 1.7f, 2.8f, 2.1f);
    }
}

void EventHorizonAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (40);
    horizonDisplay.setBounds (area.removeFromTop (490).reduced (14, 2));

    area.removeFromTop (4);
    auto buttons = area.removeFromTop (56).withSizeKeepingCentre (312, 56);
    constexpr int buttonGap = 5;
    const auto buttonWidth = (buttons.getWidth() - 3 * buttonGap) / 4;
    recordButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (buttonGap);
    loadButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (buttonGap);
    clearButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (buttonGap);
    liveButton.setBounds (buttons);

    area.removeFromTop (4);
    slotLightBounds = area.removeFromTop (22);
    area.removeFromTop (4);
    const auto placeKnob = [] (juce::Label& label, juce::Slider& slider,
                               juce::Rectangle<int> cell)
    {
        label.setBounds (cell.removeFromTop (18));
        slider.setBounds (cell.reduced (4, 0));
    };

    const auto cellWidth = area.getWidth() / 4;
    placeKnob (gravityLabel, gravitySlider, area.removeFromLeft (cellWidth));
    placeKnob (dwellLabel, dwellSlider, area.removeFromLeft (cellWidth));
    placeKnob (magnitudeLabel, magnitudeSlider, area.removeFromLeft (cellWidth));
    placeKnob (mixLabel, mixSlider, area);
}

void EventHorizonAudioProcessorEditor::timerCallback()
{
    horizonDisplay.repaint();
    repaint (slotLightBounds);
    const auto status = processor.getStatusText();
    const auto acquiring = status.startsWith ("Recording") || status.startsWith ("Analysing")
                        || status.startsWith ("Preparing");
    const auto canAcquire = ! acquiring && processor.canAcceptSource();
    recordButton.setEnabled (canAcquire && ! liveButton.getToggleState());
    loadButton.setEnabled (canAcquire);
}

void EventHorizonAudioProcessorEditor::chooseAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load up to six seconds of source", juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (browserFlags,
        [safeThis = juce::Component::SafePointer<EventHorizonAudioProcessorEditor> (this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                const auto error = safeThis->processor.loadAudioFile (file);
                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Event Horizon", error);
            }
            safeThis->fileChooser.reset();
        });
}
