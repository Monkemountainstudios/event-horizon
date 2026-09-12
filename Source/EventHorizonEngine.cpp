#include "EventHorizonEngine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;
}

EventHorizonEngine::EventHorizonEngine() = default;

void EventHorizonEngine::prepare (double newSampleRate, int maximumBlockSize)
{
    sampleRate = juce::jmax (8000.0, newSampleRate);
    // Six seconds is allocation capacity, not a mandatory phrase length.
    // Manual recording remains four seconds; LIVE receives a per-capture limit.
    preparedSamples = juce::roundToInt (sampleRate * 6.0);

    for (auto& slot : sourceSlots)
    {
        slot.audio.setSize (1, preparedSamples, false, true, false);
        slot.audio.clear();
        slot.validSamples = 0;
        slot.numChunks = 0;
        slot.isGhost = false;
        slot.slingshotConsidered = false;
        slot.state.store (SlotState::free, std::memory_order_release);
    }

    for (auto& memory : ghostMemories)
    {
        memory.audio.setSize (1, preparedSamples, false, true, false);
        memory.audio.clear();
        memory.validSamples = 0;
        memory.numChunks = 0;
        memory.ageSeconds = 0.0f;
        memory.maturitySeconds = 90.0f;
        memory.state.store (GhostState::empty, std::memory_order_release);
    }

    for (auto& chunk : chunkPool)
        chunk = {};

    for (auto& fragment : fragmentPool)
        fragment = {};
    for (auto& radiation : radiationPool)
        radiation = {};

    clearingUniverse = false;
    recordingSlot = -1;
    recordingWritePosition = 0;
    recordingTargetSamples = juce::roundToInt (sampleRate * 4.0);
    liveCaptureEnabled = false;
    liveGateOpen = false;
    liveCaptureSlot = -1;
    liveCaptureWritePosition = 0;
    liveCaptureTargetSamples = 0;
    liveQuietSamples = 0;
    liveRearmSamples = 0;
    liveRetrySamples = 0;
    liveCaptureIntervalSamples = 0;
    liveEnvelope = 0.0f;
    liveNoiseFloor = 0.0005f;
    liveCaptureAppetite = 0.65f;
    liveCaptureTimeScale = 0.45f;
    liveHunger = 0.0f;
    liveRandomState = 0x4c495645u;
    ghostRandomState = 0x47484f53u;
    ghostServiceRandomState = 0x4d454d4fu;
    rareEventRandomState = 0x52415245u;
    ghostCooldownSamples = 0;
    ghostArchiveRefillCooldownSamples = 0;
    ghostSpontaneousCheckSamples = juce::roundToInt (sampleRate * 8.0);
    ghostSafetySpent = false;
    livePreRoll.setSize (1, juce::jmax (1, juce::roundToInt (sampleRate * 0.10)),
                         false, true, false);
    livePreRoll.clear();
    livePreRollWritePosition = 0;
    recordingCommand.store (-1, std::memory_order_release);
    clearCommand.store (false, std::memory_order_release);
    pendingGhostArchiveMask.store (0, std::memory_order_release);
    pendingGhostRecallMemory.store (-1, std::memory_order_release);
    pendingGhostRecallTargetSlot.store (-1, std::memory_order_release);
    recordingProgress.store (0.0f, std::memory_order_relaxed);
    status.store (Status::empty, std::memory_order_release);
    universeGain.reset (sampleRate, 0.025);
    universeGain.setCurrentAndTargetValue (1.0f);
    fieldBuffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);
    fieldBuffer.clear();
    shimmerBuffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, false);
    shimmerBuffer.clear();
    shimmerFeedbackDelay.setSize (2, juce::jmax (1, juce::roundToInt (sampleRate * 0.75)),
                                  false, true, false);
    shimmerFeedbackDelay.clear();
    shimmerFeedbackWritePosition = 0;
    globalImpactCooldownSeconds = 0.0f;
    ruptureCooldownSeconds = 0.0f;
    reversalCooldownSeconds = 0.0f;
    escapeCooldownSeconds = 0.0f;
    sessionAgeSeconds = 0.0f;
    gravityReverb.setSampleRate (sampleRate);
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = 0.92f;
    reverbParameters.damping = 0.68f;
    reverbParameters.wetLevel = 0.52f;
    reverbParameters.dryLevel = 0.0f;
    reverbParameters.width = 0.82f;
    reverbParameters.freezeMode = 0.0f;
    gravityReverb.setParameters (reverbParameters);
    gravityReverb.reset();
    shimmerReverb.setSampleRate (sampleRate);
    juce::Reverb::Parameters shimmerParameters;
    shimmerParameters.roomSize = 0.997f;
    shimmerParameters.damping = 0.10f;
    shimmerParameters.wetLevel = 0.68f;
    shimmerParameters.dryLevel = 0.0f;
    shimmerParameters.width = 1.0f;
    shimmerParameters.freezeMode = 0.0f;
    shimmerReverb.setParameters (shimmerParameters);
    shimmerReverb.reset();
    shimmerHpCoefficient = 1.0f / (1.0f + twoPi * 5000.0f / (float) sampleRate);
    shimmerHpInputL = shimmerHpInputR = 0.0f;
    shimmerHpOutputL = shimmerHpOutputR = 0.0f;
    shimmerFeedbackDelay.clear();
    shimmerFeedbackWritePosition = 0;
    horizonFieldExpansion = 0.0f;
    updateSnapshots();
}

int EventHorizonEngine::claimFreeSlot (SlotState claimedState) noexcept
{
    if (getGenerationCount() >= sourceSlotCount)
        return -1;

    for (int i = 0; i < sourceSlotCount; ++i)
    {
        auto expected = SlotState::free;
        if (sourceSlots[(size_t) i].state.compare_exchange_strong (
                expected, claimedState, std::memory_order_acq_rel))
            return i;
    }

    return -1;
}

bool EventHorizonEngine::requestRecording()
{
    if (preparedSamples <= 0 || recordingCommand.load (std::memory_order_acquire) >= 0)
        return false;

    const auto slotIndex = claimFreeSlot (SlotState::preparedForRecording);
    if (slotIndex < 0)
        return false;

    auto& slot = sourceSlots[(size_t) slotIndex];
    slot.audio.clear();
    recordingTargetSamples = juce::jmin (
        preparedSamples, juce::roundToInt (sampleRate * 4.0));
    slot.validSamples = recordingTargetSamples;
    slot.numChunks = 0;
    slot.isGhost = false;
    slot.slingshotConsidered = false;
    recordingProgress.store (0.0f, std::memory_order_relaxed);
    status.store (Status::armed, std::memory_order_release);
    ghostSafetySpent = false;
    recordingCommand.store (slotIndex, std::memory_order_release);
    return true;
}

bool EventHorizonEngine::submitLoadedAudio (const juce::AudioBuffer<float>& source,
                                             double sourceSampleRate)
{
    if (preparedSamples <= 0 || source.getNumChannels() <= 0 || source.getNumSamples() <= 0)
        return false;

    const auto slotIndex = claimFreeSlot (SlotState::analysing);
    if (slotIndex < 0)
        return false;

    auto& slot = sourceSlots[(size_t) slotIndex];
    slot.audio.clear();
    slot.numChunks = 0;
    slot.isGhost = false;
    slot.slingshotConsidered = false;

    const auto ratio = juce::jmax (1.0e-6, sourceSampleRate / sampleRate);
    const auto availableOutput = juce::roundToInt ((double) source.getNumSamples() / ratio);
    const auto outputSamples = juce::jlimit (1, preparedSamples, availableOutput);

    juce::AudioBuffer<float> monoSource (1, source.getNumSamples());
    monoSource.clear();
    const auto channelGain = 1.0f / (float) source.getNumChannels();
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        monoSource.addFrom (0, 0, source, channel, 0, source.getNumSamples(), channelGain);

    if (std::abs (ratio - 1.0) < 1.0e-6)
    {
        slot.audio.copyFrom (0, 0, monoSource, 0, 0,
                             juce::jmin (outputSamples, monoSource.getNumSamples()));
    }
    else
    {
        const auto* input = monoSource.getReadPointer (0);
        auto* output = slot.audio.getWritePointer (0);
        for (int i = 0; i < outputSamples; ++i)
        {
            const auto position = juce::jmin ((double) source.getNumSamples() - 1.0,
                                              (double) i * ratio);
            const auto index = (int) position;
            const auto next = juce::jmin (index + 1, source.getNumSamples() - 1);
            const auto fraction = (float) (position - (double) index);
            output[i] = input[index] + fraction * (input[next] - input[index]);
        }
    }

    slot.validSamples = outputSamples;
    analyseSlot (slotIndex);
    slot.state.store (SlotState::ready, std::memory_order_release);
    status.store (Status::ready, std::memory_order_release);
    ghostSafetySpent = false;
    return true;
}

