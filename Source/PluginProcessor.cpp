#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                           juce::NormalisableRange<float> range, float def,
                                                           std::function<juce::String (float, int)> toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText)));
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (const juce::String& id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    }

    juce::NormalisableRange<float> logRange (float lo, float hi, float interval = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, interval);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    juce::StringArray toArray (const std::vector<const char*>& v)
    {
        juce::StringArray a;
        for (auto* s : v) a.add (s);
        return a;
    }

    auto percent   = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto decibels  = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto millis    = [] (float v, int) { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    auto semis     = [] (float v, int) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; };
    auto panText   = [] (float v, int)
    {
        const int p = juce::roundToInt (v);
        return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
    };
    auto filterText = [] (float v, int)
    {
        const int p = juce::roundToInt (v);
        return p == 0 ? juce::String ("OFF") : (p < 0 ? "LP " + juce::String (-p) : "HP " + juce::String (p));
    };
    auto posText   = [] (float v, int) { return juce::String (v, 2) + " %"; };

    //==========================================================================
    /** Transitoires : montée d'énergie (log) par trames de 256, seuil adaptatif, 40 ms d'écart minimum. */
    std::vector<int> detectOnsets (const phreak::Sample& s)
    {
        const int hop = 256, n = s.length();
        const int frames = n / hop;
        std::vector<float> e ((size_t) std::max (0, frames));
        for (int f = 0; f < frames; ++f)
        {
            double acc = 0.0;
            for (int i = f * hop; i < (f + 1) * hop; ++i)
            {
                const float m = 0.5f * (s.ch[0][(size_t) i] + s.ch[1][(size_t) i]);
                acc += (double) m * m;
            }
            e[(size_t) f] = (float) (10.0 * std::log10 (acc / hop + 1.0e-10));
        }

        std::vector<int> onsets;
        const int minGap = juce::jmax (1, (int) (0.04 * s.sampleRate / hop));
        int last = -minGap;
        for (int f = 2; f < frames; ++f)
        {
            const float rise = e[(size_t) f] - 0.5f * (e[(size_t) f - 1] + e[(size_t) f - 2]);
            if (rise > 6.0f && e[(size_t) f] > -50.0f && f - last >= minGap)
            {
                // affine dans la trame : premier échantillon qui dépasse 30 % du pic de la trame suivante
                int start = (f - 1) * hop, best = f * hop;
                float peak = 0.0f;
                for (int i = start; i < juce::jmin (n, (f + 1) * hop); ++i)
                    peak = juce::jmax (peak, std::abs (s.ch[0][(size_t) i]) + std::abs (s.ch[1][(size_t) i]));
                for (int i = start; i < juce::jmin (n, (f + 1) * hop); ++i)
                    if (std::abs (s.ch[0][(size_t) i]) + std::abs (s.ch[1][(size_t) i]) > 0.3f * peak) { best = i; break; }
                onsets.push_back (juce::jmax (0, best - 32));
                last = f;
            }
        }
        return onsets;
    }

    std::vector<float> computePeaks (const phreak::Sample& s)
    {
        const int block = LoadedSample::peakBlock, n = s.length();
        const int count = (n + block - 1) / block;
        std::vector<float> peaks ((size_t) count * 2);
        for (int b = 0; b < count; ++b)
        {
            float lo = 0.0f, hi = 0.0f;
            for (int i = b * block; i < juce::jmin (n, (b + 1) * block); ++i)
            {
                const float m = 0.5f * (s.ch[0][(size_t) i] + s.ch[1][(size_t) i]);
                lo = juce::jmin (lo, m);
                hi = juce::jmax (hi, m);
            }
            peaks[(size_t) b * 2] = lo;
            peaks[(size_t) b * 2 + 1] = hi;
        }
        return peaks;
    }
}

