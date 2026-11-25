#include "MainComponent.h"

MainComponent::MainComponent()
    : tabbedComponent(juce::TabbedButtonBar::TabsAtTop)
{
    tabbedComponent.setTabBarDepth(40);

    timeMetricBeatMap.bindToAppState(mainState);
    timeMetricTab.refreshTables();

    tabbedComponent.addTab("1. Venue Patch",
        juce::Colour(0xff2a2a32),
        &venueTab,
        false);

    tabbedComponent.addTab("2. Scene Builder",
        juce::Colour(0xff1e1e26),
        &sceneBuilderTab,
        false);

    tabbedComponent.addTab("3. Pattern Creator",
        juce::Colour(0xff2a2a32),
        &showBuilderTab,
        false);

    tabbedComponent.addTab("4. Time & Metric",
        juce::Colour(0xff1e1e26),
        &timeMetricTab,
        false);

    tabbedComponent.addTab("5. MIDI Export",
        juce::Colour(0xff2a2a32),
        &exportTab,
        false);

    tabbedComponent.addTab("6. DMX Live",
        juce::Colour(0xff1e202b),
        &dmxControlPanel,
        false);

    venueTab.setDmxLiveEngine(&dmxLiveEngine);
    sceneBuilderTab.setDmxLiveEngine(&dmxLiveEngine);
    showBuilderTab.setDmxLiveEngine(&dmxLiveEngine);

    tabbedComponent.onTabChanged = [this](int prevTab, int newTab)
        {
            // Indici tab: 0 = Venue Patch, 1 = Scene Builder, 2 = Pattern Creator, 3 = Time, 4 = MIDI, 5 = DMX Live
            const bool allowsFixtureTest = (newTab == 0 || newTab == 5);
            if (!allowsFixtureTest)
                venueTab.forceStopFixtureTest();


            // Consente all'anteprima della Tab 2 di restare attiva in Tab 6 SOLO se arrivi dalla Tab 2
            const bool allowTab2Dmx = (newTab == 1) || (newTab == 5 && prevTab == 1);
            sceneBuilderTab.setAllowBackgroundDmx(allowTab2Dmx);

            if (prevTab == 1 && !allowTab2Dmx)
                dmxLiveEngine.clearAllOutput();
        };

    addAndMakeVisible(tabbedComponent);

    setWantsKeyboardFocus(true);

    undoBtn.setButtonText("< Undo");
    undoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff262936));
    undoBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    undoBtn.setWantsKeyboardFocus(false);
    undoBtn.onClick = [this]() { undoManager.undo(); };
    addAndMakeVisible(undoBtn);

    redoBtn.setButtonText("Redo >");
    redoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff262936));
    redoBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    redoBtn.setWantsKeyboardFocus(false);
    redoBtn.onClick = [this]() { undoManager.redo(); };
    addAndMakeVisible(redoBtn);

    setSize(1600, 950);

    venueTab.onVenueClean = [this]() {
        captureCleanSnapshotForTab(0);
        markTabDirty(0, false);
        undoManager.commitPendingNow("Tab 1 - Venue Patch");
        };
    sceneBuilderTab.onPresetLibraryClean = [this]() {
        captureCleanSnapshotForTab(1);
        markTabDirty(1, false);
        undoManager.commitPendingNow("Tab 2 - Scene Builder");
        };
    showBuilderTab.onShowClean = [this]() {
        captureCleanSnapshotForTab(2);
        markTabDirty(2, false);
        undoManager.commitPendingNow("Tab 3 - Pattern Creator");
        };
    dmxControlPanel.onDirtyStateChanged = [this](bool d) { markTabDirty(5, d); };

    undoManager.onHistoryChanged = [this]() { updateUndoRedoButtons(); };
    undoManager.onStateRestored = [this](const StateUndoManager::RestoreResult& res)
        {
            if (res.venueChanged)
            {
                venueTab.refreshAfterUndoRedo();
                auto v = mainState.getChildWithName(StateKeys::VenueNode);
                markTabDirty(0, !v.isEquivalentTo(cleanVenueSnap));
            }
            if (res.presetLibChanged)
            {
                sceneBuilderTab.refreshAfterUndoRedo();
                auto p = mainState.getChildWithName(StateKeys::PresetLibNode);
                markTabDirty(1, !p.isEquivalentTo(cleanPresetLibSnap));
            }
            if (res.showChanged || res.otherChanged)
            {
                showBuilderTab.refreshAfterUndoRedo(res.otherChanged);
                if (res.otherChanged)
                {
                    timeMetricBeatMap.bindToAppState(mainState);
                    timeMetricTab.refreshTables();
                }
                markTabDirty(2, !makeShowSubtreeSnapshot().isEquivalentTo(cleanShowSnap));
            }
        };

    captureCleanSnapshotForTab(0);
    captureCleanSnapshotForTab(1);
    captureCleanSnapshotForTab(2);
    undoManager.resetHistory();
    updateUndoRedoButtons();

    dirtyTab1 = false;
    dirtyTab2 = false;
    dirtyTab3 = false;
    dirtyTab6 = dmxControlPanel.hasUnsavedChanges();
    refreshTabTitles();

    mainState.addListener(this);
}

