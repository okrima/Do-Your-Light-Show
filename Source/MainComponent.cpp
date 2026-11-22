#include "MainComponent.h"

MainComponent::MainComponent()
    : tabbedComponent (juce::TabbedButtonBar::TabsAtTop)
{
    tabbedComponent.setTabBarDepth(40);

    timeMetricBeatMap.bindToAppState(mainState);
    timeMetricTab.refreshTables();

    tabbedComponent.addTab("1. Venue Patch",
        juce::Colour(0xff2a2a32),
        &venueTab,
        false);

    tabbedComponent.addTab("2. Scene Builder",
        juce::Colour(0xff1e1e26),
        &sceneBuilderTab,
        false);

    tabbedComponent.addTab("3. Pattern Creator",
        juce::Colour(0xff2a2a32),
        &showBuilderTab,
        false);

    tabbedComponent.addTab("4. Time & Metric",
        juce::Colour(0xff1e1e26),
        &timeMetricTab,
        false);

    tabbedComponent.addTab("5. MIDI Export",
        juce::Colour(0xff2a2a32),
        &exportTab,
        false);

    venueTab.setDmxLiveEngine(&dmxLiveEngine);
    sceneBuilderTab.setDmxLiveEngine(&dmxLiveEngine);
    showBuilderTab.setDmxLiveEngine(&dmxLiveEngine);

    tabbedComponent.addTab("6. DMX Live", juce::Colour(0xff1e202b), new DmxOutputControlPanel(dmxLiveEngine), true);

    tabbedComponent.onTabChanged = [this](int prevTab, int newTab)
        {
            // Indici tab: 0 = Venue Patch, 1 = Scene Builder, 2 = Pattern Creator, 3 = Time, 4 = MIDI, 5 = DMX Live
            const bool allowsFixtureTest = (newTab == 0 || newTab == 5);
            if (!allowsFixtureTest)
                venueTab.forceStopFixtureTest();

            // Uscendo da Tab 2 o Tab 3 verso qualsiasi altra tab, azzera i 512 canali DMX
            // per evitare fari accesi/congelati sull'ultimo frame.
            if (prevTab == 1 || prevTab == 2)
                dmxLiveEngine.clearAllOutput();
        };

    addAndMakeVisible (tabbedComponent);
    setSize(1600, 950);
}

MainComponent::~MainComponent() {}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141417));
}

void MainComponent::resized()
{
    tabbedComponent.setBounds (getLocalBounds());
}