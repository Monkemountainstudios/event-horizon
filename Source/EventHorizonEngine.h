#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class EventHorizonEngine
{
public:
    static constexpr int maxGenerations = 6;
    static constexpr int maxChunksPerGeneration = 16;
    static constexpr int maxBootstrapChunks = 3;
    static constexpr int maxChunks = maxGenerations * maxChunksPerGeneration
                                   + maxBootstrapChunks;
    static constexpr int maxFragments = 192;
    static constexpr int maxRadiationVoices = 32;
    static constexpr int sourceSlotCount = maxGenerations + 2;
    static constexpr int ghostMemoryCount = 4;

    enum class Status
    {
        empty,
        armed,
        recording,
        analysing,
        ready,
        playing
    };

    struct Snapshot
    {
        bool active = false;
        float proximity = 0.0f;
        float gravity = 0.0f;
        float angle = 0.0f;
        float impactGlow = 0.0f;
        float visualScale = 1.0f;
        bool collisionShard = false;
        int generation = -1;
    };

    struct RadiationSnapshot
    {
        bool active = false;
        float progress = 0.0f;
        float targetPan = 0.0f;
        float intensity = 0.0f;
    };

    EventHorizonEngine();

    void prepare (double newSampleRate, int maximumBlockSize);
    bool requestRecording();
    bool submitLoadedAudio (const juce::AudioBuffer<float>& source, double sourceSampleRate);
    void servicePendingAnalysis();
    void requestClear() noexcept;

    void captureInput (const float* monoInput, int numSamples) noexcept;
    void setLiveCaptureEnabled (bool enabled) noexcept { liveCaptureEnabled = enabled; }
    void setLiveCaptureAppetite (float amount) noexcept
    {
        liveCaptureAppetite = juce::jlimit (0.0f, 1.0f, amount);
    }
    void setLiveCaptureTimeScale (float normalisedChurn) noexcept
    {
        liveCaptureTimeScale = juce::jlimit (0.0f, 1.0f, normalisedChurn);
    }
    void process (juce::AudioBuffer<float>& output, float gravityScale,
                  float dispersion, float fieldAmount, float shimmerAmount,
                  float cycleSeconds, float dwellSeconds) noexcept;

    [[nodiscard]] Status getStatus() const noexcept { return status.load (std::memory_order_acquire); }
    [[nodiscard]] float getRecordingProgress() const noexcept { return recordingProgress.load (std::memory_order_relaxed); }
    [[nodiscard]] Snapshot getSnapshot (int index) const noexcept;
    [[nodiscard]] RadiationSnapshot getRadiationSnapshot (int index) const noexcept;
    [[nodiscard]] int getGenerationCount() const noexcept;
    [[nodiscard]] int getPlayingGenerationCount() const noexcept;
    [[nodiscard]] bool canAcceptSource() const noexcept { return getGenerationCount() < sourceSlotCount; }

private:
    enum class SlotState
    {
        free,
        preparedForRecording,
        recording,
        captured,
        analysing,
        archiving,
        ready,
        active
    };

    enum class GhostState
    {
        empty,
        writing,
        maturing,
        ready,
        queuedForRecall
    };

    struct ChunkDefinition
    {
        int startSample = 0;
        int lengthSamples = 0;
        float rms = 0.0f;
        float gravity = 0.0f;
    };

    struct SourceSlot
    {
        juce::AudioBuffer<float> audio;
        std::array<ChunkDefinition, maxChunksPerGeneration> chunks {};
        std::atomic<SlotState> state { SlotState::free };
        int validSamples = 0;
        int numChunks = 0;
        bool isGhost = false;
        bool slingshotConsidered = false;
    };

    struct GhostMemory
    {
        juce::AudioBuffer<float> audio;
        std::array<ChunkDefinition, maxChunksPerGeneration> chunks {};
        std::atomic<GhostState> state { GhostState::empty };
        int validSamples = 0;
        int numChunks = 0;
        float ageSeconds = 0.0f;
        float maturitySeconds = 90.0f;
    };

    struct ChunkRuntime
    {
        bool active = false;
        int sourceSlot = -1;
        ChunkDefinition definition;
        float proximity = 0.0f;
        float angle = 0.0f;
        float orbitRateScale = 1.0f;
        float horizonDwellSeconds = 0.0f;
        float horizonDwellDuration = 10.0f;
        float horizonTextureScale = 1.0f;
        float orbitHz = 0.05f;
        float orbitDirection = 1.0f;
        float impactCooldownSeconds = 0.0f;
        float impactGlow = 0.0f;
        float playbackRate = 1.0f;
        float visualScale = 1.0f;
        float escapeProgress = 0.0f;
        float escapeDurationSeconds = 4.0f;
        float escapeStartAngle = 0.0f;
        float escapeDirection = 1.0f;
        bool plasmaSeed = false;
        bool collapsing = false;
        bool radiationEmitted = false;
        bool retrograde = false;
        bool parabolicEscape = false;
        bool escapeFromHorizon = false;
        bool collisionShard = false;
        double samplesUntilLaunch = 0.0;
        int sourceCursor = 0;
        int launchCount = 0;
        uint32_t randomState = 1;
    };

    struct FragmentVoice
    {
        bool active = false;
        int sourceSlot = -1;
        int parentChunk = -1;
        int startSample = 0;
        int lengthSamples = 0;
        int ageSamples = 0;
        float sourcePosition = 0.0f;
        float playbackRate = 1.0f;
        bool reversed = false;
        float baseGain = 0.0f;
        float panOffset = 0.0f;
        float gainL = 0.0f;
        float gainR = 0.0f;
        float gainStepL = 0.0f;
        float gainStepR = 0.0f;
        float fieldSend = 0.0f;
        float shimmerSend = 0.0f;
        float filterCoefficient = 1.0f;
        float filterStateL = 0.0f;
        float filterStateR = 0.0f;
        bool coronaActive = false;
        float coronaGainL = 0.0f;
        float coronaGainR = 0.0f;
        float coronaFilterCoefficient = 1.0f;
        float coronaFilterState1 = 0.0f;
        float coronaFilterState2 = 0.0f;
        std::array<int, 2> holeStarts { -1, -1 };
        std::array<int, 2> holeLengths { 0, 0 };
    };

    struct RadiationVoice
    {
        bool active = false;
        int sourceSlot = -1;
        int startSample = 0;
        int lengthSamples = 0;
        int delaySamples = 0;
        float sourcePosition = 0.0f;
        float pitchRatio = 2.0f;
        float targetPan = 0.0f;
        float gain = 0.0f;
    };

    struct AtomicSnapshot
    {
        std::atomic<bool> active { false };
        std::atomic<float> proximity { 0.0f };
        std::atomic<float> gravity { 0.0f };
        std::atomic<float> angle { 0.0f };
        std::atomic<float> impactGlow { 0.0f };
        std::atomic<float> visualScale { 1.0f };
        std::atomic<bool> collisionShard { false };
        std::atomic<int> generation { -1 };
    };

    struct AtomicRadiationSnapshot
    {
        std::atomic<bool> active { false };
        std::atomic<float> progress { 0.0f };
        std::atomic<float> targetPan { 0.0f };
        std::atomic<float> intensity { 0.0f };
    };

    int claimFreeSlot (SlotState claimedState) noexcept;
    void analyseSlot (int slotIndex);
    void beginActivationIfNeeded() noexcept;
    void completePendingTransitionIfReady() noexcept;
    void activateSlot (int slotIndex) noexcept;
    void deactivateUniverse() noexcept;
    void launchFragment (ChunkRuntime& chunk, float dispersion,
                         int materialSourceSlot = -1,
                         const ChunkDefinition* materialDefinition = nullptr) noexcept;
    void launchRadiation (ChunkRuntime& chunk) noexcept;
    void reverseGeneration (int sourceSlot) noexcept;
    [[nodiscard]] bool launchGenerationSlingshot (int sourceSlot) noexcept;
    void renderOneSample (float& left, float& right,
                          float& fieldLeft, float& fieldRight,
                          float& shimmerLeft, float& shimmerRight) noexcept;
    void renderRadiationSample (float& shimmerLeft, float& shimmerRight) noexcept;
    void updateSnapshots() noexcept;
    [[nodiscard]] int getTravellingGenerationCount() const noexcept;
    [[nodiscard]] int chooseReadyGhostMemory() noexcept;
    [[nodiscard]] bool queueGhostRecall (int memoryIndex) noexcept;
    [[nodiscard]] bool hasEmptyGhostMemory() const noexcept;
    void updateGhostEcology (int numSamples, bool inputWasPresent) noexcept;

    static float nextRandom (uint32_t& state) noexcept;

    std::array<SourceSlot, sourceSlotCount> sourceSlots;
    std::array<GhostMemory, ghostMemoryCount> ghostMemories;
    std::array<ChunkRuntime, maxChunks> chunkPool {};
    std::array<FragmentVoice, maxFragments> fragmentPool {};
    std::array<RadiationVoice, maxRadiationVoices> radiationPool {};
    std::array<AtomicSnapshot, maxChunks> snapshots;
    std::array<AtomicRadiationSnapshot, maxRadiationVoices> radiationSnapshots;

    std::atomic<int> recordingCommand { -1 };
    std::atomic<bool> clearCommand { false };
    std::atomic<uint32_t> pendingGhostArchiveMask { 0 };
    std::atomic<int> pendingGhostRecallMemory { -1 };
    std::atomic<int> pendingGhostRecallTargetSlot { -1 };
    std::atomic<Status> status { Status::empty };
    std::atomic<float> recordingProgress { 0.0f };

    int recordingSlot = -1;
    int recordingWritePosition = 0;
    int recordingTargetSamples = 0;
    bool liveCaptureEnabled = false;
    bool liveGateOpen = false;
    int liveCaptureSlot = -1;
    int liveCaptureWritePosition = 0;
    int liveCaptureTargetSamples = 0;
    int liveQuietSamples = 0;
    int liveRearmSamples = 0;
    int liveRetrySamples = 0;
    int liveCaptureIntervalSamples = 0;
    float liveEnvelope = 0.0f;
    float liveNoiseFloor = 0.0005f;
    float liveCaptureAppetite = 0.65f;
    float liveCaptureTimeScale = 0.45f;
    float liveHunger = 0.0f;
    uint32_t liveRandomState = 0x4c495645u;
    uint32_t ghostRandomState = 0x47484f53u;
    uint32_t ghostServiceRandomState = 0x4d454d4fu;
    uint32_t rareEventRandomState = 0x52415245u;
    int ghostCooldownSamples = 0;
    int ghostArchiveRefillCooldownSamples = 0;
    int ghostSpontaneousCheckSamples = 0;
    bool ghostSafetySpent = false;
    juce::AudioBuffer<float> livePreRoll;
    int livePreRollWritePosition = 0;
    bool clearingUniverse = false;
    int preparedSamples = 0;
    double sampleRate = 48000.0;
    juce::LinearSmoothedValue<float> universeGain;
    juce::AudioBuffer<float> fieldBuffer;
    juce::AudioBuffer<float> shimmerBuffer;
    juce::AudioBuffer<float> shimmerFeedbackDelay;
    juce::Reverb gravityReverb;
    juce::Reverb shimmerReverb;
    float horizonFieldExpansion = 0.0f;
    float shimmerHpCoefficient = 0.0f;
    float shimmerHpInputL = 0.0f;
    float shimmerHpInputR = 0.0f;
    float shimmerHpOutputL = 0.0f;
    float shimmerHpOutputR = 0.0f;
    int shimmerFeedbackWritePosition = 0;
    float globalImpactCooldownSeconds = 0.0f;
    float ruptureCooldownSeconds = 0.0f;
    float reversalCooldownSeconds = 0.0f;
    float escapeCooldownSeconds = 0.0f;
    float sessionAgeSeconds = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventHorizonEngine)
};
