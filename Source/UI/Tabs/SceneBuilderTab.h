#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/StageRoleCatalog.h"
#include "../Components/StageSimulatorView.h"
#include "../Components/FileProgressOverlay.h"
#include <vector>
#include <memory>
#include <map>

class SceneBuilderTab : public juce::Component,
                        public juce::ListBoxModel,
                        private juce::ValueTree::Listener,
                        private juce::AsyncUpdater
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
                    sanitizePresetNode(child);
                }
                else
                {
                    migrateLegacyPreset(child);
                }
            }
        }

        if (!hasModernPreset)
            populateStarterPresets();

        savePresetLibraryToDefaultDisk();

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
        cancelPendingUpdate();
        presetLibNode.removeListener(this);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            previewSimulator.stopFixtureTestIfActive();
    }

    void setDmxLiveEngine(DmxLiveEngine* engine) noexcept
    {
        previewSimulator.setDmxLiveEngine(engine);
    }

    void setAllowBackgroundDmx(bool allow) noexcept
    {
        previewSimulator.setAllowBackgroundDmx(allow);
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
        double dur = static_cast<double>(node.getProperty(StateKeys::DurationBeats, 4.0));
        bool oneShot = static_cast<bool>(node.getProperty(StateKeys::IsOneShot, false));

        juce::Colour c1 = juce::Colour::fromString(node.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());

        bool uCol = false, uDim = false, uMov = false, uStr = false;
        resolvePresetFeatureFlags(node, uCol, uDim, uMov, uStr);

        // Swatch colore a sinistra (usa il colore reale se Colore e' attivo, altrimenti tinta del Pool)
        juce::Colour swatchCol = c1;
        if (!uCol)
        {
            if (uStr)      swatchCol = juce::Colour(0xffe2e8f0);
            else if (uMov) swatchCol = juce::Colour(0xffa855f7);
            else if (uDim) swatchCol = juce::Colour(0xfff59e0b);
            else           swatchCol = juce::Colour(0xff475569);
        }
        g.setColour(swatchCol);
        g.fillRoundedRectangle(6.0f, 6.0f, 10.0f, static_cast<float>(height - 12), 3.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(name, 22, 2, width - 92, 18, juce::Justification::centredLeft, true);

        juce::StringArray attrs;
        if (uCol) attrs.add("Col");
        if (uDim) attrs.add("Dim");
        if (uMov) attrs.add("Mov");
        if (uStr) attrs.add("Str");
        juce::String attrTag = attrs.isEmpty() ? "[Vuoto]" : ("[" + attrs.joinIntoString("+") + "]");

        juce::String meta = pool + " " + attrTag + (oneShot ? " 1-Shot" : " Loop");
        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(meta, 22, 19, width - 92, 14, juce::Justification::centredLeft, true);

        // Badge durata a destra
        g.setColour(juce::Colour(0xff2a2f42));
        g.fillRoundedRectangle(static_cast<float>(width - 66), 7.0f, 58.0f, static_cast<float>(height - 14), 4.0f);
        g.setColour(juce::Colour(0xff38bdf8));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        juce::String durText;
        if (std::abs(dur - std::round(dur)) < 0.01)
            durText = juce::String(dur, 0);
        else if (std::abs(dur * 2.0 - std::round(dur * 2.0)) < 0.01)
            durText = juce::String(dur, 1);
        else
            durText = juce::String(dur, 2);
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
        
        auto rowAct2 = bottomBtns.removeFromTop(26);
        delPresetBtn.setBounds(rowAct2.removeFromLeft(halfAct).reduced(2));
        factoryPresetsBtn.setBounds(rowAct2.reduced(2));

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

    std::function<void()> onPresetLibraryClean;

    bool savePresetLibraryForExit()
    {
        savePresetLibraryToDefaultDisk();
        if (onPresetLibraryClean)
            onPresetLibraryClean();
        return true;
    }

    void refreshAfterUndoRedo()
    {
        cancelPendingUpdate();
        pendingSyncPreset = juce::ValueTree();
        pendingSyncDuration = false;

        juce::ScopedValueSetter<bool> suppressGuard(isSuppressingShowSync, true);

        const int prevRow = presetList.getSelectedRow();
        rebuildFilteredList();

        if (!filteredPresets.empty())
        {
            const int targetRow = juce::jlimit(0, static_cast<int>(filteredPresets.size()) - 1,
                prevRow < 0 ? 0 : prevRow);
            presetList.selectRow(targetRow);
            selectPresetNode(filteredPresets[static_cast<size_t>(targetRow)]);
        }
        else
        {
            presetList.deselectAllRows();
            selectPresetNode(juce::ValueTree());
        }
    }

private:

    void snapDurationToLoopCycle(juce::ValueTree& preset, const juce::String& speedOrCurveKey)
    {
        if (!preset.isValid() || static_cast<bool>(preset.getProperty(StateKeys::IsOneShot, false)))
            return;

        double cycleBars = 0.0;
        if (speedOrCurveKey.containsIgnoreCase("4Bars"))      cycleBars = 4.0;
        else if (speedOrCurveKey.containsIgnoreCase("2Bars")) cycleBars = 2.0;
        else if (speedOrCurveKey.containsIgnoreCase("1Bar"))  cycleBars = 1.0;

        if (cycleBars <= 0.0)
            return;

        const double cycleQ = cycleBars * previewSimulator.getPreviewBarLengthQ();
        const double curDur = static_cast<double>(preset.getProperty(StateKeys::DurationBeats, cycleQ));
        const double ratio = curDur / cycleQ;

        // Se la durata corrente non e' un multiplo intero del ciclo, la allinea al multiplo piu' vicino (>= 1 ciclo)
        if (std::abs(ratio - std::round(ratio)) > 0.01 || curDur < cycleQ - 0.01)
        {
            const double alignedDur = juce::jmax(1.0, std::round(ratio)) * cycleQ;
            preset.setProperty(StateKeys::DurationBeats, alignedDur, nullptr);
            juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
            durSlider.setValue(alignedDur, juce::dontSendNotification);
            presetList.repaint();
        }
    }

    class PositionAimPad : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        std::function<void(int panPct, int tiltPct)> onAimChanged;

        void setAimValues(int panPct, int tiltPct, int fanPanPct)
        {
            panVal = juce::jlimit(-100, 100, panPct);
            tiltVal = juce::jlimit(-100, 100, tiltPct);
            fanVal = juce::jlimit(-100, 100, fanPanPct);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(juce::Colour(0xff0f111a));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(juce::Colour(0xff334155));
            g.drawRoundedRectangle(r, 5.0f, 1.0f);

            const float cx = r.getCentreX();
            const float cy = r.getCentreY();

            // Assi di riferimento SX/DX e Pubblico/Band
            g.setColour(juce::Colour(0xff263042));
            g.drawHorizontalLine(juce::roundToInt(cy), r.getX() + 4.0f, r.getRight() - 4.0f);
            g.drawVerticalLine(juce::roundToInt(cx), r.getY() + 4.0f, r.getBottom() - 4.0f);

            g.setColour(juce::Colour(0xff64748b));
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            g.drawText("PUBBLICO", r.toNearestInt().withHeight(12), juce::Justification::centredTop, false);
            g.drawText("BAND", r.toNearestInt().withTop(r.toNearestInt().getBottom() - 12), juce::Justification::centredBottom, false);
            g.drawText("SX", r.toNearestInt().reduced(3, 0), juce::Justification::centredLeft, false);
            g.drawText("DX", r.toNearestInt().reduced(3, 0), juce::Justification::centredRight, false);

            auto toPoint = [&](float pPct, float tPct) -> juce::Point<float> {
                const float x = r.getX() + 8.0f + ((pPct + 100.0f) / 200.0f) * (r.getWidth() - 16.0f);
                const float y = r.getBottom() - 8.0f - ((tPct + 100.0f) / 200.0f) * (r.getHeight() - 16.0f);
                return { x, y };
                };

            const auto targetPt = toPoint(static_cast<float>(panVal), static_cast<float>(tiltVal));

            // Anteprima grafica del Fan Pan (due punti laterali SX/DX)
            if (std::abs(fanVal) > 2)
            {
                const auto leftPt = toPoint(juce::jlimit(-100.0f, 100.0f, static_cast<float>(panVal - fanVal)), static_cast<float>(tiltVal));
                const auto rightPt = toPoint(juce::jlimit(-100.0f, 100.0f, static_cast<float>(panVal + fanVal)), static_cast<float>(tiltVal));
                g.setColour(fanVal > 0 ? juce::Colour(0xff38bdf8).withAlpha(0.55f) : juce::Colour(0xfff59e0b).withAlpha(0.55f));
                g.drawLine(leftPt.x, leftPt.y, rightPt.x, rightPt.y, 1.5f);
                g.fillEllipse(leftPt.x - 3.0f, leftPt.y - 3.0f, 6.0f, 6.0f);
                g.fillEllipse(rightPt.x - 3.0f, rightPt.y - 3.0f, 6.0f, 6.0f);
            }

            // Mirino punto target principale
            g.setColour(juce::Colour(0xff00d2ff).withAlpha(0.30f));
            g.fillEllipse(targetPt.x - 9.0f, targetPt.y - 9.0f, 18.0f, 18.0f);
            g.setColour(juce::Colours::white);
            g.fillEllipse(targetPt.x - 4.0f, targetPt.y - 4.0f, 8.0f, 8.0f);
            g.setColour(juce::Colour(0xff00d2ff));
            g.drawEllipse(targetPt.x - 5.0f, targetPt.y - 5.0f, 10.0f, 10.0f, 1.4f);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.getNumberOfClicks() >= 2)
            {
                panVal = 0;
                tiltVal = 0;
                repaint();
                if (onAimChanged) onAimChanged(0, 0);
                return;
            }
            updateFromMouse(e.position);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            updateFromMouse(e.position);
        }

    private:
        void updateFromMouse(juce::Point<float> pt)
        {
            auto r = getLocalBounds().toFloat().reduced(8.0f);
            if (r.getWidth() <= 1.0f || r.getHeight() <= 1.0f) return;

            const float nx = juce::jlimit(0.0f, 1.0f, (pt.x - r.getX()) / r.getWidth());
            const float ny = juce::jlimit(0.0f, 1.0f, (r.getBottom() - pt.y) / r.getHeight());

            panVal = juce::roundToInt(nx * 200.0f - 100.0f);
            tiltVal = juce::roundToInt(ny * 200.0f - 100.0f);
            repaint();
            if (onAimChanged) onAimChanged(panVal, tiltVal);
        }

        int panVal = 0;
        int tiltVal = 0;
        int fanVal = 0;
    };

    static void resolvePresetFeatureFlags(const juce::ValueTree& p,
        bool& useColor, bool& useDimmer, bool& useMove, bool& useStrobe)
    {
        if (!p.isValid())
        {
            useColor = useDimmer = useMove = useStrobe = false;
            return;
        }

        const juce::String pool = p.getProperty(StateKeys::PresetPool, "Color").toString();

        useColor = p.hasProperty(StateKeys::PresetUseColor)
            ? static_cast<bool>(p.getProperty(StateKeys::PresetUseColor))
            : (pool == "Color" || pool == "ImpactFX");

        useDimmer = p.hasProperty(StateKeys::PresetUseDimmer)
            ? static_cast<bool>(p.getProperty(StateKeys::PresetUseDimmer))
            : (pool == "Dimmer" || pool == "ImpactFX");

        useMove = p.hasProperty(StateKeys::PresetUseMove)
            ? static_cast<bool>(p.getProperty(StateKeys::PresetUseMove))
            : (pool == "Movement");

        useStrobe = p.hasProperty(StateKeys::PresetUseStrobe)
            ? static_cast<bool>(p.getProperty(StateKeys::PresetUseStrobe))
            : (pool == "Strobe");
    }

    void saveFeatureTogglesToPreset()
    {
        if (!currentPreset.isValid()) return;
        currentPreset.setProperty(StateKeys::PresetUseColor, useColorToggle.getToggleState(), nullptr);
        currentPreset.setProperty(StateKeys::PresetUseDimmer, useDimmerToggle.getToggleState(), nullptr);
        currentPreset.setProperty(StateKeys::PresetUseMove, useMoveToggle.getToggleState(), nullptr);
        currentPreset.setProperty(StateKeys::PresetUseStrobe, useStrobeToggle.getToggleState(), nullptr);
    }
    
    static void sanitizePresetNode(juce::ValueTree& p)
    {
        if (!p.isValid() || !p.hasType(StateKeys::ScenePreset))
            return;

        ensurePresetId(p);

        bool uCol = false, uDim = false, uMov = false, uStr = false;
        resolvePresetFeatureFlags(p, uCol, uDim, uMov, uStr);

        // Rende espliciti i 4 flag attributo sul nodo
        p.setProperty(StateKeys::PresetUseColor, uCol, nullptr);
        p.setProperty(StateKeys::PresetUseDimmer, uDim, nullptr);
        p.setProperty(StateKeys::PresetUseMove, uMov, nullptr);
        p.setProperty(StateKeys::PresetUseStrobe, uStr, nullptr);

        // Preset gestito dallo Scene Builder: rigenera comandi puliti eliminando residui RGB/Dimmer/Move
        if (p.hasProperty(StateKeys::TargetRolesCsv))
        {
            rebuildPresetCommands(p);
            return;
        }

        // Scena manuale salvata da Tab 3: rimuove solo i comandi degli attributi disattivati
        for (int i = p.getNumChildren() - 1; i >= 0; --i)
        {
            auto cmd = p.getChild(i);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            const juce::String param = cmd.getProperty(StateKeys::TargetParam, "").toString();
            const juce::String curve = cmd.getProperty(StateKeys::CurveType, "").toString();

            const bool isColorParam = param.equalsIgnoreCase("Red") || param.equalsIgnoreCase("Green")
                || param.equalsIgnoreCase("Blue") || param.equalsIgnoreCase("White") || param.equalsIgnoreCase("UV");
            const bool isMoveParam = param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt");
            const bool isStrobeCmd = param.equalsIgnoreCase("Strobe")
                || (param.equalsIgnoreCase("Dimmer") && CurveEvaluator::getStrobeHitsPerQuarter(curve) > 0.0);
            const bool isDimmerCmd = (param.equalsIgnoreCase("Dimmer") && !isStrobeCmd)
                || param.equalsIgnoreCase("GeyserBlast");

            if ((isColorParam && !uCol) || (isMoveParam && !uMov) ||
                (isStrobeCmd && !uStr) || (isDimmerCmd && !uDim))
            {
                p.removeChild(i, nullptr);
            }
        }
    }

    static juce::String ensurePresetId(juce::ValueTree& preset)
    {
        if (!preset.isValid()) return {};
        juce::String id = preset.getProperty(StateKeys::PresetId, "").toString().trim();
        if (id.isEmpty())
        {
            id = juce::Uuid().toString();
            preset.setProperty(StateKeys::PresetId, id, nullptr);
        }
        return id;
    }

    static void rememberPresetAlias(juce::ValueTree& preset, const juce::String& oldName)
    {
        const juce::String trimmed = oldName.trim();
        if (!preset.isValid() || trimmed.isEmpty())
            return;

        juce::StringArray aliases;
        aliases.addTokens(preset.getProperty(StateKeys::PresetAliases, "").toString(), "|", "");
        aliases.removeEmptyStrings();

        for (const auto& existing : aliases)
            if (existing.equalsIgnoreCase(trimmed))
                return;

        aliases.add(trimmed);
        preset.setProperty(StateKeys::PresetAliases, aliases.joinIntoString("|"), nullptr);
    }

    void syncLinkedScenesInShow(juce::ValueTree preset, bool syncDuration, const juce::String& fallbackOldName)
    {
        if (!preset.isValid())
            return;

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
            return;

        const juce::String presetId = ensurePresetId(preset);
        const juce::String currentName = preset.getProperty(StateKeys::PresetName, "Scena").toString();

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            if (!scene.hasType(StateKeys::Scene))
                continue;

            const juce::String scenePresetId = scene.getProperty(StateKeys::PresetId, "").toString().trim();
            bool isLinked = scenePresetId.isNotEmpty() && (scenePresetId == presetId);

            // Retrocompatibilita': aggancia scene gia' presenti in Tab 3 senza PresetId tramite nome
            if (!isLinked && scenePresetId.isEmpty())
            {
                const juce::String sceneName = scene.getProperty(StateKeys::SceneName, "").toString();
                if ((fallbackOldName.isNotEmpty() && sceneName.equalsIgnoreCase(fallbackOldName)) ||
                    (currentName.isNotEmpty() && sceneName.equalsIgnoreCase(currentName)))
                {
                    isLinked = true;
                    scene.setProperty(StateKeys::PresetId, presetId, nullptr);
                }
            }

            if (!isLinked)
                continue;

            // Copia tutte le proprieta' del preset preservando StartBeat (e DurationBeats se non modificata)
            for (int pIdx = 0; pIdx < preset.getNumProperties(); ++pIdx)
            {
                const auto propKey = preset.getPropertyName(pIdx);
                if (propKey == StateKeys::StartBeat)
                    continue;
                if (propKey == StateKeys::DurationBeats && !syncDuration)
                    continue;

                scene.setProperty(propKey, preset.getProperty(propKey), nullptr);
            }
            scene.setProperty(StateKeys::SceneName, currentName, nullptr);
            scene.setProperty(StateKeys::PresetId, presetId, nullptr);

            // Sostituisce tutti i ROLE_COMMAND della scena con quelli aggiornati del preset
            for (int c = scene.getNumChildren() - 1; c >= 0; --c)
            {
                if (scene.getChild(c).hasType(StateKeys::RoleCommand))
                    scene.removeChild(c, nullptr);
            }
            for (int c = 0; c < preset.getNumChildren(); ++c)
            {
                auto cmd = preset.getChild(c);
                if (cmd.hasType(StateKeys::RoleCommand))
                    scene.addChild(cmd.createCopy(), -1, nullptr);
            }
        }
    }