MainComponent::~MainComponent()
{
    quitOverlay.reset();
    mainState.removeListener(this);
    tabbedComponent.clearTabs();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff141417));
}

void MainComponent::resized()
{
    tabbedComponent.setBounds(getLocalBounds());

    auto topRightBar = getLocalBounds().removeFromTop(40).removeFromRight(186).reduced(6, 6);
    undoBtn.setBounds(topRightBar.removeFromLeft(84));
    topRightBar.removeFromLeft(6);
    redoBtn.setBounds(topRightBar);

    if (quitOverlay != nullptr)
        quitOverlay->setBounds(getLocalBounds());
}

namespace
{
    class UnsavedChangesContent : public juce::Component
    {
    public:
        UnsavedChangesContent(bool t1, bool t2, bool t3, bool t6,
            std::function<void(bool, bool, bool, bool)> onSaveAndQuit,
            std::function<void()> onQuitWithoutSaving,
            std::function<void()> onCancel)
            : saveCallback(std::move(onSaveAndQuit)),
            discardCallback(std::move(onQuitWithoutSaving)),
            cancelCallback(std::move(onCancel))
        {
            setWantsKeyboardFocus(true);

            headerTitleLbl.setText("Modifiche non salvate", juce::dontSendNotification);
            headerTitleLbl.setFont(juce::FontOptions(14.5f, juce::Font::bold));
            headerTitleLbl.setColour(juce::Label::textColourId, juce::Colours::white);
            headerTitleLbl.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(headerTitleLbl);

            closeXBtn.setButtonText("X");
            closeXBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            closeXBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff991b1b));
            closeXBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffef4444));
            closeXBtn.onClick = [this]() {
                if (cancelCallback) cancelCallback();
                };
            addAndMakeVisible(closeXBtn);

            titleLbl.setText("Ci sono modifiche non salvate", juce::dontSendNotification);
            titleLbl.setFont(juce::FontOptions(16.0f, juce::Font::bold));
            titleLbl.setColour(juce::Label::textColourId, juce::Colour(0xfffef08a));
            addAndMakeVisible(titleLbl);

            subtitleLbl.setText("Seleziona le sezioni da salvare prima di chiudere il programma:", juce::dontSendNotification);
            subtitleLbl.setFont(juce::FontOptions(12.5f));
            subtitleLbl.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));
            addAndMakeVisible(subtitleLbl);

            auto setupCheck = [this](juce::ToggleButton& tb, const juce::String& text, bool show)
                {
                    tb.setButtonText(text);
                    tb.setToggleState(show, juce::dontSendNotification);
                    tb.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
                    tb.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff38bdf8));
                    tb.onClick = [this]() { updateSaveButtonState(); };
                    addChildComponent(tb);
                    tb.setVisible(show);
                };

            setupCheck(cbTab1, "Tab 1 - Venue Patch (Configurazione Palco e Fari)", t1);
            setupCheck(cbTab2, "Tab 2 - Scene Builder (Libreria Preset)", t2);
            setupCheck(cbTab3, "Tab 3 - Pattern Creator (Timeline e Show)", t3);
            setupCheck(cbTab6, "Tab 6 - DMX Live (Configurazione Interfaccia)", t6);

            saveAndQuitBtn.setButtonText("Salva e chiudi");
            saveAndQuitBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f766e));
            saveAndQuitBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            saveAndQuitBtn.onClick = [this]() {
                if (saveCallback)
                    saveCallback(cbTab1.isVisible() && cbTab1.getToggleState(),
                        cbTab2.isVisible() && cbTab2.getToggleState(),
                        cbTab3.isVisible() && cbTab3.getToggleState(),
                        cbTab6.isVisible() && cbTab6.getToggleState());
                };
            addAndMakeVisible(saveAndQuitBtn);

            quitWithoutSaveBtn.setButtonText("Chiudi senza salvare");
            quitWithoutSaveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff991b1b));
            quitWithoutSaveBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            quitWithoutSaveBtn.onClick = [this]() {
                if (discardCallback) discardCallback();
                };
            addAndMakeVisible(quitWithoutSaveBtn);

            cancelBtn.setButtonText("Annulla");
            cancelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff334155));
            cancelBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
            cancelBtn.onClick = [this]() {
                if (cancelCallback) cancelCallback();
                };
            addAndMakeVisible(cancelBtn);

            updateSaveButtonState();

            const int visibleChecks = (t1 ? 1 : 0) + (t2 ? 1 : 0) + (t3 ? 1 : 0) + (t6 ? 1 : 0);
            const int totalHeight = 198 + juce::jmax(1, visibleChecks) * 34;
            setSize(550, totalHeight);
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();

            // Corpo card arrotondato
            g.setColour(juce::Colour(0xff14151c));
            g.fillRoundedRectangle(bounds, 8.0f);

            // Barra titolo integrata
            {
                juce::Graphics::ScopedSaveState ss(g);
                juce::Path clip;
                clip.addRoundedRectangle(bounds, 8.0f);
                g.reduceClipRegion(clip);

                g.setColour(juce::Colour(0xff1e202b));
                g.fillRect(headerRect);
                g.setColour(juce::Colour(0xff2c3042));
                g.fillRect(headerRect.getX(), headerRect.getBottom() - 1, headerRect.getWidth(), 1);
            }

            // Riquadro checkbox
            if (!checksCardRect.isEmpty())
            {
                g.setColour(juce::Colour(0xff1b1d28));
                g.fillRoundedRectangle(checksCardRect.toFloat(), 6.0f);
                g.setColour(juce::Colour(0xff2c3042));
                g.drawRoundedRectangle(checksCardRect.toFloat(), 6.0f, 1.0f);
            }

            // Bordo esterno della card
            g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.35f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.2f);
        }

        void resized() override
        {
            auto b = getLocalBounds();

            headerRect = b.removeFromTop(34);
            auto hdr = headerRect.reduced(6, 3);
            closeXBtn.setBounds(hdr.removeFromRight(28));
            hdr.removeFromLeft(28); // Bilancia la centratura del titolo
            headerTitleLbl.setBounds(hdr);

            b = b.reduced(18, 16);

            auto btnRow = b.removeFromBottom(36);
            b.removeFromBottom(16);

            saveAndQuitBtn.setBounds(btnRow.removeFromLeft(172));
            btnRow.removeFromLeft(10);
            quitWithoutSaveBtn.setBounds(btnRow.removeFromLeft(198));
            btnRow.removeFromLeft(10);
            cancelBtn.setBounds(btnRow);

            titleLbl.setBounds(b.removeFromTop(24));
            b.removeFromTop(4);
            subtitleLbl.setBounds(b.removeFromTop(18));
            b.removeFromTop(12);

            checksCardRect = b;
            auto inner = checksCardRect.reduced(14, 10);
            for (auto* cb : { &cbTab1, &cbTab2, &cbTab3, &cbTab6 })
            {
                if (cb->isVisible())
                {
                    cb->setBounds(inner.removeFromTop(28));
                    inner.removeFromTop(6);
                }
            }
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                if (cancelCallback) cancelCallback();
                return true;
            }
            if (key == juce::KeyPress::returnKey && saveAndQuitBtn.isEnabled())
            {
                saveAndQuitBtn.triggerClick();
                return true;
            }
            return false;
        }

    private:
        void updateSaveButtonState()
        {
            const bool anyChecked = (cbTab1.isVisible() && cbTab1.getToggleState())
                || (cbTab2.isVisible() && cbTab2.getToggleState())
                || (cbTab3.isVisible() && cbTab3.getToggleState())
                || (cbTab6.isVisible() && cbTab6.getToggleState());

            saveAndQuitBtn.setEnabled(anyChecked);
            saveAndQuitBtn.setAlpha(anyChecked ? 1.0f : 0.45f);
        }

        juce::Rectangle<int> headerRect;
        juce::Rectangle<int> checksCardRect;
        juce::Label headerTitleLbl, titleLbl, subtitleLbl;
        juce::TextButton closeXBtn;
        juce::ToggleButton cbTab1, cbTab2, cbTab3, cbTab6;
        juce::TextButton saveAndQuitBtn, quitWithoutSaveBtn, cancelBtn;

        std::function<void(bool, bool, bool, bool)> saveCallback;
        std::function<void()> discardCallback;
        std::function<void()> cancelCallback;
    };

    class UnsavedChangesOverlay : public juce::Component
    {
    public:
        UnsavedChangesOverlay(juce::Component& backgroundToBlur,
            bool t1, bool t2, bool t3, bool t6,
            std::function<void(bool, bool, bool, bool)> onSaveAndQuit,
            std::function<void()> onQuitWithoutSaving,
            std::function<void()> onCancel)
            : card(t1, t2, t3, t6,
                std::move(onSaveAndQuit),
                std::move(onQuitWithoutSaving),
                std::move(onCancel)),
            cancelAction(onCancel)
        {
            setInterceptsMouseClicks(true, true);
            setWantsKeyboardFocus(true);

            // Cattura uno snapshot ridotto (0.25x) e applica un blur gaussiano veloce
            if (backgroundToBlur.getWidth() > 16 && backgroundToBlur.getHeight() > 16)
            {
                auto rawSnap = backgroundToBlur.createComponentSnapshot(
                    backgroundToBlur.getLocalBounds(), true, 0.25f);

                if (rawSnap.isValid() && rawSnap.getWidth() > 4 && rawSnap.getHeight() > 4)
                {
                    juce::ImageConvolutionKernel blurKernel(7);
                    blurKernel.createGaussianBlur(2.2f);
                    blurredBackdrop = juce::Image(rawSnap.getFormat(),
                        rawSnap.getWidth(), rawSnap.getHeight(), true);
                    blurKernel.applyToImage(blurredBackdrop, rawSnap, blurredBackdrop.getBounds());
                }
            }

            addAndMakeVisible(card);
        }

        void paint(juce::Graphics& g) override
        {
            // 1. Sfondo sfocato dell'UI sottostante
            if (blurredBackdrop.isValid())
            {
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                g.drawImage(blurredBackdrop, getLocalBounds().toFloat(),
                    juce::RectanglePlacement::stretchToFit);
            }

            // 2. Velo scuro semitrasparente per far risaltare il riquadro
            g.setColour(juce::Colour(0xff090a10).withAlpha(0.62f));
            g.fillRect(getLocalBounds());

            // 3. Ombra morbida sotto la card centrale
            juce::DropShadow shadow(juce::Colours::black.withAlpha(0.75f), 34, { 0, 10 });
            shadow.drawForRectangle(g, card.getBounds());
        }

        void resized() override
        {
            const int cw = card.getWidth();
            const int ch = card.getHeight();
            card.setBounds((getWidth() - cw) / 2, (getHeight() - ch) / 2, cw, ch);
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            return card.keyPressed(key);
        }

    private:
        juce::Image blurredBackdrop;
        UnsavedChangesContent card;
        std::function<void()> cancelAction;
    };
}

