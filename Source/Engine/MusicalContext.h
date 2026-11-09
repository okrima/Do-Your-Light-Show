#pragma once
#include <juce_core/juce_core.h>

constexpr int STANDARD_PPQ = 960;

struct MusicalContext
{
    int bpm = 120;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int ppq = STANDARD_PPQ;
};

enum class NoteDivision
{
    Whole,      // 1/1 (4 battute/quarti)
    Half,       // 1/2
    Quarter,    // 1/4
    Eighth,     // 1/8
    Sixteenth   // 1/16
};

inline long getTicksForDivision(NoteDivision division, const MusicalContext& context)
{
    switch (division)
    {
        case NoteDivision::Whole:     return context.ppq * 4;
        case NoteDivision::Half:      return context.ppq * 2;
        case NoteDivision::Quarter:   return context.ppq;
        case NoteDivision::Eighth:    return context.ppq / 2;
        case NoteDivision::Sixteenth: return context.ppq / 4;
        default:                      return context.ppq;
    }
}