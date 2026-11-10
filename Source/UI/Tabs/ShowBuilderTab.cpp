#include "ShowBuilderTab.h"

// =============================================================================
// SceneEditorPanel Implementation
// =============================================================================
SceneEditorPanel::SceneEditorPanel(juce::ValueTree& state) : appState(state)
{
    sceneTitleLabel.setText("Proprieta' Scena", juce::dontSendNotification);
    sceneTitleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    sceneTitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff));
    addAndMakeVisible(sceneTitleLabel);

    // Setup Scena
    nameLabel.setText("Nome Scena:", juce::dontSendNotification);
    nameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(nameLabel);
    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a32));
    nameEditor.onTextChange = [this] { if (currentSceneNode.isValid() && !isUpdatingUI) currentSceneNode.setProperty(StateKeys::SceneName, nameEditor.getText(), nullptr); };
    addAndMakeVisible(nameEditor);

    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& text, double min, double max) {
        l.setText(text, juce::dontSendNotification); l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(l);
        s.setSliderStyle(juce::Slider::IncDecButtons); s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
        s.setRange(min, max, 1.0); addAndMakeVisible(s);
        };

    setupSlider(startBarSlider, startBarLabel, "Inizio (Quarti):", 1.0, 9999.0);
    startBarSlider.setRange(1.0, 9999.0, 0.25); // Step di un sedicesimo (0.25)
    startBarSlider.onValueChange = [this] {
        if (currentSceneNode.isValid() && !isUpdatingUI)
            currentSceneNode.setProperty(StateKeys::StartBeat, startBarSlider.getValue(), nullptr);
        };

    setupSlider(durationSlider, durationLabel, "Durata (Quarti):", 0.25, 999.0);
    durationSlider.setRange(0.25, 999.0, 0.25); // Permette anche durate frazionate
    durationSlider.onValueChange = [this] {
        if (currentSceneNode.isValid() && !isUpdatingUI)
            currentSceneNode.setProperty(StateKeys::DurationBeats, durationSlider.getValue(), nullptr);
        };

    // Lista Comandi
    commandList.setModel(this);
    commandList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
    commandList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(commandList);

    addCmdBtn.setButtonText("+ Aggiungi Comando"); addCmdBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
    addCmdBtn.onClick = [this] { addNewCommand(); }; addAndMakeVisible(addCmdBtn);

    removeCmdBtn.setButtonText("- Rimuovi Selezionato"); removeCmdBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5c2626));
    removeCmdBtn.onClick = [this] {
        if (currentCommandNode.isValid() && currentSceneNode.isValid()) {
            currentSceneNode.removeChild(currentCommandNode, nullptr);
        }
        }; addAndMakeVisible(removeCmdBtn);

    // Editor Comando
    cmdTitleLabel.setText("Dettaglio Comando", juce::dontSendNotification);
    cmdTitleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    cmdTitleLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(cmdTitleLabel);

    roleLabel.setText("Ruolo Target:", juce::dontSendNotification); roleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); addAndMakeVisible(roleLabel);
    roleSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
    roleSelector.onChange = [this] { if (currentCommandNode.isValid() && !isUpdatingUI) currentCommandNode.setProperty(StateKeys::TargetRole, roleSelector.getText(), nullptr); };
    addAndMakeVisible(roleSelector);

    paramLabel.setText("Parametro:", juce::dontSendNotification); paramLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); addAndMakeVisible(paramLabel);
    paramSelector.addItemList({ "Dimmer", "Pan", "Tilt", "Red", "Green", "Blue", "White", "UV", "Strobe", "GeyserBlast" }, 1);
    paramSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
    paramSelector.onChange = [this] { if (currentCommandNode.isValid() && !isUpdatingUI) currentCommandNode.setProperty(StateKeys::TargetParam, paramSelector.getText(), nullptr); };
    addAndMakeVisible(paramSelector);

    curveLabel.setText("Azione/Curva:", juce::dontSendNotification); curveLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); addAndMakeVisible(curveLabel);
    curveSelector.addItemList({ "Step (Stato Fisso)", "LinearRamp (Transizione)", "Sine (LFO/Onda)" }, 1);
    curveSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
    curveSelector.onChange = [this] { if (currentCommandNode.isValid() && !isUpdatingUI) currentCommandNode.setProperty(StateKeys::CurveType, curveSelector.getText(), nullptr); };
    addAndMakeVisible(curveSelector);

    setupSlider(startValSlider, startValLabel, "Valore Start (0-127):", 0.0, 127.0);
    startValSlider.onValueChange = [this] { if (currentCommandNode.isValid() && !isUpdatingUI) currentCommandNode.setProperty(StateKeys::StartValue, static_cast<int>(startValSlider.getValue()), nullptr); };

    setupSlider(endValSlider, endValLabel, "Valore End (0-127):", 0.0, 127.0);
    endValSlider.onValueChange = [this] { if (currentCommandNode.isValid() && !isUpdatingUI) currentCommandNode.setProperty(StateKeys::EndValue, static_cast<int>(endValSlider.getValue()), nullptr); };

    auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
    if (venueNode.isValid()) {
        venueNode.addListener(this);
        for (auto fix : venueNode) fix.addListener(this); // ASCOLTA LE MACCHINE ESISTENTI
    }

    refreshRoleList();
    setVisible(false);
}