void EventHorizonEngine::captureInput (const float* monoInput, int numSamples) noexcept
{
    if (const auto command = recordingCommand.exchange (-1, std::memory_order_acq_rel); command >= 0)
    {
        recordingSlot = command;
        recordingWritePosition = 0;
        sourceSlots[(size_t) recordingSlot].state.store (SlotState::recording,
                                                         std::memory_order_release);
        status.store (Status::recording, std::memory_order_release);
    }

    if (monoInput == nullptr)
        return;

    if (recordingSlot >= 0)
    {
        auto& slot = sourceSlots[(size_t) recordingSlot];
        const auto remaining = recordingTargetSamples - recordingWritePosition;
        const auto toCopy = juce::jmin (remaining, numSamples);
        juce::FloatVectorOperations::copy (slot.audio.getWritePointer (0, recordingWritePosition),
                                           monoInput, toCopy);
        recordingWritePosition += toCopy;
        recordingProgress.store ((float) recordingWritePosition
                                  / (float) juce::jmax (1, recordingTargetSamples),
                                  std::memory_order_relaxed);

        if (recordingWritePosition >= recordingTargetSamples)
        {
            slot.validSamples = recordingTargetSamples;
            slot.state.store (SlotState::captured, std::memory_order_release);
            recordingSlot = -1;
            recordingWritePosition = 0;
            status.store (Status::analysing, std::memory_order_release);
        }
        updateGhostEcology (numSamples, true);
        return;
    }

    if (! liveCaptureEnabled)
    {
        if (liveCaptureSlot >= 0)
            sourceSlots[(size_t) liveCaptureSlot].state.store (SlotState::free,
                                                               std::memory_order_release);
        liveCaptureSlot = -1;
        liveCaptureWritePosition = 0;
        liveCaptureTargetSamples = 0;
        liveGateOpen = false;
        liveQuietSamples = 0;
        liveRearmSamples = 0;
        liveRetrySamples = 0;
        liveCaptureIntervalSamples = 0;
        liveEnvelope = 0.0f;
        liveNoiseFloor = 0.0005f;
        liveHunger = 0.0f;
        bool inputWasPresent = false;
        for (int i = 0; i < numSamples; ++i)
            inputWasPresent = inputWasPresent || std::abs (monoInput[i]) >= 0.0015f;
        updateGhostEcology (numSamples, inputWasPresent);
        return;
    }

    const auto preRollSamples = livePreRoll.getNumSamples();
    const auto requiredQuietSamples = juce::roundToInt (sampleRate * 0.18);
    const auto releaseCoefficient = std::exp (-1.0f / (0.045f * (float) sampleRate));
    const auto noiseCoefficient = 1.0f - std::exp (-1.0f / (1.5f * (float) sampleRate));

    // If several generations die while a continuous phrase is already holding
    // the gate open, shorten any old "satisfied" retry.  This is the population
    // feedback that prevents a once-full universe from remaining inexplicably
    // reluctant after it has become visibly starved.
    if (liveGateOpen && liveCaptureSlot < 0 && liveRetrySamples > 0)
    {
        const auto playing = getPlayingGenerationCount();
        if (playing <= 1)
            liveRetrySamples = juce::jmin (
                liveRetrySamples, juce::roundToInt (sampleRate * 0.65));
        else if (playing <= 2)
            liveRetrySamples = juce::jmin (
                liveRetrySamples, juce::roundToInt (sampleRate * 1.35));
    }

    bool inputWasPresent = false;
    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        if (liveRearmSamples > 0)
            --liveRearmSamples;
        if (liveRetrySamples > 0)
            --liveRetrySamples;
        if (liveCaptureIntervalSamples > 0)
            --liveCaptureIntervalSamples;

        const auto input = monoInput[sampleIndex];
        const auto magnitude = std::abs (input);
        livePreRoll.setSample (0, livePreRollWritePosition, input);
        livePreRollWritePosition = (livePreRollWritePosition + 1) % preRollSamples;
        liveEnvelope = juce::jmax (magnitude, liveEnvelope * releaseCoefficient);

        if (! liveGateOpen)
            liveNoiseFloor += noiseCoefficient
                            * (juce::jmin (magnitude, liveNoiseFloor * 3.0f) - liveNoiseFloor);

        const auto onsetThreshold = juce::jmax (0.0045f, liveNoiseFloor * 4.0f);
        const auto releaseThreshold = juce::jmax (0.0015f, liveNoiseFloor * 2.0f);
        inputWasPresent = inputWasPresent || magnitude >= releaseThreshold;

        bool startedCapture = false;
        const auto freshOnset = ! liveGateOpen
                             && liveRearmSamples <= 0
                             && liveEnvelope >= onsetThreshold;
        if (freshOnset)
        {
            liveGateOpen = true;
            liveQuietSamples = 0;
        }

        // Input is reconsidered at irregular intervals even if it never
        // becomes silent.  A newly opened slot can therefore be fed by a held
        // note, legato guitar line or pad instead of waiting for another onset.
        const auto ongoingOpportunity = liveGateOpen
                                     && liveCaptureSlot < 0
                                     && liveRearmSamples <= 0
                                     && liveRetrySamples <= 0
                                     && liveCaptureIntervalSamples <= 0
                                     && liveEnvelope >= releaseThreshold * 1.15f;
        const auto eligibleFreshOnset = freshOnset && liveCaptureIntervalSamples <= 0;
        if (eligibleFreshOnset || ongoingOpportunity)
        {
            const auto reserved = getGenerationCount();
            const auto playing = getPlayingGenerationCount();
            const auto waiting = juce::jmax (0, reserved - playing);
            const auto effectivePopulation = (float) playing + 0.45f * (float) waiting;

            // At genuine starvation, memory is allowed to win one fifth of the
            // otherwise valid live opportunities.  It never competes while the
            // travelling population is healthy.
            if (getTravellingGenerationCount() <= 1
                && ghostCooldownSamples <= 0
                && nextRandom (ghostRandomState) < 0.20f)
            {
                const auto ghostIndex = chooseReadyGhostMemory();
                if (ghostIndex >= 0 && queueGhostRecall (ghostIndex))
                {
                    const auto reconsiderSeconds = 1.5f
                        + 2.5f * nextRandom (liveRandomState);
                    liveRetrySamples = juce::roundToInt (
                        sampleRate * reconsiderSeconds);
                    continue;
                }
            }

            const auto onsetStrength = juce::jlimit (0.0f, 1.0f,
                (liveEnvelope / juce::jmax (onsetThreshold, 1.0e-6f) - 1.0f) / 4.0f);

            float populationChance = 0.16f;
            if (effectivePopulation < 1.5f)
                populationChance = 0.95f;
            else if (effectivePopulation < 2.5f)
                populationChance = 0.87f;
            else if (effectivePopulation < 3.5f)
                populationChance = 0.73f;
            else if (effectivePopulation < 4.5f)
                populationChance = 0.54f;
            else if (effectivePopulation < 5.5f)
                populationChance = 0.34f;

            constexpr float appetiteTemperament = 0.90f;
            const auto admissionChance = juce::jlimit (0.05f, 0.98f,
                appetiteTemperament * (populationChance
                    + 0.18f * (liveCaptureAppetite - 0.5f)
                    + 0.08f * onsetStrength)
                    + 0.18f * liveHunger);
            const auto admitted = reserved < sourceSlotCount
                               && nextRandom (liveRandomState) < admissionChance;
            const auto claimedSlot = admitted ? claimFreeSlot (SlotState::recording) : -1;
            if (claimedSlot >= 0)
            {
                liveCaptureSlot = claimedSlot;
                auto& slot = sourceSlots[(size_t) liveCaptureSlot];
                slot.numChunks = 0;
                slot.isGhost = false;
                slot.slingshotConsidered = false;
                for (int i = 0; i < preRollSamples; ++i)
                {
                    const auto readPosition = (livePreRollWritePosition + i) % preRollSamples;
                    slot.audio.setSample (0, i, livePreRoll.getSample (0, readPosition));
                }
                liveCaptureWritePosition = preRollSamples;
                // Silence remains the authoritative phrase ending. Hunger only
                // enlarges the maximum mouthful available to sustained input.
                const auto durationFloor = 2.0f + 1.0f * liveHunger;
                const auto durationCeiling = 4.2f + 1.8f * liveHunger;
                const auto captureSeconds = juce::jmap (
                    nextRandom (liveRandomState), durationFloor, durationCeiling);
                liveCaptureTargetSamples = juce::jlimit (
                    preRollSamples + 1, preparedSamples,
                    juce::roundToInt (sampleRate * captureSeconds));
                status.store (Status::recording, std::memory_order_release);
                startedCapture = true;
                liveHunger = juce::jmax (0.0f, liveHunger - 0.72f);
                liveRetrySamples = 0;

                // CHURN also establishes the pace of autonomous ingestion.  The
                // randomized interval is measured start-to-start, so time spent
                // recording is already part of the breath between captures.
                constexpr float absoluteMinimumSeconds = 4.5f;
                const auto minimumSeconds = absoluteMinimumSeconds
                    + 11.0f * std::pow (liveCaptureTimeScale, 1.15f);
                const auto maximumSeconds = minimumSeconds
                    + 3.0f + 11.0f * liveCaptureTimeScale;
                const auto intervalSeconds = juce::jmap (
                    nextRandom (liveRandomState), minimumSeconds, maximumSeconds);
                liveCaptureIntervalSamples = juce::roundToInt (
                    sampleRate * intervalSeconds);
                ghostSafetySpent = false;
            }
            else
            {
                const auto hungerStep = effectivePopulation < 2.5f ? 0.20f : 0.07f;
                liveHunger = juce::jlimit (0.0f, 1.0f, liveHunger + hungerStep);

                float retrySeconds = 1.2f + 2.8f * nextRandom (liveRandomState);
                if (effectivePopulation < 1.5f)
                    retrySeconds = 0.25f + 0.55f * nextRandom (liveRandomState);
                else if (effectivePopulation < 2.5f)
                    retrySeconds = 0.45f + 1.05f * nextRandom (liveRandomState);
                else if (effectivePopulation < 4.5f)
                    retrySeconds = 0.80f + 1.80f * nextRandom (liveRandomState);

                liveRetrySamples = juce::roundToInt (sampleRate * retrySeconds);
            }
        }

        if (liveGateOpen)
        {
            if (liveEnvelope < releaseThreshold)
                ++liveQuietSamples;
            else
                liveQuietSamples = 0;
        }

        if (liveCaptureSlot >= 0 && ! startedCapture
            && liveCaptureWritePosition < liveCaptureTargetSamples)
        {
            sourceSlots[(size_t) liveCaptureSlot].audio.setSample (
                0, liveCaptureWritePosition++, input);
        }

        const auto reachedLimit = liveCaptureSlot >= 0
                               && liveCaptureWritePosition >= liveCaptureTargetSamples;
        const auto phraseEnded = liveGateOpen && liveQuietSamples >= requiredQuietSamples;
        if (! reachedLimit && ! phraseEnded)
            continue;

        if (liveCaptureSlot >= 0)
        {
            auto& slot = sourceSlots[(size_t) liveCaptureSlot];
            const auto retainedTail = juce::roundToInt (sampleRate * 0.03);
            slot.validSamples = reachedLimit
                ? liveCaptureTargetSamples
                : juce::jlimit (1, liveCaptureTargetSamples,
                    liveCaptureWritePosition - liveQuietSamples + retainedTail);
            slot.state.store (SlotState::captured, std::memory_order_release);
            status.store (Status::analysing, std::memory_order_release);
            liveCaptureSlot = -1;
            liveCaptureWritePosition = 0;
            liveCaptureTargetSamples = 0;
        }

        if (phraseEnded)
        {
            liveGateOpen = false;
            liveQuietSamples = 0;
            liveRearmSamples = juce::roundToInt (sampleRate * 0.030);
            liveRetrySamples = 0;
        }
        else if (reachedLimit)
        {
            // A duration-limited capture can be followed by another passage
            // from the same sustain, but only after a short irregular breath.
            const auto breathSeconds = 0.18f + 0.42f * nextRandom (liveRandomState);
            liveRearmSamples = juce::roundToInt (sampleRate * breathSeconds);
            liveRetrySamples = liveRearmSamples;
            liveQuietSamples = 0;
        }
    }

    updateGhostEcology (numSamples, inputWasPresent);
}

int EventHorizonEngine::getTravellingGenerationCount() const noexcept
{
    std::array<bool, sourceSlotCount> travelling {};
    for (const auto& chunk : chunkPool)
    {
        if (chunk.active && ! chunk.plasmaSeed && chunk.sourceSlot >= 0
            && chunk.sourceSlot < sourceSlotCount && chunk.proximity < 0.82f)
            travelling[(size_t) chunk.sourceSlot] = true;
    }

    return (int) std::count (travelling.begin(), travelling.end(), true);
}

bool EventHorizonEngine::hasEmptyGhostMemory() const noexcept
{
    return std::any_of (ghostMemories.begin(), ghostMemories.end(), [] (const auto& memory)
    {
        return memory.state.load (std::memory_order_acquire) == GhostState::empty;
    });
}

int EventHorizonEngine::chooseReadyGhostMemory() noexcept
{
    float totalWeight = 0.0f;
    std::array<float, ghostMemoryCount> weights {};
    for (int i = 0; i < ghostMemoryCount; ++i)
    {
        const auto& memory = ghostMemories[(size_t) i];
        if (memory.state.load (std::memory_order_acquire) != GhostState::ready)
            continue;

        // Old memories become more tempting without making the oldest entry a
        // deterministic choice.  A ten-minute fossil carries about triple weight.
        weights[(size_t) i] = 1.0f + juce::jlimit (0.0f, 2.0f, memory.ageSeconds / 300.0f);
        totalWeight += weights[(size_t) i];
    }

    if (totalWeight <= 0.0f)
        return -1;

    auto selection = nextRandom (ghostRandomState) * totalWeight;
    for (int i = 0; i < ghostMemoryCount; ++i)
    {
        selection -= weights[(size_t) i];
        if (weights[(size_t) i] > 0.0f && selection <= 0.0f)
            return i;
    }

    return -1;
}

