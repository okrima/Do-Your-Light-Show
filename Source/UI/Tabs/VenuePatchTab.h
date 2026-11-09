#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/LogicalParam.h"

// =============================================================================
// 1. COMPONENTE RIGA PER LA MAPPATURA DEL SINGOLO PARAMETRO
// =============================================================================
class ParamMappingRow : public juce::Component
{
public:
    ParamMappingRow(LogicalParam paramType) : param(paramType)
    {
        // Checkbox di abilitazione
        enableToggle.setButtonText(logicalParamToString(param));
        enableToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(enableToggle);

        // Spinner per l'offset DMX (1-based)
        offsetSlider.setSliderStyle(juce::Slider::IncDecButtons);
        offsetSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 20);
        offsetSlider.setRange(1, 512, 1);
        offsetSlider.setValue(1);
        offsetSlider.setEnabled(false); // Disabilitato di default finché non si spunta il toggle
        addAndMakeVisible(offsetSlider);

        enableToggle.onClick = [this]() {
            offsetSlider.setEnabled(enableToggle.getToggleState());
            // Qui andrà il binding con il ValueTree per aggiornare il modello
        };
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(5, 2);
        enableToggle.setBounds(bounds.removeFromLeft(120));
        offsetSlider.setBounds(bounds.removeFromLeft(90));
    }

private:
    LogicalParam param;
    juce::ToggleButton enableToggle;
    juce::Slider offsetSlider;
};

// =============================================================================
// 2. PANNELLO EDITOR DELLA MACCHINA SELEZIONATA
// =============================================================================
class FixtureEditorPanel : public juce::Component
{
public:
    FixtureEditorPanel()
    {
        // Intestazione
        titleLabel.setText("Fixture Editor", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff)); // Neon Cyan
        addAndMakeVisible(titleLabel);

        // Nome Custom
        nameLabel.setText("Nome:", juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(nameLabel);
        
        nameEditor.setText("Wash Sinistro");
        nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a32));
        addAndMakeVisible(nameEditor);

        // Start Address
        startAddrLabel.setText("Start DMX:", juce::dontSendNotification);
        startAddrLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(startAddrLabel);

        startAddrSlider.setSliderStyle(juce::Slider::IncDecButtons);
        startAddrSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
        startAddrSlider.setRange(1, 512, 1);
        addAndMakeVisible(startAddrSlider);

        // Popolamento dinamico delle righe di mappatura
        const std::vector<LogicalParam> allParams = {
            LogicalParam::Pan, LogicalParam::Tilt, LogicalParam::Dimmer,
            LogicalParam::Red, LogicalParam::Green, LogicalParam::Blue, 
            LogicalParam::White, LogicalParam::UV, LogicalParam::Strobe, LogicalParam::GeyserBlast
        };

        for (auto p : allParams)
        {
            auto* row = new ParamMappingRow(p);
            mappingRows.add(row);
            addAndMakeVisible(row);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(15);

        titleLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(10); // Spazio

        // Riga Nome
        auto nameRow = bounds.removeFromTop(24);
        nameLabel.setBounds(nameRow.removeFromLeft(60));
        nameEditor.setBounds(nameRow.removeFromLeft(200));

        bounds.removeFromTop(10);

        // Riga Start Address
        auto addrRow = bounds.removeFromTop(24);
        startAddrLabel.setBounds(addrRow.removeFromLeft(80));
        startAddrSlider.setBounds(addrRow.removeFromLeft(100));

        bounds.removeFromTop(20);

        // Griglia Mappatura Parametri
        for (auto* row : mappingRows)
        {
            row->setBounds(bounds.removeFromTop(28));
        }
    }

private:
    juce::Label titleLabel;
    
    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    juce::Label startAddrLabel;
    juce::Slider startAddrSlider;

    juce::OwnedArray<ParamMappingRow> mappingRows;
};

// =============================================================================
// 3. MAIN TAB COMPONENT
// =============================================================================
class VenuePatchTab : public juce::Component, public juce::ListBoxModel
{
public:
    // Passiamo il ValueTree di stato dal MainComponent
    VenuePatchTab(juce::ValueTree& state) : appState(state)
    {
        // Lista Macchine (Pannello Sinistro)
        fixtureList.setModel(this);
        fixtureList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
        fixtureList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
        addAndMakeVisible(fixtureList);

        // Pulsante Aggiungi
        addFixtureBtn.setButtonText("+ Aggiungi Macchina");
        addFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
        addFixtureBtn.onClick = [this]() {
            dummyFixtureNames.add("Nuova Macchina " + juce::String(dummyFixtureNames.size() + 1));
            fixtureList.updateContent();
            fixtureList.selectRow(dummyFixtureNames.size() - 1);
        };
        addAndMakeVisible(addFixtureBtn);

        // Editor Macchina (Pannello Destro)
        addAndMakeVisible(editorPanel);

        // Dati di test iniziali
        dummyFixtureNames.add("Wash Sinistro");
        dummyFixtureNames.add("Wash Destro");
        dummyFixtureNames.add("Geyser Centrale");
        fixtureList.updateContent();
        fixtureList.selectRow(0);
    }

    // --- Override ListBoxModel ---
    int getNumRows() override { return dummyFixtureNames.size(); }
    
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f)); // Highlight

        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(dummyFixtureNames[rowNumber], 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        // Qui aggiornerai l'editorPanel caricando i dati dal ValueTree per la macchina selezionata
        editorPanel.setVisible(lastRowSelected >= 0);
    }
    // -----------------------------

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a1e));
        
        // Disegna un divisore verticale tra lista ed editor
        g.setColour(juce::Colour(0xff2a2a32));
        g.fillRect(getLocalBounds().proportionOfWidth(0.3f), 0.0f, 2.0f, (float)getHeight());
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        // Pannello Sinistro (Lista) - 30% della larghezza
        auto leftPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.3f));
        
        auto btnBounds = leftPanel.removeFromBottom(40).reduced(10, 5);
        addFixtureBtn.setBounds(btnBounds);
        
        fixtureList.setBounds(leftPanel.reduced(10));

        // Pannello Destro (Editor) - Spazio rimanente
        bounds.removeFromLeft(2); // Spazio per la linea divisoria
        editorPanel.setBounds(bounds);
    }

private:
    juce::ValueTree& appState;

    juce::ListBox fixtureList;
    juce::TextButton addFixtureBtn;
    juce::StringArray dummyFixtureNames; // Temporaneo, verrà sostituito dal parsing del ValueTree

    FixtureEditorPanel editorPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VenuePatchTab)
};