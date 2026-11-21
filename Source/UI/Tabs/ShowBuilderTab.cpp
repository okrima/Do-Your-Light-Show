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

    curveLabel.setText("Azione/Curva:", juce::dontSendNotification); 
    curveLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey); 
    addAndMakeVisible(curveLabel);
    curveSelector.addItem("Static (Fisso)", 1);
    curveSelector.addItem("Linear (Rampa sulla durata)", 2);
    curveSelector.addItem("Decay (Decadimento)", 3);
    curveSelector.addItem("Sine 1 Bar", 4);
    curveSelector.addItem("Sine 2 Bars", 5);
    curveSelector.addItem("Sine 4 Bars", 6);
    curveSelector.addItem("Sine Durata (1 Ciclo)", 7);
    curveSelector.addItem("Square 1/8 (Ottavi)", 8);
    curveSelector.addItem("Square 1/8T (Terzine)", 9);
    curveSelector.addItem("Square 1/16 (Sedicesimi)", 10);
    curveSelector.addItem("Square 1/16T (Sestine)", 11);
    curveSelector.addItem("Step (Cambio Finale)", 12);
    curveSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
    curveSelector.onChange = [this] {
        if (currentCommandNode.isValid() && !isUpdatingUI)
        {
            static const char* keys[12] = {
                "Static", "Linear", "Decay", "Sine_1Bar", "Sine_2Bars", "Sine_4Bars",
                "Sine_Duration", "Square_1_8", "Square_1_8T", "Square_1_16", "Square_1_16T", "Step"
            };
            int idx = juce::jlimit(0, 11, curveSelector.getSelectedId() - 1);
            currentCommandNode.setProperty(StateKeys::CurveType, keys[idx], nullptr);
        }
        };
    addAndMakeVisible(curveSelector);

    setupSlider(startValSlider, startValLabel, "Valore Start (0-255):", 0.0, 255.0);
    startValSlider.onValueChange = [this] {
        if (currentCommandNode.isValid() && !isUpdatingUI)
            currentCommandNode.setProperty(StateKeys::StartValue, static_cast<int>(startValSlider.getValue()), nullptr);
        };

    setupSlider(endValSlider, endValLabel, "Valore End (0-255):", 0.0, 255.0);
    endValSlider.onValueChange = [this] {
        if (currentCommandNode.isValid() && !isUpdatingUI)
            currentCommandNode.setProperty(StateKeys::EndValue, static_cast<int>(endValSlider.getValue()), nullptr);
        };

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
    juce::String c = currentCommandNode.getProperty(StateKeys::CurveType, "Static").toString();
    if (c.startsWithIgnoreCase("Linear"))              curveSelector.setSelectedId(2, juce::dontSendNotification);
    else if (c.equalsIgnoreCase("Decay"))              curveSelector.setSelectedId(3, juce::dontSendNotification);
    else if (c.containsIgnoreCase("1Bar"))             curveSelector.setSelectedId(4, juce::dontSendNotification);
    else if (c.containsIgnoreCase("2Bars"))            curveSelector.setSelectedId(5, juce::dontSendNotification);
    else if (c.containsIgnoreCase("4Bars"))            curveSelector.setSelectedId(6, juce::dontSendNotification);
    else if (c.containsIgnoreCase("Duration"))         curveSelector.setSelectedId(7, juce::dontSendNotification);
    else if (c.equalsIgnoreCase("Square_1_8T"))        curveSelector.setSelectedId(9, juce::dontSendNotification);
    else if (c.equalsIgnoreCase("Square_1_8"))         curveSelector.setSelectedId(8, juce::dontSendNotification);
    else if (c.equalsIgnoreCase("Square_1_16T"))       curveSelector.setSelectedId(11, juce::dontSendNotification);
    else if (c.startsWithIgnoreCase("Square"))         curveSelector.setSelectedId(10, juce::dontSendNotification);
    else if (c.startsWithIgnoreCase("Step"))           curveSelector.setSelectedId(12, juce::dontSendNotification);
    else                                               curveSelector.setSelectedId(1, juce::dontSendNotification);

    startValSlider.setValue(currentCommandNode.getProperty(StateKeys::StartValue, 0), juce::dontSendNotification);
    endValSlider.setValue(currentCommandNode.getProperty(StateKeys::EndValue, 255), juce::dontSendNotification);
    resized(); // Ridisegna per mostrare i controlli
}