juce::String PhreakAudioProcessor::stepText (int value)
{
    if (value <= phreak::stepTie) return "TIE";
    if (value >= phreak::stepRest) return "REST";
    return juce::String (value);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PhreakAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&layout] (auto p) { layout.add (std::move (p)); };

    // Séquenceur
    add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pointsId, 1 }, "Points", 1, numPoints, 8));
    add (choiceParam (modeId, "Mode", { "GRID", "CYCLE", "RANDOM" }, 0));
    add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { stepsId, 1 }, "Steps", 1, numSteps, 16));
    add (choiceParam (rateId, "Rate", toArray (phreak::Divisions::rateNames()), 5));
    add (boolParam (syncId, "Sync", true));
    add (floatParam (freeMsId, "Free Time", logRange (10.0f, 2000.0f, 0.1f), 125.0f, millis));
    add (floatParam (driftId, "Drift", { 0.0f, 100.0f, 0.1f }, 0.0f, percent));
    add (floatParam (swingId, "Swing", { 0.0f, 75.0f, 0.1f }, 0.0f, percent));
    add (floatParam (chanceId, "Chance", { 0.0f, 100.0f, 0.1f }, 100.0f, percent));
    add (floatParam (gateId, "Gate", { 5.0f, 100.0f, 0.1f }, 100.0f, percent));
    add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { seedId, 1 }, "Seed", 1, 9999, 1));
    add (boolParam (lockId, "Lock", true));
    add (choiceParam (triggerId, "Trigger", { "TRANSPORT", "NOTE" }, 0));
    add (floatParam (reverseId, "Reverse Chance", { 0.0f, 100.0f, 0.1f }, 0.0f, percent));
    add (floatParam (scatterId, "Scatter", { 0.0f, 500.0f, 0.1f, 0.5f }, 0.0f, millis));
    add (floatParam (fadeId, "Fade", logRange (0.5f, 50.0f, 0.01f), 3.0f, millis));
    add (choiceParam (snapId, "Snap", { "OFF", "TRANSIENTS", "ZERO" }, 0));

    // Effets
    add (floatParam (revSizeId, "Reverb Size", { 0.0f, 100.0f, 0.1f }, 60.0f, percent));
    add (floatParam (revDampId, "Reverb Damp", { 0.0f, 100.0f, 0.1f }, 40.0f, percent));
    add (floatParam (revWidthId, "Reverb Width", { 0.0f, 100.0f, 0.1f }, 100.0f, percent));
    add (floatParam (revLevelId, "Reverb Level", { 0.0f, 100.0f, 0.1f }, 80.0f, percent));
    add (choiceParam (dlyTimeId, "Delay Time", toArray (phreak::Divisions::delayNames()), 5));
    add (floatParam (dlyFbId, "Delay Feedback", { 0.0f, 95.0f, 0.1f }, 45.0f, percent));
    add (floatParam (dlyToneId, "Delay Tone", { 0.0f, 100.0f, 0.1f }, 60.0f, percent));
    add (floatParam (dlyLevelId, "Delay Level", { 0.0f, 100.0f, 0.1f }, 80.0f, percent));
    add (boolParam (dlyPingId, "Delay Ping-Pong", true));
    add (floatParam (gainId, "Output", { -36.0f, 12.0f, 0.01f }, 0.0f, decibels));
    add (boolParam (limiterId, "Limiter", true));

    // Points
    const auto stutterNames = toArray (phreak::Divisions::stutterNames());
    for (int p = 0; p < numPoints; ++p)
    {
        const auto n = "Point " + juce::String (p + 1) + " ";
        add (choiceParam (pointId (p, "Sample"), n + "Sample", { "1", "2", "3", "4" }, 0));
        add (floatParam (pointId (p, "Pos"), n + "Position", { 0.0f, 100.0f }, 100.0f * (float) p / numPoints, posText));
        add (choiceParam (pointId (p, "Dir"), n + "Direction", { "FWD", "REV", "PING" }, 0));
        add (floatParam (pointId (p, "Pitch"), n + "Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, semis));
        add (floatParam (pointId (p, "Gain"), n + "Gain", { -36.0f, 12.0f, 0.01f }, 0.0f, decibels));
        add (floatParam (pointId (p, "Pan"), n + "Pan", { -100.0f, 100.0f, 1.0f }, 0.0f, panText));
        add (floatParam (pointId (p, "Filter"), n + "Filter", { -100.0f, 100.0f, 0.1f }, 0.0f, filterText));
        add (choiceParam (pointId (p, "Stutter"), n + "Stutter", stutterNames, 0));
        add (floatParam (pointId (p, "Rev"), n + "Reverb Send", { 0.0f, 100.0f, 0.1f }, 0.0f, percent));
        add (floatParam (pointId (p, "Dly"), n + "Delay Send", { 0.0f, 100.0f, 0.1f }, 0.0f, percent));
        add (choiceParam (pointId (p, "Tail"), n + "Tail", { "KEEP", "CUT" }, 0));
    }

    // Motif
    juce::StringArray stepNames;
    for (int v = 0; v <= phreak::stepRest; ++v)
        stepNames.add (stepText (v));
    for (int k = 0; k < numSteps; ++k)
        add (choiceParam (stepId (k), "Step " + juce::String (k + 1), stepNames, (k % 8) + 1));

    return layout;
}

//==============================================================================
PhreakAudioProcessor::PhreakAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Phreak", createParameterLayout()),
      presets (apvts, "Phreak", ".phrkpreset", PresetLibrary::defaultsOnly (*this, {}, "INIT"), {}, "UNTITLED")
{
    formats.registerBasicFormats();
    for (auto& l : live) l.store (nullptr);

    for (int p = 0; p < numPoints; ++p)
    {
        auto raw = [&] (const char* what) { return apvts.getRawParameterValue (pointId (p, what)); };
        pointRaw[(size_t) p] = { raw ("Sample"), raw ("Pos"), raw ("Dir"), raw ("Pitch"), raw ("Gain"), raw ("Pan"),
                                 raw ("Filter"), raw ("Stutter"), raw ("Rev"), raw ("Dly"), raw ("Tail") };
    }
    for (int k = 0; k < numSteps; ++k)
        stepRaw[(size_t) k] = apvts.getRawParameterValue (stepId (k));

    midiEvents.reserve (1024);
    presets.restoreFromState();
    startTimerHz (4);
}

PhreakAudioProcessor::~PhreakAudioProcessor()
{
    stopTimer();
}

//==============================================================================
void PhreakAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
}

