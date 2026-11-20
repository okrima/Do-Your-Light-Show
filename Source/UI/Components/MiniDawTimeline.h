#pragma once
#include <JuceHeader.h>
#include "../../Audio/StemAudioEngine.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/DataStateKeys.h"
#include <cmath>
#include <vector>

class MiniDawTimeline : public juce::Component,
    private juce::Timer,
    private juce::ChangeListener,
    private juce::ValueTree::Listener,
    private juce::ScrollBar::Listener
{
public:
    std::function<void(int)> onSceneSelectedFromTimeline;
    std::function<void(double)> onBeatPositionChanged;

    MiniDawTimeline(juce::ValueTree& state, StemAudioEngine& engine, BeatTimeMap& beatMap)
        : appState(state),
        audioEngine(engine),
        beatTimeMap(beatMap),
        thumbnail(512, engine.getFormatManager(), thumbnailCache)
    {
        setWantsKeyboardFocus(true);

        audioEngine.startBackgroundThread();
        thumbnail.addChangeListener(this);
        appState.addListener(this);

        playPauseBtn.setButtonText("Play / Pausa");
        playPauseBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
        playPauseBtn.onClick = [this]() {
            if (hasAudioLoaded())
            {
                audioEngine.togglePlayPause();
            }
            else
            {
                virtualPlaying = !virtualPlaying;
                lastTimerMs = juce::Time::getMillisecondCounterHiRes();
                repaint();
            }
            };
        addAndMakeVisible(playPauseBtn);

        stopBtn.setButtonText("Stop");
        stopBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
        stopBtn.onClick = [this]() {
            if (hasAudioLoaded())
            {
                audioEngine.stopAndRewind();
            }
            else
            {
                virtualPlaying = false;
                virtualPositionSec = 0.0;
                if (onBeatPositionChanged)
                    onBeatPositionChanged(beatTimeMap.secondsToBeat(0.0));
            }
            repaint();
            };
        addAndMakeVisible(stopBtn);

        loadMixBtn.setButtonText("Full Mix...");
        loadMixBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4a3e));
        loadMixBtn.setTooltip("Carica la traccia audio Full Mix per waveform e scrubbing");
        loadMixBtn.onClick = [this]() { promptLoadFullMix(); };
        addAndMakeVisible(loadMixBtn);

        importMidiMapBtn.setButtonText("MIDI Map...");
        importMidiMapBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4c1d95));
        importMidiMapBtn.setTooltip("Importa Tempo Map (BPM) e cambi di Metrica da un file .mid (es. Reaper)");
        importMidiMapBtn.onClick = [this]() { promptImportMidiMap(); };
        addAndMakeVisible(importMidiMapBtn);

        bpmSlider.setSliderStyle(juce::Slider::LinearBar);
        bpmSlider.setRange(40.0, 260.0, 0.5);
        bpmSlider.setValue(beatTimeMap.getBaseBpm(), juce::dontSendNotification);
        bpmSlider.setTextValueSuffix(" BPM");
        bpmSlider.setTooltip("Imposta un BPM fisso (rimuove eventuali cambi BPM della Tempo Map)");
        bpmSlider.onValueChange = [this]() {
            if (isUpdatingControls) return;
            juce::ScopedValueSetter<bool> guard(isUpdatingControls, true);
            beatTimeMap.setFixedBpm(bpmSlider.getValue(), 0.0);
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
        meterCombo.setTooltip("Metrica base del brano (tasto destro sul righello per cambi di metrica nel mezzo)");
        meterCombo.onChange = [this]() {
            if (isUpdatingControls) return;
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
        zoomOutBtn.setTooltip("Zoom Out timeline (o rotella mouse giu)");
        zoomOutBtn.onClick = [this]() { zoomAroundCenter(1.0 / 1.5); };
        addAndMakeVisible(zoomOutBtn);

        zoomInBtn.setButtonText("+");
        zoomInBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        zoomInBtn.setTooltip("Zoom In timeline (o rotella mouse su)");
        zoomInBtn.onClick = [this]() { zoomAroundCenter(1.5); };
        addAndMakeVisible(zoomInBtn);

        zoomFitBtn.setButtonText("Fit");
        zoomFitBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        zoomFitBtn.setTooltip("Mostra l'intera durata del brano");
        zoomFitBtn.onClick = [this]() {
            zoomFactor = 1.0;
            viewStartSec = 0.0;
            updateScrollBarRange();
            repaint();
            };
        addAndMakeVisible(zoomFitBtn);

        horizontalScrollBar.setAutoHide(false);
        horizontalScrollBar.addListener(this);
        addAndMakeVisible(horizontalScrollBar);

        lastTimerMs = juce::Time::getMillisecondCounterHiRes();
        syncWithAppState();
        startTimerHz(60);
    }

    ~MiniDawTimeline() override
    {
        stopTimer();
        horizontalScrollBar.removeListener(this);
        thumbnail.removeChangeListener(this);
        appState.removeListener(this);
    }

    void syncWithAppState()
    {
        beatTimeMap.rebuildCacheFromState();
        syncControlsFromBeatMap();

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        if (stemsNode.isValid())
        {
            juce::String fullMixPath = stemsNode.getProperty(StateKeys::StemFullMix, "").toString();
            if (fullMixPath.isNotEmpty())
            {
                juce::File f(fullMixPath);
                if (f.existsAsFile() && f != audioEngine.getCurrentFullMixFile())
                {
                    if (audioEngine.loadFullMix(f, appState))
                    {
                        virtualPlaying = false;
                        thumbnail.setSource(new juce::FileInputSource(f));
                    }
                }
            }
        }

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

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        auto topBar = bounds.removeFromTop(28);

        playPauseBtn.setBounds(topBar.removeFromLeft(82));
        topBar.removeFromLeft(4);
        stopBtn.setBounds(topBar.removeFromLeft(46));
        topBar.removeFromLeft(4);
        loadMixBtn.setBounds(topBar.removeFromLeft(78));
        topBar.removeFromLeft(4);
        importMidiMapBtn.setBounds(topBar.removeFromLeft(80));
        topBar.removeFromLeft(6);
        bpmSlider.setBounds(topBar.removeFromLeft(78));
        topBar.removeFromLeft(4);
        meterCombo.setBounds(topBar.removeFromLeft(56));
        topBar.removeFromLeft(6);
        zoomOutBtn.setBounds(topBar.removeFromLeft(24));
        topBar.removeFromLeft(2);
        zoomInBtn.setBounds(topBar.removeFromLeft(24));
        topBar.removeFromLeft(2);
        zoomFitBtn.setBounds(topBar.removeFromLeft(32));
        topBar.removeFromLeft(6);

        topInfoBounds = topBar;

        bounds.removeFromTop(4);
        rulerBounds = bounds.removeFromTop(28);
        horizontalScrollBar.setBounds(bounds.removeFromBottom(12));
        bounds.removeFromBottom(2);
        sceneLaneBounds = bounds.removeFromBottom(30);
        bounds.removeFromBottom(2);
        waveformBounds = bounds;

        updateScrollBarRange();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121216));

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        const double clampedStart = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), viewStartSec);
        const double viewEndSec = clampedStart + visSec;

        const double currentSec = juce::jlimit(0.0, totalSec, getCurrentPlayheadSeconds());
        const auto musicalPos = beatTimeMap.secondsToMusicalPosition(currentSec);

        // 1. HUD Timecode + Battuta/Metrica + BPM Locale + Quarto
        if (!topInfoBounds.isEmpty())
        {
            g.setColour(juce::Colour(0xff1e1e26));
            g.fillRoundedRectangle(topInfoBounds.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff60a5fa));
            g.setFont(juce::FontOptions(12.5f, juce::Font::bold));

            juce::String statusText = BeatTimeMap::formatTimecode(currentSec)
                + " | " + musicalPos.formatBarString()
                + " | " + juce::String(musicalPos.localBpm, 1) + " BPM"
                + " | Q " + juce::String(musicalPos.totalQuarters, 2);

            g.drawText(statusText, topInfoBounds.reduced(8, 0), juce::Justification::centredRight, true);
        }

        // 2. Sfondo Righello e Forma d'Onda
        g.setColour(juce::Colour(0xff15161e));
        g.fillRect(rulerBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(rulerBounds, 1);

        g.setColour(juce::Colour(0xff18181f));
        g.fillRect(waveformBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(waveformBounds, 1);

        // 3. Righello dinamico e griglia multi-metro nella finestra zoomata
        drawRulerAndBeatGrid(g, clampedStart, viewEndSec);

        // 4. Forma d'Onda del Full Mix + Overlay Tempo Map MIDI
        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour(juce::Colour(0xff3b82f6).withAlpha(0.75f));
            thumbnail.drawChannels(g, waveformBounds.reduced(1), clampedStart, viewEndSec, 1.0f);
        }

        if (beatTimeMap.hasImportedMidiMap())
        {
            drawTempoMapOverlay(g, clampedStart, viewEndSec, thumbnail.getTotalLength() <= 0.0);
        }
        else if (thumbnail.getTotalLength() <= 0.0)
        {
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText("Carica un Full Mix o importa una MIDI Map (rotella = zoom, click/drag = scrub, tasto destro = cambio metrica)",
                waveformBounds.reduced(8, 0), juce::Justification::centred, true);
        }

        // 5. Corsia Scene (SHOW_TIMELINE) nella finestra zoomata
        drawSceneLane(g, clampedStart, viewEndSec);

        // 6. Testina di Lettura (Playhead dal righello fino alla corsia scene)
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

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && (rulerBounds.contains(e.getPosition()) || waveformBounds.contains(e.getPosition())))
        {
            showMeterContextMenu(e.x);
            return;
        }

        if (!e.mods.isLeftButtonDown())
            return;

        if (rulerBounds.contains(e.getPosition()) || waveformBounds.contains(e.getPosition()))
        {
            isScrubbing = true;
            scrubToMouseX(e.x);
        }
        else if (sceneLaneBounds.contains(e.getPosition()))
        {
            isScrubbing = true;
            selectSceneAtMouseX(e.x);
            scrubToMouseX(e.x);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isScrubbing && e.mods.isLeftButtonDown())
            scrubToMouseX(e.x);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isScrubbing = false;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (!rulerBounds.contains(e.getPosition()) &&
            !waveformBounds.contains(e.getPosition()) &&
            !sceneLaneBounds.contains(e.getPosition()))
        {
            return;
        }

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);

        // Shift + rotella (o scroll orizzontale touchpad): pan orizzontale
        if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
        {
            const double delta = (std::abs(wheel.deltaX) > 0.001f) ? -wheel.deltaX : -wheel.deltaY;
            viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec),
                viewStartSec + delta * visSec * 0.25);
            updateScrollBarRange();
            repaint();
            return;
        }

        if (std::abs(wheel.deltaY) > 0.001f)
        {
            const double factor = (wheel.deltaY > 0.0f) ? 1.25 : (1.0 / 1.25);
            zoomAtPixel(e.x, factor);
        }
    }

