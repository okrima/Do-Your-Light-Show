#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

struct StageArtworkHelper
{
    static constexpr float kTrussNormY = 0.14f;
    static constexpr float kFloorNormY = 0.84f;

    static juce::Rectangle<float> getAirZoneRect(const juce::Rectangle<float>& stageRect) noexcept
    {
        return {
            stageRect.getX() + stageRect.getWidth() * 0.02f,
            stageRect.getY() + stageRect.getHeight() * 0.04f,
            stageRect.getWidth() * 0.96f,
            stageRect.getHeight() * 0.32f
        };
    }

    static juce::Rectangle<float> getFloorZoneRect(const juce::Rectangle<float>& stageRect) noexcept
    {
        return {
            stageRect.getX() + stageRect.getWidth() * 0.125f,
            stageRect.getY() + stageRect.getHeight() * 0.62f,
            stageRect.getWidth() * 0.75f,
            stageRect.getHeight() * 0.34f
        };
    }

    static juce::Rectangle<float> getLeftWingRect(const juce::Rectangle<float>& stageRect) noexcept
    {
        return {
            stageRect.getX() + stageRect.getWidth() * 0.02f,
            stageRect.getY() + stageRect.getHeight() * 0.39f,
            stageRect.getWidth() * 0.095f,
            stageRect.getHeight() * 0.57f
        };
    }

    static juce::Rectangle<float> getRightWingRect(const juce::Rectangle<float>& stageRect) noexcept
    {
        return {
            stageRect.getX() + stageRect.getWidth() * 0.885f,
            stageRect.getY() + stageRect.getHeight() * 0.39f,
            stageRect.getWidth() * 0.095f,
            stageRect.getHeight() * 0.57f
        };
    }

    static void drawStageBackdrop(juce::Graphics& g, const juce::Rectangle<float>& stageRect)
    {
        juce::ColourGradient bgGrad(
            juce::Colour(0xff10121a), stageRect.getCentreX(), stageRect.getY(),
            juce::Colour(0xff07070b), stageRect.getCentreX(), stageRect.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(stageRect, 6.0f);
    }

    static void drawTrussAndFloorStructure(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        const float leftX = stageRect.getX() + 20.0f;
        const float rightX = stageRect.getRight() - 20.0f;

        g.setColour(juce::Colour(0xff2d3142));
        g.drawLine(leftX, trussY - 4.0f, rightX, trussY - 4.0f, 2.0f);
        g.drawLine(leftX, trussY + 4.0f, rightX, trussY + 4.0f, 2.0f);

        g.setColour(juce::Colour(0xff222533));
        for (float x = leftX; x < rightX - 18.0f; x += 18.0f)
        {
            g.drawLine(x, trussY - 4.0f, x + 9.0f, trussY + 4.0f, 1.0f);
            g.drawLine(x + 9.0f, trussY + 4.0f, x + 18.0f, trussY - 4.0f, 1.0f);
        }

        juce::Rectangle<float> floorDeck(
            stageRect.getX() + 4.0f, floorY,
            stageRect.getWidth() - 8.0f, juce::jmax(0.0f, stageRect.getBottom() - floorY - 4.0f));
        juce::ColourGradient floorGrad(
            juce::Colour(0xff181922), floorDeck.getCentreX(), floorY,
            juce::Colour(0xff0d0e14), floorDeck.getCentreX(), floorDeck.getBottom(), false);
        g.setGradientFill(floorGrad);
        g.fillRect(floorDeck);

        g.setColour(juce::Colour(0xff353849));
        g.drawLine(stageRect.getX() + 4.0f, floorY, stageRect.getRight() - 4.0f, floorY, 2.0f);
    }

    static void drawBandSilhouettes(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float floorY,
        float frontIllumination = 0.0f,
        juce::Colour frontTint = juce::Colours::white)
    {
        const juce::Colour baseSilhouette(0xff0b0c10);
        const juce::Colour litBody = baseSilhouette.interpolatedWith(frontTint, frontIllumination * 0.45f);
        const juce::Colour rimCol = juce::Colour(0xff2c3040).interpolatedWith(frontTint, frontIllumination * 0.7f);

        const float cx = stageRect.getCentreX();
        const float stageH = floorY - stageRect.getY();
        const float scale = juce::jlimit(0.6f, 1.5f, stageH / 320.0f);

        juce::Rectangle<float> drumRiser(
            cx - 55.0f * scale, floorY - 18.0f * scale,
            110.0f * scale, 18.0f * scale);
        g.setColour(juce::Colour(0xff14161f));
        g.fillRect(drumRiser);
        g.setColour(rimCol.withAlpha(0.5f));
        g.drawRect(drumRiser, 1.0f);

        g.setColour(litBody);
        g.fillEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale);
        g.fillEllipse(cx - 9.0f * scale, drumRiser.getY() - 62.0f * scale, 18.0f * scale, 18.0f * scale);
        g.setColour(rimCol);
        g.drawEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale, 1.2f);

        const float positionsX[3] = {
            stageRect.getX() + stageRect.getWidth() * 0.27f,
            cx,
            stageRect.getX() + stageRect.getWidth() * 0.73f
        };

        for (int i = 0; i < 3; ++i)
        {
            const float px = positionsX[i];
            const float bodyH = 54.0f * scale;
            const float bodyW = 22.0f * scale;

            juce::Path memberPath;
            memberPath.addRoundedRectangle(px - bodyW * 0.5f, floorY - bodyH, bodyW, bodyH, 6.0f * scale);
            memberPath.addEllipse(px - 8.0f * scale, floorY - bodyH - 18.0f * scale, 16.0f * scale, 16.0f * scale);

            g.setColour(litBody);
            g.fillPath(memberPath);
            g.setColour(rimCol.withAlpha(0.65f));
            g.strokePath(memberPath, juce::PathStrokeType(1.2f));
        }
    }
};