void MainComponent::markTabDirty(int tabIndex, bool isDirty)
{
    bool* flag = nullptr;
    if (tabIndex == 0) flag = &dirtyTab1;
    else if (tabIndex == 1) flag = &dirtyTab2;
    else if (tabIndex == 2) flag = &dirtyTab3;
    else if (tabIndex == 5) flag = &dirtyTab6;

    if (flag != nullptr && *flag != isDirty)
    {
        *flag = isDirty;
        refreshTabTitles();
    }
}

void MainComponent::refreshTabTitles()
{
    tabbedComponent.setTabName(0, dirtyTab1 ? "1. Venue Patch *" : "1. Venue Patch");
    tabbedComponent.setTabName(1, dirtyTab2 ? "2. Scene Builder *" : "2. Scene Builder");
    tabbedComponent.setTabName(2, dirtyTab3 ? "3. Pattern Creator *" : "3. Pattern Creator");
    tabbedComponent.setTabName(5, dirtyTab6 ? "6. DMX Live *" : "6. DMX Live");
    tabbedComponent.getTabbedButtonBar().repaint();
}

void MainComponent::handleStateTreeModified(const juce::ValueTree& tree)
{
    if (!tree.isValid() || undoManager.isRestoring())
        return;

    juce::ValueTree top = tree;
    while (top.getParent().isValid() && top.getParent() != mainState)
        top = top.getParent();

    if (top == mainState)
        return;

    const int activeTab = tabbedComponent.getCurrentTabIndex();

    if (top.hasType(StateKeys::VenueNode))
    {
        markTabDirty(0, true);
        undoManager.notifyStateChanged("Tab 1 - Venue Patch");
    }
    else if (top.hasType(StateKeys::PresetLibNode))
    {
        markTabDirty(1, true);
        undoManager.notifyStateChanged("Tab 2 - Scene Builder");
    }
    else if (top.hasType(StateKeys::ShowNode))
    {
        if (top.getNumChildren() > 0 || activeTab == 2)
        {
            markTabDirty(2, true);
            undoManager.notifyStateChanged("Tab 3 - Pattern Creator");
        }
    }
    else if (activeTab == 2 || activeTab == 3)
    {
        markTabDirty(2, true);
        undoManager.notifyStateChanged(activeTab == 3 ? "Tab 4 - Time & Metric" : "Tab 3 - Pattern Creator");
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (isQuitPromptOpen)
        return false;

    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const int code = key.getKeyCode();
        if ((code == 'z' || code == 'Z') && !mods.isShiftDown())
        {
            undoManager.undo();
            return true;
        }
        if (code == 'y' || code == 'Y' || ((code == 'z' || code == 'Z') && mods.isShiftDown()))
        {
            undoManager.redo();
            return true;
        }
    }

    return false;
}

