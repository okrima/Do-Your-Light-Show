#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Engine/MusicalContext.h"
#include "../../Engine/MidiRenderEngine.h"

class ExportEngineTab : public juce::Component
{
public:
    ExportEngineTab(juce::ValueTree& state);
    ~ExportEngineTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void startMidiExportProcess();

    juce::ValueTree& appState;

    juce::Label titleLabel;
    juce::TextButton exportButton;
    juce::TextEditor statusBox;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportEngineTab)
};