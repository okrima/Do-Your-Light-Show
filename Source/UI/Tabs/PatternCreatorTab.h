#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Components/PanTiltRangeSlider.h"
#include "../Components/LfoWaveformPreview.h"
#include "../../Engine/LfoGenerator.h"

class PatternCreatorTab : public juce::Component
{
public:
    PatternCreatorTab(juce::ValueTree& state) : appState(state)
    {
        // 1. Titolo
        titleLabel.setText("2. Pattern & Scene Generator (LFO, Colori, Geyser)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);

        // 2. Selettori
        fixtureSelector.addItem("Wash Sinistro", 1);
        fixtureSelector.addItem("Wash Destro", 2);
        fixtureSelector.setSelectedId(1, juce::dontSendNotification);
        fixtureSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
        addAndMakeVisible(fixtureSelector);

        shapeSelector.addItem("Forma d'onda: Seno (Sine)", 1);
        shapeSelector.addItem("Forma d'onda: Triangolo (Triangle)", 2);
        shapeSelector.addItem("Forma d'onda: Rampa (Linear Ramp)", 3);
        shapeSelector.setSelectedId(1, juce::dontSendNotification);
        shapeSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
        shapeSelector.onChange = [this]() { updatePreviewFromUI(); };
        addAndMakeVisible(shapeSelector);

        // 3. Widget grafici (Anteprima 2D e Range Slider)
        addAndMakeVisible(waveformPreview);
        addAndMakeVisible(panRangeSlider);

        // Quando il range cambia nello slider, aggiorniamo l'anteprima visiva
        panRangeSlider.onRangeChanged = [this](uint8_t minVal, uint8_t maxVal) {
            updatePreviewFromUI();
        };

        // Inizializza l'anteprima con i valori di default
        updatePreviewFromUI();
    }

    ~PatternCreatorTab() override = default;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1e));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(20);

        titleLabel.setBounds(bounds.removeFromTop(35));
        bounds.removeFromTop(10);

        auto topRow = bounds.removeFromTop(30);
        fixtureSelector.setBounds(topRow.removeFromLeft(200));
        
        bounds.removeFromTop(15);

        // Sezione LFO
        shapeSelector.setBounds(bounds.removeFromTop(30).removeFromLeft(240));
        bounds.removeFromTop(10);

        // Widget Anteprima 2D posizionato sopra lo slider (altezza 110px)
        waveformPreview.setBounds(bounds.removeFromTop(110));
        bounds.removeFromTop(10);

        // Pan/Tilt Range Slider sotto l'anteprima
        panRangeSlider.setBounds(bounds.removeFromTop(60));
    }

private:
    void updatePreviewFromUI()
    {
        WaveShape shape = WaveShape::Sine;
        int selectedShapeId = shapeSelector.getSelectedId();
        if (selectedShapeId == 2) shape = WaveShape::Triangle;
        else if (selectedShapeId == 3) shape = WaveShape::LinearRamp;

        uint8_t minVal = panRangeSlider.getMinValue();
        uint8_t maxVal = panRangeSlider.getMaxValue();

        // Invia i parametri aggiornati all'anteprima grafica 2D
        waveformPreview.setLfoParameters(shape, NoteDivision::Quarter, minVal, maxVal, 0.0, false, 2);
    }

    juce::ValueTree& appState;

    juce::Label titleLabel;
    juce::ComboBox fixtureSelector;
    juce::ComboBox shapeSelector;

    LfoWaveformPreview waveformPreview;
    PanTiltRangeSlider panRangeSlider { "Pan Range Limiter (Restrizione Movimento Testa)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternCreatorTab)
};