SceneEditorPanel::~SceneEditorPanel() {
    if (currentSceneNode.isValid()) currentSceneNode.removeListener(this);
    appState.getChildWithName(StateKeys::VenueNode).removeListener(this);
}

void SceneEditorPanel::refreshRoleList()
{
    juce::String currentText = roleSelector.getText();
    roleSelector.clear();

    auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
    juce::StringArray roles;

    if (venueNode.isValid())
    {
        for (auto fix : venueNode)
        {
            // Assicuriamoci che il pannello ascolti anche i cambi sulle singole macchine
            fix.removeListener(this);
            fix.addListener(this);

            juce::String r = fix.getProperty(StateKeys::RoleName).toString().trim();
            if (r.isNotEmpty() && r != "NESSUNO")
                roles.addIfNotAlreadyThere(r);
        }
    }

    // Se stiamo modificando un comando con un ruolo assegnato, mostralo comunque in lista
    if (currentCommandNode.isValid())
    {
        juce::String cmdRole = currentCommandNode.getProperty(StateKeys::TargetRole).toString().trim();
        if (cmdRole.isNotEmpty())
            roles.addIfNotAlreadyThere(cmdRole);
    }

    // Popola la ComboBox
    for (int i = 0; i < roles.size(); ++i)
        roleSelector.addItem(roles[i], i + 1);

    // Ripristina il valore precedente o imposta il primo disponibile
    if (currentText.isNotEmpty() && roles.contains(currentText))
        roleSelector.setText(currentText, juce::dontSendNotification);
    else if (roles.size() > 0)
        roleSelector.setSelectedId(1, juce::dontSendNotification);
}
void SceneEditorPanel::setSceneNode(juce::ValueTree sceneNode) {
    if (currentSceneNode.isValid()) currentSceneNode.removeListener(this);
    currentSceneNode = sceneNode;
    if (currentSceneNode.isValid()) {
        currentSceneNode.addListener(this);
        updateSceneUI();
        commandList.updateContent();
        if (currentSceneNode.getNumChildren() > 0) commandList.selectRow(0);
        else currentCommandNode = juce::ValueTree();
        setVisible(true);
    }
    else {
        setVisible(false);
    }
}

void SceneEditorPanel::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff2a2a32));
    g.fillRect(10, 175, getWidth() - 20, 2); // Separatore
}

void SceneEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(15);

    // 1. Info Scena
    sceneTitleLabel.setBounds(bounds.removeFromTop(30));
    auto r1 = bounds.removeFromTop(28);
    nameLabel.setBounds(r1.removeFromLeft(90)); nameEditor.setBounds(r1.removeFromLeft(200));
    auto r2 = bounds.removeFromTop(28);
    startBarLabel.setBounds(r2.removeFromLeft(100)); startBarSlider.setBounds(r2.removeFromLeft(90));
    r2.removeFromLeft(20);
    durationLabel.setBounds(r2.removeFromLeft(120)); durationSlider.setBounds(r2.removeFromLeft(90));

    bounds.removeFromTop(20);

    // 2. Lista Comandi
    auto listArea = bounds.removeFromTop(120);
    auto btnArea = listArea.removeFromRight(150);
    btnArea.removeFromLeft(10);
    addCmdBtn.setBounds(btnArea.removeFromTop(30)); btnArea.removeFromTop(10);
    removeCmdBtn.setBounds(btnArea.removeFromTop(30));
    commandList.setBounds(listArea);

    bounds.removeFromTop(20);

    // 3. Editor Comando
    cmdTitleLabel.setBounds(bounds.removeFromTop(30));

    auto cr1 = bounds.removeFromTop(28);
    roleLabel.setBounds(cr1.removeFromLeft(90)); roleSelector.setBounds(cr1.removeFromLeft(150));
    cr1.removeFromLeft(20);
    paramLabel.setBounds(cr1.removeFromLeft(80)); paramSelector.setBounds(cr1.removeFromLeft(150));

    auto cr2 = bounds.removeFromTop(28);
    curveLabel.setBounds(cr2.removeFromLeft(90)); curveSelector.setBounds(cr2.removeFromLeft(180));

    auto cr3 = bounds.removeFromTop(28);
    startValLabel.setBounds(cr3.removeFromLeft(140)); startValSlider.setBounds(cr3.removeFromLeft(80));
    cr3.removeFromLeft(20);
    endValLabel.setBounds(cr3.removeFromLeft(140)); endValSlider.setBounds(cr3.removeFromLeft(80));

    // Nascondi i controlli del comando se non ce n'è uno selezionato
    bool cmdValid = currentCommandNode.isValid();
    cmdTitleLabel.setVisible(cmdValid); roleLabel.setVisible(cmdValid); roleSelector.setVisible(cmdValid);
    paramLabel.setVisible(cmdValid); paramSelector.setVisible(cmdValid); curveLabel.setVisible(cmdValid); curveSelector.setVisible(cmdValid);
    startValLabel.setVisible(cmdValid); startValSlider.setVisible(cmdValid); endValLabel.setVisible(cmdValid); endValSlider.setVisible(cmdValid);
}

void SceneEditorPanel::updateSceneUI() {
    if (!currentSceneNode.isValid()) return;
    juce::ScopedValueSetter<bool> setter(isUpdatingUI, true);
    nameEditor.setText(currentSceneNode.getProperty(StateKeys::SceneName, "Nuova Scena").toString(), juce::dontSendNotification);
    startBarSlider.setValue(currentSceneNode.getProperty(StateKeys::StartBeat, 1.0), juce::dontSendNotification);
    durationSlider.setValue(currentSceneNode.getProperty(StateKeys::DurationBeats, 4.0), juce::dontSendNotification);
}

void SceneEditorPanel::updateCommandUI() {
    if (!currentCommandNode.isValid()) { resized(); return; }
    juce::ScopedValueSetter<bool> setter(isUpdatingUI, true);

    refreshRoleList();

    roleSelector.setText(currentCommandNode.getProperty(StateKeys::TargetRole, "").toString(), juce::dontSendNotification);
    paramSelector.setText(currentCommandNode.getProperty(StateKeys::TargetParam, "Dimmer").toString(), juce::dontSendNotification);
    curveSelector.setText(currentCommandNode.getProperty(StateKeys::CurveType, "Step (Stato Fisso)").toString(), juce::dontSendNotification);
    startValSlider.setValue(currentCommandNode.getProperty(StateKeys::StartValue, 0), juce::dontSendNotification);
    endValSlider.setValue(currentCommandNode.getProperty(StateKeys::EndValue, 127), juce::dontSendNotification);
    resized(); // Ridisegna per mostrare i controlli
}

