#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/DataStateKeys.h"
#include "../../Utils/PresetManager.h"

// =============================================================================
// SceneEditorPanel (Pannello Destro)
// =============================================================================
class SceneEditorPanel : public juce::Component,
    public juce::ListBoxModel,
    private juce::ValueTree::Listener
{
public:
    SceneEditorPanel(juce::ValueTree& state);
    ~SceneEditorPanel() override;

    void setSceneNode(juce::ValueTree sceneNode);
    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBox dei Comandi di Ruolo
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void visibilityChanged() override;

private:
    void updateSceneUI();
    void updateCommandUI();
    void refreshRoleList();
    void addNewCommand();

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree currentSceneNode;
    juce::ValueTree currentCommandNode;
    bool isUpdatingUI = false;

    // Controlli Scena
    juce::Label sceneTitleLabel;
    juce::Label nameLabel;       juce::TextEditor nameEditor;
    juce::Label startBarLabel;   juce::Slider startBarSlider;
    juce::Label durationLabel;   juce::Slider durationSlider;

    // Lista Comandi
    juce::ListBox commandList;
    juce::TextButton addCmdBtn;
    juce::TextButton removeCmdBtn;

    // Controlli Editor Comando
    juce::Label cmdTitleLabel;
    juce::Label roleLabel;       juce::ComboBox roleSelector;
    juce::Label paramLabel;      juce::ComboBox paramSelector;
    juce::Label curveLabel;      juce::ComboBox curveSelector;
    juce::Label startValLabel;   juce::Slider startValSlider;
    juce::Label endValLabel;     juce::Slider endValSlider;
};

// =============================================================================
// ShowBuilderTab (Main Tab 2)
// =============================================================================
class ShowBuilderTab : public juce::Component,
    public juce::ListBoxModel,
    private juce::ValueTree::Listener
{
public:
    ShowBuilderTab(juce::ValueTree& state);
    ~ShowBuilderTab() override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addNewSceneToState();
    void saveShow();
    void loadShow();

    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree showNode;

    juce::ListBox sceneList;
    juce::TextButton addSceneBtn;
    juce::TextButton saveBtn;
    juce::TextButton loadBtn;

    SceneEditorPanel editorPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShowBuilderTab)
};