void MainComponent::updateUndoRedoButtons()
{
    const bool canU = !isQuitPromptOpen && undoManager.canUndo();
    const bool canR = !isQuitPromptOpen && undoManager.canRedo();

    undoBtn.setEnabled(canU);
    undoBtn.setAlpha(canU ? 1.0f : 0.40f);
    undoBtn.setTooltip(canU
        ? ("Annulla: " + undoManager.getUndoDescription() + " (Ctrl+Z)")
        : "Nessuna azione da annullare (Ctrl+Z)");

    redoBtn.setEnabled(canR);
    redoBtn.setAlpha(canR ? 1.0f : 0.40f);
    redoBtn.setTooltip(canR
        ? ("Ripristina: " + undoManager.getRedoDescription() + " (Ctrl+Y / Ctrl+Shift+Z)")
        : "Nessuna azione da ripristinare (Ctrl+Y)");
}

void MainComponent::captureCleanSnapshotForTab(int tabIndex)
{
    if (tabIndex == 0)
    {
        auto v = mainState.getChildWithName(StateKeys::VenueNode);
        cleanVenueSnap = v.isValid() ? v.createCopy() : juce::ValueTree(StateKeys::VenueNode);
    }
    else if (tabIndex == 1)
    {
        auto p = mainState.getChildWithName(StateKeys::PresetLibNode);
        cleanPresetLibSnap = p.isValid() ? p.createCopy() : juce::ValueTree(StateKeys::PresetLibNode);
    }
    else if (tabIndex == 2)
    {
        cleanShowSnap = makeShowSubtreeSnapshot();
    }
}