private:
    bool hasAudioLoaded() const
    {
        return audioEngine.getTotalLengthSeconds() > 0.1;
    }

    double getCurrentPlayheadSeconds() const
    {
        return hasAudioLoaded() ? audioEngine.getCurrentPositionSeconds() : virtualPositionSec;
    }

    double getVisibleDurationSeconds(double totalSec) const noexcept
    {
        return juce::jmax(2.0, totalSec / juce::jlimit(1.0, 32.0, zoomFactor));
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
        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double oldVis = getVisibleDurationSeconds(totalSec);
        const double anchorNorm = (waveformBounds.getWidth() > 0)
            ? juce::jlimit(0.0, 1.0, static_cast<double>(anchorX - waveformBounds.getX()) / static_cast<double>(waveformBounds.getWidth()))
            : 0.5;
        const double anchorSec = viewStartSec + anchorNorm * oldVis;

        zoomFactor = juce::jlimit(1.0, 32.0, zoomFactor * multiplier);
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
        if (isUpdatingScrollBar || sb != &horizontalScrollBar)
            return;

        const double totalSec = getEffectiveTimelineDurationSeconds();
        const double visSec = getVisibleDurationSeconds(totalSec);
        viewStartSec = juce::jlimit(0.0, juce::jmax(0.0, totalSec - visSec), newRangeStart);
        repaint();
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

        const double totalSec = getEffectiveTimelineDurationSeconds();
        bool isPlayingNow = false;

        if (hasAudioLoaded())
        {
            virtualPlaying = false;
            isPlayingNow = audioEngine.isPlaying();
        }
        else if (virtualPlaying)
        {
            isPlayingNow = true;
            virtualPositionSec += dtSec;
            if (virtualPositionSec >= totalSec)
            {
                virtualPositionSec = 0.0;
                virtualPlaying = false;
            }
        }

        const double currentSec = getCurrentPlayheadSeconds();
        if (isPlayingNow || std::abs(currentSec - lastReportedSec) > 0.001)
        {
            lastReportedSec = currentSec;

            // Auto-scroll se la testina esce dalla finestra zoomata durante il play
            if (isPlayingNow && zoomFactor > 1.01)
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
            node.hasType(StateKeys::MeterPointNode))
        {
            needsBeatMapSync = true;
        }
    }

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override
    {
        markBeatMapDirtyIfRelevant(tree);
        repaint();
    }

    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override
    {
        markBeatMapDirtyIfRelevant(parent);
        markBeatMapDirtyIfRelevant(child);
        repaint();
    }

    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int) override
    {
        markBeatMapDirtyIfRelevant(parent);
        markBeatMapDirtyIfRelevant(child);
        repaint();
    }

    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { repaint(); }
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
                if (safeThis->audioEngine.loadFullMix(file, safeThis->appState))
                {
                    safeThis->virtualPlaying = false;
                    safeThis->zoomFactor = 1.0;
                    safeThis->viewStartSec = 0.0;
                    safeThis->thumbnail.setSource(new juce::FileInputSource(file));
                    safeThis->updateScrollBarRange();
                    safeThis->repaint();
                }
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

    void showMeterContextMenu(int mouseX)
    {
        if (waveformBounds.getWidth() <= 0) return;

        const double clickedSec = pixelXToTime(mouseX);
        const double clickedBeat = beatTimeMap.secondsToBeat(clickedSec);
        const auto pos = beatTimeMap.beatToMusicalPosition(clickedBeat);
        const double barStartQ = juce::jmax(1.0, pos.totalQuarters - pos.beatInBarQ);

        juce::PopupMenu menu;
        menu.addSectionHeader("Cambio Metrica su M" + juce::String(pos.barIndex)
            + " (Q " + juce::String(barStartQ, 2) + ")");

        menu.addItem(1, "Imposta 4/4 da qui");
        menu.addItem(2, "Imposta 3/4 da qui");
        menu.addItem(3, "Imposta 6/8 da qui");
        menu.addItem(4, "Imposta 5/4 da qui");
        menu.addItem(5, "Imposta 7/8 da qui");
        menu.addItem(6, "Imposta 12/8 da qui");
        menu.addItem(7, "Imposta 2/4 da qui");
        menu.addItem(8, "Imposta 9/8 da qui");
        menu.addSeparator();
        menu.addItem(100, "Rimuovi cambio metrica vicino a M" + juce::String(pos.barIndex), barStartQ > 1.05);
        menu.addItem(101, "Ripristina metrica fissa su tutto il brano");

        juce::Component::SafePointer<MiniDawTimeline> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, barStartQ](int result) {
            if (safeThis == nullptr || result == 0) return;

            juce::ScopedValueSetter<bool> guard(safeThis->isUpdatingControls, true);
            int num = 4, den = 4;
            if (meterIdToSignature(result, num, den))
            {
                safeThis->beatTimeMap.addOrUpdateMeterChange(barStartQ, num, den);
                safeThis->syncControlsFromBeatMap();
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            else if (result == 100)
            {
                safeThis->beatTimeMap.removeMeterChangeNear(barStartQ, 1.0);
                safeThis->syncControlsFromBeatMap();
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            else if (result == 101)
            {
                safeThis->beatTimeMap.setFixedMeter(
                    safeThis->beatTimeMap.getBaseTimeSigNum(),
                    safeThis->beatTimeMap.getBaseTimeSigDen());
                safeThis->syncControlsFromBeatMap();
                safeThis->updateScrollBarRange();
                safeThis->repaint();
            }
            });
    }

    double getEffectiveTimelineDurationSeconds() const
    {
        double maxSec = 30.0;

        const double audioLen = audioEngine.getTotalLengthSeconds();
        if (audioLen > 1.0)
            maxSec = juce::jmax(maxSec, audioLen);

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
        return maxSec;
    }

    void scrubToMouseX(int mouseX)
    {
        if (waveformBounds.getWidth() <= 0) return;

        const double targetSec = pixelXToTime(mouseX);
        if (hasAudioLoaded())
            audioEngine.setPositionSeconds(targetSec);
        else
            virtualPositionSec = targetSec;

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

    void drawRulerAndBeatGrid(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        const double visSec = vEndSec - vStartSec;
        if (visSec <= 0.001 || waveformBounds.getWidth() <= 0)
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
            const double subStepQ = 4.0 / static_cast<double>(juce::jmax(1, seg.den));

            const double sampleBeat = juce::jmax(seg.startBeat, viewStartBeat);
            const double secA = beatTimeMap.beatToSeconds(sampleBeat);
            const double secB = beatTimeMap.beatToSeconds(sampleBeat + barLenQ);
            const float pxPerBar = static_cast<float>(((secB - secA) / visSec) * waveW);

            int barStep = 1;
            if (pxPerBar < 2.0f)  barStep = 16;
            else if (pxPerBar < 4.5f)  barStep = 8;
            else if (pxPerBar < 9.0f)  barStep = 4;
            else if (pxPerBar < 16.0f) barStep = 2;

            const bool drawSubdivisions = (pxPerBar >= 40.0f && seg.num > 1 && seg.num <= 16);

            // Se il segmento e' iniziato fuori a sinistra dello schermo, mostra un badge fisso a SX
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
                            g.setColour(juce::Colour(0xffa78bfa).withAlpha(0.8f));
                            g.drawVerticalLine(x,
                                static_cast<float>(rulerBounds.getY()),
                                static_cast<float>(waveformBounds.getBottom()));
                        }
                        else
                        {
                            const bool majorBar = (barNum == 1) || ((barNum - 1) % (barStep * 4) == 0);
                            g.setColour(majorBar ? juce::Colour(0xff34364a) : juce::Colour(0xff232533));
                            g.drawVerticalLine(x,
                                static_cast<float>(waveformBounds.getY()),
                                static_cast<float>(waveformBounds.getBottom()));

                            g.setColour(juce::Colour(0xff475569));
                            g.drawVerticalLine(x,
                                static_cast<float>(rulerBounds.getBottom() - (majorBar ? 8 : 5)),
                                static_cast<float>(rulerBounds.getBottom()));
                        }
                    }

                    // Fascia alta righello: badge metrica e numeri di battuta
                    if (isMeterStart)
                    {
                        const juce::String sigTxt = juce::String(seg.num) + "/" + juce::String(seg.den);
                        const int badgeW = (sigTxt.length() >= 4) ? 32 : 26;
                        const int badgeX = juce::jlimit(leftX + 1, juce::jmax(leftX + 1, rightX - badgeW - 1), x + 2);
                        juce::Rectangle<int> badgeRect(badgeX, rulerBounds.getY() + 2, badgeW, 13);

                        g.setColour(s == 0 ? juce::Colour(0xff312e81) : juce::Colour(0xff6d28d9));
                        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
                        g.setColour(juce::Colour(0xffc4b5fd));
                        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 1.0f);
                        g.setColour(juce::Colours::white);
                        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                        g.drawText(sigTxt, badgeRect, juce::Justification::centred, false);

                        lastLabelRightX = juce::jmax(lastLabelRightX, badgeRect.getRight() + 4);
                    }
                    else if (shouldDrawLine && x > lastLabelRightX + 4 && x < rightX - 24)
                    {
                        const juce::String barTxt = "M" + juce::String(barNum);
                        g.setColour(juce::Colour(0xff94a3b8));
                        g.setFont(juce::FontOptions(9.5f));
                        g.drawText(barTxt, x + 3, rulerBounds.getY() + 1, 34, 13, juce::Justification::centredLeft, false);
                        lastLabelRightX = x + 28;
                    }
                }

                if (drawSubdivisions)
                {
                    g.setColour(juce::Colour(0xff1d1f2b));
                    for (int sub = 1; sub < seg.num; ++sub)
                    {
                        const double subQ = barQ + static_cast<double>(sub) * subStepQ;
                        if (subQ >= segEndQ - 1e-4)
                            break;
                        const int sx = beatToX(subQ);
                        if (sx > leftX && sx < rightX)
                            g.drawVerticalLine(sx,
                                static_cast<float>(waveformBounds.getY()),
                                static_cast<float>(waveformBounds.getBottom()));
                    }
                }
            }
        }

        // Fascia bassa righello: pillole ambra dei cambi BPM dalla Tempo Map
        const auto& tempoAnchors = beatTimeMap.getBeatAnchors();
        if (!tempoAnchors.empty())
        {
            double prevBpm = -1.0;
            int lastBpmRightX = leftX - 100;

            for (size_t i = 0; i < tempoAnchors.size(); ++i)
            {
                const auto& a = tempoAnchors[i];
                if (prevBpm > 0.0 && std::abs(a.localBpm - prevBpm) < 0.15)
                    continue;
                prevBpm = a.localBpm;

                if (a.timeSec < vStartSec || a.timeSec > vEndSec)
                    continue;

                const int x = timeToPixelX(a.timeSec, vStartSec, visSec);
                if (x < leftX || x > rightX) continue;

                if (i > 0)
                {
                    g.setColour(juce::Colour(0xfff59e0b).withAlpha(0.70f));
                    g.drawVerticalLine(x,
                        static_cast<float>(rulerBounds.getY() + 14),
                        static_cast<float>(waveformBounds.getBottom()));
                }

                if (x > lastBpmRightX + 3 && x < rightX - 30)
                {
                    juce::String bpmTxt = juce::String(juce::roundToInt(a.localBpm));
                    juce::Rectangle<int> tag(x + 2, rulerBounds.getBottom() - 12, 26, 11);
                    g.setColour(juce::Colour(0xff78350f));
                    g.fillRoundedRectangle(tag.toFloat(), 2.0f);
                    g.setColour(juce::Colour(0xfffef08a));
                    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
                    g.drawText(bpmTxt, tag, juce::Justification::centred, false);
                    lastBpmRightX = tag.getRight() + 3;
                }
            }
        }
    }

    // Disegna la curva a gradini della Tempo Map MIDI dentro l'area waveform
    void drawTempoMapOverlay(juce::Graphics& g, double vStartSec, double vEndSec, bool noAudioLoaded)
    {
        const double visSec = vEndSec - vStartSec;
        if (visSec <= 0.001 || waveformBounds.getWidth() <= 0 || waveformBounds.getHeight() < 16)
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

        const float topY = static_cast<float>(waveformBounds.getY() + 18);
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
        }

        g.setColour(juce::Colour(0xfff59e0b).withAlpha(noAudioLoaded ? 0.85f : 0.45f));
        g.strokePath(linePath, juce::PathStrokeType(noAudioLoaded ? 1.8f : 1.2f));

        // Badge informativo compatto in alto a sinistra nell'area waveform
        juce::String badge = "MIDI Map: "
            + juce::String(static_cast<int>(anchors.size())) + " BPM, "
            + juce::String(static_cast<int>(beatTimeMap.getMeterAnchors().size())) + " Metri";
        if (noAudioLoaded)
            badge += " (senza audio - rotella = zoom, drag = scrub)";

        g.setColour(juce::Colour(0xff0b0c12).withAlpha(0.75f));
        auto pill = juce::Rectangle<int>(waveformBounds.getX() + 6, waveformBounds.getY() + 3,
            juce::jmin(waveformBounds.getWidth() - 12, noAudioLoaded ? 390 : 210), 15);
        g.fillRoundedRectangle(pill.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xfffbbf24));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(badge, pill.reduced(5, 0), juce::Justification::centredLeft, true);
    }

    void drawSceneLane(juce::Graphics& g, double vStartSec, double vEndSec)
    {
        g.setColour(juce::Colour(0xff16161d));
        g.fillRect(sceneLaneBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(sceneLaneBounds, 1);

        const double visSec = vEndSec - vStartSec;
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || visSec <= 0.001 || sceneLaneBounds.getWidth() <= 0)
            return;

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            double durB = static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0));

            double startSec = beatTimeMap.beatToSeconds(startB);
            double endSec = beatTimeMap.beatToSeconds(startB + durB);
            if (endSec < vStartSec || startSec > vEndSec)
                continue;

            int x1 = timeToPixelX(startSec, vStartSec, visSec);
            int x2 = timeToPixelX(endSec, vStartSec, visSec);
            int w = juce::jmax(6, x2 - x1);

            juce::Rectangle<int> blockRect(x1, sceneLaneBounds.getY() + 3, w, sceneLaneBounds.getHeight() - 6);
            blockRect = blockRect.getIntersection(sceneLaneBounds.reduced(1));

            if (!blockRect.isEmpty())
            {
                bool isSel = (i == selectedSceneIndex);
                g.setColour(isSel ? juce::Colour(0xff3b82f6) : juce::Colour(0xff2e3846));
                g.fillRoundedRectangle(blockRect.toFloat(), 3.0f);

                g.setColour(isSel ? juce::Colours::white : juce::Colour(0xff64748b));
                g.drawRoundedRectangle(blockRect.toFloat(), 3.0f, 1.0f);

                juce::String name = scene.getProperty(StateKeys::SceneName, "Scena").toString();
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(11.0f));
                g.drawText(name, blockRect.reduced(4, 0), juce::Justification::centredLeft, true);
            }
        }
    }

    juce::ValueTree& appState;
    StemAudioEngine& audioEngine;
    BeatTimeMap& beatTimeMap;

    juce::AudioThumbnailCache thumbnailCache{ 5 };
    juce::AudioThumbnail thumbnail;

    juce::TextButton playPauseBtn;
    juce::TextButton stopBtn;
    juce::TextButton loadMixBtn;
    juce::TextButton importMidiMapBtn;
    juce::Slider bpmSlider;
    juce::ComboBox meterCombo;
    juce::TextButton zoomOutBtn, zoomInBtn, zoomFitBtn;
    juce::ScrollBar horizontalScrollBar{ false };

    juce::Rectangle<int> topInfoBounds;
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> waveformBounds;
    juce::Rectangle<int> sceneLaneBounds;
    std::unique_ptr<juce::FileChooser> fileChooser;

    double lastReportedSec = -1.0;
    double lastTimerMs = 0.0;
    double virtualPositionSec = 0.0;
    bool virtualPlaying = false;

    double zoomFactor = 1.0;
    double viewStartSec = 0.0;
    bool isUpdatingScrollBar = false;

    int selectedSceneIndex = -1;
    bool isScrubbing = false;
    bool isUpdatingControls = false;
    bool needsBeatMapSync = false;
};