bool EventHorizonEngine::queueGhostRecall (int memoryIndex) noexcept
{
    if (memoryIndex < 0 || memoryIndex >= ghostMemoryCount
        || pendingGhostRecallMemory.load (std::memory_order_acquire) >= 0)
        return false;

    const auto targetSlot = claimFreeSlot (SlotState::analysing);
    if (targetSlot < 0)
        return false;

    auto& memory = ghostMemories[(size_t) memoryIndex];
    auto expected = GhostState::ready;
    if (! memory.state.compare_exchange_strong (
            expected, GhostState::queuedForRecall, std::memory_order_acq_rel))
    {
        sourceSlots[(size_t) targetSlot].state.store (SlotState::free,
                                                       std::memory_order_release);
        return false;
    }

    pendingGhostRecallTargetSlot.store (targetSlot, std::memory_order_release);
    pendingGhostRecallMemory.store (memoryIndex, std::memory_order_release);
    ghostCooldownSamples = juce::roundToInt (sampleRate
        * (180.0f + 300.0f * nextRandom (ghostRandomState)));
    ghostArchiveRefillCooldownSamples = juce::roundToInt (sampleRate
        * (5.0f + 15.0f * nextRandom (ghostRandomState)));
    return true;
}

void EventHorizonEngine::updateGhostEcology (int numSamples,
                                              bool inputWasPresent) noexcept
{
    const auto elapsedSeconds = (float) numSamples / (float) sampleRate;
    ghostCooldownSamples = juce::jmax (0, ghostCooldownSamples - numSamples);
    ghostArchiveRefillCooldownSamples = juce::jmax (
        0, ghostArchiveRefillCooldownSamples - numSamples);
    ghostSpontaneousCheckSamples -= numSamples;

    for (auto& memory : ghostMemories)
    {
        const auto state = memory.state.load (std::memory_order_acquire);
        if (state != GhostState::maturing && state != GhostState::ready)
            continue;

        memory.ageSeconds += elapsedSeconds;
        if (state == GhostState::maturing && memory.ageSeconds >= memory.maturitySeconds)
            memory.state.store (GhostState::ready, std::memory_order_release);
    }

    if (inputWasPresent)
        ghostSafetySpent = false;

    const auto travelling = getTravellingGenerationCount();
    const auto canRecall = ghostCooldownSamples <= 0
                        && pendingGhostRecallMemory.load (std::memory_order_acquire) < 0
                        && liveCaptureSlot < 0 && recordingSlot < 0
                        && getGenerationCount() < sourceSlotCount;

    // Starvation safety is a one-shot handover.  It does not interrupt the last
    // survivor: only a completely empty audible universe may consume the stored
    // memory.  The safety latch is reset by genuinely new input, so silence after
    // the recalled Ghost is allowed to become absolute.
    const auto universeEmpty = getGenerationCount() == 0;
    const auto safetyCanRecall = ! inputWasPresent && universeEmpty
                              && ! ghostSafetySpent
                              && pendingGhostRecallMemory.load (
                                     std::memory_order_acquire) < 0
                              && liveCaptureSlot < 0 && recordingSlot < 0;
    if (safetyCanRecall)
    {
        const auto readyGhost = chooseReadyGhostMemory();
        if (readyGhost >= 0 && queueGhostRecall (readyGhost))
        {
            ghostSafetySpent = true;
            return;
        }
    }

    if (ghostSpontaneousCheckSamples <= 0)
    {
        ghostSpontaneousCheckSamples = juce::roundToInt (sampleRate
            * (5.0f + 10.0f * nextRandom (ghostRandomState)));

        if (canRecall && travelling < maxGenerations
            && getPlayingGenerationCount() > 0
            && (inputWasPresent || ! ghostSafetySpent))
        {
            const auto saturation = juce::jlimit (
                0.0f, 1.0f, (float) travelling / (float) maxGenerations);
            const auto scarcity = 1.0f - saturation;
            const auto spontaneousChance = 0.0002f
                + 0.0080f * scarcity * scarcity * scarcity * scarcity;
            if (nextRandom (ghostRandomState) < spontaneousChance)
            {
                const auto memoryIndex = chooseReadyGhostMemory();
                if (memoryIndex >= 0)
                    (void) queueGhostRecall (memoryIndex);
            }
        }
    }
}

void EventHorizonEngine::servicePendingAnalysis()
{
    if (const auto memoryIndex = pendingGhostRecallMemory.exchange (
            -1, std::memory_order_acq_rel); memoryIndex >= 0)
    {
        const auto targetSlot = pendingGhostRecallTargetSlot.exchange (
            -1, std::memory_order_acq_rel);
        if (memoryIndex < ghostMemoryCount && targetSlot >= 0 && targetSlot < sourceSlotCount)
        {
            auto& memory = ghostMemories[(size_t) memoryIndex];
            auto& target = sourceSlots[(size_t) targetSlot];
            target.audio.clear();
            target.audio.copyFrom (0, 0, memory.audio, 0, 0, memory.validSamples);
            target.chunks = memory.chunks;
            target.validSamples = memory.validSamples;
            target.numChunks = memory.numChunks;
            target.isGhost = true;
            target.slingshotConsidered = false;
            target.state.store (SlotState::ready, std::memory_order_release);

            memory.validSamples = 0;
            memory.numChunks = 0;
            memory.ageSeconds = 0.0f;
            memory.state.store (GhostState::empty, std::memory_order_release);
            status.store (Status::ready, std::memory_order_release);
        }
    }

    for (int i = 0; i < sourceSlotCount; ++i)
    {
        auto expected = SlotState::captured;
        if (sourceSlots[(size_t) i].state.compare_exchange_strong (
                expected, SlotState::analysing, std::memory_order_acq_rel))
        {
            analyseSlot (i);
            sourceSlots[(size_t) i].state.store (SlotState::ready, std::memory_order_release);
            status.store (Status::ready, std::memory_order_release);
        }
    }

    const auto archiveMask = pendingGhostArchiveMask.exchange (
        0, std::memory_order_acq_rel);
    for (int slotIndex = 0; slotIndex < sourceSlotCount; ++slotIndex)
    {
        if ((archiveMask & (1u << (uint32_t) slotIndex)) == 0)
            continue;

        auto& source = sourceSlots[(size_t) slotIndex];
        if (source.state.load (std::memory_order_acquire) != SlotState::archiving)
            continue;

        GhostMemory* destination = nullptr;
        for (auto& memory : ghostMemories)
        {
            auto expected = GhostState::empty;
            if (memory.state.compare_exchange_strong (
                    expected, GhostState::writing, std::memory_order_acq_rel))
            {
                destination = &memory;
                break;
            }
        }

        if (destination != nullptr)
        {
            destination->audio.clear();
            destination->audio.copyFrom (0, 0, source.audio, 0, 0, source.validSamples);
            destination->chunks = source.chunks;
            destination->validSamples = source.validSamples;
            destination->numChunks = source.numChunks;
            destination->ageSeconds = 0.0f;
            destination->maturitySeconds = 60.0f
                + 120.0f * nextRandom (ghostServiceRandomState);
            destination->state.store (GhostState::maturing, std::memory_order_release);
        }

        source.validSamples = 0;
        source.numChunks = 0;
        source.isGhost = false;
        source.slingshotConsidered = false;
        source.state.store (SlotState::free, std::memory_order_release);
    }
}

void EventHorizonEngine::analyseSlot (int slotIndex)
{
    auto& slot = sourceSlots[(size_t) slotIndex];
    slot.numChunks = 0;
    slot.slingshotConsidered = false;

    const auto numSamples = slot.validSamples;
    if (numSamples <= 0)
        return;

    const auto* samples = slot.audio.getReadPointer (0);
    const auto frameSize = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));
    const auto numFrames = (numSamples + frameSize - 1) / frameSize;
    std::vector<float> envelope ((size_t) numFrames, 0.0f);
    float peakEnvelope = 0.0f;

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto start = frame * frameSize;
        const auto length = juce::jmin (frameSize, numSamples - start);
        double sumSquares = 0.0;
        for (int i = 0; i < length; ++i)
            sumSquares += (double) samples[start + i] * (double) samples[start + i];

        envelope[(size_t) frame] = (float) std::sqrt (sumSquares / juce::jmax (1, length));
        peakEnvelope = juce::jmax (peakEnvelope, envelope[(size_t) frame]);
    }

    if (peakEnvelope < 0.001f)
        return;

    auto sortedEnvelope = envelope;
    const auto noiseIndex = (size_t) juce::jlimit (0, numFrames - 1, numFrames / 5);
    std::nth_element (sortedEnvelope.begin(), sortedEnvelope.begin() + (ptrdiff_t) noiseIndex,
                      sortedEnvelope.end());
    const auto noiseFloor = sortedEnvelope[noiseIndex];
    const auto enterThreshold = juce::jmax (0.0015f, noiseFloor * 3.0f, peakEnvelope * 0.060f);
    const auto estimatedGap = juce::jmax (0.0010f, noiseFloor * 1.8f, peakEnvelope * 0.045f);
    const auto leaveThreshold = juce::jmin (estimatedGap, enterThreshold * 0.80f);
    const auto minimumSilentFrames = juce::jmax (1, juce::roundToInt (0.040 * sampleRate / frameSize));
    const auto minimumChunkSamples = juce::roundToInt (0.060 * sampleRate);
    const auto paddingSamples = juce::roundToInt (0.010 * sampleRate);

    bool inSound = false;
    int startFrame = 0;
    int quietStartFrame = -1;

    const auto addChunk = [&] (int rawStart, int rawEnd)
    {
        if (slot.numChunks >= maxChunksPerGeneration)
            return;

        const auto start = juce::jmax (0, rawStart - paddingSamples);
        const auto end = juce::jmin (numSamples, rawEnd + paddingSamples);
        const auto length = end - start;
        if (length < minimumChunkSamples)
            return;

        double sumSquares = 0.0;
        for (int i = start; i < end; ++i)
            sumSquares += (double) samples[i] * (double) samples[i];

        auto& chunk = slot.chunks[(size_t) slot.numChunks++];
        chunk.startSample = start;
        chunk.lengthSamples = length;
        chunk.rms = (float) std::sqrt (sumSquares / (double) length);
    };

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto level = envelope[(size_t) frame];
        if (! inSound)
        {
            if (level >= enterThreshold)
            {
                inSound = true;
                startFrame = frame;
                quietStartFrame = -1;
            }
        }
        else if (level < leaveThreshold)
        {
            if (quietStartFrame < 0)
                quietStartFrame = frame;

            if (frame - quietStartFrame + 1 >= minimumSilentFrames)
            {
                addChunk (startFrame * frameSize, quietStartFrame * frameSize);
                inSound = false;
                quietStartFrame = -1;
            }
        }
        else
        {
            quietStartFrame = -1;
        }
    }

    if (inSound)
        addChunk (startFrame * frameSize, numSamples);

    if (slot.numChunks == 0)
        addChunk (0, numSamples);

    float minimumDb = 0.0f;
    float maximumDb = -100.0f;
    for (int i = 0; i < slot.numChunks; ++i)
    {
        const auto db = juce::Decibels::gainToDecibels (slot.chunks[(size_t) i].rms, -80.0f);
        minimumDb = juce::jmin (minimumDb, db);
        maximumDb = juce::jmax (maximumDb, db);
    }

    const auto rangeDb = maximumDb - minimumDb;
    for (int i = 0; i < slot.numChunks; ++i)
    {
        auto& chunk = slot.chunks[(size_t) i];
        const auto db = juce::Decibels::gainToDecibels (chunk.rms, -80.0f);
        const auto normalized = rangeDb > 4.0f
            ? (db - minimumDb) / rangeDb
            : juce::jmap (juce::jlimit (-48.0f, -6.0f, db), -48.0f, -6.0f, 0.0f, 1.0f);
        chunk.gravity = 0.85f + 0.30f
            * std::pow (juce::jlimit (0.0f, 1.0f, normalized), 0.9f);
    }
}