void SceneEditorPanel::addNewCommand() {
    if (!currentSceneNode.isValid()) return;
    juce::ValueTree newCmd(StateKeys::RoleCommand);
    juce::String defRole = roleSelector.getNumItems() > 0 ? roleSelector.getItemText(0) : "WASH_BACK";
    newCmd.setProperty(StateKeys::TargetRole, defRole, nullptr);
    newCmd.setProperty(StateKeys::TargetParam, "Dimmer", nullptr);
    newCmd.setProperty(StateKeys::StartValue, 255, nullptr);
    newCmd.setProperty(StateKeys::EndValue, 255, nullptr);
    newCmd.setProperty(StateKeys::CurveType, "Static", nullptr);
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

void SceneEditorPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f));

    if (row >= 0 && row < currentSceneNode.getNumChildren())
    {
        auto cmd = currentSceneNode.getChild(row);
        juce::String targetRole = cmd.getProperty(StateKeys::TargetRole, "?").toString();
        juce::String param = cmd.getProperty(StateKeys::TargetParam, "?").toString();
        int startVal = static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0));
        int endVal = static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255));
        juce::String curve = cmd.getProperty(StateKeys::CurveType, "Static").toString();

        juce::String text = targetRole + " -> " + param + " [" + curve.upToFirstOccurrenceOf(" ", false, false)
            + "] (" + juce::String(startVal) + " to " + juce::String(endVal) + ")";

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f));
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
ShowBuilderTab::ShowBuilderTab(juce::ValueTree& state) : appState(state), miniDawTimeline(state, audioEngine, beatTimeMap), editorPanel(state), aiWizardPanel(state, audioEngine), stageSimulator(state, audioEngine, beatTimeMap)
{

    // 1. Garantisce che AUDIO_STEMS, AESTHETIC_CONFIG, BEAT_MAP, VENUE, SHOW e PRESET esistano
    ShowArchiveManager::ensureValidAppStateStructure(appState);

    // 2. Aggancia il convertitore Tempo <-> Quarti allo stato dell'app
    beatTimeMap.bindToAppState(appState);

    setWantsKeyboardFocus(true);
    addKeyListener(this);                // Ascolta la tastiera sul pannello generale
    miniDawTimeline.addKeyListener(this); // Ascolta la tastiera quando clicchi sulla forma d'onda
    sceneList.addKeyListener(this);      // Ascolta la tastiera quando sei sulla lista scene

    stageSimulator.onToggleExpandRequested = [this]() {
        resized();
        repaint();
        };
    addAndMakeVisible(stageSimulator);

    // Collega il click sulle scene della timeline alla selezione nella lista
    miniDawTimeline.onSceneSelectedFromTimeline = [this](int sceneIndex) {
        sceneList.selectRow(sceneIndex);
        };

    miniDawTimeline.onSceneDragFinished = [this](juce::ValueTree editedScene) {
        sortScenesByStartBeat(editedScene);
        };
    addAndMakeVisible(miniDawTimeline);
    miniDawTimeline.syncWithAppState();

    // Sincronizza la forma d'onda se il Full Mix viene caricato o trascinato dagli slot Stem
    aiWizardPanel.onFullMixUpdated = [this]() {
        miniDawTimeline.syncWithAppState();
        };

    aiWizardPanel.onGenerateShowRequested = [this]() {
        audioEngine.pause();

        analysisJob = std::make_unique<AudioAnalysisEngine::AnalysisJob>(
            appState,
            audioEngine,
            [this](const AudioAnalysisEngine::AnalysisResult& analysis) {
                if (!analysis.success)
                {
                    juce::NativeMessageBox::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Errore Analisi",
                        "Impossibile completare l'analisi del brano.");
                    return;
                }

                // 1. Applica la griglia dei quarti e il BPM rilevato
                AudioAnalysisEngine::applyResultToState(analysis, appState, beatTimeMap);

                // 2. Genera automaticamente le SCENE e i ROLE_COMMAND (o applica il Layering)
                auto genStats = AIShowGenerator::generateShowFromAnalysis(appState, analysis);

                // 3. Aggiorna la UI (Mini-DAW, Lista Scene e seleziona la prima scena)
                miniDawTimeline.syncWithAppState();
                sortScenesByStartBeat();

                if (showNode.getNumChildren() > 0)
                    sceneList.selectRow(0);
                repaint();

                juce::String modeStr = genStats.wasIncrementalLayering
                    ? "Layering Incrementale (preservate scene esistenti)"
                    : "Generazione Completa Show";

                juce::String report = "Modalita': " + modeStr + "\n"
                    + "BPM Rilevato: " + juce::String(analysis.detectedBpm, 1) + "\n"
                    + "Scene generate/aggiornate: " + juce::String(genStats.scenesCreatedOrUpdated) + "\n"
                    + "Comandi luminosi creati: " + juce::String(genStats.commandsGenerated);

                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Show Generato con Successo",
                    report);
            });

        analysisJob->launchThread();
        };

    addAndMakeVisible(aiWizardPanel);

    showNode = appState.getChildWithName(StateKeys::ShowNode);
    if (!showNode.isValid()) {
        showNode = juce::ValueTree(StateKeys::ShowNode);
        appState.addChild(showNode, -1, nullptr);
    }
    showNode.addListener(this);

    // INIZIALIZZAZIONE LIBRERIA PRESET
    presetLibNode = appState.getChildWithName(StateKeys::PresetLibNode);
    if (!presetLibNode.isValid()) {
        presetLibNode = juce::ValueTree(StateKeys::PresetLibNode);
        appState.addChild(presetLibNode, -1, nullptr);
    }
    presetLibNode.addListener(this);

    // -> CARICA I PRESET SALVATI IN PRECEDENZA DAL DISCO
    if (presetLibNode.getNumChildren() == 0)
        loadPresetLibraryFromDisk();

    sceneList.setModel(this);
    sceneList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
    sceneList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
    sceneList.addKeyListener(this);
    addAndMakeVisible(sceneList);

    addSceneBtn.setButtonText("+ Nuova Scena"); addSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
    addSceneBtn.onClick = [this]() { addNewSceneToState(); }; addAndMakeVisible(addSceneBtn);

    // Configurazione del bottone Elimina Scena
    deleteSceneBtn.setButtonText("Elimina Scena...");
    deleteSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4d2626)); // Rosso scuro
    deleteSceneBtn.onClick = [this]() { deleteSelectedScene(); };
    addAndMakeVisible(deleteSceneBtn);

    // Configurazione Bottone Salva Preset
    savePresetBtn.setButtonText("Salva come Preset..."); savePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3229));
    savePresetBtn.onClick = [this]() { saveSceneAsPreset(); }; addAndMakeVisible(savePresetBtn);

    // Configurazione Bottone Inserisci Preset
    insertPresetBtn.setButtonText("Inserisci da Preset..."); insertPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff29333d));
    insertPresetBtn.onClick = [this]() { insertPresetIntoShow(); }; addAndMakeVisible(insertPresetBtn);

    loadBtn.setButtonText("Carica Show..."); loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d29));
    loadBtn.onClick = [this] { loadShow(); }; addAndMakeVisible(loadBtn);

    saveBtn.setButtonText("Salva Show..."); saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d));
    saveBtn.onClick = [this] { saveShow(); }; addAndMakeVisible(saveBtn);

    // Configurazione del pulsante Elimina Preset
    deletePresetBtnUI.setButtonText("Elimina Preset...");
    deletePresetBtnUI.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4d3326));
    deletePresetBtnUI.onClick = [this]() { deletePresetFromLibrary(); };
    addAndMakeVisible(deletePresetBtnUI);

    addAndMakeVisible(editorPanel);
    if (showNode.getNumChildren() > 0) sceneList.selectRow(0);
}

