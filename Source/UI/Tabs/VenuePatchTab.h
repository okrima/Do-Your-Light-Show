#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/LogicalParam.h"
#include "../../Utils/PresetManager.h"
#include <vector>
#include <memory>

// Struttura di supporto per i controlli di mappatura dei parametri
struct ParamControl {
    juce::Label label;
    juce::Slider slider;
};

// =============================================================================
// FixtureEditorPanel (AGGIORNATO CON MAPPATURA)
// =============================================================================
class FixtureEditorPanel : public juce::Component,
    private juce::ValueTree::Listener
{
public:
    FixtureEditorPanel(juce::ValueTree& state);
    ~FixtureEditorPanel() override;

    void setFixtureNode(juce::ValueTree fixtureNode);
    void resized() override;

private:
    void updateUIFromNode();

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree currentFixtureNode;
    bool isUpdatingUI = false;

    // Controlli Base
    juce::Label titleLabel;
    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    // NUOVI CONTROLLI RUOLO
    juce::Label roleLabel;
    juce::TextEditor roleEditor;

    juce::Label startAddrLabel;
    juce::Slider startAddrSlider;

    juce::Label chCountLabel;
    juce::Slider chCountSlider;

    // Controlli Mappatura Parametri
    juce::Label mapTitleLabel;
    juce::StringArray availableParams;
    std::vector<std::unique_ptr<ParamControl>> paramControls;
};

// =============================================================================
// VenuePatchTab (Resta identica a prima)
// =============================================================================
class VenuePatchTab : public juce::Component,
    public juce::ListBoxModel,
    private juce::ValueTree::Listener
{
    // ... Mantieni ESATTAMENTE la stessa dichiarazione della classe VenuePatchTab di prima ...
public:
    VenuePatchTab(juce::ValueTree& state);
    ~VenuePatchTab() override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addNewFixtureToState();
    void saveVenue();
    void loadVenue();

    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree venueNode;

    juce::ListBox fixtureList;
    juce::TextButton addFixtureBtn;

    juce::TextButton saveBtn;
    juce::TextButton loadBtn;

    FixtureEditorPanel editorPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VenuePatchTab)
};