void EventHorizonEngine::requestClear() noexcept
{
    clearCommand.store (true, std::memory_order_release);
}

void EventHorizonEngine::beginActivationIfNeeded() noexcept
{
    if (clearCommand.exchange (false, std::memory_order_acq_rel))
    {
        clearingUniverse = true;
        universeGain.setTargetValue (0.0f);
        return;
    }

    if (clearingUniverse)
        return;

    for (int i = 0; i < sourceSlotCount; ++i)
    {
        if (getPlayingGenerationCount() >= maxGenerations)
            break;

        if (sourceSlots[(size_t) i].state.load (std::memory_order_acquire) == SlotState::ready)
            activateSlot (i);
    }
}

void EventHorizonEngine::completePendingTransitionIfReady() noexcept
{
    if (! clearingUniverse || universeGain.getCurrentValue() > 0.001f)
        return;

    deactivateUniverse();
    recordingCommand.store (-1, std::memory_order_release);
    recordingSlot = -1;
    recordingTargetSamples = juce::jmin (
        preparedSamples, juce::roundToInt (sampleRate * 4.0));
    liveCaptureSlot = -1;
    liveCaptureWritePosition = 0;
    liveCaptureTargetSamples = 0;
    liveGateOpen = false;
    liveQuietSamples = 0;
    liveRearmSamples = 0;
    liveRetrySamples = 0;
    liveCaptureIntervalSamples = 0;
    liveNoiseFloor = 0.0005f;
    liveCaptureTimeScale = 0.45f;
    liveHunger = 0.0f;
    status.store (Status::empty, std::memory_order_release);
    clearingUniverse = false;
    universeGain.setTargetValue (1.0f);
}

void EventHorizonEngine::deactivateUniverse() noexcept
{
    for (auto& chunk : chunkPool)
        chunk = {};
    for (auto& fragment : fragmentPool)
        fragment = {};
    for (auto& radiation : radiationPool)
        radiation = {};

    for (auto& slot : sourceSlots)
    {
        slot.isGhost = false;
        slot.slingshotConsidered = false;
        slot.state.store (SlotState::free, std::memory_order_release);
    }
    for (auto& memory : ghostMemories)
    {
        memory.validSamples = 0;
        memory.numChunks = 0;
        memory.ageSeconds = 0.0f;
        memory.state.store (GhostState::empty, std::memory_order_release);
    }
    pendingGhostArchiveMask.store (0, std::memory_order_release);
    pendingGhostRecallMemory.store (-1, std::memory_order_release);
    pendingGhostRecallTargetSlot.store (-1, std::memory_order_release);
    ghostCooldownSamples = 0;
    ghostArchiveRefillCooldownSamples = 0;
    ghostSpontaneousCheckSamples = juce::roundToInt (sampleRate * 8.0);
    ghostSafetySpent = false;
    gravityReverb.reset();
    shimmerReverb.reset();
    shimmerHpInputL = shimmerHpInputR = 0.0f;
    shimmerHpOutputL = shimmerHpOutputR = 0.0f;
    globalImpactCooldownSeconds = 0.0f;
    ruptureCooldownSeconds = 0.0f;
    reversalCooldownSeconds = 0.0f;
    escapeCooldownSeconds = 0.0f;
    sessionAgeSeconds = 0.0f;
    updateSnapshots();
}

void EventHorizonEngine::activateSlot (int slotIndex) noexcept
{
    const auto shouldPrimePlasma = std::none_of (
        chunkPool.begin(), chunkPool.end(), [] (const auto& chunk) { return chunk.active; });
    auto& source = sourceSlots[(size_t) slotIndex];
    source.state.store (SlotState::active, std::memory_order_release);

    // Cooldowns alone impose certainty.  Outside them every ordinary phrase has
    // a small non-zero chance, with Retrograde intentionally more plausible
    // than an outright parabolic escape.
    const auto sourceFlyby = ! source.isGhost && escapeCooldownSeconds <= 0.0f
                          && nextRandom (rareEventRandomState) < 0.015f;
    const auto sourceRetrograde = ! sourceFlyby && ! source.isGhost
                               && reversalCooldownSeconds <= 0.0f
                               && nextRandom (rareEventRandomState) < 0.045f;
    const auto escapeDirection = nextRandom (rareEventRandomState) < 0.5f ? -1.0f : 1.0f;
    const auto escapeStartAngle = escapeDirection > 0.0f
        ? -juce::MathConstants<float>::halfPi
        : juce::MathConstants<float>::halfPi;
    const auto sourceDurationSeconds = (float) source.validSamples / (float) sampleRate;

    if (sourceFlyby)
        escapeCooldownSeconds = 240.0f + 240.0f * nextRandom (rareEventRandomState);
    if (sourceRetrograde)
        reversalCooldownSeconds = 120.0f + 120.0f * nextRandom (rareEventRandomState);

    for (int i = 0; i < source.numChunks; ++i)
    {
        auto runtimeIterator = std::find_if (chunkPool.begin(), chunkPool.end(),
                                             [] (const auto& chunk) { return ! chunk.active; });
        if (runtimeIterator == chunkPool.end())
            break;

        auto& runtime = *runtimeIterator;
        runtime.active = true;
        runtime.sourceSlot = slotIndex;
        runtime.definition = source.chunks[(size_t) i];
        runtime.proximity = 0.0f;
        const auto phraseCentre = twoPi * (0.17f + 0.31f * (float) slotIndex);
        const auto phraseOffset = ((float) i - 0.5f * (float) (source.numChunks - 1)) * 0.085f;
        runtime.angle = phraseCentre + phraseOffset;
        runtime.randomState = 0x9e3779b9u ^ (uint32_t) (i * 7919 + slotIndex * 104729);
        auto subgroupState = 0x85ebca6bu
                           ^ (uint32_t) ((i / 2) * 2246822519u + slotIndex * 3266489917u);
        const auto subgroupRate = 0.88f + 0.24f * nextRandom (subgroupState);
        runtime.orbitRateScale = subgroupRate
                               + 0.03f * (nextRandom (runtime.randomState) - 0.5f);
        runtime.horizonDwellDuration = 10.0f
            * (0.80f + 0.40f * nextRandom (runtime.randomState))
            * (1.0f - 0.10f * (runtime.definition.gravity - 1.0f));
        runtime.horizonTextureScale = 0.82f + 0.36f * nextRandom (runtime.randomState);
        runtime.orbitHz = 0.05f;
        runtime.orbitDirection = sourceRetrograde ? -1.0f : 1.0f;
        runtime.impactCooldownSeconds = 0.0f;
        runtime.impactGlow = 0.0f;
        runtime.playbackRate = 1.0f;
        runtime.visualScale = 1.0f;
        runtime.horizonDwellSeconds = 0.0f;
        runtime.escapeProgress = 0.0f;
        runtime.escapeDurationSeconds = sourceDurationSeconds + 2.8f;
        runtime.escapeStartAngle = escapeStartAngle + phraseOffset;
        runtime.escapeDirection = escapeDirection;
        runtime.plasmaSeed = false;
        runtime.collapsing = false;
        runtime.radiationEmitted = false;
        runtime.retrograde = sourceRetrograde;
        runtime.parabolicEscape = sourceFlyby;
        runtime.escapeFromHorizon = false;
        runtime.collisionShard = false;
        runtime.samplesUntilLaunch = sourceRetrograde
            ? (double) juce::jmax (0, source.validSamples
                - runtime.definition.startSample - runtime.definition.lengthSamples)
            : (double) runtime.definition.startSample;
        runtime.sourceCursor = 0;
        runtime.launchCount = 0;
    }

    // An empty universe receives a small, accelerated echo of the first phrase.
    // The ordinary cohort remains untouched, so recognisable outer memory and a
    // quickly established plasma bed can coexist without duplicating every chunk.
    if (shouldPrimePlasma && ! source.isGhost && ! sourceFlyby && source.numChunks > 0)
    {
        const auto seedCount = juce::jmin (maxBootstrapChunks, source.numChunks);
        for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
        {
            const auto chunkIndex = seedCount == 1 ? 0 : juce::roundToInt (
                (float) seedIndex * (float) (source.numChunks - 1) / (float) (seedCount - 1));
            const auto& definition = source.chunks[(size_t) chunkIndex];
            auto original = std::find_if (chunkPool.begin(), chunkPool.end(),
                [slotIndex, &definition] (const auto& chunk)
                {
                    return chunk.active && ! chunk.plasmaSeed && chunk.sourceSlot == slotIndex
                        && chunk.definition.startSample == definition.startSample;
                });
            auto seed = std::find_if (chunkPool.begin(), chunkPool.end(),
                                      [] (const auto& chunk) { return ! chunk.active; });
            if (original == chunkPool.end() || seed == chunkPool.end())
                break;

            *seed = *original;
            seed->proximity = 0.58f + 0.035f * (float) seedIndex;
            seed->angle += 0.22f + 0.09f * (float) seedIndex;
            seed->orbitRateScale *= 1.04f + 0.03f * (float) seedIndex;
            seed->plasmaSeed = true;
            seed->launchCount = 4;
            seed->randomState ^= 0x27d4eb2du * (uint32_t) (seedIndex + 1);
        }
    }

    status.store (Status::playing, std::memory_order_release);
    updateSnapshots();
}

float EventHorizonEngine::nextRandom (uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float) (state & 0x00ffffffu) / (float) 0x01000000u;
}

void EventHorizonEngine::reverseGeneration (int sourceSlot) noexcept
{
    for (auto& chunk : chunkPool)
    {
        if (! chunk.active || chunk.sourceSlot != sourceSlot || chunk.parabolicEscape)
            continue;

        chunk.retrograde = ! chunk.retrograde;
        chunk.orbitDirection *= -1.0f;
        chunk.impactGlow = juce::jmax (chunk.impactGlow, 0.85f);
    }
}