ShowBuilderTab::~ShowBuilderTab() { 
    analysisJob.reset();
    showNode.removeListener(this); 
}

void ShowBuilderTab::saveSceneAsPreset()
{
    int selectedRow = sceneList.getSelectedRow();
    if (selectedRow < 0 || selectedRow >= showNode.getNumChildren()) return;

    auto sourceScene = showNode.getChild(selectedRow);
    if (!sourceScene.isValid()) return;

    juce::String sceneName = sourceScene.getProperty(StateKeys::SceneName, "Scena").toString().trim();
    if (sceneName.isEmpty())
        sceneName = "Scena";

    juce::ValueTree presetCopy(StateKeys::ScenePreset);

    for (int i = 0; i < sourceScene.getNumProperties(); ++i)
    {
        auto propName = sourceScene.getPropertyName(i);
        if (propName.isValid() && propName != StateKeys::StartBeat)
            presetCopy.setProperty(propName, sourceScene.getProperty(propName), nullptr);
    }

    // Allinea le proprieta' lette dalla Tab 2 (Scene Builder) e dal menu Inserisci Preset
    presetCopy.setProperty(StateKeys::PresetName, sceneName, nullptr);
    presetCopy.setProperty(StateKeys::SceneName, sceneName, nullptr);
    if (!presetCopy.hasProperty(StateKeys::PresetPool))
        presetCopy.setProperty(StateKeys::PresetPool, "Color", nullptr);
    if (!presetCopy.hasProperty(StateKeys::LayerType))
        presetCopy.setProperty(StateKeys::LayerType, "Base", nullptr);

    for (int i = 0; i < sourceScene.getNumChildren(); ++i)
    {
        auto child = sourceScene.getChild(i);
        if (child.isValid())
            presetCopy.addChild(child.createCopy(), -1, nullptr);
    }

    if (presetLibNode.isValid())
    {
        presetLibNode.addChild(presetCopy, -1, nullptr);
        savePresetLibraryToDisk();
    }

    juce::NativeMessageBox::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon,
        "Preset Salvato",
        "La scena '" + sceneName + "' e' stata salvata nei preferiti in modo permanente!"
    );
}

