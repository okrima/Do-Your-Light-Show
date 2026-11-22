#pragma once
#include <JuceHeader.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/StageRoleCatalog.h"
#include "../../Audio/StemAudioEngine.h"
#include "../../Model/CurveEvaluator.h"
#include "../../Engine/DmxLiveEngine.h"
#include "StageArtworkHelper.h"
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

class StageSimulatorView : public juce::Component,
    private juce::Timer
{
public:
    std::function<void()> onToggleExpandRequested;
    std::function<void(bool)> onPreviewPlaybackChanged;

    void setDmxLiveEngine(DmxLiveEngine* engine) noexcept { dmxLiveEngine = engine; }

    void stopFixtureTestIfActive()
    {
        if (dmxLiveEngine != nullptr && dmxLiveEngine->isFixtureTestActive())
            dmxLiveEngine->stopFixtureTest();
    }

    // 1. Costruttore Timeline (usato in ShowBuilderTab)
    StageSimulatorView(juce::ValueTree& state,
        StemAudioEngine& engine,
        BeatTimeMap& beatMap)
        : appState(state),
        audioEngine(&engine),
        beatTimeMap(&beatMap),
        standalonePreviewMode(false)
    {
        initCommon();
    }

    // 2. Costruttore Standalone Preview (usato in SceneBuilderTab con metronomo BPM interno)
    explicit StageSimulatorView(juce::ValueTree& state)
        : appState(state),
        audioEngine(nullptr),
        beatTimeMap(nullptr),
        standalonePreviewMode(true),
        allowStageDrag(true)
    {
        initCommon();
        expandBtn.setVisible(false);
    }

    ~StageSimulatorView() override
    {
        stopTimer();
    }

    bool getIsExpanded() const noexcept { return isExpanded; }

    // --- API Modalita' Standalone Preview (Scene Builder) ---
    void setPreviewPreset(juce::ValueTree presetNode)
    {
        previewPresetNode = presetNode;
        previewBeat = 1.0;
        currentTimeSec = 0.0;
        oneShotFinished = false;
        currentFogDensity = 0.08f; // Ripartenza pulita della nebbia sul quarto 1.00
        previewPlaying = presetNode.isValid();
        lastTickMs = juce::Time::getMillisecondCounterHiRes();

        if (onPreviewPlaybackChanged)
            onPreviewPlaybackChanged(previewPlaying);

        repaint();
    }

    void setPreviewBpm(double bpm)
    {
        previewBpm = juce::jlimit(40.0, 260.0, bpm);
    }

    void setPreviewTimeSignature(int num, int den)
    {
        previewTimeSigNum = juce::jlimit(1, 32, num);
        previewTimeSigDen = BeatTimeMap::sanitizeDenominator(den);
        previewBeat = 1.0;
        currentTimeSec = 0.0;
        oneShotFinished = false;
        repaint();
    }

    double getPreviewBarLengthQ() const noexcept
    {
        return static_cast<double>(previewTimeSigNum) * (4.0 / static_cast<double>(previewTimeSigDen));
    }

    void setPreviewPlaying(bool shouldPlay)
    {
        previewPlaying = shouldPlay;
        if (previewPlaying && oneShotFinished)
        {
            previewBeat = 1.0;
            currentTimeSec = 0.0;
            oneShotFinished = false;
            currentFogDensity = 0.08f;
        }
        lastTickMs = juce::Time::getMillisecondCounterHiRes();

        if (onPreviewPlaybackChanged)
            onPreviewPlaybackChanged(previewPlaying);
    }

    bool getPreviewPlaying() const noexcept { return previewPlaying; }

    void triggerOneShotPreview()
    {
        previewBeat = 1.0;
        currentTimeSec = 0.0;
        oneShotFinished = false;
        previewPlaying = true;
        currentFogDensity = 0.08f;
        lastTickMs = juce::Time::getMillisecondCounterHiRes();

        if (onPreviewPlaybackChanged)
            onPreviewPlaybackChanged(true);

        repaint();
    }

    void notifyPresetEdited()
    {
        if (!standalonePreviewMode)
            return;

        if (oneShotFinished)
        {
            previewBeat = 1.0;
            oneShotFinished = false;
            previewPlaying = true;
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
        }
        repaint();
    }

    void resized() override
    {
        auto topBar = getLocalBounds().removeFromTop(28).reduced(8, 3);
        expandBtn.setBounds(topBar.removeFromRight(175));
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff09090d));

        drawHeaderHud(g, bounds.withHeight(26.0f));

        auto stageRect = getStageRect();
        drawStageBackdrop(g, stageRect);

        const float trussY = stageRect.getY() + stageRect.getHeight() * 0.14f;
        const float floorY = stageRect.getY() + stageRect.getHeight() * 0.84f;

        drawTrussAndFloorStructure(g, stageRect, trussY, floorY);
        drawDragZoneGuide(g, stageRect);

        std::vector<VisualFixture> fixtures = buildVisualFixtures(stageRect, trussY, floorY);



        // 1. Controluce Aria e Terra (dietro la band)
        for (const auto& vf : fixtures)
        {
            if (vf.category == StageRoleCatalog::Category::AirWashBack ||
                vf.category == StageRoleCatalog::Category::FloorBackSilhouette)
            {
                drawFixtureLightEffect(g, vf, stageRect, trussY, floorY);
            }
        }

        // 2. Sagome Band
        drawBandSilhouettes(g, stageRect, floorY, fixtures);

        // 3. Fasci frontali, Teste Mobili (con Pan + Tilt), Tagli, Geyser e Blinder
        for (const auto& vf : fixtures)
        {
            if (vf.category != StageRoleCatalog::Category::AirWashBack &&
                vf.category != StageRoleCatalog::Category::FloorBackSilhouette)
            {
                drawFixtureLightEffect(g, vf, stageRect, trussY, floorY);
            }
        }

        if (currentFogDensity > 0.01f)
            drawFogOverlay(g, stageRect, floorY);

        int targetedCount = 0;
        int blockedStrobeCount = 0;
        int blockedMoveCount = 0;

        for (const auto& vf : fixtures)
        {
            bool isHovered = allowStageDrag && vf.unitNode.isValid() && (vf.unitNode == hoveredUnitNode);
            bool isDragged = allowStageDrag && vf.unitNode.isValid() && (vf.unitNode == draggedUnitNode);
            drawFixtureHardwareBody(g, vf, isHovered, isDragged, trussY);

            if (vf.state.hasAnyCommand)            ++targetedCount;
            if (vf.state.blockedByStrobePolicy)    ++blockedStrobeCount;
            if (vf.state.blockedByMovePolicy)      ++blockedMoveCount;
        }

        const int blockedCount = juce::jmax(blockedStrobeCount, blockedMoveCount);
        if (blockedCount > 0)
        {
            const bool isMoveBanner = (blockedMoveCount >= blockedStrobeCount);
            const bool allBlocked = (targetedCount > 0 && blockedCount == targetedCount);
            const float boxW = juce::jmin(490.0f, stageRect.getWidth() - 24.0f);
            const float boxH = 30.0f;

            juce::Rectangle<float> bannerRect = allBlocked
                ? stageRect.withSizeKeepingCentre(boxW, boxH)
                : juce::Rectangle<float>(stageRect.getCentreX() - boxW * 0.5f,
                    stageRect.getBottom() - boxH - 8.0f,
                    boxW,
                    boxH);

            juce::String msg;
            if (isMoveBanner)
            {
                msg = allBlocked
                    ? "Pan/Tilt non mappati (Ch = 0 in Tab 1) su tutti i fari del preset"
                    : ("Movimento ignorato su " + juce::String(blockedMoveCount) + "/" + juce::String(targetedCount)
                        + " fari (Pan/Tilt Ch = 0 in Tab 1)");
            }
            else
            {
                msg = allBlocked
                    ? "Strobo disabilitato in Tab 1 su tutti i fari del preset"
                    : ("Strobo ignorato su " + juce::String(blockedStrobeCount) + "/" + juce::String(targetedCount)
                        + " fari (disabilitato in Tab 1)");
            }

            g.setColour(juce::Colour(0xff14110a).withAlpha(0.88f));
            g.fillRoundedRectangle(bannerRect, 6.0f);
            g.setColour(juce::Colour(0xfff59e0b).withAlpha(0.22f));
            g.fillRoundedRectangle(bannerRect, 6.0f);
            g.setColour(juce::Colour(0xfff59e0b));
            g.drawRoundedRectangle(bannerRect, 6.0f, 1.3f);
            g.setColour(juce::Colour(0xfffbbf24));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(msg, bannerRect.reduced(8.0f, 0.0f), juce::Justification::centred, true);
        }

        g.setColour(juce::Colour(0xff2a2a38));
        g.drawRoundedRectangle(stageRect, 6.0f, 1.5f);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (!allowStageDrag)
            return;

        StageRoleCatalog::Category dummyCat;
        juce::String dummyName;
        auto hit = findPlacedUnitAt(e.position, dummyCat, dummyName);
        if (hit != hoveredUnitNode)
        {
            hoveredUnitNode = hit;
            setMouseCursor(hoveredUnitNode.isValid() ? juce::MouseCursor::PointingHandCursor
                : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (!allowStageDrag || draggedUnitNode.isValid())
            return;

        if (hoveredUnitNode.isValid())
        {
            hoveredUnitNode = juce::ValueTree();
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!allowStageDrag || !e.mods.isLeftButtonDown())
            return;

        StageRoleCatalog::Category hitCat = StageRoleCatalog::Category::AirWashBack;
        juce::String hitName;
        auto hit = findPlacedUnitAt(e.position, hitCat, hitName);
        if (hit.isValid())
        {
            draggedUnitNode = hit;
            draggedCategory = hitCat;
            draggedUnitLabel = hitName;
            hoveredUnitNode = hit;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!allowStageDrag || !draggedUnitNode.isValid())
            return;

        auto stageRect = getStageRect();
        if (stageRect.getWidth() <= 1.0f || stageRect.getHeight() <= 1.0f)
            return;

        double rawNx = static_cast<double>((e.position.x - stageRect.getX()) / stageRect.getWidth());
        double rawNy = static_cast<double>((e.position.y - stageRect.getY()) / stageRect.getHeight());

        auto clamped = clampNormPosToCategoryZone(rawNx, rawNy, draggedCategory);

        // Aggiorna il ValueTree condiviso: si sincronizzano istantaneamente Tab 1, Tab 2 e Salva Venue
        draggedUnitNode.setProperty(StateKeys::PosX, clamped.x, nullptr);
        draggedUnitNode.setProperty(StateKeys::PosY, clamped.y, nullptr);
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!draggedUnitNode.isValid())
            return;

        draggedUnitNode = juce::ValueTree();
        draggedUnitLabel.clear();

        StageRoleCatalog::Category dummyCat;
        juce::String dummyName;
        hoveredUnitNode = findPlacedUnitAt(e.position, dummyCat, dummyName);
        setMouseCursor(hoveredUnitNode.isValid() ? juce::MouseCursor::PointingHandCursor
            : juce::MouseCursor::NormalCursor);
        repaint();
    }

