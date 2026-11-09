#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Tabs/VenuePatchTab.h"
#include "UI/Tabs/PatternCreatorTab.h"
#include "UI/Tabs/ExportEngineTab.h"

class MainComponent  : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TabbedComponent tabbedComponent;

    VenuePatchTab venueTab;
    PatternCreatorTab patternTab;
    ExportEngineTab exportTab;

    juce::ValueTree mainState { "DO_YOUR_LIGHT_SHOW_STATE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};