void ShowBuilderTab::insertPresetIntoShow()
{
    if (!presetLibNode.isValid() || presetLibNode.getNumChildren() == 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Libreria Vuota",
            "Non ci sono preset disponibili. Creane uno nella Tab 2 (Scene Builder)!");
        return;
    }

    const juce::StringArray pools = { "Color", "Dimmer", "Strobe", "Movement", "ImpactFX" };
    juce::PopupMenu menu;

    for (const auto& poolName : pools)
    {
        juce::PopupMenu sub;
        int count = 0;
        for (int i = 0; i < presetLibNode.getNumChildren(); ++i)
        {
            auto preset = presetLibNode.getChild(i);
            if (!preset.isValid()) continue;

            juce::String pPool = preset.getProperty(StateKeys::PresetPool, "Color").toString();
            if (pPool != poolName) continue;

            juce::String name = preset.getProperty(StateKeys::PresetName, "").toString();
            if (name.isEmpty())
                name = preset.getProperty(StateKeys::SceneName, "Preset").toString();

            double dur = static_cast<double>(preset.getProperty(StateKeys::DurationBeats, 4.0));
            sub.addItem(i + 1, name + "  [" + juce::String(dur, 1) + " Q]");
            ++count;
        }
        if (count > 0)
            menu.addSubMenu(poolName, sub);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&insertPresetBtn), [this](int result) {
        if (result <= 0) return;

        auto chosenPreset = presetLibNode.getChild(result - 1);
        if (!chosenPreset.isValid()) return;

        juce::ValueTree newSceneInstance(StateKeys::Scene);
        for (int i = 0; i < chosenPreset.getNumProperties(); ++i)
        {
            auto propName = chosenPreset.getPropertyName(i);
            if (propName.isValid())
                newSceneInstance.setProperty(propName, chosenPreset.getProperty(propName), nullptr);
        }

        // Assicura che SceneName sia popolato dal PresetName dello Scene Builder
        juce::String pName = chosenPreset.getProperty(StateKeys::PresetName, "").toString();
        if (pName.isNotEmpty())
            newSceneInstance.setProperty(StateKeys::SceneName, pName, nullptr);

        for (int i = 0; i < chosenPreset.getNumChildren(); ++i)
        {
            auto child = chosenPreset.getChild(i);
            if (child.isValid())
                newSceneInstance.addChild(child.createCopy(), -1, nullptr);
        }

        // Posizionamento: se il playhead e' oltre 0s usa la posizione corrente (snappata a 0.5 Q),
        // altrimenti accoda subito dopo l'ultima scena
        double nextBeat = 1.0;
        double playSec = audioEngine.getCurrentPositionSeconds();
        if (playSec > 0.05)
        {
            double rawBeat = beatTimeMap.secondsToBeat(playSec);
            nextBeat = juce::jmax(1.0, std::round(rawBeat * 2.0) * 0.5);
        }
        else if (showNode.getNumChildren() > 0)
        {
            auto lastScene = showNode.getChild(showNode.getNumChildren() - 1);
            if (lastScene.isValid())
            {
                double lastStart = lastScene.getProperty(StateKeys::StartBeat, 1.0);
                double lastDuration = lastScene.getProperty(StateKeys::DurationBeats, 4.0);
                nextBeat = lastStart + lastDuration;
            }
        }
        newSceneInstance.setProperty(StateKeys::StartBeat, nextBeat, nullptr);

        showNode.addChild(newSceneInstance, -1, nullptr);
        sortScenesByStartBeat(newSceneInstance);
        });
}