juce::ValueTree MainComponent::makeShowSubtreeSnapshot() const
{
    juce::ValueTree snap("SHOW_SUBTREE_SNAP");
    for (int i = 0; i < mainState.getNumChildren(); ++i)
    {
        auto ch = mainState.getChild(i);
        if (!ch.hasType(StateKeys::VenueNode) && !ch.hasType(StateKeys::PresetLibNode))
            snap.addChild(ch.createCopy(), -1, nullptr);
    }
    return snap;
}

void MainComponent::requestApplicationQuit(std::function<void()> onReadyToQuit)
{
    dirtyTab6 = dirtyTab6 || dmxControlPanel.hasUnsavedChanges();

    if (!dirtyTab1 && !dirtyTab2 && !dirtyTab3 && !dirtyTab6)
    {
        dmxLiveEngine.stopOutput(true);
        if (onReadyToQuit) onReadyToQuit();
        return;
    }

    if (isQuitPromptOpen && quitOverlay != nullptr)
    {
        quitOverlay->toFront(true);
        quitOverlay->grabKeyboardFocus();
        return;
    }

    isQuitPromptOpen = true;
    juce::Component::SafePointer<MainComponent> safeThis(this);

    auto hideOverlayNow = [safeThis]() {
        if (safeThis != nullptr && safeThis->quitOverlay != nullptr)
            safeThis->quitOverlay->setVisible(false);
        };

    auto cancelOverlayAsync = [safeThis, hideOverlayNow]() {
        hideOverlayNow();
        juce::MessageManager::callAsync([safeThis]() {
            if (safeThis == nullptr) return;
            safeThis->quitOverlay.reset();
            safeThis->tabbedComponent.setEnabled(true);
            safeThis->isQuitPromptOpen = false;
            safeThis->updateUndoRedoButtons();
            });
        };

    // Cattura lo snapshot PRIMA di disabilitare tabbedComponent, poi blocca l'UI sottostante
    quitOverlay = std::make_unique<UnsavedChangesOverlay>(
        tabbedComponent,
        dirtyTab1, dirtyTab2, dirtyTab3, dirtyTab6,
        // 1. Salva e chiudi
        [safeThis, hideOverlayNow, onReadyToQuit](bool s1, bool s2, bool s3, bool s6) {
            hideOverlayNow();
            juce::MessageManager::callAsync([safeThis, onReadyToQuit, s1, s2, s3, s6]() {
                if (safeThis == nullptr) return;
                safeThis->quitOverlay.reset();
                safeThis->saveSelectedTabsAndQuit(s1, s2, s3, s6, onReadyToQuit);
                });
        },
        // 2. Chiudi senza salvare
        [safeThis, hideOverlayNow, onReadyToQuit]() {
            hideOverlayNow();
            juce::MessageManager::callAsync([safeThis, onReadyToQuit]() {
                if (safeThis == nullptr) return;
                safeThis->quitOverlay.reset();
                safeThis->isQuitPromptOpen = false;
                safeThis->dmxLiveEngine.stopOutput(true);
                if (onReadyToQuit) onReadyToQuit();
                });
        },
        // 3. Annulla (o X / ESC)
        cancelOverlayAsync
    );

    tabbedComponent.setEnabled(false);

    updateUndoRedoButtons();

    addAndMakeVisible(*quitOverlay);
    quitOverlay->setBounds(getLocalBounds());
    quitOverlay->toFront(true);
    quitOverlay->grabKeyboardFocus();
}

