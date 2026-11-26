#include "LightShowProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LightShowProcessor(true);
}