int ShowBuilderTab::getNumRows() { return showNode.getNumChildren(); }

void ShowBuilderTab::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.2f));

    if (row >= 0 && row < showNode.getNumChildren())
    {
        auto scene = showNode.getChild(row);
        juce::String name = scene.getProperty(StateKeys::SceneName, "?").toString();
        double beat = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));

        auto pos = beatTimeMap.beatToMusicalPosition(beat);
        juce::String text = "[" + pos.formatBarString() + " | Q " + juce::String(beat, 1) + "] " + name;

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.5f));
        g.drawText(text, 10, 0, w - 20, h, juce::Justification::centredLeft, true);
    }
}

void ShowBuilderTab::selectedRowsChanged(int lastRow) {
    miniDawTimeline.setSelectedSceneIndex(lastRow);

    if (lastRow >= 0 && lastRow < showNode.getNumChildren()) 
        editorPanel.setSceneNode(showNode.getChild(lastRow));
    else editorPanel.setSceneNode(juce::ValueTree());
}

void ShowBuilderTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1e));
    g.setColour(juce::Colour(0xff2a2a32));

    const float topOffset = 285.0f;
    const float dividerX = getLocalBounds().toFloat().proportionOfWidth(0.30f);
    g.fillRect(juce::Rectangle<float>(dividerX, topOffset, 2.0f, static_cast<float>(getHeight()) - topOffset));
}

