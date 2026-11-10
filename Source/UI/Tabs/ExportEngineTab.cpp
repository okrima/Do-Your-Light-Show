#include "ExportEngineTab.h"

ExportEngineTab::ExportEngineTab(juce::ValueTree& state) : appState(state)
{
    titleLabel.setText("3. MIDI Export Engine (.mid per Reaper)", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff));
    addAndMakeVisible(titleLabel);

    exportButton.setButtonText("Esporta Sequenza MIDI (.mid)...");
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00d2ff));
    exportButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff141417));
    exportButton.onClick = [this]() { startMidiExportProcess(); };
    addAndMakeVisible(exportButton);

    statusBox.setMultiLine(true);
    statusBox.setReadOnly(true);
    statusBox.setCaretVisible(false);
    statusBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141417));
    statusBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2a32));
    statusBox.setText("Pronto per l'esportazione.\nIl motore tradurra' i pattern a quarti (Tab 2) sugli indirizzi DMX fisici del locale (Tab 1).");
    addAndMakeVisible(statusBox);
}

ExportEngineTab::~ExportEngineTab() = default;

void ExportEngineTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1e));
}

void ExportEngineTab::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(35));
    bounds.removeFromTop(20);

    exportButton.setBounds(bounds.removeFromTop(40).removeFromLeft(280));

    bounds.removeFromTop(25);
    statusBox.setBounds(bounds.removeFromTop(120));
}

void ExportEngineTab::startMidiExportProcess()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Esporta File MIDI per Reaper",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("LiveShow.mid"),
        "*.mid"
    );

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file == juce::File{}) return; // Annullato

        // Contesto musicale basato sui quarti (PPQ standard 960)
        MusicalContext ctx;
        ctx.ppq = STANDARD_PPQ; // 960
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;

        // Esegue il rendering unendo Venue (Tab 1) e Pattern Creator (Tab 2)
        auto midiSequence = MidiRenderEngine::renderShow(appState, ctx);

        if (midiSequence.getNumEvents() == 0)
        {
            statusBox.setText("ATTENZIONE: Nessun evento generato.\nVerifica che i Ruoli Target nel Tab 2 corrispondano ai Ruoli definiti nel Tab 1.");
            return;
        }

        bool success = MidiRenderEngine::exportToFile(midiSequence, file, ctx.ppq);

        if (success)
            statusBox.setText("SUCCESSO: File MIDI generato con " + juce::String(midiSequence.getNumEvents()) + " eventi.\nSalvato in: " + file.getFullPathName());
        else
            statusBox.setText("ERRORE: Impossibile scrivere il file sul disco.");
        });
}