#pragma once
#include <JuceHeader.h>
#include "BeatTimeMap.h"
#include <cmath>

class CurveEvaluator
{
public:
    // Restituisce i colpi per Quarto (Q) se la curva e' uno strobo musicale, altrimenti 0.0
    static double getStrobeHitsPerQuarter(const juce::String& curve)
    {
        if (curve.equalsIgnoreCase("Square_1_8"))   return 2.0; // Ottavi
        if (curve.equalsIgnoreCase("Square_1_8T"))  return 3.0; // Terzine di ottavi
        if (curve.equalsIgnoreCase("Square_1_16T")) return 6.0; // Sestine
        if (curve.equalsIgnoreCase("Square") ||
            curve.equalsIgnoreCase("Square_1_16"))  return 4.0; // Sedicesimi
        return 0.0;
    }

    static float evaluate(const juce::String& curve,
        float startVal,
        float endVal,
        float clipProgress0to1,
        const BeatTimeMap::MusicalPosition& pos,
        double phaseOffsetCycles,
        bool centerAnchoredMotion = false,
        double clipElapsedBars = -1.0)
    {
        const float progress = juce::jlimit(0.0f, 1.0f, clipProgress0to1);

        if (curve.equalsIgnoreCase("Static"))
            return startVal;

        if (curve.equalsIgnoreCase("Step"))
            return (progress < 0.99f) ? startVal : endVal;

        if (curve.equalsIgnoreCase("Decay"))
        {
            float tail = std::pow(1.0f - progress, 1.8f);
            return endVal + (startVal - endVal) * tail;
        }

        // LFO Sinusoidali / Coseno (Cerchio) / Doppia Frequenza (Figura a 8)
        if (curve.startsWithIgnoreCase("Sine") || curve.startsWithIgnoreCase("Cosine"))
        {
            double basePhase = 0.0;
            if (curve.containsIgnoreCase("Duration"))
            {
                basePhase = static_cast<double>(progress);
            }
            else
            {
                double cycleBars = 0.5; // Default legacy "Sine" = mezza battuta
                if (curve.containsIgnoreCase("4Bars"))      cycleBars = 4.0;
                else if (curve.containsIgnoreCase("2Bars")) cycleBars = 2.0;
                else if (curve.containsIgnoreCase("1Bar"))  cycleBars = 1.0;

                const double barClock = (centerAnchoredMotion && clipElapsedBars >= 0.0)
                    ? juce::jmax(0.0, clipElapsedBars)
                    : juce::jmax(0.0, pos.continuousBarPos);

                basePhase = std::fmod(barClock / cycleBars, 1.0);
            }

            const double freqMult = curve.startsWithIgnoreCase("Sine2x") ? 2.0 : 1.0;
            double phase = std::fmod(basePhase + phaseOffsetCycles, 1.0);
            if (phase < 0.0) phase += 1.0;

            const double angle = phase * juce::MathConstants<double>::twoPi * freqMult;

            if (centerAnchoredMotion)
            {
                // Curve periodiche pure centrate sulla taratura:
                // fine ciclo e inizio ciclo combaciano sempre (posizione e velocita' identiche)
                const bool isCosine = curve.startsWithIgnoreCase("Cosine");
                const double osc = isCosine ? -std::cos(angle) : std::sin(angle);

                const float mid = 0.5f * (startVal + endVal);
                const float halfAmp = 0.5f * (endVal - startVal);
                return mid + halfAmp * static_cast<float>(osc);
            }

            // Curve unipolari (Dimmer / Respiro): partono da startVal verso endVal
            const float wave = curve.startsWithIgnoreCase("Cosine")
                ? 0.5f * (1.0f + static_cast<float>(std::sin(angle)))
                : 0.5f * (1.0f - static_cast<float>(std::cos(angle)));

            return startVal + (endVal - startVal) * wave;
        }

        // Strobo Dimmer ancorato al battere locale della misura (beatInBarQ)
        double strobeHitsPerQ = getStrobeHitsPerQuarter(curve);
        if (strobeHitsPerQ > 0.0)
        {
            double subBeat = std::fmod(juce::jmax(0.0, pos.beatInBarQ) * strobeHitsPerQ + phaseOffsetCycles, 1.0);
            if (subBeat < 0.0) subBeat += 1.0;
            return (subBeat < 0.45) ? endVal : startVal;
        }

        // Default: Linear
        return startVal + (endVal - startVal) * progress;
    }
};