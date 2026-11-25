#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Tabs/VenuePatchTab.h"
#include "UI/Tabs/ShowBuilderTab.h"
#include "UI/Tabs/ExportEngineTab.h"
#include "UI/Tabs/SceneBuilderTab.h"
#include "UI/Components/TimeMetricManager.h"
#include "Engine/DmxLiveEngine.h"
#include "UI/Components/DmxOutputControlPanel.h"
#include "Utils/StateUndoManager.h"

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

class MainComponent : public juce::Component,
                        private juce::ValueTree::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void requestApplicationQuit(std::function<void()> onReadyToQuit);

    bool keyPressed(const juce::KeyPress& key) override;

private:
    void markTabDirty(int tabIndex, bool isDirty);
    void refreshTabTitles();
    void handleStateTreeModified(const juce::ValueTree& tree);
    void saveSelectedTabsAndQuit(bool saveT1, bool saveT2, bool saveT3, bool saveT6,
        std::function<void()> onReadyToQuit);

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override { handleStateTreeModified(tree); }
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override { handleStateTreeModified(child); juce::ignoreUnused(parent); }
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree&, int) override { handleStateTreeModified(parent); }
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override { handleStateTreeModified(parent); }

    void updateUndoRedoButtons();
    void captureCleanSnapshotForTab(int tabIndex);
    juce::ValueTree makeShowSubtreeSnapshot() const;

    void valueTreeParentChanged(juce::ValueTree&) override {}

    DmxLiveEngine dmxLiveEngine;
    LiveTabbedComponent tabbedComponent;

    juce::ValueTree mainState{ "DO_YOUR_LIGHT_SHOW_STATE" };
    BeatTimeMap timeMetricBeatMap;

    StateUndoManager undoManager{ mainState };
    juce::TextButton undoBtn, redoBtn;
    juce::ValueTree cleanVenueSnap;
    juce::ValueTree cleanPresetLibSnap;
    juce::ValueTree cleanShowSnap;

    VenuePatchTab venueTab{ mainState };
    ShowBuilderTab showBuilderTab{ mainState };
    SceneBuilderTab sceneBuilderTab{ mainState };
    ExportEngineTab exportTab{ mainState };
    TimeMetricManagerComponent timeMetricTab{ mainState, timeMetricBeatMap };
    DmxOutputControlPanel dmxControlPanel{ dmxLiveEngine };

    bool dirtyTab1 = false;
    bool dirtyTab2 = false;
    bool dirtyTab3 = false;
    bool dirtyTab6 = false;
    bool isQuitPromptOpen = false;
    std::unique_ptr<juce::Component> quitOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};