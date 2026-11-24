// Tests du plugin complet (processeur JUCE + interface), sans DAW.
// Usage : PhreakPluginTests [dossier où écrire les captures de l'interface]

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

static int failures = 0, checks = 0;

static void check (bool condition, const juce::String& message)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::cout << "  ECHEC : " << message << std::endl;
    }
}

using P = PhreakAudioProcessor;

struct FakeHead final : juce::AudioPlayHead
{
    bool playing = true;
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying (playing);
        i.setBpm (bpm);
        i.setPpqPosition (ppq);
        return i;
    }
};

static std::unique_ptr<P> makePlugin (double fs = 48000.0, int block = 512)
{
    std::unique_ptr<P> p (dynamic_cast<P*> (createPluginFilter()));
    p->setRateAndBufferSizeDetails (fs, block);
    p->prepareToPlay (fs, block);
    return p;
}

/** Rend `samples` échantillons ; avance la tête de lecture si elle joue. */
static juce::AudioBuffer<float> render (P& p, FakeHead& head, int samples, int block = 512, juce::MidiBuffer midiAtStart = {})
{
    juce::AudioBuffer<float> out (2, samples), io (2, block);
    for (int pos = 0; pos < samples; pos += block)
    {
        const int n = juce::jmin (block, samples - pos);
        io.setSize (2, n, false, false, true);
        io.clear();
        juce::MidiBuffer midi;
        if (pos == 0) midi = midiAtStart;
        p.processBlock (io, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
        if (head.playing) head.ppq += n / (60.0 / head.bpm * 48000.0);
    }
    return out;
}

/** Fichier de test : 4 s, sinus doux + 8 coups nets (à 0,25 s puis toutes les 0,5 s). */
static juce::File writeTestWav (const juce::File& dir, const juce::String& name, double fs = 44100.0)
{
    const int n = (int) (4.0 * fs);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    for (int i = 0; i < n; ++i)
    {
        float v = 0.02f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * i / fs);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    for (int h = 0; h < 8; ++h)
    {
        const int start = (int) ((0.25 + 0.5 * h) * fs);
        for (int i = 0; i < (int) (0.05 * fs); ++i)
        {
            const float v = 0.8f * (float) std::exp (-i / (0.008 * fs)) * (float) std::sin (i * 0.3);
            b.addSample (0, start + i, v);
            b.addSample (1, start + i, v);
        }
    }
    auto file = dir.getChildFile (name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, 2, 24, {}, 0));
    if (w != nullptr)
    {
        os.release();
        w->writeFromAudioSampleBuffer (b, 0, n);
    }
    return file;
}

