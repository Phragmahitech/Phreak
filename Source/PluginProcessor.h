#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PresetLibrary.h"
#include "PhreakEngine.h"

/** Un sample chargé, côté interface (le moteur ne voit que `data`). */
struct LoadedSample
{
    std::shared_ptr<phreak::Sample> data;
    juce::String path, name;
    bool missing = false;                 // chemin gardé dans le projet mais fichier introuvable
    std::vector<float> peaks;             // min/max par bloc de peakBlock échantillons (mono)
    static constexpr int peakBlock = 256;

    bool isLoaded() const noexcept        { return data != nullptr && data->length() > 0; }
    double seconds() const noexcept       { return isLoaded() ? data->length() / data->sampleRate : 0.0; }
};

class PhreakAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    //==========================================================================
    // Identifiants (ne pas renommer : les projets sauvegardés en dépendent)
    static juce::String pointId (int point, const char* what)   { return "p" + juce::String (point + 1) + what; }
    static juce::String stepId (int step)                       { return "step" + juce::String (step + 1); }

    static constexpr const char* pointsId    = "points";
    static constexpr const char* modeId      = "mode";
    static constexpr const char* stepsId     = "steps";
    static constexpr const char* rateId      = "rate";
    static constexpr const char* syncId      = "sync";
    static constexpr const char* freeMsId    = "freeMs";
    static constexpr const char* driftId     = "drift";
    static constexpr const char* swingId     = "swing";
    static constexpr const char* chanceId    = "chance";
    static constexpr const char* gateId      = "gate";
    static constexpr const char* seedId      = "seed";
    static constexpr const char* lockId      = "lock";
    static constexpr const char* triggerId   = "trigger";
    static constexpr const char* reverseId   = "reverse";
    static constexpr const char* scatterId   = "scatter";
    static constexpr const char* fadeId      = "fade";
    static constexpr const char* snapId      = "snap";
    static constexpr const char* revSizeId   = "revSize";
    static constexpr const char* revDampId   = "revDamp";
    static constexpr const char* revWidthId  = "revWidth";
    static constexpr const char* revLevelId  = "revLevel";
    static constexpr const char* dlyTimeId   = "dlyTime";
    static constexpr const char* dlyFbId     = "dlyFeedback";
    static constexpr const char* dlyToneId   = "dlyTone";
    static constexpr const char* dlyLevelId  = "dlyLevel";
    static constexpr const char* dlyPingId   = "dlyPing";
    static constexpr const char* gainId      = "gain";
    static constexpr const char* limiterId   = "limiter";

    static constexpr int numPoints = phreak::maxPoints, numSlots = phreak::maxSlots, numSteps = phreak::maxSteps;

    static juce::String stepText (int value);

    //==========================================================================
    PhreakAudioProcessor();
    ~PhreakAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Phreak"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 3.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetLibrary presets;

    //==========================================================================
    // Samples (fil de l'interface)
    bool loadSample (int slot, const juce::File& file, juce::String* error = nullptr);
    void clearSample (int slot);
    const LoadedSample& getSample (int slot) const          { return slots[(size_t) juce::jlimit (0, numSlots - 1, slot)]; }
    int getSampleGeneration() const noexcept                { return sampleGeneration; }
    juce::AudioFormatManager& getFormats() noexcept         { return formats; }
    /** Pour les tests : installe un sample déjà en mémoire. */
    void setSampleData (int slot, std::shared_ptr<phreak::Sample> data, const juce::String& name);

    // Hasard et placement des points (fil de l'interface)
    enum RandomFlags { rndPositions = 1, rndSamples = 2, rndDirections = 4, rndStutter = 8, rndPitch = 16, rndFx = 32, rndTails = 64 };
    void randomizePoints (int flags, juce::Random& rng);
    void randomizePoints (int flags)                        { juce::Random rng; randomizePoints (flags, rng); }
    void spreadPointsEvenly();
    void pointsOnTransients();
    void randomizePattern (juce::Random& rng);
    void randomizePattern()                                 { juce::Random rng; randomizePattern (rng); }
    void resetPattern();
    /** Position (0..1) recalée selon SNAP. */
    float snapPosition (int slot, float pos) const;

    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const           { return apvts.getRawParameterValue (id)->load(); }

    // Affichage
    const phreak::Engine& getEngine() const noexcept        { return engine; }
    float takeOutputPeak() noexcept                         { return outputPeak.exchange (0.0f); }

    /** Pour les tests : moteur interne. */
    phreak::Engine& engineForTests() noexcept               { return engine; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    void publishSample (int slot);
    void readParameters();

    juce::AudioFormatManager formats;
    std::array<LoadedSample, numSlots> slots;
    std::array<std::atomic<phreak::Sample*>, numSlots> live {};
    struct Retired { std::shared_ptr<phreak::Sample> data; juce::uint64 after; };
    std::vector<Retired> retired;
    std::atomic<juce::uint64> blocksDone { 0 };
    int sampleGeneration = 0;

    phreak::Engine engine;
    std::vector<phreak::MidiEvent> midiEvents;
    juce::AudioBuffer<float> work;

    // paramètres lus vite
    struct PointRaw
    {
        std::atomic<float>* sample; std::atomic<float>* pos; std::atomic<float>* dir; std::atomic<float>* pitch;
        std::atomic<float>* gain; std::atomic<float>* pan; std::atomic<float>* filter; std::atomic<float>* stutter;
        std::atomic<float>* rev; std::atomic<float>* dly; std::atomic<float>* tail;
    };
    std::array<PointRaw, numPoints> pointRaw {};
    std::array<std::atomic<float>*, numSteps> stepRaw {};

    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhreakAudioProcessor)
};
