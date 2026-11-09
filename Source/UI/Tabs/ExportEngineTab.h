#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ExportEngineTab : public juce::Component
{
public:
    ExportEngineTab()
    {
        titleLabel.setText("3. MIDI Export Engine (.mid per Reaper)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);
    }

    ~ExportEngineTab() override = default;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportEngineTab)
};