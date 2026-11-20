#pragma once
#include <JuceHeader.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/StageRoleCatalog.h"
#include "../../Audio/StemAudioEngine.h"
#include "../../Model/CurveEvaluator.h"
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
            drawFixtureHardwareBody(g, vf, isHovered, isDragged);

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
            // Quinte laterali SX o DX
            double clampedX = (nx < 0.5) ? juce::jlimit(0.04, 0.22, nx)
                : juce::jlimit(0.78, 0.96, nx);
            double clampedY = juce::jlimit(0.45, 0.92, ny);
            return { clampedX, clampedY };
        }

        if (isAirCategory(cat))
        {
            // Zona Aria / Americana superiore
            return { juce::jlimit(0.04, 0.96, nx), juce::jlimit(0.06, 0.44, ny) };
        }

        // Zona Terra / Palco inferiore
        return { juce::jlimit(0.04, 0.96, nx), juce::jlimit(0.54, 0.94, ny) };
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

        auto drawZoneBox = [&](float x0, float y0, float x1, float y1, juce::Colour col, const juce::String& title)
            {
                juce::Rectangle<float> zr(
                    stageRect.getX() + x0 * stageRect.getWidth(),
                    stageRect.getY() + y0 * stageRect.getHeight(),
                    (x1 - x0) * stageRect.getWidth(),
                    (y1 - y0) * stageRect.getHeight());

                g.setColour(col.withAlpha(0.10f));
                g.fillRoundedRectangle(zr, 6.0f);
                g.setColour(col.withAlpha(0.65f));
                g.drawRoundedRectangle(zr, 6.0f, 1.5f);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(title, zr.reduced(6.0f, 4.0f), juce::Justification::topLeft, true);
            };

        if (draggedCategory == StageRoleCatalog::Category::FloorSideWash)
        {
            drawZoneBox(0.03f, 0.44f, 0.23f, 0.93f, juce::Colour(0xffa855f7), "QUINTA SX");
            drawZoneBox(0.77f, 0.44f, 0.97f, 0.93f, juce::Colour(0xffa855f7), "QUINTA DX");
        }
        else if (isAirCategory(draggedCategory))
        {
            drawZoneBox(0.03f, 0.05f, 0.97f, 0.45f, juce::Colour(0xff38bdf8), "ZONA ARIA: " + draggedUnitLabel);
        }
        else
        {
            drawZoneBox(0.03f, 0.53f, 0.97f, 0.95f, juce::Colour(0xfff59e0b), "ZONA TERRA: " + draggedUnitLabel);
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
        juce::Colour fallbackColor{ 0xff1e3a8a };

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

    bool isSimulationRunning() const
    {
        if (standalonePreviewMode)
            return previewPlaying && !oneShotFinished && activeNode.isValid();

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
            selectActiveTimelineScene(currentBeat);
        }

        updateFogFromActiveNode(dtSec);
        repaint();
    }

    void updateStandalonePreviewClock(double dtSec)
    {
        activeNode = previewPresetNode;
        activeSceneStartBeat = 1.0;

        if (!activeNode.isValid())
        {
            activeSceneName = "Seleziona un Preset da testare";
            activeSceneDuration = 4.0;
            activeSceneProgress = 0.0;
            currentBeat = 1.0;
            currentTimeSec = 0.0;
            return;
        }

        activeSceneName = activeNode.getProperty(StateKeys::PresetName, "Preset").toString();
        activeSceneDuration = juce::jmax(0.25, static_cast<double>(activeNode.getProperty(StateKeys::DurationBeats, 4.0)));
        bool isOneShot = static_cast<bool>(activeNode.getProperty(StateKeys::IsOneShot, false));

        if (previewPlaying && !oneShotFinished)
        {
            previewBeat += dtSec * (previewBpm / 60.0);
            if (previewBeat >= 1.0 + activeSceneDuration)
            {
                if (isOneShot)
                {
                    previewBeat = 1.0 + activeSceneDuration;
                    oneShotFinished = true;
                    previewPlaying = false;
                    if (onPreviewPlaybackChanged)
                        onPreviewPlaybackChanged(false);
                }
                else
                {
                    previewBeat = 1.0 + std::fmod(previewBeat - 1.0, activeSceneDuration);
                }
            }
        }

        currentBeat = previewBeat;
        currentTimeSec = juce::jmax(0.0, currentBeat - 1.0) * (60.0 / previewBpm);
        currentMusicalPos = BeatTimeMap::makeFixedMeterPosition(
            currentBeat, previewTimeSigNum, previewTimeSigDen, previewBpm);
        activeSceneProgress = juce::jlimit(0.0, 1.0, (currentBeat - 1.0) / activeSceneDuration);
    }

    void selectActiveTimelineScene(double beat)
    {
        activeNode = juce::ValueTree();
        activeSceneName = "Nessuna Scena Attiva (Blackout)";
        activeSceneStartBeat = 1.0;
        activeSceneDuration = 4.0;
        activeSceneProgress = 0.0;

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
            return;

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            if (!scene.hasType(StateKeys::Scene))
                continue;

            double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));

            if (beat >= startB && beat < (startB + durB))
            {
                activeNode = scene;
                activeSceneStartBeat = startB;
                activeSceneDuration = durB;
                activeSceneProgress = juce::jlimit(0.0, 1.0, (beat - startB) / durB);
                activeSceneName = scene.getProperty(StateKeys::SceneName, "Scena").toString();
                break;
            }
        }
    }

    void updateFogFromActiveNode(double dtSec)
    {
        // Blocca l'evoluzione della nebbia se il loop o la timeline sono in pausa
        if (!isSimulationRunning())
            return;

        float smokeOut = 0.0f;
        if (activeNode.isValid())
        {
            for (int c = 0; c < activeNode.getNumChildren(); ++c)
            {
                auto cmd = activeNode.getChild(c);
                if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                juce::String roleName = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
                juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
                if (StageRoleCatalog::classifyRole(roleName) == StageRoleCatalog::Category::FxSmokeHazer &&
                    param.equalsIgnoreCase("Dimmer"))
                {
                    float sVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0))) / 255.0f;
                    float eVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255))) / 255.0f;
                    juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();
                    smokeOut = juce::jmax(smokeOut,
                        CurveEvaluator::evaluate(curve, sVal, eVal, static_cast<float>(activeSceneProgress), currentMusicalPos, 0.0));
                }
            }
        }

        float dt = static_cast<float>(dtSec);
        if (smokeOut > 0.02f)
            currentFogDensity = juce::jmin(1.0f, currentFogDensity + smokeOut * 0.27f * dt);
        else
            currentFogDensity = juce::jmax(0.08f, currentFogDensity - 0.05f * dt);
    }

    void evaluateStateForFixture(VisualFixture& vf) const
    {
        vf.state = RoleRuntimeState();
        if (!activeNode.isValid())
            return;

        if (standalonePreviewMode && oneShotFinished)
            return;

        juce::String phaseMode = activeNode.getProperty(StateKeys::PhaseMode, "Sync").toString();
        double phaseOffsetCycles = 0.0;
        if (phaseMode.equalsIgnoreCase("OddEven"))
        {
            phaseOffsetCycles = (vf.roleSpatialIndex % 2 == 1) ? 0.5 : 0.0;
        }
        else if (phaseMode.equalsIgnoreCase("LeftToRight") && vf.roleSpatialTotal > 1)
        {
            phaseOffsetCycles = static_cast<double>(vf.roleSpatialIndex) / static_cast<double>(vf.roleSpatialTotal);
        }

        // 1. Cerca comandi che corrispondono esattamente al nome ruolo (o alla categoria come fallback)
        bool foundExactRole = false;
        for (int c = 0; c < activeNode.getNumChildren(); ++c)
        {
            auto cmd = activeNode.getChild(c);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;
            if (cmd.getProperty(StateKeys::TargetRole, "").toString().trim() == vf.roleName)
            {
                foundExactRole = true;
                break;
            }
        }

        bool hasMusicalStrobeCmd = false;
        double strobeHitsPerBeat = 2.0;

        bool requestedPan = false, requestedTilt = false;
        bool requestedActivePan = false, requestedActiveTilt = false;

        for (int c = 0; c < activeNode.getNumChildren(); ++c)
        {
            auto cmd = activeNode.getChild(c);
            if (!cmd.hasType(StateKeys::RoleCommand)) continue;

            juce::String cmdRole = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
            bool matches = foundExactRole
                ? (cmdRole == vf.roleName)
                : (StageRoleCatalog::classifyRole(cmdRole) == vf.category);

            if (!matches)
                continue;

            juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
            float startVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0))) / 255.0f;
            float endVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255))) / 255.0f;
            juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();
            juce::String defaultHex = activeNode.getProperty(StateKeys::PresetColor1, "ff1e3a8a").toString();
            juce::String colHex = cmd.getProperty(StateKeys::CommandColor, defaultHex).toString();

            float val = CurveEvaluator::evaluate(curve, startVal, endVal,
                static_cast<float>(activeSceneProgress), currentMusicalPos, phaseOffsetCycles);

            vf.state.hasAnyCommand = true;
            vf.state.fallbackColor = juce::Colour::fromString(colHex);

            if (param.equalsIgnoreCase("Dimmer"))
            {
                vf.state.hasDimmerCmd = true;
                vf.state.dimmer = val;

                double hitsPerQ = CurveEvaluator::getStrobeHitsPerQuarter(curve);
                if (hitsPerQ > 0.0)
                {
                    hasMusicalStrobeCmd = true;
                    strobeHitsPerBeat = hitsPerQ;
                }
            }
            else if (param.equalsIgnoreCase("Red"))     vf.state.red = val;
            else if (param.equalsIgnoreCase("Green"))   vf.state.green = val;
            else if (param.equalsIgnoreCase("Blue"))    vf.state.blue = val;
            else if (param.equalsIgnoreCase("White"))   vf.state.white = val;
            else if (param.equalsIgnoreCase("UV"))      vf.state.uv = val;
            else if (param.equalsIgnoreCase("Strobe"))  vf.state.strobe = val;
            else if (param.equalsIgnoreCase("Pan"))
            {
                requestedPan = true;
                if (!curve.equalsIgnoreCase("Static"))
                    requestedActivePan = true;

                if (vf.panTiltSpec.hasPanChannel)
                {
                    vf.state.hasPanCmd = true;
                    vf.state.panCmd = val;
                }
            }
            else if (param.equalsIgnoreCase("Tilt"))
            {
                requestedTilt = true;
                if (!curve.equalsIgnoreCase("Static"))
                    requestedActiveTilt = true;

                if (vf.panTiltSpec.hasTiltChannel)
                {
                    vf.state.hasTiltCmd = true;
                    vf.state.tiltCmd = val;
                }
            }
        }

        if (hasMusicalStrobeCmd)
        {
            const double activeBpm = currentMusicalPos.localBpm;

            if (vf.strobeSpec.mode == 1 || vf.strobeSpec.mode == 2)
            {
                vf.state.dimmer = 1.0f;
                vf.state.strobe = 1.0f;
                vf.state.strobeTargetHz = static_cast<float>((activeBpm / 60.0) * strobeHitsPerBeat);
            }
            else if (vf.strobeSpec.mode == 3)
            {
                vf.state.dimmer = 0.0f;
                vf.state.blockedByStrobePolicy = true;
            }
            // mode == 0 (Dimmer Flash): mantiene il Dimmer Flash 0/255 gia' calcolato da CurveEvaluator
        }

        if (requestedPan || requestedTilt)
        {
            const bool canMoveAnyRequested = (requestedPan && vf.panTiltSpec.hasPanChannel)
                || (requestedTilt && vf.panTiltSpec.hasTiltChannel);
            const bool missingOnlyActiveAxis = (requestedActivePan && !vf.panTiltSpec.hasPanChannel && !requestedActiveTilt)
                || (requestedActiveTilt && !vf.panTiltSpec.hasTiltChannel && !requestedActivePan);

            if (!canMoveAnyRequested || missingOnlyActiveAxis)
                vf.state.blockedByMovePolicy = true;
        }

        // 2. Cortesia in Preview Standalone: se stai testando un preset di solo Colore o solo Movimento
        //    che non scrive il Dimmer, accendiamo la luce in anteprima cosi' vedi subito l'effetto!
        if (standalonePreviewMode && vf.state.hasAnyCommand)
        {
            juce::String pool = activeNode.getProperty(StateKeys::PresetPool, "Color").toString();
            bool hasRgb = (vf.state.red >= 0.0f || vf.state.green >= 0.0f || vf.state.blue >= 0.0f);
            bool hasMove = (vf.state.hasPanCmd || vf.state.hasTiltCmd);

            if (!vf.state.hasDimmerCmd && !vf.state.blockedByMovePolicy && 
                (pool.equalsIgnoreCase("Color") || pool.equalsIgnoreCase("Movement") || hasRgb || hasMove))
            {
                vf.state.dimmer = (hasMove && !hasRgb) ? 0.85f : 1.0f;
                if (hasMove && !hasRgb)
                    vf.state.fallbackColor = juce::Colour(0xff38bdf8);
            }
        }

        // 3. Applica lo shutter hardware calibrato in Hz reali del singolo faro
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
        juce::ColourGradient bgGrad(juce::Colour(0xff10121a), stageRect.getCentreX(), stageRect.getY(),
            juce::Colour(0xff07070b), stageRect.getCentreX(), stageRect.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(stageRect, 6.0f);
    }

    void drawTrussAndFloorStructure(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        float leftX = stageRect.getX() + 20.0f;
        float rightX = stageRect.getRight() - 20.0f;

        g.setColour(juce::Colour(0xff2d3142));
        g.drawLine(leftX, trussY - 4.0f, rightX, trussY - 4.0f, 2.0f);
        g.drawLine(leftX, trussY + 4.0f, rightX, trussY + 4.0f, 2.0f);

        g.setColour(juce::Colour(0xff222533));
        for (float x = leftX; x < rightX - 18.0f; x += 18.0f)
        {
            g.drawLine(x, trussY - 4.0f, x + 9.0f, trussY + 4.0f, 1.0f);
            g.drawLine(x + 9.0f, trussY + 4.0f, x + 18.0f, trussY - 4.0f, 1.0f);
        }

        juce::Rectangle<float> floorDeck(stageRect.getX() + 4.0f, floorY,
            stageRect.getWidth() - 8.0f, stageRect.getBottom() - floorY - 4.0f);
        juce::ColourGradient floorGrad(juce::Colour(0xff181922), floorDeck.getCentreX(), floorY,
            juce::Colour(0xff0d0e14), floorDeck.getCentreX(), floorDeck.getBottom(), false);
        g.setGradientFill(floorGrad);
        g.fillRect(floorDeck);

        g.setColour(juce::Colour(0xff353849));
        g.drawLine(stageRect.getX() + 4.0f, floorY, stageRect.getRight() - 4.0f, floorY, 2.0f);
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
                frontTint = vf.state.getEffectiveColour();
            }
        }

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
        juce::String phaseMode = activeNode.isValid()
            ? activeNode.getProperty(StateKeys::PhaseMode, "SymmetricalFan").toString()
            : "SymmetricalFan";

        const bool mirrorPan = phaseMode.equalsIgnoreCase("SymmetricalFan")
            && vf.roleSpatialTotal > 1
            && vf.normX > 0.501f;

        if (vf.panTiltSpec.hasPanChannel)
        {
            float u = vf.state.hasPanCmd ? vf.state.panCmd : 0.5f;
            if (mirrorPan) u = 1.0f - u;
            const float spanFactor = juce::jlimit(0.0f, 1.0f, static_cast<float>(vf.panTiltSpec.getPanSpan()) / 140.0f);
            outPanSigned = juce::jlimit(-1.0f, 1.0f, (u - 0.5f) * 2.0f * spanFactor);
        }
        else
        {
            outPanSigned = 0.0f;
        }

        if (vf.panTiltSpec.hasTiltChannel)
        {
            const float u = vf.state.hasTiltCmd ? vf.state.tiltCmd : 0.5f;
            const float spanFactor = juce::jlimit(0.0f, 1.0f, static_cast<float>(vf.panTiltSpec.getTiltSpan()) / 100.0f);
            outTiltNorm = juce::jlimit(0.05f, 0.95f, 0.45f + (u - 0.5f) * 0.85f * spanFactor);
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
                isBeam ? juce::jmin(0.95f, beamAlpha * 1.35f) : beamAlpha);

            if (tiltNorm > 0.65f)
            {
                float eyeFactor = (tiltNorm - 0.65f) / 0.35f;
                float flareRad = (isBeam ? 45.0f : 32.0f) * eyeFactor * dim;
                juce::ColourGradient flare(juce::Colours::white.withAlpha(eyeFactor * dim * 0.85f), targetX, targetY,
                    col.withAlpha(0.0f), targetX + flareRad, targetY, true);
                g.setGradientFill(flare);
                g.fillEllipse(targetX - flareRad, targetY - flareRad, flareRad * 2.0f, flareRad * 2.0f);
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

            drawLightCone(g, vf.pos, { targetX, targetY }, isBeam ? 6.0f : 14.0f, endW, col, beamAlpha * 1.2f);
            break;
        }

        case Cat::FloorSideWash:
        {
            bool isLeft = (vf.pos.x < stageRect.getCentreX());
            float targetX = isLeft ? (stageRect.getX() + stageRect.getWidth() * 0.72f)
                : (stageRect.getX() + stageRect.getWidth() * 0.28f);
            float targetY = floorY - stageSpanH * juce::jlimit(0.05f, 0.75f, tiltNorm * 0.65f);
            drawLightCone(g, vf.pos, { targetX, targetY }, 10.0f, 90.0f, col, beamAlpha);
            break;
        }

        case Cat::ImpactBlinder:
        {
            juce::Colour blindCol = juce::Colour(0xfffffbeb).interpolatedWith(col, 0.3f);
            float radius = 95.0f * dim + 25.0f;
            juce::ColourGradient grad(blindCol.withAlpha(juce::jmin(1.0f, dim * 0.95f)), vf.pos.x, vf.pos.y,
                blindCol.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
            g.setGradientFill(grad);
            g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);

            g.setColour(juce::Colours::white.withAlpha(dim * 0.85f));
            g.drawLine(vf.pos.x - radius * 0.7f, vf.pos.y, vf.pos.x + radius * 0.7f, vf.pos.y, 2.5f);
            g.drawLine(vf.pos.x, vf.pos.y - radius * 0.7f, vf.pos.x, vf.pos.y + radius * 0.7f, 2.5f);
            break;
        }

        case Cat::ImpactStrobe:
        {
            float radius = 130.0f * dim;
            juce::ColourGradient grad(juce::Colours::white.withAlpha(dim * 0.90f), vf.pos.x, vf.pos.y,
                col.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
            g.setGradientFill(grad);
            g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);

            if (dim > 0.6f)
            {
                g.setColour(juce::Colours::white.withAlpha((dim - 0.5f) * 0.22f));
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

                juce::Colour puffCol = col.interpolatedWith(juce::Colour(0xffe2e8f0), ratio * 0.65f);
                g.setColour(puffCol.withAlpha(puffAlpha));
                g.fillEllipse(vf.pos.x - puffRadius, py - puffRadius * 0.7f, puffRadius * 2.0f, puffRadius * 1.4f);
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
        float alpha)
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

    static void drawFixtureHardwareBody(juce::Graphics& g,
        const VisualFixture& vf,
        bool isHovered,
        bool isDragged)
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
            juce::Colour lensCol = vf.state.getEffectiveColour();
            float dim = vf.state.dimmer;
            g.setColour(dim > 0.02f ? lensCol.withAlpha(0.35f + 0.65f * dim) : juce::Colour(0xff0f172a));
            g.fillEllipse(vf.pos.x - 4.5f, vf.pos.y - 4.5f, 9.0f, 9.0f);
        }

        juce::Colour labelCol = juce::Colour(0xff94a3b8);
        if (isDragged)            labelCol = juce::Colour(0xfffef08a);
        else if (isPolicyBlocked) labelCol = juce::Colour(0xfffbbf24);

        g.setColour(labelCol.withAlpha(0.95f));
        g.setFont(juce::FontOptions(9.5f, (isDragged || isPolicyBlocked) ? juce::Font::bold : juce::Font::plain));
        float textY = (vf.pos.y < 180.0f) ? (vf.pos.y - 17.0f) : (vf.pos.y + 7.0f);
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

    // Stato Scena / Preset attivo
    juce::ValueTree activeNode;
    double currentBeat = 1.0;
    double currentTimeSec = 0.0;
    double activeSceneStartBeat = 1.0;
    double activeSceneDuration = 4.0;
    double activeSceneProgress = 0.0;
    float currentFogDensity = 0.12f;
    juce::String activeSceneName{ "Pronto" };

    // Stato Drag & Drop diretto sul Palco 2D (solo Tab 2)
    bool allowStageDrag = false;
    juce::ValueTree hoveredUnitNode;
    juce::ValueTree draggedUnitNode;
    StageRoleCatalog::Category draggedCategory = StageRoleCatalog::Category::AirWashBack;
    juce::String draggedUnitLabel;
};