#include "VenuePatchTab.h"

// =============================================================================
// FixtureEditorPanel Implementation
// =============================================================================
FixtureEditorPanel::FixtureEditorPanel(juce::ValueTree& state) : appState(state)
{
    titleLabel.setText("Fixture Editor & Patch", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff));
    addAndMakeVisible(titleLabel);

    // -- NOME --
    nameLabel.setText("Nome Macchina:", juce::dontSendNotification);
    nameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(nameLabel);

    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a32));
    nameEditor.onTextChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::FixtureName, nameEditor.getText(), nullptr);
        };
    addAndMakeVisible(nameEditor);

    // -- RUOLO --
    roleLabel.setText("Ruolo/Gruppo:", juce::dontSendNotification);
    roleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(roleLabel);

    roleEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a32));
    roleEditor.onTextChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::RoleName, roleEditor.getText(), nullptr);
        };
    addAndMakeVisible(roleEditor);

    // -- START DMX ADDRESS --
    startAddrLabel.setText("Start DMX:", juce::dontSendNotification);
    startAddrLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(startAddrLabel);

    startAddrSlider.setSliderStyle(juce::Slider::IncDecButtons);
    startAddrSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 20);
    startAddrSlider.setRange(1, 512, 1);
    startAddrSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::StartAddress, static_cast<int>(startAddrSlider.getValue()), nullptr);
        };
    addAndMakeVisible(startAddrSlider);

    // -- TOTAL CHANNELS --
    chCountLabel.setText("Totale Canali:", juce::dontSendNotification);
    chCountLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(chCountLabel);

    chCountSlider.setSliderStyle(juce::Slider::IncDecButtons);
    chCountSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 20);
    chCountSlider.setRange(1, 64, 1);
    chCountSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::ChannelCount, static_cast<int>(chCountSlider.getValue()), nullptr);
        };
    addAndMakeVisible(chCountSlider);

    // -- TITOLO MAPPATURA --
    mapTitleLabel.setText("Mappatura Parametri (0 = Non Assegnato)", juce::dontSendNotification);
    mapTitleLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    mapTitleLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(mapTitleLabel);

    // -- GENERAZIONE DINAMICA MAPPATURA --
    availableParams = { "Dimmer", "Pan", "Tilt", "Red", "Green", "Blue", "White", "UV", "Strobe", "GeyserBlast" };

    for (const auto& param : availableParams)
    {
        auto ctrl = std::make_unique<ParamControl>();

        ctrl->label.setText(param + " [Ch]:", juce::dontSendNotification);
        ctrl->label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(ctrl->label);

        ctrl->slider.setSliderStyle(juce::Slider::IncDecButtons);
        ctrl->slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 20);
        ctrl->slider.setRange(0, 64, 1); // 0 significa parametro ignorato/non presente nella macchina
        ctrl->slider.setTooltip("Seleziona in quale canale (da 1 a Totale) si trova " + param);

        // Listener personalizzato per ogni slider che salva la mappatura
        ctrl->slider.onValueChange = [this, param, sPtr = &ctrl->slider]() {
            if (currentFixtureNode.isValid() && !isUpdatingUI)
                currentFixtureNode.setProperty(StateKeys::getMappingKey(param), static_cast<int>(sPtr->getValue()), nullptr);
            };
        addAndMakeVisible(ctrl->slider);

        paramControls.push_back(std::move(ctrl));
    }

    setVisible(false);
}
FixtureEditorPanel::~FixtureEditorPanel()
{
    if (currentFixtureNode.isValid())
        currentFixtureNode.removeListener(this);
}

void FixtureEditorPanel::setFixtureNode(juce::ValueTree fixtureNode)
{
    if (currentFixtureNode.isValid()) currentFixtureNode.removeListener(this);
    currentFixtureNode = fixtureNode;

    if (currentFixtureNode.isValid()) {
        currentFixtureNode.addListener(this);
        updateUIFromNode();
        setVisible(true);
    }
    else {
        setVisible(false);
    }
}

