#pragma once
#include <cmath>
#include <cstdint>
#include "MusicalContext.h"

struct DmxColor
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t w = 0;
    uint8_t uv = 0;
};

enum class ColorTransitionMode
{
    Static,
    Fade,
    Step
};

class ColorGenerator
{
public:
    ColorGenerator(DmxColor colA = {}, DmxColor colB = {}, 
                   ColorTransitionMode mode = ColorTransitionMode::Static, 
                   NoteDivision rate = NoteDivision::Quarter)
        : colorA(colA), colorB(colB), mode(mode), rate(rate) {}

    DmxColor getColorAtTick(long currentTick, const MusicalContext& context) const
    {
        if (mode == ColorTransitionMode::Static) return colorA;

        long cycleTicks = getTicksForDivision(rate, context);
        if (cycleTicks <= 0) return colorA;

        double phase = static_cast<double>(currentTick % cycleTicks) / cycleTicks;

        if (mode == ColorTransitionMode::Step)
            return (phase < 0.5) ? colorA : colorB;

        double blendFactor = 2.0 * std::abs(phase - 0.5);
        return interpolate(colorA, colorB, blendFactor);
    }

    DmxColor getColorA() const { return colorA; }
    DmxColor getColorB() const { return colorB; }
    ColorTransitionMode getMode() const { return mode; }
    NoteDivision getRate() const { return rate; }

private:
    DmxColor colorA;
    DmxColor colorB;
    ColorTransitionMode mode;
    NoteDivision rate;

    static DmxColor interpolate(const DmxColor& c1, const DmxColor& c2, double factor)
    {
        DmxColor out;
        out.r = static_cast<uint8_t>(c1.r + (c2.r - c1.r) * factor);
        out.g = static_cast<uint8_t>(c1.g + (c2.g - c1.g) * factor);
        out.b = static_cast<uint8_t>(c1.b + (c2.b - c1.b) * factor);
        out.w = static_cast<uint8_t>(c1.w + (c2.w - c1.w) * factor);
        out.uv = static_cast<uint8_t>(c1.uv + (c2.uv - c1.uv) * factor);
        return out;
    }
};