void PhreakAudioProcessor::reset()
{
    engine.reset();
}

bool PhreakAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void PhreakAudioProcessor::readParameters()
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    phreak::Settings s;
    s.numPoints = juce::roundToInt (v (pointsId));
    s.mode = juce::roundToInt (v (modeId));
    s.steps = juce::roundToInt (v (stepsId));
    s.rate = juce::roundToInt (v (rateId));
    s.sync = v (syncId) > 0.5f;
    s.freeMs = v (freeMsId);
    s.drift = v (driftId) * 0.01f;
    s.swing = v (swingId) * 0.01f;
    s.chance = v (chanceId) * 0.01f;
    s.gate = v (gateId) * 0.01f;
    s.seed = (std::uint32_t) juce::roundToInt (v (seedId));
    s.lock = v (lockId) > 0.5f;
    s.trigger = juce::roundToInt (v (triggerId));
    s.reverseChance = v (reverseId) * 0.01f;
    s.scatterMs = v (scatterId);
    s.fadeMs = v (fadeId);
    s.revSize = v (revSizeId) * 0.01f;
    s.revDamp = v (revDampId) * 0.01f;
    s.revWidth = v (revWidthId) * 0.01f;
    s.revLevel = v (revLevelId) * 0.01f;
    s.dlyDiv = juce::roundToInt (v (dlyTimeId));
    s.dlyFeedback = v (dlyFbId) * 0.01f;
    s.dlyTone = v (dlyToneId) * 0.01f;
    s.dlyLevel = v (dlyLevelId) * 0.01f;
    s.dlyPing = v (dlyPingId) > 0.5f;
    s.outGain = juce::Decibels::decibelsToGain (v (gainId), -100.0f);
    s.limiter = v (limiterId) > 0.5f;
    engine.setSettings (s);

    for (int p = 0; p < numPoints; ++p)
    {
        const auto& r = pointRaw[(size_t) p];
        phreak::PointParams pp;
        pp.slot = juce::roundToInt (r.sample->load());
        pp.pos = r.pos->load() * 0.01f;
        pp.dir = juce::roundToInt (r.dir->load());
        pp.pitch = r.pitch->load();
        pp.gain = juce::Decibels::decibelsToGain (r.gain->load(), -100.0f);
        pp.pan = r.pan->load() * 0.01f;
        pp.filter = r.filter->load() * 0.01f;
        pp.stutter = juce::roundToInt (r.stutter->load());
        pp.rev = r.rev->load() * 0.01f;
        pp.dly = r.dly->load() * 0.01f;
        pp.tailKeep = r.tail->load() < 0.5f;
        engine.setPoint (p, pp);
    }
    for (int k = 0; k < numSteps; ++k)
        engine.setStep (k, juce::roundToInt (stepRaw[(size_t) k]->load()));
}

void PhreakAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (int s = 0; s < numSlots; ++s)
        engine.setSample (s, live[(size_t) s].load (std::memory_order_acquire));
    readParameters();

    phreak::Transport t;
    t.playing = false;
    t.bpm = 120.0;
    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition()) t.ppq = *ppq;
            else if (const auto secs = pos->getTimeInSeconds()) t.ppq = *secs * t.bpm / 60.0;
            else t.playing = false;
        }
    }

    midiEvents.clear();
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (midiEvents.size() >= midiEvents.capacity()) break;
        if (msg.isNoteOn())
            midiEvents.push_back ({ m.samplePosition, true, msg.getNoteNumber(), msg.getFloatVelocity() });
        else if (msg.isNoteOff())
            midiEvents.push_back ({ m.samplePosition, false, msg.getNoteNumber(), 0.0f });
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (int note = 0; note < 128 && midiEvents.size() < midiEvents.capacity(); ++note)
                midiEvents.push_back ({ m.samplePosition, false, note, 0.0f });
    }

    if (work.getNumSamples() < n)
        work.setSize (2, n, false, false, true);

    engine.process (work.getWritePointer (0), work.getWritePointer (1), n, t, midiEvents.data(), (int) midiEvents.size());

    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, work, 0, 0, n);
        buffer.copyFrom (1, 0, work, 1, 0, n);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.copyFrom (0, 0, work.getReadPointer (0), n, 0.5f);
        buffer.addFrom (0, 0, work.getReadPointer (1), n, 0.5f);
    }

    float peak = 0.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, n));
    if (peak > outputPeak.load()) outputPeak.store (peak);

    blocksDone.fetch_add (1, std::memory_order_release);
}

//==============================================================================
// Samples
void PhreakAudioProcessor::publishSample (int slot)
{
    auto& l = slots[(size_t) slot];
    auto* old = live[(size_t) slot].exchange (l.data.get(), std::memory_order_acq_rel);
    juce::ignoreUnused (old);
    ++sampleGeneration;
}

void PhreakAudioProcessor::setSampleData (int slot, std::shared_ptr<phreak::Sample> data, const juce::String& name)
{
    slot = juce::jlimit (0, numSlots - 1, slot);
    auto& l = slots[(size_t) slot];
    if (l.data != nullptr)
        retired.push_back ({ l.data, blocksDone.load() + 2 });
    if (data != nullptr && data->onsets.empty())
        data->onsets = detectOnsets (*data);
    l.data = std::move (data);
    l.name = name;
    l.missing = false;
    l.peaks = l.data != nullptr ? computePeaks (*l.data) : std::vector<float>();
    publishSample (slot);
}

bool PhreakAudioProcessor::loadSample (int slot, const juce::File& file, juce::String* error)
{
    slot = juce::jlimit (0, numSlots - 1, slot);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        if (error != nullptr) *error = "Format non reconnu : " + file.getFileName();
        return false;
    }

    const double maxSeconds = 20.0 * 60.0;
    const auto length = (juce::int64) juce::jmin ((double) reader->lengthInSamples, maxSeconds * reader->sampleRate);
    if (length < 16)
    {
        if (error != nullptr) *error = "Fichier vide : " + file.getFileName();
        return false;
    }

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), (int) length);
    reader->read (&buf, 0, (int) length, 0, true, true);

    auto data = std::make_shared<phreak::Sample>();
    data->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    for (int c = 0; c < 2; ++c)
    {
        data->ch[c].resize ((size_t) length);
        if (buf.getNumChannels() == 1)
            std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + length, data->ch[c].begin());
        else if (buf.getNumChannels() == 2)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + length, data->ch[c].begin());
        else
        {
            // plus de deux canaux : les pairs à gauche, les impairs à droite
            std::fill (data->ch[c].begin(), data->ch[c].end(), 0.0f);
            int count = 0;
            for (int src = c; src < buf.getNumChannels(); src += 2, ++count)
                juce::FloatVectorOperations::add (data->ch[c].data(), buf.getReadPointer (src), (int) length);
            if (count > 1) juce::FloatVectorOperations::multiply (data->ch[c].data(), 1.0f / (float) count, (int) length);
        }
    }

    setSampleData (slot, data, file.getFileNameWithoutExtension());
    slots[(size_t) slot].path = file.getFullPathName();
    return true;
}

