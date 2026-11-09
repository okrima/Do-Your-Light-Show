#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class VenuePatchTab : public juce::Component
{
public:
    VenuePatchTab()
    {
        titleLabel.setText("1. Setup Locale (Venue & Fixture Profile)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);
    }

    ~VenuePatchTab() override = default;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1e));
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        titleLabel.setBounds(bounds.removeFromTop(50));
    }

private:
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VenuePatchTab)
};