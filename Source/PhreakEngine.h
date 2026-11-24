#pragma once

/**
    Phreak — moteur audio (sans JUCE).

    Un ou plusieurs samples (jusqu'à 4), 16 points de départ posés dessus.
    Un séquenceur de pas choisit quel point joue à chaque pas (GRID / CYCLE / RANDOM),
    calé sur le transport de l'hôte (SYNC) ou en temps libre (ms, avec DRIFT).
    Chaque point a son sens de lecture, son pitch (vitesse, façon bande), son LOOP
    (relance calée sur le tempo, avant les effets), son filtre, son pan, et ses envois
    vers la reverb et le delay.

    Ordre de passage d'un point à l'autre : GRID (grille), UP, DOWN, UP-DOWN, RANDOM,
    SHUFFLE (tous les points une fois par tour, ordre tiré), DRUNK (pas de ±1) ;
    HOLD = nombre de pas avant de passer au point suivant.

    Rafraîchissements (REFRESH) : tous les N temps, calés sur la grille de l'hôte, on tire
    à nouveau les positions des points, quels points bouclent, lesquels envoient dans la
    reverb, lesquels dans le delay. Un rythme commun (LINK) ou un par voie.

    Mode NOTE : une note longue fait tourner la séquence ; quand l'hôte joue, les pas
    restent calés sur sa grille (la note démarre sur le pas où elle tombe).

    TAIL par point : KEEP = la queue des effets continue après le changement de point ;
    CUT = quand on quitte ce point, les effets s'éteignent en ~6 ms (on bascule sur
    une unité d'effets neuve, l'ancienne s'efface puis est remise à zéro).

    Hasard : toujours tiré d'un hachage (SEED, numéro du pas, usage). Avec LOCK, le numéro
    du pas est pris modulo la longueur du motif : la séquence rejoue exactement pareil
    à chaque tour. Sans LOCK, elle change à chaque tour mais reste identique pour une même
    position dans le morceau (le rendu d'Ableton est donc reproductible).
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace phreak
{
constexpr int maxPoints = 16, maxSlots = 4, maxSteps = 64, maxVoices = 8, numFxUnits = 3;
constexpr int stepTie = 0, stepRest = maxPoints + 1;   // valeurs d'un pas : 0 = TIE, 1..16 = point, 17 = REST
constexpr int firstPointNote = 36;                     // C1 (Ableton) = point 1 ... D#2 = point 16
constexpr double pi = 3.14159265358979323846;

//==============================================================================
struct Sample
{
    std::vector<float> ch[2];      // toujours deux canaux (mono dupliqué)
    double sampleRate = 44100.0;
    std::vector<int> onsets;       // transitoires détectées (positions en échantillons)

    int length() const noexcept    { return (int) ch[0].size(); }
};

/** Divisions en noires. */
struct Divisions
{
    /** LOOP par point (sans OFF). */
    static const std::vector<double>& loop()
    {
        static const std::vector<double> d { 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625, 0.03125 };
        return d;
    }
    static const std::vector<const char*>& loopNames()
    {
        static const std::vector<const char*> n { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128" };
        return n;
    }
    /** Rafraîchissements, en temps (noires). */
    static const std::vector<double>& refresh()
    {
        static const std::vector<double> d { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
        return d;
    }
    static const std::vector<const char*>& refreshNames()
    {
        static const std::vector<const char*> n { "1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS", "16 BEATS", "32 BEATS", "64 BEATS" };
        return n;
    }
    static const std::vector<double>& rate()
    {
        static const std::vector<double> d { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        return d;
    }
    static const std::vector<const char*>& rateNames()
    {
        static const std::vector<const char*> n { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
        return n;
    }
    static const std::vector<double>& delay()
    {
        static const std::vector<double> d { 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
        return d;
    }
    static const std::vector<const char*>& delayNames()
    {
        static const std::vector<const char*> n { "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T", "1/32" };
        return n;
    }
};

enum Direction { forward = 0, reverse = 1, pingPong = 2 };
enum Mode { modeGrid = 0, modeUp, modeDown, modeUpDown, modeRandom, modeShuffle, modeDrunk, numModes };
constexpr int modeCycle = modeUp;
enum Lane { lanePos = 0, laneLoop, laneRev, laneDly, numLanes };
enum Snap { snapOff = 0, snapOnsets, snapZero };
enum Trigger { triggerTransport = 0, triggerNote = 1 };

struct PointParams
{
    int slot = 0;            // sample 0..3
    float pos = 0.0f;        // 0..1 dans le sample
    int dir = forward;
    float pitch = 0.0f;      // demi-tons
    float gain = 1.0f;       // linéaire
    float pan = 0.0f;        // -1..1
    float filter = 0.0f;     // -1 (passe-bas) .. 0 (rien) .. +1 (passe-haut)
    bool loop = false;       // LOOP : relance le point en boucle
    int loopRate = 4;        // index dans Divisions::loop (1/16)
    float rev = 0.0f, dly = 0.0f;   // envois 0..1
    bool tailKeep = true;
};

struct Settings
{
    int numPoints = 8;
    int mode = modeGrid;
    int steps = 16;
    int rate = 5;               // 1/16
    bool sync = true;
    float freeMs = 125.0f;
    float drift = 0.0f;         // 0..1 (temps libre seulement)
    float swing = 0.0f;         // 0..0.75
    float chance = 1.0f;        // probabilité qu'un pas déclenche (sinon TIE)
    float gate = 1.0f;          // fraction du pas
    std::uint32_t seed = 1;
    bool lock = true;
    int trigger = triggerTransport;
    float reverseChance = 0.0f;
    float scatterMs = 0.0f;
    float attackMs = 2.0f;
    float releaseMs = 10.0f;
    int hold = 1;               // nombre de pas avant de passer au point suivant
    int snap = snapOff;         // pour les positions rafraîchies

    // Rafraîchissements (re-tirage au hasard tous les N temps, calés sur la grille)
    struct LaneSettings { bool on = false; int rate = 2; float amount = 1.0f; };
    int refreshRate = 2;        // index dans Divisions::refresh (4 temps)
    bool link = true;           // tous les rafraîchissements suivent refreshRate
    LaneSettings lanes[numLanes] { { false, 2, 1.0f }, { false, 2, 0.4f }, { false, 2, 0.3f }, { false, 2, 0.3f } };
    bool refreshSamples = false;   // POSITIONS : tire aussi le sample parmi ceux chargés
    bool loopRandomRate = false;   // LOOPS : tire aussi la vitesse du loop

    float revSize = 0.6f, revDamp = 0.4f, revWidth = 1.0f, revLevel = 0.8f;
    int dlyDiv = 5;             // 1/8T
    float dlyFeedback = 0.45f, dlyTone = 0.6f, dlyLevel = 0.8f;
    bool dlyPing = true;
    float outGain = 1.0f;
    bool limiter = true;
};

struct Transport
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
};

struct MidiEvent
{
    int offset = 0;
    bool on = true;
    int note = 60;
    float velocity = 1.0f;
};

/** Pour les tests : un déclenchement (instant en échantillons depuis reset, point, pas). */
struct TriggerInfo
{
    std::int64_t time;
    int point;      // -1 = REST
    int step;
};

/** Ce qu'un point joue vraiment au moment où il est déclenché (après les rafraîchissements). */
struct Effective
{
    float pos = 0.0f;
    int slot = 0;
    bool loop = false;
    int loopRate = 4;
    float rev = 0.0f, dly = 0.0f;
    bool revLane = false, dlyLane = false;
};

//==============================================================================
inline double hashRandom (std::uint32_t seed, std::int64_t k, std::uint32_t salt) noexcept
{
    std::uint64_t z = (std::uint64_t) seed * 0x9E3779B97F4A7C15ull
                    ^ (std::uint64_t) k * 0xBF58476D1CE4E5B9ull
                    ^ ((std::uint64_t) salt + 1) * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double) (z >> 11) * (1.0 / 9007199254740992.0);
}

inline std::int64_t posMod (std::int64_t a, std::int64_t m) noexcept
{
    const auto r = a % m;
    return r < 0 ? r + m : r;
}

//==============================================================================
/** Freeverb (Jezar), sortie 100 % mouillée. */
class Reverb
{
public:
    void prepare (double sr)
    {
        static constexpr int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int allTuning[4] = { 556, 441, 341, 225 };
        const double scale = sr / 44100.0;
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 8; ++i)
                comb[c][i].buf.assign ((size_t) std::max (8, (int) ((combTuning[i] + c * 23) * scale)), 0.0f);
            for (int i = 0; i < 4; ++i)
                all[c][i].buf.assign ((size_t) std::max (8, (int) ((allTuning[i] + c * 23) * scale)), 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (auto& side : comb) for (auto& f : side) { std::fill (f.buf.begin(), f.buf.end(), 0.0f); f.idx = 0; f.store = 0.0f; }
        for (auto& side : all)  for (auto& f : side) { std::fill (f.buf.begin(), f.buf.end(), 0.0f); f.idx = 0; }
    }

    void set (float size, float damp, float width)
    {
        feedback = 0.7f + 0.28f * std::clamp (size, 0.0f, 1.0f);
        damp1 = 0.4f * std::clamp (damp, 0.0f, 1.0f);
        damp2 = 1.0f - damp1;
        wet1 = 0.5f + 0.5f * width;
        wet2 = 0.5f * (1.0f - width);
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float in = (inL + inR) * 0.015f;
        float o[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c)
        {
            float acc = 0.0f;
            for (auto& f : comb[c])
            {
                const float out = f.buf[(size_t) f.idx];
                f.store = out * damp2 + f.store * damp1;
                f.buf[(size_t) f.idx] = in + f.store * feedback;
                if (++f.idx >= (int) f.buf.size()) f.idx = 0;
                acc += out;
            }
            for (auto& f : all[c])
            {
                const float b = f.buf[(size_t) f.idx];
                const float out = b - acc;
                f.buf[(size_t) f.idx] = acc + b * 0.5f;
                if (++f.idx >= (int) f.buf.size()) f.idx = 0;
                acc = out;
            }
            o[c] = acc;
        }
        outL = o[0] * wet1 + o[1] * wet2;
        outR = o[1] * wet1 + o[0] * wet2;
    }

private:
    struct Comb { std::vector<float> buf; int idx = 0; float store = 0.0f; };
    struct All { std::vector<float> buf; int idx = 0; };
    Comb comb[2][8];
    All all[2][4];
    float feedback = 0.84f, damp1 = 0.2f, damp2 = 0.8f, wet1 = 1.0f, wet2 = 0.0f;
};

//==============================================================================
/** Delay stéréo (ping-pong ou non), passe-bas dans la réinjection. */
class Delay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& b : buf) b.assign ((size_t) (sr * 4.0) + 4, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        write = 0;
        lp[0] = lp[1] = 0.0f;
        timeNow = -1.0;
    }

    void set (double timeSamples, float fb, float tone, bool ping)
    {
        target = std::clamp (timeSamples, 16.0, (double) buf[0].size() - 4.0);
        if (timeNow < 0.0) timeNow = target;
        feedback = std::clamp (fb, 0.0f, 0.95f);
        const double hz = 500.0 * std::pow (36.0, (double) std::clamp (tone, 0.0f, 1.0f));   // 500 Hz .. 18 kHz
        lpCoef = (float) (1.0 - std::exp (-2.0 * pi * hz / sr));
        pingPong = ping;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        timeNow += (target - timeNow) * 0.0005;   // glisse doucement quand le tempo change
        const int size = (int) buf[0].size();
        double rp = (double) write - timeNow;
        if (rp < 0.0) rp += size;
        const int i0 = (int) rp;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float fr = (float) (rp - i0);
        const float dl = buf[0][(size_t) i0] + (buf[0][(size_t) i1] - buf[0][(size_t) i0]) * fr;
        const float dr = buf[1][(size_t) i0] + (buf[1][(size_t) i1] - buf[1][(size_t) i0]) * fr;

        lp[0] += (dl - lp[0]) * lpCoef;
        lp[1] += (dr - lp[1]) * lpCoef;

        if (pingPong)
        {
            buf[0][(size_t) write] = (inL + inR) * 0.5f + lp[1] * feedback;
            buf[1][(size_t) write] = lp[0] * feedback;
        }
        else
        {
            buf[0][(size_t) write] = inL + lp[0] * feedback;
            buf[1][(size_t) write] = inR + lp[1] * feedback;
        }
        if (++write >= size) write = 0;
        outL = dl;
        outR = dr;
    }

private:
    std::vector<float> buf[2];
    int write = 0;
    double sr = 44100.0, timeNow = -1.0, target = 1000.0;
    float feedback = 0.4f, lpCoef = 0.5f, lp[2] {};
    bool pingPong = true;
};

//==============================================================================
class Engine
{
public:
    Engine()
    {
        for (int k = 0; k < maxSteps; ++k)
            pattern[(size_t) k] = (k % 8) + 1;
        stepEvents.reserve (4200);
        seqNotes.reserve (128);
        pointNotes.reserve (128);
        for (int p = 0; p < maxPoints; ++p) { effPos[(size_t) p].store (-1.0f); effSlot[(size_t) p].store (0); effFlags[(size_t) p].store (0); }
    }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& u : fx) { u.reverb.prepare (sr); u.delay.prepare (sr); }
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = Voice();
        main = -1;
        for (auto& u : fx) { u.reverb.reset(); u.delay.reset(); u.state = FxUnit::idle; u.fade = 0.0f; }
        activeFx = 0;
        fx[0].state = FxUnit::active;
        fx[0].fade = 1.0f;
        wasPlaying = wasGridLocked = false;
        transportStep = 0;
        expectedPpq = -1.0e9;
        clockRunning = false;
        clockCountdown = 0.0;
        clockStep = 0;
        clockBeat = 0.0;
        lastBeat = 0.0;
        seqNotes.clear();
        pointNotes.clear();
        transpose = 0;
        sampleClock = 0;
        lastStepK = INT64_MIN;
        lastStepTime = -1;
        drunkPos = 0;
        drunkSeq = INT64_MIN;
        outGainNow = settings.outGain;
        displayStep.store (-1);
        displayPoint.store (-1);
        displayPos.store (-1.0f);
        refreshDisplay (0.0);
    }

    //==========================================================================
    void setSample (int slot, const Sample* s) noexcept           { samples[(size_t) slot] = s; }
    void setPoint (int i, const PointParams& p) noexcept          { points[(size_t) i] = p; }
    void setSettings (const Settings& s) noexcept                 { settings = s; }
    void setStep (int k, int value) noexcept                      { pattern[(size_t) k] = std::clamp (value, 0, stepRest); }

    const Settings& getSettings() const noexcept                  { return settings; }
    const PointParams& getPoint (int i) const noexcept            { return points[(size_t) i]; }

    std::vector<TriggerInfo>* triggerLog = nullptr;   // tests seulement

    // Affichage (lus par l'interface)
    std::atomic<int> displayStep { -1 }, displayPoint { -1 }, displaySlot { -1 };
    std::atomic<float> displayPos { -1.0f };
    /** Ce que chaque point joue en ce moment (après rafraîchissement) : position, sample, bits 1 LOOP / 2 REVERB / 4 DELAY. */
    std::array<std::atomic<float>, maxPoints> effPos;
    std::array<std::atomic<int>, maxPoints> effSlot, effFlags;

    //==========================================================================
    void process (float* outL, float* outR, int n, const Transport& t, const MidiEvent* events, int numEvents)
    {
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);

        bpm = t.bpm > 1.0 ? t.bpm : 120.0;
        spq = 60.0 / bpm * sr;
        updateFxParams();
        for (auto& v : voices)
            if (v.active)
                updateVoice (v);

        stepEvents.clear();
        const bool transportMode = settings.trigger == triggerTransport;
        const bool playing = t.playing;
        const bool jump = playing && (! wasPlaying || std::abs (t.ppq - expectedPpq) > 8.0 / spq);
        gridLocked = settings.sync && playing;
        blockPpq = t.ppq;

        if (gridLocked)
        {
            // Pas calés sur la grille de l'hôte (mode TRANSPORT, et mode NOTE quand l'hôte joue)
            clockRunning = false;
            if (jump || ! wasGridLocked)
            {
                transportStep = stepIndexAt (t.ppq);
                if (transportMode || ! seqNotes.empty())
                    stepEvents.push_back ({ 0, transportStep });
                drunkSeq = INT64_MIN;
            }
            else
            {
                // RATE ou SWING changés pendant la lecture : on se recale sans redéclencher
                const auto here = stepIndexAt (t.ppq + 0.5 / spq);
                if (here != transportStep && here != transportStep + 1)
                    transportStep = here;
            }
            for (int guard = 0; guard < 4096; ++guard)
            {
                const double next = stepStart (transportStep + 1);
                const double off = std::ceil ((next - t.ppq) * spq - 1.0e-6);
                if (off >= n) break;
                ++transportStep;
                stepEvents.push_back ({ (int) std::max (0.0, off), transportStep });
            }
        }
        else if (transportMode)
        {
            if (playing)
            {
                if (jump || ! clockRunning)
                    startClock();
            }
            else
            {
                clockRunning = false;
            }
        }
        else
        {
            // mode NOTE sans grille : l'horloge interne suit les notes
            if (wasGridLocked && ! seqNotes.empty() && ! clockRunning)
                startClock();
            if (seqNotes.empty())
                clockRunning = false;
        }

        if (transportMode && wasPlaying && ! playing)
            stopSequence();
        wasPlaying = playing;
        wasGridLocked = gridLocked;
        if (playing)
            expectedPpq = t.ppq + n / spq;

        // --- Rendu par segments entre les événements ---
        int i = 0, ev = 0;
        size_t se = 0;
        currentOffset = 0;
        while (i < n)
        {
            int next = n;
            if (ev < numEvents) next = std::min (next, std::max (i, events[ev].offset));
            if (se < stepEvents.size()) next = std::min (next, stepEvents[se].offset);
            if (clockRunning) next = std::min (next, i + std::max (0, (int) std::ceil (clockCountdown - 1.0e-9)));
            next = std::clamp (next, i, n);

            render (outL, outR, i, next);
            if (clockRunning) clockCountdown -= (next - i);
            i = next;
            if (i >= n) break;

            while (ev < numEvents && events[ev].offset <= i)
                handleMidi (events[ev++]);
            while (se < stepEvents.size() && stepEvents[se].offset <= i)
            {
                const auto k = stepEvents[se++].step;
                if (transportMode || ! seqNotes.empty())
                    doStep (k, stepLengthSync (k), stepStart (k));
                else
                    displayStep.store (-1);
            }
            if (clockRunning && clockCountdown <= 0.0)
            {
                const double len = stepLengthClock (clockStep);
                doStep (clockStep, len, clockBeat);
                ++clockStep;
                clockBeat += len / spq;
                clockCountdown += len;
            }
        }
        while (ev < numEvents)
            handleMidi (events[ev++]);

        // Sortie
        const float target = settings.outGain;
        for (int s = 0; s < n; ++s)
        {
            outGainNow += (target - outGainNow) * 0.002f;
            float l = outL[s] * outGainNow, r = outR[s] * outGainNow;
            if (settings.limiter) { l = softClip (l); r = softClip (r); }
            outL[s] = std::isfinite (l) ? l : 0.0f;
            outR[s] = std::isfinite (r) ? r : 0.0f;
        }

        sampleClock += n;

        // Affichage
        if (main >= 0 && voices[(size_t) main].active)
        {
            const auto& v = voices[(size_t) main];
            const Sample* smp = samples[(size_t) v.slot];
            displayPoint.store (v.point);
            displaySlot.store (v.slot);
            displayPos.store (smp != nullptr && smp->length() > 1 ? (float) (v.pos / (smp->length() - 1)) : -1.0f);
        }
        else
        {
            displayPoint.store (-1);
            displayPos.store (-1.0f);
        }
        const bool sequencing = (transportMode && playing) || (! transportMode && ! seqNotes.empty());
        if (! sequencing)
        {
            displayStep.store (-1);
            if (++idleBlocks > 8) { idleBlocks = 0; refreshDisplay (lastBeat); }   // l'interface suit les réglages même à l'arrêt
        }
    }

    /** Nombre de voix qui jouent (tests). */
    int activeVoices() const noexcept
    {
        int c = 0;
        for (auto& v : voices) c += v.active ? 1 : 0;
        return c;
    }

    int mainPoint() const noexcept      { return main >= 0 && voices[(size_t) main].active ? voices[(size_t) main].point : -1; }

    /** Ce que le point p jouerait au temps `beat` (tests et affichage). */
    Effective effective (int p, double beat) const noexcept
    {
        p = std::clamp (p, 0, maxPoints - 1);
        const auto& pp = points[(size_t) p];
        Effective e;
        e.pos = std::clamp (pp.pos, 0.0f, 1.0f);
        e.slot = std::clamp (pp.slot, 0, maxSlots - 1);
        e.loop = pp.loop;
        e.loopRate = pp.loopRate;
        e.rev = pp.rev;
        e.dly = pp.dly;

        const auto& L = settings.lanes;
        const std::uint32_t seed = settings.seed;

        if (L[lanePos].on)
        {
            const auto c = cycleOf (lanePos, beat);
            if (settings.refreshSamples)
            {
                int loaded[maxSlots], count = 0;
                for (int s = 0; s < maxSlots; ++s)
                    if (samples[(size_t) s] != nullptr && samples[(size_t) s]->length() >= 8) loaded[count++] = s;
                if (count > 0)
                    e.slot = loaded[std::min (count - 1, (int) (hashRandom (seed, c, 300u + (std::uint32_t) p) * count))];
            }
            const double r = hashRandom (seed, c, 100u + (std::uint32_t) p);
            const float amount = std::clamp (L[lanePos].amount, 0.0f, 1.0f);
            double pos = amount >= 0.999f ? r * 0.98 : pp.pos + (r - 0.5) * amount;
            if (pos < 0.0) pos = -pos;
            if (pos > 0.98) pos = 1.96 - pos;
            e.pos = snapPos (e.slot, (float) std::clamp (pos, 0.0, 0.98));
        }
        if (L[laneLoop].on)
        {
            const auto c = cycleOf (laneLoop, beat);
            e.loop = hashRandom (seed, c, 200u + (std::uint32_t) p) < L[laneLoop].amount;
            if (settings.loopRandomRate)
            {
                static constexpr int choices[] = { 2, 3, 4, 4, 5, 6, 6, 8 };   // 1/8 .. 1/64, surtout 1/16 et 1/32
                e.loopRate = choices[std::min (7, (int) (hashRandom (seed, c, 250u + (std::uint32_t) p) * 8.0))];
            }
        }
        if (L[laneRev].on)
        {
            const auto c = cycleOf (laneRev, beat);
            const bool on = hashRandom (seed, c, 400u + (std::uint32_t) p) < L[laneRev].amount;
            e.rev = on ? (pp.rev > 0.001f ? pp.rev : 0.5f) : 0.0f;
            e.revLane = true;
        }
        if (L[laneDly].on)
        {
            const auto c = cycleOf (laneDly, beat);
            const bool on = hashRandom (seed, c, 500u + (std::uint32_t) p) < L[laneDly].amount;
            e.dly = on ? (pp.dly > 0.001f ? pp.dly : 0.5f) : 0.0f;
            e.dlyLane = true;
        }
        return e;
    }

    /** Numéro du cycle de rafraîchissement d'une voie au temps `beat`. */
    std::int64_t cycleOf (int lane, double beat) const noexcept
    {
        const int idx = settings.link ? settings.refreshRate : settings.lanes[lane].rate;
        const double len = Divisions::refresh()[(size_t) std::clamp (idx, 0, (int) Divisions::refresh().size() - 1)];
        return (std::int64_t) std::floor (beat / len + 1.0e-7);
    }