void PhreakAudioProcessor::clearSample (int slot)
{
    slot = juce::jlimit (0, numSlots - 1, slot);
    setSampleData (slot, nullptr, {});
    slots[(size_t) slot].path = {};
}

void PhreakAudioProcessor::timerCallback()
{
    // libère les samples remplacés une fois que le fil audio ne peut plus les lire
    const auto done = blocksDone.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [done] (const Retired& r) { return done >= r.after; }), retired.end());
}

//==============================================================================
// Points
void PhreakAudioProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

float PhreakAudioProcessor::snapPosition (int slot, float pos) const
{
    const auto& l = getSample (slot);
    if (! l.isLoaded()) return pos;
    const int len = l.data->length();
    const int at = juce::jlimit (0, len - 1, (int) std::round (pos * (len - 1)));
    const int snap = juce::roundToInt (getParam (snapId));

    if (snap == 1 && ! l.data->onsets.empty())
    {
        const auto& on = l.data->onsets;
        auto it = std::lower_bound (on.begin(), on.end(), at);
        int best = it == on.end() ? on.back() : *it;
        if (it != on.begin() && (it == on.end() || at - *(it - 1) < *it - at))
            best = *(it - 1);
        return (float) best / (float) (len - 1);
    }
    if (snap == 2)
    {
        const auto& d = l.data->ch[0];
        const int range = (int) (0.005 * l.data->sampleRate);
        for (int off = 0; off < range; ++off)
            for (int sgn : { 1, -1 })
            {
                const int i = at + sgn * off;
                if (i > 0 && i < len && (d[(size_t) i - 1] <= 0.0f) != (d[(size_t) i] <= 0.0f))
                    return (float) i / (float) (len - 1);
            }
    }
    return pos;
}

void PhreakAudioProcessor::randomizePoints (int flags, juce::Random& rng)
{
    juce::Array<int> loaded;
    for (int s = 0; s < numSlots; ++s)
        if (slots[(size_t) s].isLoaded()) loaded.add (s);

    const int count = juce::roundToInt (getParam (pointsId));
    static const int pitches[] = { 0, 0, 0, 0, -12, 12, -5, 7, -24, -7, 5, 3 };

    for (int p = 0; p < count; ++p)
    {
        int slot = juce::roundToInt (getParam (pointId (p, "Sample")));
        if ((flags & rndSamples) != 0 && ! loaded.isEmpty())
        {
            slot = loaded[rng.nextInt (loaded.size())];
            setParam (pointId (p, "Sample"), (float) slot);
        }
        if ((flags & rndPositions) != 0)
            setParam (pointId (p, "Pos"), 100.0f * snapPosition (slot, rng.nextFloat() * 0.98f));
        if ((flags & rndDirections) != 0)
        {
            const float r = rng.nextFloat();
            setParam (pointId (p, "Dir"), r < 0.6f ? 0.0f : (r < 0.85f ? 1.0f : 2.0f));
        }
        if ((flags & rndStutter) != 0)
            setParam (pointId (p, "Stutter"), rng.nextFloat() < 0.5f ? 0.0f : (float) (3 + rng.nextInt (7)));
        if ((flags & rndPitch) != 0)
            setParam (pointId (p, "Pitch"), (float) pitches[rng.nextInt ((int) std::size (pitches))]);
        if ((flags & rndFx) != 0)
        {
            setParam (pointId (p, "Rev"), rng.nextFloat() < 0.3f ? 20.0f + 60.0f * rng.nextFloat() : 0.0f);
            setParam (pointId (p, "Dly"), rng.nextFloat() < 0.3f ? 20.0f + 60.0f * rng.nextFloat() : 0.0f);
            setParam (pointId (p, "Filter"), rng.nextFloat() < 0.25f ? (rng.nextBool() ? -1.0f : 1.0f) * (20.0f + 50.0f * rng.nextFloat()) : 0.0f);
            setParam (pointId (p, "Pan"), rng.nextFloat() < 0.3f ? (float) rng.nextInt ({ -60, 61 }) : 0.0f);
        }
        if ((flags & rndTails) != 0)
            setParam (pointId (p, "Tail"), rng.nextFloat() < 0.5f ? 1.0f : 0.0f);
    }
}