bool EventHorizonEngine::launchGenerationSlingshot (int sourceSlot) noexcept
{
    if (! juce::isPositiveAndBelow (sourceSlot, sourceSlotCount))
        return false;

    auto& source = sourceSlots[(size_t) sourceSlot];
    const auto direction = nextRandom (rareEventRandomState) < 0.5f ? -1.0f : 1.0f;
    const auto duration = 2.8f + (float) source.validSamples / (float) sampleRate
                        + 1.4f * nextRandom (rareEventRandomState);
    bool launched = false;

    for (auto& chunk : chunkPool)
    {
        if (! chunk.active || chunk.sourceSlot != sourceSlot)
            continue;

        chunk.parabolicEscape = true;
        chunk.escapeFromHorizon = true;
        chunk.escapeProgress = 0.0f;
        chunk.escapeDurationSeconds = duration;
        chunk.escapeStartAngle = chunk.angle;
        chunk.escapeDirection = direction;
        chunk.retrograde = true;
        chunk.orbitDirection = direction;
        chunk.collapsing = false;
        chunk.radiationEmitted = true;
        chunk.sourceCursor = 0;
        chunk.launchCount = 0;
        chunk.samplesUntilLaunch = (double) juce::jmax (0, source.validSamples
            - chunk.definition.startSample - chunk.definition.lengthSamples);
        chunk.impactGlow = 1.0f;
        launched = true;
    }

    if (launched)
        escapeCooldownSeconds = 240.0f + 240.0f * nextRandom (rareEventRandomState);
    return launched;
}

void EventHorizonEngine::launchFragment (ChunkRuntime& chunk, float dispersion,
                                          int materialSourceSlot,
                                          const ChunkDefinition* materialDefinition) noexcept
{
    FragmentVoice* freeVoice = nullptr;
    for (auto& voice : fragmentPool)
    {
        if (! voice.active)
        {
            freeVoice = &voice;
            break;
        }
    }

    if (freeVoice == nullptr)
        return;

    const auto proximity = chunk.proximity;
    const auto sourceSlot = materialSourceSlot >= 0 ? materialSourceSlot : chunk.sourceSlot;
    const auto& definition = materialDefinition != nullptr ? *materialDefinition : chunk.definition;
    auto start = definition.startSample;
    auto length = definition.lengthSamples;

    if (! chunk.parabolicEscape && proximity >= 0.38f)
    {
        const auto granular = proximity >= 0.72f;
        const auto zonePosition = granular
            ? juce::jlimit (0.0f, 1.0f, (proximity - 0.72f) / 0.28f)
            : juce::jlimit (0.0f, 1.0f, (proximity - 0.38f) / 0.34f);
        auto milliseconds = granular
            ? juce::jmap (zonePosition, 120.0f, 20.0f)
            : juce::jmap (zonePosition, 600.0f, 100.0f);
        if (granular && proximity > 0.90f)
        {
            const auto horizonBlend = juce::jlimit (0.0f, 1.0f, (proximity - 0.90f) / 0.065f);
            const auto horizonLength = 160.0f * chunk.horizonTextureScale;
            milliseconds = juce::jmap (horizonBlend, milliseconds, horizonLength);
        }
        length = juce::jlimit (juce::jmin (64, definition.lengthSamples), definition.lengthSamples,
                               juce::roundToInt (milliseconds * 0.001 * sampleRate));

        const auto travel = juce::jmax (1, definition.lengthSamples - length);
        const auto randomOffset = (nextRandom (chunk.randomState) * 2.0f - 1.0f)
                                * dispersion * (float) travel * (granular ? 0.7f : 0.18f);
        const auto cursor = chunk.sourceCursor + juce::roundToInt (randomOffset);
        start += juce::jlimit (0, travel, cursor);
        chunk.sourceCursor = (chunk.sourceCursor + juce::jmax (1, length * 3 / 4)) % travel;

        const auto chaosPosition = juce::jlimit (0.0f, 1.0f,
            (proximity - 0.68f) / (0.955f - 0.68f));
        const auto prePlasmaChaos = std::sin (juce::MathConstants<float>::pi * chaosPosition);
        const auto omissionChance = (granular ? 0.025f + 0.20f * prePlasmaChaos : 0.06f)
                                  * dispersion;
        if (nextRandom (chunk.randomState) < omissionChance)
            return;
    }

    const auto spatialWidth = 0.10f + 0.80f * std::pow (1.0f - proximity, 1.3f);
    const auto parentPan = std::sin (chunk.angle) * spatialWidth;
    const auto breakupPosition = juce::jlimit (0.0f, 1.0f, (proximity - 0.30f) / 0.62f);
    const auto breakupEnvelope = std::sin (juce::MathConstants<float>::pi * breakupPosition);
    const auto breakupSpread = (nextRandom (chunk.randomState) * 2.0f - 1.0f)
                             * dispersion * 0.12f * breakupEnvelope;
    const auto pan = juce::jlimit (-0.95f, 0.95f, parentPan + breakupSpread);
    const auto panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const auto cutoff = chunk.parabolicEscape
        ? 18000.0f
        : 300.0f + 17700.0f * std::pow (1.0f - proximity, 2.2f);

    *freeVoice = {};
    freeVoice->active = true;
    freeVoice->sourceSlot = sourceSlot;
    freeVoice->parentChunk = (int) (&chunk - chunkPool.data());
    freeVoice->startSample = start;
    freeVoice->lengthSamples = length;
    freeVoice->sourcePosition = 0.0f;
    freeVoice->playbackRate = chunk.playbackRate;
    freeVoice->reversed = chunk.retrograde
                       || (proximity > 0.72f
                           && nextRandom (chunk.randomState) < 0.045f * dispersion);
    const auto earlyPosition = juce::jlimit (0.0f, 1.0f, (float) chunk.launchCount / 4.0f);
    const auto earlyCurve = earlyPosition * earlyPosition * (3.0f - 2.0f * earlyPosition);
    const auto latePosition = juce::jlimit (0.0f, 1.0f,
        (proximity - 0.18f) / (0.965f - 0.18f));
    const auto fallGain = 1.0f - 0.25f * earlyCurve
                        - 0.50f * std::pow (latePosition, 1.35f);
    const auto deathFade = proximity < 0.97f
        ? 1.0f
        : juce::jlimit (0.0f, 1.0f, (1.0f - proximity) / 0.03f);
    const auto chunkCount = (float) juce::jmax (
        1, sourceSlots[(size_t) sourceSlot].numChunks);
    const auto overlapPosition = juce::jlimit (0.0f, 1.0f,
        (proximity - 0.20f) / 0.45f);
    const auto overlapCurve = overlapPosition * overlapPosition
                            * (3.0f - 2.0f * overlapPosition);
    const auto chunkNormalization = 1.0f
        + overlapCurve * (std::sqrt (chunkCount) - 1.0f);
    const auto plasmaPosition = juce::jlimit (0.0f, 1.0f,
        (proximity - 0.72f) / (0.965f - 0.72f));
    const auto plasmaCurve = plasmaPosition * plasmaPosition
                           * (3.0f - 2.0f * plasmaPosition);
    const auto safeSourceRms = juce::jlimit (0.006f, 0.50f, definition.rms);
    const auto targetPlasmaRms = 0.080f;
    const auto fullPlasmaNormalization = juce::jlimit (
        0.20f, 4.0f, targetPlasmaRms / safeSourceRms);
    const auto plasmaNormalization = std::pow (fullPlasmaNormalization, plasmaCurve);
    const auto baseGain = 0.72f * fallGain * deathFade
                        * plasmaNormalization
                        / chunkNormalization
                        / std::sqrt ((float) juce::jmax (1, getPlayingGenerationCount()));
    freeVoice->baseGain = baseGain;
    freeVoice->panOffset = breakupSpread;
    freeVoice->gainL = baseGain * std::cos (panAngle);
    freeVoice->gainR = baseGain * std::sin (panAngle);
    const auto fieldProximity = juce::jlimit (0.0f, 1.0f, proximity / 0.965f);
    const auto fieldCurve = fieldProximity * fieldProximity * (3.0f - 2.0f * fieldProximity);
    freeVoice->fieldSend = 0.20f + 0.80f * fieldCurve;
    const auto shimmerProximity = juce::jlimit (0.0f, 1.0f, (proximity - 0.90f) / 0.065f);
    freeVoice->shimmerSend = chunk.parabolicEscape ? 0.0f
        : 0.075f * shimmerProximity * shimmerProximity
          * (3.0f - 2.0f * shimmerProximity);
    freeVoice->filterCoefficient = 1.0f - std::exp (-twoPi * cutoff / (float) sampleRate);

    // Preserve the narrow, identifiable plasma voice.  Selected inner grains
    // additionally shed a quiet, steep high-passed corona into the wider field.
    const auto coronaPosition = juce::jlimit (0.0f, 1.0f,
        (proximity - 0.82f) / (0.965f - 0.82f));
    if (! chunk.parabolicEscape && coronaPosition > 0.0f
        && nextRandom (chunk.randomState) < dispersion * (0.10f + 0.24f * coronaPosition))
    {
        const auto coronaPanMagnitude = 0.68f + 0.27f * nextRandom (chunk.randomState);
        const auto coronaPan = nextRandom (chunk.randomState) < 0.5f
            ? -coronaPanMagnitude : coronaPanMagnitude;
        const auto coronaPanAngle = (coronaPan + 1.0f)
                                  * juce::MathConstants<float>::pi * 0.25f;
        const auto coronaGain = baseGain * coronaPosition
                              * (0.10f + 0.10f * nextRandom (chunk.randomState));
        const auto coronaCutoff = 1200.0f + 2600.0f * nextRandom (chunk.randomState);
        freeVoice->coronaActive = true;
        freeVoice->coronaGainL = coronaGain * std::cos (coronaPanAngle);
        freeVoice->coronaGainR = coronaGain * std::sin (coronaPanAngle);
        freeVoice->coronaFilterCoefficient = 1.0f
            - std::exp (-twoPi * coronaCutoff / (float) sampleRate);
    }

    if (! chunk.parabolicEscape && proximity > 0.42f && dispersion > 0.0f)
    {
        const auto damage = juce::jlimit (0.0f, 1.0f, (proximity - 0.42f) / 0.55f);
        for (int hole = 0; hole < (int) freeVoice->holeStarts.size(); ++hole)
        {
            const auto chaosPosition = juce::jlimit (0.0f, 1.0f,
                (proximity - 0.68f) / (0.955f - 0.68f));
            const auto prePlasmaChaos = std::sin (
                juce::MathConstants<float>::pi * chaosPosition);
            const auto chance = dispersion * damage
                              * ((hole == 0 ? 0.48f : 0.20f)
                                 + (hole == 0 ? 0.22f : 0.12f) * prePlasmaChaos);
            if (nextRandom (chunk.randomState) >= chance)
                continue;

            const auto baseHoleMs = juce::jmap (damage, 60.0f, 7.0f);
            const auto variedHoleMs = (baseHoleMs + 24.0f * prePlasmaChaos)
                                    * (0.65f + 0.70f * nextRandom (chunk.randomState));
            const auto maximumHole = juce::jmax (1, freeVoice->lengthSamples / 3);
            const auto holeLength = juce::jlimit (1, maximumHole,
                juce::roundToInt (variedHoleMs * 0.001 * sampleRate));
            const auto availableStart = juce::jmax (1, freeVoice->lengthSamples - holeLength);
            freeVoice->holeStarts[(size_t) hole] = juce::roundToInt (
                nextRandom (chunk.randomState) * (float) (availableStart - 1));
            freeVoice->holeLengths[(size_t) hole] = holeLength;
        }
    }
}

