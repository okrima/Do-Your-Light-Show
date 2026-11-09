#pragma once
#include <juce_core/juce_core.h>

enum class LogicalParam
{
    Pan,
    Tilt,
    Dimmer,
    Red,
    Green,
    Blue,
    White,
    UV,
    Strobe,
    GeyserBlast
};

inline juce::String logicalParamToString(LogicalParam param)
{
    switch (param)
    {
        case LogicalParam::Pan:          return "Pan";
        case LogicalParam::Tilt:         return "Tilt";
        case LogicalParam::Dimmer:       return "Dimmer";
        case LogicalParam::Red:          return "Red";
        case LogicalParam::Green:        return "Green";
        case LogicalParam::Blue:         return "Blue";
        case LogicalParam::White:        return "White";
        case LogicalParam::UV:           return "UV";
        case LogicalParam::Strobe:       return "Strobe";
        case LogicalParam::GeyserBlast:  return "Geyser Blast";
        default:                         return "Unknown";
    }
}