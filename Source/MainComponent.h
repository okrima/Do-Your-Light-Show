#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Tabs/VenuePatchTab.h"
#include "UI/Tabs/ShowBuilderTab.h"
#include "UI/Tabs/ExportEngineTab.h"
#include "UI/Tabs/SceneBuilderTab.h"
#include "UI/Components/TimeMetricManager.h"
#include "Engine/DmxLiveEngine.h"
#include "UI/Components/DmxOutputControlPanel.h"

class LiveTabbedComponent : public juce::TabbedComponent
{
public:
    using juce::TabbedComponent::TabbedComponent;

    std::function<void(int prevTabIndex, int newTabIndex)> onTabChanged;

    void currentTabChanged(int newCurrentTabIndex, const juce::String&) override
    {
        const int prev = lastTabIndex;
        lastTabIndex = newCurrentTabIndex;

        if (onTabChanged && prev != newCurrentTabIndex)
            onTabChanged(prev, newCurrentTabIndex);
    }

private:
    int lastTabIndex = -1;
};

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DmxLiveEngine dmxLiveEngine;
    LiveTabbedComponent tabbedComponent;

    juce::ValueTree mainState{ "DO_YOUR_LIGHT_SHOW_STATE" };
    BeatTimeMap timeMetricBeatMap;

    VenuePatchTab venueTab{ mainState };
    ShowBuilderTab showBuilderTab{ mainState };
    SceneBuilderTab sceneBuilderTab{ mainState };
    ExportEngineTab exportTab{ mainState };
    TimeMetricManagerComponent timeMetricTab{ mainState, timeMetricBeatMap };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};