void EventHorizonEngine::renderOneSample (float& left, float& right,
                                          float& fieldLeft, float& fieldRight,
                                          float& shimmerLeft, float& shimmerRight) noexcept
{
    for (auto& voice : fragmentPool)
    {
        if (! voice.active)
            continue;

        if (voice.sourcePosition >= (float) voice.lengthSamples)
        {
            voice.active = false;
            continue;
        }

        const auto forwardPosition = juce::jlimit (
            0.0f, (float) juce::jmax (0, voice.lengthSamples - 1), voice.sourcePosition);
        const auto readPosition = voice.reversed
            ? (float) (voice.lengthSamples - 1) - forwardPosition
            : forwardPosition;
        const auto firstSample = juce::jlimit (
            0, voice.lengthSamples - 1, (int) std::floor (readPosition));
        const auto secondSample = juce::jmin (voice.lengthSamples - 1, firstSample + 1);
        const auto fraction = readPosition - (float) firstSample;
        const auto* source = sourceSlots[(size_t) voice.sourceSlot].audio.getReadPointer (
            0, voice.startSample);
        const auto sample = source[firstSample]
                          + fraction * (source[secondSample] - source[firstSample]);
        const auto edgeSeconds = voice.lengthSamples > juce::roundToInt (sampleRate * 0.10)
            ? 0.015
            : 0.005;
        const auto fadeLength = juce::jmax (1, juce::jmin (juce::roundToInt (sampleRate * edgeSeconds),
                                                          voice.lengthSamples / 4));
        const auto fadeIn = juce::jmin (1.0f, (float) voice.ageSamples / (float) fadeLength);
        const auto fadeOut = juce::jmin (1.0f,
            ((float) voice.lengthSamples - voice.sourcePosition) / (float) fadeLength);
        const auto envelope = juce::jmax (0.0f, juce::jmin (fadeIn, fadeOut));
        float holeGate = 1.0f;
        for (int hole = 0; hole < (int) voice.holeStarts.size(); ++hole)
        {
            const auto holeStart = voice.holeStarts[(size_t) hole];
            const auto holeLength = voice.holeLengths[(size_t) hole];
            const auto relativeAge = juce::roundToInt (voice.sourcePosition) - holeStart;
            if (holeStart < 0 || relativeAge < 0 || relativeAge >= holeLength)
                continue;

            const auto edge = juce::jmax (1, juce::jmin (juce::roundToInt (sampleRate * 0.002),
                                                         holeLength / 3));
            if (relativeAge < edge)
                holeGate = juce::jmin (holeGate, 1.0f - (float) relativeAge / (float) edge);
            else if (relativeAge >= holeLength - edge)
                holeGate = juce::jmin (holeGate,
                    (float) (relativeAge - (holeLength - edge)) / (float) edge);
            else
                holeGate = 0.0f;
        }

        const auto shapedSample = sample * envelope * holeGate;
        const auto inputL = shapedSample * voice.gainL;
        const auto inputR = shapedSample * voice.gainR;
        voice.filterStateL += voice.filterCoefficient * (inputL - voice.filterStateL);
        voice.filterStateR += voice.filterCoefficient * (inputR - voice.filterStateR);
        left += voice.filterStateL;
        right += voice.filterStateR;
        fieldLeft += voice.filterStateL * voice.fieldSend;
        fieldRight += voice.filterStateR * voice.fieldSend;
        shimmerLeft += voice.filterStateL * voice.shimmerSend;
        shimmerRight += voice.filterStateR * voice.shimmerSend;
        if (voice.coronaActive)
        {
            voice.coronaFilterState1 += voice.coronaFilterCoefficient
                                      * (shapedSample - voice.coronaFilterState1);
            const auto firstHighPass = shapedSample - voice.coronaFilterState1;
            voice.coronaFilterState2 += voice.coronaFilterCoefficient
                                      * (firstHighPass - voice.coronaFilterState2);
            const auto corona = firstHighPass - voice.coronaFilterState2;
            left += corona * voice.coronaGainL;
            right += corona * voice.coronaGainR;
        }
        voice.gainL += voice.gainStepL;
        voice.gainR += voice.gainStepR;
        voice.sourcePosition += voice.playbackRate;
        ++voice.ageSamples;
    }
}

void EventHorizonEngine::launchRadiation (ChunkRuntime& chunk) noexcept
{
    auto voice = std::find_if (radiationPool.begin(), radiationPool.end(),
                               [] (const auto& candidate) { return ! candidate.active; });
    if (voice == radiationPool.end())
        return;

    const auto& definition = chunk.definition;
    // Most deaths remain brief crystalline punctuation.  A minority become
    // larger ruptures with twice the source-bearing life and causal pre-delay.
    const auto isLongBloom = nextRandom (chunk.randomState) < 0.20f;
    const auto bloomScale = isLongBloom ? 2.0f : 1.0f;
    const auto desiredLength = juce::roundToInt (sampleRate
        * bloomScale * (0.16f + 0.16f * nextRandom (chunk.randomState)));
    const auto length = juce::jlimit (juce::jmin (32, definition.lengthSamples),
                                      definition.lengthSamples, desiredLength);
    const auto travel = juce::jmax (0, definition.lengthSamples - length);

    *voice = {};
    voice->active = true;
    voice->sourceSlot = chunk.sourceSlot;
    voice->startSample = definition.startSample
                       + juce::roundToInt (nextRandom (chunk.randomState) * (float) travel);
    voice->lengthSamples = length;
    voice->delaySamples = juce::roundToInt (sampleRate
        * bloomScale * (0.050f + 0.100f * nextRandom (chunk.randomState)));
    voice->pitchRatio = nextRandom (chunk.randomState) < 0.72f ? 2.0f : 1.5f;
    voice->targetPan = nextRandom (chunk.randomState) * 2.0f - 1.0f;
    voice->gain = (0.34f + 0.30f * nextRandom (chunk.randomState))
                * (0.72f + 0.28f * definition.gravity);
}

void EventHorizonEngine::renderRadiationSample (float& shimmerLeft,
                                                 float& shimmerRight) noexcept
{
    for (auto& voice : radiationPool)
    {
        if (! voice.active)
            continue;

        if (voice.delaySamples > 0)
        {
            --voice.delaySamples;
            continue;
        }

        if (voice.sourcePosition >= (float) juce::jmax (1, voice.lengthSamples - 1))
        {
            voice.active = false;
            continue;
        }

        const auto index = (int) voice.sourcePosition;
        const auto fraction = voice.sourcePosition - (float) index;
        const auto next = juce::jmin (index + 1, voice.lengthSamples - 1);
        const auto* source = sourceSlots[(size_t) voice.sourceSlot].audio.getReadPointer (
            0, voice.startSample);
        const auto sample = source[index] + fraction * (source[next] - source[index]);
        const auto progress = voice.sourcePosition / (float) juce::jmax (1, voice.lengthSamples - 1);
        const auto envelope = std::sin (juce::MathConstants<float>::pi * progress);
        const auto outward = progress * progress * (3.0f - 2.0f * progress);
        const auto pan = voice.targetPan * outward;
        const auto panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto value = sample * envelope * voice.gain;
        shimmerLeft += value * std::cos (panAngle);
        shimmerRight += value * std::sin (panAngle);
        voice.sourcePosition += voice.pitchRatio;
    }
}

