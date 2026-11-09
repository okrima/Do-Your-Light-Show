#pragma once
#include "MusicalContext.h"

class TriggerGenerator
{
public:
    TriggerGenerator(NoteDivision triggerRate = NoteDivision::Quarter, 
                     NoteDivision duration = NoteDivision::Sixteenth)
        : rate(triggerRate), duration(duration) {}

    bool isActiveAtTick(long currentTick, const MusicalContext& context) const
    {
        long cycleTicks = getTicksForDivision(rate, context);
        long durationTicks = getTicksForDivision(duration, context);
        if (cycleTicks <= 0) return false;

        long tickInCycle = currentTick % cycleTicks;
        return (tickInCycle < durationTicks);
    }

    NoteDivision getRate() const { return rate; }
    NoteDivision getDuration() const { return duration; }

private:
    NoteDivision rate;
    NoteDivision duration;
};