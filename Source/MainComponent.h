#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/LightShowProcessor.h"
#include "UI/Tabs/VenuePatchTab.h"
#include "UI/Tabs/ShowBuilderTab.h"
#include "UI/Tabs/ExportEngineTab.h"
#include "UI/Tabs/SceneBuilderTab.h"
#include "UI/Components/TimeMetricManager.h"
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
    explicit MainComponent(LightShowProcessor& processorRef);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void requestApplicationQuit(std::function<void()> onReadyToQuit);

    bool keyPressed(const juce::KeyPress& key) override;

private:
    void refreshTabTitles();
    void updateUndoRedoButtons();
    void refreshAllTabsFromProcessorState();
    void saveSelectedTabsAndQuit(bool saveT1, bool saveT2, bool saveT3, bool saveT6,
        std::function<void()> onReadyToQuit);

    LightShowProcessor& processor;
    LiveTabbedComponent tabbedComponent;

    juce::TextButton undoBtn, redoBtn;

    VenuePatchTab venueTab{ processor.getMainState() };
    ShowBuilderTab showBuilderTab{ processor.getMainState() };
    SceneBuilderTab sceneBuilderTab{ processor.getMainState() };
    ExportEngineTab exportTab{ processor.getMainState() };
    TimeMetricManagerComponent timeMetricTab{ processor.getMainState(), processor.getBeatTimeMap() };
    DmxOutputControlPanel dmxControlPanel{ processor.getDmxLiveEngine() };

    bool isQuitPromptOpen = false;
    std::unique_ptr<juce::Component> quitOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};