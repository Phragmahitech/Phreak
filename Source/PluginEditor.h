#pragma once

#include "PluginProcessor.h"
#include "PresetBar.h"

/**
    Look de Phreak : celui de la série Phragma (skin Serum 2 « Phragma electricity ») —
    panneaux bleu nuit, texte clair, accents bleu clair (#3CD2F8), mauve (#9850E8), lilas (#C898F8).
    Police Chakra Petch embarquée.
*/
class PhreakLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PhreakLookAndFeel();

    static juce::Colour ground()      { return juce::Colour (0xff0e181c); }
    static juce::Colour panelTop()    { return juce::Colour (0xff1e2f36); }
    static juce::Colour panelBottom() { return juce::Colour (0xff17252b); }
    static juce::Colour edge()        { return juce::Colour (0xff2b4250); }
    static juce::Colour screen()      { return juce::Colour (0xff0b1418); }
    static juce::Colour text()        { return juce::Colour (0xffdde8ee); }
    static juce::Colour dim()         { return juce::Colour (0xff8aa0ac); }
    static juce::Colour accent()      { return juce::Colour (0xff3cd2f8); }
    static juce::Colour mauve()       { return juce::Colour (0xff9850e8); }
    static juce::Colour lilac()       { return juce::Colour (0xffc898f8); }
    static juce::Colour steel()       { return juce::Colour (0xff5a8cf0); }

    /** Couleur d'un point (0..15) : du bleu clair au lilas en passant par le mauve. */
    static juce::Colour pointColour (int point);

    static juce::Path chamfered (juce::Rectangle<float> r, float cut);
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title, const juce::Font& font);

    juce::Font font (float height, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    juce::Font getPopupMenuFont() override                  { return font (12.0f, true, 0.06f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& w, int& h) override;

private:
    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
/**
    Forme d'onde du sample affiché, avec les points posés dessus.

      glisser un point            le déplacer (Maj : fin ; SNAP s'applique)
      clic sur un point           le choisir
      double-clic                 pose le point choisi ici (et sur ce sample)
      molette                     zoom autour de la souris (Maj ou molette horizontale : défiler)
      glisser le fond             défiler quand on a zoomé
      déposer des fichiers        charge le sample (plusieurs fichiers : emplacements suivants)
*/
class WaveView final : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    WaveView (PhreakAudioProcessor&, PhreakLookAndFeel&);
    ~WaveView() override;

    void setSlot (int slot);
    int getSlot() const noexcept                        { return slot; }
    void setSelectedPoint (int p);

    std::function<void (int)> onPointSelected;
    std::function<void (int)> onSlotChanged;   // après un dépôt de fichier

    void paint (juce::Graphics&) override;
    void resized() override                             { cacheValid = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dropping = false; repaint(); }

    // Pour les tests
    float xForPos (float pos) const;
    float posForX (float x) const;
    void setView (double start, double length)          { viewStart = start; viewLength = length; clampView(); cacheValid = false; repaint(); }

private:
    void timerCallback() override;
    void clampView();
    void rebuildCache();
    int hitPoint (float x) const;
    bool pointOnThisSlot (int p) const;
    juce::Rectangle<float> plot() const                 { return getLocalBounds().toFloat().reduced (1.0f); }

    PhreakAudioProcessor& processor;
    PhreakLookAndFeel& lf;
    int slot = 0, selected = 0, dragging = -1, hover = -1;
    double viewStart = 0.0, viewLength = 1.0;   // fraction du sample
    bool panning = false, dropping = false, cacheValid = false;
    double panStartView = 0.0;
    float dragStartPos = 0.0f, dragStartX = 0.0f;
    int lastGeneration = -1;
    juce::Image cache;
    float playPos = -1.0f;
    int playPoint = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveView)
};

