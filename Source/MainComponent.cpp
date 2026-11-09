#include "MainComponent.h"

MainComponent::MainComponent()
    : tabbedComponent (juce::TabbedButtonBar::TabsAtTop)
{
    tabbedComponent.setTabBarDepth (40);

    tabbedComponent.addTab ("1. Venue Patch", 
                           juce::Colour (0xff2a2a32), 
                           &venueTab, 
                           false);

    tabbedComponent.addTab ("2. Pattern Creator", 
                           juce::Colour (0xff2a2a32), 
                           &patternTab, 
                           false);

    tabbedComponent.addTab ("3. MIDI Export", 
                           juce::Colour (0xff2a2a32), 
                           &exportTab, 
                           false);

    addAndMakeVisible (tabbedComponent);
    setSize (1100, 720);
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