void EventHorizonEngine::process (juce::AudioBuffer<float>& output,
                                  float gravityScale, float dispersion,
                                  float fieldAmount, float shimmerAmount,
                                  float cycleSeconds, float dwellSeconds) noexcept
{
    beginActivationIfNeeded();
    completePendingTransitionIfReady();

    gravityScale = juce::jlimit (0.1f, 4.0f, gravityScale);
    dispersion = juce::jlimit (0.0f, 1.0f, dispersion);
    fieldAmount = juce::jlimit (0.0f, 1.0f, fieldAmount);
    shimmerAmount = juce::jlimit (0.0f, 1.0f, shimmerAmount);
    cycleSeconds = juce::jlimit (4.0f, 20.0f, cycleSeconds);
    dwellSeconds = juce::jlimit (4.0f, 120.0f, dwellSeconds);
    const auto blockSeconds = (float) output.getNumSamples() / (float) sampleRate;
    sessionAgeSeconds += blockSeconds;
    globalImpactCooldownSeconds = juce::jmax (
        0.0f, globalImpactCooldownSeconds - blockSeconds);
    ruptureCooldownSeconds = juce::jmax (
        0.0f, ruptureCooldownSeconds - blockSeconds);
    reversalCooldownSeconds = juce::jmax (
        0.0f, reversalCooldownSeconds - blockSeconds);
    escapeCooldownSeconds = juce::jmax (
        0.0f, escapeCooldownSeconds - blockSeconds);

    float targetFieldExpansion = 0.0f;
    for (const auto& chunk : chunkPool)
        if (chunk.active)
            targetFieldExpansion = juce::jmax (targetFieldExpansion,
                juce::jlimit (0.0f, 1.0f, (chunk.proximity - 0.88f) / 0.085f));
    const auto fieldSmoothing = 1.0f - std::exp (-blockSeconds / 0.45f);
    horizonFieldExpansion += fieldSmoothing * (targetFieldExpansion - horizonFieldExpansion);

    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = 0.92f + 0.06f * horizonFieldExpansion;
    reverbParameters.damping = 0.68f - 0.10f * horizonFieldExpansion;
    reverbParameters.wetLevel = 0.52f + 0.12f * horizonFieldExpansion;
    reverbParameters.dryLevel = 0.0f;
    reverbParameters.width = 0.82f + 0.17f * horizonFieldExpansion;
    reverbParameters.freezeMode = 0.0f;
    gravityReverb.setParameters (reverbParameters);

    for (auto& chunk : chunkPool)
    {
        if (! chunk.active)
            continue;

        chunk.impactCooldownSeconds = juce::jmax (
            0.0f, chunk.impactCooldownSeconds - blockSeconds);
        chunk.impactGlow *= std::exp (-blockSeconds / 0.28f);

        constexpr float horizonBoundary = 0.965f;
        if (chunk.parabolicEscape)
        {
            chunk.escapeProgress = juce::jmin (1.0f, chunk.escapeProgress
                + blockSeconds / juce::jmax (0.25f, chunk.escapeDurationSeconds));
            const auto t = chunk.escapeProgress;
            const auto smooth = t * t * (3.0f - 2.0f * t);

            if (chunk.escapeFromHorizon)
            {
                // A terminal slingshot curls around the rim, then loses the
                // black hole's spatial narrowing as it accelerates outward.
                chunk.proximity = horizonBoundary * (1.0f - smooth);
                chunk.angle = chunk.escapeStartAngle
                            + chunk.escapeDirection * 0.72f
                              * juce::MathConstants<float>::pi * smooth;
            }
            else
            {
                // A flyby crosses the complete stereo field on one continuous
                // parabolic arc and never enters the destructive inner zones.
                chunk.proximity = 0.62f * std::pow (
                    juce::jmax (0.0f, std::sin (juce::MathConstants<float>::pi * t)), 0.78f);
                chunk.angle = chunk.escapeStartAngle
                            + chunk.escapeDirection * juce::MathConstants<float>::pi * smooth;
            }

            chunk.orbitHz = chunk.escapeDirection
                * 0.5f / juce::jmax (0.25f, chunk.escapeDurationSeconds);
            if (t >= 1.0f)
                chunk.active = false;
            continue;
        }

        if (chunk.collapsing)
        {
            const auto collapseRate = gravityScale * (0.18f + 0.12f * chunk.definition.gravity);
            chunk.proximity += collapseRate * blockSeconds;
        }
        else if (chunk.proximity >= horizonBoundary)
        {
            chunk.proximity = horizonBoundary;
            const auto dwellClockRate = gravityScale * (0.80f + 0.40f * chunk.definition.gravity);
            chunk.horizonDwellSeconds += blockSeconds * dwellClockRate;
            const auto scaledDwellDuration = chunk.horizonDwellDuration * dwellSeconds / 10.0f;
            if (chunk.horizonDwellSeconds >= scaledDwellDuration)
                chunk.collapsing = true;
        }
        else
        {
            const auto repetitionPosition = juce::jlimit (0.0f, 1.0f,
                (float) chunk.launchCount / 4.0f);
            const auto repetitionMaturity = repetitionPosition * repetitionPosition
                                          * (3.0f - 2.0f * repetitionPosition);
            const auto fallRate = gravityScale
                                * 0.038f * chunk.definition.gravity
                                * (1.0f + 0.75f * chunk.proximity)
                                * (0.18f + 0.82f * repetitionMaturity)
                                * (chunk.plasmaSeed ? 5.0f : 1.0f);
            chunk.proximity = juce::jmin (horizonBoundary,
                                          chunk.proximity + fallRate * blockSeconds);
        }
        const auto orbitCurve = std::pow (chunk.proximity, 1.12f);
        const auto cohesionRelease = juce::jlimit (0.0f, 1.0f,
            (chunk.proximity - 0.18f) / 0.42f);
        const auto effectiveOrbitScale = 1.0f
            + cohesionRelease * (chunk.orbitRateScale - 1.0f);
        const auto orbitHz = juce::jlimit (0.05f, 10.0f,
            0.05f * std::pow (200.0f, orbitCurve) * effectiveOrbitScale);
        chunk.orbitHz = orbitHz;
        chunk.angle += chunk.orbitDirection * blockSeconds * twoPi * orbitHz;

        if (chunk.proximity >= 1.0f)
        {
            auto& dyingSource = sourceSlots[(size_t) chunk.sourceSlot];
            if (! dyingSource.slingshotConsidered)
            {
                dyingSource.slingshotConsidered = true;
                const auto maySlingshot = ! dyingSource.isGhost
                                        && escapeCooldownSeconds <= 0.0f;
                if (maySlingshot && nextRandom (rareEventRandomState) < 0.010f
                    && launchGenerationSlingshot (chunk.sourceSlot))
                    continue;
            }

            if (! chunk.radiationEmitted)
            {
                launchRadiation (chunk);
                chunk.radiationEmitted = true;
            }
            chunk.active = false;
        }
    }

    // Intact outer bodies from different recordings may graze one another.
    // Impacts can exchange material and momentum; strong test-build contacts
    // can reverse an entire phrase-family before it has broken into grains.
    if (globalImpactCooldownSeconds <= 0.0f)
    {
        constexpr float horizonBoundary = 0.965f;
        bool impactOccurred = false;
        for (int firstIndex = 0; firstIndex < maxChunks && ! impactOccurred; ++firstIndex)
        {
            auto& first = chunkPool[(size_t) firstIndex];
            if (! first.active || first.collapsing || first.parabolicEscape
                || first.proximity < 0.08f || first.proximity > 0.52f
                || first.impactCooldownSeconds > 0.0f)
                continue;

            for (int secondIndex = firstIndex + 1; secondIndex < maxChunks; ++secondIndex)
            {
                auto& second = chunkPool[(size_t) secondIndex];
                if (! second.active || second.collapsing || second.parabolicEscape
                    || second.proximity < 0.08f || second.proximity > 0.52f
                    || second.impactCooldownSeconds > 0.0f)
                    continue;

                const auto samePhrase = first.sourceSlot == second.sourceSlot;
                // A newly captured string of pearls remains coherent at first.
                // Once it has begun to loosen, its own members may jostle too.
                if (samePhrase && juce::jmin (first.proximity, second.proximity) < 0.20f)
                    continue;

                const auto radialDistance = std::abs (first.proximity - second.proximity);
                const auto angularDistance = std::abs (std::remainder (
                    first.angle - second.angle, twoPi));
                if (radialDistance > 0.055f || angularDistance > 0.18f)
                    continue;

                const auto closeness = (1.0f - radialDistance / 0.055f)
                                     * (1.0f - angularDistance / 0.18f);
                const auto relativeSpeed = juce::jlimit (0.0f, 1.0f,
                    std::abs (first.orbitDirection * first.orbitHz
                            - second.orbitDirection * second.orbitHz) / 2.0f);
                const auto rawImpactChance = (0.08f + 0.38f * closeness)
                                           * (0.65f + 0.35f * relativeSpeed)
                                           * (0.65f + 0.35f * dispersion)
                                           * (samePhrase ? 0.32f : 1.0f);
                const auto impactChance = juce::jlimit (
                    samePhrase ? 0.012f : 0.06f,
                    samePhrase ? 0.18f : 0.55f, rawImpactChance);
                if (nextRandom (first.randomState) >= impactChance)
                    continue;

                // A rare near-central cross-phrase hit destroys the lighter
                // body and divides the heavier one's remaining identity into
                // normal-speed and half-speed descendants.  The victim's
                // runtime is safely reused for the second descendant, so the
                // fixed real-time pools remain bounded even at full capacity.
                const auto directHit = ! samePhrase && closeness > 0.86f;
                const auto ruptureChance = 0.18f + 0.18f
                    * juce::jlimit (0.0f, 1.0f, (closeness - 0.86f) / 0.14f);
                if (directHit && ruptureCooldownSeconds <= 0.0f
                    && nextRandom (first.randomState) < ruptureChance)
                {
                    auto* survivor = &first;
                    auto* victim = &second;
                    if (second.definition.gravity > first.definition.gravity
                        || (std::abs (second.definition.gravity - first.definition.gravity) < 0.01f
                            && nextRandom (first.randomState) < 0.5f))
                    {
                        survivor = &second;
                        victim = &first;
                    }

                    auto descendant = *survivor;
                    survivor->collisionShard = true;
                    survivor->visualScale = 0.62f;
                    survivor->playbackRate = 1.0f;
                    survivor->angle -= 0.075f;
                    survivor->orbitRateScale *= 0.94f;
                    survivor->samplesUntilLaunch = 0.0;
                    survivor->launchCount = 0;
                    survivor->impactGlow = 1.0f;
                    survivor->impactCooldownSeconds = 9.0f;

                    *victim = descendant;
                    victim->collisionShard = true;
                    victim->visualScale = 0.50f;
                    victim->playbackRate = 0.5f;
                    victim->angle += 0.11f;
                    victim->proximity = juce::jmax (0.03f, victim->proximity - 0.018f);
                    victim->orbitRateScale *= 1.07f;
                    victim->samplesUntilLaunch = 0.0;
                    victim->launchCount = 0;
                    victim->impactGlow = 1.0f;
                    victim->impactCooldownSeconds = 9.0f;
                    victim->randomState ^= 0xa511e9b3u;

                    ruptureCooldownSeconds = 180.0f
                        + 180.0f * nextRandom (rareEventRandomState);
                    globalImpactCooldownSeconds = 1.5f;
                    impactOccurred = true;
                    break;
                }

                auto* outward = nextRandom (first.randomState) < 0.5f ? &first : &second;
                auto* inward = outward == &first ? &second : &first;
                const auto nudge = (0.006f + 0.020f * closeness)
                                 * (0.85f + 0.30f * nextRandom (first.randomState));
                outward->proximity = juce::jmax (0.03f, outward->proximity - nudge);
                inward->proximity = juce::jmin (
                    horizonBoundary, inward->proximity + 0.45f * nudge);

                const auto angularNudge = 0.015f + 0.055f * closeness;
                first.angle -= angularNudge;
                second.angle += angularNudge;
                first.orbitRateScale *= 0.97f + 0.02f * nextRandom (first.randomState);
                second.orbitRateScale *= 1.01f + 0.02f * nextRandom (second.randomState);

                first.impactGlow = second.impactGlow = 0.55f + 0.45f * closeness;
                first.impactCooldownSeconds = 3.0f
                    + 4.0f * nextRandom (first.randomState);
                second.impactCooldownSeconds = 3.0f
                    + 4.0f * nextRandom (second.randomState);
                globalImpactCooldownSeconds = 0.55f
                    + 0.95f * nextRandom (first.randomState);

                // Different ancestries exchange one transient grain. Members
                // of one phrase only trade momentum, never duplicate material.
                if (! samePhrase)
                {
                    launchFragment (first, dispersion, second.sourceSlot, &second.definition);
                    launchFragment (second, dispersion, first.sourceSlot, &first.definition);
                }

                const auto reversalChance = juce::jlimit (
                    0.08f, 0.22f, (0.08f + 0.14f * closeness)
                                * (0.75f + 0.25f * dispersion));
                if (! samePhrase && reversalCooldownSeconds <= 0.0f
                    && nextRandom (first.randomState) < reversalChance)
                {
                    const auto reversedSlot = nextRandom (first.randomState) < 0.5f
                        ? first.sourceSlot : second.sourceSlot;
                    reverseGeneration (reversedSlot);
                    reversalCooldownSeconds = 120.0f
                        + 120.0f * nextRandom (rareEventRandomState);
                }
                impactOccurred = true;
                break;
            }
        }
    }

    const auto panRampSamples = (float) juce::jmax (1, output.getNumSamples());
    for (auto& voice : fragmentPool)
    {
        if (! voice.active || voice.parentChunk < 0 || voice.parentChunk >= maxChunks)
            continue;

        const auto& parent = chunkPool[(size_t) voice.parentChunk];
        if (! parent.active)
        {
            voice.gainStepL = voice.gainStepR = 0.0f;
            continue;
        }

        const auto width = 0.10f + 0.80f * std::pow (1.0f - parent.proximity, 1.3f);
        const auto pan = juce::jlimit (-0.95f, 0.95f,
            std::sin (parent.angle) * width + voice.panOffset);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto targetGainL = voice.baseGain * std::cos (angle);
        const auto targetGainR = voice.baseGain * std::sin (angle);
        voice.gainStepL = (targetGainL - voice.gainL) / panRampSamples;
        voice.gainStepR = (targetGainR - voice.gainR) / panRampSamples;
    }

    auto* left = output.getWritePointer (0);
    auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1) : left;

    const auto fieldCapacity = fieldBuffer.getNumSamples();
    for (int blockStart = 0; blockStart < output.getNumSamples(); blockStart += fieldCapacity)
    {
        const auto segmentSamples = juce::jmin (fieldCapacity, output.getNumSamples() - blockStart);
        fieldBuffer.clear (0, 0, segmentSamples);
        fieldBuffer.clear (1, 0, segmentSamples);
        shimmerBuffer.clear (0, 0, segmentSamples);
        shimmerBuffer.clear (1, 0, segmentSamples);
        auto* fieldLeft = fieldBuffer.getWritePointer (0);
        auto* fieldRight = fieldBuffer.getWritePointer (1);
        auto* shimmerLeft = shimmerBuffer.getWritePointer (0);
        auto* shimmerRight = shimmerBuffer.getWritePointer (1);

        for (int localSample = 0; localSample < segmentSamples; ++localSample)
        {
            for (auto& chunk : chunkPool)
            {
                if (! chunk.active)
                    continue;

                if (chunk.samplesUntilLaunch <= 0.0)
                {
                    launchFragment (chunk, dispersion);
                    ++chunk.launchCount;
                    if (chunk.parabolicEscape)
                    {
                        // A flyby or slingshot states its captured phrase once;
                        // it is a trajectory, not another repeating orbit.
                        chunk.samplesUntilLaunch = (chunk.escapeDurationSeconds + 1.0f)
                                                 * sampleRate;
                        --chunk.samplesUntilLaunch;
                        continue;
                    }
                    const auto p = chunk.proximity;
                    double intervalSeconds = 0.0;
                    if (p < 0.16f)
                    {
                        intervalSeconds = (double) cycleSeconds;
                    }
                    else if (p < 0.82f)
                    {
                        const auto momentumPosition = juce::jlimit (
                            0.0f, 1.0f, (p - 0.16f) / 0.66f);
                        const auto momentum = momentumPosition * momentumPosition
                                            * (3.0f - 2.0f * momentumPosition);
                        constexpr double granularInterval = 0.16;
                        intervalSeconds = (double) cycleSeconds * std::pow (
                            granularInterval / (double) cycleSeconds, (double) momentum);
                    }
                    else
                    {
                        intervalSeconds = juce::jmap (
                            (double) ((p - 0.82f) / 0.18f), 0.16, 0.10);
                        const auto chaosPosition = juce::jlimit (0.0f, 1.0f,
                            (p - 0.68f) / (0.955f - 0.68f));
                        const auto prePlasmaChaos = std::sin (
                            juce::MathConstants<float>::pi * chaosPosition);
                        const auto intervalVariation = 0.65f
                            + 1.30f * nextRandom (chunk.randomState);
                        intervalSeconds *= juce::jmap ((double) prePlasmaChaos,
                                                       1.0, (double) intervalVariation);
                        if (nextRandom (chunk.randomState)
                            < 0.16f * dispersion * prePlasmaChaos)
                        {
                            intervalSeconds += 0.06
                                + 0.14 * (double) nextRandom (chunk.randomState);
                        }
                    }
                    chunk.samplesUntilLaunch += intervalSeconds * sampleRate;
                }
                --chunk.samplesUntilLaunch;
            }

            float sampleL = 0.0f;
            float sampleR = 0.0f;
            float sendL = 0.0f;
            float sendR = 0.0f;
            float shimmerL = 0.0f;
            float shimmerR = 0.0f;
            renderOneSample (sampleL, sampleR, sendL, sendR, shimmerL, shimmerR);
            renderRadiationSample (shimmerL, shimmerR);
            const auto outputIndex = blockStart + localSample;
            left[outputIndex] = right == left ? sampleL + sampleR : sampleL;
            if (right != left)
                right[outputIndex] = sampleR;
            fieldLeft[localSample] = sendL * fieldAmount;
            fieldRight[localSample] = sendR * fieldAmount;
            shimmerLeft[localSample] = shimmerL * shimmerAmount;
            shimmerRight[localSample] = shimmerR * shimmerAmount;
        }

        gravityReverb.processStereo (fieldLeft, fieldRight, segmentSamples);
        const auto feedbackCapacity = shimmerFeedbackDelay.getNumSamples();
        const auto delaySamplesL = juce::jmin (feedbackCapacity - 1,
                                               juce::roundToInt (sampleRate * 0.230));
        const auto delaySamplesR = juce::jmin (feedbackCapacity - 1,
                                               juce::roundToInt (sampleRate * 0.347));
        for (int localSample = 0; localSample < segmentSamples; ++localSample)
        {
            const auto readPositionL = (shimmerFeedbackWritePosition - delaySamplesL
                                      + feedbackCapacity) % feedbackCapacity;
            const auto readPositionR = (shimmerFeedbackWritePosition - delaySamplesR
                                      + feedbackCapacity) % feedbackCapacity;
            const auto delayedL = shimmerFeedbackDelay.getSample (0, readPositionL);
            const auto delayedR = shimmerFeedbackDelay.getSample (1, readPositionR);
            const auto inputL = shimmerLeft[localSample] + 0.68f * delayedL + 0.10f * delayedR;
            const auto inputR = shimmerRight[localSample] + 0.68f * delayedR + 0.10f * delayedL;
            shimmerHpOutputL = shimmerHpCoefficient
                             * (shimmerHpOutputL + inputL - shimmerHpInputL);
            shimmerHpOutputR = shimmerHpCoefficient
                             * (shimmerHpOutputR + inputR - shimmerHpInputR);
            shimmerHpInputL = inputL;
            shimmerHpInputR = inputR;
            shimmerLeft[localSample] = shimmerHpOutputL;
            shimmerRight[localSample] = shimmerHpOutputR;
            shimmerFeedbackDelay.setSample (0, shimmerFeedbackWritePosition, shimmerHpOutputL);
            shimmerFeedbackDelay.setSample (1, shimmerFeedbackWritePosition, shimmerHpOutputR);
            shimmerFeedbackWritePosition = (shimmerFeedbackWritePosition + 1) % feedbackCapacity;
        }
        shimmerReverb.processStereo (shimmerLeft, shimmerRight, segmentSamples);

        for (int localSample = 0; localSample < segmentSamples; ++localSample)
        {
            const auto outputIndex = blockStart + localSample;
            const auto gain = universeGain.getNextValue();
            if (right == left)
                left[outputIndex] = std::tanh ((left[outputIndex] + fieldLeft[localSample]
                                              + fieldRight[localSample] + shimmerLeft[localSample]
                                              + shimmerRight[localSample]) * gain * 0.7071f);
            else
            {
                left[outputIndex] = std::tanh ((left[outputIndex] + fieldLeft[localSample]
                                              + shimmerLeft[localSample]) * gain);
                right[outputIndex] = std::tanh ((right[outputIndex] + fieldRight[localSample]
                                               + shimmerRight[localSample]) * gain);
            }
        }
    }

    completePendingTransitionIfReady();
    updateSnapshots();

    for (int slotIndex = 0; slotIndex < sourceSlotCount; ++slotIndex)
    {
        if (sourceSlots[(size_t) slotIndex].state.load (std::memory_order_acquire) != SlotState::active)
            continue;

        bool slotAlive = false;
        for (const auto& chunk : chunkPool)
            slotAlive = slotAlive || (chunk.active && chunk.sourceSlot == slotIndex);
        for (const auto& fragment : fragmentPool)
            slotAlive = slotAlive || (fragment.active && fragment.sourceSlot == slotIndex);
        for (const auto& radiation : radiationPool)
            slotAlive = slotAlive || (radiation.active && radiation.sourceSlot == slotIndex);

        if (! slotAlive)
        {
            auto& source = sourceSlots[(size_t) slotIndex];
            const auto mayArchive = ! source.isGhost
                                 && ghostArchiveRefillCooldownSamples <= 0
                                 && hasEmptyGhostMemory();
            if (mayArchive)
            {
                source.state.store (SlotState::archiving, std::memory_order_release);
                pendingGhostArchiveMask.fetch_or (
                    1u << (uint32_t) slotIndex, std::memory_order_acq_rel);
            }
            else
            {
                source.isGhost = false;
                source.state.store (SlotState::free, std::memory_order_release);
            }
        }
    }

    if (getGenerationCount() == 0 && status.load (std::memory_order_acquire) == Status::playing)
        status.store (Status::empty, std::memory_order_release);
}