private:

    int previewTimeSigNum = 4;
    int previewTimeSigDen = 4;
    BeatTimeMap::MusicalPosition currentMusicalPos;
    DmxLiveEngine* dmxLiveEngine = nullptr;
    bool wasTab3PlayingDmx = false;

    juce::Rectangle<float> getStageRect() const
    {
        auto stageRect = getLocalBounds().toFloat().reduced(8.0f, 6.0f);
        stageRect.removeFromTop(24.0f);
        return stageRect;
    }

    static bool isAirCategory(StageRoleCatalog::Category cat)
    {
        using Cat = StageRoleCatalog::Category;
        return cat == Cat::AirWashBack || cat == Cat::AirParFront ||
            cat == Cat::AirMovingBeam || cat == Cat::ImpactBlinder ||
            cat == Cat::ImpactStrobe;
    }

    static juce::Point<double> clampNormPosToCategoryZone(double nx, double ny, StageRoleCatalog::Category cat)
    {
        if (cat == StageRoleCatalog::Category::FloorSideWash)
        {
            double clampedX = (nx < 0.5) ? juce::jlimit(0.035, 0.105, nx)
                : juce::jlimit(0.895, 0.965, nx);
            double clampedY = juce::jlimit(0.41, 0.94, ny);
            return { clampedX, clampedY };
        }

        if (isAirCategory(cat))
            return { juce::jlimit(0.03, 0.97, nx), juce::jlimit(0.06, 0.35, ny) };

        return { juce::jlimit(0.14, 0.86, nx), juce::jlimit(0.64, 0.94, ny) };
    }

    juce::ValueTree findPlacedUnitAt(juce::Point<float> pt,
        StageRoleCatalog::Category& outCat,
        juce::String& outName) const
    {
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        if (!venueNode.isValid())
            return {};

        auto stageRect = getStageRect();
        juce::ValueTree bestUnit;
        float bestDistSq = 18.0f * 18.0f; // Raggio di presa comodo (18 px)

        for (int f = 0; f < venueNode.getNumChildren(); ++f)
        {
            auto fix = venueNode.getChild(f);
            if (!fix.hasType(StateKeys::Fixture)) continue;

            juce::String fName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
            juce::String rName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
            if (rName.isEmpty()) rName = fName;
            auto cat = StageRoleCatalog::classifyRole(rName, fName);

            int unitCount = 0;
            for (int u = 0; u < fix.getNumChildren(); ++u)
            {
                auto unit = fix.getChild(u);
                if (!unit.hasType(StateKeys::FixtureUnit)) continue;
                ++unitCount;

                if (!static_cast<bool>(unit.getProperty(StateKeys::IsPlaced, false)))
                    continue;

                float ux = stageRect.getX() + static_cast<float>(static_cast<double>(unit.getProperty(StateKeys::PosX, 0.5))) * stageRect.getWidth();
                float uy = stageRect.getY() + static_cast<float>(static_cast<double>(unit.getProperty(StateKeys::PosY, 0.14))) * stageRect.getHeight();

                float dx = pt.x - ux;
                float dy = pt.y - uy;
                float dSq = dx * dx + dy * dy;
                if (dSq <= bestDistSq)
                {
                    bestDistSq = dSq;
                    bestUnit = unit;
                    outCat = cat;
                    outName = fName + " #" + juce::String(unitCount);
                }
            }
        }
        return bestUnit;
    }

    void drawDragZoneGuide(juce::Graphics& g, const juce::Rectangle<float>& stageRect) const
    {
        if (!draggedUnitNode.isValid())
            return;

        auto drawZoneBox = [&](const juce::Rectangle<float>& zr, juce::Colour col, const juce::String& title)
            {
                g.setColour(col.withAlpha(0.11f));
                g.fillRoundedRectangle(zr, 5.0f);
                g.setColour(col.withAlpha(0.75f));
                g.drawRoundedRectangle(zr, 5.0f, 1.5f);
                g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
                g.drawText(title, zr.reduced(6.0f, 4.0f), juce::Justification::topLeft, true);
            };

        if (draggedCategory == StageRoleCatalog::Category::FloorSideWash)
        {
            drawZoneBox(StageArtworkHelper::getLeftWingRect(stageRect), juce::Colour(0xffa855f7), "QUINTA SX");
            drawZoneBox(StageArtworkHelper::getRightWingRect(stageRect), juce::Colour(0xffa855f7), "QUINTA DX");
        }
        else if (isAirCategory(draggedCategory))
        {
            drawZoneBox(StageArtworkHelper::getAirZoneRect(stageRect), juce::Colour(0xff38bdf8), "ZONA ARIA: " + draggedUnitLabel);
        }
        else
        {
            drawZoneBox(StageArtworkHelper::getFloorZoneRect(stageRect), juce::Colour(0xfff59e0b), "ZONA TERRA: " + draggedUnitLabel);
        }
    }
    

    void initCommon()
    {
        expandBtn.setButtonText("Ingrandisci Palco");
        expandBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272732));
        expandBtn.onClick = [this]() {
            isExpanded = !isExpanded;
            expandBtn.setButtonText(isExpanded ? "Dividi Vista (Palco + Editor)" : "Ingrandisci Palco");
            if (onToggleExpandRequested)
                onToggleExpandRequested();
            };
        addAndMakeVisible(expandBtn);

        lastTickMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }

    struct RoleRuntimeState
    {
        bool hasAnyCommand = false;
        bool hasDimmerCmd = false;
        bool blockedByStrobePolicy = false;
        bool blockedByMovePolicy = false;
        float dimmer = 0.0f;
        float red = -1.0f;
        float green = -1.0f;
        float blue = -1.0f;
        float white = -1.0f;
        float uv = -1.0f;
        float strobe = 0.0f;
        float strobeTargetHz = 0.0f; // > 0 quando lo strobo hardware e' sincronizzato al BPM
        bool hasPanCmd = false;
        bool hasTiltCmd = false;
        float panCmd = 0.5f;
        float tiltCmd = 0.5f;
        bool panIsStatic = false;
        bool tiltIsStatic = false;
        float motionPanFan = 0.0f;
        float motionTiltFan = 0.0f;
        juce::Colour fallbackColor{ 0xffffffff };
        juce::String motionPhaseMode{ "SymmetricalFan" };

        juce::Colour getEffectiveColour() const
        {
            if (red >= 0.0f || green >= 0.0f || blue >= 0.0f || white >= 0.0f || uv >= 0.0f)
            {
                float rF = (red >= 0.0f ? red : 0.0f) + (white >= 0.0f ? white : 0.0f) + (uv >= 0.0f ? uv * 0.45f : 0.0f);
                float gF = (green >= 0.0f ? green : 0.0f) + (white >= 0.0f ? white : 0.0f);
                float bF = (blue >= 0.0f ? blue : 0.0f) + (white >= 0.0f ? white : 0.0f) + (uv >= 0.0f ? uv : 0.0f);

                juce::uint8 r = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(rF * 255.0f)));
                juce::uint8 g = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(gF * 255.0f)));
                juce::uint8 b = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>(bF * 255.0f)));
                if (r > 5 || g > 5 || b > 5)
                    return juce::Colour(r, g, b);
            }
            return fallbackColor;
        }

        bool hasExplicitColor() const noexcept
        {
            return red >= 0.0f || green >= 0.0f || blue >= 0.0f || white >= 0.0f || uv >= 0.0f;
        }
    };

    struct VisualFixture
    {
        juce::String name;
        juce::String roleName;
        StageRoleCatalog::Category category;
        juce::Point<float> pos;
        float normX = 0.5f;
        int dmxStartAddress = 1;
        int unitIndex = 0;
        int roleSpatialIndex = 0;
        int roleSpatialTotal = 1;

        StageRoleCatalog::PanTiltHardwareSpec panTiltSpec;

        StageRoleCatalog::StrobeHardwareSpec strobeSpec;
        RoleRuntimeState state;

        juce::ValueTree unitNode;
    };

    struct ActiveSceneEntry
    {
        juce::ValueTree node;
        double startBeat = 1.0;
        double durationBeats = 4.0;
        double progress = 0.0;
        int orderIndex = 0;
        int layerPriority = 0; // 0 = Base, 1 = Movement, 2 = OverlayFX
    };

    static int getLayerPriority(const juce::ValueTree& node)
    {
        const juce::String layer = node.getProperty(StateKeys::LayerType, "Base").toString();
        if (layer.equalsIgnoreCase("OverlayFX")) return 2;
        if (layer.equalsIgnoreCase("Movement"))  return 1;
        return 0;
    }

    bool isSimulationRunning() const
    {
        if (standalonePreviewMode)
            return previewPlaying && !oneShotFinished && !activeScenes.empty();

        return audioEngine != nullptr && audioEngine->isPlaying();
    }

    void timerCallback() override
    {
        double nowMs = juce::Time::getMillisecondCounterHiRes();
        double dtSec = juce::jlimit(0.0, 0.1, (nowMs - lastTickMs) * 0.001);
        lastTickMs = nowMs;

        if (standalonePreviewMode)
        {
            updateStandalonePreviewClock(dtSec);
        }
        else if (audioEngine != nullptr && beatTimeMap != nullptr)
        {
            currentTimeSec = audioEngine->getCurrentPositionSeconds();
            currentMusicalPos = beatTimeMap->secondsToMusicalPosition(currentTimeSec);
            currentBeat = currentMusicalPos.totalQuarters;
            selectActiveTimelineScenes(currentBeat);
        }

        updateFogFromActiveScenes(dtSec);
        if (dmxLiveEngine != nullptr && dmxLiveEngine->isOutputActive())
        {
            if (!isShowing())
            {
                wasTab3PlayingDmx = false;
            }
            else
            {
                const bool shouldSendDmx = standalonePreviewMode || audioEngine->isPlaying();

                if (shouldSendDmx)
                {
                    if (!standalonePreviewMode)
                        wasTab3PlayingDmx = true;

                    std::vector<DmxLiveEngine::LiveSceneInput> liveInputs;
                    if (!standalonePreviewMode || !oneShotFinished)
                    {
                        liveInputs.reserve(activeScenes.size());
                        for (const auto& entry : activeScenes)
                            liveInputs.push_back({ entry.node, entry.startBeat, entry.durationBeats, entry.progress });
                    }
                    dmxLiveEngine->renderLiveFrame(appState, liveInputs, currentBeat, currentMusicalPos, standalonePreviewMode);
                }
                else if (wasTab3PlayingDmx)
                {
                    wasTab3PlayingDmx = false;
                    dmxLiveEngine->clearAllOutput();
                }
            }
        }
        repaint();
    }

    void updateStandalonePreviewClock(double dtSec)
    {
        activeScenes.clear();

        if (!previewPresetNode.isValid())
        {
            activeSceneName = "Seleziona un Preset da testare";
            currentBeat = 1.0;
            currentTimeSec = 0.0;
            return;
        }

        activeSceneName = previewPresetNode.getProperty(StateKeys::PresetName, "Preset").toString();
        const double dur = juce::jmax(0.25, static_cast<double>(previewPresetNode.getProperty(StateKeys::DurationBeats, 4.0)));
        const bool isOneShot = static_cast<bool>(previewPresetNode.getProperty(StateKeys::IsOneShot, false));

        if (previewPlaying && !oneShotFinished)
        {
            previewBeat += dtSec * (previewBpm / 60.0);
            if (previewBeat >= 1.0 + dur)
            {
                if (isOneShot)
                {
                    previewBeat = 1.0 + dur;
                    oneShotFinished = true;
                    previewPlaying = false;
                    if (onPreviewPlaybackChanged)
                        onPreviewPlaybackChanged(false);
                }
                else
                {
                    previewBeat = 1.0 + std::fmod(previewBeat - 1.0, dur);
                }
            }
        }

        currentBeat = previewBeat;
        currentTimeSec = juce::jmax(0.0, currentBeat - 1.0) * (60.0 / previewBpm);
        currentMusicalPos = BeatTimeMap::makeFixedMeterPosition(
            currentBeat, previewTimeSigNum, previewTimeSigDen, previewBpm);

        ActiveSceneEntry entry;
        entry.node = previewPresetNode;
        entry.startBeat = 1.0;
        entry.durationBeats = dur;
        entry.progress = juce::jlimit(0.0, 1.0, (currentBeat - 1.0) / dur);
        entry.orderIndex = 0;
        entry.layerPriority = getLayerPriority(previewPresetNode);
        activeScenes.push_back(entry);
    }

    void selectActiveTimelineScenes(double beat)
    {
        activeScenes.clear();
        activeSceneName = "Nessuna Scena Attiva (Blackout)";

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
            return;

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            if (!scene.hasType(StateKeys::Scene))
                continue;

            const double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            const double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));

            if (beat >= startB && beat < (startB + durB))
            {
                ActiveSceneEntry entry;
                entry.node = scene;
                entry.startBeat = startB;
                entry.durationBeats = durB;
                entry.progress = juce::jlimit(0.0, 1.0, (beat - startB) / durB);
                entry.orderIndex = i;
                entry.layerPriority = getLayerPriority(scene);
                activeScenes.push_back(entry);
            }
        }

        if (activeScenes.empty())
            return;

        // Ordine di valutazione: Base -> Movement -> OverlayFX; a parita' di layer vince chi inizia dopo (LTP)
        std::sort(activeScenes.begin(), activeScenes.end(),
            [](const ActiveSceneEntry& a, const ActiveSceneEntry& b) {
                if (a.layerPriority != b.layerPriority) return a.layerPriority < b.layerPriority;
                if (std::abs(a.startBeat - b.startBeat) > 1e-5) return a.startBeat < b.startBeat;
                return a.orderIndex < b.orderIndex;
            });

        juce::StringArray names;
        const int maxNames = juce::jmin(3, static_cast<int>(activeScenes.size()));
        for (int i = 0; i < maxNames; ++i)
            names.add(activeScenes[static_cast<size_t>(i)].node.getProperty(StateKeys::SceneName, "Scena").toString());

        activeSceneName = names.joinIntoString(" + ");
        if (static_cast<int>(activeScenes.size()) > maxNames)
            activeSceneName += " (+" + juce::String(static_cast<int>(activeScenes.size()) - maxNames) + ")";
    }

    static bool isCommandAllowedBySceneMask(const juce::ValueTree& sceneNode,
        const juce::String& param,
        const juce::String& curve)
    {
        if (!sceneNode.isValid())
            return true;

        const bool isColorParam = param.equalsIgnoreCase("Red") || param.equalsIgnoreCase("Green")
            || param.equalsIgnoreCase("Blue") || param.equalsIgnoreCase("White") || param.equalsIgnoreCase("UV");
        if (isColorParam)
            return !sceneNode.hasProperty(StateKeys::PresetUseColor)
            || static_cast<bool>(sceneNode.getProperty(StateKeys::PresetUseColor));

        const bool isMoveParam = param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt");
        if (isMoveParam)
            return !sceneNode.hasProperty(StateKeys::PresetUseMove)
            || static_cast<bool>(sceneNode.getProperty(StateKeys::PresetUseMove));

        const bool isStrobeCmd = param.equalsIgnoreCase("Strobe")
            || (param.equalsIgnoreCase("Dimmer") && CurveEvaluator::getStrobeHitsPerQuarter(curve) > 0.0);
        if (isStrobeCmd)
            return !sceneNode.hasProperty(StateKeys::PresetUseStrobe)
            || static_cast<bool>(sceneNode.getProperty(StateKeys::PresetUseStrobe));

        const bool isDimmerCmd = param.equalsIgnoreCase("Dimmer") || param.equalsIgnoreCase("GeyserBlast");
        if (isDimmerCmd)
            return !sceneNode.hasProperty(StateKeys::PresetUseDimmer)
            || static_cast<bool>(sceneNode.getProperty(StateKeys::PresetUseDimmer));

        return true;
    }

    void updateFogFromActiveScenes(double dtSec)
    {
        if (!isSimulationRunning())
            return;

        float smokeOut = 0.0f;
        const double barLenQ = juce::jmax(0.25, currentMusicalPos.barLengthQ);

        for (const auto& entry : activeScenes)
        {
            if (!entry.node.isValid()) continue;
            const double clipElapsedBars = juce::jmax(0.0, currentBeat - entry.startBeat) / barLenQ;

            for (int c = 0; c < entry.node.getNumChildren(); ++c)
            {
                auto cmd = entry.node.getChild(c);
                if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                const juce::String roleName = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
                const juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
                if (StageRoleCatalog::classifyRole(roleName) == StageRoleCatalog::Category::FxSmokeHazer &&
                    param.equalsIgnoreCase("Dimmer"))
                {
                    const float sVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0))) / 255.0f;
                    const float eVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255))) / 255.0f;
                    const juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();

                    if (!isCommandAllowedBySceneMask(entry.node, param, curve))
                        continue;

                    smokeOut = juce::jmax(smokeOut,
                        CurveEvaluator::evaluate(curve, sVal, eVal,
                            static_cast<float>(entry.progress), currentMusicalPos, 0.0, false, clipElapsedBars));
                }

            }
        }

        const float dt = static_cast<float>(dtSec);
        if (smokeOut > 0.02f)
            currentFogDensity = juce::jmin(1.0f, currentFogDensity + smokeOut * 0.27f * dt);
        else
            currentFogDensity = juce::jmax(0.08f, currentFogDensity - 0.05f * dt);
    }

    void evaluateStateForFixture(VisualFixture& vf) const
    {
        vf.state = RoleRuntimeState();
        if (activeScenes.empty())
            return;

        if (standalonePreviewMode && oneShotFinished)
            return;

        const double barLenQ = juce::jmax(0.25, currentMusicalPos.barLengthQ);

        for (const auto& entry : activeScenes)
        {
            if (!entry.node.isValid())
                continue;

            const juce::String phaseMode = entry.node.getProperty(StateKeys::PhaseMode, "Sync").toString();
            double phaseOffsetCycles = 0.0;
            if (phaseMode.equalsIgnoreCase("OddEven"))
                phaseOffsetCycles = (vf.roleSpatialIndex % 2 == 1) ? 0.5 : 0.0;
            else if (phaseMode.equalsIgnoreCase("LeftToRight") && vf.roleSpatialTotal > 1)
                phaseOffsetCycles = static_cast<double>(vf.roleSpatialIndex) / static_cast<double>(vf.roleSpatialTotal);

            bool foundExactRole = false;
            for (int c = 0; c < entry.node.getNumChildren(); ++c)
            {
                auto cmd = entry.node.getChild(c);
                if (!cmd.hasType(StateKeys::RoleCommand)) continue;
                if (cmd.getProperty(StateKeys::TargetRole, "").toString().trim() == vf.roleName)
                {
                    foundExactRole = true;
                    break;
                }
            }

            bool localAny = false;
            bool localHasColor = false;
            float locR = -1.0f, locG = -1.0f, locB = -1.0f, locW = -1.0f, locUv = -1.0f;
            juce::Colour locFallback = vf.state.fallbackColor;

            bool localHasDimmer = false;
            float locDimmer = 0.0f;
            bool localHasMusicalStrobe = false;
            double localStrobeHitsPerQ = 2.0;
            bool localHasHwStrobe = false;
            float locHwStrobe = 0.0f;

            bool localHasPan = false, localHasTilt = false;
            bool localPanStatic = false, localTiltStatic = false;
            float locPan = 0.5f, locTilt = 0.5f;
            bool requestedPan = false, requestedTilt = false;
            bool requestedActivePan = false, requestedActiveTilt = false;

            const double clipElapsedBars = juce::jmax(0.0, currentBeat - entry.startBeat) / barLenQ;

            for (int c = 0; c < entry.node.getNumChildren(); ++c)
            {
                auto cmd = entry.node.getChild(c);
                if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                const juce::String cmdRole = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
                const bool matches = foundExactRole
                    ? (cmdRole == vf.roleName)
                    : (StageRoleCatalog::classifyRole(cmdRole) == vf.category);

                if (!matches)
                    continue;

                const juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
                const float startVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0))) / 255.0f;
                const float endVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255))) / 255.0f;
                const juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();
                const juce::String defaultHex = entry.node.getProperty(StateKeys::PresetColor1, "ff1e3a8a").toString();
                const juce::String colHex = cmd.getProperty(StateKeys::CommandColor, defaultHex).toString();

                if (!isCommandAllowedBySceneMask(entry.node, param, curve))
                    continue;

                const bool isPanOrTilt = param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt");
                const float val = CurveEvaluator::evaluate(curve, startVal, endVal,
                    static_cast<float>(entry.progress), currentMusicalPos, phaseOffsetCycles,
                    isPanOrTilt, clipElapsedBars);

                localAny = true;
                locFallback = juce::Colour::fromString(colHex);

                if (param.equalsIgnoreCase("Dimmer"))
                {
                    localHasDimmer = true;
                    locDimmer = val;

                    const double hitsPerQ = CurveEvaluator::getStrobeHitsPerQuarter(curve);
                    if (hitsPerQ > 0.0)
                    {
                        localHasMusicalStrobe = true;
                        localStrobeHitsPerQ = hitsPerQ;
                    }
                }
                else if (param.equalsIgnoreCase("Red")) { localHasColor = true; locR = val; }
                else if (param.equalsIgnoreCase("Green")) { localHasColor = true; locG = val; }
                else if (param.equalsIgnoreCase("Blue")) { localHasColor = true; locB = val; }
                else if (param.equalsIgnoreCase("White")) { localHasColor = true; locW = val; }
                else if (param.equalsIgnoreCase("UV")) { localHasColor = true; locUv = val; }
                else if (param.equalsIgnoreCase("Strobe")) { localHasHwStrobe = true; locHwStrobe = val; }
                else if (param.equalsIgnoreCase("Pan"))
                {
                    localPanStatic = curve.equalsIgnoreCase("Static");
                    requestedPan = true;
                    if (!curve.equalsIgnoreCase("Static"))
                        requestedActivePan = true;

                    if (vf.panTiltSpec.hasPanChannel)
                    {
                        localHasPan = true;
                        locPan = val;
                    }
                }
                else if (param.equalsIgnoreCase("Tilt"))
                {
                    localTiltStatic = curve.equalsIgnoreCase("Static");
                    requestedTilt = true;
                    if (!curve.equalsIgnoreCase("Static"))
                        requestedActiveTilt = true;

                    if (vf.panTiltSpec.hasTiltChannel)
                    {
                        localHasTilt = true;
                        locTilt = val;
                    }
                }
            }

            if (!localAny)
                continue;

            vf.state.hasAnyCommand = true;

            // 1. COLORE -> LTP (solo se questa scena scrive davvero canali colore)
            if (localHasColor)
            {
                vf.state.red = locR;
                vf.state.green = locG;
                vf.state.blue = locB;
                vf.state.white = locW;
                vf.state.uv = locUv;
                vf.state.fallbackColor = locFallback;
            }

            // 2. MOVIMENTO PAN/TILT -> LTP (mantiene il PhaseMode della scena di movimento)
            const bool isFixedShape = entry.node.getProperty(StateKeys::PresetMoveShape, "Fixed").toString().equalsIgnoreCase("Fixed");
            const float scenePanFan = isFixedShape ? (static_cast<float>(static_cast<double>(entry.node.getProperty(StateKeys::PresetMovePanFan, 0))) / 100.0f) : 0.0f;
            const float sceneTiltFan = isFixedShape ? (static_cast<float>(static_cast<double>(entry.node.getProperty(StateKeys::PresetMoveTiltFan, 0))) / 100.0f) : 0.0f;

            if (localHasPan)
            {
                vf.state.hasPanCmd = true;
                vf.state.panCmd = locPan;
                vf.state.panIsStatic = localPanStatic;
                vf.state.motionPanFan = scenePanFan;
                vf.state.motionPhaseMode = phaseMode;
            }
            if (localHasTilt)
            {
                vf.state.hasTiltCmd = true;
                vf.state.tiltCmd = locTilt;
                vf.state.tiltIsStatic = localTiltStatic;
                vf.state.motionTiltFan = sceneTiltFan;
                vf.state.motionPhaseMode = phaseMode;
            }

            if (requestedPan || requestedTilt)
            {
                const bool canMoveAny = (requestedPan && vf.panTiltSpec.hasPanChannel)
                    || (requestedTilt && vf.panTiltSpec.hasTiltChannel);
                const bool missingActiveAxis = (requestedActivePan && !vf.panTiltSpec.hasPanChannel && !requestedActiveTilt)
                    || (requestedActiveTilt && !vf.panTiltSpec.hasTiltChannel && !requestedActivePan);

                if (!canMoveAny || missingActiveAxis)
                    vf.state.blockedByMovePolicy = true;
                else
                    vf.state.blockedByMovePolicy = false;
            }

            // 3. STROBO (Override) oppure DIMMER (HTP)
            if (localHasMusicalStrobe)
            {
                const double activeBpm = currentMusicalPos.localBpm;
                if (vf.strobeSpec.mode == 1 || vf.strobeSpec.mode == 2)
                {
                    vf.state.hasDimmerCmd = true;
                    vf.state.dimmer = 1.0f;
                    vf.state.strobe = 1.0f;
                    vf.state.strobeTargetHz = static_cast<float>((activeBpm / 60.0) * localStrobeHitsPerQ);
                    vf.state.blockedByStrobePolicy = false;
                }
                else if (vf.strobeSpec.mode == 3)
                {
                    // Strobo disabilitato su questo faro: ignora lo strobo ma non spegne un eventuale Dimmer Base attivo
                    vf.state.blockedByStrobePolicy = true;
                }
                else
                {
                    // mode == 0 (Dimmer Flash): prevale sul Dimmer Base per lampeggiare davvero
                    vf.state.hasDimmerCmd = true;
                    vf.state.dimmer = locDimmer;
                    vf.state.blockedByStrobePolicy = false;
                }
            }
            else if (localHasDimmer)
            {
                vf.state.dimmer = vf.state.hasDimmerCmd
                    ? juce::jmax(vf.state.dimmer, locDimmer)
                    : locDimmer;
                vf.state.hasDimmerCmd = true;
            }

            if (localHasHwStrobe)
                vf.state.strobe = juce::jmax(vf.state.strobe, locHwStrobe);
        }

        // Cortesia in Preview Standalone (Tab 2): accende il faro se testi un preset di solo Colore o solo Movimento
        if (standalonePreviewMode && vf.state.hasAnyCommand && !activeScenes.empty())
        {
            const juce::String pool = activeScenes.back().node.getProperty(StateKeys::PresetPool, "Color").toString();
            const bool hasCol = vf.state.hasExplicitColor();
            const bool hasMove = (vf.state.hasPanCmd || vf.state.hasTiltCmd);

            if (!vf.state.hasDimmerCmd && !vf.state.blockedByMovePolicy &&
                (pool.equalsIgnoreCase("Color") || pool.equalsIgnoreCase("Movement") || hasCol || hasMove))
            {
                vf.state.dimmer = (hasMove && !hasCol) ? 0.85f : 1.0f;
            }
        }

        // Shutter hardware calibrato in Hz reali del singolo faro
        applyHardwareStrobeToFixture(vf, currentTimeSec);
    }

    static void applyHardwareStrobeToFixture(VisualFixture& vf, double simTimeSec)
    {
        if (vf.strobeSpec.mode != 1 && vf.strobeSpec.mode != 2)
            return;

        if (vf.state.strobe <= 0.01f)
            return;

        if (vf.strobeSpec.mode == 1 && vf.state.dimmer <= 0.01f)
            return;

        float rawHz = 0.0f;
        if (vf.state.strobeTargetHz > 0.01f)
        {
            // Converte Hz -> DMX del faro -> Hz reali (rispetta il tetto hardware minHz..maxHz del faro!)
            int dmxVal = vf.strobeSpec.hzToDmx(vf.state.strobeTargetHz);
            rawHz = vf.strobeSpec.dmxToHz(dmxVal);
        }
        else
        {
            rawHz = vf.strobeSpec.minHz + vf.state.strobe * (vf.strobeSpec.maxHz - vf.strobeSpec.minHz);
        }

        if (rawHz <= 0.05f)
            return;

        float simHz = juce::jlimit(0.5f, 25.0f, rawHz);
        double phase = std::fmod(simTimeSec * static_cast<double>(simHz), 1.0);
        const bool isFlashOn = (phase < 0.25);

        if (vf.strobeSpec.mode == 1)
        {
            if (!isFlashOn)
                vf.state.dimmer = 0.0f;
        }
        else if (vf.strobeSpec.mode == 2)
        {
            vf.state.dimmer = isFlashOn ? 1.0f : 0.0f;
        }
    }

    std::vector<VisualFixture> buildVisualFixtures(const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY) const
    {
        std::vector<VisualFixture> placedList;
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);

        if (venueNode.isValid())
        {
            for (int f = 0; f < venueNode.getNumChildren(); ++f)
            {
                auto fix = venueNode.getChild(f);
                if (!fix.hasType(StateKeys::Fixture))
                    continue;

                juce::String fName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
                juce::String rName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
                if (rName.isEmpty()) rName = fName;
                auto cat = StageRoleCatalog::classifyRole(rName, fName);

                int startAddr = static_cast<int>(fix.getProperty(StateKeys::StartAddress, 1));
                auto fixPanTiltSpec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(fix);
                auto fixStrobeSpec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(fix);

                int unitCount = 0;
                for (int u = 0; u < fix.getNumChildren(); ++u)
                {
                    auto unit = fix.getChild(u);
                    if (!unit.hasType(StateKeys::FixtureUnit))
                        continue;

                    ++unitCount;
                    if (!static_cast<bool>(unit.getProperty(StateKeys::IsPlaced, false)))
                        continue;

                    float nx = static_cast<float>(static_cast<double>(unit.getProperty(StateKeys::PosX, 0.5)));
                    float ny = static_cast<float>(static_cast<double>(unit.getProperty(StateKeys::PosY, 0.14)));

                    VisualFixture vf;
                    vf.name = fName + " #" + juce::String(unitCount);
                    vf.roleName = rName;
                    vf.category = cat;
                    vf.normX = nx;
                    vf.pos = {
                        stageRect.getX() + nx * stageRect.getWidth(),
                        stageRect.getY() + ny * stageRect.getHeight()
                    };
                    vf.unitIndex = unitCount - 1;
                    vf.panTiltSpec = fixPanTiltSpec;
                    vf.strobeSpec = fixStrobeSpec;
                    vf.dmxStartAddress = startAddr;
                    vf.unitNode = unit;

                    placedList.push_back(vf);
                }
            }
        }

        // Fallback se nessun faro e' ancora stato piazzato dal Light Case
        if (placedList.empty())
        {
            using Cat = StageRoleCatalog::Category;
            struct DefItem { juce::String name; juce::String role; Cat cat; float nx; float ny; int idx; };
            std::vector<DefItem> defaults = {
                { "Wash Back #1",  "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack,         0.20f, 0.14f, 0 },
                { "Wash Back #2",  "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack,         0.50f, 0.14f, 1 },
                { "Wash Back #3",  "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack,         0.80f, 0.14f, 2 },
                { "Beam Aria #1",  "Aria: Teste Mobili / Beam",          Cat::AirMovingBeam,       0.28f, 0.14f, 0 },
                { "Beam Aria #2",  "Aria: Teste Mobili / Beam",          Cat::AirMovingBeam,       0.72f, 0.14f, 1 },
                { "Par Front #1",  "Aria: Par / Spot Frontali (Band)",   Cat::AirParFront,         0.38f, 0.14f, 0 },
                { "Par Front #2",  "Aria: Par / Spot Frontali (Band)",   Cat::AirParFront,         0.62f, 0.14f, 1 },
                { "Blinder #1",    "Impatto: Blinder Pubblico",          Cat::ImpactBlinder,       0.12f, 0.14f, 0 },
                { "Blinder #2",    "Impatto: Blinder Pubblico",          Cat::ImpactBlinder,       0.88f, 0.14f, 1 },
                { "Strobo Truss",  "Impatto: Strobo",                    Cat::ImpactStrobe,        0.50f, 0.10f, 0 },
                { "Floor Back #1", "Terra: Controluce / Sotto-Batteria", Cat::FloorBackSilhouette, 0.36f, 0.84f, 0 },
                { "Floor Back #2", "Terra: Controluce / Sotto-Batteria", Cat::FloorBackSilhouette, 0.64f, 0.84f, 1 },
                { "Geyser SX",     "FX: Geyser Verticale (Fumo + LED)",  Cat::FxGeyserBurst,       0.24f, 0.84f, 0 },
                { "Geyser DX",     "FX: Geyser Verticale (Fumo + LED)",  Cat::FxGeyserBurst,       0.76f, 0.84f, 1 },
                { "Hazer Palco",   "FX: Macchina del Fumo / Hazer",      Cat::FxSmokeHazer,        0.50f, 0.84f, 0 }
            };

            for (const auto& d : defaults)
            {
                VisualFixture vf;
                vf.name = d.name;
                vf.roleName = d.role;
                vf.category = d.cat;
                vf.normX = d.nx;
                vf.pos = { stageRect.getX() + d.nx * stageRect.getWidth(),
                           (d.ny < 0.5f) ? trussY : floorY };
                vf.unitIndex = d.idx;
                vf.dmxStartAddress = static_cast<int>(placedList.size()) * 10 + 1;

                vf.panTiltSpec.applyQuickSpread(1);
                const bool isMovingFallback = (d.cat == Cat::AirMovingBeam || d.cat == Cat::FloorMovingBeam);
                vf.panTiltSpec.hasPanChannel = isMovingFallback;
                vf.panTiltSpec.hasTiltChannel = isMovingFallback;

                placedList.push_back(vf);
            }
        }

        // Ordina da sinistra a destra per posizionamento spaziale (Onda SX->DX, Pari/Dispari, Ventaglio)
        std::sort(placedList.begin(), placedList.end(),
            [](const VisualFixture& a, const VisualFixture& b) { return a.normX < b.normX; });

        // Ordina da sinistra a destra: il primo indirizzo DMX che appare a SX prende indice 0, il secondo indice 1, ecc.
                // I cloni che condividono lo stesso indirizzo DMX ricevono lo STESSO indice e restano sempre sincroni!
        std::map<juce::String, std::vector<int>> roleUniqueAddresses;
        for (const auto& vf : placedList)
        {
            auto& addrList = roleUniqueAddresses[vf.roleName];
            if (std::find(addrList.begin(), addrList.end(), vf.dmxStartAddress) == addrList.end())
                addrList.push_back(vf.dmxStartAddress);
        }

        for (auto& vf : placedList)
        {
            const auto& addrList = roleUniqueAddresses[vf.roleName];
            auto it = std::find(addrList.begin(), addrList.end(), vf.dmxStartAddress);
            vf.roleSpatialIndex = static_cast<int>(std::distance(addrList.begin(), it));
            vf.roleSpatialTotal = juce::jmax(1, static_cast<int>(addrList.size()));
            evaluateStateForFixture(vf);
        }

        return placedList;
    }

    void drawHeaderHud(juce::Graphics& g, const juce::Rectangle<float>& hudRect)
    {
        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));

        juce::String prefix = standalonePreviewMode
            ? ("ANTEPRIMA (" + juce::String(previewBpm, 0) + " BPM)")
            : "SIMULATORE PALCO 2D (60 FPS)";

        juce::String status = prefix
            + "  |  " + currentMusicalPos.formatBarString()
            + "  |  Q: " + juce::String(currentBeat, 2)
            + "  |  " + activeSceneName
            + "  |  Nebbia: " + juce::String(static_cast<int>(currentFogDensity * 100.0f)) + "%";

        g.drawText(status, hudRect.reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void drawStageBackdrop(juce::Graphics& g, const juce::Rectangle<float>& stageRect)
    {
        StageArtworkHelper::drawStageBackdrop(g, stageRect);
    }

    void drawTrussAndFloorStructure(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        StageArtworkHelper::drawTrussAndFloorStructure(g, stageRect, trussY, floorY);
    }

    void drawBandSilhouettes(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float floorY,
        const std::vector<VisualFixture>& fixtures)
    {
        float frontIllumination = 0.0f;
        juce::Colour frontTint = juce::Colours::white;

        for (const auto& vf : fixtures)
        {
            if ((vf.category == StageRoleCatalog::Category::AirParFront ||
                vf.category == StageRoleCatalog::Category::FloorSideWash) &&
                vf.state.dimmer > frontIllumination)
            {
                frontIllumination = vf.state.dimmer;
                frontTint = vf.state.hasExplicitColor()
                    ? vf.state.getEffectiveColour()
                    : juce::Colour(0xff94a3b8);
            }
        }

        StageArtworkHelper::drawBandSilhouettes(g, stageRect, floorY, frontIllumination, frontTint);

        juce::Colour baseSilhouette(0xff0b0c10);
        juce::Colour litBody = baseSilhouette.interpolatedWith(frontTint, frontIllumination * 0.45f);
        juce::Colour rimCol = juce::Colour(0xff2c3040).interpolatedWith(frontTint, frontIllumination * 0.7f);

        float cx = stageRect.getCentreX();
        float stageH = floorY - stageRect.getY();
        float scale = juce::jlimit(0.6f, 1.5f, stageH / 320.0f);

        juce::Rectangle<float> drumRiser(cx - 55.0f * scale, floorY - 18.0f * scale, 110.0f * scale, 18.0f * scale);
        g.setColour(juce::Colour(0xff14161f));
        g.fillRect(drumRiser);
        g.setColour(rimCol.withAlpha(0.5f));
        g.drawRect(drumRiser, 1.0f);

        g.setColour(litBody);
        g.fillEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale);
        g.fillEllipse(cx - 9.0f * scale, drumRiser.getY() - 62.0f * scale, 18.0f * scale, 18.0f * scale);
        g.setColour(rimCol);
        g.drawEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale, 1.2f);

        float positionsX[3] = {
            stageRect.getX() + stageRect.getWidth() * 0.27f,
            cx,
            stageRect.getX() + stageRect.getWidth() * 0.73f
        };

        for (int i = 0; i < 3; ++i)
        {
            float px = positionsX[i];
            float bodyH = 54.0f * scale;
            float bodyW = 22.0f * scale;

            juce::Path memberPath;
            memberPath.addRoundedRectangle(px - bodyW * 0.5f, floorY - bodyH, bodyW, bodyH, 6.0f * scale);
            memberPath.addEllipse(px - 8.0f * scale, floorY - bodyH - 18.0f * scale, 16.0f * scale, 16.0f * scale);

            g.setColour(litBody);
            g.fillPath(memberPath);
            g.setColour(rimCol.withAlpha(0.65f));
            g.strokePath(memberPath, juce::PathStrokeType(1.2f));
        }
    }

    void computeEffectivePanTilt(const VisualFixture& vf, float& outPanSigned, float& outTiltNorm) const
    {
        const juce::String phaseMode = vf.state.motionPhaseMode.isNotEmpty()
            ? vf.state.motionPhaseMode
            : "SymmetricalFan";

        float effPan = vf.state.hasPanCmd ? vf.state.panCmd : 0.5f;
        float effTilt = vf.state.hasTiltCmd ? vf.state.tiltCmd : 0.5f;

        if (vf.state.panIsStatic || vf.state.tiltIsStatic)
        {
            float fannedPan = effPan, fannedTilt = effTilt;
            StageRoleCatalog::PanTiltHardwareSpec::applyPositionFan(
                effPan, effTilt,
                vf.state.panIsStatic ? vf.state.motionPanFan : 0.0f,
                vf.state.tiltIsStatic ? vf.state.motionTiltFan : 0.0f,
                vf.roleSpatialIndex, vf.roleSpatialTotal, phaseMode,
                fannedPan, fannedTilt);
            effPan = fannedPan;
            effTilt = fannedTilt;
        }

        const bool mirrorPan = !vf.state.panIsStatic
            && phaseMode.equalsIgnoreCase("SymmetricalFan")
            && vf.roleSpatialTotal > 1
            && vf.normX > 0.501f;

        if (vf.panTiltSpec.hasPanChannel)
        {
            const int panDmx = vf.panTiltSpec.mapStagePanToDmx(effPan, mirrorPan);

            const float panCenter = 0.5f * static_cast<float>(vf.panTiltSpec.panLeft + vf.panTiltSpec.panRight);
            const float panHalfSpan = 0.5f * static_cast<float>(vf.panTiltSpec.panRight - vf.panTiltSpec.panLeft);
            const float normSigned = (std::abs(panHalfSpan) > 0.5f)
                ? juce::jlimit(-1.0f, 1.0f, (static_cast<float>(panDmx) - panCenter) / panHalfSpan)
                : 0.0f;

            const float widthScale = juce::jlimit(0.25f, 1.0f,
                static_cast<float>(vf.panTiltSpec.getPanSpan()) / 180.0f);
            outPanSigned = juce::jlimit(-1.0f, 1.0f, normSigned * widthScale);
        }
        else
        {
            outPanSigned = 0.0f;
        }

        if (vf.panTiltSpec.hasTiltChannel)
        {
            const int tiltDmx = vf.panTiltSpec.mapStageTiltToDmx(effTilt);

            const float tiltCenter = 0.5f * static_cast<float>(vf.panTiltSpec.tiltLow + vf.panTiltSpec.tiltHigh);
            const float tiltHalfSpan = 0.5f * static_cast<float>(vf.panTiltSpec.tiltHigh - vf.panTiltSpec.tiltLow);
            const float normSigned = (std::abs(tiltHalfSpan) > 0.5f)
                ? juce::jlimit(-1.0f, 1.0f, (static_cast<float>(tiltDmx) - tiltCenter) / tiltHalfSpan)
                : 0.0f;

            const float rangeScale = juce::jlimit(0.25f, 1.0f,
                static_cast<float>(vf.panTiltSpec.getTiltSpan()) / 180.0f);
            outTiltNorm = juce::jlimit(0.05f, 0.95f, 0.35f + normSigned * 0.48f * rangeScale);
        }
        else
        {
            outTiltNorm = 0.35f;
        }
    }

    void drawFixtureLightEffect(juce::Graphics& g,
        const VisualFixture& vf,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        const float dim = vf.state.dimmer;
        if (dim <= 0.01f)
            return;

        using Cat = StageRoleCatalog::Category;
        juce::Colour col = vf.state.getEffectiveColour();

        const bool isColorless = !vf.state.hasExplicitColor();
        if (isColorless)
            col = juce::Colours::white;

        float fogBoost = 0.75f + 0.45f * currentFogDensity;
        float beamAlpha = juce::jlimit(0.0f, 0.92f, dim * 0.55f * fogBoost);

        float panSigned = 0.0f;
        float tiltNorm = 0.35f;
        computeEffectivePanTilt(vf, panSigned, tiltNorm);

        float stageSpanH = floorY - trussY;

        switch (vf.category)
        {
        case Cat::AirWashBack:
        case Cat::AirParFront:
        case Cat::AirMovingBeam:
        {
            bool isBeam = (vf.category == Cat::AirMovingBeam);
            float startW = isBeam ? 6.0f : 12.0f;
            float baseEndW = isBeam ? 54.0f
                : (vf.category == Cat::AirWashBack ? stageRect.getWidth() * 0.25f : stageRect.getWidth() * 0.16f);

            float maxSwingX = stageRect.getWidth() * (isBeam ? 0.45f : 0.22f);
            float targetX = juce::jlimit(stageRect.getX() + 12.0f,
                stageRect.getRight() - 12.0f,
                vf.pos.x + panSigned * maxSwingX);

            float reachFactor = juce::jlimit(0.15f, 1.05f, 1.15f - tiltNorm * 0.95f);
            float targetY = vf.pos.y + stageSpanH * reachFactor;
            float perspectiveEndW = baseEndW * (0.55f + 0.45f * reachFactor);

            drawLightCone(g, vf.pos, { targetX, targetY }, startW, perspectiveEndW, col,
                isBeam ? juce::jmin(0.95f, beamAlpha * 1.35f) : beamAlpha, isColorless);
            

            if (tiltNorm > 0.65f)
            {
                float eyeFactor = (tiltNorm - 0.65f) / 0.35f;
                float flareRad = (isBeam ? 45.0f : 32.0f) * eyeFactor * dim;
                if (isColorless)
                {
                    g.setColour(juce::Colours::white.withAlpha(eyeFactor * dim * 0.7f));
                    g.drawEllipse(targetX - flareRad * 0.5f, targetY - flareRad * 0.5f, flareRad, flareRad, 1.3f);
                }
                else
                {
                    juce::ColourGradient flare(juce::Colours::white.withAlpha(eyeFactor * dim * 0.85f), targetX, targetY,
                        col.withAlpha(0.0f), targetX + flareRad, targetY, true);
                    g.setGradientFill(flare);
                    g.fillEllipse(targetX - flareRad, targetY - flareRad, flareRad * 2.0f, flareRad * 2.0f);
                }
            }
            break;
        }

        case Cat::FloorBackSilhouette:
        case Cat::FloorMovingBeam:
        {
            bool isBeam = (vf.category == Cat::FloorMovingBeam);
            float maxSwingX = stageRect.getWidth() * (isBeam ? 0.42f : 0.20f);
            float targetX = juce::jlimit(stageRect.getX() + 12.0f,
                stageRect.getRight() - 12.0f,
                vf.pos.x + panSigned * maxSwingX);

            float upReach = juce::jlimit(0.25f, 1.0f, 0.35f + tiltNorm * 0.65f);
            float targetY = vf.pos.y - stageSpanH * upReach;
            float endW = isBeam ? 50.0f : stageRect.getWidth() * 0.23f;

            drawLightCone(g, vf.pos, { targetX, targetY }, isBeam ? 6.0f : 14.0f, endW, col, beamAlpha * 1.2f, isColorless);
            break;
        }

        case Cat::FloorSideWash:
        {
            bool isLeft = (vf.pos.x < stageRect.getCentreX());
            float targetX = isLeft ? (stageRect.getX() + stageRect.getWidth() * 0.72f)
                : (stageRect.getX() + stageRect.getWidth() * 0.28f);
            float targetY = floorY - stageSpanH * juce::jlimit(0.05f, 0.75f, tiltNorm * 0.65f);
            drawLightCone(g, vf.pos, { targetX, targetY }, 10.0f, 90.0f, col, beamAlpha, isColorless);
            break;
        }

        case Cat::ImpactBlinder:
        {
            float radius = 95.0f * dim + 25.0f;
            if (isColorless)
            {
                g.setColour(juce::Colours::white.withAlpha(dim * 0.75f));
                g.drawEllipse(vf.pos.x - radius * 0.6f, vf.pos.y - radius * 0.6f, radius * 1.2f, radius * 1.2f, 1.5f);
                g.setColour(juce::Colours::white.withAlpha(dim * 0.35f));
                g.drawEllipse(vf.pos.x - radius * 0.9f, vf.pos.y - radius * 0.9f, radius * 1.8f, radius * 1.8f, 1.0f);
            }
            else
            {
                juce::Colour blindCol = juce::Colour(0xfffffbeb).interpolatedWith(col, 0.3f);
                juce::ColourGradient grad(blindCol.withAlpha(juce::jmin(1.0f, dim * 0.95f)), vf.pos.x, vf.pos.y,
                    blindCol.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
                g.setGradientFill(grad);
                g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);
            }

            g.setColour(juce::Colours::white.withAlpha(dim * 0.85f));
            g.drawLine(vf.pos.x - radius * 0.7f, vf.pos.y, vf.pos.x + radius * 0.7f, vf.pos.y, isColorless ? 1.4f : 2.5f);
            g.drawLine(vf.pos.x, vf.pos.y - radius * 0.7f, vf.pos.x, vf.pos.y + radius * 0.7f, isColorless ? 1.4f : 2.5f);
            break;
        }

        case Cat::ImpactStrobe:
        {
            float radius = 130.0f * dim;
            if (isColorless)
            {
                g.setColour(juce::Colours::white.withAlpha(dim * 0.80f));
                g.drawEllipse(vf.pos.x - radius * 0.55f, vf.pos.y - radius * 0.55f, radius * 1.1f, radius * 1.1f, 1.6f);
                g.setColour(juce::Colours::white.withAlpha(dim * 0.30f));
                g.drawEllipse(vf.pos.x - radius * 0.9f, vf.pos.y - radius * 0.9f, radius * 1.8f, radius * 1.8f, 1.0f);
            }
            else
            {
                juce::ColourGradient grad(juce::Colours::white.withAlpha(dim * 0.90f), vf.pos.x, vf.pos.y,
                    col.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
                g.setGradientFill(grad);
                g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);
            }

            if (dim > 0.6f)
            {
                g.setColour(juce::Colours::white.withAlpha((dim - 0.5f) * (isColorless ? 0.14f : 0.22f)));
                g.fillRoundedRectangle(stageRect, 6.0f);
            }
            break;
        }

        case Cat::FxGeyserBurst:
        {
            float jetHeight = stageSpanH * 0.82f * dim;
            int numPuffs = 9;
            for (int p = 0; p < numPuffs; ++p)
            {
                float ratio = static_cast<float>(p) / static_cast<float>(numPuffs);
                float py = vf.pos.y - ratio * jetHeight;
                float puffRadius = 10.0f + ratio * 34.0f * dim;
                float puffAlpha = (1.0f - ratio * 0.75f) * dim * 0.75f;

                if (isColorless)
                {
                    g.setColour(juce::Colours::white.withAlpha(puffAlpha * 0.85f));
                    g.drawEllipse(vf.pos.x - puffRadius, py - puffRadius * 0.7f, puffRadius * 2.0f, puffRadius * 1.4f, 1.2f);
                }
                else
                {
                    juce::Colour puffCol = col.interpolatedWith(juce::Colour(0xffe2e8f0), ratio * 0.65f);
                    g.setColour(puffCol.withAlpha(puffAlpha));
                    g.fillEllipse(vf.pos.x - puffRadius, py - puffRadius * 0.7f, puffRadius * 2.0f, puffRadius * 1.4f);
                }
            }
            break;
        }

        case Cat::FxSmokeHazer:
        {
            float sbuffW = 80.0f * dim;
            juce::ColourGradient smokeGrad(juce::Colour(0xffcbd5e1).withAlpha(dim * 0.45f), vf.pos.x, vf.pos.y - 10.0f,
                juce::Colour(0xffcbd5e1).withAlpha(0.0f), vf.pos.x + sbuffW, vf.pos.y - 35.0f, true);
            g.setGradientFill(smokeGrad);
            g.fillEllipse(vf.pos.x - sbuffW, vf.pos.y - 45.0f, sbuffW * 2.0f, 50.0f);
            break;
        }
        }
    }

    static void drawLightCone(juce::Graphics& g,
        juce::Point<float> origin,
        juce::Point<float> target,
        float startWidth,
        float endWidth,
        juce::Colour colour,
        float alpha,
        bool colorlessWireframe = false)
    {
        float dx = target.x - origin.x;
        float dy = target.y - origin.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) return;

        float nx = -dy / len;
        float ny = dx / len;

        juce::Path cone;
        cone.startNewSubPath(origin.x + nx * (startWidth * 0.5f), origin.y + ny * (startWidth * 0.5f));
        cone.lineTo(origin.x - nx * (startWidth * 0.5f), origin.y - ny * (startWidth * 0.5f));
        cone.lineTo(target.x - nx * (endWidth * 0.5f), target.y - ny * (endWidth * 0.5f));
        cone.lineTo(target.x + nx * (endWidth * 0.5f), target.y + ny * (endWidth * 0.5f));
        cone.closeSubPath();

        if (colorlessWireframe)
        {
            // Interno quasi trasparente (lascia comunque leggere l'intensita' del Dimmer)
            juce::ColourGradient faintFill(
                juce::Colours::white.withAlpha(alpha * 0.07f), origin.x, origin.y,
                juce::Colours::white.withAlpha(0.0f), target.x, target.y, false);
            g.setGradientFill(faintFill);
            g.fillPath(cone);

            // Bordi laterali del fascio a gradiente bianco -> argento sfumato
            juce::Path sideEdges;
            sideEdges.startNewSubPath(origin.x + nx * (startWidth * 0.5f), origin.y + ny * (startWidth * 0.5f));
            sideEdges.lineTo(target.x + nx * (endWidth * 0.5f), target.y + ny * (endWidth * 0.5f));
            sideEdges.startNewSubPath(origin.x - nx * (startWidth * 0.5f), origin.y - ny * (startWidth * 0.5f));
            sideEdges.lineTo(target.x - nx * (endWidth * 0.5f), target.y - ny * (endWidth * 0.5f));

            const float edgeAlpha = juce::jlimit(0.18f, 0.95f, alpha * 1.4f);

            // Alone morbido lungo il perimetro
            juce::ColourGradient glowGrad(
                juce::Colours::white.withAlpha(edgeAlpha * 0.26f), origin.x, origin.y,
                juce::Colours::white.withAlpha(0.02f), target.x, target.y, false);
            g.setGradientFill(glowGrad);
            g.strokePath(sideEdges, juce::PathStrokeType(3.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Linea perimetrale nitida a gradiente
            juce::ColourGradient edgeGrad(
                juce::Colours::white.withAlpha(edgeAlpha), origin.x, origin.y,
                juce::Colour(0xffcbd5e1).withAlpha(edgeAlpha * 0.14f), target.x, target.y, false);
            g.setGradientFill(edgeGrad);
            g.strokePath(sideEdges, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Base tratteggiata sul lato largo per chiudere il profilo del cono
            juce::Path cap;
            cap.startNewSubPath(target.x - nx * (endWidth * 0.5f), target.y - ny * (endWidth * 0.5f));
            cap.lineTo(target.x + nx * (endWidth * 0.5f), target.y + ny * (endWidth * 0.5f));
            const float dashes[2] = { 4.0f, 4.0f };
            juce::Path dashedCap;
            juce::PathStrokeType(1.0f).createDashedStroke(dashedCap, cap, dashes, 2);
            g.setColour(juce::Colours::white.withAlpha(edgeAlpha * 0.25f));
            g.fillPath(dashedCap);
            return;
        }

        juce::ColourGradient grad(colour.withAlpha(alpha), origin.x, origin.y,
            colour.withAlpha(0.02f), target.x, target.y, false);
        g.setGradientFill(grad);
        g.fillPath(cone);

        juce::Path core;
        core.startNewSubPath(origin.x + nx * (startWidth * 0.2f), origin.y + ny * (startWidth * 0.2f));
        core.lineTo(origin.x - nx * (startWidth * 0.2f), origin.y - ny * (startWidth * 0.2f));
        core.lineTo(target.x - nx * (endWidth * 0.18f), target.y - ny * (endWidth * 0.18f));
        core.lineTo(target.x + nx * (endWidth * 0.18f), target.y + ny * (endWidth * 0.18f));
        core.closeSubPath();

        juce::ColourGradient coreGrad(colour.brighter(0.6f).withAlpha(alpha * 0.65f), origin.x, origin.y,
            colour.withAlpha(0.0f), target.x, target.y, false);
        g.setGradientFill(coreGrad);
        g.fillPath(core);
    }

    void drawFogOverlay(juce::Graphics& g, const juce::Rectangle<float>& stageRect, float floorY)
    {
        float fogTop = stageRect.getY() + stageRect.getHeight() * 0.30f;
        juce::Rectangle<float> fogArea(stageRect.getX() + 6.0f, fogTop,
            stageRect.getWidth() - 12.0f, floorY - fogTop);

        float alpha = currentFogDensity * 0.22f;
        juce::ColourGradient fogGrad(juce::Colour(0xff94a3b8).withAlpha(0.0f), fogArea.getCentreX(), fogTop,
            juce::Colour(0xffcbd5e1).withAlpha(alpha), fogArea.getCentreX(), floorY, false);
        g.setGradientFill(fogGrad);
        g.fillRect(fogArea);
    }

    static void drawFixtureHardwareBody(juce::Graphics& g, const VisualFixture& vf, bool isHovered, bool isDragged, float trussY)
    {
        const bool isPolicyBlocked = vf.state.blockedByStrobePolicy || vf.state.blockedByMovePolicy;
        juce::Rectangle<float> box(vf.pos.x - 8.0f, vf.pos.y - 6.0f, 16.0f, 12.0f);

        if (isDragged || isHovered)
        {
            juce::Colour hiCol = isDragged ? juce::Colour(0xfffacc15) : juce::Colour(0xff38bdf8);
            g.setColour(hiCol.withAlpha(isDragged ? 0.35f : 0.22f));
            g.fillRoundedRectangle(box.expanded(4.0f), 5.0f);
        }
        else if (isPolicyBlocked)
        {
            g.setColour(juce::Colour(0xfff59e0b).withAlpha(0.24f));
            g.fillRoundedRectangle(box.expanded(3.5f), 5.0f);
        }

        g.setColour(juce::Colour(0xff1e202b));
        g.fillRoundedRectangle(box, 3.0f);

        juce::Colour borderCol = juce::Colour(0xff475569);
        float borderThick = 1.0f;
        if (isDragged) { borderCol = juce::Colour(0xfffacc15); borderThick = 2.0f; }
        else if (isHovered) { borderCol = juce::Colour(0xff38bdf8); borderThick = 2.0f; }
        else if (isPolicyBlocked) { borderCol = juce::Colour(0xfff59e0b); borderThick = 1.6f; }

        g.setColour(borderCol);
        g.drawRoundedRectangle(box, 3.0f, borderThick);

        if (isPolicyBlocked && vf.state.dimmer <= 0.02f)
        {
            g.setColour(juce::Colour(0xff1a1408));
            g.fillEllipse(vf.pos.x - 4.5f, vf.pos.y - 4.5f, 9.0f, 9.0f);

            g.setColour(juce::Colour(0xfff59e0b));
            g.drawEllipse(vf.pos.x - 4.2f, vf.pos.y - 4.2f, 8.4f, 8.4f, 1.3f);
            g.drawLine(vf.pos.x - 2.8f, vf.pos.y - 2.8f,
                vf.pos.x + 2.8f, vf.pos.y + 2.8f, 1.4f);
        }
        else
        {
            const float dim = vf.state.dimmer;
            if (dim > 0.02f && !vf.state.hasExplicitColor())
            {
                g.setColour(juce::Colour(0xff0f172a));
                g.fillEllipse(vf.pos.x - 4.5f, vf.pos.y - 4.5f, 9.0f, 9.0f);
                g.setColour(juce::Colours::white.withAlpha(0.40f + 0.60f * dim));
                g.drawEllipse(vf.pos.x - 4.2f, vf.pos.y - 4.2f, 8.4f, 8.4f, 1.3f);
            }
            else
            {
                const juce::Colour lensCol = vf.state.getEffectiveColour();
                g.setColour(dim > 0.02f ? lensCol.withAlpha(0.35f + 0.65f * dim) : juce::Colour(0xff0f172a));
                g.fillEllipse(vf.pos.x - 4.5f, vf.pos.y - 4.5f, 9.0f, 9.0f);
            }
        }

        juce::Colour labelCol = juce::Colour(0xff94a3b8);
        if (isDragged)            labelCol = juce::Colour(0xfffef08a);
        else if (isPolicyBlocked) labelCol = juce::Colour(0xfffbbf24);

        g.setColour(labelCol.withAlpha(0.95f));
        g.setFont(juce::FontOptions(9.5f, (isDragged || isPolicyBlocked) ? juce::Font::bold : juce::Font::plain));
        float textY = (vf.pos.y <= trussY + 4.0f) ? (vf.pos.y - 17.0f) : (vf.pos.y + 7.0f);
        g.drawText(vf.name, static_cast<int>(vf.pos.x - 44.0f), static_cast<int>(textY),
            88, 12, juce::Justification::centred, true);
    }

    juce::ValueTree& appState;
    StemAudioEngine* audioEngine = nullptr;
    BeatTimeMap* beatTimeMap = nullptr;
    bool standalonePreviewMode = false;


    juce::TextButton expandBtn;
    bool isExpanded = false;

    // Stato Standalone Preview
    juce::ValueTree previewPresetNode;
    double previewBpm = 130.0;
    double previewBeat = 1.0;
    bool previewPlaying = true;
    bool oneShotFinished = false;
    double lastTickMs = 0.0;

    // Stato Scene / Preset attivi in parallelo
    std::vector<ActiveSceneEntry> activeScenes;
    double currentBeat = 1.0;
    double currentTimeSec = 0.0;
    float currentFogDensity = 0.12f;
    juce::String activeSceneName{ "Pronto" };

    // Stato Drag & Drop diretto sul Palco 2D (solo Tab 2)
    bool allowStageDrag = false;
    juce::ValueTree hoveredUnitNode;
    juce::ValueTree draggedUnitNode;
    StageRoleCatalog::Category draggedCategory = StageRoleCatalog::Category::AirWashBack;
    juce::String draggedUnitLabel;
};