void SceneEditorPanel::addNewCommand() {
    if (!currentSceneNode.isValid()) return;
    juce::ValueTree newCmd(StateKeys::RoleCommand);
    juce::String defRole = roleSelector.getNumItems() > 0 ? roleSelector.getItemText(0) : "WASH_BACK";
    newCmd.setProperty(StateKeys::TargetRole, defRole, nullptr);
    newCmd.setProperty(StateKeys::TargetParam, "Dimmer", nullptr);
    newCmd.setProperty(StateKeys::StartValue, 127, nullptr);
    newCmd.setProperty(StateKeys::EndValue, 127, nullptr);
    newCmd.setProperty(StateKeys::CurveType, "Step (Stato Fisso)", nullptr);
    currentSceneNode.addChild(newCmd, -1, nullptr);
    commandList.updateContent();
    commandList.selectRow(currentSceneNode.getNumChildren() - 1);
}

int SceneEditorPanel::getNumRows() { return currentSceneNode.isValid() ? currentSceneNode.getNumChildren() : 0; }

void SceneEditorPanel::selectedRowsChanged(int lastRow) {
    if (lastRow >= 0 && lastRow < currentSceneNode.getNumChildren()) currentCommandNode = currentSceneNode.getChild(lastRow);
    else currentCommandNode = juce::ValueTree();
    updateCommandUI();
}

void SceneEditorPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (selected)
        g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f));

    if (row < currentSceneNode.getNumChildren()) {
        auto cmd = currentSceneNode.getChild(row);
        juce::String targetRole = cmd.getProperty(StateKeys::TargetRole, "?").toString();
        juce::String param = cmd.getProperty(StateKeys::TargetParam, "?").toString();
        int startVal = cmd.getProperty(StateKeys::StartValue, 0);
        int endVal = cmd.getProperty(StateKeys::EndValue, 127);
        juce::String curve = cmd.getProperty(StateKeys::CurveType, "Step").toString();

        // Formato pulito: WASH_BACK -> Dimmer [Step] (0 -> 127)
        juce::String text = targetRole + " -> " + param + " [" + curve.upToFirstOccurrenceOf(" ", false, false) + "] (" + juce::String(startVal) + " to " + juce::String(endVal) + ")";

        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(text, 10, 0, w - 20, h, juce::Justification::centredLeft, true);
    }
}

void SceneEditorPanel::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& i) {
    if (tree == currentSceneNode) updateSceneUI();
    else if (tree == currentCommandNode) updateCommandUI();

    // Il cuore del fix: se modifichi il testo "Ruolo" in una macchina nel Tab 1, aggiorna le tendine!
    if (tree.hasType(StateKeys::Fixture) && i == StateKeys::RoleName) refreshRoleList();

    if (tree.hasType(StateKeys::RoleCommand)) commandList.repaint();
}

void SceneEditorPanel::valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree& child) {
    if (t == currentSceneNode) {
        commandList.updateContent();
    }
    else if (t.hasType(StateKeys::VenueNode)) {
        child.addListener(this); // Mettiti in ascolto della nuova macchina
        refreshRoleList();
    }
}

void SceneEditorPanel::valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree& child, int) {
    if (t == currentSceneNode) {
        commandList.updateContent();
        currentCommandNode = juce::ValueTree();
        updateCommandUI();
    }
    else if (t.hasType(StateKeys::VenueNode)) {
        child.removeListener(this); // Rimuovi l'ascolto se cancelli la macchina
        refreshRoleList();
    }
}

void SceneEditorPanel::visibilityChanged()
{
    if (isVisible())
        refreshRoleList();
}

// =============================================================================
// ShowBuilderTab Implementation (Main)
// =============================================================================
ShowBuilderTab::ShowBuilderTab(juce::ValueTree& state) : appState(state), editorPanel(state)
{
    showNode = appState.getChildWithName(StateKeys::ShowNode);
    if (!showNode.isValid()) { showNode = juce::ValueTree(StateKeys::ShowNode); appState.addChild(showNode, -1, nullptr); }
    showNode.addListener(this);

    sceneList.setModel(this);
    sceneList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
    sceneList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(sceneList);

    addSceneBtn.setButtonText("+ Nuova Scena (Cue)"); addSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
    addSceneBtn.onClick = [this]() { addNewSceneToState(); }; addAndMakeVisible(addSceneBtn);

    loadBtn.setButtonText("Carica Show..."); loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d29));
    loadBtn.onClick = [this] { loadShow(); }; addAndMakeVisible(loadBtn);

    saveBtn.setButtonText("Salva Show..."); saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d));
    saveBtn.onClick = [this] { saveShow(); }; addAndMakeVisible(saveBtn);

    addAndMakeVisible(editorPanel);
    if (showNode.getNumChildren() > 0) sceneList.selectRow(0);
}