private:
    //==========================================================================
    struct Voice
    {
        bool active = false, releasing = false, fromNote = false;
        int point = 0, slot = 0, fxUnit = 0, dir = forward;
        double pos = 0.0, startPos = 0.0, lo = 0.0, hi = 0.0, rate = 1.0;
        int ppDir = 1;
        double loopPeriod = 0.0, loopCount = 0.0, loopRamp = 1.0;
        float env = 0.0f, atk = 1.0f, rel = 1.0f;
        std::int64_t gateLeft = -1;
        float velocity = 1.0f;
        int transpose = 0;
        bool reversed = false;
        bool revLane = false, dlyLane = false;
        float effRev = 0.0f, effDly = 0.0f;

        float gl = 0.0f, gr = 0.0f, tgl = 0.0f, tgr = 0.0f;
        float rev = 0.0f, dly = 0.0f, trev = 0.0f, tdly = 0.0f;
        int filterMode = 0;
        float a1 = 0, a2 = 0, a3 = 0, k = 1.1f;
        float ic1[2] {}, ic2[2] {};
    };

    struct FxUnit
    {
        enum State { idle, active, dying };
        Reverb reverb;
        Delay delay;
        State state = idle;
        float fade = 0.0f;
        float inRev[2] {}, inDly[2] {};
    };

    struct StepEvent { int offset; std::int64_t step; };

    static float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float over = a - 0.8f;
        const float y = 0.8f + 0.2f * std::tanh (over / 0.2f);
        return x < 0.0f ? -y : y;
    }

    static std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    //==========================================================================
    float snapPos (int slot, float pos) const noexcept
    {
        const Sample* s = samples[(size_t) slot];
        if (s == nullptr || s->length() < 8 || settings.snap == snapOff) return pos;
        const int len = s->length();
        const int at = std::clamp ((int) std::lround (pos * (len - 1)), 0, len - 1);
        if (settings.snap == snapOnsets)
        {
            const auto& on = s->onsets;
            if (on.empty()) return pos;
            auto it = std::lower_bound (on.begin(), on.end(), at);
            int best = it == on.end() ? on.back() : *it;
            if (it != on.begin() && (it == on.end() || at - *(it - 1) < *it - at))
                best = *(it - 1);
            return (float) best / (float) (len - 1);
        }
        const auto& d = s->ch[0];
        const int range = (int) (0.005 * s->sampleRate);
        for (int off = 0; off < range; ++off)
            for (int sgn = -1; sgn <= 1; sgn += 2)
            {
                const int i = at + sgn * off;
                if (i > 0 && i < len && (d[(size_t) i - 1] <= 0.0f) != (d[(size_t) i] <= 0.0f))
                    return (float) i / (float) (len - 1);
            }
        return pos;
    }

    void refreshDisplay (double beat)
    {
        for (int p = 0; p < maxPoints; ++p)
        {
            const auto e = effective (p, beat);
            effPos[(size_t) p].store (e.pos);
            effSlot[(size_t) p].store (e.slot);
            effFlags[(size_t) p].store ((e.loop ? 1 : 0) | (e.rev > 0.001f ? 2 : 0) | (e.dly > 0.001f ? 4 : 0));
        }
    }

    //==========================================================================
    // Temps
    double rateQN() const noexcept         { return Divisions::rate()[(size_t) std::clamp (settings.rate, 0, (int) Divisions::rate().size() - 1)]; }
    double swingAmount() const noexcept    { return std::clamp ((double) settings.swing, 0.0, 0.75) * 0.5; }

    double stepStart (std::int64_t k) const noexcept
    {
        const double L = rateQN();
        const std::int64_t pair = k >= 0 ? k / 2 : (k - 1) / 2;
        const bool odd = (k - pair * 2) == 1;
        return (double) pair * 2.0 * L + (odd ? L * (1.0 + swingAmount()) : 0.0);
    }

    std::int64_t stepIndexAt (double ppq) const noexcept
    {
        const double L = rateQN();
        const auto pair = (std::int64_t) std::floor (ppq / (2.0 * L));
        const double r = ppq - (double) pair * 2.0 * L;
        return pair * 2 + (r >= L * (1.0 + swingAmount()) - 1.0e-9 ? 1 : 0);
    }

    double stepLengthSync (std::int64_t k) const noexcept   { return (stepStart (k + 1) - stepStart (k)) * spq; }

    double stepLengthClock (std::int64_t k) const noexcept
    {
        double len;
        if (settings.sync)
        {
            len = rateQN() * spq;
            const double s = swingAmount();
            len *= (posMod (k, 2) == 0) ? 1.0 + s : 1.0 - s;
        }
        else
        {
            len = std::max (1.0, (double) settings.freeMs) * 0.001 * sr;
            if (settings.drift > 0.0f)
            {
                const double r = hashRandom (settings.seed, randomKey (k), 7) * 2.0 - 1.0;
                len *= std::pow (2.0, r * settings.drift);
            }
        }
        return std::max (16.0, len);
    }

    std::int64_t randomKey (std::int64_t k) const noexcept
    {
        return settings.lock ? posMod (k, std::max (1, settings.steps)) : k;
    }

    void startClock()
    {
        clockRunning = true;
        clockStep = 0;
        clockCountdown = 0.0;
        clockBeat = 0.0;
        drunkSeq = INT64_MIN;
    }

    //==========================================================================
    int pickPoint (std::int64_t seq, int n)
    {
        switch (settings.mode)
        {
            case modeDown:   return n - 1 - (int) posMod (seq, n);
            case modeUpDown:
            {
                if (n == 1) return 0;
                const int period = 2 * n - 2;
                const int r = (int) posMod (seq, period);
                return r < n ? r : period - r;
            }
            case modeRandom: return std::min (n - 1, (int) (hashRandom (settings.seed, seq, 2) * n));
            case modeShuffle:
            {
                // chaque tour joue tous les points une fois, dans un ordre tiré au hasard
                const auto cycle = floorDiv (seq, n);
                int perm[maxPoints];
                for (int i = 0; i < n; ++i) perm[i] = i;
                for (int i = n - 1; i > 0; --i)
                {
                    const int j = std::min (i, (int) (hashRandom (settings.seed, cycle, 600u + (std::uint32_t) i) * (i + 1)));
                    std::swap (perm[i], perm[j]);
                }
                return perm[posMod (seq, n)];
            }
            case modeDrunk:
            {
                if (seq != drunkSeq)
                {
                    if (drunkSeq == INT64_MIN) drunkPos = 0;
                    else
                    {
                        const double r = hashRandom (settings.seed, seq, 5);
                        drunkPos = (int) posMod (drunkPos + (r < 0.42 ? -1 : (r < 0.84 ? 1 : 0)), n);
                    }
                    drunkSeq = seq;
                }
                return std::clamp (drunkPos, 0, n - 1);
            }
            default:         return (int) posMod (seq, n);
        }
    }

    void doStep (std::int64_t k, double lengthSamples, double beat)
    {
        const int steps = std::clamp (settings.steps, 1, maxSteps);
        const int pos = (int) posMod (k, steps);
        displayStep.store (pos);
        lastBeat = beat;
        refreshDisplay (beat);

        // même pas au même instant (note posée pile sur un pas) : une seule fois
        const auto now = sampleClock + currentOffset;
        if (k == lastStepK && now == lastStepTime) return;
        lastStepK = k;
        lastStepTime = now;

        if (! pointNotes.empty())
            return;   // une note de point tenue a la priorité

        const auto key = randomKey (k);
        const std::int64_t gate = settings.gate >= 0.999f ? -1 : std::max<std::int64_t> (1, (std::int64_t) (settings.gate * lengthSamples));
        const bool playingNow = main >= 0 && voices[(size_t) main].active && ! voices[(size_t) main].releasing;

        const int numPoints = std::clamp (settings.numPoints, 1, maxPoints);
        int point = -1;
        if (settings.mode == modeGrid)
        {
            if (hashRandom (settings.seed, key, 1) >= settings.chance) { extendMain (gate); return; }
            const int v = pattern[(size_t) pos];
            if (v == stepTie) { extendMain (gate); return; }
            if (v >= stepRest) { leaveMain(); log (-1, pos); return; }
            point = v - 1;
        }
        else
        {
            const int hold = std::clamp (settings.hold, 1, 16);
            const auto idx = settings.lock ? posMod (k, steps) : k;
            if (posMod (idx, hold) != 0 && playingNow) { extendMain (gate); return; }
            if (hashRandom (settings.seed, key, 1) >= settings.chance) { extendMain (gate); return; }
            point = pickPoint (floorDiv (idx, hold), numPoints);
        }

        const bool rev = hashRandom (settings.seed, key, 3) < settings.reverseChance;
        const double scatter = hashRandom (settings.seed, key, 4) * 2.0 - 1.0;
        triggerPoint (point, 1.0f, gate, rev, scatter, false, beat);
        log (point, pos);
    }

    void log (int point, int step)
    {
        if (triggerLog != nullptr)
            triggerLog->push_back ({ sampleClock + currentOffset, point, step });
    }

    void extendMain (std::int64_t gate)
    {
        if (main >= 0 && voices[(size_t) main].active && ! voices[(size_t) main].releasing && ! voices[(size_t) main].fromNote)
            voices[(size_t) main].gateLeft = gate;
    }

    void leaveMain()
    {
        if (main >= 0 && voices[(size_t) main].active)
        {
            if (! points[(size_t) voices[(size_t) main].point].tailKeep)
                cutFx();
            voices[(size_t) main].releasing = true;
        }
        main = -1;
    }

    void stopSequence()
    {
        if (pointNotes.empty())
            leaveMain();
    }

    void triggerPoint (int p, float velocity, std::int64_t gate, bool reverseIt, double scatter, bool fromNote, double beat)
    {
        p = std::clamp (p, 0, maxPoints - 1);
        const auto& pp = points[(size_t) p];
        const auto eff = effective (p, beat);
        const Sample* s = samples[(size_t) eff.slot];
        if (s == nullptr || s->length() < 8)
        {
            leaveMain();
            return;
        }

        const bool change = main < 0 || ! voices[(size_t) main].active || voices[(size_t) main].point != p;
        if (main >= 0 && voices[(size_t) main].active)
        {
            if (change && ! points[(size_t) voices[(size_t) main].point].tailKeep)
                cutFx();
            voices[(size_t) main].releasing = true;
        }

        const int vi = allocateVoice();
        auto& v = voices[(size_t) vi];
        v = Voice();
        v.active = true;
        v.point = p;
        v.slot = eff.slot;
        v.fxUnit = activeFx;
        v.fromNote = fromNote;
        v.velocity = velocity;
        v.transpose = transpose;
        v.gateLeft = gate;
        v.reversed = reverseIt;
        v.revLane = eff.revLane;
        v.dlyLane = eff.dlyLane;
        v.effRev = eff.rev;
        v.effDly = eff.dly;
        v.dir = pp.dir;
        if (reverseIt && v.dir != pingPong)
            v.dir = v.dir == forward ? reverse : forward;
        v.ppDir = (reverseIt && v.dir == pingPong) ? -1 : 1;

        const int len = s->length();
        double start = (double) eff.pos * (len - 1);
        start += scatter * settings.scatterMs * 0.001 * s->sampleRate;
        start = std::clamp (start, 0.0, (double) len - 2.0);
        v.pos = v.startPos = start;

        const double attackSamples = std::max (1.0, settings.attackMs * 0.001 * sr);
        v.atk = (float) (1.0 / attackSamples);
        v.rel = (float) (1.0 / std::max (8.0, settings.releaseMs * 0.001 * sr));
        v.env = 0.0f;

        updateVoice (v);
        v.gl = v.tgl; v.gr = v.tgr; v.rev = v.trev; v.dly = v.tdly;

        const double div = eff.loop ? Divisions::loop()[(size_t) std::clamp (eff.loopRate, 0, (int) Divisions::loop().size() - 1)] : 0.0;
        if (div > 0.0 && v.dir != pingPong)
        {
            v.loopPeriod = std::max (32.0, div * spq);
            v.loopRamp = std::clamp (attackSamples, 8.0, v.loopPeriod * 0.25);
            v.loopCount = 0.0;
        }
        if (v.dir == pingPong)
        {
            const double windowOut = (div > 0.0 ? div : 0.25) * spq;
            const double window = std::max (8.0, windowOut * std::abs (v.rate));
            v.lo = start;
            v.hi = std::min ((double) len - 2.0, start + window);
            if (v.hi - v.lo < 8.0) { v.lo = std::max (0.0, v.hi - window); }
            if (v.hi - v.lo < 8.0) v.dir = forward;
        }

        main = vi;
    }

    int allocateVoice()
    {
        for (int i = 0; i < maxVoices; ++i)
            if (! voices[(size_t) i].active)
                return i;
        int best = -1;
        float lowest = 2.0f;
        for (int i = 0; i < maxVoices; ++i)
            if (i != main && voices[(size_t) i].env < lowest)
            {
                lowest = voices[(size_t) i].env;
                best = i;
            }
        return best < 0 ? 0 : best;
    }

    void cutFx()
    {
        int fresh = -1;
        for (int u = 0; u < numFxUnits; ++u)
            if (fx[(size_t) u].state == FxUnit::idle) { fresh = u; break; }
        if (fresh < 0)
        {
            float lowest = 2.0f;
            for (int u = 0; u < numFxUnits; ++u)
                if (u != activeFx && fx[(size_t) u].fade < lowest) { lowest = fx[(size_t) u].fade; fresh = u; }
            fx[(size_t) fresh].reverb.reset();
            fx[(size_t) fresh].delay.reset();
        }
        fx[(size_t) activeFx].state = FxUnit::dying;
        activeFx = fresh;
        fx[(size_t) fresh].state = FxUnit::active;
        fx[(size_t) fresh].fade = 1.0f;
        updateFxParams();
    }

    //==========================================================================
    void handleMidi (const MidiEvent& e)
    {
        const int p = e.note - firstPointNote;
        const bool pointNote = p >= 0 && p < maxPoints;

        if (pointNote)
        {
            if (e.on && e.velocity > 0.0f)
            {
                pointNotes.erase (std::remove (pointNotes.begin(), pointNotes.end(), e.note), pointNotes.end());
                pointNotes.push_back (e.note);
                triggerPoint (p, std::clamp (e.velocity, 0.0f, 1.0f), -1, false, 0.0, true, currentBeat());
                log (p, -1);
            }
            else
            {
                const bool wasTop = ! pointNotes.empty() && pointNotes.back() == e.note;
                pointNotes.erase (std::remove (pointNotes.begin(), pointNotes.end(), e.note), pointNotes.end());
                if (wasTop)
                {
                    if (! pointNotes.empty())
                        triggerPoint (pointNotes.back() - firstPointNote, 1.0f, -1, false, 0.0, true, currentBeat());
                    else if (main >= 0 && voices[(size_t) main].fromNote)
                        leaveMain();
                }
            }
            return;
        }

        if (e.on && e.velocity > 0.0f)
        {
            const bool first = seqNotes.empty();
            seqNotes.erase (std::remove (seqNotes.begin(), seqNotes.end(), e.note), seqNotes.end());
            seqNotes.push_back (e.note);
            transpose = std::clamp (e.note - 60, -48, 48);
            if (settings.trigger == triggerNote && first)
            {
                if (gridLocked)
                {
                    // note longue + Ableton qui joue : on démarre sur le pas de la grille où tombe la note
                    const double ppq = blockPpq + currentOffset / spq;
                    const auto k = stepIndexAt (ppq + 0.5 / spq);
                    drunkSeq = INT64_MIN;
                    doStep (k, (stepStart (k + 1) - ppq) * spq, stepStart (k));
                }
                else if (! clockRunning)
                {
                    startClock();
                }
            }
        }
        else
        {
            seqNotes.erase (std::remove (seqNotes.begin(), seqNotes.end(), e.note), seqNotes.end());
            transpose = seqNotes.empty() ? 0 : std::clamp (seqNotes.back() - 60, -48, 48);
            if (settings.trigger == triggerNote && seqNotes.empty())
            {
                clockRunning = false;
                stopSequence();
            }
        }
    }

    double currentBeat() const noexcept
    {
        if (gridLocked) return blockPpq + currentOffset / spq;
        return lastBeat;
    }

    //==========================================================================
    void updateVoice (Voice& v)
    {
        const auto& pp = points[(size_t) v.point];
        const Sample* s = samples[(size_t) v.slot];
        const double srcRate = s != nullptr ? s->sampleRate : sr;
        const double speed = std::pow (2.0, (pp.pitch + (float) v.transpose) / 12.0) * srcRate / sr;
        v.rate = v.dir == reverse ? -speed : speed;

        const float pan = std::clamp (pp.pan, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * (float) pi;
        const float g = pp.gain * v.velocity;
        v.tgl = g * std::cos (angle) * 1.41421356f;
        v.tgr = g * std::sin (angle) * 1.41421356f;
        v.trev = std::clamp (v.revLane ? v.effRev : pp.rev, 0.0f, 1.0f);
        v.tdly = std::clamp (v.dlyLane ? v.effDly : pp.dly, 0.0f, 1.0f);

        const float f = std::clamp (pp.filter, -1.0f, 1.0f);
        if (std::abs (f) < 0.01f)
        {
            v.filterMode = 0;
        }
        else
        {
            double fc = f < 0.0f ? 20000.0 * std::pow (80.0 / 20000.0, (double) -f)
                                 : 20.0 * std::pow (8000.0 / 20.0, (double) f);
            fc = std::min (fc, sr * 0.45);
            const double gg = std::tan (pi * fc / sr);
            const double k = 1.0 / 0.9;
            v.k = (float) k;
            v.a1 = (float) (1.0 / (1.0 + gg * (gg + k)));
            v.a2 = (float) (gg * v.a1);
            v.a3 = (float) (gg * v.a2);
            const int mode = f < 0.0f ? 1 : 2;
            if (mode != v.filterMode) { v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.0f; }
            v.filterMode = mode;
        }
    }

    void updateFxParams()
    {
        const double dTime = Divisions::delay()[(size_t) std::clamp (settings.dlyDiv, 0, (int) Divisions::delay().size() - 1)] * spq;
        for (auto& u : fx)
        {
            u.reverb.set (settings.revSize, settings.revDamp, settings.revWidth);
            u.delay.set (dTime, settings.dlyFeedback, settings.dlyTone, settings.dlyPing);
        }
    }

    static inline float readCubic (const std::vector<float>& d, double pos) noexcept
    {
        const int n = (int) d.size();
        const int i = (int) std::floor (pos);
        if (i < 0 || i >= n - 1) return 0.0f;
        const float f = (float) (pos - i);
        const float y0 = d[(size_t) std::max (0, i - 1)], y1 = d[(size_t) i], y2 = d[(size_t) (i + 1)], y3 = d[(size_t) std::min (n - 1, i + 2)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void render (float* outL, float* outR, int from, int to)
    {
        if (to <= from) { currentOffset = from; return; }

        for (int s = from; s < to; ++s)
        {
            for (auto& u : fx) { u.inRev[0] = u.inRev[1] = u.inDly[0] = u.inDly[1] = 0.0f; }

            float sumL = 0.0f, sumR = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const Sample* smp = samples[(size_t) v.slot];
                if (smp == nullptr || smp->length() < 8) { v.active = false; continue; }

                if (v.releasing)
                {
                    v.env -= v.rel;
                    if (v.env <= 0.0f) { v.active = false; continue; }
                }
                else if (v.env < 1.0f)
                {
                    v.env = std::min (1.0f, v.env + v.atk);
                }
                if (v.gateLeft > 0 && --v.gateLeft == 0)
                    v.releasing = true;

                // LOOP : retour au départ toutes les divisions, fondus de la durée d'ATTACK
                float lg = 1.0f;
                if (v.loopPeriod > 0.0)
                {
                    if (v.loopCount >= v.loopPeriod)
                    {
                        v.loopCount -= v.loopPeriod;
                        v.pos = v.startPos;
                    }
                    const double edge = std::min (v.loopCount + 1.0, v.loopPeriod - v.loopCount);
                    lg = (float) std::clamp (edge / v.loopRamp, 0.0, 1.0);
                    v.loopCount += 1.0;
                }

                float l = readCubic (smp->ch[0], v.pos);
                float r = readCubic (smp->ch[1], v.pos);

                if (v.dir == pingPong)
                {
                    v.pos += std::abs (v.rate) * v.ppDir;
                    if (v.pos >= v.hi) { v.pos = 2.0 * v.hi - v.pos; v.ppDir = -1; }
                    if (v.pos <= v.lo) { v.pos = 2.0 * v.lo - v.pos; v.ppDir = 1; }
                    v.pos = std::clamp (v.pos, v.lo, v.hi);
                }
                else
                {
                    v.pos += v.rate;
                }

                if (v.filterMode != 0)
                {
                    float* io[2] = { &l, &r };
                    for (int c = 0; c < 2; ++c)
                    {
                        const float x = *io[c];
                        const float v3 = x - v.ic2[c];
                        const float v1 = v.a1 * v.ic1[c] + v.a2 * v3;
                        const float v2 = v.ic2[c] + v.a2 * v.ic1[c] + v.a3 * v3;
                        v.ic1[c] = 2.0f * v1 - v.ic1[c];
                        v.ic2[c] = 2.0f * v2 - v.ic2[c];
                        *io[c] = v.filterMode == 1 ? v2 : x - v.k * v1 - v2;
                    }
                }

                v.gl += (v.tgl - v.gl) * 0.003f;
                v.gr += (v.tgr - v.gr) * 0.003f;
                v.rev += (v.trev - v.rev) * 0.003f;
                v.dly += (v.tdly - v.dly) * 0.003f;

                // le LOOP passe avant la reverb et le delay : les envois prennent le son déjà bouclé
                const float e = v.env * lg;
                l *= e * v.gl;
                r *= e * v.gr;
                sumL += l;
                sumR += r;

                auto& u = fx[(size_t) v.fxUnit];
                u.inRev[0] += l * v.rev; u.inRev[1] += r * v.rev;
                u.inDly[0] += l * v.dly; u.inDly[1] += r * v.dly;
            }

            const float fadeStep = (float) (1.0 / (0.006 * sr));
            for (auto& u : fx)
            {
                if (u.state == FxUnit::idle) continue;
                float rl, rr, dl, dr;
                u.reverb.process (u.inRev[0], u.inRev[1], rl, rr);
                u.delay.process (u.inDly[0], u.inDly[1], dl, dr);
                const float level = u.fade;
                sumL += (rl * settings.revLevel * 3.0f + dl * settings.dlyLevel) * level;
                sumR += (rr * settings.revLevel * 3.0f + dr * settings.dlyLevel) * level;
                if (u.state == FxUnit::dying)
                {
                    u.fade -= fadeStep;
                    if (u.fade <= 0.0f)
                    {
                        u.fade = 0.0f;
                        u.state = FxUnit::idle;
                        u.reverb.reset();
                        u.delay.reset();
                    }
                }
            }

            outL[s] += sumL;
            outR[s] += sumR;
        }
        currentOffset = to;
    }

    //==========================================================================
    double sr = 44100.0, bpm = 120.0, spq = 22050.0;
    Settings settings;
    std::array<PointParams, maxPoints> points {};
    std::array<int, maxSteps> pattern {};
    std::array<const Sample*, maxSlots> samples {};

    std::array<Voice, maxVoices> voices {};
    int main = -1;
    std::array<FxUnit, numFxUnits> fx {};
    int activeFx = 0;

    bool wasPlaying = false, wasGridLocked = false, gridLocked = false;
    double blockPpq = 0.0;
    std::int64_t transportStep = 0;
    double expectedPpq = -1.0e9;
    std::vector<StepEvent> stepEvents;

    bool clockRunning = false;
    double clockCountdown = 0.0, clockBeat = 0.0, lastBeat = 0.0;
    std::int64_t clockStep = 0;

    std::vector<int> seqNotes, pointNotes;
    int transpose = 0;
    std::int64_t sampleClock = 0;
    int currentOffset = 0;
    float outGainNow = 1.0f;
    std::int64_t lastStepK = INT64_MIN, lastStepTime = -1, drunkSeq = INT64_MIN;
    int drunkPos = 0, idleBlocks = 0;
};
}
