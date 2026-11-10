#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "MusicalContext.h"

// Costante PI sicura e indipendente dal compilatore/sistema operativo
const double PI = 3.14159265358979323846;

enum class WaveShape
{
    Sine,
    Triangle,
    LinearRamp
};

class LfoGenerator
{
public:
    LfoGenerator(WaveShape shape = WaveShape::Sine,
        NoteDivision rate = NoteDivision::Quarter,
        uint8_t minValue = 0,
        uint8_t maxValue = 127,
        double phaseOffset = 0.0,
        bool isMirrored = false)
        : shape(shape), rate(rate), minValue(minValue), maxValue(maxValue),
        phaseOffset(phaseOffset), isMirrored(isMirrored)
    {
        if (this->minValue > this->maxValue)
            std::swap(this->minValue, this->maxValue);
    }

    uint8_t getValueAtTick(long currentTick, const MusicalContext& context) const
    {
        long cycleTicks = getTicksForDivision(rate, context);
        if (cycleTicks <= 0) return minValue;

        double rawPhase = static_cast<double>(currentTick % cycleTicks) / cycleTicks;
        double phase = std::fmod(rawPhase + phaseOffset, 1.0);
        if (phase < 0.0) phase += 1.0;

        double normalizedValue = 0.0;

        switch (shape)
        {
        case WaveShape::Sine:
            // Sostituito M_PI con la costante locale PI
            normalizedValue = (std::sin(phase * 2.0 * PI) + 1.0) / 2.0;
            break;
        case WaveShape::Triangle:
            normalizedValue = 2.0 * std::abs(phase - 0.5);
            break;
        case WaveShape::LinearRamp:
            normalizedValue = phase;
            break;
        }

        if (isMirrored)
            normalizedValue = 1.0 - normalizedValue;

        double mappedValue = minValue + (normalizedValue * (maxValue - minValue));
        return static_cast<uint8_t>(std::round(mappedValue));
    }

    WaveShape getShape() const { return shape; }
    NoteDivision getRate() const { return rate; }
    uint8_t getMinValue() const { return minValue; }
    uint8_t getMaxValue() const { return maxValue; }
    double getPhaseOffset() const { return phaseOffset; }
    bool isMirroredState() const { return isMirrored; }

private:
    WaveShape shape;
    NoteDivision rate;
    uint8_t minValue;
    uint8_t maxValue;
    double phaseOffset;
    bool isMirrored;
};