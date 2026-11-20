#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/StageRoleCatalog.h"
#include "../Components/StageSimulatorView.h"
#include <vector>
#include <memory>
#include <map>

class SceneBuilderTab : public juce::Component,
    public juce::ListBoxModel,
    private juce::ValueTree::Listener
{
public:
    explicit SceneBuilderTab(juce::ValueTree& state)
        : appState(state), previewSimulator(state)
    {
        presetLibNode = appState.getChildWithName(StateKeys::PresetLibNode);
        if (!presetLibNode.isValid())
        {
            presetLibNode = juce::ValueTree(StateKeys::PresetLibNode);
            appState.addChild(presetLibNode, -1, nullptr);
        }

        bool hasModernPreset = false;
        for (int i = 0; i < presetLibNode.getNumChildren(); ++i)
        {
            auto child = presetLibNode.getChild(i);
            if (child.hasType(StateKeys::ScenePreset))
            {
                if (child.hasProperty(StateKeys::PresetPool))
                {
                    hasModernPreset = true;
                    if (child.getNumChildren() == 0)
                        rebuildPresetCommands(child);
                }
                else
                {
                    migrateLegacyPreset(child);
                }
            }
        }

        if (!hasModernPreset)
            populateStarterPresets();

        presetLibNode.addListener(this);

        initLeftLibraryUI();
        initCenterEditorUI();
        initRightPreviewUI();

        rebuildFilteredList();
        if (!filteredPresets.empty())
        {
            int defaultRow = 0;
            for (size_t i = 0; i < filteredPresets.size(); ++i)
            {
                if (filteredPresets[i].getProperty(StateKeys::PresetPool, "").toString() == "Color")
                {
                    defaultRow = static_cast<int>(i);
                    break;
                }
            }
            presetList.selectRow(defaultRow);
        }
    }

    ~SceneBuilderTab() override
    {
        presetLibNode.removeListener(this);
    }

    // --- ListBoxModel ---
    int getNumRows() override
    {
        return static_cast<int>(filteredPresets.size());
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.22f));
        else if (rowNumber % 2 == 1)
            g.fillAll(juce::Colour(0xff191b24));

        if (rowNumber < 0 || rowNumber >= static_cast<int>(filteredPresets.size()))
            return;

        auto node = filteredPresets[static_cast<size_t>(rowNumber)];
        juce::String name = node.getProperty(StateKeys::PresetName, "Preset").toString();
        juce::String pool = node.getProperty(StateKeys::PresetPool, "Color").toString();
        juce::String layer = node.getProperty(StateKeys::LayerType, "Base").toString();
        double dur = static_cast<double>(node.getProperty(StateKeys::DurationBeats, 4.0));
        bool oneShot = static_cast<bool>(node.getProperty(StateKeys::IsOneShot, false));
        juce::Colour c1 = juce::Colour::fromString(node.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());

        // Swatch colore a sinistra
        g.setColour(c1);
        g.fillRoundedRectangle(6.0f, 6.0f, 10.0f, static_cast<float>(height - 12), 3.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(name, 22, 2, width - 92, 18, juce::Justification::centredLeft, true);

        juce::String meta = pool + " | " + layer + (oneShot ? " [1-Shot]" : " [Loop]");
        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(meta, 22, 19, width - 92, 14, juce::Justification::centredLeft, true);

        // Badge durata a destra
        g.setColour(juce::Colour(0xff2a2f42));
        g.fillRoundedRectangle(static_cast<float>(width - 66), 7.0f, 58.0f, static_cast<float>(height - 14), 4.0f);
        g.setColour(juce::Colour(0xff38bdf8));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        juce::String durText = (std::abs(dur - std::round(dur)) > 0.01)
            ? juce::String(dur, 1)
            : juce::String(dur, 0);
        g.drawText(durText + " Q", width - 66, 0, 58, height, juce::Justification::centred, true);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(filteredPresets.size()))
            selectPresetNode(filteredPresets[static_cast<size_t>(lastRowSelected)]);
        else
            selectPresetNode(juce::ValueTree());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff14151c));
    }

    void resized() override
    {
        auto b = getLocalBounds();

        // 1. Colonna Sinistra (24%): Filtri Pool + Lista Preset + Pulsanti Libreria
        auto left = b.removeFromLeft(juce::roundToInt(b.getWidth() * 0.24f)).reduced(8);
        libraryTitle.setBounds(left.removeFromTop(22));
        left.removeFromTop(4);

        auto poolRow1 = left.removeFromTop(24);
        int pw3 = poolRow1.getWidth() / 3;
        poolButtons[0]->setBounds(poolRow1.removeFromLeft(pw3).reduced(1)); // Tutti
        poolButtons[1]->setBounds(poolRow1.removeFromLeft(pw3).reduced(1)); // Color
        poolButtons[2]->setBounds(poolRow1.reduced(1));                     // Dimmer

        left.removeFromTop(3);
        auto poolRow2 = left.removeFromTop(24);
        poolButtons[3]->setBounds(poolRow2.removeFromLeft(pw3).reduced(1)); // Strobe
        poolButtons[4]->setBounds(poolRow2.removeFromLeft(pw3).reduced(1)); // Movement
        poolButtons[5]->setBounds(poolRow2.reduced(1));                     // ImpactFX

        left.removeFromTop(6);

        auto bottomBtns = left.removeFromBottom(94);
        auto rowAct1 = bottomBtns.removeFromTop(28);
        int halfAct = rowAct1.getWidth() / 2;
        addPresetBtn.setBounds(rowAct1.removeFromLeft(halfAct).reduced(2));
        dupPresetBtn.setBounds(rowAct1.reduced(2));
        bottomBtns.removeFromTop(4);
        delPresetBtn.setBounds(bottomBtns.removeFromTop(26).reduced(2));
        bottomBtns.removeFromTop(4);
        auto rowFile = bottomBtns.removeFromTop(26);
        saveLibBtn.setBounds(rowFile.removeFromLeft(halfAct).reduced(2));
        loadLibBtn.setBounds(rowFile.reduced(2));

        left.removeFromBottom(6);
        presetList.setBounds(left);

        // 2. Colonna Centrale (36%): Editor Contestuale Preset Standalone
        auto center = b.removeFromLeft(juce::roundToInt(b.getWidth() * 0.47f)).reduced(10, 8);
        layoutCenterEditor(center);

        // 3. Colonna Destra (40%): Metronomo Preview + Simulatore Palco 2D
        auto right = b.reduced(6, 8);
        auto transport = right.removeFromTop(34);
        playPreviewBtn.setBounds(transport.removeFromLeft(88).reduced(2));
        triggerOneShotBtn.setBounds(transport.removeFromLeft(105).reduced(2));
        bpmLabel.setBounds(transport.removeFromLeft(34));
        bpmSlider.setBounds(transport.removeFromLeft(94).reduced(2));
        meterCombo.setBounds(transport.removeFromLeft(66).reduced(2));
        strobeHzInfoLabel.setBounds(transport.reduced(4, 2));

        right.removeFromTop(4);
        previewSimulator.setBounds(right);
    }

