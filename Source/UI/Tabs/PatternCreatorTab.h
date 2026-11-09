#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class PatternCreatorTab : public juce::Component
{
public:
    PatternCreatorTab()
    {
        titleLabel.setText("2. Pattern & Scene Generator (LFO, Colori, Geyser)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centred);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(titleLabel);
    }

    ~PatternCreatorTab() override = default;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternCreatorTab)
};