void MainComponent::saveSelectedTabsAndQuit(bool saveT1, bool saveT2, bool saveT3, bool saveT6,
    std::function<void()> onReadyToQuit)
{
    if (saveT6)
        dmxControlPanel.saveSettingsToDisk();

    if (saveT2)
        sceneBuilderTab.savePresetLibraryForExit();

    juce::Component::SafePointer<MainComponent> safeThis(this);

    auto restoreOnCancel = [safeThis]() {
        if (safeThis == nullptr) return;
        safeThis->tabbedComponent.setEnabled(true);
        safeThis->isQuitPromptOpen = false;
        safeThis->updateUndoRedoButtons();
        };

    auto finalizeQuit = [safeThis, onReadyToQuit]() {
        if (safeThis == nullptr) return;
        safeThis->isQuitPromptOpen = false;
        safeThis->dmxLiveEngine.stopOutput(true);
        if (onReadyToQuit) onReadyToQuit();
        };

    auto stepTab3 = [safeThis, saveT3, finalizeQuit, restoreOnCancel]() {
        if (safeThis == nullptr) return;
        if (saveT3)
        {
            safeThis->showBuilderTab.saveShowForExitAsync([safeThis, finalizeQuit, restoreOnCancel](bool ok) {
                if (safeThis == nullptr) return;
                if (ok)
                    finalizeQuit();
                else
                    restoreOnCancel();
                });
        }
        else
        {
            finalizeQuit();
        }
        };

    if (saveT1)
    {
        venueTab.saveVenueForExitAsync([safeThis, stepTab3, restoreOnCancel](bool ok) {
            if (safeThis == nullptr) return;
            if (ok)
                stepTab3();
            else
                restoreOnCancel();
            });
    }
    else
    {
        stepTab3();
    }
}