private:

    static void migrateLegacyPreset(juce::ValueTree& p)
    {
        if (!p.hasProperty(StateKeys::PresetName))
        {
            juce::String legacyName = p.getProperty(StateKeys::SceneName, "Preset Legacy").toString();
            p.setProperty(StateKeys::PresetName, legacyName, nullptr);
        }

        juce::String presetName = p.getProperty(StateKeys::PresetName, "").toString();
        juce::String lowerName = presetName.toLowerCase();

        // 1. Individua prima i ruoli effettivamente accesi (Dimmer > 0)
        std::map<juce::String, int> roleMaxDimmer;
        bool hasHwStrobe = false, hasSquareDimmer = false, hasDimmer = false, hasMove = false;
        bool isSixteenth = lowerName.contains("blast") || lowerName.contains("1/16");

        for (int i = 0; i < p.getNumChildren(); ++i)
        {
            auto cmd = p.getChild(i);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            juce::String r = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
            juce::String param = cmd.getProperty(StateKeys::TargetParam, "").toString();
            juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();
            int peak = juce::jmax(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0)),
                static_cast<int>(cmd.getProperty(StateKeys::EndValue, 0)));

            if (r.isNotEmpty())
            {
                if (param.equalsIgnoreCase("Dimmer"))
                {
                    hasDimmer = true;
                    auto it = roleMaxDimmer.find(r);
                    roleMaxDimmer[r] = (it == roleMaxDimmer.end()) ? peak : juce::jmax(it->second, peak);

                    if (curve.startsWithIgnoreCase("Square"))
                    {
                        hasSquareDimmer = true;
                        if (curve.contains("16")) isSixteenth = true;
                    }
                }
                else if (roleMaxDimmer.find(r) == roleMaxDimmer.end())
                {
                    roleMaxDimmer[r] = 255;
                }
            }

            if (param.equalsIgnoreCase("Strobe") && peak > 0) hasHwStrobe = true;
            if (param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt")) hasMove = true;
        }

        // 2. Filtra ruoli attivi rispettando eventuali vincoli espliciti nel nome ("FLOOR" / "BACK UP")
        auto roleFor = [](StageRoleCatalog::Category targetCat) -> juce::String
            {
                for (const auto& r : StageRoleCatalog::getAllRoles())
                    if (StageRoleCatalog::classifyRole(r.displayName) == targetCat)
                        return r.displayName;
                return {};
            };

        juce::StringArray foundRoles;
        if (lowerName.contains("floor") || lowerName.contains("terra"))
        {
            foundRoles.add(roleFor(StageRoleCatalog::Category::FloorBackSilhouette));
            foundRoles.add(roleFor(StageRoleCatalog::Category::FloorSideWash));
            foundRoles.add(roleFor(StageRoleCatalog::Category::FloorMovingBeam));
        }
        else if (lowerName.contains("back up") || lowerName.contains("backup"))
        {
            foundRoles.add(roleFor(StageRoleCatalog::Category::AirWashBack));
            foundRoles.add(roleFor(StageRoleCatalog::Category::AirMovingBeam));
        }
        else
        {
            for (const auto& kv : roleMaxDimmer)
                if (kv.second > 0)
                    foundRoles.add(kv.first);
        }
        foundRoles.removeEmptyStrings();

        // 3. Ricostruisce il colore SOLO dai ruoli accesi, dando priorita' a eventuali nomi colore espliciti
        int rVal = -1, gVal = -1, bVal = -1, wVal = -1;
        juce::String explicitHex;

        for (int i = 0; i < p.getNumChildren(); ++i)
        {
            auto cmd = p.getChild(i);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            juce::String r = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
            auto dimIt = roleMaxDimmer.find(r);
            if (dimIt != roleMaxDimmer.end() && dimIt->second <= 0)
                continue; // Ignora colori di ruoli spenti!

            juce::String param = cmd.getProperty(StateKeys::TargetParam, "").toString();
            int peak = juce::jmax(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0)),
                static_cast<int>(cmd.getProperty(StateKeys::EndValue, 0)));

            if (param.equalsIgnoreCase("Red"))   rVal = juce::jmax(rVal, peak);
            if (param.equalsIgnoreCase("Green")) gVal = juce::jmax(gVal, peak);
            if (param.equalsIgnoreCase("Blue"))  bVal = juce::jmax(bVal, peak);
            if (param.equalsIgnoreCase("White")) wVal = juce::jmax(wVal, peak);

            if (explicitHex.isEmpty() && cmd.hasProperty(StateKeys::CommandColor))
                explicitHex = cmd.getProperty(StateKeys::CommandColor).toString();
        }

        juce::Colour inferredColor(0xff38bdf8);
        if (lowerName.contains("red") || lowerName.contains("rosso"))
            inferredColor = juce::Colour(0xffdc2626);
        else if (lowerName.contains("white") || lowerName.contains("bianco"))
            inferredColor = juce::Colours::white;
        else if (lowerName.contains("blue") || lowerName.contains("blu"))
            inferredColor = juce::Colour(0xff1d4ed8);
        else if (lowerName.contains("amber") || lowerName.contains("ambra"))
            inferredColor = juce::Colour(0xfff59e0b);
        else if (rVal >= 0 || gVal >= 0 || bVal >= 0)
            inferredColor = juce::Colour(
                static_cast<juce::uint8>(juce::jlimit(0, 255, rVal >= 0 ? rVal : 0)),
                static_cast<juce::uint8>(juce::jlimit(0, 255, gVal >= 0 ? gVal : 0)),
                static_cast<juce::uint8>(juce::jlimit(0, 255, bVal >= 0 ? bVal : 0)));
        else if (wVal > 0)
            inferredColor = juce::Colours::white;
        else if (explicitHex.isNotEmpty())
            inferredColor = juce::Colour::fromString(explicitHex);

        // 4. Scrive i metadati coerenti (sovrascrive quelli incoerenti dei legacy)
        bool isStrobePreset = hasHwStrobe || hasSquareDimmer || lowerName.contains("blast") || lowerName.contains("strobe");
        juce::String inferredPool = isStrobePreset ? "Strobe"
            : (hasMove ? "Movement" : (hasDimmer ? "Dimmer" : "Color"));

        p.setProperty(StateKeys::TargetRolesCsv, foundRoles.joinIntoString("|"), nullptr);
        p.setProperty(StateKeys::PresetPool, inferredPool, nullptr);
        p.setProperty(StateKeys::LayerType, isStrobePreset ? "OverlayFX" : (hasMove ? "Movement" : "Base"), nullptr);
        p.setProperty(StateKeys::PresetColor1, inferredColor.toString(), nullptr);
        p.setProperty(StateKeys::PresetColor2, inferredColor.toString(), nullptr);
        p.setProperty(StateKeys::PresetDimmerMin, 0, nullptr);
        p.setProperty(StateKeys::PresetDimmerMax, 255, nullptr);
        p.setProperty(StateKeys::PresetCurve, "Static", nullptr);
        p.setProperty(StateKeys::PresetStrobeUseHardware, hasHwStrobe, nullptr);
        p.setProperty(StateKeys::PresetStrobeSubdiv, isSixteenth ? "1/16" : "1/8", nullptr);

        rebuildPresetCommands(p);
    }

    // --- ValueTree::Listener ---
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree&) override
    {
        if (parent == presetLibNode)
            rebuildFilteredList();
    }
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree&, int) override
    {
        if (parent == presetLibNode)
            rebuildFilteredList();
    }
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override
    {
        if (parent == presetLibNode)
            rebuildFilteredList();
    }
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree.hasType(StateKeys::ScenePreset))
            presetList.repaint();
    }
    void valueTreeParentChanged(juce::ValueTree&) override {}

    void initLeftLibraryUI()
    {
        libraryTitle.setText("LIBRERIA PRESET (POOL)", juce::dontSendNotification);
        libraryTitle.setFont(juce::FontOptions(14.5f, juce::Font::bold));
        libraryTitle.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff));
        addAndMakeVisible(libraryTitle);

        const juce::StringArray poolNames = { "Tutti", "Color", "Dimmer", "Strobe", "Movement", "ImpactFX" };
        const juce::StringArray poolLabels = { "Tutti", "Colori", "Dimmer", "Strobo", "Movim.", "FX/Imp." };

        for (int i = 0; i < poolNames.size(); ++i)
        {
            auto btn = std::make_unique<juce::TextButton>(poolLabels[i]);
            juce::String filterKey = poolNames[i];
            btn->onClick = [this, filterKey]() {
                activePoolFilter = filterKey;
                updatePoolFilterButtonColors();
                rebuildFilteredList();
                if (!filteredPresets.empty())
                    presetList.selectRow(0);
                else
                    selectPresetNode(juce::ValueTree());
                };
            addAndMakeVisible(*btn);
            poolButtons.push_back(std::move(btn));
        }
        updatePoolFilterButtonColors();

        presetList.setModel(this);
        presetList.setRowHeight(36);
        presetList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff101118));
        presetList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a38));
        addAndMakeVisible(presetList);

        addPresetBtn.setButtonText("+ Nuovo");
        addPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
        addPresetBtn.onClick = [this]() { createNewPreset(); };
        addAndMakeVisible(addPresetBtn);

        dupPresetBtn.setButtonText("Duplica");
        dupPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff334155));
        dupPresetBtn.onClick = [this]() { duplicateCurrentPreset(); };
        addAndMakeVisible(dupPresetBtn);

        delPresetBtn.setButtonText("- Elimina Selezionato");
        delPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f1d1d));
        delPresetBtn.onClick = [this]() { deleteCurrentPreset(); };
        addAndMakeVisible(delPresetBtn);

        saveLibBtn.setButtonText("Salva Lib...");
        saveLibBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d));
        saveLibBtn.onClick = [this]() { savePresetLibrary(); };
        addAndMakeVisible(saveLibBtn);

        loadLibBtn.setButtonText("Carica Lib...");
        loadLibBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d29));
        loadLibBtn.onClick = [this]() { loadPresetLibrary(); };
        addAndMakeVisible(loadLibBtn);
    }

    void initCenterEditorUI()
    {
        auto setupLbl = [this](juce::Label& l, const juce::String& txt, bool isHeader = false, juce::Colour col = juce::Colours::lightgrey)
            {
                l.setText(txt, juce::dontSendNotification);
                l.setFont(juce::FontOptions(isHeader ? 14.0f : 12.0f, isHeader ? juce::Font::bold : juce::Font::plain));
                l.setColour(juce::Label::textColourId, col);
                addAndMakeVisible(l);
            };

        setupLbl(editorTitle, "EDITOR PRESET STANDALONE", true, juce::Colour(0xff00d2ff));
        setupLbl(nameLbl, "Nome:");
        setupLbl(poolLbl, "Pool:");
        setupLbl(layerLbl, "Layer:");
        setupLbl(durLbl, "Durata (Q):");
        setupLbl(phaseLbl, "Sfasamento:");

        nameEdit.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222430));
        nameEdit.onTextChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::PresetName, nameEdit.getText(), nullptr);
            presetList.repaint();
            };
        addAndMakeVisible(nameEdit);

        poolCombo.addItem("Color", 1);
        poolCombo.addItem("Dimmer", 2);
        poolCombo.addItem("Strobe", 3);
        poolCombo.addItem("Movement", 4);
        poolCombo.addItem("ImpactFX", 5);
        poolCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            juce::String newPool = poolCombo.getText();
            currentPreset.setProperty(StateKeys::PresetPool, newPool, nullptr);
            // Layer consigliato automatico al cambio Pool
            if (newPool == "Movement") currentPreset.setProperty(StateKeys::LayerType, "Movement", nullptr);
            else if (newPool == "Strobe" || newPool == "ImpactFX") currentPreset.setProperty(StateKeys::LayerType, "OverlayFX", nullptr);
            else currentPreset.setProperty(StateKeys::LayerType, "Base", nullptr);

            rebuildPresetCommands(currentPreset);
            updateEditorFromPreset();
            resized();
            rebuildFilteredList();
            };
        addAndMakeVisible(poolCombo);

        layerCombo.addItem("Base", 1);
        layerCombo.addItem("Movement", 2);
        layerCombo.addItem("OverlayFX", 3);
        layerCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::LayerType, layerCombo.getText(), nullptr);
            };
        addAndMakeVisible(layerCombo);

        durSlider.setSliderStyle(juce::Slider::IncDecButtons);
        durSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 42, 20);
        durSlider.setRange(0.5, 32.0, 0.5);
        durSlider.setNumDecimalPlacesToDisplay(1);
        durSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::DurationBeats, durSlider.getValue(), nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(durSlider);

        oneShotToggle.setButtonText("One-Shot");
        oneShotToggle.onClick = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::IsOneShot, oneShotToggle.getToggleState(), nullptr);
            };
        addAndMakeVisible(oneShotToggle);

        phaseCombo.addItem("Sync (Tutti Insieme)", 1);
        phaseCombo.addItem("OddEven (Pari / Dispari)", 2);
        phaseCombo.addItem("LeftToRight (Onda SX->DX)", 3);
        phaseCombo.addItem("SymmetricalFan (Ventaglio)", 4);
        phaseCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            const char* ids[4] = { "Sync", "OddEven", "LeftToRight", "SymmetricalFan" };
            int idx = juce::jlimit(0, 3, phaseCombo.getSelectedId() - 1);
            currentPreset.setProperty(StateKeys::PhaseMode, ids[idx], nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(phaseCombo);

        // --- RUOLI TARGET (Checklist 10 categorie) ---
        setupLbl(rolesHeaderLbl, "Ruoli / Categorie Pilotate:", true, juce::Colours::orange);

        selectAllRolesBtn.setButtonText("Tutti");
        selectAllRolesBtn.onClick = [this]() { setAllRoleToggles(true); };
        addAndMakeVisible(selectAllRolesBtn);

        selectNoneRolesBtn.setButtonText("Nessuno");
        selectNoneRolesBtn.onClick = [this]() { setAllRoleToggles(false); };
        addAndMakeVisible(selectNoneRolesBtn);

        for (const auto& roleInfo : StageRoleCatalog::getAllRoles())
        {
            auto tb = std::make_unique<juce::ToggleButton>(roleInfo.displayName);
            tb->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffcbd5e1));
            tb->onClick = [this]() {
                if (isUpdatingUI || !currentPreset.isValid()) return;
                saveRoleTogglesToPreset();
                rebuildPresetCommands(currentPreset);
                previewSimulator.notifyPresetEdited();
                };
            addAndMakeVisible(*tb);
            roleToggles.push_back(std::move(tb));
        }

        // --- SEZIONE CONTESTUALE PARAMETRI DEL POOL ---
        setupLbl(paramsHeaderLbl, "Parametri Effetto:", true, juce::Colour(0xffa78bfa));

        // 1. Controlli Colore (Palette rapida + RGB)
        setupLbl(color1Lbl, "Colore Primario:");
        setupLbl(color2Lbl, "Colore Terra (Split):");

        const std::vector<std::pair<juce::String, juce::Colour>> quickColors = {
            { "Blu",     juce::Colour(0xff1d4ed8) },
            { "Ghiaccio",juce::Colour(0xff38bdf8) },
            { "Rosso",   juce::Colour(0xffdc2626) },
            { "Ambra",   juce::Colour(0xfff59e0b) },
            { "Viola",   juce::Colour(0xff9333ea) },
            { "Verde",   juce::Colour(0xff10b981) },
            { "Bianco",  juce::Colour(0xffffffff) },
            { "Caldo",   juce::Colour(0xffffedd5) }
        };

        for (const auto& qc : quickColors)
        {
            auto b1 = std::make_unique<juce::TextButton>("");
            b1->setColour(juce::TextButton::buttonColourId, qc.second);
            b1->setTooltip("Primario: " + qc.first);
            juce::Colour colVal = qc.second;
            b1->onClick = [this, colVal]() {
                if (!currentPreset.isValid()) return;
                currentPreset.setProperty(StateKeys::PresetColor1, colVal.toString(), nullptr);
                updateRgbSlidersFromColor(colVal);
                rebuildPresetCommands(currentPreset);
                previewSimulator.notifyPresetEdited();
                };
            addAndMakeVisible(*b1);
            quickColorBtns1.push_back(std::move(b1));

            auto b2 = std::make_unique<juce::TextButton>("");
            b2->setColour(juce::TextButton::buttonColourId, qc.second);
            b2->setTooltip("Terra/Contrasto: " + qc.first);
            b2->onClick = [this, colVal]() {
                if (!currentPreset.isValid()) return;
                currentPreset.setProperty(StateKeys::PresetColor2, colVal.toString(), nullptr);
                rebuildPresetCommands(currentPreset);
                previewSimulator.notifyPresetEdited();
                };
            addAndMakeVisible(*b2);
            quickColorBtns2.push_back(std::move(b2));
        }

        auto setupRgbSlider = [this](juce::Slider& s, juce::Colour thumbCol)
            {
                s.setSliderStyle(juce::Slider::LinearHorizontal);
                s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
                s.setRange(0, 255, 1);
                s.setColour(juce::Slider::thumbColourId, thumbCol);
                s.onValueChange = [this]() {
                    if (isUpdatingUI || !currentPreset.isValid()) return;
                    juce::Colour c(static_cast<juce::uint8>(rSlider.getValue()),
                        static_cast<juce::uint8>(gSlider.getValue()),
                        static_cast<juce::uint8>(bSlider.getValue()));
                    currentPreset.setProperty(StateKeys::PresetColor1, c.toString(), nullptr);
                    rebuildPresetCommands(currentPreset);
                    previewSimulator.notifyPresetEdited();
                    };
                addAndMakeVisible(s);
            };
        setupRgbSlider(rSlider, juce::Colours::red);
        setupRgbSlider(gSlider, juce::Colours::limegreen);
        setupRgbSlider(bSlider, juce::Colours::dodgerblue);

        // 2. Controlli Dimmer / ImpactFX
        setupLbl(dimMinLbl, "Dimmer Min:");
        setupLbl(dimMaxLbl, "Dimmer Max:");
        setupLbl(curveLbl, "Curva:");

        dimMinSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        dimMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 20);
        dimMinSlider.setRange(0, 255, 1);
        dimMinSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::PresetDimmerMin, static_cast<int>(dimMinSlider.getValue()), nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(dimMinSlider);

        dimMaxSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        dimMaxSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 20);
        dimMaxSlider.setRange(0, 255, 1);
        dimMaxSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::PresetDimmerMax, static_cast<int>(dimMaxSlider.getValue()), nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(dimMaxSlider);

        curveCombo.addItem("Static (Fisso)", 1);
        curveCombo.addItem("Linear (Rampa sulla durata)", 2);
        curveCombo.addItem("Decay (Decadimento sulla durata)", 3);
        curveCombo.addItem("Sine 1 Bar (4 Q)", 4);
        curveCombo.addItem("Sine 2 Bars (8 Q)", 5);
        curveCombo.addItem("Sine 4 Bars (16 Q)", 6);
        curveCombo.addItem("Sine Durata (1 Ciclo sul Blocco)", 7);
        curveCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            static const char* curveKeys[7] = {
                "Static", "Linear", "Decay", "Sine_1Bar", "Sine_2Bars", "Sine_4Bars", "Sine_Duration"
            };
            int idx = juce::jlimit(0, 6, curveCombo.getSelectedId() - 1);
            currentPreset.setProperty(StateKeys::PresetCurve, curveKeys[idx], nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(curveCombo);

        // 3. Controlli Strobo
        setupLbl(strobeSubdivLbl, "Suddivisione:");

        strobeSubdivCombo.addItem("1/8 (Ottavi - 2 colpi/Q)", 1);
        strobeSubdivCombo.addItem("1/8T (Terzine - 3 colpi/Q)", 2);
        strobeSubdivCombo.addItem("1/16 (Sedicesimi - 4 colpi/Q)", 3);
        strobeSubdivCombo.addItem("1/16T (Sestine - 6 colpi/Q)", 4);
        strobeSubdivCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            static const char* subs[4] = { "1/8", "1/8T", "1/16", "1/16T" };
            int idx = juce::jlimit(0, 3, strobeSubdivCombo.getSelectedId() - 1);
            currentPreset.setProperty(StateKeys::PresetStrobeSubdiv, subs[idx], nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(strobeSubdivCombo);

        // 4. Controlli Movimenti Pan/Tilt
        setupLbl(moveShapeLbl, "Forma Moto:");
        setupLbl(moveSpeedLbl, "Velocita':");
        setupLbl(ampPanLbl, "Amp. Pan %:");
        setupLbl(ampTiltLbl, "Amp. Tilt %:");

        moveShapeCombo.addItem("Fixed (Posizione Home)", 1);
        moveShapeCombo.addItem("PanSweep (Spazzata Pan)", 2);
        moveShapeCombo.addItem("TiltWave (Onda Verticale)", 3);
        moveShapeCombo.addItem("Circle (Cerchio Pan+Tilt)", 4);
        moveShapeCombo.addItem("Figure8 (Otto Pan+Tilt)", 5);
        moveShapeCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            const char* shapes[5] = { "Fixed", "PanSweep", "TiltWave", "Circle", "Figure8" };
            int idx = juce::jlimit(0, 4, moveShapeCombo.getSelectedId() - 1);
            currentPreset.setProperty(StateKeys::PresetMoveShape, shapes[idx], nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(moveShapeCombo);

        moveSpeedCombo.addItem("Lento (4 Bars / 16 Q)", 1);
        moveSpeedCombo.addItem("Medio (2 Bars / 8 Q)", 2);
        moveSpeedCombo.addItem("Veloce (1 Bar / 4 Q)", 3);
        moveSpeedCombo.addItem("Su Durata (1 Ciclo sul Blocco)", 4);
        moveSpeedCombo.onChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            static const char* speeds[4] = { "Sine_4Bars", "Sine_2Bars", "Sine_1Bar", "Sine_Duration" };
            int idx = juce::jlimit(0, 3, moveSpeedCombo.getSelectedId() - 1);
            currentPreset.setProperty(StateKeys::PresetMoveSpeed, speeds[idx], nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(moveSpeedCombo);

        ampPanSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        ampPanSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
        ampPanSlider.setRange(0, 100, 1);
        ampPanSlider.setTextValueSuffix("%");
        ampPanSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::PresetMoveAmpPan, static_cast<int>(ampPanSlider.getValue()), nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(ampPanSlider);

        ampTiltSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        ampTiltSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
        ampTiltSlider.setRange(0, 100, 1);
        ampTiltSlider.setTextValueSuffix("%");
        ampTiltSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            currentPreset.setProperty(StateKeys::PresetMoveAmpTilt, static_cast<int>(ampTiltSlider.getValue()), nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(ampTiltSlider);
    }

    void initRightPreviewUI()
    {

        auto updatePlayBtnVisual = [this](bool isPlaying)
            {
                playPreviewBtn.setButtonText(isPlaying ? "Pausa Loop" : "Avvia Loop");
                playPreviewBtn.setColour(juce::TextButton::buttonColourId,
                    isPlaying ? juce::Colour(0xff10b981) : juce::Colour(0xff334155));
            };

        playPreviewBtn.setButtonText("Pausa Loop");
        playPreviewBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff10b981));
        previewSimulator.onPreviewPlaybackChanged = updatePlayBtnVisual;
        playPreviewBtn.onClick = [this]() {
            previewSimulator.setPreviewPlaying(!previewSimulator.getPreviewPlaying());
            };
 
        updatePlayBtnVisual(previewSimulator.getPreviewPlaying());
        addAndMakeVisible(playPreviewBtn);

        triggerOneShotBtn.setButtonText("Trigger 1-Shot");
        triggerOneShotBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff59e0b));
        triggerOneShotBtn.onClick = [this]() {
            previewSimulator.triggerOneShotPreview();
            };
        addAndMakeVisible(triggerOneShotBtn);

        bpmLabel.setText("BPM:", juce::dontSendNotification);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(bpmLabel);

        bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 22);
        bpmSlider.setRange(60, 220, 1);
        bpmSlider.setValue(130, juce::dontSendNotification);
        bpmSlider.onValueChange = [this]() {
            double bpm = bpmSlider.getValue();
            previewSimulator.setPreviewBpm(bpm);
            updateStrobeHzInfo(bpm);
            };
        addAndMakeVisible(bpmSlider);
        
        meterCombo.addItem("4/4", 1);
        meterCombo.addItem("3/4", 2);
        meterCombo.addItem("6/8", 3);
        meterCombo.addItem("5/8", 4);
        meterCombo.addItem("7/8", 5);
        meterCombo.addItem("7/4", 6);
        meterCombo.addItem("12/8", 7);
        meterCombo.setSelectedId(1, juce::dontSendNotification);
        meterCombo.onChange = [this]() {
            static const int nums[7] = { 4, 3, 6, 5, 7, 7, 12 };
            static const int dens[7] = { 4, 4, 8, 8, 8, 4,  8 };
            int idx = juce::jlimit(0, 6, meterCombo.getSelectedId() - 1);
            previewSimulator.setPreviewTimeSignature(nums[idx], dens[idx]);
            refreshBarLabelsForPreviewMeter();
            };
        addAndMakeVisible(meterCombo);
        refreshBarLabelsForPreviewMeter();

        strobeHzInfoLabel.setFont(juce::FontOptions(11.5f));
        strobeHzInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
        addAndMakeVisible(strobeHzInfoLabel);
        updateStrobeHzInfo(130.0);

        addAndMakeVisible(previewSimulator);
    }

    void updateStrobeHzInfo(double bpm)
    {
        double hz8 = (bpm / 60.0) * 2.0;
        double hz16 = (bpm / 60.0) * 4.0;
        strobeHzInfoLabel.setText("1/8: " + juce::String(hz8, 1) + " Hz | 1/16: " + juce::String(hz16, 1) + " Hz",
            juce::dontSendNotification);
    }

    void layoutCenterEditor(juce::Rectangle<int> area)
    {
        editorTitle.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);

        // Riga 1: Nome + Pool
        auto r1 = area.removeFromTop(24);
        nameLbl.setBounds(r1.removeFromLeft(46));
        nameEdit.setBounds(r1.removeFromLeft(165));
        r1.removeFromLeft(8);
        poolLbl.setBounds(r1.removeFromLeft(38));
        poolCombo.setBounds(r1);

        area.removeFromTop(6);

        // Riga 2: Layer + Durata + OneShot
        auto r2 = area.removeFromTop(24);
        layerLbl.setBounds(r2.removeFromLeft(46));
        layerCombo.setBounds(r2.removeFromLeft(102));
        r2.removeFromLeft(8);
        durLbl.setBounds(r2.removeFromLeft(68));
        durSlider.setBounds(r2.removeFromLeft(86));
        r2.removeFromLeft(8);
        oneShotToggle.setBounds(r2);

        area.removeFromTop(6);

        // Riga 3: PhaseMode
        auto r3 = area.removeFromTop(24);
        phaseLbl.setBounds(r3.removeFromLeft(80));
        phaseCombo.setBounds(r3);

        area.removeFromTop(10);

        // Sezione Ruoli Target
        auto rRolesHdr = area.removeFromTop(22);
        selectNoneRolesBtn.setBounds(rRolesHdr.removeFromRight(66).reduced(1));
        selectAllRolesBtn.setBounds(rRolesHdr.removeFromRight(52).reduced(1));
        rolesHeaderLbl.setBounds(rRolesHdr);

        area.removeFromTop(4);
        int numRoles = static_cast<int>(roleToggles.size());
        int rows = (numRoles + 1) / 2;
        auto roleGrid = area.removeFromTop(rows * 21);
        int halfW = roleGrid.getWidth() / 2;
        for (int i = 0; i < numRoles; ++i)
        {
            int col = i % 2;
            int row = i / 2;
            roleToggles[static_cast<size_t>(i)]->setBounds(
                roleGrid.getX() + col * halfW, roleGrid.getY() + row * 21, halfW - 4, 20);
        }

        area.removeFromTop(10);
        paramsHeaderLbl.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);

        juce::String pool = currentPreset.isValid()
            ? currentPreset.getProperty(StateKeys::PresetPool, "Color").toString()
            : "Color";

        bool showColor1 = (pool == "Color" || pool == "Strobe" || pool == "ImpactFX");
        bool showColor2 = (pool == "Color");
        bool showDimmer = (pool == "Dimmer" || pool == "ImpactFX");
        bool showStrobe = (pool == "Strobe");
        bool showMove = (pool == "Movement");

        // Visibilita' contestuale
        color1Lbl.setVisible(showColor1);
        rSlider.setVisible(showColor1);
        gSlider.setVisible(showColor1);
        bSlider.setVisible(showColor1);
        for (auto& btn : quickColorBtns1) btn->setVisible(showColor1);

        color2Lbl.setVisible(showColor2);
        for (auto& btn : quickColorBtns2) btn->setVisible(showColor2);

        dimMinLbl.setVisible(showDimmer); dimMinSlider.setVisible(showDimmer);
        dimMaxLbl.setVisible(showDimmer); dimMaxSlider.setVisible(showDimmer);
        curveLbl.setVisible(showDimmer);  curveCombo.setVisible(showDimmer);

        strobeSubdivLbl.setVisible(showStrobe); strobeSubdivCombo.setVisible(showStrobe);

        moveShapeLbl.setVisible(showMove);  moveShapeCombo.setVisible(showMove);
        moveSpeedLbl.setVisible(showMove);  moveSpeedCombo.setVisible(showMove);
        ampPanLbl.setVisible(showMove);     ampPanSlider.setVisible(showMove);
        ampTiltLbl.setVisible(showMove);    ampTiltSlider.setVisible(showMove);

        // Posizionamento dei blocchi visibili
        if (showStrobe)
        {
            auto sr = area.removeFromTop(24);
            strobeSubdivLbl.setBounds(sr.removeFromLeft(95));
            strobeSubdivCombo.setBounds(sr);
            area.removeFromTop(8);
        }

        if (showDimmer)
        {
            auto dr1 = area.removeFromTop(24);
            curveLbl.setBounds(dr1.removeFromLeft(85));
            curveCombo.setBounds(dr1);
            area.removeFromTop(5);
            auto dr2 = area.removeFromTop(24);
            dimMinLbl.setBounds(dr2.removeFromLeft(85));
            dimMinSlider.setBounds(dr2);
            area.removeFromTop(5);
            auto dr3 = area.removeFromTop(24);
            dimMaxLbl.setBounds(dr3.removeFromLeft(85));
            dimMaxSlider.setBounds(dr3);
            area.removeFromTop(8);
        }

        if (showMove)
        {
            auto mr1 = area.removeFromTop(24);
            moveShapeLbl.setBounds(mr1.removeFromLeft(85));
            moveShapeCombo.setBounds(mr1);
            area.removeFromTop(5);
            auto mr2 = area.removeFromTop(24);
            moveSpeedLbl.setBounds(mr2.removeFromLeft(85));
            moveSpeedCombo.setBounds(mr2);
            area.removeFromTop(5);
            auto mr3 = area.removeFromTop(24);
            ampPanLbl.setBounds(mr3.removeFromLeft(85));
            ampPanSlider.setBounds(mr3);
            area.removeFromTop(5);
            auto mr4 = area.removeFromTop(24);
            ampTiltLbl.setBounds(mr4.removeFromLeft(85));
            ampTiltSlider.setBounds(mr4);
            area.removeFromTop(8);
        }

        if (showColor1)
        {
            auto cr1 = area.removeFromTop(24);
            color1Lbl.setBounds(cr1.removeFromLeft(115));
            int swW = juce::jmax(20, cr1.getWidth() / static_cast<int>(quickColorBtns1.size()));
            for (auto& btn : quickColorBtns1)
                btn->setBounds(cr1.removeFromLeft(swW).reduced(2));

            area.removeFromTop(4);
            auto rgbRow = area.removeFromTop(22);
            int thirdW = rgbRow.getWidth() / 3;
            rSlider.setBounds(rgbRow.removeFromLeft(thirdW).reduced(1));
            gSlider.setBounds(rgbRow.removeFromLeft(thirdW).reduced(1));
            bSlider.setBounds(rgbRow.reduced(1));
            area.removeFromTop(6);
        }

        if (showColor2)
        {
            auto cr2 = area.removeFromTop(24);
            color2Lbl.setBounds(cr2.removeFromLeft(115));
            int swW = juce::jmax(20, cr2.getWidth() / static_cast<int>(quickColorBtns2.size()));
            for (auto& btn : quickColorBtns2)
                btn->setBounds(cr2.removeFromLeft(swW).reduced(2));
        }
    }

    void updatePoolFilterButtonColors()
    {
        const juce::StringArray poolNames = { "Tutti", "Color", "Dimmer", "Strobe", "Movement", "ImpactFX" };
        for (size_t i = 0; i < poolButtons.size(); ++i)
        {
            bool active = (poolNames[static_cast<int>(i)] == activePoolFilter);
            poolButtons[i]->setColour(juce::TextButton::buttonColourId,
                active ? juce::Colour(0xff0284c7) : juce::Colour(0xff262936));
        }
    }

    void rebuildFilteredList()
    {
        filteredPresets.clear();
        for (int i = 0; i < presetLibNode.getNumChildren(); ++i)
        {
            auto child = presetLibNode.getChild(i);
            if (!child.hasType(StateKeys::ScenePreset)) continue;

            juce::String pool = child.getProperty(StateKeys::PresetPool, "Color").toString();
            if (activePoolFilter == "Tutti" || pool == activePoolFilter)
                filteredPresets.push_back(child);
        }
        presetList.updateContent();
        presetList.repaint();
    }

    void selectPresetNode(juce::ValueTree node)
    {
        currentPreset = node;
        updateEditorFromPreset();
        resized();
        previewSimulator.setPreviewPreset(currentPreset);
    }

    void updateEditorFromPreset()
    {
        juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);

        bool valid = currentPreset.isValid();
        nameEdit.setEnabled(valid);
        poolCombo.setEnabled(valid);
        layerCombo.setEnabled(valid);
        durSlider.setEnabled(valid);
        oneShotToggle.setEnabled(valid);
        phaseCombo.setEnabled(valid);

        if (!valid)
            return;

        nameEdit.setText(currentPreset.getProperty(StateKeys::PresetName, "Preset").toString(), juce::dontSendNotification);

        juce::String pool = currentPreset.getProperty(StateKeys::PresetPool, "Color").toString();
        if (pool == "Color")         poolCombo.setSelectedId(1, juce::dontSendNotification);
        else if (pool == "Dimmer")   poolCombo.setSelectedId(2, juce::dontSendNotification);
        else if (pool == "Strobe")   poolCombo.setSelectedId(3, juce::dontSendNotification);
        else if (pool == "Movement") poolCombo.setSelectedId(4, juce::dontSendNotification);
        else                         poolCombo.setSelectedId(5, juce::dontSendNotification);

        juce::String layer = currentPreset.getProperty(StateKeys::LayerType, "Base").toString();
        if (layer == "Movement")       layerCombo.setSelectedId(2, juce::dontSendNotification);
        else if (layer == "OverlayFX") layerCombo.setSelectedId(3, juce::dontSendNotification);
        else                           layerCombo.setSelectedId(1, juce::dontSendNotification);

        durSlider.setValue(static_cast<double>(currentPreset.getProperty(StateKeys::DurationBeats, 4.0)), juce::dontSendNotification);
        oneShotToggle.setToggleState(static_cast<bool>(currentPreset.getProperty(StateKeys::IsOneShot, false)), juce::dontSendNotification);

        juce::String phase = currentPreset.getProperty(StateKeys::PhaseMode, "Sync").toString();
        if (phase == "OddEven")             phaseCombo.setSelectedId(2, juce::dontSendNotification);
        else if (phase == "LeftToRight")    phaseCombo.setSelectedId(3, juce::dontSendNotification);
        else if (phase == "SymmetricalFan") phaseCombo.setSelectedId(4, juce::dontSendNotification);
        else                                phaseCombo.setSelectedId(1, juce::dontSendNotification);

        /// Sincronizza checklist Ruoli Target (match per nome o per categoria)
        juce::String rolesCsv = currentPreset.getProperty(StateKeys::TargetRolesCsv, "").toString();
        juce::StringArray selectedRoles;
        selectedRoles.addTokens(rolesCsv, "|", "");
        selectedRoles.removeEmptyStrings();

        for (auto& tb : roleToggles)
        {
            juce::String btnRole = tb->getButtonText();
            bool isSelected = selectedRoles.contains(btnRole);

            if (!isSelected)
            {
                auto btnCat = StageRoleCatalog::classifyRole(btnRole);
                for (const auto& sr : selectedRoles)
                {
                    if (StageRoleCatalog::classifyRole(sr) == btnCat)
                    {
                        isSelected = true;
                        break;
                    }
                }
            }
            tb->setToggleState(isSelected, juce::dontSendNotification);
        }

        juce::Colour c1 = juce::Colour::fromString(currentPreset.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());
        updateRgbSlidersFromColor(c1);

        dimMinSlider.setValue(static_cast<double>(currentPreset.getProperty(StateKeys::PresetDimmerMin, 0)), juce::dontSendNotification);
        dimMaxSlider.setValue(static_cast<double>(currentPreset.getProperty(StateKeys::PresetDimmerMax, 255)), juce::dontSendNotification);

        juce::String curve = currentPreset.getProperty(StateKeys::PresetCurve, "Sine_2Bars").toString();
        if (curve == "Static")             curveCombo.setSelectedId(1, juce::dontSendNotification);
        else if (curve == "Linear")        curveCombo.setSelectedId(2, juce::dontSendNotification);
        else if (curve == "Decay")         curveCombo.setSelectedId(3, juce::dontSendNotification);
        else if (curve == "Sine_1Bar")     curveCombo.setSelectedId(4, juce::dontSendNotification);
        else if (curve == "Sine_4Bars")    curveCombo.setSelectedId(6, juce::dontSendNotification);
        else if (curve == "Sine_Duration") curveCombo.setSelectedId(7, juce::dontSendNotification);
        else                               curveCombo.setSelectedId(5, juce::dontSendNotification);

        juce::String sub = currentPreset.getProperty(StateKeys::PresetStrobeSubdiv, "1/8").toString();
        if (sub == "1/8T")       strobeSubdivCombo.setSelectedId(2, juce::dontSendNotification);
        else if (sub == "1/16")  strobeSubdivCombo.setSelectedId(3, juce::dontSendNotification);
        else if (sub == "1/16T") strobeSubdivCombo.setSelectedId(4, juce::dontSendNotification);
        else                     strobeSubdivCombo.setSelectedId(1, juce::dontSendNotification);

        juce::String shape = currentPreset.getProperty(StateKeys::PresetMoveShape, "PanSweep").toString();
        if (shape == "Fixed")         moveShapeCombo.setSelectedId(1, juce::dontSendNotification);
        else if (shape == "PanSweep") moveShapeCombo.setSelectedId(2, juce::dontSendNotification);
        else if (shape == "TiltWave") moveShapeCombo.setSelectedId(3, juce::dontSendNotification);
        else if (shape == "Circle")   moveShapeCombo.setSelectedId(4, juce::dontSendNotification);
        else                          moveShapeCombo.setSelectedId(5, juce::dontSendNotification);

        juce::String speed = currentPreset.getProperty(StateKeys::PresetMoveSpeed, "Sine_4Bars").toString();
        if (speed.startsWith("Sine_1Bar"))          moveSpeedCombo.setSelectedId(3, juce::dontSendNotification);
        else if (speed.startsWith("Sine_2Bars"))    moveSpeedCombo.setSelectedId(2, juce::dontSendNotification);
        else if (speed.startsWith("Sine_Duration")) moveSpeedCombo.setSelectedId(4, juce::dontSendNotification);
        else                                        moveSpeedCombo.setSelectedId(1, juce::dontSendNotification);

        ampPanSlider.setValue(static_cast<double>(currentPreset.getProperty(StateKeys::PresetMoveAmpPan, 80)), juce::dontSendNotification);
        ampTiltSlider.setValue(static_cast<double>(currentPreset.getProperty(StateKeys::PresetMoveAmpTilt, 60)), juce::dontSendNotification);
    }

    void updateRgbSlidersFromColor(juce::Colour c)
    {
        juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
        rSlider.setValue(c.getRed(), juce::dontSendNotification);
        gSlider.setValue(c.getGreen(), juce::dontSendNotification);
        bSlider.setValue(c.getBlue(), juce::dontSendNotification);
    }

    void setAllRoleToggles(bool state)
    {
        if (!currentPreset.isValid()) return;
        {
            juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
            for (auto& tb : roleToggles)
                tb->setToggleState(state, juce::dontSendNotification);
        }
        saveRoleTogglesToPreset();
        rebuildPresetCommands(currentPreset);
        previewSimulator.notifyPresetEdited();
    }

    void saveRoleTogglesToPreset()
    {
        juce::StringArray chosen;
        for (const auto& tb : roleToggles)
            if (tb->getToggleState())
                chosen.add(tb->getButtonText());

        currentPreset.setProperty(StateKeys::TargetRolesCsv, chosen.joinIntoString("|"), nullptr);
    }

    // Compila i nodi ROLE_COMMAND reali dentro il SCENE_PRESET (pronti per Simulatore, Timeline e AI)
    static void rebuildPresetCommands(juce::ValueTree& preset)
    {
        if (!preset.isValid()) return;
        preset.removeAllChildren(nullptr);

        juce::String pool = preset.getProperty(StateKeys::PresetPool, "Color").toString();

        if (!preset.hasProperty(StateKeys::PresetPool))
            preset.setProperty(StateKeys::PresetPool, pool, nullptr);

        juce::String rolesCsv = preset.getProperty(StateKeys::TargetRolesCsv, "").toString();
        juce::StringArray roles;
        roles.addTokens(rolesCsv, "|", "");
        roles.removeEmptyStrings();

        juce::Colour c1 = juce::Colour::fromString(preset.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());
        juce::Colour c2 = juce::Colour::fromString(preset.getProperty(StateKeys::PresetColor2, "ff1d4ed8").toString());

        auto addCmd = [&](const juce::String& role, const juce::String& param, int sVal, int eVal, const juce::String& curve, juce::Colour col)
            {
                juce::ValueTree cmd(StateKeys::RoleCommand);
                cmd.setProperty(StateKeys::TargetRole, role, nullptr);
                cmd.setProperty(StateKeys::TargetParam, param, nullptr);
                cmd.setProperty(StateKeys::StartValue, juce::jlimit(0, 255, sVal), nullptr);
                cmd.setProperty(StateKeys::EndValue, juce::jlimit(0, 255, eVal), nullptr);
                cmd.setProperty(StateKeys::CurveType, curve, nullptr);
                cmd.setProperty(StateKeys::CommandColor, col.toString(), nullptr);
                preset.addChild(cmd, -1, nullptr);
            };

        for (const auto& role : roles)
        {
            auto cat = StageRoleCatalog::classifyRole(role);
            bool isFloorRole = (cat == StageRoleCatalog::Category::FloorBackSilhouette ||
                cat == StageRoleCatalog::Category::FloorSideWash ||
                cat == StageRoleCatalog::Category::FloorMovingBeam);

            if (pool == "Color")
            {
                juce::Colour useCol = isFloorRole ? c2 : c1;
                addCmd(role, "Red", useCol.getRed(), useCol.getRed(), "Static", useCol);
                addCmd(role, "Green", useCol.getGreen(), useCol.getGreen(), "Static", useCol);
                addCmd(role, "Blue", useCol.getBlue(), useCol.getBlue(), "Static", useCol);
            }
            else if (pool == "Dimmer")
            {
                int dMin = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMin, 30));
                int dMax = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMax, 255));
                juce::String curve = preset.getProperty(StateKeys::PresetCurve, "Sine_2Bars").toString();

                if (curve == "Static")
                    addCmd(role, "Dimmer", dMax, dMax, "Static", c1);
                else
                    addCmd(role, "Dimmer", dMin, dMax, curve, c1);
            }
            else if (pool == "Strobe")
            {
                juce::String sub = preset.getProperty(StateKeys::PresetStrobeSubdiv, "1/8").toString();
                juce::String sqCurve = "Square_1_8";
                if (sub == "1/8T")       sqCurve = "Square_1_8T";
                else if (sub == "1/16")  sqCurve = "Square_1_16";
                else if (sub == "1/16T") sqCurve = "Square_1_16T";

                addCmd(role, "Red", c1.getRed(), c1.getRed(), "Static", c1);
                addCmd(role, "Green", c1.getGreen(), c1.getGreen(), "Static", c1);
                addCmd(role, "Blue", c1.getBlue(), c1.getBlue(), "Static", c1);
                addCmd(role, "Dimmer", 0, 255, sqCurve, c1);
            }
            else if (pool == "Movement")
            {
                juce::String shape = preset.getProperty(StateKeys::PresetMoveShape, "PanSweep").toString();
                juce::String speed = preset.getProperty(StateKeys::PresetMoveSpeed, "Sine_4Bars").toString();
                int ampP = static_cast<int>(preset.getProperty(StateKeys::PresetMoveAmpPan, 80));
                int ampT = static_cast<int>(preset.getProperty(StateKeys::PresetMoveAmpTilt, 60));

                int halfP = juce::roundToInt(127.0f * (static_cast<float>(ampP) / 100.0f));
                int halfT = juce::roundToInt(127.0f * (static_cast<float>(ampT) / 100.0f));
                int pStart = 128 - halfP, pEnd = 128 + halfP;
                int tStart = 128 - halfT, tEnd = 128 + halfT;

                if (shape == "Fixed")
                {
                    addCmd(role, "Pan", 128, 128, "Static", c1);
                    addCmd(role, "Tilt", 128, 128, "Static", c1);
                }
                else if (shape == "PanSweep")
                {
                    addCmd(role, "Pan", pStart, pEnd, speed, c1);
                    addCmd(role, "Tilt", 128, 128, "Static", c1);
                }
                else if (shape == "TiltWave")
                {
                    addCmd(role, "Pan", 128, 128, "Static", c1);
                    addCmd(role, "Tilt", tStart, tEnd, speed, c1);
                }
                else if (shape == "Circle")
                {
                    juce::String cosSpeed = speed.replace("Sine", "Cosine");
                    addCmd(role, "Pan", pStart, pEnd, speed, c1);
                    addCmd(role, "Tilt", tStart, tEnd, cosSpeed, c1);
                }
                else // Figure8
                {
                    juce::String doubleSpeed = speed.replace("Sine", "Sine2x");
                    addCmd(role, "Pan", pStart, pEnd, speed, c1);
                    addCmd(role, "Tilt", tStart, tEnd, doubleSpeed, c1);
                }
            }
            else // ImpactFX
            {
                int dMin = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMin, 0));
                int dMax = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMax, 255));
                juce::String curve = preset.getProperty(StateKeys::PresetCurve, "Decay").toString();

                addCmd(role, "Red", c1.getRed(), c1.getRed(), "Static", c1);
                addCmd(role, "Green", c1.getGreen(), c1.getGreen(), "Static", c1);
                addCmd(role, "Blue", c1.getBlue(), c1.getBlue(), "Static", c1);

                // In Decay parte da dMax e scende a dMin
                if (curve == "Decay")
                    addCmd(role, "Dimmer", dMax, dMin, "Decay", c1);
                else if (curve == "Static")
                    addCmd(role, "Dimmer", dMax, dMax, "Static", c1);
                else
                    addCmd(role, "Dimmer", dMin, dMax, curve, c1);
            }
        }
    }

    void populateStarterPresets()
    {
        auto addStarter = [this](const juce::String& name, const juce::String& pool, const juce::String& layer,
            double dur, bool oneShot, const juce::String& phase, const juce::String& rolesCsv,
            const juce::String& c1, const juce::String& c2,
            int dMin, int dMax, const juce::String& curve,
            bool hwStrobe, const juce::String& sub,
            const juce::String& shape, const juce::String& speed, int ampP, int ampT)
            {
                juce::ValueTree p(StateKeys::ScenePreset);
                p.setProperty(StateKeys::PresetName, name, nullptr);
                p.setProperty(StateKeys::PresetPool, pool, nullptr);
                p.setProperty(StateKeys::LayerType, layer, nullptr);
                p.setProperty(StateKeys::DurationBeats, dur, nullptr);
                p.setProperty(StateKeys::IsOneShot, oneShot, nullptr);
                p.setProperty(StateKeys::PhaseMode, phase, nullptr);
                p.setProperty(StateKeys::TargetRolesCsv, rolesCsv, nullptr);
                p.setProperty(StateKeys::PresetColor1, c1, nullptr);
                p.setProperty(StateKeys::PresetColor2, c2, nullptr);
                p.setProperty(StateKeys::PresetDimmerMin, dMin, nullptr);
                p.setProperty(StateKeys::PresetDimmerMax, dMax, nullptr);
                p.setProperty(StateKeys::PresetCurve, curve, nullptr);
                p.setProperty(StateKeys::PresetStrobeUseHardware, hwStrobe, nullptr);
                p.setProperty(StateKeys::PresetStrobeSubdiv, sub, nullptr);
                p.setProperty(StateKeys::PresetMoveShape, shape, nullptr);
                p.setProperty(StateKeys::PresetMoveSpeed, speed, nullptr);
                p.setProperty(StateKeys::PresetMoveAmpPan, ampP, nullptr);
                p.setProperty(StateKeys::PresetMoveAmpTilt, ampT, nullptr);
                rebuildPresetCommands(p);
                presetLibNode.addChild(p, -1, nullptr);
            };

        auto roleFor = [](StageRoleCatalog::Category targetCat) -> juce::String
            {
                for (const auto& r : StageRoleCatalog::getAllRoles())
                    if (StageRoleCatalog::classifyRole(r.displayName) == targetCat)
                        return r.displayName;
                return {};
            };

        using Cat = StageRoleCatalog::Category;
        const juce::String airWash = roleFor(Cat::AirWashBack);
        const juce::String airFront = roleFor(Cat::AirParFront);
        const juce::String airBeam = roleFor(Cat::AirMovingBeam);
        const juce::String floorBack = roleFor(Cat::FloorBackSilhouette);
        const juce::String floorBeam = roleFor(Cat::FloorMovingBeam);
        const juce::String blinder = roleFor(Cat::ImpactBlinder);
        const juce::String strobe = roleFor(Cat::ImpactStrobe);
        const juce::String geyser = roleFor(Cat::FxGeyserBurst);
        const juce::String hazer = roleFor(Cat::FxSmokeHazer);
        const juce::String floorSide = roleFor(Cat::FloorSideWash);

        // 1. Colori
        addStarter("Split Blu Notte / Ghiaccio", "Color", "Base", 8.0, false, "Sync",
            airWash + "|" + airFront + "|" + floorBack,
            "ff1d4ed8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Rosso Sangue / Ambra Terra", "Color", "Base", 8.0, false, "Sync",
            airWash + "|" + airBeam + "|" + floorBack,
            "ffdc2626", "fff59e0b", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        // 2. Dimmer
        addStarter("Respiro Atmosferico (4 Bars / 16 Q)", "Dimmer", "Base", 16.0, false, "Sync",
            airWash + "|" + floorBack,
            "ff38bdf8", "ff1d4ed8", 40, 240, "Sine_4Bars", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Chase Pari/Dispari (1 Bar / 4 Q)", "Dimmer", "Base", 4.0, false, "OddEven",
            airWash + "|" + airFront,
            "fff59e0b", "ffdc2626", 15, 255, "Sine_1Bar", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        // 3. Strobo
        addStarter("Strobo 1/8 (Ottavi Full-Rig)", "Strobe", "OverlayFX", 4.0, false, "Sync",
            airWash + "|" + airFront + "|" + airBeam + "|" + floorBack + "|" + floorSide + "|" + floorBeam,
            "ffffffff", "ffffffff", 0, 255, "Square_1_8", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo 1/16 (Sedicesimi)", "Strobe", "OverlayFX", 4.0, false, "Sync",
            strobe + "|" + airBeam,
            "ffffffff", "ffffffff", 0, 255, "Square_1_16", true, "1/16", "Fixed", "Sine_1Bar", 0, 0);

        // 4. Movimenti Pan/Tilt
        addStarter("Sweep Lento Ventaglio (4 Bars / 16 Q)", "Movement", "Movement", 16.0, false, "SymmetricalFan",
            airBeam + "|" + floorBeam,
            "ff38bdf8", "ff38bdf8", 0, 255, "Sine_4Bars", false, "1/8", "PanSweep", "Sine_4Bars", 85, 40);

        addStarter("Cerchio Ipnotico (2 Bars / 8 Q)", "Movement", "Movement", 8.0, false, "SymmetricalFan",
            airBeam + "|" + floorBeam,
            "ff38bdf8", "ff38bdf8", 0, 255, "Sine_2Bars", false, "1/8", "Circle", "Sine_2Bars", 75, 70);

        // 5. Impatto & FX
        addStarter("Blinder Decay Impatto (4 Q)", "ImpactFX", "OverlayFX", 4.0, true, "Sync",
            blinder,
            "ffffedd5", "ffffedd5", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Geyser Drop Rosso (4 Q)", "ImpactFX", "OverlayFX", 4.0, true, "Sync",
            geyser,
            "ffdc2626", "ffdc2626", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Pre-Carica Nebbia Hazer", "ImpactFX", "OverlayFX", 8.0, false, "Sync",
            hazer,
            "ffcbd5e1", "ffcbd5e1", 180, 180, "Static", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);
    }

    void createNewPreset()
    {
        juce::String pool = (activePoolFilter == "Tutti") ? "Color" : activePoolFilter;
        juce::ValueTree p(StateKeys::ScenePreset);
        p.setProperty(StateKeys::PresetName, "Nuovo " + pool + " " + juce::String(presetLibNode.getNumChildren() + 1), nullptr);
        p.setProperty(StateKeys::PresetPool, pool, nullptr);
        p.setProperty(StateKeys::LayerType, pool == "Movement" ? "Movement" : ((pool == "Strobe" || pool == "ImpactFX") ? "OverlayFX" : "Base"), nullptr);
        p.setProperty(StateKeys::DurationBeats, pool == "Movement" ? 16.0 : 4.0, nullptr);
        p.setProperty(StateKeys::IsOneShot, pool == "ImpactFX", nullptr);
        p.setProperty(StateKeys::PhaseMode, pool == "Movement" ? "SymmetricalFan" : "Sync", nullptr);
        p.setProperty(StateKeys::TargetRolesCsv, StageRoleCatalog::getAllRoles()[0].displayName, nullptr);
        p.setProperty(StateKeys::PresetColor1, "ff38bdf8", nullptr);
        p.setProperty(StateKeys::PresetColor2, "ff1d4ed8", nullptr);
        p.setProperty(StateKeys::PresetDimmerMin, 0, nullptr);
        p.setProperty(StateKeys::PresetDimmerMax, 255, nullptr);
        p.setProperty(StateKeys::PresetCurve, pool == "ImpactFX" ? "Decay" : "Sine_2Bars", nullptr);
        p.setProperty(StateKeys::PresetStrobeUseHardware, false, nullptr);
        p.setProperty(StateKeys::PresetStrobeSubdiv, "1/8", nullptr);
        p.setProperty(StateKeys::PresetMoveShape, "PanSweep", nullptr);
        p.setProperty(StateKeys::PresetMoveSpeed, "Sine_4Bars", nullptr);
        p.setProperty(StateKeys::PresetMoveAmpPan, 80, nullptr);
        p.setProperty(StateKeys::PresetMoveAmpTilt, 60, nullptr);

        rebuildPresetCommands(p);
        presetLibNode.addChild(p, -1, nullptr);
        rebuildFilteredList();

        for (size_t i = 0; i < filteredPresets.size(); ++i)
        {
            if (filteredPresets[i] == p)
            {
                presetList.selectRow(static_cast<int>(i));
                break;
            }
        }
    }

    void duplicateCurrentPreset()
    {
        if (!currentPreset.isValid()) return;
        auto copy = currentPreset.createCopy();
        copy.setProperty(StateKeys::PresetName,
            copy.getProperty(StateKeys::PresetName, "Preset").toString() + " (Copia)", nullptr);
        presetLibNode.addChild(copy, -1, nullptr);
        rebuildFilteredList();
    }

    void deleteCurrentPreset()
    {
        if (!currentPreset.isValid()) return;
        presetLibNode.removeChild(currentPreset, nullptr);
        rebuildFilteredList();
        if (!filteredPresets.empty())
            presetList.selectRow(0);
        else
            selectPresetNode(juce::ValueTree());
    }

    void savePresetLibrary()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Salva Libreria Preset Scene Builder",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MieSceneDMX.presets"),
            "*.presets");

        auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{})
            {
                if (auto xml = presetLibNode.createXml())
                    xml->writeTo(file);
            }
            });
    }

    void loadPresetLibrary()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Carica Libreria Preset Scene Builder",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.presets");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                if (auto xml = juce::XmlDocument::parse(file))
                {
                    auto loaded = juce::ValueTree::fromXml(*xml);
                    if (loaded.isValid() && loaded.hasType(StateKeys::PresetLibNode))
                    {
                        presetLibNode.removeAllChildren(nullptr);
                        for (int i = 0; i < loaded.getNumChildren(); ++i)
                            presetLibNode.addChild(loaded.getChild(i).createCopy(), -1, nullptr);

                        rebuildFilteredList();
                        if (!filteredPresets.empty())
                            presetList.selectRow(0);
                    }
                }
            }
            });
    }

    void refreshBarLabelsForPreviewMeter()
    {
        auto fmtQ = [](double q) -> juce::String {
            return (std::abs(q - std::round(q)) > 0.01) ? juce::String(q, 1) : juce::String(q, 0);
            };

        const double barQ = previewSimulator.getPreviewBarLengthQ();
        juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);

        curveCombo.changeItemText(4, "Sine 1 Bar (" + fmtQ(barQ) + " Q)");
        curveCombo.changeItemText(5, "Sine 2 Bars (" + fmtQ(barQ * 2.0) + " Q)");
        curveCombo.changeItemText(6, "Sine 4 Bars (" + fmtQ(barQ * 4.0) + " Q)");

        moveSpeedCombo.changeItemText(1, "Lento (4 Bars / " + fmtQ(barQ * 4.0) + " Q)");
        moveSpeedCombo.changeItemText(2, "Medio (2 Bars / " + fmtQ(barQ * 2.0) + " Q)");
        moveSpeedCombo.changeItemText(3, "Veloce (1 Bar / " + fmtQ(barQ) + " Q)");
    }

    juce::ValueTree& appState;
    juce::ValueTree presetLibNode;
    juce::ValueTree currentPreset;
    bool isUpdatingUI = false;

    // Colonna Sinistra
    juce::Label libraryTitle;
    juce::String activePoolFilter{ "Tutti" };
    std::vector<std::unique_ptr<juce::TextButton>> poolButtons;
    std::vector<juce::ValueTree> filteredPresets;
    juce::ListBox presetList;
    juce::TextButton addPresetBtn, dupPresetBtn, delPresetBtn, saveLibBtn, loadLibBtn;

    // Colonna Centrale
    juce::Label editorTitle, nameLbl, poolLbl, layerLbl, durLbl, phaseLbl;
    juce::TextEditor nameEdit;
    juce::ComboBox poolCombo, layerCombo, phaseCombo;
    juce::Slider durSlider;
    juce::ToggleButton oneShotToggle;

    juce::Label rolesHeaderLbl;
    juce::TextButton selectAllRolesBtn, selectNoneRolesBtn;
    std::vector<std::unique_ptr<juce::ToggleButton>> roleToggles;

    juce::Label paramsHeaderLbl;
    juce::Label color1Lbl, color2Lbl;
    std::vector<std::unique_ptr<juce::TextButton>> quickColorBtns1;
    std::vector<std::unique_ptr<juce::TextButton>> quickColorBtns2;
    juce::Slider rSlider, gSlider, bSlider;

    juce::Label dimMinLbl, dimMaxLbl, curveLbl;
    juce::Slider dimMinSlider, dimMaxSlider;
    juce::ComboBox curveCombo;

    juce::Label strobeSubdivLbl;
    juce::ComboBox strobeSubdivCombo;

    juce::Label moveShapeLbl, moveSpeedLbl, ampPanLbl, ampTiltLbl;
    juce::ComboBox moveShapeCombo, moveSpeedCombo;
    juce::Slider ampPanSlider, ampTiltSlider;

    // Colonna Destra
    juce::TextButton playPreviewBtn, triggerOneShotBtn;
    juce::Label bpmLabel, strobeHzInfoLabel;
    juce::Slider bpmSlider;
    juce::ComboBox meterCombo;

    StageSimulatorView previewSimulator;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneBuilderTab)
};