ShowBuilderTab::~ShowBuilderTab() { showNode.removeListener(this); }

int ShowBuilderTab::getNumRows() { return showNode.getNumChildren(); }

void ShowBuilderTab::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (selected) g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f));
    if (row < showNode.getNumChildren()) {
        auto scene = showNode.getChild(row);
        juce::String name = scene.getProperty(StateKeys::SceneName, "?").toString();
        double beat = scene.getProperty(StateKeys::StartBeat, 1.0);
        g.setColour(juce::Colours::white); g.setFont(14.0f);
        g.drawText("[Beat " + juce::String(beat, 2) + "] " + name, 10, 0, w - 20, h, juce::Justification::centredLeft, true);
    }
}
void ShowBuilderTab::selectedRowsChanged(int lastRow) {
    if (lastRow >= 0 && lastRow < showNode.getNumChildren()) editorPanel.setSceneNode(showNode.getChild(lastRow));
    else editorPanel.setSceneNode(juce::ValueTree());
}
void ShowBuilderTab::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1e)); g.setColour(juce::Colour(0xff2a2a32));
    g.fillRect(juce::Rectangle<float>(getLocalBounds().proportionOfWidth(0.30f), 0.0f, 2.0f, static_cast<float>(getHeight())));
}
void ShowBuilderTab::resized() {
    auto bounds = getLocalBounds();
    auto leftPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.30f));
    auto bottomArea = leftPanel.removeFromBottom(110).reduced(10, 5);
    saveBtn.setBounds(bottomArea.removeFromBottom(30)); bottomArea.removeFromBottom(5);
    loadBtn.setBounds(bottomArea.removeFromBottom(30)); bottomArea.removeFromBottom(5);
    addSceneBtn.setBounds(bottomArea.removeFromBottom(30));
    sceneList.setBounds(leftPanel.reduced(10));
    bounds.removeFromLeft(2); editorPanel.setBounds(bounds);
}
void ShowBuilderTab::addNewSceneToState() {
    juce::ValueTree newScene(StateKeys::Scene);
    newScene.setProperty(StateKeys::SceneName, "Scena " + juce::String(showNode.getNumChildren() + 1), nullptr);
    newScene.setProperty(StateKeys::StartBeat, 1.0, nullptr);
    newScene.setProperty(StateKeys::DurationBeats, 4.0, nullptr);
    showNode.addChild(newScene, -1, nullptr);
    sceneList.updateContent(); sceneList.selectRow(showNode.getNumChildren() - 1);
}

void ShowBuilderTab::saveShow() {
    fileChooser = std::make_unique<juce::FileChooser>("Salva Show", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NuovoBrano.show"), "*.show");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File{}) PresetManager::saveShow(appState, fc.getResult());
        });
}
void ShowBuilderTab::loadShow() {
    fileChooser = std::make_unique<juce::FileChooser>("Carica Show", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.show");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File{}) { PresetManager::loadShow(appState, fc.getResult()); sceneList.updateContent(); if (showNode.getNumChildren() > 0) sceneList.selectRow(0); }
        });
}

void ShowBuilderTab::valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree&) { if (t == showNode) sceneList.updateContent(); }
void ShowBuilderTab::valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree&, int) { if (t == showNode) { sceneList.updateContent(); editorPanel.setSceneNode(juce::ValueTree()); } }
void ShowBuilderTab::valueTreeChildOrderChanged(juce::ValueTree&, int, int) { sceneList.updateContent(); }
void ShowBuilderTab::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) { if (tree.hasType(StateKeys::Scene)) sceneList.repaint(); }