void PhreakAudioProcessor::spreadPointsEvenly()
{
    const int count = juce::roundToInt (getParam (pointsId));
    for (int p = 0; p < count; ++p)
    {
        const int slot = juce::roundToInt (getParam (pointId (p, "Sample")));
        setParam (pointId (p, "Pos"), 100.0f * snapPosition (slot, (float) p / (float) count));
    }
}

void PhreakAudioProcessor::pointsOnTransients()
{
    const int count = juce::roundToInt (getParam (pointsId));
    for (int p = 0; p < count; ++p)
    {
        const int slot = juce::roundToInt (getParam (pointId (p, "Sample")));
        const auto& l = getSample (slot);
        if (! l.isLoaded() || l.data->onsets.empty()) continue;
        const auto& on = l.data->onsets;
        // répartit les points sur les transitoires, du début à la fin
        const size_t idx = on.size() <= (size_t) count ? (size_t) p % on.size()
                                                       : (size_t) std::round ((double) p * (double) (on.size() - 1) / juce::jmax (1, count - 1));
        setParam (pointId (p, "Pos"), 100.0f * (float) on[juce::jmin (idx, on.size() - 1)] / (float) (l.data->length() - 1));
    }
}

void PhreakAudioProcessor::randomizePattern (juce::Random& rng)
{
    const int count = juce::roundToInt (getParam (pointsId));
    for (int k = 0; k < numSteps; ++k)
    {
        const float r = rng.nextFloat();
        const int v = r < 0.18f ? phreak::stepTie : (r < 0.26f ? phreak::stepRest : 1 + rng.nextInt (count));
        setParam (stepId (k), (float) (k == 0 && v == phreak::stepTie ? 1 : v));
    }
}

void PhreakAudioProcessor::resetPattern()
{
    const int count = juce::roundToInt (getParam (pointsId));
    for (int k = 0; k < numSteps; ++k)
        setParam (stepId (k), (float) (k % count + 1));
}

//==============================================================================
juce::AudioProcessorEditor* PhreakAudioProcessor::createEditor()
{
    return new PhreakAudioProcessorEditor (*this);
}

void PhreakAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto samples = state.getOrCreateChildWithName ("SAMPLES", nullptr);
    for (int s = 0; s < numSlots; ++s)
    {
        samples.setProperty ("path" + juce::String (s + 1), slots[(size_t) s].path, nullptr);
        samples.setProperty ("name" + juce::String (s + 1), slots[(size_t) s].name, nullptr);
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhreakAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto samples = tree.getChildWithName ("SAMPLES");
    apvts.replaceState (tree);
    presets.restoreFromState();

    for (int s = 0; s < numSlots; ++s)
    {
        const auto path = samples.isValid() ? samples.getProperty ("path" + juce::String (s + 1)).toString() : juce::String();
        if (path == slots[(size_t) s].path && slots[(size_t) s].isLoaded())
            continue;
        if (path.isEmpty())
        {
            if (slots[(size_t) s].isLoaded()) clearSample (s);
            continue;
        }
        const juce::File f (path);
        if (! (f.existsAsFile() && loadSample (s, f)))
        {
            clearSample (s);
            slots[(size_t) s].path = path;
            slots[(size_t) s].name = samples.getProperty ("name" + juce::String (s + 1)).toString();
            slots[(size_t) s].missing = true;
            ++sampleGeneration;
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhreakAudioProcessor();
}
