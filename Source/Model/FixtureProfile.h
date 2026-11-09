#pragma once
#include <juce_core/juce_core.h>
#include <unordered_map>
#include <string>
#include "LogicalParam.h"

struct FixtureProfile
{
    std::string id;            // es: "generic_wash_7ch"
    std::string displayName;   // es: "Wash LED 7CH"
    
    // Offset DMX relativi (1-based, es: Pan = 1, Tilt = 2, Red = 4)
    std::unordered_map<LogicalParam, uint16_t> channelOffsets;
};