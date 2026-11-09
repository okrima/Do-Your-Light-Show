#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Engine/LfoGenerator.h"

class LfoWaveformPreview : public juce::Component
{
public:
    LfoWaveformPreview()
    {
        setOpaque(false);
    }

    // Aggiorna i parametri dell'LFO da visualizzare e forza il ridisegno
    void setLfoParameters(WaveShape shape, NoteDivision rate,
                          uint8_t minVal, uint8_t maxVal,
                          double phaseOffset, bool isMirrored,
                          int barsToDisplay = 2)
    {
        currentShape = shape;
        currentRate = rate;
        minValue = minVal;
        maxValue = maxVal;
        phase = phaseOffset;
        mirrored = isMirrored;
        displayBars = barsToDisplay;

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        if (bounds.isEmpty()) return;

        // 1. Sfondo scuro e griglia di riferimento
        g.fillAll(juce::Colour(0xff18181c));
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRect(bounds, 1.0f);

        float yCenter = bounds.getHeight() * 0.5f;
        g.drawHorizontalLine(static_cast<int>(yCenter), 0.0f, bounds.getWidth());

        // 2. Area evidenziata del Range Min/Max DMX
        float yMinDmx = dmxToY(minValue, bounds.getHeight());
        float yMaxDmx = dmxToY(maxValue, bounds.getHeight());

        juce::Rectangle<float> rangeRect(
            bounds.getX(),
            yMaxDmx,
            bounds.getWidth(),
            yMinDmx - yMaxDmx
        );

        g.setColour(juce::Colour(0xff00d2ff).withAlpha(0.10f));
        g.fillRect(rangeRect);

        g.setColour(juce::Colour(0xff00d2ff).withAlpha(0.25f));
        g.drawHorizontalLine(static_cast<int>(yMinDmx), 0.0f, bounds.getWidth());
        g.drawHorizontalLine(static_cast<int>(yMaxDmx), 0.0f, bounds.getWidth());

        // 3. Generazione tracciato 2D dell'onda LFO
        LfoGenerator lfo(currentShape, currentRate, minValue, maxValue, phase, mirrored);
        MusicalContext context; // PPQ 960 standard, 120 BPM, 4/4

        long totalTicksToDisplay = displayBars * context.ppq * context.timeSigNumerator;
        float width = bounds.getWidth();
        float height = bounds.getHeight();

        juce::Path wavePath;
        bool firstPoint = true;

        for (float x = 0; x <= width; x += 1.0f)
        {
            long currentTick = static_cast<long>((x / width) * totalTicksToDisplay);
            uint8_t dmxVal = lfo.getValueAtTick(currentTick, context);
            float y = dmxToY(dmxVal, height);

            if (firstPoint)
            {
                wavePath.startNewSubPath(x, y);
                firstPoint = false;
            }
            else
            {
                wavePath.lineTo(x, y);
            }
        }

        // 4. Rendering stroke dell'onda (Neon Cyan)
        g.setColour(juce::Colour(0xff00d2ff));
        g.strokePath(wavePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 5. Testo informativo sui limiti
        g.setFont(juce::FontOptions(10.0f));
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawText("Max: " + juce::String(maxValue), 4, static_cast<int>(yMaxDmx) - 12, 60, 12, juce::Justification::left);
        g.drawText("Min: " + juce::String(minValue), 4, static_cast<int>(yMinDmx) + 2, 60, 12, juce::Justification::left);
    }

private:
    static float dmxToY(uint8_t dmxValue, float componentHeight)
    {
        float normalized = dmxValue / 127.0f;
        return componentHeight - (normalized * componentHeight);
    }

    WaveShape currentShape = WaveShape::Sine;
    NoteDivision currentRate = NoteDivision::Quarter;
    uint8_t minValue = 0;
    uint8_t maxValue = 127;
    double phase = 0.0;
    bool mirrored = false;
    int displayBars = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoWaveformPreview)
};