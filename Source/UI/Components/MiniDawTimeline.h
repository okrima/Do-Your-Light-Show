#pragma once
#include <JuceHeader.h>
#include "../../Audio/StemAudioEngine.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/DataStateKeys.h"
#include "TimeMetricManager.h"
#include "../../Engine/DawTransportSync.h"
#include <cmath>
#include <vector>
#include <algorithm>

class MiniDawTimeline : public juce::Component,
    private juce::Timer,
    private juce::ChangeListener,
    private juce::ValueTree::Listener,
    private juce::ScrollBar::Listener,
    private juce::KeyListener
{
public:
    std::function<void(int)> onSceneSelectedFromTimeline;
    std::function<void(double)> onBeatPositionChanged;
    std::function<void()> onOpenTimeMetricManager;
    std::function<void(juce::ValueTree)> onSceneDragFinished;
    std::function<void()> onPreferredHeightChanged;

    int getPreferredHeight() const noexcept
    {
        const int visibleLanes = juce::jlimit(1, kMaxVisibleLanes, juce::jmax(1, totalSceneLanes));
        return 125 + visibleLanes * kSceneLaneHeight;
    }

    bool isDraggingAnyScene() const noexcept { return sceneDragMode != SceneDragMode::None; }

    MiniDawTimeline(juce::ValueTree& state, StemAudioEngine& engine, BeatTimeMap& beatMap)
        : appState(state),
        audioEngine(engine),
        beatTimeMap(beatMap),
        thumbnail(512, engine.getFormatManager(), thumbnailCache)
    {
        setWantsKeyboardFocus(true);

        audioEngine.setBeatTimeMap(&beatTimeMap);
        audioEngine.startBackgroundThread();
        thumbnail.addChangeListener(this);
        appState.addListener(this);

        playPauseBtn.setButtonText("Play");
        playPauseBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
        playPauseBtn.setWantsKeyboardFocus(false);
        playPauseBtn.onClick = [this]() { togglePlayPause(); };
        addAndMakeVisible(playPauseBtn);

        stopBtn.setButtonText("Stop");
        stopBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
        stopBtn.setWantsKeyboardFocus(false);
        stopBtn.onClick = [this]() { stopAndRewind(); };
        addAndMakeVisible(stopBtn);

        clickBtn.setButtonText("Click");
        clickBtn.setClickingTogglesState(true);
        clickBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        clickBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd97706));
        clickBtn.setWantsKeyboardFocus(false);
        clickBtn.setTooltip("Attiva/Disattiva il metronomo sincronizzato a BPM e cambi di metrica");
        clickBtn.onClick = [this]() {
            const bool en = clickBtn.getToggleState();
            audioEngine.setMetronomeEnabled(en);
            if (!isUpdatingControls)
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
                if (auto beatNode = appState.getChildWithName(StateKeys::BeatMapNode); beatNode.isValid())
                    beatNode.setProperty(StateKeys::MetronomeEnabled, en, nullptr);
            }
            };
        addAndMakeVisible(clickBtn);

        clickVolSlider.setSliderStyle(juce::Slider::LinearBar);
        clickVolSlider.setRange(0.0, 100.0, 1.0);
        clickVolSlider.setValue(65.0, juce::dontSendNotification);
        clickVolSlider.setTextValueSuffix("%");
        clickVolSlider.setTooltip("Volume metronomo");
        clickVolSlider.onValueChange = [this]() {
            const float g = static_cast<float>(clickVolSlider.getValue() / 100.0);
            audioEngine.setMetronomeGain(g);
            if (!isUpdatingControls)
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
                if (auto beatNode = appState.getChildWithName(StateKeys::BeatMapNode); beatNode.isValid())
                    beatNode.setProperty(StateKeys::MetronomeGain, static_cast<double>(g), nullptr);
            }
            };
        addAndMakeVisible(clickVolSlider);

        snapBtn.setButtonText("Snap");
        snapBtn.setClickingTogglesState(true);
        snapBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        snapBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0284c7));
        snapBtn.setWantsKeyboardFocus(false);
        snapBtn.setTooltip("Magnete cursore: aggancia Playhead/scrubbing alla griglia visibile (fino a 1/128)");
        addAndMakeVisible(snapBtn);

        loadMixBtn.setButtonText("Full Mix...");
        loadMixBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4a3e));
        loadMixBtn.setWantsKeyboardFocus(false);
        loadMixBtn.setTooltip("Carica la traccia audio Full Mix");
        loadMixBtn.onClick = [this]() { promptLoadFullMix(); };
        addAndMakeVisible(loadMixBtn);

        alignBtn.setButtonText("Allinea...");
        alignBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f766e));
        alignBtn.setWantsKeyboardFocus(false);
        alignBtn.setTooltip("Allinea automaticamente il Full Mix alla griglia (o trascina la barra del clip / Alt+Drag)");
        alignBtn.onClick = [this]() { showAlignMenu(); };
        addAndMakeVisible(alignBtn);

        importMidiMapBtn.setButtonText("MIDI Map...");
        importMidiMapBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4c1d95));
        importMidiMapBtn.setWantsKeyboardFocus(false);
        importMidiMapBtn.setTooltip("Importa Tempo Map (BPM) e cambi di Metrica da un file .mid (es. Reaper)");
        importMidiMapBtn.onClick = [this]() { promptImportMidiMap(); };
        addAndMakeVisible(importMidiMapBtn);

        bpmSlider.setSliderStyle(juce::Slider::LinearBar);
        bpmSlider.setRange(40.0, 260.0, 0.5);
        bpmSlider.setValue(beatTimeMap.getBaseBpm(), juce::dontSendNotification);
        bpmSlider.setTextValueSuffix(" BPM");
        bpmSlider.setTooltip("Imposta un BPM fisso (rimuove eventuali cambi BPM della Tempo Map)");
        bpmSlider.onValueChange = [this]() {
            if (isUpdatingControls || dawTransportBridge != nullptr) return;
            juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
            beatTimeMap.setFixedBpm(bpmSlider.getValue(), beatTimeMap.getFirstDownbeatSeconds());
            updateScrollBarRange();
            repaint();
            };
        addAndMakeVisible(bpmSlider);

        meterCombo.addItem("4/4", 1);
        meterCombo.addItem("3/4", 2);
        meterCombo.addItem("6/8", 3);
        meterCombo.addItem("5/4", 4);
        meterCombo.addItem("7/8", 5);
        meterCombo.addItem("12/8", 6);
        meterCombo.addItem("2/4", 7);
        meterCombo.addItem("9/8", 8);
        meterCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff272736));
        meterCombo.setWantsKeyboardFocus(false);
        meterCombo.setTooltip("Metrica base del brano (trascina i marker sul righello o tasto destro per aggiungere/modificare)");
        meterCombo.onChange = [this]() {
            if (isUpdatingControls || dawTransportBridge != nullptr) return;
            int num = 4, den = 4;
            if (meterIdToSignature(meterCombo.getSelectedId(), num, den))
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
                beatTimeMap.addOrUpdateMeterChange(1.0, num, den);
                updateScrollBarRange();
                repaint();
            }
            };
        addAndMakeVisible(meterCombo);

        zoomOutBtn.setButtonText("-");
        zoomOutBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        zoomOutBtn.setWantsKeyboardFocus(false);
        zoomOutBtn.setTooltip("Zoom Out timeline (o rotella mouse giu)");
        zoomOutBtn.onClick = [this]() { zoomAroundCenter(1.0 / 1.6); };
        addAndMakeVisible(zoomOutBtn);

        zoomInBtn.setButtonText("+");
        zoomInBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        zoomInBtn.setWantsKeyboardFocus(false);
        zoomInBtn.setTooltip("Zoom In timeline fino a 1/128 (o rotella mouse su)");
        zoomInBtn.onClick = [this]() { zoomAroundCenter(1.6); };
        addAndMakeVisible(zoomInBtn);

        zoomFitBtn.setButtonText("Fit");
        zoomFitBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        zoomFitBtn.setWantsKeyboardFocus(false);
        zoomFitBtn.setTooltip("Mostra l'intera durata del brano");
        zoomFitBtn.onClick = [this]() {
            if (dawTransportBridge != nullptr)
                dawWorkspaceEndSec = juce::jmax(60.0, getContentDurationSeconds() + 16.0);
            zoomFactor = 1.0;
            viewStartSec = 0.0;
            updateScrollBarRange();
            repaint();
            };
        addAndMakeVisible(zoomFitBtn);

        horizontalScrollBar.setAutoHide(false);
        horizontalScrollBar.addListener(this);
        addAndMakeVisible(horizontalScrollBar);

        verticalLaneScrollBar.setAutoHide(false);
        verticalLaneScrollBar.addListener(this);
        addChildComponent(verticalLaneScrollBar);

        lastTimerMs = juce::Time::getMillisecondCounterHiRes();
        syncWithAppState();
        startTimerHz(60);
    }

    ~MiniDawTimeline() override
    {
        if (keyListenerTarget != nullptr)
            keyListenerTarget->removeKeyListener(this);

        stopTimer();
        horizontalScrollBar.removeListener(this);
        verticalLaneScrollBar.removeListener(this);
        thumbnail.removeChangeListener(this);
        appState.removeListener(this);
    }

    void syncWithAppState()
    {
        juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);

        beatTimeMap.rebuildCacheFromState();
        syncControlsFromBeatMap();
        audioEngine.syncAllStemsFromState(appState);

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        juce::String fullMixPath = stemsNode.isValid()
            ? stemsNode.getProperty(StateKeys::StemFullMix, "").toString()
            : "";

        if (fullMixPath.isNotEmpty() && juce::File(fullMixPath).existsAsFile())
        {
            juce::File f(fullMixPath);
            if (f != lastThumbnailFile)
            {
                thumbnail.setSource(new juce::FileInputSource(f));
                lastThumbnailFile = f;
            }
        }
        else
        {
            thumbnail.setSource(nullptr);
            lastThumbnailFile = juce::File();
        }

        clickBtn.setToggleState(audioEngine.isMetronomeEnabled(), juce::dontSendNotification);
        clickVolSlider.setValue(audioEngine.getMetronomeGain() * 100.0, juce::dontSendNotification);
        alignBtn.setEnabled(audioEngine.hasFullMixLoaded());

        refreshSceneLanesAndNotify(true);

        updateScrollBarRange();
        repaint();
    }

    void setSelectedSceneIndex(int index)
    {
        if (selectedSceneIndex != index)
        {
            selectedSceneIndex = index;
            repaint();
        }
    }

    void setDawTransportBridge(const LockFreeTransportBridge* bridge)
    {
        dawTransportBridge = bridge;

        if (dawTransportBridge != nullptr)
        {
            fallbackPlaying = false;
            audioEngine.pause();
            dawWorkspaceEndSec = juce::jmax(300.0, getContentDurationSeconds() + 120.0);
        }

        updateDawControlStates();
        updateScrollBarRange();
        repaint();
    }

    void togglePlayPause()
    {
        if (dawTransportBridge != nullptr)
        {
            if (onDawTogglePlayPause)
                onDawTogglePlayPause();
            return;
        }

        if (audioEngine.hasActiveAudioDevice())
        {
            audioEngine.togglePlayPause();
        }
        else
        {
            fallbackPlaying = !fallbackPlaying;
            lastTimerMs = juce::Time::getMillisecondCounterHiRes();
        }
        updatePlayButtonLabel();
        repaint();
    }

    void stopAndRewind()
    {
        if (dawTransportBridge != nullptr)
        {
            if (onDawStopOrRewind)
                onDawStopOrRewind();
            return;
        }

        fallbackPlaying = false;
        fallbackPosSec = 0.0;
        audioEngine.stopAndRewind();
        if (onBeatPositionChanged)
            onBeatPositionChanged(beatTimeMap.secondsToBeat(0.0));
        updatePlayButtonLabel();
        repaint();
    }

    void openTimeMetricManager()
    {
        if (onOpenTimeMetricManager)
        {
            onOpenTimeMetricManager();
            return;
        }

        auto* mgr = new TimeMetricManagerComponent(appState, beatTimeMap);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(mgr);
        opts.dialogTitle = "Time & Metric Manager";
        opts.dialogBackgroundColour = juce::Colour(0xff101118);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        opts.launchAsync();
    }

    void parentHierarchyChanged() override
    {
        auto* newTop = getTopLevelComponent();
        if (newTop == this)
            newTop = nullptr;

        if (newTop == keyListenerTarget.getComponent())
            return;

        if (keyListenerTarget != nullptr)
            keyListenerTarget->removeKeyListener(this);

        keyListenerTarget = newTop;
        if (keyListenerTarget != nullptr)
            keyListenerTarget->addKeyListener(this);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            if (dawTransportBridge != nullptr)
            {
                if (onDawTogglePlayPause)
                {
                    onDawTogglePlayPause();
                    return true;
                }
                return false;
            }

            togglePlayPause();
            return true;
        }
        return false;
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        auto topBar = bounds.removeFromTop(28);

        playPauseBtn.setBounds(topBar.removeFromLeft(54));
        topBar.removeFromLeft(3);
        stopBtn.setBounds(topBar.removeFromLeft(44));
        topBar.removeFromLeft(6);

        clickBtn.setBounds(topBar.removeFromLeft(48));
        topBar.removeFromLeft(3);
        clickVolSlider.setBounds(topBar.removeFromLeft(46));
        topBar.removeFromLeft(6);

        snapBtn.setBounds(topBar.removeFromLeft(52));
        topBar.removeFromLeft(6);

        loadMixBtn.setBounds(topBar.removeFromLeft(74));
        topBar.removeFromLeft(3);
        alignBtn.setBounds(topBar.removeFromLeft(68));
        topBar.removeFromLeft(3);
        importMidiMapBtn.setBounds(topBar.removeFromLeft(78));
        topBar.removeFromLeft(6);

        bpmSlider.setBounds(topBar.removeFromLeft(78));
        topBar.removeFromLeft(4);
        meterCombo.setBounds(topBar.removeFromLeft(72));
        topBar.removeFromLeft(6);

        zoomOutBtn.setBounds(topBar.removeFromLeft(26));
        topBar.removeFromLeft(2);
        zoomInBtn.setBounds(topBar.removeFromLeft(26));
        topBar.removeFromLeft(2);
        zoomFitBtn.setBounds(topBar.removeFromLeft(44));
        topBar.removeFromLeft(6);

        topInfoBounds = topBar;

        recomputeSceneLanes();
        const int visibleLanes = juce::jlimit(1, kMaxVisibleLanes, juce::jmax(1, totalSceneLanes));
        const int laneAreaH = visibleLanes * kSceneLaneHeight + 4;

        bounds.removeFromTop(4);
        rulerBounds = bounds.removeFromTop(30);
        horizontalScrollBar.setBounds(bounds.removeFromBottom(12));
        bounds.removeFromBottom(2);

        auto fullSceneArea = bounds.removeFromBottom(laneAreaH);
        if (totalSceneLanes > kMaxVisibleLanes)
        {
            verticalLaneScrollBar.setVisible(true);
            verticalLaneScrollBar.setBounds(fullSceneArea.removeFromRight(10));
            fullSceneArea.removeFromRight(2);
        }
        else
        {
            verticalLaneScrollBar.setVisible(false);
            laneScrollOffsetPx = 0.0;
        }
        sceneLaneBounds = fullSceneArea;

        bounds.removeFromBottom(2);
        waveformBounds = bounds;

        updateScrollBarRange();
        updateVerticalLaneScrollBar();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121216));

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        const double clampedStart = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), viewStartSec);
        const double viewEndSec = clampedStart + visSec;

        const double currentSec = juce::jlimit(0.0, totalSec, getCurrentPlayheadSeconds());
        const bool hasDawPos = (dawTransportBridge != nullptr && lastDawTransport.isHostActive);
        const auto musicalPos = hasDawPos
            ? lastDawTransport.toMusicalPosition()
            : beatTimeMap.secondsToMusicalPosition(currentSec);
        const double hudTimeSec = hasDawPos ? lastDawTransport.timeSeconds : currentSec;

        // 1. HUD Timecode + Battuta/Metrica + BPM Locale + Quarto
        if (!topInfoBounds.isEmpty())
        {
            g.setColour(juce::Colour(0xff1e1e26));
            g.fillRoundedRectangle(topInfoBounds.toFloat(), 4.0f);
            g.setColour(hasDawPos ? juce::Colour(0xff34d399) : juce::Colour(0xff60a5fa));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));

            juce::String statusText = (hasDawPos ? "[DAW] " : "")
                + BeatTimeMap::formatTimecode(hudTimeSec)
                + " | " + musicalPos.formatBarString()
                + " | " + juce::String(musicalPos.localBpm, 1) + " BPM"
                + " | Q " + juce::String(musicalPos.totalQuarters, 2);

            g.drawText(statusText, topInfoBounds.reduced(6, 0), juce::Justification::centredRight, true);
        }

        // 2. Sfondo Righello e Area Traccia Audio
        g.setColour(juce::Colour(0xff15161e));
        g.fillRect(rulerBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(rulerBounds, 1);

        g.setColour(juce::Colour(0xff18181f));
        g.fillRect(waveformBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(waveformBounds, 1);

        // 3. Righello dinamico e griglia adattiva fino a 1/128 + Hitboxes marker
        drawRulerAndBeatGrid(g, clampedStart, viewEndSec);

        // 4. Clip Audio Full Mix spostabile
        if (thumbnail.getTotalLength() > 0.0)
            drawFullMixClip(g, clampedStart, viewEndSec);

        if (beatTimeMap.hasImportedMidiMap())
        {
            drawTempoMapOverlay(g, clampedStart, viewEndSec, thumbnail.getTotalLength() <= 0.0);
        }
        else if (thumbnail.getTotalLength() <= 0.0)
        {
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText("Carica un Full Mix o importa una MIDI Map (rotella = zoom fino a 1/128, trascina i marker sul righello)",
                waveformBounds.reduced(8, 0), juce::Justification::centred, true);
        }

        // Badge risoluzione griglia attiva (es. Grid: 1/16, Grid: 1/128) in alto a destra nella waveform
        if (activeGridLabel.isNotEmpty() && waveformBounds.getWidth() > 120)
        {
            const int pillW = 86;
            juce::Rectangle<int> gridPill(waveformBounds.getRight() - pillW - 5, waveformBounds.getY() + 3, pillW, 15);
            g.setColour(juce::Colour(0xff0b0c12).withAlpha(0.82f));
            g.fillRoundedRectangle(gridPill.toFloat(), 3.0f);
            g.setColour(juce::Colour(0xff38bdf8));
            g.drawRoundedRectangle(gridPill.toFloat(), 3.0f, 0.8f);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText("Grid: " + activeGridLabel, gridPill, juce::Justification::centred, false);
        }

        // 4b. Marker e Region della scaletta importati da Reaper
        drawProjectMarkers(g, clampedStart, viewEndSec);

        // 5. Corsia Scene (SHOW_TIMELINE)
        drawSceneLane(g, clampedStart, viewEndSec);

        // 6. Anteprima Ghost durante il drag di un marker BPM/Metrica
        if (isDraggingMarker)
        {
            const double previewSec = beatTimeMap.beatToSeconds(markerDragPreviewBeatQ);
            const int gx = timeToPixelX(previewSec, clampedStart, visSec);
            if (gx >= waveformBounds.getX() && gx <= waveformBounds.getRight())
            {
                const juce::Colour col = (draggingMarkerKind == RulerMarkerHit::Kind::Meter)
                    ? juce::Colour(0xffc084fc) : juce::Colour(0xfffacc15);
                g.setColour(col);
                g.drawVerticalLine(gx,
                    static_cast<float>(rulerBounds.getY()),
                    static_cast<float>(sceneLaneBounds.getBottom()));

                const auto pos = beatTimeMap.beatToMusicalPosition(markerDragPreviewBeatQ);
                juce::String tip = juce::String(draggingMarkerKind == RulerMarkerHit::Kind::Meter ? "Metro -> M" : "BPM -> M")
                    + juce::String(pos.barIndex) + " (Q " + juce::String(markerDragPreviewBeatQ, 3) + ")";
                const int tipW = 148;
                const int tipX = juce::jlimit(waveformBounds.getX() + 2, waveformBounds.getRight() - tipW - 2, gx + 4);
                juce::Rectangle<int> tipRect(tipX, waveformBounds.getY() + 22, tipW, 18);
                g.setColour(juce::Colour(0xff0b0c12).withAlpha(0.92f));
                g.fillRoundedRectangle(tipRect.toFloat(), 3.0f);
                g.setColour(col);
                g.drawRoundedRectangle(tipRect.toFloat(), 3.0f, 1.0f);
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(tip, tipRect, juce::Justification::centred, true);
            }
        }

        // 7. Testina di Lettura (Playhead)
        if (visSec > 0.0 && waveformBounds.getWidth() > 0 &&
            currentSec >= clampedStart && currentSec <= viewEndSec)
        {
            const int playheadX = timeToPixelX(currentSec, clampedStart, visSec);
            if (playheadX >= waveformBounds.getX() && playheadX <= waveformBounds.getRight())
            {
                g.setColour(juce::Colour(0xfff59e0b));
                g.drawVerticalLine(playheadX,
                    static_cast<float>(rulerBounds.getY()),
                    static_cast<float>(sceneLaneBounds.getBottom()));

                juce::Path headTriangle;
                headTriangle.addTriangle(
                    static_cast<float>(playheadX - 5), static_cast<float>(rulerBounds.getY()),
                    static_cast<float>(playheadX + 5), static_cast<float>(rulerBounds.getY()),
                    static_cast<float>(playheadX), static_cast<float>(rulerBounds.getY() + 8));
                g.fillPath(headTriangle);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (dawTransportBridge == nullptr)
        {
            if (const auto* hit = hitTestRulerMarker(e.getPosition()))
            {
                setMouseCursor(hit->index > 0 ? juce::MouseCursor::LeftRightResizeCursor
                    : juce::MouseCursor::PointingHandCursor);
                return;
            }
        }

        if (audioEngine.hasFullMixLoaded() &&
            (clipHeaderBounds.contains(e.getPosition()) ||
                (e.mods.isAltDown() && waveformBounds.contains(e.getPosition()))))
        {
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        else if (sceneLaneBounds.contains(e.getPosition()))
        {
            int hitIdx = -1;
            const auto mode = hitTestSceneBlock(e.getPosition(), hitIdx);
            if (mode == SceneDragMode::ResizeLeft || mode == SceneDragMode::ResizeRight)
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else if (mode == SceneDragMode::Move)
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            else
                setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        else
        {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Tasto destro su un marker specifico del righello
        if (e.mods.isPopupMenu())
        {
            if (dawTransportBridge != nullptr)
                return;

            if (const auto* hit = hitTestRulerMarker(e.getPosition()))
            {
                showMarkerSpecificContextMenu(*hit);
                return;
            }

            if (rulerBounds.contains(e.getPosition()) || waveformBounds.contains(e.getPosition()))
            {
                showMeterContextMenu(e.x);
                return;
            }
        }

        if (!e.mods.isLeftButtonDown())
            return;

        grabKeyboardFocus();

        // Click o Doppio-Click su un marker del righello
        if (dawTransportBridge == nullptr)
        {
            if (const auto* hit = hitTestRulerMarker(e.getPosition()))
            {
                if (e.getNumberOfClicks() >= 2)
                {
                    openTimeMetricManager();
                    return;
                }
                if (hit->index > 0)
                {
                    isDraggingMarker = true;
                    draggingMarkerKind = hit->kind;
                    draggingMarkerIndex = hit->index;
                    markerDragPreviewBeatQ = hit->beatQ;
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    repaint();
                    return;
                }
            }
        }

        // Drag manuale del Full Mix (blocca la scala timeline e crea cache offscreen anti-sfarfallio)
        if (audioEngine.hasFullMixLoaded() &&
            (clipHeaderBounds.contains(e.getPosition()) ||
                (e.mods.isAltDown() && waveformBounds.contains(e.getPosition()))))
        {
            isDraggingAudioClip = true;
            clipDragLastX = e.position.x;
            clipDragCurrentOffsetSec = audioEngine.getFullMixOffsetSeconds();
            clipDragFrozenTotalSec = getEffectiveTimelineDurationSeconds();
            clipDragVisSec = getVisibleDurationSeconds(clipDragFrozenTotalSec);
            clipDragViewStartSec = juce::jlimit(0.0,
                juce::jmax(0.0, clipDragFrozenTotalSec - clipDragVisSec), viewStartSec);

            rebuildWaveformDragCache();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            repaint();
            return;
        }

        if (rulerBounds.contains(e.getPosition()) || waveformBounds.contains(e.getPosition()))
        {
            lastDawSeekTimeMs = 0.0;
            isScrubbing = true;
            scrubToMouseX(e.x);
        }
        else if (sceneLaneBounds.contains(e.getPosition()))
        {
            int hitIdx = -1;
            const auto mode = hitTestSceneBlock(e.getPosition(), hitIdx);
            auto showNode = appState.getChildWithName(StateKeys::ShowNode);

            if (mode != SceneDragMode::None && showNode.isValid() &&
                hitIdx >= 0 && hitIdx < showNode.getNumChildren())
            {
                selectedSceneIndex = hitIdx;
                if (onSceneSelectedFromTimeline)
                    onSceneSelectedFromTimeline(hitIdx);

                sceneDragMode = mode;
                draggingSceneNode = showNode.getChild(hitIdx);
                sceneDragInitialStartBeat = static_cast<double>(draggingSceneNode.getProperty(StateKeys::StartBeat, 1.0));
                sceneDragInitialDurBeat = juce::jmax(0.25, static_cast<double>(draggingSceneNode.getProperty(StateKeys::DurationBeats, 4.0)));

                const double mouseBeat = beatTimeMap.secondsToBeat(pixelXToTime(e.x));
                sceneDragGrabOffsetBeat = mouseBeat - sceneDragInitialStartBeat;

                setMouseCursor((mode == SceneDragMode::Move)
                    ? juce::MouseCursor::DraggingHandCursor
                    : juce::MouseCursor::LeftRightResizeCursor);
                repaint();
                return;
            }

            lastDawSeekTimeMs = 0.0;
            isScrubbing = true;
            scrubToMouseX(e.x);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isDraggingMarker && e.mods.isLeftButtonDown())
        {
            const double targetSec = pixelXToTime(e.x);
            const double rawBeat = beatTimeMap.secondsToBeat(targetSec);
            const double snapped = snapBeatToActiveGrid(rawBeat, e.mods.isAltDown());
            markerDragPreviewBeatQ = juce::jmax(1.03125, snapped);

            // I cambi di metrica non deformano l'asse secondi -> aggiornamento live immediato
            if (draggingMarkerKind == RulerMarkerHit::Kind::Meter)
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
                beatTimeMap.moveMeterAnchor(draggingMarkerIndex, markerDragPreviewBeatQ);
                syncControlsFromBeatMap();
            }
            repaint();
            return;
        }

        if (sceneDragMode != SceneDragMode::None && draggingSceneNode.isValid() && e.mods.isLeftButtonDown())
        {
            const double rawBeat = beatTimeMap.secondsToBeat(pixelXToTime(e.x));
            const bool useSnap = snapBtn.getToggleState();
            const double minDur = 0.25;

            if (sceneDragMode == SceneDragMode::Move)
            {
                double newStart = juce::jmax(1.0, rawBeat - sceneDragGrabOffsetBeat);
                if (useSnap)
                    newStart = juce::jmax(1.0, snapBeatToActiveGrid(newStart, e.mods.isAltDown()));

                draggingSceneNode.setProperty(StateKeys::StartBeat, newStart, nullptr);
            }
            else if (sceneDragMode == SceneDragMode::ResizeRight)
            {
                double targetEnd = rawBeat;
                if (useSnap)
                    targetEnd = snapBeatToActiveGrid(targetEnd, e.mods.isAltDown());

                const double newDur = juce::jmax(minDur, targetEnd - sceneDragInitialStartBeat);
                draggingSceneNode.setProperty(StateKeys::DurationBeats, newDur, nullptr);
            }
            else if (sceneDragMode == SceneDragMode::ResizeLeft)
            {
                const double fixedEnd = sceneDragInitialStartBeat + sceneDragInitialDurBeat;
                double newStart = rawBeat;
                if (useSnap)
                    newStart = snapBeatToActiveGrid(newStart, e.mods.isAltDown());

                newStart = juce::jlimit(1.0, juce::jmax(1.0, fixedEnd - minDur), newStart);
                const double newDur = juce::jmax(minDur, fixedEnd - newStart);
                draggingSceneNode.setProperty(StateKeys::StartBeat, newStart, nullptr);
                draggingSceneNode.setProperty(StateKeys::DurationBeats, newDur, nullptr);
            }

            if (dawTransportBridge != nullptr)
            {
                const double sB = static_cast<double>(draggingSceneNode.getProperty(StateKeys::StartBeat, 1.0));
                const double dB = juce::jmax(0.25, static_cast<double>(draggingSceneNode.getProperty(StateKeys::DurationBeats, 4.0)));
                const double endSec = beatTimeMap.beatToSeconds(sB + dB);
                if (endSec + 15.0 > dawWorkspaceEndSec)
                    dawWorkspaceEndSec = juce::jmin(14400.0, endSec + 60.0);
            }

            updateScrollBarRange();
            refreshSceneLanesAndNotify(false);
            repaint();
            return;
        }

        if (isDraggingAudioClip && e.mods.isLeftButtonDown())
        {
            if (waveformBounds.getWidth() > 0 && clipDragVisSec > 0.0001)
            {
                const double dx = static_cast<double>(e.position.x - clipDragLastX);
                clipDragLastX = e.position.x;

                // Delta incrementale: premere/rilasciare Shift a meta' drag non causa scatti
                const double scale = e.mods.isShiftDown() ? 0.2 : 1.0;
                const double deltaSec = (dx / static_cast<double>(waveformBounds.getWidth())) * clipDragVisSec * scale;

                clipDragCurrentOffsetSec = juce::jlimit(-600.0, 600.0, clipDragCurrentOffsetSec + deltaSec);

                // Aggiorna solo l'atomic dell'audio engine (niente scritture ValueTree durante il drag)
                audioEngine.setFullMixOffsetSeconds(clipDragCurrentOffsetSec, nullptr);
                repaint();
            }
            return;
        }

        if (isScrubbing && e.mods.isLeftButtonDown())
            scrubToMouseX(e.x);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isScrubbing = false;

        if (isDraggingMarker)
        {
            if (draggingMarkerKind == RulerMarkerHit::Kind::Tempo)
            {
                juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
                beatTimeMap.moveTempoAnchor(draggingMarkerIndex, markerDragPreviewBeatQ);
                syncControlsFromBeatMap();
                updateScrollBarRange();
            }
            isDraggingMarker = false;
            draggingMarkerIndex = -1;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }

        if (isDraggingAudioClip)
        {
            isDraggingAudioClip = false;
            waveformDragCache = juce::Image();
            clipDragFrozenTotalSec = 0.0;

            // Persiste l'offset nel ValueTree e riallinea la scrollbar una sola volta al rilascio
            audioEngine.setFullMixOffsetSeconds(clipDragCurrentOffsetSec, &appState);
            updateScrollBarRange();
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }

        if (sceneDragMode != SceneDragMode::None)
        {
            auto editedNode = draggingSceneNode;
            sceneDragMode = SceneDragMode::None;
            draggingSceneNode = juce::ValueTree();
            setMouseCursor(juce::MouseCursor::NormalCursor);

            refreshSceneLanesAndNotify(true);

            if (onSceneDragFinished && editedNode.isValid())
                onSceneDragFinished(editedNode);
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (isDraggingAudioClip)
            return;

        if (!rulerBounds.contains(e.getPosition()) &&
            !waveformBounds.contains(e.getPosition()) &&
            !sceneLaneBounds.contains(e.getPosition()))
        {
            return;
        }

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);

        if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
        {
            const double delta = (std::abs(wheel.deltaX) > 0.001f) ? -wheel.deltaX : -wheel.deltaY;
            const double targetStart = viewStartSec + delta * visSec * 0.25;
            double effTotal = totalSec;

            if (dawTransportBridge != nullptr && targetStart > effTotal - visSec)
            {
                dawWorkspaceEndSec = juce::jmin(14400.0, targetStart + visSec * 1.5);
                effTotal = getEffectiveTimelineDurationSeconds();
            }

            viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, effTotal - visSec), targetStart);
            updateScrollBarRange();
            repaint();
            return;
        }

        if (std::abs(wheel.deltaY) > 0.001f)
        {
            const double factor = (wheel.deltaY > 0.0f) ? 1.32 : (1.0 / 1.32);
            zoomAtPixel(e.x, factor);
        }
    }

    std::function<bool()> onForceReaperSync;
    std::function<bool()> onDawTogglePlayPause;
    std::function<bool()> onDawStopOrRewind;
    std::function<bool(double beat1Based)> onDawSeekBeat;

    void setReaperSyncEnabled(bool enabled)
    {
        reaperSyncEnabled = enabled;
        if (reaperSyncEnabled)
        {
            importMidiMapBtn.setButtonText("Sync Reaper");
            importMidiMapBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f766e));
            importMidiMapBtn.setTooltip("Forza la sincronizzazione di Tempo Map, Metriche e Marker/Region da Reaper");
            importMidiMapBtn.onClick = [this]() {
                if (onForceReaperSync)
                    onForceReaperSync();
                beatTimeMap.rebuildCacheFromState();
                syncControlsFromBeatMap();
                updateScrollBarRange();
                repaint();
                };
        }
        updateDawControlStates();
    }

private:
    struct RulerMarkerHit
    {
        enum class Kind { Meter, Tempo };
        Kind kind = Kind::Meter;
        int index = 0;
        juce::Rectangle<int> bounds;
        double beatQ = 1.0;
    };

    static constexpr double kMaxZoomFactor = 2048.0;

    enum class SceneDragMode { None, Move, ResizeLeft, ResizeRight };

    static int getSceneLayerPriority(const juce::ValueTree& scene)
    {
        const juce::String layer = scene.getProperty(StateKeys::LayerType, "Base").toString();
        if (layer.equalsIgnoreCase("OverlayFX")) return 2;
        if (layer.equalsIgnoreCase("Movement"))  return 1;
        return 0;
    }

    static juce::Colour getSceneBlockColour(const juce::ValueTree& scene, bool& outHasColorSwatch, juce::Colour& outSwatch)
    {
        outHasColorSwatch = false;
        outSwatch = juce::Colour(0xff38bdf8);

        bool hasCol = false, hasDim = false, hasMove = false, hasStrobe = false;
        for (int c = 0; c < scene.getNumChildren(); ++c)
        {
            auto cmd = scene.getChild(c);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            const juce::String param = cmd.getProperty(StateKeys::TargetParam, "").toString();
            const juce::String curve = cmd.getProperty(StateKeys::CurveType, "").toString();

            if (param.equalsIgnoreCase("Red") || param.equalsIgnoreCase("Green") ||
                param.equalsIgnoreCase("Blue") || param.equalsIgnoreCase("White") ||
                param.equalsIgnoreCase("UV"))
                hasCol = true;
            else if (param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt"))
                hasMove = true;
            else if (param.equalsIgnoreCase("Strobe") ||
                (param.equalsIgnoreCase("Dimmer") && curve.startsWithIgnoreCase("Square")))
                hasStrobe = true;
            else if (param.equalsIgnoreCase("Dimmer") || param.equalsIgnoreCase("GeyserBlast"))
                hasDim = true;
        }

        if (hasCol)
        {
            outHasColorSwatch = true;
            outSwatch = juce::Colour::fromString(scene.getProperty(StateKeys::PresetColor1, "ff38bdf8").toString());
        }

        if (hasStrobe)                        return juce::Colour(0xff991b1b); // Rosso Strobo
        if (hasMove && !hasCol && !hasDim)    return juce::Colour(0xff5b21b6); // Viola Movimento
        if (hasCol && !hasDim && !hasMove)    return juce::Colour(0xff075985); // Blu/Ciano Colore
        if (hasDim && !hasCol && !hasMove)    return juce::Colour(0xff92400e); // Ambra Dimmer
        return juce::Colour(0xff115e59);                                       // Petrolio Multi-Attributo
    }

    void recomputeSceneLanes()
    {
        sceneLaneByIndex.clear();
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || showNode.getNumChildren() == 0)
        {
            totalSceneLanes = 1;
            return;
        }

        const int n = showNode.getNumChildren();
        sceneLaneByIndex.assign(static_cast<size_t>(n), 0);

        std::vector<int> order(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) order[static_cast<size_t>(i)] = i;

        std::sort(order.begin(), order.end(), [&](int a, int b) {
            auto sa = showNode.getChild(a);
            auto sb = showNode.getChild(b);
            const double startA = static_cast<double>(sa.getProperty(StateKeys::StartBeat, 1.0));
            const double startB = static_cast<double>(sb.getProperty(StateKeys::StartBeat, 1.0));
            if (std::abs(startA - startB) > 1e-5)
                return startA < startB;

            const int prioA = getSceneLayerPriority(sa);
            const int prioB = getSceneLayerPriority(sb);
            if (prioA != prioB)
                return prioA < prioB;

            return a < b;
            });

        std::vector<double> laneEndBeats;
        for (int idx : order)
        {
            auto sc = showNode.getChild(idx);
            const double startB = static_cast<double>(sc.getProperty(StateKeys::StartBeat, 1.0));
            const double durB = juce::jmax(0.25, static_cast<double>(sc.getProperty(StateKeys::DurationBeats, 4.0)));
            const double endB = startB + durB;

            int assignedLane = -1;
            for (size_t l = 0; l < laneEndBeats.size(); ++l)
            {
                if (laneEndBeats[l] <= startB + 1e-4)
                {
                    assignedLane = static_cast<int>(l);
                    laneEndBeats[l] = endB;
                    break;
                }
            }

            if (assignedLane < 0)
            {
                assignedLane = static_cast<int>(laneEndBeats.size());
                laneEndBeats.push_back(endB);
            }

            sceneLaneByIndex[static_cast<size_t>(idx)] = assignedLane;
        }

        totalSceneLanes = juce::jmax(1, static_cast<int>(laneEndBeats.size()));
    }

    void updateVerticalLaneScrollBar()
    {
        const double contentH = static_cast<double>(juce::jmax(1, totalSceneLanes) * kSceneLaneHeight);
        const double visibleH = static_cast<double>(juce::jmax(kSceneLaneHeight, sceneLaneBounds.getHeight() - 4));
        const double maxScroll = juce::jmax(0.0, contentH - visibleH);
        laneScrollOffsetPx = juce::jlimit(0.0, maxScroll, laneScrollOffsetPx);

        isUpdatingScrollBar = true;
        verticalLaneScrollBar.setRangeLimits(0.0, contentH, juce::dontSendNotification);
        verticalLaneScrollBar.setCurrentRange(laneScrollOffsetPx, visibleH, juce::dontSendNotification);
        isUpdatingScrollBar = false;
    }

    void refreshSceneLanesAndNotify(bool allowShrink = true)
    {
        const int prevTotal = totalSceneLanes;
        const int prevVisible = juce::jlimit(1, kMaxVisibleLanes, juce::jmax(1, prevTotal));

        recomputeSceneLanes();

        if (!allowShrink && totalSceneLanes < prevTotal)
            totalSceneLanes = prevTotal;

        const int newVisible = juce::jlimit(1, kMaxVisibleLanes, juce::jmax(1, totalSceneLanes));
        if (newVisible != prevVisible)
        {
            if (onPreferredHeightChanged)
                onPreferredHeightChanged();
            else
                resized();
        }
        else if (totalSceneLanes != prevTotal)
        {
            resized();
        }
        else
        {
            updateVerticalLaneScrollBar();
        }
    }

    int getLaneTopY(int laneIdx) const noexcept
    {
        return sceneLaneBounds.getY() + 2 + laneIdx * kSceneLaneHeight - juce::roundToInt(laneScrollOffsetPx);
    }

    SceneDragMode hitTestSceneBlock(juce::Point<int> pt, int& outSceneIdx) const
    {
        outSceneIdx = -1;
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || !sceneLaneBounds.contains(pt) || sceneLaneBounds.getWidth() <= 0)
            return SceneDragMode::None;

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        const double clampedStart = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), viewStartSec);
        const double viewEndSec = clampedStart + visSec;

        for (int i = showNode.getNumChildren() - 1; i >= 0; --i)
        {
            auto scene = showNode.getChild(i);
            const double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            const double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));
            const double startSec = beatTimeMap.beatToSeconds(startB);
            const double endSec = beatTimeMap.beatToSeconds(startB + durB);

            if (endSec < clampedStart || startSec > viewEndSec)
                continue;

            const int x1 = timeToPixelX(startSec, clampedStart, visSec);
            const int x2 = timeToPixelX(endSec, clampedStart, visSec);
            const int w = juce::jmax(8, x2 - x1);

            const int laneIdx = (i < static_cast<int>(sceneLaneByIndex.size())) ? sceneLaneByIndex[static_cast<size_t>(i)] : 0;
            const int yTop = getLaneTopY(laneIdx);

            juce::Rectangle<int> blockRect(x1, yTop, w, kSceneLaneHeight - 3);
            blockRect = blockRect.getIntersection(sceneLaneBounds);

            if (!blockRect.isEmpty() && blockRect.expanded(3, 0).contains(pt))
            {
                outSceneIdx = i;
                const int handleW = juce::jmin(7, juce::jmax(3, blockRect.getWidth() / 4));
                if (std::abs(pt.x - blockRect.getRight()) <= handleW)
                    return SceneDragMode::ResizeRight;
                if (std::abs(pt.x - blockRect.getX()) <= handleW)
                    return SceneDragMode::ResizeLeft;
                return SceneDragMode::Move;
            }
        }
        return SceneDragMode::None;
    }

    const RulerMarkerHit* hitTestRulerMarker(juce::Point<int> pt) const
    {
        for (int i = static_cast<int>(rulerMarkerHits.size()) - 1; i >= 0; --i)
        {
            if (rulerMarkerHits[static_cast<size_t>(i)].bounds.expanded(2).contains(pt))
                return &rulerMarkerHits[static_cast<size_t>(i)];
        }
        return nullptr;
    }

    double snapBeatToActiveGrid(double rawBeatQ, bool forceFine128) const
    {
        const double safeB = juce::jmax(1.0, rawBeatQ);
        const auto pos = beatTimeMap.beatToMusicalPosition(safeB);
        const double barStartQ = juce::jmax(1.0, pos.totalQuarters - pos.beatInBarQ);

        const double stepQ = forceFine128
            ? 0.03125
            : (activeGridStepQ > 0.0 ? activeGridStepQ : pos.barLengthQ);

        const double offsetInBar = safeB - barStartQ;
        const double snappedOffset = std::round(offsetInBar / stepQ) * stepQ;
        return juce::jmax(1.0, barStartQ + snappedOffset);
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (!isShowing() || key != juce::KeyPress::spaceKey)
            return false;

        if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
            return false;

        if (dawTransportBridge != nullptr)
        {
            if (onDawTogglePlayPause)
            {
                onDawTogglePlayPause();
                return true;
            }
            return false;
        }

        togglePlayPause();
        return true;
    }

    void updatePlayButtonLabel()
    {
        if (dawTransportBridge != nullptr)
        {
            const bool dawPlay = lastDawTransport.isHostActive && lastDawTransport.isPlaying;
            playPauseBtn.setButtonText(dawPlay ? "Pausa" : "Play");
            playPauseBtn.setColour(juce::TextButton::buttonColourId,
                dawPlay ? juce::Colour(0xff10b981) : juce::Colour(0xff0f766e));
            return;
        }

        const bool playing = audioEngine.hasActiveAudioDevice() ? audioEngine.isPlaying() : fallbackPlaying;
        playPauseBtn.setButtonText(playing ? "Pausa" : "Play");
    }

    double getCurrentPlayheadSeconds() const
    {
        if (dawTransportBridge != nullptr)
            return dawTimelinePosSec;

        return audioEngine.hasActiveAudioDevice() ? audioEngine.getCurrentPositionSeconds() : fallbackPosSec;
    }

    double getVisibleDurationSeconds(double totalSec) const noexcept
    {
        const double minVis = juce::jmin(totalSec, 0.20); // fino a 200 ms visibili per vedere bene 1/128
        return juce::jmax(minVis, totalSec / juce::jlimit(1.0, kMaxZoomFactor, zoomFactor));
    }

    int timeToPixelX(double sec, double vStart, double vDur) const noexcept
    {
        if (vDur <= 0.0001 || waveformBounds.getWidth() <= 0)
            return waveformBounds.getX();
        const double norm = (sec - vStart) / vDur;
        return waveformBounds.getX() + juce::roundToInt(norm * static_cast<double>(waveformBounds.getWidth()));
    }

    double pixelXToTime(int mouseX) const
    {
        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        const double clampedStart = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), viewStartSec);
        if (waveformBounds.getWidth() <= 0)
            return clampedStart;

        const double norm = juce::jlimit(0.0, 1.0,
            static_cast<double>(mouseX - waveformBounds.getX()) / static_cast<double>(waveformBounds.getWidth()));
        return juce::jlimit(0.0, totalSec, clampedStart + norm * visSec);
    }

    void zoomAtPixel(int anchorX, double multiplier)
    {
        double totalSec = getEffectiveTimelineDurationSeconds();
        const double oldVis = getVisibleDurationSeconds(totalSec);
        const double anchorNorm = (waveformBounds.getWidth() > 0)
            ? juce::jlimit(0.0, 1.0, static_cast<double>(anchorX - waveformBounds.getX()) / static_cast<double>(waveformBounds.getWidth()))
            : 0.5;
        const double anchorSec = viewStartSec + anchorNorm * oldVis;

        const double proposedZoom = zoomFactor * multiplier;
        if (dawTransportBridge != nullptr && proposedZoom < 1.0 && multiplier < 1.0)
        {
            const double expandRatio = 1.0 / juce::jmax(0.1, proposedZoom);
            dawWorkspaceEndSec = juce::jlimit(60.0, 14400.0, totalSec * expandRatio);
            totalSec = getEffectiveTimelineDurationSeconds();
            zoomFactor = 1.0;
        }
        else
        {
            zoomFactor = juce::jlimit(1.0, kMaxZoomFactor, proposedZoom);
        }

        const double newVis = getVisibleDurationSeconds(totalSec);
        viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - newVis), anchorSec - anchorNorm * newVis);

        updateScrollBarRange();
        repaint();
    }

    void zoomAroundCenter(double multiplier)
    {
        const int centerX = waveformBounds.isEmpty() ? (getWidth() / 2) : waveformBounds.getCentreX();
        zoomAtPixel(centerX, multiplier);
    }

    void updateScrollBarRange()
    {
        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), viewStartSec);

        isUpdatingScrollBar = true;
        horizontalScrollBar.setRangeLimits(0.0, totalSec, juce::dontSendNotification);
        horizontalScrollBar.setCurrentRange(viewStartSec, visSec, juce::dontSendNotification);
        isUpdatingScrollBar = false;
    }

    void scrollBarMoved(juce::ScrollBar* sb, double newRangeStart) override
    {
        if (isUpdatingScrollBar)
            return;

        if (sb == &horizontalScrollBar)
        {
            double totalSec = getEffectiveTimelineDurationSeconds();
            const double visSec = getVisibleDurationSeconds(totalSec);

            if (dawTransportBridge != nullptr && (newRangeStart + visSec) >= totalSec - juce::jmax(1.0, visSec * 0.02))
            {
                dawWorkspaceEndSec = juce::jmin(14400.0, totalSec + juce::jmax(60.0, visSec * 0.35));
                totalSec = getEffectiveTimelineDurationSeconds();
                viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), newRangeStart);
                updateScrollBarRange();
            }
            else
            {
                viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), newRangeStart);
            }
            repaint();
        }
        else if (sb == &verticalLaneScrollBar)
        {
            const double maxScroll = juce::jmax(0.0, static_cast<double>(totalSceneLanes * kSceneLaneHeight - (sceneLaneBounds.getHeight() - 4)));
            laneScrollOffsetPx = juce::jlimit(0.0, maxScroll, newRangeStart);
            repaint();
        }
    }

    static bool meterIdToSignature(int id, int& outNum, int& outDen) noexcept
    {
        switch (id)
        {
        case 1: outNum = 4;  outDen = 4; return true;
        case 2: outNum = 3;  outDen = 4; return true;
        case 3: outNum = 6;  outDen = 8; return true;
        case 4: outNum = 5;  outDen = 4; return true;
        case 5: outNum = 7;  outDen = 8; return true;
        case 6: outNum = 12; outDen = 8; return true;
        case 7: outNum = 2;  outDen = 4; return true;
        case 8: outNum = 9;  outDen = 8; return true;
        default: return false;
        }
    }

    static int signatureToMeterId(int num, int den) noexcept
    {
        if (num == 4 && den == 4) return 1;
        if (num == 3 && den == 4) return 2;
        if (num == 6 && den == 8) return 3;
        if (num == 5 && den == 4) return 4;
        if (num == 7 && den == 8) return 5;
        if (num == 12 && den == 8) return 6;
        if (num == 2 && den == 4) return 7;
        if (num == 9 && den == 8) return 8;
        return 0;
    }

    void syncControlsFromBeatMap()
    {
        juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
        bpmSlider.setValue(beatTimeMap.getBaseBpm(), juce::dontSendNotification);

        const int id = signatureToMeterId(beatTimeMap.getBaseTimeSigNum(), beatTimeMap.getBaseTimeSigDen());
        if (id > 0)
            meterCombo.setSelectedId(id, juce::dontSendNotification);
        else
            meterCombo.setText(juce::String(beatTimeMap.getBaseTimeSigNum()) + "/"
                + juce::String(beatTimeMap.getBaseTimeSigDen()), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double dtSec = juce::jlimit(0.0, 0.1, (nowMs - lastTimerMs) * 0.001);
        lastTimerMs = nowMs;

        if (needsBeatMapSync)
        {
            needsBeatMapSync = false;
            beatTimeMap.rebuildCacheFromState();
            syncControlsFromBeatMap();
            updateScrollBarRange();
            repaint();
        }

        if (dawTransportBridge != nullptr)
        {
            const auto dawTs = dawTransportBridge->read();
            if (dawTs.isHostActive)
            {
                const double safeBpm = (dawTs.bpm > 1.0 && dawTs.bpm < 500.0) ? dawTs.bpm : 120.0;
                const double rawPpq = juce::jmax(0.0, dawTs.ppqPosition);
                const double rawSec = juce::jmax(0.0, dawTs.timeSeconds);

                if (!dawTs.isPlaying)
                {
                    smoothDawPpq = rawPpq;
                    smoothDawSec = rawSec;
                    lastRawDawPpq = rawPpq;
                    wasDawPlaying = false;
                }
                else if (!wasDawPlaying || lastRawDawPpq < 0.0)
                {
                    smoothDawPpq = rawPpq;
                    smoothDawSec = rawSec;
                    lastRawDawPpq = rawPpq;
                    wasDawPlaying = true;
                }
                else
                {
                    const double prevPpq = smoothDawPpq;
                    smoothDawPpq += dtSec * (safeBpm / 60.0);
                    smoothDawSec += dtSec;

                    if (std::abs(rawPpq - lastRawDawPpq) > 1e-6)
                    {
                        lastRawDawPpq = rawPpq;
                        const double driftQ = rawPpq - smoothDawPpq;
                        if (std::abs(driftQ) > 0.35)
                        {
                            smoothDawPpq = rawPpq;
                            smoothDawSec = rawSec;
                        }
                        else
                        {
                            smoothDawPpq = juce::jmax(prevPpq, smoothDawPpq + driftQ * 0.25);
                            smoothDawSec += (rawSec - smoothDawSec) * 0.25;
                        }
                    }
                    else
                    {
                        const double maxLeadQ = juce::jmax(0.08, (safeBpm / 60.0) * 0.12);
                        if (smoothDawPpq - rawPpq > maxLeadQ)
                            smoothDawPpq = rawPpq + maxLeadQ;
                        if (smoothDawSec - rawSec > 0.12)
                            smoothDawSec = rawSec + 0.12;
                    }
                }
                const bool playStateChanged = (dawTs.isPlaying != lastDawTransport.isPlaying);

                lastDawTransport = dawTs;
                lastDawTransport.ppqPosition = smoothDawPpq;
                lastDawTransport.timeSeconds = smoothDawSec;

                const double dawBeatQ = 1.0 + smoothDawPpq;
                dawTimelinePosSec = beatTimeMap.beatToSeconds(dawBeatQ);

                const double totalSec = getEffectiveTimelineDurationSeconds();
                if (dawTimelinePosSec > totalSec - 2.0)
                    updateScrollBarRange();

                updatePlayButtonLabel();

                if (dawTs.isPlaying || playStateChanged || std::abs(dawTimelinePosSec - lastReportedSec) > 0.001)
                {
                    lastReportedSec = dawTimelinePosSec;

                    if (dawTs.isPlaying && zoomFactor > 1.01 && !isDraggingAudioClip)
                    {
                        const double visSec = getVisibleDurationSeconds(totalSec);
                        if (dawTimelinePosSec < viewStartSec || dawTimelinePosSec > viewStartSec + visSec * 0.95)
                        {
                            viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), dawTimelinePosSec - visSec * 0.15);
                            updateScrollBarRange();
                        }
                    }

                    if (onBeatPositionChanged)
                        onBeatPositionChanged(dawBeatQ);
                    repaint();
                }
            }
            return;
        }

        const double totalSec = getEffectiveTimelineDurationSeconds();
        bool isPlayingNow = false;

        if (audioEngine.hasActiveAudioDevice())
        {
            isPlayingNow = audioEngine.isPlaying();
            if (isPlayingNow && audioEngine.getCurrentPositionSeconds() >= totalSec)
                stopAndRewind();
        }
        else if (fallbackPlaying)
        {
            isPlayingNow = true;
            fallbackPosSec += dtSec;
            if (fallbackPosSec >= totalSec)
            {
                fallbackPosSec = 0.0;
                fallbackPlaying = false;
            }
        }

        updatePlayButtonLabel();
        const double currentSec = getCurrentPlayheadSeconds();

        if (isPlayingNow || std::abs(currentSec - lastReportedSec) > 0.001)
        {
            lastReportedSec = currentSec;

            if (isPlayingNow && zoomFactor > 1.01 && !isDraggingAudioClip)
            {
                const double visSec = getVisibleDurationSeconds(totalSec);
                if (currentSec < viewStartSec || currentSec > viewStartSec + visSec * 0.95)
                {
                    viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), currentSec - visSec * 0.15);
                    updateScrollBarRange();
                }
            }

            if (onBeatPositionChanged)
                onBeatPositionChanged(beatTimeMap.secondsToBeat(currentSec));
            repaint();
        }
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        updateScrollBarRange();
        repaint();
    }

    void markBeatMapDirtyIfRelevant(const juce::ValueTree& node)
    {
        if (isUpdatingControls) return;
        if (node.hasType(StateKeys::BeatMapNode) ||
            node.hasType(StateKeys::BeatPointNode) ||
            node.hasType(StateKeys::MeterPointNode) ||
            node.hasType(BeatTimeMap::kProjectMarkerNode))
        {
            needsBeatMapSync = true;
        }
    }

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& prop) override
    {
        markBeatMapDirtyIfRelevant(tree);
        if (tree.hasType(StateKeys::Scene) &&
            (prop == StateKeys::StartBeat || prop == StateKeys::DurationBeats))
        {
            refreshSceneLanesAndNotify(sceneDragMode == SceneDragMode::None);
        }
        repaint();
    }

    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override
    {
        markBeatMapDirtyIfRelevant(parent);
        markBeatMapDirtyIfRelevant(child);
        if (parent.hasType(StateKeys::ShowNode) || child.hasType(StateKeys::Scene))
            refreshSceneLanesAndNotify(true);
        repaint();
    }

    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int) override
    {
        markBeatMapDirtyIfRelevant(parent);
        markBeatMapDirtyIfRelevant(child);
        if (parent.hasType(StateKeys::ShowNode) || child.hasType(StateKeys::Scene))
            refreshSceneLanesAndNotify(true);
        repaint();
    }

    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override
    {
        if (parent.hasType(StateKeys::ShowNode))
            refreshSceneLanesAndNotify(true);
        repaint();
    }

    void valueTreeParentChanged(juce::ValueTree&) override {}

    void promptLoadFullMix()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Seleziona il file Audio Full Mix",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                if (safeThis->audioEngine.loadFullMix(file, safeThis->appState, false))
                {
                    safeThis->zoomFactor = 1.0;
                    safeThis->viewStartSec = 0.0;
                    safeThis->thumbnail.setSource(new juce::FileInputSource(file));
                    safeThis->lastThumbnailFile = file;
                    safeThis->alignBtn.setEnabled(true);
                    safeThis->updateScrollBarRange();
                    safeThis->repaint();
                }
            }
            });
    }

    void showAlignMenu()
    {
        if (!audioEngine.hasFullMixLoaded())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Allineamento Full Mix",
                "Carica prima un file Full Mix per usare l'allineamento.");
            return;
        }

        const int curMs = juce::roundToInt(audioEngine.getFullMixOffsetSeconds() * 1000.0);
        juce::String curSign = (curMs >= 0 ? "+" : "") + juce::String(curMs) + " ms";

        juce::PopupMenu menu;
        menu.addSectionHeader("Allinea Full Mix (Offset attuale: " + curSign + ")");
        menu.addItem(1, "Auto: Primo transiente -> M1.1");
        menu.addItem(2, "Auto: Aggancia transiente vicino al cursore -> Battere");
        menu.addItem(3, "Sposta il punto al cursore -> M1.1");
        menu.addSeparator();
        menu.addItem(10, "Sposta -10 ms (anticipa)");
        menu.addItem(11, "Sposta -1 ms");
        menu.addItem(12, "Sposta +1 ms");
        menu.addItem(13, "Sposta +10 ms (ritarda)");
        menu.addSeparator();
        menu.addItem(20, "Azzera offset (0 ms)");

        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&alignBtn), [safeThis](int res) {
            if (safeThis == nullptr || res == 0) return;

            if (res == 1)
            {
                double transientSec = 0.0;
                if (safeThis->audioEngine.detectFirstTransientSeconds(transientSec, 60.0))
                {
                    const double targetBar1Sec = safeThis->beatTimeMap.beatToSeconds(1.0);
                    safeThis->audioEngine.setFullMixOffsetSeconds(targetBar1Sec - transientSec, &safeThis->appState);
                    safeThis->updateScrollBarRange();
                    safeThis->repaint();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Allineamento Automatico",
                        "Nessun transiente netto trovato nei primi 60 secondi.");
                }
            }
            else if (res == 2)
            {
                const double playheadSec = safeThis->getCurrentPlayheadSeconds();
                const double curOffset = safeThis->audioEngine.getFullMixOffsetSeconds();
                const double fileSecAtCursor = playheadSec - curOffset;

                double detectedFileSec = 0.0;
                if (safeThis->audioEngine.detectTransientNearFileTime(fileSecAtCursor, 0.35, detectedFileSec))
                {
                    const auto pos = safeThis->beatTimeMap.secondsToMusicalPosition(playheadSec);
                    const double prevBarQ = pos.totalQuarters - pos.beatInBarQ;
                    const double nextBarQ = prevBarQ + pos.barLengthQ;
                    const double nearestBarQ = (pos.beatInBarQ <= pos.barLengthQ * 0.5) ? prevBarQ : nextBarQ;
                    const double targetBarSec = safeThis->beatTimeMap.beatToSeconds(nearestBarQ);

                    safeThis->audioEngine.setFullMixOffsetSeconds(targetBarSec - detectedFileSec, &safeThis->appState);
                    safeThis->updateScrollBarRange();
                    safeThis->repaint();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Aggancio Transiente",
                        "Nessun attacco netto trovato entro +/-350 ms dal cursore.");
                }
            }
            else if (res == 3)
            {
                const double playheadSec = safeThis->getCurrentPlayheadSeconds();
                const double curOffset = safeThis->audioEngine.getFullMixOffsetSeconds();
                const double fileSecAtCursor = playheadSec - curOffset;
                const double bar1Sec = safeThis->beatTimeMap.beatToSeconds(1.0);

                safeThis->audioEngine.setFullMixOffsetSeconds(bar1Sec - fileSecAtCursor, &safeThis->appState);
                safeThis->audioEngine.setPositionSeconds(bar1Sec);
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            else if (res >= 10 && res <= 13)
            {
                const double delta = (res == 10) ? -0.010 : (res == 11 ? -0.001 : (res == 12 ? 0.001 : 0.010));
                safeThis->audioEngine.setFullMixOffsetSeconds(
                    safeThis->audioEngine.getFullMixOffsetSeconds() + delta, &safeThis->appState);
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            else if (res == 20)
            {
                safeThis->audioEngine.setFullMixOffsetSeconds(0.0, &safeThis->appState);
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            });
    }

    void promptImportMidiMap()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Importa Tempo Map e Metrica da file MIDI (.mid)",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.mid;*.midi");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            BeatTimeMap::MidiMapImportResult res;
            {
                juce::ScopedValueSetter<bool> guard(safeThis->isUpdatingControls, true);
                res = safeThis->beatTimeMap.importMidiTempoAndMeterMap(file, 0.0);
            }

            if (res.success)
            {
                safeThis->zoomFactor = 1.0;
                safeThis->viewStartSec = 0.0;
                safeThis->syncControlsFromBeatMap();
                safeThis->updateScrollBarRange();
                safeThis->repaint();

                juce::String msg = "Tempo Map caricata correttamente.\n"
                    "Cambi BPM importati: " + juce::String(res.tempoChangesImported) + "\n"
                    "Cambi Metrica importati: " + juce::String(res.meterChangesImported) + "\n"
                    "Partenza: " + juce::String(res.initialBpm, 1) + " BPM ("
                    + juce::String(res.initialNum) + "/" + juce::String(res.initialDen) + ")";

                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "MIDI Map Importata", msg);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Errore Importazione MIDI Map", res.errorMessage);
            }
            });
    }

    void showMarkerSpecificContextMenu(const RulerMarkerHit& hit)
    {
        juce::PopupMenu menu;
        const bool isMeter = (hit.kind == RulerMarkerHit::Kind::Meter);
        menu.addSectionHeader((isMeter ? "Marker Metrica #" : "Marker BPM #")
            + juce::String(hit.index + 1) + " (Q " + juce::String(hit.beatQ, 3) + ")");
        menu.addItem(1, "Apri Time & Metric Manager...");
        if (hit.index > 0)
            menu.addItem(2, isMeter ? "Elimina questo cambio Metrica" : "Elimina questo cambio BPM");

        const auto kind = hit.kind;
        const int idx = hit.index;
        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, kind, idx](int res) {
            if (safeThis == nullptr || res == 0) return;
            if (res == 1)
            {
                safeThis->openTimeMetricManager();
            }
            else if (res == 2 && idx > 0)
            {
                juce::ScopedValueSetter<bool> guard(safeThis->isUpdatingControls, true);
                if (kind == RulerMarkerHit::Kind::Meter)
                    safeThis->beatTimeMap.removeMeterAnchor(idx);
                else
                    safeThis->beatTimeMap.removeTempoAnchor(idx);
                safeThis->syncControlsFromBeatMap();
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            });
    }

    void showMeterContextMenu(int mouseX)
    {
        if (waveformBounds.getWidth() <= 0) return;

        const double clickedSec = pixelXToTime(mouseX);
        const double rawBeat = beatTimeMap.secondsToBeat(clickedSec);
        const double snappedBeat = snapBeatToActiveGrid(rawBeat, false);
        const auto pos = beatTimeMap.beatToMusicalPosition(snappedBeat);
        const double barStartQ = juce::jmax(1.0, pos.totalQuarters - pos.beatInBarQ);

        juce::PopupMenu menu;
        menu.addSectionHeader("Posizione M" + juce::String(pos.barIndex)
            + " (Q " + juce::String(snappedBeat, 3) + ")");

        menu.addItem(1, "Imposta 4/4 su M" + juce::String(pos.barIndex));
        menu.addItem(2, "Imposta 3/4 su M" + juce::String(pos.barIndex));
        menu.addItem(3, "Imposta 6/8 su M" + juce::String(pos.barIndex));
        menu.addItem(4, "Imposta 5/4 su M" + juce::String(pos.barIndex));
        menu.addItem(5, "Imposta 7/8 su M" + juce::String(pos.barIndex));
        menu.addItem(6, "Imposta 12/8 su M" + juce::String(pos.barIndex));
        menu.addItem(7, "Imposta 2/4 su M" + juce::String(pos.barIndex));
        menu.addItem(8, "Imposta 9/8 su M" + juce::String(pos.barIndex));
        menu.addSeparator();
        menu.addItem(50, "Inserisci cambio BPM qui (" + juce::String(pos.localBpm, 1) + " BPM)");
        menu.addItem(60, "Apri Time & Metric Manager...");
        menu.addSeparator();
        menu.addItem(100, "Rimuovi cambio metrica vicino a M" + juce::String(pos.barIndex), barStartQ > 1.05);
        menu.addItem(101, "Ripristina metrica fissa su tutto il brano");

        const double curBpmHere = pos.localBpm;
        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, barStartQ, snappedBeat, curBpmHere](int result) {
            if (safeThis == nullptr || result == 0) return;

            if (result == 60)
            {
                safeThis->openTimeMetricManager();
                return;
            }

            juce::ScopedValueSetter<bool> guard(safeThis->isUpdatingControls, true);
            int num = 4, den = 4;
            if (meterIdToSignature(result, num, den))
            {
                safeThis->beatTimeMap.addOrUpdateMeterChange(barStartQ, num, den);
            }
            else if (result == 50)
            {
                safeThis->beatTimeMap.addOrUpdateTempoChange(snappedBeat, curBpmHere);
            }
            else if (result == 100)
            {
                safeThis->beatTimeMap.removeMeterChangeNear(barStartQ, 1.0);
            }
            else if (result == 101)
            {
                safeThis->beatTimeMap.setFixedMeter(
                    safeThis->beatTimeMap.getBaseTimeSigNum(),
                    safeThis->beatTimeMap.getBaseTimeSigDen());
            }

            safeThis->syncControlsFromBeatMap();
            safeThis->updateScrollBarRange();
            safeThis->repaint();
            });
    }

    double getContentDurationSeconds() const
    {
        double maxSec = 30.0;

        const double audioLen = audioEngine.getTotalLengthSeconds();
        if (audioLen > 1.0)
        {
            const double clipEnd = audioLen + audioEngine.getFullMixOffsetSeconds();
            maxSec = juce::jmax(maxSec, juce::jmax(audioLen, clipEnd));
        }

        const double mapEndSec = beatTimeMap.getMapEndSeconds();
        if (mapEndSec > 1.0)
            maxSec = juce::jmax(maxSec, mapEndSec);

        double maxBeat = 64.0;
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (showNode.isValid())
        {
            for (int i = 0; i < showNode.getNumChildren(); ++i)
            {
                auto sc = showNode.getChild(i);
                double endBeat = static_cast<double>(sc.getProperty(StateKeys::StartBeat, 1.0))
                    + static_cast<double>(sc.getProperty(StateKeys::DurationBeats, 4.0));
                if (endBeat > maxBeat)
                    maxBeat = endBeat + 8.0;
            }
        }
        maxSec = juce::jmax(maxSec, beatTimeMap.beatToSeconds(maxBeat));

        if (dawTransportBridge != nullptr && lastDawTransport.isHostActive)
            maxSec = juce::jmax(maxSec, dawTimelinePosSec + 30.0);

        return maxSec;
    }

    double getEffectiveTimelineDurationSeconds() const
    {
        if (isDraggingAudioClip && clipDragFrozenTotalSec > 0.0)
            return clipDragFrozenTotalSec;

        const double contentSec = getContentDurationSeconds();
        if (dawTransportBridge != nullptr)
            return juce::jmax(contentSec + 60.0, dawWorkspaceEndSec);

        return contentSec;
    }

    void scrubToMouseX(int mouseX)
    {
        if (dawTransportBridge != nullptr)
        {
            if (onDawSeekBeat)
            {
                double targetSec = pixelXToTime(mouseX);
                double targetBeat = beatTimeMap.secondsToBeat(targetSec);
                if (snapBtn.getToggleState())
                    targetBeat = snapBeatToActiveGrid(targetBeat, false);

                targetBeat = juce::jmax(1.0, targetBeat);

                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                const bool dragMovedEnough = std::abs(targetBeat - lastDawSeekBeatSent) > 0.005;
                if (lastDawSeekTimeMs <= 0.0 || (dragMovedEnough && (nowMs - lastDawSeekTimeMs) >= 35.0))
                {
                    lastDawSeekBeatSent = targetBeat;
                    lastDawSeekTimeMs = nowMs;
                    onDawSeekBeat(targetBeat);
                }

                // Aggiornamento immediato playhead VST prima del prossimo blocco ASIO
                smoothDawPpq = targetBeat - 1.0;
                lastRawDawPpq = -1.0;
                dawTimelinePosSec = beatTimeMap.beatToSeconds(targetBeat);
                smoothDawSec = dawTimelinePosSec;

                if (onBeatPositionChanged)
                    onBeatPositionChanged(targetBeat);
                repaint();
            }
            return;
        }

        if (waveformBounds.getWidth() <= 0) return;

        double targetSec = pixelXToTime(mouseX);
        if (snapBtn.getToggleState())
        {
            const double rawBeat = beatTimeMap.secondsToBeat(targetSec);
            const double snappedBeat = snapBeatToActiveGrid(rawBeat, false);
            const double totalSec = getEffectiveTimelineDurationSeconds();
            targetSec = juce::jlimit(0.0, totalSec, beatTimeMap.beatToSeconds(snappedBeat));
        }

        if (audioEngine.hasActiveAudioDevice())
            audioEngine.setPositionSeconds(targetSec);
        else
            fallbackPosSec = targetSec;

        if (onBeatPositionChanged)
            onBeatPositionChanged(beatTimeMap.secondsToBeat(targetSec));
        repaint();
    }

    void selectSceneAtMouseX(int mouseX)
    {
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || sceneLaneBounds.getWidth() <= 0) return;

        const double clickedBeat = beatTimeMap.secondsToBeat(pixelXToTime(mouseX));

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            double durB = static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0));
            if (clickedBeat >= startB && clickedBeat <= (startB + durB))
            {
                selectedSceneIndex = i;
                if (onSceneSelectedFromTimeline)
                    onSceneSelectedFromTimeline(i);
                repaint();
                break;
            }
        }
    }

    int getClipHeaderHeight() const noexcept
    {
        return juce::jmin(16, juce::jmax(10, (waveformBounds.getHeight() - 2) / 4));
    }

    void rebuildWaveformDragCache()
    {
        waveformDragCache = juce::Image();
        const int waveW = waveformBounds.getWidth();
        const int headerH = getClipHeaderHeight();
        const int bodyH = juce::jmax(1, waveformBounds.getHeight() - 2 - headerH);
        const double fileLen = thumbnail.getTotalLength();

        if (waveW <= 0 || bodyH <= 0 || clipDragVisSec <= 0.0001 || fileLen <= 0.01)
            return;

        cachePadPx = juce::jmin(waveW, 1200);
        cachePxPerSec = static_cast<double>(waveW) / clipDragVisSec;
        cacheBaseOffsetSec = clipDragCurrentOffsetSec;

        const int cacheW = waveW + 2 * cachePadPx;
        waveformDragCache = juce::Image(juce::Image::ARGB, cacheW, bodyH, true);
        juce::Graphics imgG(waveformDragCache);

        const double cacheTimelineStart = clipDragViewStartSec - (static_cast<double>(cachePadPx) / cachePxPerSec);
        const double cacheTimelineEnd = clipDragViewStartSec + clipDragVisSec + (static_cast<double>(cachePadPx) / cachePxPerSec);
        const double fileStart = cacheTimelineStart - cacheBaseOffsetSec;
        const double fileEnd = cacheTimelineEnd - cacheBaseOffsetSec;

        const double drawFileStart = juce::jmax(0.0, fileStart);
        const double drawFileEnd = juce::jmin(fileLen, fileEnd);

        if (drawFileEnd > drawFileStart)
        {
            const int ix1 = juce::roundToInt((drawFileStart - fileStart) * cachePxPerSec);
            const int ix2 = juce::roundToInt((drawFileEnd - fileStart) * cachePxPerSec);
            if (ix2 > ix1)
            {
                imgG.setColour(juce::Colour(0xff38bdf8).withAlpha(0.90f));
                thumbnail.drawChannels(imgG, juce::Rectangle<int>(ix1, 0, ix2 - ix1, bodyH),
                    drawFileStart, drawFileEnd, 1.0f);
            }
        }
    }

    void drawFullMixClip(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        clipHeaderBounds = {};
        const double visSec = vEndSec - vStartSec;
        const double fileLen = thumbnail.getTotalLength();
        if (visSec <= 0.001 || fileLen <= 0.01 || waveformBounds.getWidth() <= 0)
            return;

        const double offsetSec = isDraggingAudioClip
            ? clipDragCurrentOffsetSec
            : audioEngine.getFullMixOffsetSeconds();

        const double clipStartTimeline = offsetSec;
        const double clipEndTimeline = offsetSec + fileLen;

        const double visClipStart = juce::jmax(vStartSec, clipStartTimeline);
        const double visClipEnd = juce::jmin(vEndSec, clipEndTimeline);
        if (visClipEnd <= visClipStart)
            return;

        const int x1 = timeToPixelX(visClipStart, vStartSec, visSec);
        const int x2 = timeToPixelX(visClipEnd, vStartSec, visSec);
        const int w = juce::jmax(4, x2 - x1);

        auto fullClipRect = juce::Rectangle<int>(x1, waveformBounds.getY() + 1, w, waveformBounds.getHeight() - 2)
            .getIntersection(waveformBounds.reduced(1));
        if (fullClipRect.isEmpty())
            return;

        auto clipRect = fullClipRect;

        g.setColour(isDraggingAudioClip ? juce::Colour(0xff1e3a5f).withAlpha(0.55f)
            : juce::Colour(0xff172554).withAlpha(0.32f));
        g.fillRect(clipRect);

        const int headerH = getClipHeaderHeight();
        clipHeaderBounds = clipRect.removeFromTop(headerH);

        g.setColour(isDraggingAudioClip ? juce::Colour(0xff0ea5e9) : juce::Colour(0xff1e3a8a).withAlpha(0.90f));
        g.fillRect(clipHeaderBounds);

        const int offsetMs = juce::roundToInt(offsetSec * 1000.0);
        juce::String clipLabel = audioEngine.getCurrentFullMixFile().getFileName()
            + "   [Offset: " + (offsetMs >= 0 ? "+" : "") + juce::String(offsetMs) + " ms]"
            + "   (trascina barra o Alt+Drag)";

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(clipLabel, clipHeaderBounds.reduced(5, 0), juce::Justification::centredLeft, true);

        if (!clipRect.isEmpty())
        {
            if (isDraggingAudioClip)
            {
                int shiftPx = juce::roundToInt((offsetSec - cacheBaseOffsetSec) * cachePxPerSec);
                if (!waveformDragCache.isValid() || std::abs(shiftPx) > juce::jmax(16, cachePadPx - 32))
                {
                    rebuildWaveformDragCache();
                    shiftPx = 0;
                }

                if (waveformDragCache.isValid())
                {
                    g.saveState();
                    g.reduceClipRegion(clipRect);
                    g.drawImageAt(waveformDragCache, waveformBounds.getX() - cachePadPx + shiftPx, clipRect.getY());
                    g.restoreState();
                }
            }
            else
            {
                const double audioStart = juce::jmax(0.0, visClipStart - offsetSec);
                const double audioEnd = juce::jmin(fileLen, visClipEnd - offsetSec);
                if (audioEnd > audioStart)
                {
                    g.setColour(juce::Colour(0xff3b82f6).withAlpha(0.78f));
                    thumbnail.drawChannels(g, clipRect, audioStart, audioEnd, 1.0f);
                }
            }
        }

        g.setColour(isDraggingAudioClip ? juce::Colour(0xff38bdf8) : juce::Colour(0xff3b82f6).withAlpha(0.65f));
        g.drawRect(fullClipRect, 1);
    }

    // Righello + Griglia Adattiva Gerarchica (Misure -> 1/2 -> 1/4 -> 1/8 -> 1/16 -> 1/32 -> 1/64 -> 1/128)
    void drawRulerAndBeatGrid(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        rulerMarkerHits.clear();
        activeGridStepQ = 0.0;
        activeGridLabel.clear();

        const double visSec = vEndSec - vStartSec;
        if (visSec <= 0.0001 || waveformBounds.getWidth() <= 0)
            return;

        const double viewStartBeat = beatTimeMap.secondsToBeat(vStartSec);
        const double viewEndBeat = beatTimeMap.secondsToBeat(vEndSec);
        const int leftX = waveformBounds.getX();
        const int rightX = waveformBounds.getRight();
        const float waveW = static_cast<float>(waveformBounds.getWidth());

        auto beatToX = [&](double beatQ) -> int
            {
                return timeToPixelX(beatTimeMap.beatToSeconds(beatQ), vStartSec, visSec);
            };

        std::vector<BeatTimeMap::MeterAnchor> segments = beatTimeMap.getMeterAnchors();
        if (segments.empty())
        {
            BeatTimeMap::MeterAnchor def;
            def.startBeat = 1.0;
            def.num = beatTimeMap.getBaseTimeSigNum();
            def.den = beatTimeMap.getBaseTimeSigDen();
            def.startBar = 1;
            segments.push_back(def);
        }

        struct SubLevel { double stepQ; const char* label; };
        static constexpr SubLevel kSubLevels[] = {
            { 2.0,     "1/2" },
            { 1.0,     "1/4" },
            { 0.5,     "1/8" },
            { 0.25,    "1/16" },
            { 0.125,   "1/32" },
            { 0.0625,  "1/64" },
            { 0.03125, "1/128" }
        };

        int lastLabelRightX = leftX - 100;

        for (size_t s = 0; s < segments.size(); ++s)
        {
            const auto& seg = segments[s];
            if (seg.startBeat > viewEndBeat + 1e-4)
                break;

            const double segEndQ = (s + 1 < segments.size())
                ? juce::jmin(viewEndBeat + 1e-4, segments[s + 1].startBeat)
                : (viewEndBeat + 1e-4);

            if (segEndQ < viewStartBeat - 1e-4)
                continue;

            const double barLenQ = juce::jmax(0.25, seg.getBarLengthQ());

            const double sampleBeat = juce::jmax(seg.startBeat, viewStartBeat);
            const double secA = beatTimeMap.beatToSeconds(sampleBeat);
            const double secB = beatTimeMap.beatToSeconds(sampleBeat + barLenQ);
            const float pxPerBar = static_cast<float>(((secB - secA) / visSec) * waveW);
            const float pxPerQ = pxPerBar / static_cast<float>(barLenQ);

            int barStep = 1;
            if (pxPerBar < 0.35f) barStep = 128;
            else if (pxPerBar < 0.70f) barStep = 64;
            else if (pxPerBar < 1.30f) barStep = 32;
            else if (pxPerBar < 2.50f) barStep = 16;
            else if (pxPerBar < 5.00f) barStep = 8;
            else if (pxPerBar < 10.0f) barStep = 4;
            else if (pxPerBar < 18.0f) barStep = 2;

            // Sceglie la suddivisione musicale piu' fitta leggibile (>= 7 px tra due linee) fino a 1/128
            double finestSubQ = 0.0;
            juce::String segGridLabel = (barStep > 1) ? ("M" + juce::String(barStep)) : "1 Bar";
            if (barStep == 1)
            {
                for (const auto& lv : kSubLevels)
                {
                    if (lv.stepQ >= barLenQ - 1e-5)
                        continue;
                    if (static_cast<float>(lv.stepQ) * pxPerQ >= 7.0f)
                    {
                        finestSubQ = lv.stepQ;
                        segGridLabel = lv.label;
                    }
                }
            }

            if (activeGridLabel.isEmpty())
            {
                activeGridStepQ = (finestSubQ > 0.0) ? finestSubQ : (barLenQ * static_cast<double>(barStep));
                activeGridLabel = segGridLabel;
            }

            // Badge di contesto a sinistra se il segmento e' iniziato fuori dallo schermo
            if (seg.startBeat < viewStartBeat - 1e-4)
            {
                const juce::String sigTxt = juce::String(seg.num) + "/" + juce::String(seg.den);
                const int badgeW = (sigTxt.length() >= 4) ? 32 : 26;
                juce::Rectangle<int> badgeRect(leftX + 2, rulerBounds.getY() + 2, badgeW, 13);
                g.setColour(juce::Colour(0xff312e81));
                g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xffc4b5fd));
                g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(sigTxt, badgeRect, juce::Justification::centred, false);
                lastLabelRightX = juce::jmax(lastLabelRightX, badgeRect.getRight() + 4);
            }

            const int firstK = juce::jmax(0,
                static_cast<int>(std::floor((viewStartBeat - seg.startBeat) / barLenQ)));

            for (int k = firstK; ; ++k)
            {
                const double barQ = seg.startBeat + static_cast<double>(k) * barLenQ;
                if (barQ >= segEndQ - 1e-4)
                    break;

                const double barEndQ = juce::jmin(segEndQ, barQ + barLenQ);
                const int barNum = seg.startBar + k;
                const bool isMeterStart = (k == 0);
                const bool shouldDrawLine = isMeterStart || ((barNum - 1) % barStep == 0);
                const int x = beatToX(barQ);

                if (x >= leftX && x <= rightX)
                {
                    if (shouldDrawLine)
                    {
                        if (isMeterStart && s > 0)
                        {
                            g.setColour(juce::Colour(0xffa78bfa).withAlpha(0.85f));
                            g.drawVerticalLine(x,
                                static_cast<float>(rulerBounds.getY()),
                                static_cast<float>(waveformBounds.getBottom()));
                        }
                        else
                        {
                            const bool majorBar = (barNum == 1) || ((barNum - 1) % (barStep * 4) == 0);
                            g.setColour(majorBar ? juce::Colour(0xff3b3e57) : juce::Colour(0xff2c2f42));
                            g.drawVerticalLine(x,
                                static_cast<float>(waveformBounds.getY()),
                                static_cast<float>(waveformBounds.getBottom()));

                            g.setColour(juce::Colour(0xff64748b));
                            g.drawVerticalLine(x,
                                static_cast<float>(rulerBounds.getBottom() - (majorBar ? 11 : 8)),
                                static_cast<float>(rulerBounds.getBottom()));
                        }
                    }

                    // Badge Metrica (draggable se s > 0)
                    if (isMeterStart)
                    {
                        const juce::String sigTxt = juce::String(seg.num) + "/" + juce::String(seg.den);
                        const int badgeW = (sigTxt.length() >= 4) ? 34 : 28;
                        const int badgeX = juce::jlimit(leftX + 1, juce::jmax(leftX + 1, rightX - badgeW - 1), x + 2);
                        juce::Rectangle<int> badgeRect(badgeX, rulerBounds.getY() + 2, badgeW, 14);

                        const bool isBeingDragged = isDraggingMarker
                            && draggingMarkerKind == RulerMarkerHit::Kind::Meter
                            && draggingMarkerIndex == static_cast<int>(s);

                        g.setColour(isBeingDragged ? juce::Colour(0xff9333ea)
                            : (s == 0 ? juce::Colour(0xff312e81) : juce::Colour(0xff6d28d9)));
                        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
                        g.setColour(isBeingDragged ? juce::Colours::white : juce::Colour(0xffc4b5fd));
                        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                        g.drawText(sigTxt, badgeRect, juce::Justification::centred, false);

                        rulerMarkerHits.push_back({ RulerMarkerHit::Kind::Meter, static_cast<int>(s), badgeRect, seg.startBeat });
                        lastLabelRightX = juce::jmax(lastLabelRightX, badgeRect.getRight() + 4);
                    }
                    else if (shouldDrawLine && x > lastLabelRightX + 4 && x < rightX - 24)
                    {
                        const juce::String barTxt = "M" + juce::String(barNum);
                        g.setColour(juce::Colour(0xffcbd5e1));
                        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                        g.drawText(barTxt, x + 3, rulerBounds.getY() + 1, 36, 14, juce::Justification::centredLeft, false);
                        lastLabelRightX = x + 30;
                    }
                }

                // Suddivisioni adattive dentro la misura (1/2 -> 1/4 -> 1/8 -> 1/16 -> 1/32 -> 1/64 -> 1/128)
                if (finestSubQ > 0.0)
                {
                    const int firstSub = juce::jmax(1,
                        static_cast<int>(std::floor((viewStartBeat - barQ) / finestSubQ)));
                    const int lastSub = static_cast<int>(std::ceil((barEndQ - barQ) / finestSubQ)) - 1;

                    for (int sub = firstSub; sub <= lastSub; ++sub)
                    {
                        const double offsetQ = static_cast<double>(sub) * finestSubQ;
                        const double subQ = barQ + offsetQ;
                        if (subQ >= barEndQ - 1e-5)
                            break;

                        const int sx = beatToX(subQ);
                        if (sx <= leftX || sx >= rightX)
                            continue;

                        // Classifica gerarchica in unita' di 1/128 (1 Q = 32 unita' di 1/128)
                        const int u128 = juce::roundToInt(offsetQ * 32.0);
                        juce::Colour lineCol;
                        int tickH = 3;

                        if (u128 % 64 == 0) { lineCol = juce::Colour(0xff2c3148); tickH = 7; } // 1/2
                        else if (u128 % 32 == 0) { lineCol = juce::Colour(0xff262a3d); tickH = 6; } // 1/4
                        else if (u128 % 16 == 0) { lineCol = juce::Colour(0xff212434); tickH = 5; } // 1/8
                        else if (u128 % 8 == 0) { lineCol = juce::Colour(0xff1d202d); tickH = 4; } // 1/16
                        else if (u128 % 4 == 0) { lineCol = juce::Colour(0xff1a1d28); tickH = 3; } // 1/32
                        else if (u128 % 2 == 0) { lineCol = juce::Colour(0xff181a24); tickH = 2; } // 1/64
                        else { lineCol = juce::Colour(0xff161821); tickH = 2; } // 1/128

                        g.setColour(lineCol);
                        g.drawVerticalLine(sx,
                            static_cast<float>(waveformBounds.getY()),
                            static_cast<float>(waveformBounds.getBottom()));

                        g.setColour(juce::Colour(0xff475569).withAlpha(u128 % 32 == 0 ? 0.85f : 0.45f));
                        g.drawVerticalLine(sx,
                            static_cast<float>(rulerBounds.getBottom() - tickH),
                            static_cast<float>(rulerBounds.getBottom()));

                        // Etichette intermedie sul righello quando c'e' abbastanza spazio (es. M1.2, M1.3, M1.2.2)
                        const bool isQuarter = (u128 % 32 == 0);
                        const bool isEighth = (u128 % 16 == 0);
                        const bool isSixteen = (u128 % 8 == 0);

                        if ((isQuarter && pxPerQ >= 38.0f) ||
                            (isEighth && pxPerQ >= 96.0f) ||
                            (isSixteen && pxPerQ >= 220.0f))
                        {
                            if (sx > lastLabelRightX + 4 && sx < rightX - 28)
                            {
                                const int qIdx = static_cast<int>(std::floor(offsetQ + 1e-6)) + 1;
                                const int s16Idx = (juce::roundToInt((offsetQ - std::floor(offsetQ + 1e-6)) * 4.0)) + 1;
                                juce::String subTxt = juce::String(barNum) + "." + juce::String(qIdx);
                                if (!isQuarter)
                                    subTxt += "." + juce::String(s16Idx);

                                g.setColour(juce::Colour(0xff64748b));
                                g.setFont(juce::FontOptions(9.0f));
                                g.drawText(subTxt, sx + 2, rulerBounds.getY() + 2, 34, 12,
                                    juce::Justification::centredLeft, false);
                                lastLabelRightX = sx + 28;
                            }
                        }
                    }
                }
            }
        }

        // Fascia bassa del righello: Marker BPM dalla Tempo Map (draggable se i > 0)
        const auto& tempoAnchors = beatTimeMap.getBeatAnchors();
        if (!tempoAnchors.empty())
        {
            int lastBpmRightX = leftX - 100;

            for (size_t i = 0; i < tempoAnchors.size(); ++i)
            {
                const auto& a = tempoAnchors[i];
                if (a.timeSec < vStartSec || a.timeSec > vEndSec)
                    continue;

                const int x = timeToPixelX(a.timeSec, vStartSec, visSec);
                if (x < leftX || x > rightX) continue;

                if (i > 0)
                {
                    g.setColour(juce::Colour(0xfff59e0b).withAlpha(0.75f));
                    g.drawVerticalLine(x,
                        static_cast<float>(rulerBounds.getY() + 15),
                        static_cast<float>(waveformBounds.getBottom()));
                }

                if (x > lastBpmRightX + 2 && x < rightX - 30)
                {
                    const bool isBeingDragged = isDraggingMarker
                        && draggingMarkerKind == RulerMarkerHit::Kind::Tempo
                        && draggingMarkerIndex == static_cast<int>(i);

                    juce::String bpmTxt = juce::String(juce::roundToInt(a.localBpm));
                    juce::Rectangle<int> tag(x + 2, rulerBounds.getBottom() - 13, 28, 12);
                    g.setColour(isBeingDragged ? juce::Colour(0xffd97706) : juce::Colour(0xff78350f));
                    g.fillRoundedRectangle(tag.toFloat(), 2.0f);
                    g.setColour(isBeingDragged ? juce::Colours::white : juce::Colour(0xfff59e0b));
                    g.drawRoundedRectangle(tag.toFloat(), 2.0f, 0.8f);
                    g.setColour(juce::Colour(0xfffef08a));
                    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
                    g.drawText(bpmTxt, tag, juce::Justification::centred, false);

                    rulerMarkerHits.push_back({ RulerMarkerHit::Kind::Tempo, static_cast<int>(i), tag, a.beat });
                    lastBpmRightX = tag.getRight() + 3;
                }
            }
        }
    }

    void drawTempoMapOverlay(juce::Graphics& g, double vStartSec, double vEndSec, bool noAudioLoaded)
    {
        const double visSec = vEndSec - vStartSec;
        if (visSec <= 0.001 || waveformBounds.getWidth() <= 0 || waveformBounds.getHeight() < 20)
            return;

        const auto& anchors = beatTimeMap.getBeatAnchors();
        double minBpm = beatTimeMap.getBaseBpm();
        double maxBpm = beatTimeMap.getBaseBpm();
        for (const auto& a : anchors)
        {
            minBpm = juce::jmin(minBpm, a.localBpm);
            maxBpm = juce::jmax(maxBpm, a.localBpm);
        }
        if (maxBpm - minBpm < 20.0)
        {
            const double mid = 0.5 * (minBpm + maxBpm);
            minBpm = mid - 10.0;
            maxBpm = mid + 10.0;
        }
        else
        {
            minBpm -= 5.0;
            maxBpm += 5.0;
        }

        const float topY = static_cast<float>(waveformBounds.getY() + (noAudioLoaded ? 18 : 20));
        const float botY = static_cast<float>(waveformBounds.getBottom() - 6);
        auto bpmToY = [&](double bpm) -> float
            {
                const float norm = static_cast<float>(juce::jlimit(0.0, 1.0, (bpm - minBpm) / (maxBpm - minBpm)));
                return botY - norm * (botY - topY);
            };

        const float leftF = static_cast<float>(waveformBounds.getX() + 1);
        const float rightF = static_cast<float>(waveformBounds.getRight() - 1);

        double curBpm = beatTimeMap.getBpmAtBeat(beatTimeMap.secondsToBeat(vStartSec));
        float curY = bpmToY(curBpm);

        juce::Path linePath;
        linePath.startNewSubPath(leftF, curY);

        juce::Path fillPath;
        fillPath.startNewSubPath(leftF, botY);
        fillPath.lineTo(leftF, curY);

        for (const auto& a : anchors)
        {
            if (a.timeSec <= vStartSec) continue;
            if (a.timeSec >= vEndSec)   break;

            const float x = static_cast<float>(timeToPixelX(a.timeSec, vStartSec, visSec));
            const float nextY = bpmToY(a.localBpm);
            linePath.lineTo(x, curY);
            linePath.lineTo(x, nextY);
            fillPath.lineTo(x, curY);
            fillPath.lineTo(x, nextY);
            curY = nextY;
        }

        linePath.lineTo(rightF, curY);
        fillPath.lineTo(rightF, curY);
        fillPath.lineTo(rightF, botY);
        fillPath.closeSubPath();

        if (noAudioLoaded)
        {
            g.setColour(juce::Colour(0xfff59e0b).withAlpha(0.11f));
            g.fillPath(fillPath);

            juce::String badge = "MIDI Map: "
                + juce::String(static_cast<int>(anchors.size())) + " BPM, "
                + juce::String(static_cast<int>(beatTimeMap.getMeterAnchors().size())) + " Metri (doppio click su marker = Manager)";

            g.setColour(juce::Colour(0xff0b0c12).withAlpha(0.75f));
            auto pill = juce::Rectangle<int>(waveformBounds.getX() + 6, waveformBounds.getY() + 3,
                juce::jmin(waveformBounds.getWidth() - 104, 370), 15);
            g.fillRoundedRectangle(pill.toFloat(), 3.0f);
            g.setColour(juce::Colour(0xfffbbf24));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(badge, pill.reduced(5, 0), juce::Justification::centredLeft, true);
        }

        g.setColour(juce::Colour(0xfff59e0b).withAlpha(noAudioLoaded ? 0.85f : 0.45f));
        g.strokePath(linePath, juce::PathStrokeType(noAudioLoaded ? 1.8f : 1.2f));
    }

    void drawProjectMarkers(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        const auto& markers = beatTimeMap.getProjectMarkers();
        if (markers.empty() || waveformBounds.getWidth() <= 0)
            return;

        const double visSec = vEndSec - vStartSec;
        if (visSec <= 0.0001)
            return;

        const int leftX = waveformBounds.getX();
        const int rightX = waveformBounds.getRight();

        g.saveState();
        g.reduceClipRegion(waveformBounds.getUnion(sceneLaneBounds));

        // 1. Prima le Region (bande titolo brano in alto nella waveform)
        for (const auto& pm : markers)
        {
            if (!pm.isRegion)
                continue;

            const double sSec = beatTimeMap.beatToSeconds(pm.beat);
            const double eSec = beatTimeMap.beatToSeconds(pm.endBeat);
            if (eSec < vStartSec || sSec > vEndSec)
                continue;

            const int x1 = juce::jlimit(leftX, rightX, timeToPixelX(sSec, vStartSec, visSec));
            const int x2 = juce::jlimit(leftX, rightX, timeToPixelX(eSec, vStartSec, visSec));
            const int w = juce::jmax(4, x2 - x1);

            juce::Rectangle<int> barRect(x1, waveformBounds.getY() + 1, w, 16);
            g.setColour(pm.colour.withAlpha(0.28f));
            g.fillRect(barRect);
            g.setColour(pm.colour.withAlpha(0.85f));
            g.drawRect(barRect, 1);

            g.setColour(pm.colour.withAlpha(0.35f));
            g.drawVerticalLine(x1, static_cast<float>(waveformBounds.getY()), static_cast<float>(sceneLaneBounds.getBottom()));
            g.drawVerticalLine(x2, static_cast<float>(waveformBounds.getY()), static_cast<float>(sceneLaneBounds.getBottom()));

            if (w >= 26)
            {
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(pm.name, barRect.reduced(4, 0), juce::Justification::centredLeft, true);
            }
        }

        // 2. Poi i Marker puntuali (bandierina + linea verticale fino alle scene)
        for (const auto& pm : markers)
        {
            if (pm.isRegion)
                continue;

            const double sSec = beatTimeMap.beatToSeconds(pm.beat);
            if (sSec < vStartSec || sSec > vEndSec)
                continue;

            const int x = timeToPixelX(sSec, vStartSec, visSec);
            if (x < leftX || x > rightX)
                continue;

            g.setColour(pm.colour.withAlpha(0.75f));
            g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(sceneLaneBounds.getBottom()));

            const int tagW = juce::jlimit(36, 130, pm.name.length() * 6 + 14);
            const int tagX = juce::jmin(rightX - tagW - 2, x + 2);
            juce::Rectangle<int> tag(tagX, waveformBounds.getY() + 18, tagW, 15);
            g.setColour(pm.colour.darker(0.45f).withAlpha(0.92f));
            g.fillRoundedRectangle(tag.toFloat(), 3.0f);
            g.setColour(pm.colour);
            g.drawRoundedRectangle(tag.toFloat(), 3.0f, 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText(pm.name, tag.reduced(4, 0), juce::Justification::centredLeft, true);
        }

        g.restoreState();
    }

    void drawSceneLane(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        g.setColour(juce::Colour(0xff16161d));
        g.fillRect(sceneLaneBounds);

        const double visSec = vEndSec - vStartSec;
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);

        g.saveState();
        g.reduceClipRegion(sceneLaneBounds.reduced(1));

        // Guide orizzontali delle corsie parallele
        for (int l = 1; l < totalSceneLanes; ++l)
        {
            const int ly = getLaneTopY(l) - 2;
            g.setColour(juce::Colour(0xff232533));
            g.drawHorizontalLine(ly, static_cast<float>(sceneLaneBounds.getX()), static_cast<float>(sceneLaneBounds.getRight()));
        }

        if (showNode.isValid() && visSec > 0.001 && sceneLaneBounds.getWidth() > 0)
        {
            for (int i = 0; i < showNode.getNumChildren(); ++i)
            {
                auto scene = showNode.getChild(i);
                const double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
                const double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));

                const double startSec = beatTimeMap.beatToSeconds(startB);
                const double endSec = beatTimeMap.beatToSeconds(startB + durB);
                if (endSec < vStartSec || startSec > vEndSec)
                    continue;

                const int x1 = timeToPixelX(startSec, vStartSec, visSec);
                const int x2 = timeToPixelX(endSec, vStartSec, visSec);
                const int w = juce::jmax(6, x2 - x1);

                const int laneIdx = (i < static_cast<int>(sceneLaneByIndex.size())) ? sceneLaneByIndex[static_cast<size_t>(i)] : 0;
                const int yTop = getLaneTopY(laneIdx);

                juce::Rectangle<int> rawRect(x1, yTop, w, kSceneLaneHeight - 3);
                auto blockRect = rawRect.getIntersection(sceneLaneBounds.reduced(1));

                if (!blockRect.isEmpty())
                {
                    const bool isSel = (i == selectedSceneIndex);
                    bool hasSwatch = false;
                    juce::Colour swatchCol;
                    const juce::Colour baseCol = getSceneBlockColour(scene, hasSwatch, swatchCol);

                    g.setColour(isSel ? baseCol.brighter(0.45f) : baseCol.withAlpha(0.88f));
                    g.fillRoundedRectangle(blockRect.toFloat(), 3.0f);

                    if (hasSwatch && blockRect.getWidth() >= 10)
                    {
                        g.setColour(swatchCol);
                        g.fillRoundedRectangle(static_cast<float>(blockRect.getX() + 1),
                            static_cast<float>(blockRect.getY() + 1),
                            4.0f,
                            static_cast<float>(blockRect.getHeight() - 2),
                            2.0f);
                    }

                    g.setColour(isSel ? juce::Colours::white : baseCol.brighter(0.6f).withAlpha(0.75f));
                    g.drawRoundedRectangle(blockRect.toFloat(), 3.0f, isSel ? 1.6f : 1.0f);

                    const juce::String name = scene.getProperty(StateKeys::SceneName, "Scena").toString();
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::FontOptions(10.5f, isSel ? juce::Font::bold : juce::Font::plain));
                    const int textLeftPad = hasSwatch ? 8 : 4;

                    const bool showWarn = sceneNeedsDimmerWarning(showNode, i) && blockRect.getWidth() >= 18;
                    g.drawText(name, blockRect.withTrimmedLeft(textLeftPad).withTrimmedRight(showWarn ? 19 : 3),
                        juce::Justification::centredLeft, true);

                    if (showWarn)
                    {
                        auto warnPill = juce::Rectangle<int>(blockRect.getRight() - 16, blockRect.getY() + 2, 14, blockRect.getHeight() - 4);
                        g.setColour(juce::Colour(0xfff59e0b));
                        g.fillRoundedRectangle(warnPill.toFloat(), 2.5f);
                        g.setColour(juce::Colour(0xff09090d));
                        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
                        g.drawText("!", warnPill, juce::Justification::centred, false);
                    }
                }
            }
        }

        g.restoreState();

        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(sceneLaneBounds, 1);
    }

    void updateDawControlStates()
    {
        if (dawTransportBridge != nullptr)
        {
            const bool canControlDaw = reaperSyncEnabled && static_cast<bool>(onDawTogglePlayPause);

            bpmSlider.setEnabled(false);
            bpmSlider.setTooltip("BPM sincronizzati automaticamente dalla DAW (non modificabili in VST)");
            meterCombo.setEnabled(false);
            meterCombo.setTooltip("Metrica sincronizzata automaticamente dalla DAW (non modificabile in VST)");

            clickBtn.setToggleState(false, juce::dontSendNotification);
            clickBtn.setEnabled(false);
            clickVolSlider.setEnabled(false);

            playPauseBtn.setEnabled(canControlDaw);
            playPauseBtn.setTooltip(canControlDaw
                ? "Play / Pausa bidirezionale con Reaper (Spazio)"
                : "Transport guidato dalla DAW");

            stopBtn.setEnabled(canControlDaw);
            stopBtn.setTooltip(canControlDaw
                ? "Stop Reaper (premi ancora per tornare a 00:00)"
                : "Transport guidato dalla DAW");
            updatePlayButtonLabel();
        }
        else
        {
            bpmSlider.setEnabled(true);
            meterCombo.setEnabled(true);
            playPauseBtn.setEnabled(true);
            playPauseBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
            stopBtn.setEnabled(true);
            clickBtn.setEnabled(true);
            clickVolSlider.setEnabled(true);
            updatePlayButtonLabel();
        }
    }

    static void inspectSceneAttributes(const juce::ValueTree& scene,
        bool& outDim, bool& outCol, bool& outMove, bool& outStrobe)
    {
        outDim = outCol = outMove = outStrobe = false;
        const bool useCol = !scene.hasProperty(StateKeys::PresetUseColor) || static_cast<bool>(scene.getProperty(StateKeys::PresetUseColor));
        const bool useMove = !scene.hasProperty(StateKeys::PresetUseMove) || static_cast<bool>(scene.getProperty(StateKeys::PresetUseMove));
        const bool useStrobe = !scene.hasProperty(StateKeys::PresetUseStrobe) || static_cast<bool>(scene.getProperty(StateKeys::PresetUseStrobe));
        const bool useDim = !scene.hasProperty(StateKeys::PresetUseDimmer) || static_cast<bool>(scene.getProperty(StateKeys::PresetUseDimmer));

        for (int c = 0; c < scene.getNumChildren(); ++c)
        {
            auto cmd = scene.getChild(c);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            const juce::String param = cmd.getProperty(StateKeys::TargetParam, "").toString();
            const juce::String curve = cmd.getProperty(StateKeys::CurveType, "").toString();

            if (param.equalsIgnoreCase("Red") || param.equalsIgnoreCase("Green") ||
                param.equalsIgnoreCase("Blue") || param.equalsIgnoreCase("White") ||
                param.equalsIgnoreCase("UV"))
            {
                if (useCol) outCol = true;
            }
            else if (param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt"))
            {
                if (useMove) outMove = true;
            }
            else if (param.equalsIgnoreCase("Strobe") ||
                (param.equalsIgnoreCase("Dimmer") && curve.startsWithIgnoreCase("Square")))
            {
                if (useStrobe) outStrobe = true;
            }
            else if (param.equalsIgnoreCase("Dimmer") || param.equalsIgnoreCase("GeyserBlast"))
            {
                if (useDim) outDim = true;
            }
        }
    }

    static bool sceneNeedsDimmerWarning(const juce::ValueTree& showNode, int sceneIndex)
    {
        auto sc = showNode.getChild(sceneIndex);
        bool hasDim = false, hasCol = false, hasMove = false, hasStrobe = false;
        inspectSceneAttributes(sc, hasDim, hasCol, hasMove, hasStrobe);

        if (hasDim || (!hasCol && !hasMove && !hasStrobe))
            return false;

        const double sA = static_cast<double>(sc.getProperty(StateKeys::StartBeat, 1.0));
        const double eA = sA + juce::jmax(0.25, static_cast<double>(sc.getProperty(StateKeys::DurationBeats, 4.0)));

        for (int j = 0; j < showNode.getNumChildren(); ++j)
        {
            if (j == sceneIndex) continue;
            auto other = showNode.getChild(j);
            const double sB = static_cast<double>(other.getProperty(StateKeys::StartBeat, 1.0));
            const double eB = sB + juce::jmax(0.25, static_cast<double>(other.getProperty(StateKeys::DurationBeats, 4.0)));

            if (eB > sA + 1e-4 && sB < eA - 1e-4)
            {
                bool oDim = false, oCol = false, oMove = false, oStrobe = false;
                inspectSceneAttributes(other, oDim, oCol, oMove, oStrobe);
                if (oDim)
                    return false;
            }
        }
        return true;
    }

    juce::ValueTree& appState;
    StemAudioEngine& audioEngine;
    BeatTimeMap& beatTimeMap;

    juce::AudioThumbnailCache thumbnailCache{ 5 };
    juce::AudioThumbnail thumbnail;

    juce::TextButton playPauseBtn;
    juce::TextButton stopBtn;
    juce::TextButton clickBtn;
    juce::Slider clickVolSlider;
    juce::TextButton loadMixBtn;
    juce::TextButton alignBtn;
    juce::TextButton importMidiMapBtn;
    juce::Slider bpmSlider;
    juce::ComboBox meterCombo;
    juce::TextButton zoomOutBtn, zoomInBtn, zoomFitBtn;
    juce::ScrollBar horizontalScrollBar{ false };

    juce::TextButton snapBtn;

    juce::Rectangle<int> topInfoBounds;
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> waveformBounds;
    juce::Rectangle<int> clipHeaderBounds;
    juce::Rectangle<int> sceneLaneBounds;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::vector<RulerMarkerHit> rulerMarkerHits;
    double activeGridStepQ = 1.0;
    juce::String activeGridLabel;

    bool isDraggingMarker = false;
    RulerMarkerHit::Kind draggingMarkerKind = RulerMarkerHit::Kind::Meter;
    int draggingMarkerIndex = -1;
    double markerDragPreviewBeatQ = 1.0;

    double lastReportedSec = -1.0;
    double lastTimerMs = 0.0;
    double fallbackPosSec = 0.0;
    bool fallbackPlaying = false;

    double zoomFactor = 1.0;
    double viewStartSec = 0.0;
    bool isUpdatingScrollBar = false;

    int selectedSceneIndex = -1;
    bool isScrubbing = false;
    bool isDraggingAudioClip = false;
    float clipDragLastX = 0.0f;
    double clipDragInitialOffsetSec = 0.0;

    bool isUpdatingControls = false;
    bool needsBeatMapSync = false;
    juce::Component::SafePointer<juce::Component> keyListenerTarget;

    juce::File lastThumbnailFile;

    double clipDragCurrentOffsetSec = 0.0;
    double clipDragFrozenTotalSec = 0.0;
    double clipDragVisSec = 0.0;
    double clipDragViewStartSec = 0.0;
    juce::Image waveformDragCache;
    double cacheBaseOffsetSec = 0.0;
    double cachePxPerSec = 1.0;
    int cachePadPx = 0;

    SceneDragMode sceneDragMode = SceneDragMode::None;
    juce::ValueTree draggingSceneNode;
    double sceneDragInitialStartBeat = 1.0;
    double sceneDragInitialDurBeat = 4.0;
    double sceneDragGrabOffsetBeat = 0.0;

    static constexpr int kSceneLaneHeight = 24;
    static constexpr int kMaxVisibleLanes = 5;
    juce::ScrollBar verticalLaneScrollBar{ true };
    std::vector<int> sceneLaneByIndex;
    int totalSceneLanes = 1;
    double laneScrollOffsetPx = 0.0;

    const LockFreeTransportBridge* dawTransportBridge = nullptr;
    DawTransportState lastDawTransport;
    double lastRawDawPpq = -1.0;
    double smoothDawPpq = 0.0;
    double smoothDawSec = 0.0;
    double dawTimelinePosSec = 0.0;
    bool wasDawPlaying = false;
    bool reaperSyncEnabled = false;

    double dawWorkspaceEndSec = 300.0;
    double lastDawSeekTimeMs = 0.0;
    double lastDawSeekBeatSent = -1.0;
};