// Tests du moteur Phreak (sans JUCE).
#include "PhreakEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

using namespace phreak;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("ECHEC l.%d : %s  ", __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

constexpr double SR = 48000.0;

static Sample makeSine (double seconds, double hz = 220.0)
{
    Sample s;
    s.sampleRate = SR;
    const int n = (int) (seconds * SR);
    for (auto& c : s.ch) c.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        s.ch[0][(size_t) i] = s.ch[1][(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * hz * i / SR);
    return s;
}

static Sample makeRamp (double seconds)
{
    Sample s;
    s.sampleRate = SR;
    const int n = (int) (seconds * SR);
    for (auto& c : s.ch) c.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        s.ch[0][(size_t) i] = s.ch[1][(size_t) i] = (float) i / (float) n;
    return s;
}

struct Rig
{
    Engine e;
    std::vector<float> L, R;
    std::vector<TriggerInfo> log;
    double ppq = 0.0;

    Rig() { e.prepare (SR, 512); e.triggerLog = &log; }

    /** Rend `samples` échantillons avec le transport (playing) en blocs de `block`. */
    void run (int samples, bool playing, int block = 512, double bpm = 120.0, std::vector<MidiEvent> midi = {})
    {
        std::vector<float> l ((size_t) block), r ((size_t) block);
        int done = 0;
        while (done < samples)
        {
            const int n = std::min (block, samples - done);
            std::vector<MidiEvent> ev;
            for (auto& m : midi)
                if (m.offset >= done && m.offset < done + n)
                    ev.push_back ({ m.offset - done, m.on, m.note, m.velocity });
            Transport t { playing, ppq, bpm };
            e.process (l.data(), r.data(), n, t, ev.data(), (int) ev.size());
            L.insert (L.end(), l.begin(), l.begin() + n);
            R.insert (R.end(), r.begin(), r.begin() + n);
            if (playing) ppq += n / (60.0 / bpm * SR);
            done += n;
        }
    }

    double energy (size_t from, size_t to) const
    {
        double s = 0.0;
        for (size_t i = from; i < std::min (to, L.size()); ++i) s += (double) L[i] * L[i] + (double) R[i] * R[i];
        return s;
    }
};

int main()
{
    const Sample sine = makeSine (8.0);
    const Sample ramp = makeRamp (4.0);

    // 1. Sans sample : silence, pas de plantage
    {
        Rig r;
        r.run (48000, true);
        CHECK (r.energy (0, r.L.size()) == 0.0, "energie %g", r.energy (0, r.L.size()));
    }

    // 2. Pas calés sur le transport : 1/16 à 120 BPM = 6000 échantillons
    {
        Rig r;
        r.e.setSample (0, &sine);
        for (int p = 0; p < maxPoints; ++p) { PointParams pp; pp.pos = p / 16.0f; r.e.setPoint (p, pp); }
        r.run (96000, true, 512);
        CHECK (r.log.size() == 16, "%d declenchements", (int) r.log.size());
        bool timesOk = true, pointsOk = true;
        for (size_t k = 0; k < r.log.size(); ++k)
        {
            if (r.log[k].time != (std::int64_t) (k * 6000)) timesOk = false;
            if (r.log[k].point != (int) (k % 8)) pointsOk = false;
        }
        CHECK (timesOk, "instants");
        CHECK (pointsOk, "points");
        CHECK (r.energy (0, r.L.size()) > 100.0, "son");
        // blocs de taille irrégulière : mêmes instants
        Rig r2;
        r2.e.setSample (0, &sine);
        for (int p = 0; p < maxPoints; ++p) { PointParams pp; pp.pos = p / 16.0f; r2.e.setPoint (p, pp); }
        r2.run (96000, true, 97);
        bool same = r2.log.size() == r.log.size();
        for (size_t k = 0; same && k < r.log.size(); ++k) same = r2.log[k].time == r.log[k].time;
        CHECK (same, "blocs 97");
    }

    // 3. Swing 50 % : le pas impair est retardé de 25 % (7500)
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.swing = 0.5f; r.e.setSettings (s);
        r.run (24000, true);
        CHECK (r.log.size() == 4 && r.log[1].time == 7500 && r.log[2].time == 12000 && r.log[3].time == 19500,
               "swing %lld %lld", r.log.size() > 1 ? (long long) r.log[1].time : -1LL, r.log.size() > 3 ? (long long) r.log[3].time : -1LL);
    }

    // 4. RANDOM : reproductible, LOCK rejoue pareil, SEED change tout
    {
        auto render = [&] (std::uint32_t seed, bool lock, std::vector<int>& pts)
        {
            Rig r;
            r.e.setSample (0, &sine);
            Settings s; s.mode = modeRandom; s.seed = seed; s.lock = lock; s.numPoints = 16; s.chance = 0.8f; s.reverseChance = 0.3f; s.scatterMs = 20.0f;
            r.e.setSettings (s);
            for (int p = 0; p < maxPoints; ++p) { PointParams pp; pp.pos = p / 16.0f; r.e.setPoint (p, pp); }
            r.run (192000, true);
            for (auto& t : r.log) pts.push_back (t.point * 1000 + t.step);
            return r.L;
        };
        std::vector<int> a, b, c, d;
        const auto la = render (7, true, a);
        const auto lb = render (7, true, b);
        render (8, true, c);
        render (7, false, d);
        CHECK (la == lb, "rendu identique");
        CHECK (a != c, "seed");
        // LOCK : mesure 1 == mesure 2 (pas 0..15 puis 16..31)
        std::vector<int> bar1, bar2;
        for (auto v : a) (bar1.size() < a.size() / 2 ? bar1 : bar2).push_back (v);
        CHECK (bar1 == bar2 && ! bar1.empty(), "lock %d %d", (int) bar1.size(), (int) bar2.size());
        std::vector<int> d1, d2;
        for (auto v : d) (d1.size() < d.size() / 2 ? d1 : d2).push_back (v);
        CHECK (d1 != d2, "sans lock ca change");
        int distinct = 0; bool seen[16] {};
        for (auto v : a) { const int p = v / 1000; if (p >= 0 && ! seen[p]) { seen[p] = true; ++distinct; } }
        CHECK (distinct >= 6, "distincts %d", distinct);
        CHECK (a.size() < 32, "chance 80 %% : %d declenchements", (int) a.size());
    }

    // 5. Saut de transport (boucle) : redéclenche tout de suite
    {
        Rig r;
        r.e.setSample (0, &sine);
        r.run (3000, true);
        r.ppq = 8.0 + 0.1;   // saut
        r.run (512, true);
        CHECK (r.log.size() == 2 && r.log[1].time == 3000, "saut %d", (int) r.log.size());
    }

    // 6. TAIL : CUT coupe la reverb au changement de point, KEEP la laisse
    for (int keep = 0; keep < 2; ++keep)
    {
        Sample click;
        click.sampleRate = SR;
        for (auto& ch : click.ch) { ch.assign (48000, 0.0f); for (int i = 0; i < 2400; ++i) ch[(size_t) i] = (i % 40 < 20 ? 0.8f : -0.8f); }
        Rig r;
        r.e.setSample (0, &click);
        PointParams p1; p1.pos = 0.0f; p1.rev = 1.0f; p1.tailKeep = keep == 1;
        PointParams p2; p2.pos = 0.5f;
        r.e.setPoint (0, p1);
        r.e.setPoint (1, p2);
        Settings s; s.steps = 4; s.revSize = 0.9f; s.revLevel = 1.0f; r.e.setSettings (s);
        r.e.setStep (0, 1); r.e.setStep (1, 2); r.e.setStep (2, 2); r.e.setStep (3, 2);
        r.run (24000, true);
        const double tail = r.energy (6000 + 960, 12000);   // 20 ms après le changement
        if (keep) CHECK (tail > 1.0, "KEEP queue %g", tail);
        else      CHECK (tail < 1.0e-6, "CUT queue %g", tail);
    }

    // 7. Note MIDI de point, transport à l'arrêt
    {
        Rig r;
        r.e.setSample (0, &sine);
        PointParams pp; pp.pos = 0.25f; r.e.setPoint (3, pp);
        r.run (24000, false, 512, 120.0, { { 1000, true, firstPointNote + 3, 1.0f }, { 12000, false, firstPointNote + 3, 0.0f } });
        CHECK (r.energy (0, 1000) == 0.0, "avant");
        CHECK (r.energy (2000, 11000) > 100.0, "pendant");
        CHECK (r.energy (13000, 24000) == 0.0, "apres %g", r.energy (13000, 24000));
        CHECK (r.log.size() == 1 && r.log[0].point == 3 && r.log[0].time == 1000, "log");
    }

    // 8. Mode NOTE : la séquence tourne seulement pendant la note
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.trigger = triggerNote; r.e.setSettings (s);
        r.run (48000, false, 512, 120.0, { { 4000, true, 60, 1.0f }, { 4000 + 24000, false, 60, 0.0f } });
        CHECK (r.log.size() == 4 && r.log[0].time == 4000 && r.log[1].time == 10000, "note %d", (int) r.log.size());
        CHECK (r.energy (4000 + 24000 + 1000, 48000) == 0.0, "arret");
        // transport qui joue sans note : rien
        Rig q;
        q.e.setSample (0, &sine);
        q.e.setSettings (s);
        q.run (24000, true);
        CHECK (q.log.empty(), "pas de transport en mode NOTE");
    }

    // 9. Stutter 1/16 : le son se répète toutes les 6000 échantillons
    {
        Rig r;
        r.e.setSample (0, &sine);
        PointParams pp; pp.pos = 0.1f; pp.loop = true; pp.loopRate = 4;
        for (int p = 0; p < maxPoints; ++p) r.e.setPoint (p, pp);
        Settings s; s.rate = 1; r.e.setSettings (s);   // pas à la noire : 4 répétitions par pas
        r.run (24000, true);
        double diff = 0.0, ref = 0.0;
        for (int i = 1000; i < 5000; ++i) { diff += std::abs (r.L[(size_t) i] - r.L[(size_t) i + 6000]); ref += std::abs (r.L[(size_t) i]); }
        CHECK (diff < ref * 1.0e-3 && ref > 10.0, "stutter diff %g ref %g", diff, ref);
    }

    // 10. Sens : arrière = rampe qui descend ; ping-pong reste dans sa fenêtre
    {
        Rig r;
        r.e.setSample (0, &ramp);
        PointParams pp; pp.pos = 0.5f; pp.dir = reverse;
        for (int p = 0; p < maxPoints; ++p) r.e.setPoint (p, pp);
        r.run (5000, true);
        CHECK (r.L[4000] < r.L[1000] && r.L[1000] > 0.3f, "arriere %g %g", r.L[1000], r.L[4000]);

        Rig q;
        q.e.setSample (0, &ramp);
        pp.dir = pingPong; pp.loop = true; pp.loopRate = 6;   // fenêtre d'une triple croche = 3000 échantillons
        for (int p = 0; p < maxPoints; ++p) q.e.setPoint (p, pp);
        q.run (6000, true);
        float lo = 1.0f, hi = 0.0f;
        for (int i = 500; i < 6000; ++i) { lo = std::min (lo, q.L[(size_t) i]); hi = std::max (hi, q.L[(size_t) i]); }
        const float expectedHi = 0.5f + 3000.0f / (float) ramp.length();
        CHECK (lo > 0.49f && hi < expectedHi + 0.01f && hi > expectedHi - 0.01f, "ping-pong %g %g", lo, hi);
    }

    // 11. Temps libre : 100 ms = 4800 échantillons, DRIFT varie mais reste reproductible
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.sync = false; s.freeMs = 100.0f; r.e.setSettings (s);
        r.run (48000, true);
        CHECK (r.log.size() == 10 && r.log[3].time == 14400, "libre %d", (int) r.log.size());
        Rig q;
        q.e.setSample (0, &sine);
        s.drift = 1.0f; q.e.setSettings (s);
        q.run (96000, true);
        bool varied = false;
        for (size_t k = 2; k < q.log.size(); ++k) varied |= (q.log[k].time - q.log[k - 1].time) != (q.log[1].time - q.log[0].time);
        CHECK (varied, "drift");
    }

    // 12. RATE changé pendant la lecture : pas de rafale de déclenchements
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.rate = 9; r.e.setSettings (s);   // 1/64
        r.run (30000, true);
        const auto before = r.log.size();
        s.rate = 0; r.e.setSettings (s);   // 1/2
        r.run (512, true);
        CHECK (r.log.size() - before <= 1, "rafale %d", (int) (r.log.size() - before));
    }

    // 13. GATE 50 % : silence dans la deuxième moitié du pas ; REST : silence
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.gate = 0.5f; s.attackMs = 1.0f; s.releaseMs = 1.0f; r.e.setSettings (s);
        r.run (12000, true);
        CHECK (r.energy (3200, 5900) == 0.0 && r.energy (500, 2900) > 10.0, "gate");

        Rig q;
        q.e.setSample (0, &sine);
        q.e.setStep (1, stepRest);
        q.run (12000, true);
        CHECK (q.energy (6500, 12000) == 0.0 && q.energy (500, 5900) > 10.0, "rest");
    }

    // 14. Pitch +12 : le cycle dure moitié moins ; sample à une autre fréquence d'échantillonnage
    {
        Sample s44 = makeSine (2.0, 441.0);
        s44.sampleRate = 44100.0;
        // reconstruit à 44,1 kHz : 441 Hz = 100 échantillons
        for (int i = 0; i < s44.length(); ++i) s44.ch[0][(size_t) i] = s44.ch[1][(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * i / 100.0);
        Rig r;
        r.e.setSample (0, &s44);
        PointParams pp; pp.pitch = 12.0f;
        for (int p = 0; p < maxPoints; ++p) r.e.setPoint (p, pp);
        r.run (6000, true);
        int crossings = 0;
        for (int i = 1001; i < 5801; ++i) crossings += (r.L[(size_t) i - 1] < 0.0f && r.L[(size_t) i] >= 0.0f) ? 1 : 0;
        // 882 Hz à 48 kHz sur 4800 échantillons = 88,2 cycles
        CHECK (std::abs (crossings - 88) <= 1, "pitch %d", crossings);
    }

    // 15. Tout à fond : pas de NaN, sortie bornée
    {
        Rig r;
        r.e.setSample (0, &sine);
        r.e.setSample (1, &ramp);
        Settings s; s.mode = modeRandom; s.numPoints = 16; s.revSize = 1.0f; s.dlyFeedback = 0.95f; s.revLevel = 1.0f; s.dlyLevel = 1.0f; s.outGain = 4.0f; s.scatterMs = 500.0f; s.reverseChance = 0.5f;
        r.e.setSettings (s);
        for (int p = 0; p < maxPoints; ++p)
        {
            PointParams pp; pp.slot = p % 3; pp.pos = p / 15.0f; pp.dir = p % 3; pp.pitch = (float) (p * 6 - 48); pp.gain = 4.0f; pp.filter = (p % 5 - 2) / 2.0f;
            pp.loop = p % 3 == 0; pp.loopRate = p % 10; pp.rev = 1.0f; pp.dly = 1.0f; pp.tailKeep = p % 2 == 0;
            r.e.setPoint (p, pp);
        }
        r.run (480000, true, 333);
        bool finite = true; float peak = 0.0f;
        for (auto v : r.L) { finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        CHECK (finite && peak <= 1.0f && peak > 0.1f, "peak %g", peak);
    }

    // 16. Mode NOTE avec Ableton qui joue : calé sur la grille
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.trigger = triggerNote; r.e.setSettings (s);
        r.run (36000, true, 512, 120.0, { { 7000, true, 60, 1.0f }, { 30000, false, 60, 0.0f } });
        CHECK (r.log.size() == 4 && r.log[0].time == 7000 && r.log[0].step == 1 && r.log[1].time == 12000 && r.log[3].time == 24000,
               "note grille %d %lld", (int) r.log.size(), r.log.size() > 1 ? (long long) r.log[1].time : -1LL);
        CHECK (r.energy (31000, 36000) == 0.0, "note relachee");
        Rig q;
        q.e.setSample (0, &sine);
        q.e.setSettings (s);
        q.run (13000, true, 512, 120.0, { { 12000, true, 60, 1.0f } });
        CHECK (q.log.size() == 1 && q.log[0].time == 12000 && q.log[0].step == 2, "note pile sur un pas : une fois (%d)", (int) q.log.size());
    }

    // 17. REFRESH des positions : fixe dans un cycle, change au suivant ; LINK
    {
        Engine e;
        e.prepare (SR, 512);
        e.setSample (0, &sine);
        Settings s; s.lanes[lanePos].on = true; s.link = false; s.lanes[lanePos].rate = 2;   // 4 temps
        e.setSettings (s);
        const auto a = e.effective (3, 0.0), b = e.effective (3, 3.99), c = e.effective (3, 4.0), d = e.effective (3, 8.0);
        CHECK (a.pos == b.pos && a.pos != c.pos && c.pos != d.pos, "refresh pos %g %g %g", a.pos, c.pos, d.pos);
        CHECK (e.effective (3, 4.0).pos != e.effective (4, 4.0).pos, "points differents");
        s.link = true; s.refreshRate = 0; e.setSettings (s);   // tout suit 1 temps
        CHECK (e.effective (3, 0.0).pos != e.effective (3, 1.0).pos, "link");
        s.lanes[lanePos].amount = 0.1f; s.refreshRate = 2; e.setSettings (s);
        PointParams pp; pp.pos = 0.5f; e.setPoint (3, pp);
        bool near = true;
        for (int c2 = 0; c2 < 50; ++c2) near &= std::abs (e.effective (3, c2 * 4.0).pos - 0.5f) <= 0.0501f;
        CHECK (near, "amount 10 %% autour de la position");
    }

    // 18. REFRESH des loops / reverb / delay
    {
        Engine e;
        e.prepare (SR, 512);
        e.setSample (0, &sine);
        Settings s; s.lanes[laneLoop].on = true; s.lanes[laneLoop].amount = 0.0f;
        s.lanes[laneRev].on = true; s.lanes[laneRev].amount = 1.0f;
        s.lanes[laneDly].on = true; s.lanes[laneDly].amount = 0.5f;
        e.setSettings (s);
        bool noLoop = true, allRev = true; int dlyOn = 0;
        for (int c = 0; c < 64; ++c)
            for (int p = 0; p < 16; ++p)
            {
                const auto x = e.effective (p, c * 4.0);
                noLoop &= ! x.loop;
                allRev &= std::abs (x.rev - 0.5f) < 1e-6f;
                dlyOn += x.dly > 0.0f ? 1 : 0;
            }
        CHECK (noLoop && allRev, "loop 0 %%, reverb 100 %%");
        CHECK (dlyOn > 400 && dlyOn < 620, "delay 50 %% : %d / 1024", dlyOn);
        s.lanes[laneLoop].amount = 1.0f; s.loopRandomRate = true; e.setSettings (s);
        bool allLoop = true, rateVaries = false;
        for (int p = 0; p < 16; ++p) { const auto x = e.effective (p, 0.0); allLoop &= x.loop; rateVaries |= x.loopRate != e.effective (0, 0.0).loopRate; }
        CHECK (allLoop && rateVaries, "loops 100 %% et vitesse tiree");

        // le loop tiré s'entend : le son se répète
        Rig r;
        r.e.setSample (0, &sine);
        s = Settings(); s.rate = 1; s.lanes[laneLoop].on = true; s.lanes[laneLoop].amount = 1.0f;
        r.e.setSettings (s);
        for (int p = 0; p < maxPoints; ++p) { PointParams pp; pp.pos = 0.1f; pp.loopRate = 4; r.e.setPoint (p, pp); }
        r.run (24000, true);
        double diff = 0.0, ref = 0.0;
        for (int i = 1000; i < 5000; ++i) { diff += std::abs (r.L[(size_t) i] - r.L[(size_t) i + 6000]); ref += std::abs (r.L[(size_t) i]); }
        CHECK (diff < ref * 1.0e-3 && ref > 10.0, "loop tire");
    }

    // 19. Ordres de passage
    {
        auto order = [&] (int mode, int n, int hold, int steps, std::uint32_t seed = 1)
        {
            Rig r;
            r.e.setSample (0, &sine);
            Settings s; s.mode = mode; s.numPoints = n; s.hold = hold; s.steps = 64; s.lock = false; s.seed = seed;
            r.e.setSettings (s);
            r.run (steps * 6000, true);
            std::vector<int> pts;
            for (auto& t : r.log) pts.push_back (t.point);
            return pts;
        };
        CHECK ((order (modeUpDown, 4, 1, 8) == std::vector<int> { 0, 1, 2, 3, 2, 1, 0, 1 }), "up-down");
        CHECK ((order (modeDown, 4, 1, 5) == std::vector<int> { 3, 2, 1, 0, 3 }), "down");
        CHECK ((order (modeUp, 4, 2, 8) == std::vector<int> { 0, 1, 2, 3 }), "hold 2");
        const auto sh = order (modeShuffle, 8, 1, 24);
        bool eachOnce = sh.size() == 24;
        for (int c = 0; c < 3 && eachOnce; ++c)
        {
            bool seen[8] {};
            for (int i = 0; i < 8; ++i) { if (seen[sh[(size_t) (c * 8 + i)]]) eachOnce = false; seen[sh[(size_t) (c * 8 + i)]] = true; }
        }
        CHECK (eachOnce, "shuffle : chaque point une fois par tour");
        CHECK (! std::equal (sh.begin(), sh.begin() + 8, sh.begin() + 8), "shuffle change d'un tour a l'autre");
        const auto dr = order (modeDrunk, 8, 1, 40);
        bool steps1 = dr.size() == 40, moved = false;
        for (size_t i = 1; i < dr.size(); ++i)
        {
            const int d = (dr[i] - dr[i - 1] + 8) % 8;
            steps1 &= d == 0 || d == 1 || d == 7;
            moved |= d != 0;
        }
        CHECK (steps1 && moved, "drunk");
    }

    // 20. ATTACK : montée progressive
    {
        Rig r;
        r.e.setSample (0, &sine);
        Settings s; s.attackMs = 20.0f; r.e.setSettings (s);
        r.run (3000, true);
        float early = 0.0f, late = 0.0f;
        for (int i = 0; i < 100; ++i) early = std::max (early, std::abs (r.L[(size_t) i]));
        for (int i = 1000; i < 1300; ++i) late = std::max (late, std::abs (r.L[(size_t) i]));
        CHECK (early < 0.15f * late && late > 0.3f, "attack %g %g", early, late);
    }

    std::printf ("%d verifications, %d echecs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