//==============================================================================
/**
    Grille des 64 pas (deux rangées de 32) en mode GRID.

      clic            le pas joue le point choisi (déjà ce point : REST)
      Alt + clic      TIE (le point précédent continue)
      Maj + clic      REST (silence)
      glisser         change la valeur (haut / bas)
      clic droit      menu (TIE, REST, 1 à 16)
*/
class StepGrid final : public juce::Component,
                       private juce::Timer
{
public:
    StepGrid (PhreakAudioProcessor&, PhreakLookAndFeel&);
    ~StepGrid() override;

    std::function<int()> selectedPoint;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<float> cellBounds (int step) const;
    int cellAt (juce::Point<float>) const;

private:
    void timerCallback() override;
    int value (int step) const;
    void setValue (int step, int v);

    PhreakAudioProcessor& processor;
    PhreakLookAndFeel& lf;
    int dragStep = -1, dragStartValue = 0, shownStep = -1;
    float dragStartY = 0.0f;
    bool dragged = false;
    juce::RangedAudioParameter* gestureParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

//==============================================================================
class PhreakFacade final : public juce::Component,
                           private juce::Timer
{
public:
    static constexpr int width = 1280, height = 860;

    explicit PhreakFacade (PhreakAudioProcessor&);
    ~PhreakFacade() override;

    phragma::PresetBar& getPresetBar() noexcept         { return presetBar; }
    WaveView& getWave() noexcept                        { return wave; }
    StepGrid& getGrid() noexcept                        { return grid; }

    void selectPoint (int p);
    int getSelectedPoint() const noexcept               { return selectedPoint; }
    void showSlot (int slot);

    void paint (juce::Graphics&) override;

    juce::Component* findControl (const juce::String& componentId);

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };
    struct ChoiceMenu
    {
        juce::TextButton button;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void knob (juce::Slider&, const juce::String& id, juce::Rectangle<int> area, const juce::String& name, bool bipolar = false);
    void toggle (juce::TextButton&, const juce::String& id, juce::Rectangle<int> area);
    void choiceSwitch (ChoiceSwitch&, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area);
    void choiceMenu (ChoiceMenu&, const juce::String& id, juce::Rectangle<int> area, const juce::String& componentId);
    void bindPointControls();
    void refreshSlotTabs();
    void loadIntoSlot (int slot);
    void showRandomMenu();
    void showPatternMenu();
    void timerCallback() override;

    PhreakAudioProcessor& audioProcessor;
    PhreakLookAndFeel lookAndFeel;
    WaveView wave;
    StepGrid grid;
    int selectedPoint = 0, shownSlot = 0, lastGeneration = -1, lastPointSlot = -1;

    // Samples
    juce::TextButton slotTabs[4], loadButton { "LOAD" }, clearButton { "CLEAR" };
    std::unique_ptr<juce::FileChooser> chooser;

    // Point choisi
    juce::TextButton pointChips[16];
    juce::Slider posKnob, pitchKnob, gainKnob, panKnob, filterKnob, revKnob, dlyKnob;
    ChoiceSwitch pointSampleSwitch, dirSwitch, tailSwitch;
    ChoiceMenu stutterMenu;
    std::unique_ptr<APVTS::SliderAttachment> posA, pitchA, gainA, panA, filterA, revA, dlyA;

    // Hasard
    juce::Slider pointsKnob, seedKnob, chanceKnob, reverseKnob, scatterKnob;
    juce::TextButton randomButton { "RANDOMIZE" }, spreadButton { "SPREAD" }, transientsButton { "TRANSIENTS" }, diceButton { "NEW SEED" };
    juce::TextButton lockButton { "LOCK" };
    ChoiceSwitch snapSwitch;

    // Séquenceur
    ChoiceSwitch modeSwitch, triggerSwitch;
    ChoiceMenu rateMenu;
    juce::TextButton syncButton { "SYNC" }, patternButton { "PATTERN" };
    juce::Slider stepsKnob, freeKnob, driftKnob, swingKnob, gateKnob, fadeKnob;

    // Effets et sortie
    juce::Slider revSizeKnob, revDampKnob, revWidthKnob, revLevelKnob, dlyFbKnob, dlyToneKnob, dlyLevelKnob, gainKnobOut;
    ChoiceMenu dlyTimeMenu;
    juce::TextButton pingButton { "PING-PONG" }, limiterButton { "LIMIT" };

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    float outLevel = -60.0f;

    phragma::PresetBar presetBar;   // déclarée en dernier, détruite en premier

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhreakFacade)
};

//==============================================================================
class PhreakAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PhreakAudioProcessorEditor (PhreakAudioProcessor&);
    ~PhreakAudioProcessorEditor() override;

    void resized() override;

    PhreakFacade& getFacade() noexcept                  { return facade; }
    float getUiScale() const                            { return (float) getWidth() / (float) PhreakFacade::width; }
    void setUiScale (float scale);

private:
    PhreakAudioProcessor& audioProcessor;
    PhreakFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhreakAudioProcessorEditor)
};