void FixtureEditorPanel::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    // Impaginazione Base (Nome, Start, Totale)
    auto row1 = bounds.removeFromTop(24);
    nameLabel.setBounds(row1.removeFromLeft(100));
    nameEditor.setBounds(row1.removeFromLeft(150));

    bounds.removeFromTop(10);
    auto rowRole = bounds.removeFromTop(24);
    roleLabel.setBounds(rowRole.removeFromLeft(100));
    roleEditor.setBounds(rowRole.removeFromLeft(200));

    bounds.removeFromTop(10);
    auto row2 = bounds.removeFromTop(24);

    // AUMENTATO LO SPAZIO DEGLI SLIDER DA 80 A 110 PER MOSTRARE IL SIMBOLO "+"
    startAddrLabel.setBounds(row2.removeFromLeft(90));
    startAddrSlider.setBounds(row2.removeFromLeft(110));

    row2.removeFromLeft(20); // Spaziatura centrale
    chCountLabel.setBounds(row2.removeFromLeft(100));
    chCountSlider.setBounds(row2.removeFromLeft(110));

    bounds.removeFromTop(20);
    mapTitleLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    // Impaginazione Mappatura a Griglia (2 Colonne)
    auto gridBounds = bounds.removeFromTop(160);
    int colWidth = gridBounds.getWidth() / 2;
    int rowHeight = 28;

    for (size_t i = 0; i < paramControls.size(); ++i)
    {
        int col = i % 2;
        int row = i / 2;
        auto cell = gridBounds.withTrimmedLeft(col * colWidth).withWidth(colWidth).withTrimmedTop(row * rowHeight).withHeight(rowHeight);

        paramControls[i]->label.setBounds(cell.removeFromLeft(100));
        paramControls[i]->slider.setBounds(cell.removeFromLeft(70));
    }
}

void FixtureEditorPanel::updateUIFromNode()
{
    if (!currentFixtureNode.isValid()) return;
    juce::ScopedValueSetter<bool> setter(isUpdatingUI, true);

    nameEditor.setText(currentFixtureNode.getProperty(StateKeys::FixtureName, "Nuova Macchina").toString(), juce::dontSendNotification);

    // LEGGIAMO IL RUOLO (Se non c'è, mostriamo "NESSUNO")
    roleEditor.setText(currentFixtureNode.getProperty(StateKeys::RoleName, "NESSUNO").toString(), juce::dontSendNotification);

    startAddrSlider.setValue(currentFixtureNode.getProperty(StateKeys::StartAddress, 1), juce::dontSendNotification);
    chCountSlider.setValue(currentFixtureNode.getProperty(StateKeys::ChannelCount, 1), juce::dontSendNotification);

    // Aggiorna gli slider di mappatura
    for (size_t i = 0; i < availableParams.size(); ++i)
    {
        auto key = StateKeys::getMappingKey(availableParams[i]);
        int val = currentFixtureNode.getProperty(key, 0); // 0 = Default non assegnato
        paramControls[i]->slider.setValue(val, juce::dontSendNotification);
    }
}

void FixtureEditorPanel::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&)
{
    updateUIFromNode();
}

// =============================================================================
// VenuePatchTab Implementation
// =============================================================================
VenuePatchTab::VenuePatchTab(juce::ValueTree& state)
    : appState(state), editorPanel(state)
{
    venueNode = appState.getChildWithName("VENUE");
    if (!venueNode.isValid())
    {
        venueNode = juce::ValueTree("VENUE");
        appState.addChild(venueNode, -1, nullptr);
    }
    venueNode.addListener(this);

    fixtureList.setModel(this);
    fixtureList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
    fixtureList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(fixtureList);

    // Configurazione Pulsanti
    addFixtureBtn.setButtonText("+ Aggiungi Macchina");
    addFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
    addFixtureBtn.onClick = [this] { addNewFixtureToState(); };
    addAndMakeVisible(addFixtureBtn);

    loadBtn.setButtonText("Carica Venue...");
    loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d29)); // Giallognolo scuro
    loadBtn.onClick = [this] { loadVenue(); };
    addAndMakeVisible(loadBtn);

    saveBtn.setButtonText("Salva Venue...");
    saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d)); // Verdino scuro
    saveBtn.onClick = [this] { saveVenue(); };
    addAndMakeVisible(saveBtn);

    addAndMakeVisible(editorPanel);

    if (venueNode.getNumChildren() > 0)
        fixtureList.selectRow(0);
}

VenuePatchTab::~VenuePatchTab()
{
    venueNode.removeListener(this);
}

int VenuePatchTab::getNumRows()
{
    return venueNode.getNumChildren();
}