static void testParametersAndAudio (const juce::File& dir)
{
    std::cout << "Parametres, samples, son" << std::endl;
    auto p = makePlugin();
    check (p->getParameters().size() == 46 + 16 * 12 + 64, "nombre de parametres : " + juce::String (p->getParameters().size()));
    check (p->acceptsMidi() && p->getTotalNumInputChannels() == 0 && p->getTotalNumOutputChannels() == 2, "instrument stereo");

    FakeHead head;
    p->setPlayHead (&head);

    // sans sample : silence
    auto out = render (*p, head, 24000);
    check (out.getMagnitude (0, 0, 24000) == 0.0f, "silence sans sample");

    const auto wavFile = writeTestWav (dir, "phreak-test.wav");
    juce::String error;
    check (p->loadSample (0, wavFile, &error), "chargement : " + error);
    const auto& s = p->getSample (0);
    check (s.isLoaded() && std::abs (s.seconds() - 4.0) < 0.01, "duree " + juce::String (s.seconds()));
    check ((int) s.data->onsets.size() == 8, "transitoires : " + juce::String ((int) s.data->onsets.size()));
    bool onsetsOk = true;
    for (size_t h = 0; h < s.data->onsets.size(); ++h)
        onsetsOk &= std::abs (s.data->onsets[h] - (0.25 + 0.5 * h) * 44100.0) < 300.0;
    check (onsetsOk, "position des transitoires");

    check (juce::roundToInt (p->getParam (P::triggerId)) == 1, "NOTE par defaut");
    head.ppq = 0.0;
    out = render (*p, head, 48000);
    check (out.getMagnitude (0, 0, 48000) < 1.0e-6f, "mode NOTE sans note : silence");

    // une note longue avec Ableton qui joue, tous les rafraichissements : sequence sans fin
    {
        p->setParam (P::modeId, 5.0f);   // SHUFFLE
        for (int l = 0; l < 4; ++l) p->setParam (P::laneId (l, "On"), 1.0f);
        p->setParam (P::refreshRateId, 1.0f);   // 2 temps
        juce::MidiBuffer longNote;
        longNote.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        head.ppq = 0.0;
        out = render (*p, head, 48000 * 6, 512, longNote);
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i));
        check (finite && out.getMagnitude (0, 0, 48000 * 6) > 0.01f && out.getMagnitude (0, 48000 * 5, 48000) > 0.01f, "note longue");
        // les positions jouées ont changé d'un rafraîchissement à l'autre
        check (p->getEngine().effPos[0].load() >= 0.0f, "positions rafraichies affichees");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        render (*p, head, 512, 512, off);
        for (int l = 0; l < 4; ++l) p->setParam (P::laneId (l, "On"), 0.0f);
        p->setParam (P::modeId, 0.0f);
        p->reset();   // vide les queues de reverb / delay
    }

    p->setParam (P::triggerId, 0.0f);   // TRANSPORT
    head.ppq = 0.0;
    out = render (*p, head, 48000);
    check (out.getMagnitude (0, 0, 48000) > 0.01f, "son avec le transport");

    // transport à l'arrêt : silence (après la fin du fondu)
    head.playing = false;
    render (*p, head, 4800);
    out = render (*p, head, 24000);
    check (out.getMagnitude (0, 0, 24000) < 1.0e-6f, "silence a l'arret");

    // note MIDI C1 = point 1
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
    out = render (*p, head, 24000, 512, midi);
    check (out.getMagnitude (0, 0, 24000) > 0.01f, "note C1");
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    render (*p, head, 4800, 512, off);
    out = render (*p, head, 12000);
    check (out.getMagnitude (0, 0, 12000) < 1.0e-6f, "relache");

    // tout le hasard, réglages extrêmes : pas de NaN
    juce::Random rng (3);
    p->setParam (P::pointsId, 16.0f);
    p->randomizePoints (P::rndPositions | P::rndDirections | P::rndLoops | P::rndPitch | P::rndFx | P::rndTails, rng);
    p->randomizePattern (rng);
    p->setParam (P::modeId, 2.0f);
    p->setParam (P::scatterId, 300.0f);
    p->setParam (P::reverseId, 50.0f);
    p->setParam (P::gainId, 12.0f);
    head.playing = true;
    head.ppq = 0.0;
    out = render (*p, head, 96000, 333);
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i)
            finite &= std::isfinite (out.getSample (c, i));
    check (finite && out.getMagnitude (0, 96000) <= 1.0f, "pas de NaN, borne");

    // SNAP sur les transitoires
    p->setParam (P::snapId, 1.0f);
    p->randomizePoints (P::rndPositions, rng);
    bool snapped = true;
    for (int pt = 0; pt < 16; ++pt)
    {
        const float pos = p->getParam (P::pointId (pt, "Pos")) * 0.01f;
        const int at = juce::roundToInt (pos * (s.data->length() - 1));
        bool onOnset = false;
        for (int o : s.data->onsets) onOnset |= std::abs (o - at) <= 2;
        snapped &= onOnset;
    }
    check (snapped, "snap transitoires");

    p->pointsOnTransients();
    check (std::abs (p->getParam (P::pointId (1, "Pos")) * 0.01f * (s.data->length() - 1) - s.data->onsets[1]) < 3.0f, "points sur transitoires");

    p->spreadPointsEvenly();
    p->setParam (P::snapId, 0.0f);
    p->spreadPointsEvenly();
    check (std::abs (p->getParam (P::pointId (4, "Pos")) - 25.0f) < 0.01f, "spread");

    // Etat : sample rechargé, réglages gardés
    p->setParam (P::stepId (5), 17.0f);
    p->setParam (P::pointId (3, "Pitch"), 7.0f);
    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makePlugin();
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (q->getSample (0).isLoaded() && q->getSample (0).name == "phreak-test", "sample recharge");
    check (juce::roundToInt (q->getParam (P::stepId (5))) == 17 && juce::roundToInt (q->getParam (P::pointId (3, "Pitch"))) == 7, "reglages recharges");

    // fichier disparu : signalé, sans planter
    wavFile.deleteFile();
    auto r = makePlugin();
    r->setStateInformation (state.getData(), (int) state.getSize());
    check (! r->getSample (0).isLoaded() && r->getSample (0).missing, "fichier manquant");
    juce::MemoryBlock again;
    r->getStateInformation (again);
    auto again2 = makePlugin();
    again2->setStateInformation (again.getData(), (int) again.getSize());
    check (again2->getSample (0).missing && again2->getSample (0).name == "phreak-test", "chemin garde meme manquant");
}