bool ShowBuilderTab::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused(originatingComponent);

    // Sicurezza: se l'utente sta scrivendo dentro una casella di testo (es. Nome Scena),
    // lascia che i tasti (spazio, W, backspace) scrivano normalmente nel campo.
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    // 1. BARRA SPAZIATRICE -> Play / Pausa immediato della Mini-DAW
    if (key == juce::KeyPress::spaceKey)
    {
        miniDawTimeline.togglePlayPause();
        miniDawTimeline.repaint();
        return true;
    }

    // 2. TASTO W oppure HOME (Stile Reaper) -> Porta istantaneamente il cursore a 00:00.000 (Beat 1.00)
    if (key == juce::KeyPress::homeKey || key.getTextCharacter() == 'w' || key.getTextCharacter() == 'W')
    {
        audioEngine.setPositionSeconds(0.0);
        miniDawTimeline.repaint();
        return true;
    }

    // 3. CANC / BACKSPACE -> Elimina la scena selezionata
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedScene();
        return true;
    }

    return false;
}

void ShowBuilderTab::deleteSelectedScene()
{
    int selectedRow = sceneList.getSelectedRow();
    if (selectedRow < 0 || selectedRow >= showNode.getNumChildren())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Attenzione",
            "Seleziona prima una scena dalla lista da eliminare!"
        );
        return;
    }

    // Prende il nodo della scena da rimuovere
    auto sceneToRemove = showNode.getChild(selectedRow);
    if (sceneToRemove.isValid())
    {
        // Rimuovendo il figlio dal ValueTree, l'interfaccia si aggiornerà da sola via listener
        showNode.removeChild(sceneToRemove, nullptr);

        // Seleziona un'altra riga vicina per evitare selezioni orfane
        int newRow = juce::jmin(selectedRow, showNode.getNumChildren() - 1);
        sceneList.selectRow(newRow);
    }
}

void ShowBuilderTab::resized()
{
    auto bounds = getLocalBounds();

    // 1. Fascia Superiore: Mini-DAW (145 px) + AI Wizard & Slot Stem (108 px)
    miniDawTimeline.setBounds(bounds.removeFromTop(145));
    aiWizardPanel.setBounds(bounds.removeFromTop(136));
    bounds.removeFromTop(4);

    // 2. Parte Inferiore: Lista Scene a sinistra + SceneEditorPanel a destra
    auto leftPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.30f));
    auto bottomArea = leftPanel.removeFromBottom(240).reduced(10, 5);

    saveBtn.setBounds(bottomArea.removeFromBottom(28));           bottomArea.removeFromBottom(4);
    loadBtn.setBounds(bottomArea.removeFromBottom(28));           bottomArea.removeFromBottom(4);
    deletePresetBtnUI.setBounds(bottomArea.removeFromBottom(28)); bottomArea.removeFromBottom(4);
    deleteSceneBtn.setBounds(bottomArea.removeFromBottom(28));    bottomArea.removeFromBottom(4);
    insertPresetBtn.setBounds(bottomArea.removeFromBottom(28));   bottomArea.removeFromBottom(4);
    savePresetBtn.setBounds(bottomArea.removeFromBottom(28));     bottomArea.removeFromBottom(4);
    addSceneBtn.setBounds(bottomArea.removeFromBottom(28));

    sceneList.setBounds(leftPanel.reduced(10));
    bounds.removeFromLeft(2);
    
    // Area Destra: Simulatore Palco 2D + Editor Comandi Scena
    if (stageSimulator.getIsExpanded())
    {
        stageSimulator.setBounds(bounds.reduced(4));
        editorPanel.setVisible(false);
    }
    else
    {
        editorPanel.setVisible(true);
        auto simArea = bounds.removeFromTop(juce::roundToInt(bounds.getHeight() * 0.52f));
        stageSimulator.setBounds(simArea.reduced(4, 2));
        editorPanel.setBounds(bounds);
    }
}