void EventHorizonEngine::updateSnapshots() noexcept
{
    for (int i = 0; i < maxChunks; ++i)
    {
        const auto& chunk = chunkPool[(size_t) i];
        auto& snapshot = snapshots[(size_t) i];
        snapshot.gravity.store (chunk.definition.gravity, std::memory_order_relaxed);
        snapshot.proximity.store (chunk.proximity, std::memory_order_relaxed);
        snapshot.angle.store (chunk.angle, std::memory_order_relaxed);
        snapshot.impactGlow.store (chunk.impactGlow, std::memory_order_relaxed);
        snapshot.visualScale.store (chunk.visualScale, std::memory_order_relaxed);
        snapshot.collisionShard.store (chunk.collisionShard, std::memory_order_relaxed);
        snapshot.generation.store (chunk.sourceSlot, std::memory_order_relaxed);
        snapshot.active.store (chunk.active, std::memory_order_release);
    }

    for (int i = 0; i < maxRadiationVoices; ++i)
    {
        const auto& voice = radiationPool[(size_t) i];
        auto& snapshot = radiationSnapshots[(size_t) i];
        const auto audible = voice.active && voice.delaySamples <= 0;
        const auto progress = audible
            ? juce::jlimit (0.0f, 1.0f, voice.sourcePosition
                / (float) juce::jmax (1, voice.lengthSamples - 1))
            : 0.0f;
        snapshot.progress.store (progress, std::memory_order_relaxed);
        snapshot.targetPan.store (voice.targetPan, std::memory_order_relaxed);
        snapshot.intensity.store (voice.gain, std::memory_order_relaxed);
        snapshot.active.store (audible, std::memory_order_release);
    }
}

EventHorizonEngine::Snapshot EventHorizonEngine::getSnapshot (int index) const noexcept
{
    Snapshot result;
    if (! juce::isPositiveAndBelow (index, maxChunks))
        return result;

    const auto& source = snapshots[(size_t) index];
    result.active = source.active.load (std::memory_order_acquire);
    result.proximity = source.proximity.load (std::memory_order_relaxed);
    result.gravity = source.gravity.load (std::memory_order_relaxed);
    result.angle = source.angle.load (std::memory_order_relaxed);
    result.impactGlow = source.impactGlow.load (std::memory_order_relaxed);
    result.visualScale = source.visualScale.load (std::memory_order_relaxed);
    result.collisionShard = source.collisionShard.load (std::memory_order_relaxed);
    result.generation = source.generation.load (std::memory_order_relaxed);
    return result;
}

EventHorizonEngine::RadiationSnapshot EventHorizonEngine::getRadiationSnapshot (
    int index) const noexcept
{
    RadiationSnapshot result;
    if (! juce::isPositiveAndBelow (index, maxRadiationVoices))
        return result;

    const auto& source = radiationSnapshots[(size_t) index];
    result.active = source.active.load (std::memory_order_acquire);
    result.progress = source.progress.load (std::memory_order_relaxed);
    result.targetPan = source.targetPan.load (std::memory_order_relaxed);
    result.intensity = source.intensity.load (std::memory_order_relaxed);
    return result;
}

int EventHorizonEngine::getGenerationCount() const noexcept
{
    int count = 0;
    for (const auto& slot : sourceSlots)
        if (slot.state.load (std::memory_order_acquire) != SlotState::free)
            ++count;
    return count;
}

int EventHorizonEngine::getPlayingGenerationCount() const noexcept
{
    int count = 0;
    for (const auto& slot : sourceSlots)
        if (slot.state.load (std::memory_order_acquire) == SlotState::active)
            ++count;
    return count;
}
