#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Tabs/VenuePatchTab.h"
#include "UI/Tabs/ShowBuilderTab.h"
#include "UI/Tabs/ExportEngineTab.h"
#include "UI/Tabs/SceneBuilderTab.h"
#include "UI/Components/TimeMetricManager.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::TabbedComponent tabbedComponent;

    juce::ValueTree mainState{ "DO_YOUR_LIGHT_SHOW_STATE" };
    BeatTimeMap timeMetricBeatMap;

    VenuePatchTab venueTab{ mainState };
    ShowBuilderTab showBuilderTab{ mainState };
    SceneBuilderTab sceneBuilderTab{ mainState };
    ExportEngineTab exportTab{ mainState };
    TimeMetricManagerComponent timeMetricTab{ mainState, timeMetricBeatMap };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};