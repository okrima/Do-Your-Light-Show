#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class PanTiltRangeSlider : public juce::Component
{
public:
    PanTiltRangeSlider(const juce::String& labelText = "Pan Range")
    {
        // 1. Etichetta del Titolo
        titleLabel.setText(labelText, juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);

        // 2. Slider a doppio cursore (Min / Max)
        rangeSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
        rangeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); // Nascondiamo il box testuale di default
        rangeSlider.setRange(0.0, 127.0, 1.0);                             // Range standard MIDI/DMX
        rangeSlider.setMinAndMaxValues(0.0, 127.0, juce::dontSendNotification);
        
        // Personalizzazione Colori (Neon Cyan per la traccia attiva)
        rangeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00d2ff).withAlpha(0.5f));
        rangeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a32));
        rangeSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d2ff));
        addAndMakeVisible(rangeSlider);

        // 3. Etichette di lettura dinamica
        minLabel.setFont(juce::FontOptions(12.0f));
        minLabel.setJustificationType(juce::Justification::centredRight);
        minLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(minLabel);

        maxLabel.setFont(juce::FontOptions(12.0f));
        maxLabel.setJustificationType(juce::Justification::centredLeft);
        maxLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(maxLabel);

        // 4. Callback Lambda scatenata allo spostamento dei cursori
        rangeSlider.onValueChange = [this]() {
            updateReadout();
            if (onRangeChanged) {
                onRangeChanged(getMinValue(), getMaxValue());
            }
        };

        updateReadout();
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        
        // Titolo in alto (20px di altezza)
        titleLabel.setBounds(bounds.removeFromTop(20));

        // Area slider + etichette min/max
        auto sliderRow = bounds.removeFromTop(30);
        minLabel.setBounds(sliderRow.removeFromLeft(50));   // Spazio a sinistra per "Min: 0"
        maxLabel.setBounds(sliderRow.removeFromRight(50));  // Spazio a destra per "Max: 127"
        
        // Lo slider occupa lo spazio centrale rimanente
        rangeSlider.setBounds(sliderRow.reduced(5, 0));
    }

    // --- GETTERS & SETTERS ---
    uint8_t getMinValue() const { return static_cast<uint8_t>(rangeSlider.getMinValue()); }
    uint8_t getMaxValue() const { return static_cast<uint8_t>(rangeSlider.getMaxValue()); }

    void setRangeValues(uint8_t minVal, uint8_t maxVal, juce::NotificationType notification = juce::dontSendNotification)
    {
        rangeSlider.setMinAndMaxValues(static_cast<double>(minVal), static_cast<double>(maxVal), notification);
        updateReadout();
    }

    // Riferimento public allo slider interno (utile se dobbiamo agganciarlo a un ValueTreeAttachment)
    juce::Slider& getSliderComponent() { return rangeSlider; }

    // Callback per comunicare i cambiamenti al modello o all'anteprima
    std::function<void(uint8_t minDmx, uint8_t maxDmx)> onRangeChanged;

private:
    void updateReadout()
    {
        minLabel.setText("Min: " + juce::String(getMinValue()), juce::dontSendNotification);
        maxLabel.setText("Max: " + juce::String(getMaxValue()), juce::dontSendNotification);
    }

    juce::Label titleLabel;
    juce::Slider rangeSlider;
    juce::Label minLabel;
    juce::Label maxLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanTiltRangeSlider)
};