#pragma once
#include <JuceHeader.h>
#include "../../Audio/StemAudioEngine.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/DataStateKeys.h"


class MiniDawTimeline : public juce::Component,
    private juce::Timer,
    private juce::ChangeListener,
    private juce::ValueTree::Listener
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

        // Permette alla timeline di ricevere la Barra Spaziatrice subito dopo uno scrubbing col mouse
        setWantsKeyboardFocus(true);

        audioEngine.startBackgroundThread();
        thumbnail.addChangeListener(this);
        appState.addListener(this);

        // Pulsante Play / Pausa
        playPauseBtn.setButtonText("Play / Pausa");
        playPauseBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
        playPauseBtn.onClick = [this]() { audioEngine.togglePlayPause(); };
        addAndMakeVisible(playPauseBtn);

        // Pulsante Stop
        stopBtn.setButtonText("Stop");
        stopBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a45));
        stopBtn.onClick = [this]() {
            audioEngine.stopAndRewind();
            repaint();
            };
        addAndMakeVisible(stopBtn);

        // Pulsante Carica Full Mix
        loadMixBtn.setButtonText("Carica Full Mix...");
        loadMixBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4a3e));
        loadMixBtn.onClick = [this]() { promptLoadFullMix(); };
        addAndMakeVisible(loadMixBtn);

        // Slider BPM Base
        bpmSlider.setSliderStyle(juce::Slider::LinearBar);
        bpmSlider.setRange(40.0, 260.0, 0.5);
        bpmSlider.setValue(beatTimeMap.getBaseBpm(), juce::dontSendNotification);
        bpmSlider.setTextValueSuffix(" BPM");
        bpmSlider.onValueChange = [this]() {
            beatTimeMap.setFixedBpm(bpmSlider.getValue(), 0.0);
            repaint();
            };
        addAndMakeVisible(bpmSlider);

        syncWithAppState();
        startTimerHz(60); // Aggiornamento Playhead a 60 FPS
    }

    ~MiniDawTimeline() override
    {
        stopTimer();
        thumbnail.removeChangeListener(this);
        appState.removeListener(this);
    }

    // Ricarica il Full Mix e il BPM se lo stato dell'app viene caricato da disco
    void syncWithAppState()
    {
        beatTimeMap.rebuildCacheFromState();
        bpmSlider.setValue(beatTimeMap.getBaseBpm(), juce::dontSendNotification);

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
                        thumbnail.setSource(new juce::FileInputSource(f));
                }
            }
        }
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
        auto topBar = bounds.removeFromTop(30);

        playPauseBtn.setBounds(topBar.removeFromLeft(105));
        topBar.removeFromLeft(6);
        stopBtn.setBounds(topBar.removeFromLeft(65));
        topBar.removeFromLeft(6);
        loadMixBtn.setBounds(topBar.removeFromLeft(135));
        topBar.removeFromLeft(10);
        bpmSlider.setBounds(topBar.removeFromLeft(110));

        bounds.removeFromTop(6);
        sceneLaneBounds = bounds.removeFromBottom(32);
        bounds.removeFromBottom(2);
        waveformBounds = bounds;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121216));

        double currentSec = audioEngine.getCurrentPositionSeconds();
        double currentBeat = beatTimeMap.secondsToBeat(currentSec);
        double totalSec = getEffectiveTimelineDurationSeconds();

        // 1. Display Timecode + Quarto attuale in alto a destra
        auto topInfoRect = juce::Rectangle<int>(getWidth() - 320, 6, 310, 30);
        g.setColour(juce::Colour(0xff1e1e26));
        g.fillRoundedRectangle(topInfoRect.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff60a5fa));
        g.setFont(juce::FontOptions(14.5f, juce::Font::bold));

        juce::String statusText = BeatTimeMap::formatTimecode(currentSec)
            + "   |   "
            + BeatTimeMap::formatBeat(currentBeat);
        g.drawText(statusText, topInfoRect.reduced(8, 0), juce::Justification::centredRight, false);

        // 2. Sfondo Forma d'Onda
        g.setColour(juce::Colour(0xff18181f));
        g.fillRect(waveformBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(waveformBounds, 1);

        // 3. Disegno Griglia delle Battute (ogni 4 quarti)
        drawBeatGrid(g, totalSec);

        // 4. Disegno Forma d'Onda del Full Mix
        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour(juce::Colour(0xff3b82f6).withAlpha(0.75f));
            thumbnail.drawChannels(g, waveformBounds.reduced(1), 0.0, totalSec, 1.0f);
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(13.0f));
            g.drawText("Nessun Full Mix caricato - Clicca 'Carica Full Mix...' per visualizzare la traccia e fare scrubbing",
                waveformBounds, juce::Justification::centred, false);
        }

        // 5. Disegno Corsia Scene (SHOW_TIMELINE) sotto la forma d'onda
        drawSceneLane(g, totalSec);

        // 6. Disegno Testina di Lettura (Playhead verticale rossa/ambra)
        if (totalSec > 0.0)
        {
            float normX = static_cast<float>(currentSec / totalSec);
            int playheadX = waveformBounds.getX() + juce::roundToInt(normX * waveformBounds.getWidth());

            if (playheadX >= waveformBounds.getX() && playheadX <= waveformBounds.getRight())
            {
                g.setColour(juce::Colour(0xfff59e0b));
                g.drawVerticalLine(playheadX, static_cast<float>(waveformBounds.getY()), static_cast<float>(sceneLaneBounds.getBottom()));

                juce::Path headTriangle;
                headTriangle.addTriangle(static_cast<float>(playheadX - 5), static_cast<float>(waveformBounds.getY()),
                    static_cast<float>(playheadX + 5), static_cast<float>(waveformBounds.getY()),
                    static_cast<float>(playheadX), static_cast<float>(waveformBounds.getY() + 8));
                g.fillPath(headTriangle);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (waveformBounds.contains(e.getPosition()))
        {
            scrubToMouseX(e.x);
        }
        else if (sceneLaneBounds.contains(e.getPosition()))
        {
            selectSceneAtMouseX(e.x);
            scrubToMouseX(e.x);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && (waveformBounds.contains(e.getMouseDownPosition()) || sceneLaneBounds.contains(e.getMouseDownPosition())))
        {
            scrubToMouseX(e.x);
        }
    }

private:
    void timerCallback() override
    {
        double currentSec = audioEngine.getCurrentPositionSeconds();
        if (audioEngine.isPlaying() || std::abs(currentSec - lastReportedSec) > 0.001)
        {
            lastReportedSec = currentSec;
            if (onBeatPositionChanged)
                onBeatPositionChanged(beatTimeMap.secondsToBeat(currentSec));
            repaint();
        }
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        repaint();
    }

    // Aggiorna la corsia delle scene se vengono aggiunte, modificate o eliminate scene
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { repaint(); }
    void valueTreeParentChanged(juce::ValueTree&) override {}

    void promptLoadFullMix()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Seleziona il file Audio Full Mix",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                if (audioEngine.loadFullMix(file, appState))
                {
                    thumbnail.setSource(new juce::FileInputSource(file));
                    repaint();
                }
            }
            });
    }

    // Se non c'e' un file audio caricato, mostra una finestra di default basata sulle scene (minimo 60s)
    double getEffectiveTimelineDurationSeconds() const
    {
        double audioLen = audioEngine.getTotalLengthSeconds();
        if (audioLen > 1.0)
            return audioLen;

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
        return juce::jmax(30.0, beatTimeMap.beatToSeconds(maxBeat));
    }

    void scrubToMouseX(int mouseX)
    {
        if (waveformBounds.getWidth() <= 0) return;

        double totalSec = getEffectiveTimelineDurationSeconds();
        double norm = juce::jlimit(0.0, 1.0, static_cast<double>(mouseX - waveformBounds.getX()) / waveformBounds.getWidth());
        double targetSec = norm * totalSec;

        audioEngine.setPositionSeconds(targetSec);
        if (onBeatPositionChanged)
            onBeatPositionChanged(beatTimeMap.secondsToBeat(targetSec));
        repaint();
    }

    void selectSceneAtMouseX(int mouseX)
    {
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || sceneLaneBounds.getWidth() <= 0) return;

        double totalSec = getEffectiveTimelineDurationSeconds();
        double norm = juce::jlimit(0.0, 1.0, static_cast<double>(mouseX - sceneLaneBounds.getX()) / sceneLaneBounds.getWidth());
        double clickedBeat = beatTimeMap.secondsToBeat(norm * totalSec);

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

    void drawBeatGrid(juce::Graphics& g, double totalSec)
    {
        if (totalSec <= 0.0 || waveformBounds.getWidth() <= 0) return;

        double totalBeats = beatTimeMap.secondsToBeat(totalSec);
        int stepBeats = (totalBeats > 160.0) ? 16 : 4; // Griglia ogni 1 o 4 battute in base allo zoom

        g.setColour(juce::Colour(0xff252530));
        for (double b = 1.0; b <= totalBeats; b += stepBeats)
        {
            double sec = beatTimeMap.beatToSeconds(b);
            int x = waveformBounds.getX() + juce::roundToInt((sec / totalSec) * waveformBounds.getWidth());
            if (x > waveformBounds.getX() && x < waveformBounds.getRight())
                g.drawVerticalLine(x, static_cast<float>(waveformBounds.getY()), static_cast<float>(waveformBounds.getBottom()));
        }
    }

    void drawSceneLane(juce::Graphics& g, double totalSec)
    {
        g.setColour(juce::Colour(0xff16161d));
        g.fillRect(sceneLaneBounds);
        g.setColour(juce::Colour(0xff2a2a35));
        g.drawRect(sceneLaneBounds, 1);

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid() || totalSec <= 0.0) return;

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            double durB = static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0));

            double startSec = beatTimeMap.beatToSeconds(startB);
            double endSec = beatTimeMap.beatToSeconds(startB + durB);

            int x1 = sceneLaneBounds.getX() + juce::roundToInt((startSec / totalSec) * sceneLaneBounds.getWidth());
            int x2 = sceneLaneBounds.getX() + juce::roundToInt((endSec / totalSec) * sceneLaneBounds.getWidth());
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
                g.setFont(juce::FontOptions(11.5f));
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
    juce::Slider bpmSlider;

    juce::Rectangle<int> waveformBounds;
    juce::Rectangle<int> sceneLaneBounds;
    std::unique_ptr<juce::FileChooser> fileChooser;

    double lastReportedSec = -1.0;
    int selectedSceneIndex = -1;
};