void ShowBuilderTab::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu() || row < 0 || row >= showNode.getNumChildren())
        return;

    sceneList.selectRow(row);

    juce::PopupMenu menu;
    menu.addItem(1, "Duplica Scena");
    menu.addItem(2, "Elimina Scena");

    juce::Component::SafePointer<ShowBuilderTab> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, row](int res) {
        if (safeThis == nullptr || res == 0) return;
        if (res == 1)      safeThis->duplicateScene(row);
        else if (res == 2) safeThis->deleteSelectedScene();
        });
}

void ShowBuilderTab::duplicateScene(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= showNode.getNumChildren())
        return;

    auto src = showNode.getChild(rowIndex);
    if (!src.isValid())
        return;

    auto copy = src.createCopy();
    const juce::String origName = src.getProperty(StateKeys::SceneName, "Scena").toString();
    const double startB = static_cast<double>(src.getProperty(StateKeys::StartBeat, 1.0));
    const double durB = juce::jmax(0.25, static_cast<double>(src.getProperty(StateKeys::DurationBeats, 4.0)));

    copy.setProperty(StateKeys::SceneName, origName + " (Copia)", nullptr);
    copy.setProperty(StateKeys::StartBeat, startB + durB, nullptr);

    showNode.addChild(copy, -1, nullptr);
    sortScenesByStartBeat(copy);
}

void ShowBuilderTab::sortScenesByStartBeat(juce::ValueTree sceneToKeepSelected)
{
    if (isSortingScenes || !showNode.isValid())
        return;

    juce::ScopedValueSetter<bool> guard(isSortingScenes, true);

    if (!sceneToKeepSelected.isValid())
    {
        const int sel = sceneList.getSelectedRow();
        if (sel >= 0 && sel < showNode.getNumChildren())
            sceneToKeepSelected = showNode.getChild(sel);
    }

    struct SceneStartComparator
    {
        static int compareElements(const juce::ValueTree& a, const juce::ValueTree& b) noexcept
        {
            const double sa = static_cast<double>(a.getProperty(StateKeys::StartBeat, 1.0));
            const double sb = static_cast<double>(b.getProperty(StateKeys::StartBeat, 1.0));
            if (sa < sb - 1e-6) return -1;
            if (sa > sb + 1e-6) return 1;

            const double da = static_cast<double>(a.getProperty(StateKeys::DurationBeats, 4.0));
            const double db = static_cast<double>(b.getProperty(StateKeys::DurationBeats, 4.0));
            if (da < db - 1e-6) return -1;
            if (da > db + 1e-6) return 1;
            return 0;
        }
    } comparator;

    showNode.sort(comparator, nullptr, false);
    sceneList.updateContent();

    if (sceneToKeepSelected.isValid())
    {
        const int newIdx = showNode.indexOf(sceneToKeepSelected);
        if (newIdx >= 0)
        {
            sceneList.selectRow(newIdx);
            miniDawTimeline.setSelectedSceneIndex(newIdx);
        }
    }

    sceneList.repaint();
    miniDawTimeline.repaint();
}

void ShowBuilderTab::deletePresetFromLibrary()
{
    if (!presetLibNode.isValid() || presetLibNode.getNumChildren() == 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Libreria Vuota",
            "Non ci sono preset salvati da eliminare."
        );
        return;
    }

    // Creiamo un menu a tendina con i preset salvati
    juce::PopupMenu menu;
    for (int i = 0; i < presetLibNode.getNumChildren(); ++i)
    {
        auto preset = presetLibNode.getChild(i);
        juce::String name = preset.getProperty(StateKeys::PresetName, "").toString();
        if (name.isEmpty()) name = preset.getProperty(StateKeys::SceneName, "Preset Senza Nome").toString();
        menu.addItem(i + 1, name);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&deletePresetBtnUI), [this](int result) {
        if (result > 0)
        {
            int index = result - 1;
            auto presetToRemove = presetLibNode.getChild(index);
            if (presetToRemove.isValid())
            {
                // Rimuove il preset dalla libreria in memoria
                presetLibNode.removeChild(presetToRemove, nullptr);

                // Aggiorna immediatamente il file XML su disco
                savePresetLibraryToDisk();

                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Preset Eliminato",
                    "Il preset e' stato rimosso con successo dalla libreria."
                );
            }
        }
        });
}