void handleAsyncUpdate() override
    {
        if (!pendingSyncPreset.isValid())
            return;

        auto presetToSync = pendingSyncPreset;
        const bool syncDur = pendingSyncDuration;
        pendingSyncPreset = juce::ValueTree();
        pendingSyncDuration = false;

        const juce::String currentName = presetToSync.getProperty(StateKeys::PresetName, "").toString();
        const juce::String fallbackName = (presetToSync == currentPreset)
            ? selectedPresetLastSyncedName
            : currentName;

        if (fallbackName.isNotEmpty() && !fallbackName.equalsIgnoreCase(currentName))
        {
            juce::ScopedValueSetter<bool> suppressGuard(isSuppressingShowSync, true);
            rememberPresetAlias(presetToSync, fallbackName);
        }

        syncLinkedScenesInShow(presetToSync, syncDur, fallbackName);

        if (presetToSync == currentPreset)
            selectedPresetLastSyncedName = currentName;
    }

    static void migrateLegacyPreset(juce::ValueTree& p)
    {
        ensurePresetId(p);

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

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& prop) override
    {
        if (tree.hasType(StateKeys::ScenePreset))
        {
            presetList.repaint();

            if (!isSuppressingShowSync && tree == currentPreset)
            {
                pendingSyncPreset = tree;
                if (prop == StateKeys::DurationBeats)
                    pendingSyncDuration = true;
                triggerAsyncUpdate();
            }
        }
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

        delPresetBtn.setButtonText("- Elimina");
        delPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f1d1d));
        delPresetBtn.onClick = [this]() { deleteCurrentPreset(); };
        addAndMakeVisible(delPresetBtn);

        factoryPresetsBtn.setButtonText("+ Factory");
        factoryPresetsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f766e));
        factoryPresetsBtn.setTooltip("Aggiunge tutti i preset professionali mancanti senza toccare i tuoi");
        factoryPresetsBtn.onClick = [this]() {
            juce::ScopedValueSetter<bool> suppressGuard(isSuppressingShowSync, true);
            const int added = populateStarterPresets(true);
            rebuildFilteredList();
            savePresetLibraryToDefaultDisk();
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Libreria Factory",
                added > 0
                ? ("Aggiunti " + juce::String(added) + " preset factory alla libreria!")
                : "Tutti i preset factory sono gia' presenti nella tua libreria.");
            };
        addAndMakeVisible(factoryPresetsBtn);

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
            currentPreset.setProperty(StateKeys::SceneName, nameEdit.getText(), nullptr);
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
            // Fissa i flag espliciti correnti prima di cambiare la categoria in libreria
            saveFeatureTogglesToPreset();

            juce::String newPool = poolCombo.getText();
            currentPreset.setProperty(StateKeys::PresetPool, newPool, nullptr);

            if (newPool == "Movement")
                currentPreset.setProperty(StateKeys::LayerType, "Movement", nullptr);
            else if (newPool == "Strobe" || newPool == "ImpactFX")
                currentPreset.setProperty(StateKeys::LayerType, "OverlayFX", nullptr);
            else
                currentPreset.setProperty(StateKeys::LayerType, "Base", nullptr);

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
        durSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 54, 20);
        durSlider.setRange(0.25, 999.0, 0.25);
        durSlider.setNumDecimalPlacesToDisplay(2);
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
        setupLbl(paramsHeaderLbl, "Attributi Inclusi nella Scena (seleziona cosa controlla):", true, juce::Colour(0xffa78bfa));

        auto setupFeatureToggle = [this](juce::ToggleButton& tb, const juce::String& text, const juce::String& tip)
            {
                tb.setButtonText(text);
                tb.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe2e8f0));
                tb.setTooltip(tip);
                tb.onClick = [this]() {
                    if (isUpdatingUI || !currentPreset.isValid()) return;
                    saveFeatureTogglesToPreset();
                    rebuildPresetCommands(currentPreset);
                    resized();
                    presetList.repaint();
                    previewSimulator.notifyPresetEdited();
                    };
                addAndMakeVisible(tb);
            };

        setupFeatureToggle(useColorToggle, "Colore", "Invia comandi RGB (se spento lascia invariato il colore attivo)");
        setupFeatureToggle(useDimmerToggle, "Dimmer / FX", "Invia curva Dimmer (statica, rampa, respiro, decay)");
        setupFeatureToggle(useMoveToggle, "Movimento", "Invia Pan/Tilt (se spento lascia invariato il movimento attivo)");
        setupFeatureToggle(useStrobeToggle, "Strobo", "Invia flash ritmico sul Dimmer (ha priorita' sulla curva Dimmer)");

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
        dimMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        dimMinSlider.setRange(0, 100, 1);
        dimMinSlider.setTextValueSuffix("%");
        dimMinSlider.setTooltip("Intensita' minima (0-100%, rimappata automaticamente sul range Dimmer sicuro di ogni faro)");
        dimMinSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            const int raw255 = juce::jlimit(0, 255, juce::roundToInt((dimMinSlider.getValue() / 100.0) * 255.0));
            currentPreset.setProperty(StateKeys::PresetDimmerMin, raw255, nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(dimMinSlider);

        dimMaxSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        dimMaxSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        dimMaxSlider.setRange(0, 100, 1);
        dimMaxSlider.setTextValueSuffix("%");
        dimMaxSlider.setTooltip("Intensita' massima (100% = piena luce fissa, mai strobo anche su canali condivisi)");
        dimMaxSlider.onValueChange = [this]() {
            if (isUpdatingUI || !currentPreset.isValid()) return;
            const int raw255 = juce::jlimit(0, 255, juce::roundToInt((dimMaxSlider.getValue() / 100.0) * 255.0));
            currentPreset.setProperty(StateKeys::PresetDimmerMax, raw255, nullptr);
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
            snapDurationToLoopCycle(currentPreset, curveKeys[idx]);
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

        moveShapeCombo.addItem("Fixed (Posizione Statica / Fan)", 1);
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
            resized();
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
            snapDurationToLoopCycle(currentPreset, speeds[idx]);
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

        // 4b. Controlli Posizione Fissa & Fan (stile MA3 / Avolites / SoundSwitch)
        setupLbl(panCenterLbl, "Pan Base:");
        setupLbl(tiltCenterLbl, "Tilt Base:");
        setupLbl(panFanLbl, "Fan Pan <>:");
        setupLbl(tiltFanLbl, "Fan Tilt:");

        auto setupSignedPctSlider = [this](juce::Slider& s, const juce::Identifier& key, const juce::String& tip)
            {
                s.setSliderStyle(juce::Slider::LinearHorizontal);
                s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
                s.setRange(-100, 100, 1);
                s.setTextValueSuffix("%");
                s.setTooltip(tip);
                s.onValueChange = [this, key, &s]() {
                    if (isUpdatingUI || !currentPreset.isValid()) return;
                    currentPreset.setProperty(key, static_cast<int>(s.getValue()), nullptr);
                    positionAimPad.setAimValues(
                        static_cast<int>(panCenterSlider.getValue()),
                        static_cast<int>(tiltCenterSlider.getValue()),
                        static_cast<int>(panFanSlider.getValue()));
                    rebuildPresetCommands(currentPreset);
                    previewSimulator.notifyPresetEdited();
                    };
                addAndMakeVisible(s);
            };

        setupSignedPctSlider(panCenterSlider, StateKeys::PresetMovePanCenter, "Direzione orizzontale (-100% SX, 0% Centro, +100% DX)");
        setupSignedPctSlider(tiltCenterSlider, StateKeys::PresetMoveTiltCenter, "Altezza fascio (-100% Band/Giu, 0% Centro, +100% Pubblico/Su)");
        setupSignedPctSlider(panFanSlider, StateKeys::PresetMovePanFan, "Apertura Pan: negativo = Incrocio X (><), 0 = Parallelo, positivo = Ventaglio (<>)");
        setupSignedPctSlider(tiltFanSlider, StateKeys::PresetMoveTiltFan, "Apertura Tilt (segue Sfasamento: Arco simmetrico, Alternato o Diagonale)");

        positionAimPad.setTooltip("Trascina per puntare Pan & Tilt live (doppio click = torna a Home 0/0)");
        positionAimPad.onAimChanged = [this](int pPct, int tPct) {
            if (!currentPreset.isValid()) return;
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
                panCenterSlider.setValue(pPct, juce::dontSendNotification);
                tiltCenterSlider.setValue(tPct, juce::dontSendNotification);
            }
            currentPreset.setProperty(StateKeys::PresetMovePanCenter, pPct, nullptr);
            currentPreset.setProperty(StateKeys::PresetMoveTiltCenter, tPct, nullptr);
            rebuildPresetCommands(currentPreset);
            previewSimulator.notifyPresetEdited();
            };
        addAndMakeVisible(positionAimPad);

        struct QuickPos { const char* label; int pan; int tilt; int fanP; int fanT; };
        const QuickPos quickPositions[6] = {
            { "Home",        0,    0,   0,  0 },
            { "Ventaglio",   0,   20,  70,  0 },
            { "Incrocio X",  0,   15, -75,  0 },
            { "Focus Band",  0,  -45, -35,  0 },
            { "Pubblico",    0,   75,  45,  0 },
            { "Cielo",       0,  100,  20,  0 }
        };

        for (const auto& qp : quickPositions)
        {
            auto btn = std::make_unique<juce::TextButton>(qp.label);
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272b3d));
            btn->onClick = [this, qp]() {
                if (!currentPreset.isValid()) return;
                currentPreset.setProperty(StateKeys::PresetMovePanCenter, qp.pan, nullptr);
                currentPreset.setProperty(StateKeys::PresetMoveTiltCenter, qp.tilt, nullptr);
                currentPreset.setProperty(StateKeys::PresetMovePanFan, qp.fanP, nullptr);
                currentPreset.setProperty(StateKeys::PresetMoveTiltFan, qp.fanT, nullptr);
                {
                    juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
                    panCenterSlider.setValue(qp.pan, juce::dontSendNotification);
                    tiltCenterSlider.setValue(qp.tilt, juce::dontSendNotification);
                    panFanSlider.setValue(qp.fanP, juce::dontSendNotification);
                    tiltFanSlider.setValue(qp.fanT, juce::dontSendNotification);
                }
                positionAimPad.setAimValues(qp.pan, qp.tilt, qp.fanP);
                rebuildPresetCommands(currentPreset);
                previewSimulator.notifyPresetEdited();
                };
            addAndMakeVisible(*btn);
            quickPosBtns.push_back(std::move(btn));
        }
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
            if (currentPreset.isValid())
            {
                bool uCol = false, uDim = false, uMov = false, uStr = false;
                resolvePresetFeatureFlags(currentPreset, uCol, uDim, uMov, uStr);
                if (uMov)
                    snapDurationToLoopCycle(currentPreset, currentPreset.getProperty(StateKeys::PresetMoveSpeed, "").toString());
                else if (uDim)
                    snapDurationToLoopCycle(currentPreset, currentPreset.getProperty(StateKeys::PresetCurve, "").toString());
            }
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
        layerLbl.setBounds(r2.removeFromLeft(44));
        layerCombo.setBounds(r2.removeFromLeft(94));
        r2.removeFromLeft(6);
        durLbl.setBounds(r2.removeFromLeft(66));
        durSlider.setBounds(r2.removeFromLeft(104));
        r2.removeFromLeft(6);
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

        area.removeFromTop(8);
        paramsHeaderLbl.setBounds(area.removeFromTop(18));
        area.removeFromTop(4);

        // Barra dei 4 interruttori indipendenti (Colore / Dimmer / Movimento / Strobo)
        auto featRow = area.removeFromTop(24);
        int qW = featRow.getWidth() / 4;
        useColorToggle.setBounds(featRow.removeFromLeft(qW));
        useDimmerToggle.setBounds(featRow.removeFromLeft(qW));
        useMoveToggle.setBounds(featRow.removeFromLeft(qW));
        useStrobeToggle.setBounds(featRow);
        area.removeFromTop(6);

        bool showColor = false, showDimmer = false, showMove = false, showStrobe = false;
        resolvePresetFeatureFlags(currentPreset, showColor, showDimmer, showMove, showStrobe);

        // Visibilita' guidata esclusivamente dagli interruttori attivi
        color1Lbl.setVisible(showColor);
        rSlider.setVisible(showColor);
        gSlider.setVisible(showColor);
        bSlider.setVisible(showColor);
        for (auto& btn : quickColorBtns1) btn->setVisible(showColor);

        color2Lbl.setVisible(showColor);
        for (auto& btn : quickColorBtns2) btn->setVisible(showColor);

        dimMinLbl.setVisible(showDimmer); dimMinSlider.setVisible(showDimmer);
        dimMaxLbl.setVisible(showDimmer); dimMaxSlider.setVisible(showDimmer);
        curveLbl.setVisible(showDimmer);  curveCombo.setVisible(showDimmer);

        strobeSubdivLbl.setVisible(showStrobe); strobeSubdivCombo.setVisible(showStrobe);

        const juce::String curShape = currentPreset.isValid()
            ? currentPreset.getProperty(StateKeys::PresetMoveShape, "PanSweep").toString()
            : "PanSweep";
        const bool isFixedMove = showMove && curShape.equalsIgnoreCase("Fixed");
        const bool isDynamicMove = showMove && !isFixedMove;

        moveShapeLbl.setVisible(showMove);      moveShapeCombo.setVisible(showMove);
        moveSpeedLbl.setVisible(isDynamicMove); moveSpeedCombo.setVisible(isDynamicMove);
        ampPanLbl.setVisible(isDynamicMove);    ampPanSlider.setVisible(isDynamicMove);
        ampTiltLbl.setVisible(isDynamicMove);   ampTiltSlider.setVisible(isDynamicMove);

        positionAimPad.setVisible(isFixedMove);
        panCenterLbl.setVisible(isFixedMove);   panCenterSlider.setVisible(isFixedMove);
        tiltCenterLbl.setVisible(isFixedMove);  tiltCenterSlider.setVisible(isFixedMove);
        panFanLbl.setVisible(isFixedMove);      panFanSlider.setVisible(isFixedMove);
        tiltFanLbl.setVisible(isFixedMove);     tiltFanSlider.setVisible(isFixedMove);
        for (auto& btn : quickPosBtns)          btn->setVisible(isFixedMove);

        // Posizionamento compatto: tutte e 4 le sezioni convivono senza sovrapporsi
        if (showStrobe && area.getHeight() >= 24)
        {
            auto sr = area.removeFromTop(22);
            strobeSubdivLbl.setBounds(sr.removeFromLeft(95));
            strobeSubdivCombo.setBounds(sr);
            area.removeFromTop(5);
        }

        if (isDynamicMove && area.getHeight() >= 96)
        {
            auto mr1 = area.removeFromTop(22);
            moveShapeLbl.setBounds(mr1.removeFromLeft(85));
            moveShapeCombo.setBounds(mr1);
            area.removeFromTop(3);
            auto mr2 = area.removeFromTop(22);
            moveSpeedLbl.setBounds(mr2.removeFromLeft(85));
            moveSpeedCombo.setBounds(mr2);
            area.removeFromTop(3);
            auto mr3 = area.removeFromTop(22);
            ampPanLbl.setBounds(mr3.removeFromLeft(85));
            ampPanSlider.setBounds(mr3);
            area.removeFromTop(3);
            auto mr4 = area.removeFromTop(22);
            ampTiltLbl.setBounds(mr4.removeFromLeft(85));
            ampTiltSlider.setBounds(mr4);
            area.removeFromTop(6);
        }
        else if (isFixedMove && area.getHeight() >= 118)
        {
            auto mr1 = area.removeFromTop(22);
            moveShapeLbl.setBounds(mr1.removeFromLeft(85));
            moveShapeCombo.setBounds(mr1);
            area.removeFromTop(4);

            auto qpRow = area.removeFromTop(22);
            const int qpW = juce::jmax(40, qpRow.getWidth() / juce::jmax(1, static_cast<int>(quickPosBtns.size())));
            for (auto& btn : quickPosBtns)
                btn->setBounds(qpRow.removeFromLeft(qpW).reduced(1));
            area.removeFromTop(5);

            auto aimBlock = area.removeFromTop(92);
            positionAimPad.setBounds(aimBlock.removeFromLeft(118).reduced(1));
            aimBlock.removeFromLeft(8);

            auto pr1 = aimBlock.removeFromTop(21);
            panCenterLbl.setBounds(pr1.removeFromLeft(74));
            panCenterSlider.setBounds(pr1);
            aimBlock.removeFromTop(2);

            auto pr2 = aimBlock.removeFromTop(21);
            tiltCenterLbl.setBounds(pr2.removeFromLeft(74));
            tiltCenterSlider.setBounds(pr2);
            aimBlock.removeFromTop(2);

            auto pr3 = aimBlock.removeFromTop(21);
            panFanLbl.setBounds(pr3.removeFromLeft(74));
            panFanSlider.setBounds(pr3);
            aimBlock.removeFromTop(2);

            auto pr4 = aimBlock.removeFromTop(21);
            tiltFanLbl.setBounds(pr4.removeFromLeft(74));
            tiltFanSlider.setBounds(pr4);

            area.removeFromTop(6);
        }

        if (showDimmer && area.getHeight() >= 72)
        {
            auto dr1 = area.removeFromTop(22);
            curveLbl.setBounds(dr1.removeFromLeft(85));
            curveCombo.setBounds(dr1);
            area.removeFromTop(3);
            auto dr2 = area.removeFromTop(22);
            dimMinLbl.setBounds(dr2.removeFromLeft(85));
            dimMinSlider.setBounds(dr2);
            area.removeFromTop(3);
            auto dr3 = area.removeFromTop(22);
            dimMaxLbl.setBounds(dr3.removeFromLeft(85));
            dimMaxSlider.setBounds(dr3);
            area.removeFromTop(6);
        }

        if (showColor && area.getHeight() >= 72)
        {
            auto cr1 = area.removeFromTop(22);
            color1Lbl.setBounds(cr1.removeFromLeft(115));
            int swW = juce::jmax(18, cr1.getWidth() / juce::jmax(1, static_cast<int>(quickColorBtns1.size())));
            for (auto& btn : quickColorBtns1)
                btn->setBounds(cr1.removeFromLeft(swW).reduced(2));

            area.removeFromTop(3);
            auto rgbRow = area.removeFromTop(20);
            int thirdW = rgbRow.getWidth() / 3;
            rSlider.setBounds(rgbRow.removeFromLeft(thirdW).reduced(1));
            gSlider.setBounds(rgbRow.removeFromLeft(thirdW).reduced(1));
            bSlider.setBounds(rgbRow.reduced(1));

            area.removeFromTop(4);
            auto cr2 = area.removeFromTop(22);
            color2Lbl.setBounds(cr2.removeFromLeft(115));
            int swW2 = juce::jmax(18, cr2.getWidth() / juce::jmax(1, static_cast<int>(quickColorBtns2.size())));
            for (auto& btn : quickColorBtns2)
                btn->setBounds(cr2.removeFromLeft(swW2).reduced(2));
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
        handleUpdateNowIfNeeded(); // Completa eventuali sync del preset precedente prima di cambiare selezione
        currentPreset = node;
        if (currentPreset.isValid())
        {
            juce::ScopedValueSetter<bool> suppressGuard(isSuppressingShowSync, true);
            ensurePresetId(currentPreset);
            selectedPresetLastSyncedName = currentPreset.getProperty(StateKeys::PresetName, "").toString();
        }
        else
        {
            selectedPresetLastSyncedName.clear();
        }
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
        useColorToggle.setEnabled(valid);
        useDimmerToggle.setEnabled(valid);
        useMoveToggle.setEnabled(valid);
        useStrobeToggle.setEnabled(valid);

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

        bool uCol = false, uDim = false, uMov = false, uStr = false;
        resolvePresetFeatureFlags(currentPreset, uCol, uDim, uMov, uStr);
        useColorToggle.setToggleState(uCol, juce::dontSendNotification);
        useDimmerToggle.setToggleState(uDim, juce::dontSendNotification);
        useMoveToggle.setToggleState(uMov, juce::dontSendNotification);
        useStrobeToggle.setToggleState(uStr, juce::dontSendNotification);

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

        const int rawDimMin = static_cast<int>(currentPreset.getProperty(StateKeys::PresetDimmerMin, 0));
        const int rawDimMax = static_cast<int>(currentPreset.getProperty(StateKeys::PresetDimmerMax, 255));
        dimMinSlider.setValue(juce::roundToInt((static_cast<double>(rawDimMin) / 255.0) * 100.0), juce::dontSendNotification);
        dimMaxSlider.setValue(juce::roundToInt((static_cast<double>(rawDimMax) / 255.0) * 100.0), juce::dontSendNotification);

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

        const int panC = static_cast<int>(currentPreset.getProperty(StateKeys::PresetMovePanCenter, 0));
        const int tiltC = static_cast<int>(currentPreset.getProperty(StateKeys::PresetMoveTiltCenter, 0));
        const int panF = static_cast<int>(currentPreset.getProperty(StateKeys::PresetMovePanFan, 0));
        const int tiltF = static_cast<int>(currentPreset.getProperty(StateKeys::PresetMoveTiltFan, 0));
        panCenterSlider.setValue(panC, juce::dontSendNotification);
        tiltCenterSlider.setValue(tiltC, juce::dontSendNotification);
        panFanSlider.setValue(panF, juce::dontSendNotification);
        tiltFanSlider.setValue(tiltF, juce::dontSendNotification);
        positionAimPad.setAimValues(panC, tiltC, panF);
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

        bool useColor = false, useDimmer = false, useMove = false, useStrobe = false;
        resolvePresetFeatureFlags(preset, useColor, useDimmer, useMove, useStrobe);

        juce::String rolesCsv = preset.getProperty(StateKeys::TargetRolesCsv, "").toString();
        juce::StringArray roles;
        roles.addTokens(rolesCsv, "|", "");
        roles.removeEmptyStrings();

        juce::Colour c1 = juce::Colour::fromString(preset.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());
        juce::Colour c2 = juce::Colour::fromString(preset.getProperty(StateKeys::PresetColor2, "ff1d4ed8").toString());

        const int dMin = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMin, 0));
        const int dMax = static_cast<int>(preset.getProperty(StateKeys::PresetDimmerMax, 255));
        const juce::String curve = preset.getProperty(StateKeys::PresetCurve, "Sine_2Bars").toString();

        const juce::String shape = preset.getProperty(StateKeys::PresetMoveShape, "PanSweep").toString();
        const juce::String speed = preset.getProperty(StateKeys::PresetMoveSpeed, "Sine_4Bars").toString();
        const int ampP = static_cast<int>(preset.getProperty(StateKeys::PresetMoveAmpPan, 80));
        const int ampT = static_cast<int>(preset.getProperty(StateKeys::PresetMoveAmpTilt, 60));

        const int halfP = juce::roundToInt(127.0f * (static_cast<float>(ampP) / 100.0f));
        const int halfT = juce::roundToInt(127.0f * (static_cast<float>(ampT) / 100.0f));
        const int pStart = 128 - halfP, pEnd = 128 + halfP;
        const int tStart = 128 - halfT, tEnd = 128 + halfT;

        auto addCmd = [&](const juce::String& role, const juce::String& param, int sVal, int eVal, const juce::String& cType, juce::Colour col)
            {
                juce::ValueTree cmd(StateKeys::RoleCommand);
                cmd.setProperty(StateKeys::TargetRole, role, nullptr);
                cmd.setProperty(StateKeys::TargetParam, param, nullptr);
                cmd.setProperty(StateKeys::StartValue, juce::jlimit(0, 255, sVal), nullptr);
                cmd.setProperty(StateKeys::EndValue, juce::jlimit(0, 255, eVal), nullptr);
                cmd.setProperty(StateKeys::CurveType, cType, nullptr);
                cmd.setProperty(StateKeys::CommandColor, col.toString(), nullptr);
                preset.addChild(cmd, -1, nullptr);
            };

        for (const auto& role : roles)
        {
            const auto cat = StageRoleCatalog::classifyRole(role);
            const bool isFloorRole = (cat == StageRoleCatalog::Category::FloorBackSilhouette ||
                cat == StageRoleCatalog::Category::FloorSideWash ||
                cat == StageRoleCatalog::Category::FloorMovingBeam);
            const juce::Colour useCol = useColor
                ? (isFloorRole ? c2 : c1)
                : juce::Colours::white;

            // 1. Colore (solo se spuntato)
            if (useColor)
            {
                addCmd(role, "Red", useCol.getRed(), useCol.getRed(), "Static", useCol);
                addCmd(role, "Green", useCol.getGreen(), useCol.getGreen(), "Static", useCol);
                addCmd(role, "Blue", useCol.getBlue(), useCol.getBlue(), "Static", useCol);
            }

            // 2. Strobo oppure Dimmer/FX (solo se spuntato)
            if (useStrobe)
            {
                const juce::String sub = preset.getProperty(StateKeys::PresetStrobeSubdiv, "1/8").toString();
                juce::String sqCurve = "Square_1_8";
                if (sub == "1/8T")       sqCurve = "Square_1_8T";
                else if (sub == "1/16")  sqCurve = "Square_1_16";
                else if (sub == "1/16T") sqCurve = "Square_1_16T";
                addCmd(role, "Dimmer", 0, 255, sqCurve, useCol);
            }
            else if (useDimmer)
            {
                if (curve == "Decay")
                    addCmd(role, "Dimmer", dMax, dMin, "Decay", useCol);
                else if (curve == "Static")
                    addCmd(role, "Dimmer", dMax, dMax, "Static", useCol);
                else
                    addCmd(role, "Dimmer", dMin, dMax, curve, useCol);
            }

            // 3. Movimento Pan/Tilt (solo se spuntato)
            if (useMove)
            {
                if (shape == "Fixed")
                {
                    const int panC = static_cast<int>(preset.getProperty(StateKeys::PresetMovePanCenter, 0));
                    const int tiltC = static_cast<int>(preset.getProperty(StateKeys::PresetMoveTiltCenter, 0));
                    const int panDmx = juce::jlimit(0, 255, 128 + juce::roundToInt(static_cast<float>(panC) * 1.27f));
                    const int tiltDmx = juce::jlimit(0, 255, 128 + juce::roundToInt(static_cast<float>(tiltC) * 1.27f));
                    addCmd(role, "Pan", panDmx, panDmx, "Static", useCol);
                    addCmd(role, "Tilt", tiltDmx, tiltDmx, "Static", useCol);
                }
                else if (shape == "PanSweep")
                {
                    addCmd(role, "Pan", pStart, pEnd, speed, useCol);
                    addCmd(role, "Tilt", 128, 128, "Static", useCol);
                }
                else if (shape == "TiltWave")
                {
                    addCmd(role, "Pan", 128, 128, "Static", useCol);
                    addCmd(role, "Tilt", tStart, tEnd, speed, useCol);
                }
                else if (shape == "Circle")
                {
                    const juce::String cosSpeed = juce::String(speed).replace("Sine", "Cosine");
                    addCmd(role, "Pan", pStart, pEnd, speed, useCol);
                    addCmd(role, "Tilt", tStart, tEnd, cosSpeed, useCol);
                }
                else // Figure8
                {
                    const juce::String doubleSpeed = juce::String(speed).replace("Sine", "Sine2x");
                    addCmd(role, "Pan", pStart, pEnd, speed, useCol);
                    addCmd(role, "Tilt", tStart, tEnd, doubleSpeed, useCol);
                }
            }
        }
    }

    void savePresetLibraryToDefaultDisk()
    {
        if (!presetLibNode.isValid()) return;
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DoYourLightShow");
        dir.createDirectory();
        if (auto xml = presetLibNode.createXml())
            xml->writeTo(dir.getChildFile("preset_library.xml"));

        if (onPresetLibraryClean)
            onPresetLibraryClean();
    }

    int populateStarterPresets(bool onlyAddMissing = false)
    {
        auto findPresetByName = [this](const juce::String& targetName) -> juce::ValueTree
            {
                for (int i = 0; i < presetLibNode.getNumChildren(); ++i)
                {
                    auto ch = presetLibNode.getChild(i);
                    if (ch.hasType(StateKeys::ScenePreset) &&
                        ch.getProperty(StateKeys::PresetName, "").toString().equalsIgnoreCase(targetName))
                        return ch;
                }
                return {};
            };

        auto roleFor = [](StageRoleCatalog::Category targetCat) -> juce::String
            {
                for (const auto& r : StageRoleCatalog::getAllRoles())
                    if (StageRoleCatalog::classifyRole(r.displayName) == targetCat)
                        return r.displayName;
                return {};
            };

        auto joinRoles = [](std::initializer_list<juce::String> list) -> juce::String
            {
                juce::StringArray arr;
                for (const auto& s : list)
                    if (s.isNotEmpty())
                        arr.addIfNotAlreadyThere(s);
                return arr.joinIntoString("|");
            };

        using Cat = StageRoleCatalog::Category;
        const juce::String airWash = roleFor(Cat::AirWashBack);
        const juce::String airFront = roleFor(Cat::AirParFront);
        const juce::String airBeam = roleFor(Cat::AirMovingBeam);
        const juce::String floorBack = roleFor(Cat::FloorBackSilhouette);
        const juce::String floorSide = roleFor(Cat::FloorSideWash);
        const juce::String floorBeam = roleFor(Cat::FloorMovingBeam);
        const juce::String blinder = roleFor(Cat::ImpactBlinder);
        const juce::String strobe = roleFor(Cat::ImpactStrobe);
        const juce::String geyser = roleFor(Cat::FxGeyserBurst);
        const juce::String hazer = roleFor(Cat::FxSmokeHazer);

        const juce::String fullStageLights = joinRoles({ airWash, airFront, airBeam, floorBack, floorSide, floorBeam });
        const juce::String backAndFloor = joinRoles({ airWash, floorBack, floorSide });
        const juce::String airGroup = joinRoles({ airWash, airFront, airBeam });
        const juce::String allMovers = joinRoles({ airBeam, floorBeam });
        const juce::String strobeRig = joinRoles({ strobe, airBeam, airWash, floorBack });

        int addedCount = 0;

        auto addStarter = [&](const juce::String& name,
            const juce::String& pool,
            const juce::String& layer,
            double dur,
            bool oneShot,
            const juce::String& phase,
            const juce::String& rolesCsv,
            bool useCol, bool useDim, bool useMov, bool useStr,
            const juce::String& c1,
            const juce::String& c2,
            int dMin, int dMax,
            const juce::String& curve,
            bool hwStrobe,
            const juce::String& sub,
            const juce::String& shape,
            const juce::String& speed,
            int ampP, int ampT)
            {
                const juce::String factoryName = name.startsWithIgnoreCase("[F] ") ? name : ("[F] " + name);

                if (onlyAddMissing)
                {
                    if (auto existingF = findPresetByName(factoryName); existingF.isValid())
                    {
                        sanitizePresetNode(existingF);
                        return;
                    }

                    if (auto existing = findPresetByName(name); existing.isValid())
                    {
                        existing.setProperty(StateKeys::PresetName, factoryName, nullptr);
                        existing.setProperty(StateKeys::SceneName, factoryName, nullptr);
                        existing.setProperty(StateKeys::PresetUseColor, useCol, nullptr);
                        existing.setProperty(StateKeys::PresetUseDimmer, useDim, nullptr);
                        existing.setProperty(StateKeys::PresetUseMove, useMov, nullptr);
                        existing.setProperty(StateKeys::PresetUseStrobe, useStr, nullptr);
                        rebuildPresetCommands(existing);
                        ++addedCount;
                        return;
                    }
                }

                juce::ValueTree p(StateKeys::ScenePreset);

                ensurePresetId(p);

                p.setProperty(StateKeys::PresetName, factoryName, nullptr);
                p.setProperty(StateKeys::SceneName, factoryName, nullptr);
                p.setProperty(StateKeys::PresetPool, pool, nullptr);
                p.setProperty(StateKeys::LayerType, layer, nullptr);
                p.setProperty(StateKeys::DurationBeats, dur, nullptr);
                p.setProperty(StateKeys::IsOneShot, oneShot, nullptr);
                p.setProperty(StateKeys::PhaseMode, phase, nullptr);
                p.setProperty(StateKeys::TargetRolesCsv, rolesCsv, nullptr);

                p.setProperty(StateKeys::PresetUseColor, useCol, nullptr);
                p.setProperty(StateKeys::PresetUseDimmer, useDim, nullptr);
                p.setProperty(StateKeys::PresetUseMove, useMov, nullptr);
                p.setProperty(StateKeys::PresetUseStrobe, useStr, nullptr);

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
                ++addedCount;
            };

        // =====================================================================
        // 1. COLOR POOL (11 preset — solo Colore, sovrapponibili a tutto)
        // =====================================================================
        addStarter("Colore: Bianco Freddo (Daylight)", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ffe0f2fe", "ffbae6fd", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Colore: Tungsteno Caldo (Ambra)", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ffffedd5", "fff59e0b", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Colore: Blu Notte Profondo", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff1d4ed8", "ff1e3a8a", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Colore: Rosso Sangue", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ffdc2626", "ff7f1d1d", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Colore: UV / Viola Profondo", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff7c3aed", "ff3b0764", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Colore: Verde Bosco / Smeraldo", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff15803d", "ff064e3b", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Split Blu Notte / Ghiaccio", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff1d4ed8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Split Rosso Sangue / Ambra Terra", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ffdc2626", "fff59e0b", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Split Blu Ghiaccio / Ambra Calda", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff0284c7", "ffd97706", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Split Viola / Ciano", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff7e22ce", "ff06b6d4", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Split Rosso / Bianco Taglio", "Color", "Base", 8.0, false, "Sync", fullStageLights,
            true, false, false, false, "ff991b1b", "ffffffff", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        // =====================================================================
        // 2. DIMMER POOL (11 preset — solo Intensita' & Chase)
        // =====================================================================
        addStarter("Dimmer: Full On 100% (Full Rig)", "Dimmer", "Base", 8.0, false, "Sync", fullStageLights,
            false, true, false, false, "ffffffff", "ffffffff", 255, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Dimmer: Atmosfera 45% (Soft)", "Dimmer", "Base", 8.0, false, "Sync", fullStageLights,
            false, true, false, false, "ff94a3b8", "ff94a3b8", 115, 115, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Dimmer: Solo Controluce Silhouette", "Dimmer", "Base", 8.0, false, "Sync", backAndFloor,
            false, true, false, false, "ff38bdf8", "ff38bdf8", 230, 230, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Dimmer: Blackout Totale (0%)", "Dimmer", "Base", 4.0, false, "Sync", fullStageLights,
            false, true, false, false, "ff1e293b", "ff1e293b", 0, 0, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Fade In Lento (16 Q)", "Dimmer", "Base", 16.0, true, "Sync", fullStageLights,
            false, true, false, false, "ffffffff", "ffffffff", 0, 255, "Linear", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Fade Out Lento (16 Q)", "Dimmer", "Base", 16.0, true, "Sync", fullStageLights,
            false, true, false, false, "ffffffff", "ffffffff", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Crescendo Build-Up (8 Q)", "Dimmer", "Base", 8.0, true, "Sync", fullStageLights,
            false, true, false, false, "fff59e0b", "fff59e0b", 15, 255, "Linear", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Respiro Atmosferico (4 Bars / 16 Q)", "Dimmer", "Base", 16.0, false, "Sync", backAndFloor,
            false, true, false, false, "ff38bdf8", "ff1d4ed8", 35, 240, "Sine_4Bars", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        addStarter("Pulsazione Media (2 Bars / 8 Q)", "Dimmer", "Base", 8.0, false, "Sync", fullStageLights,
            false, true, false, false, "ff38bdf8", "ff1d4ed8", 25, 255, "Sine_2Bars", false, "1/8", "Fixed", "Sine_2Bars", 0, 0);

        addStarter("Chase Pari/Dispari (1 Bar / 4 Q)", "Dimmer", "Base", 4.0, false, "OddEven", airGroup,
            false, true, false, false, "fff59e0b", "ffdc2626", 15, 255, "Sine_1Bar", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Onda Dimmer SX -> DX (2 Bars)", "Dimmer", "Base", 8.0, false, "LeftToRight", fullStageLights,
            false, true, false, false, "ff38bdf8", "ff38bdf8", 0, 255, "Sine_2Bars", false, "1/8", "Fixed", "Sine_2Bars", 0, 0);

        // =====================================================================
        // 3. MOVEMENT POOL (10 preset — solo Pan/Tilt, mai override Colore/Dimmer)
        // =====================================================================
        addStarter("Posizione Home (Centro Fisso)", "Movement", "Movement", 4.0, false, "Sync", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);

        auto addStaticPos = [&](const juce::String& name, int panC, int tiltC, int fanP, int fanT)
            {
                const int before = presetLibNode.getNumChildren();
                addStarter(name, "Movement", "Movement", 4.0, false, "SymmetricalFan", allMovers,
                    false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Fixed", "Sine_4Bars", 0, 0);
                if (presetLibNode.getNumChildren() > before)
                {
                    auto p = presetLibNode.getChild(presetLibNode.getNumChildren() - 1);
                    p.setProperty(StateKeys::PresetMovePanCenter, panC, nullptr);
                    p.setProperty(StateKeys::PresetMoveTiltCenter, tiltC, nullptr);
                    p.setProperty(StateKeys::PresetMovePanFan, fanP, nullptr);
                    p.setProperty(StateKeys::PresetMoveTiltFan, fanT, nullptr);
                    rebuildPresetCommands(p);
                }
            };

        addStaticPos("Posizione: Ventaglio Aperto (<>)", 0, 20, 70, 0);
        addStaticPos("Posizione: Incrocio X (><)", 0, 15, -75, 0);
        addStaticPos("Posizione: Focus Centro Band", 0, -45, -35, 0);
        addStaticPos("Posizione: Pubblico (Crowd)", 0, 75, 45, 0);

        addStarter("Sweep Lento Ventaglio (4 Bars / 16 Q)", "Movement", "Movement", 16.0, false, "SymmetricalFan", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "PanSweep", "Sine_4Bars", 85, 0);

        addStarter("Sweep Pan Veloce Sync (1 Bar)", "Movement", "Movement", 4.0, false, "Sync", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "PanSweep", "Sine_1Bar", 70, 0);

        addStarter("Tilt Wave Verticale (2 Bars)", "Movement", "Movement", 8.0, false, "Sync", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "TiltWave", "Sine_2Bars", 0, 75);

        addStarter("Tilt Wave Onda SX -> DX (2 Bars)", "Movement", "Movement", 8.0, false, "LeftToRight", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "TiltWave", "Sine_2Bars", 0, 80);

        addStarter("Tilt Alternato Pari/Dispari (1 Bar)", "Movement", "Movement", 4.0, false, "OddEven", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "TiltWave", "Sine_1Bar", 0, 70);

        addStarter("Cerchio Ampio Lento (4 Bars)", "Movement", "Movement", 16.0, false, "SymmetricalFan", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Circle", "Sine_4Bars", 85, 75);

        addStarter("Cerchio Ipnotico (2 Bars / 8 Q)", "Movement", "Movement", 8.0, false, "SymmetricalFan", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Circle", "Sine_2Bars", 70, 65);

        addStarter("Figure-8 Ventaglio (4 Bars)", "Movement", "Movement", 16.0, false, "SymmetricalFan", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Figure8", "Sine_4Bars", 85, 70);

        addStarter("Figure-8 Onda SX -> DX (2 Bars)", "Movement", "Movement", 8.0, false, "LeftToRight", allMovers,
            false, false, true, false, "ff38bdf8", "ff38bdf8", 0, 255, "Static", false, "1/8", "Figure8", "Sine_2Bars", 75, 60);

        // =====================================================================
        // 4. STROBE POOL (7 preset — 6 trasparenti sul colore attivo + 1 Bianco)
        // =====================================================================
        addStarter("Strobo 1/8 (Colore Attivo - Full Rig)", "Strobe", "OverlayFX", 4.0, false, "Sync", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_8", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo 1/8T Terzine (Colore Attivo)", "Strobe", "OverlayFX", 4.0, false, "Sync", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_8T", false, "1/8T", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo 1/16 Blast (Colore Attivo)", "Strobe", "OverlayFX", 4.0, false, "Sync", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_16", false, "1/16", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo 1/16T Sestine (Colore Attivo)", "Strobe", "OverlayFX", 4.0, false, "Sync", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_16T", false, "1/16T", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo Alternato Pari/Dispari (1/8)", "Strobe", "OverlayFX", 4.0, false, "OddEven", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_8", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo Onda SX -> DX (1/16)", "Strobe", "OverlayFX", 4.0, false, "LeftToRight", fullStageLights,
            false, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_16", false, "1/16", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Strobo Bianco Impatto (1/16)", "Strobe", "OverlayFX", 4.0, false, "Sync", strobeRig,
            true, false, false, true, "ffffffff", "ffffffff", 0, 255, "Square_1_16", true, "1/16", "Fixed", "Sine_1Bar", 0, 0);

        // =====================================================================
        // 5. IMPACT FX & ATMOSPHERE POOL (9 preset — Blinder, Flash, Geyser, Hazer)
        // =====================================================================
        addStarter("Blinder Hit Rapido (2 Q)", "ImpactFX", "OverlayFX", 2.0, true, "Sync", blinder,
            true, true, false, false, "ffffedd5", "ffffedd5", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Blinder Decay Impatto (4 Q)", "ImpactFX", "OverlayFX", 4.0, true, "Sync", blinder,
            true, true, false, false, "ffffedd5", "ffffedd5", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Blinder Full Hold (4 Q)", "ImpactFX", "OverlayFX", 4.0, false, "Sync", blinder,
            true, true, false, false, "ffffedd5", "ffffedd5", 255, 255, "Static", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Blinder Crescendo (8 Q)", "ImpactFX", "OverlayFX", 8.0, true, "Sync", blinder,
            true, true, false, false, "ffffedd5", "ffffedd5", 0, 255, "Linear", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Flash Impatto Full-Rig (2 Q)", "ImpactFX", "OverlayFX", 2.0, true, "Sync", fullStageLights,
            true, true, false, false, "ffffffff", "ffffffff", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Geyser Colpo Secco Bianco (2 Q)", "ImpactFX", "OverlayFX", 2.0, true, "Sync", geyser,
            true, true, false, false, "ffe0f2fe", "ffe0f2fe", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Geyser Drop Rosso (4 Q)", "ImpactFX", "OverlayFX", 4.0, true, "Sync", geyser,
            true, true, false, false, "ffdc2626", "ffdc2626", 0, 255, "Decay", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Hazer Mantenimento Leggero (35%)", "ImpactFX", "OverlayFX", 8.0, false, "Sync", hazer,
            false, true, false, false, "ffcbd5e1", "ffcbd5e1", 90, 90, "Static", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        addStarter("Pre-Carica Nebbia Hazer (75%)", "ImpactFX", "OverlayFX", 8.0, false, "Sync", hazer,
            false, true, false, false, "ffcbd5e1", "ffcbd5e1", 190, 190, "Static", false, "1/8", "Fixed", "Sine_1Bar", 0, 0);

        return addedCount;
    }

    void createNewPreset()
    {
        juce::String pool = (activePoolFilter == "Tutti") ? "Color" : activePoolFilter;
        juce::ValueTree p(StateKeys::ScenePreset);

        ensurePresetId(p);

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

        p.setProperty(StateKeys::PresetUseColor, (pool == "Color" || pool == "ImpactFX"), nullptr);
        p.setProperty(StateKeys::PresetUseDimmer, (pool == "Dimmer" || pool == "ImpactFX"), nullptr);
        p.setProperty(StateKeys::PresetUseMove, (pool == "Movement"), nullptr);
        p.setProperty(StateKeys::PresetUseStrobe, (pool == "Strobe"), nullptr);

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
        copy.setProperty(StateKeys::PresetId, juce::Uuid().toString(), nullptr);
        copy.removeProperty(StateKeys::PresetAliases, nullptr);
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
        juce::Component::SafePointer<SceneBuilderTab> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            auto snapshot = safeThis->presetLibNode.createCopy();
            const int count = snapshot.getNumChildren();

            FileProgressRunner::runBackground(
                "Salvataggio Libreria Preset",
                "Preparazione libreria (" + juce::String(count) + " preset)...",
                [safeThis, snapshot, file](std::function<void(double, const juce::String&)> report) -> bool {
                    report(0.25, "Serializzazione XML...");
                    auto xml = snapshot.createXml();
                    if (xml == nullptr) return false;

                    report(0.70, "Scrittura su disco: " + file.getFileName() + "...");
                    const bool ok = xml->writeTo(file);
                    if (ok)
                    {
                        juce::MessageManager::callAsync([safeThis]() {
                            if (safeThis != nullptr)
                                safeThis->savePresetLibraryToDefaultDisk();
                            });
                    }
                    return ok;
                },
                "Salvataggio Completato",
                "Libreria '" + file.getFileName() + "' salvata correttamente (" + juce::String(count) + " preset).",
                "Errore Salvataggio",
                "Impossibile salvare la libreria in:\n" + file.getFullPathName());
            });
    }

    void loadPresetLibrary()
    {
        juce::ScopedValueSetter<bool> suppressGuard(isSuppressingShowSync, true);

        fileChooser = std::make_unique<juce::FileChooser>(
            "Carica Libreria Preset Scene Builder",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.presets");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        juce::Component::SafePointer<SceneBuilderTab> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto parsedTree = std::make_shared<juce::ValueTree>();
            auto loadedCount = std::make_shared<int>(0);

            std::vector<FileProgressRunner::Step> steps;
            steps.push_back({ "Lettura file " + file.getFileName() + "...", 0.35, [file, parsedTree]() {
                if (auto xml = juce::XmlDocument::parse(file))
                {
                    *parsedTree = juce::ValueTree::fromXml(*xml);
                    return parsedTree->isValid() && parsedTree->hasType(StateKeys::PresetLibNode);
                }
                return false;
            } });
            steps.push_back({ "Importazione e verifica attributi preset...", 0.80, [safeThis, parsedTree, loadedCount]() {
                if (safeThis == nullptr) return false;
                safeThis->presetLibNode.removeAllChildren(nullptr);
                for (int i = 0; i < parsedTree->getNumChildren(); ++i)
                {
                    auto copy = parsedTree->getChild(i).createCopy();
                    sanitizePresetNode(copy);
                    safeThis->presetLibNode.addChild(copy, -1, nullptr);
                }
                *loadedCount = safeThis->presetLibNode.getNumChildren();
                safeThis->savePresetLibraryToDefaultDisk();
                return true;
            } });
            steps.push_back({ "Aggiornamento interfaccia...", 1.0, [safeThis]() {
                if (safeThis == nullptr) return false;
                safeThis->rebuildFilteredList();
                if (!safeThis->filteredPresets.empty())
                    safeThis->presetList.selectRow(0);
                return true;
            } });

            FileProgressRunner::runStepped(
                "Caricamento Libreria Preset",
                std::move(steps),
                "Caricamento Completato",
                [file, loadedCount]() {
                    return "Libreria '" + file.getFileName() + "' caricata correttamente ("
                        + juce::String(*loadedCount) + " preset).";
                },
                "Errore Caricamento",
                "Il file selezionato non e' una libreria preset valida.");
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
    juce::ToggleButton useColorToggle, useDimmerToggle, useMoveToggle, useStrobeToggle;
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

    PositionAimPad positionAimPad;
    juce::Label panCenterLbl, tiltCenterLbl, panFanLbl, tiltFanLbl;
    juce::Slider panCenterSlider, tiltCenterSlider, panFanSlider, tiltFanSlider;
    std::vector<std::unique_ptr<juce::TextButton>> quickPosBtns;

    // Colonna Destra
    juce::TextButton playPreviewBtn, triggerOneShotBtn;
    juce::Label bpmLabel, strobeHzInfoLabel;
    juce::Slider bpmSlider;
    juce::ComboBox meterCombo;

    StageSimulatorView previewSimulator;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton addPresetBtn, dupPresetBtn, delPresetBtn, factoryPresetsBtn, saveLibBtn, loadLibBtn;

    bool isSuppressingShowSync = false;
    juce::ValueTree pendingSyncPreset;
    bool pendingSyncDuration = false;
    juce::String selectedPresetLastSyncedName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneBuilderTab)
};