void VenuePatchTab::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f));

    if (rowNumber < venueNode.getNumChildren())
    {
        auto fixNode = venueNode.getChild(rowNumber);
        auto name = fixNode.getProperty(StateKeys::FixtureName, "Senza Nome").toString();
        auto role = fixNode.getProperty(StateKeys::RoleName, "NESSUNO").toString(); // ESTRAI RUOLO

        // Calcolo del range DMX
        int startAddr = fixNode.getProperty(StateKeys::StartAddress, 1);
        int chCount = fixNode.getProperty(StateKeys::ChannelCount, 1);
        int endAddr = startAddr + chCount - 1; // Es. se parte da 9 ed ha 8 canali -> 9 + 8 - 1 = 16

        // Formattazione: [RUOLO] Nome Macchina (DMX: 1 - 8)
        juce::String labelText = "[" + role + "] " + name + " (DMX: " + juce::String(startAddr) + " - " + juce::String(endAddr) + ")";

        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(labelText, 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
}

void VenuePatchTab::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < venueNode.getNumChildren())
    {
        auto selectedNode = venueNode.getChild(lastRowSelected);
        editorPanel.setFixtureNode(selectedNode);
    }
    else
    {
        editorPanel.setFixtureNode(juce::ValueTree());
    }
}

void VenuePatchTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1e));

    g.setColour(juce::Colour(0xff2a2a32));
    float divX = getLocalBounds().proportionOfWidth(0.3f);
    g.fillRect(juce::Rectangle<float>(divX, 0.0f, 2.0f, static_cast<float>(getHeight())));
}

void VenuePatchTab::resized()
{
    auto bounds = getLocalBounds();

    auto leftPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.3f));

    // Spazio per 3 pulsanti in basso
    auto bottomArea = leftPanel.removeFromBottom(110).reduced(10, 5);

    saveBtn.setBounds(bottomArea.removeFromBottom(30));
    bottomArea.removeFromBottom(5);

    loadBtn.setBounds(bottomArea.removeFromBottom(30));
    bottomArea.removeFromBottom(5);

    addFixtureBtn.setBounds(bottomArea.removeFromBottom(30));

    fixtureList.setBounds(leftPanel.reduced(10));

    bounds.removeFromLeft(2);
    editorPanel.setBounds(bounds);
}

void VenuePatchTab::addNewFixtureToState()
{
    juce::ValueTree newFix(StateKeys::Fixture);
    int newIndex = venueNode.getNumChildren() + 1;
    newFix.setProperty(StateKeys::FixtureName, "Nuova Macchina " + juce::String(newIndex), nullptr);
    newFix.setProperty(StateKeys::StartAddress, (newIndex - 1) * 10 + 1, nullptr);
    newFix.setProperty(StateKeys::ChannelCount, 8, nullptr); // Default: 8 Canali
    newFix.setProperty(StateKeys::RoleName, "WASH_BACK", nullptr);

    // Assegnazione di base per comodità (es. Wash classico cinese 7 canali)
    newFix.setProperty(StateKeys::getMappingKey("Dimmer"), 1, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Red"), 2, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Green"), 3, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Blue"), 4, nullptr);

    venueNode.addChild(newFix, -1, nullptr);
    fixtureList.updateContent();
    fixtureList.selectRow(venueNode.getNumChildren() - 1);
}

// =============================================================================
// Gestione File
// =============================================================================
void VenuePatchTab::saveVenue()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Salva Configurazione Venue",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NuovoLocale.venue"),
        "*.venue"
    );

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            PresetManager::saveVenue(appState, file);
        }
        });
}

void VenuePatchTab::loadVenue()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Carica Configurazione Venue",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.venue"
    );

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            PresetManager::loadVenue(appState, file);
            fixtureList.updateContent();

            if (venueNode.getNumChildren() > 0)
                fixtureList.selectRow(0);
        }
        });
}

void VenuePatchTab::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) { fixtureList.updateContent(); }
void VenuePatchTab::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int)
{
    fixtureList.updateContent();
    editorPanel.setFixtureNode(juce::ValueTree());
}
void VenuePatchTab::valueTreeChildOrderChanged(juce::ValueTree&, int, int) { fixtureList.updateContent(); }
void VenuePatchTab::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) { fixtureList.repaint(); }