void ShowBuilderTab::addNewSceneToState() {
    juce::ValueTree newScene(StateKeys::Scene);
    newScene.setProperty(StateKeys::SceneName, "Scena " + juce::String(showNode.getNumChildren() + 1), nullptr);
    newScene.setProperty(StateKeys::StartBeat, 1.0, nullptr);
    newScene.setProperty(StateKeys::DurationBeats, 4.0, nullptr);
    showNode.addChild(newScene, -1, nullptr);
    sortScenesByStartBeat(newScene);
}

void ShowBuilderTab::saveShow() {
    fileChooser = std::make_unique<juce::FileChooser>("Salva Show", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NuovoBrano.show"), "*.show");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File{}) PresetManager::saveShow(appState, fc.getResult());
        });
}
void ShowBuilderTab::loadShow()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Carica Show",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.show");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File{} || !file.existsAsFile())
            return;

        PresetManager::loadShow(appState, file);

        ShowArchiveManager::ensureValidAppStateStructure(appState);
        beatTimeMap.bindToAppState(appState);

        miniDawTimeline.syncWithAppState();
        aiWizardPanel.syncUIFromState();

        sortScenesByStartBeat();

        sceneList.updateContent();

        if (showNode.getNumChildren() > 0)
            sceneList.selectRow(0);
        else
            editorPanel.setSceneNode(juce::ValueTree());

        sceneList.repaint();
        stageSimulator.repaint();
        });
}

void ShowBuilderTab::valueTreeChildAdded(juce::ValueTree& t, juce::ValueTree&) { if (t == showNode) sceneList.updateContent(); }
void ShowBuilderTab::valueTreeChildRemoved(juce::ValueTree& t, juce::ValueTree&, int) { if (t == showNode) { sceneList.updateContent(); editorPanel.setSceneNode(juce::ValueTree()); } }
void ShowBuilderTab::valueTreeChildOrderChanged(juce::ValueTree&, int, int) { sceneList.updateContent(); }
void ShowBuilderTab::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (!tree.hasType(StateKeys::Scene))
        return;

    if (prop == StateKeys::StartBeat && !isSortingScenes && !miniDawTimeline.isDraggingAnyScene())
        sortScenesByStartBeat(tree);
    else
        sceneList.repaint();
}

juce::File getPresetLibraryFile()
{
    // Salva in una cartella dedicata ai documenti dell'utente o nei file speciali di JUCE
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DoYourLightShow");
    dir.createDirectory(); // Assicura che la cartella esista
    return dir.getChildFile("preset_library.xml");
}

void ShowBuilderTab::savePresetLibraryToDisk()
{
    if (!presetLibNode.isValid()) return;

    // Converte il ValueTree in un blocco XML
    std::unique_ptr<juce::XmlElement> xml(presetLibNode.createXml());
    if (xml != nullptr)
    {
        auto file = getPresetLibraryFile();
        xml->writeTo(file);
    }
}

void ShowBuilderTab::loadPresetLibraryFromDisk()
{
    auto file = getPresetLibraryFile();
    if (file.existsAsFile())
    {
        std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
        if (xml != nullptr)
        {
            auto loadedTree = juce::ValueTree::fromXml(*xml);
            if (loadedTree.isValid() && loadedTree.hasType(StateKeys::PresetLibNode))
            {
                // Sostituisce i figli attuali con quelli caricati dal disco
                presetLibNode.removeAllChildren(nullptr);
                for (int i = 0; i < loadedTree.getNumChildren(); ++i)
                {
                    presetLibNode.addChild(loadedTree.getChild(i).createCopy(), -1, nullptr);
                }
            }
        }
    }
}