static void testEditor (const juce::File& snapshotDir)
{
    std::cout << "Interface" << std::endl;
    auto p = makePlugin();
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto wavFile = writeTestWav (dir, "phreak-ui.wav");
    p->loadSample (0, wavFile);

    // un deuxième sample plus long, synthétique
    auto longer = std::make_shared<phreak::Sample>();
    longer->sampleRate = 48000.0;
    for (auto& c : longer->ch) c.resize (48000 * 30);
    for (int i = 0; i < 48000 * 30; ++i)
    {
        const float env = (float) std::exp (-(i % 12000) / 3000.0);
        const float v = 0.6f * env * (float) std::sin (i * 0.02 + std::sin (i * 0.0003) * 8.0);
        longer->ch[0][(size_t) i] = v;
        longer->ch[1][(size_t) i] = v * 0.8f;
    }
    p->setSampleData (1, longer, "Pad dark 30s");

    p->setParam (P::pointId (2, "Rev"), 60.0f);
    p->setParam (P::pointId (2, "Loop"), 1.0f);
    p->setParam (P::pointId (2, "LoopRate"), 4.0f);
    p->setParam (P::pointId (2, "Tail"), 1.0f);
    p->setParam (P::pointId (2, "Pitch"), -5.0f);
    p->setParam (P::pointId (9, "Sample"), 1.0f);
    p->setParam (P::stepId (3), 0.0f);
    p->setParam (P::stepId (7), 17.0f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* editor = dynamic_cast<PhreakAudioProcessorEditor*> (ed.get());
    check (editor != nullptr, "editeur");
    if (editor == nullptr) return;
    auto& facade = editor->getFacade();
    check (editor->getWidth() == PhreakFacade::width && editor->getHeight() == PhreakFacade::height, "taille");

    facade.selectPoint (2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    check (facade.getWave().getSlot() == 0, "sample du point 3");

    // le bouton de point relié au bon paramètre
    auto* posKnob = dynamic_cast<juce::Slider*> (facade.findControl ("pointPos"));
    check (posKnob != nullptr && std::abs (posKnob->getValue() - p->getParam (P::pointId (2, "Pos"))) < 0.01, "POSITION liee au point 3");

    // clic sur un pas : il prend le point choisi
    auto& grid = facade.getGrid();
    const auto cell = grid.cellBounds (11).getCentre();
    juce::MouseEvent* none = nullptr; juce::ignoreUnused (none);
    {
        const auto& src = juce::Desktop::getInstance().getMainMouseSource();
        juce::MouseEvent e (src, cell, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                            &grid, &grid, juce::Time::getCurrentTime(), cell, juce::Time::getCurrentTime(), 1, false);
        grid.mouseDown (e);
        grid.mouseUp (e);
    }
    check (juce::roundToInt (p->getParam (P::stepId (11))) == 3, "pas 12 = point 3 : " + juce::String (p->getParam (P::stepId (11))));

    // glisser un point sur la forme d'onde
    auto& wave = facade.getWave();
    {
        const auto& src = juce::Desktop::getInstance().getMainMouseSource();
        const float x0 = wave.xForPos (p->getParam (P::pointId (2, "Pos")) * 0.01f);
        const juce::Point<float> a (x0, 100.0f), b (x0 + 100.0f, 100.0f);
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, a, {}, 1.0f, 0, 0, 0, 0, &wave, &wave, now, a, now, 1, false);
        juce::MouseEvent drag (src, b, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &wave, &wave, now, a, now, 1, false);
        const float before = p->getParam (P::pointId (2, "Pos"));
        wave.mouseDown (down);
        wave.mouseDrag (drag);
        wave.mouseUp (drag);
        const float expected = before + 100.0f * 100.0f / ((float) wave.getWidth() - 2.0f);
        check (std::abs (p->getParam (P::pointId (2, "Pos")) - expected) < 0.2f, "glisser un point : " + juce::String (p->getParam (P::pointId (2, "Pos"))) + " / " + juce::String (expected));
    }

    // jouer un peu pour la tête de lecture
    p->setParam (P::triggerId, 0.0f);
    FakeHead head;
    p->setPlayHead (&head);
    render (*p, head, 30000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

    auto snapshot = [&] (const juce::String& name)
    {
        if (snapshotDir == juce::File()) return;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = snapshotDir.getChildFile (name + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream())
            png.writeImageToStream (img, *os);
    };

    snapshot ("phreak");
    facade.selectPoint (9);
    wave.setView (0.2, 0.1);
    snapshot ("phreak-slot2-zoom");
    p->setParam (P::modeId, 5.0f);
    p->setParam (P::laneId (0, "On"), 1.0f);
    p->setParam (P::laneId (1, "On"), 1.0f);
    p->setParam (P::laneId (2, "On"), 1.0f);
    p->setParam (P::laneId (1, "Amount"), 60.0f);
    facade.selectPoint (2);
    render (*p, head, 30000);
    snapshot ("phreak-refresh");
    editor->setUiScale (0.75f);
    check (std::abs (editor->getUiScale() - 0.75f) < 0.01f, "echelle");
    snapshot ("phreak-75");

    p->editorBeingDeleted (editor);
    ed.reset();
    wavFile.deleteFile();
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    tmp.createDirectory();

    testParametersAndAudio (tmp);
    testEditor (argc > 1 ? juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])) : juce::File());

    std::cout << checks << " verifications, " << failures << " echecs" << std::endl;
    return failures == 0 ? 0 : 1;
}
