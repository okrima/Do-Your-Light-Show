#pragma once
#include "DmxOutputManager.h"
#include "../Model/DataStateKeys.h"
#include "../Model/BeatTimeMap.h"
#include "../Model/StageRoleCatalog.h"
#include "../Model/CurveEvaluator.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

class DmxLiveEngine
{
public:
    struct LiveSceneInput
    {
        juce::ValueTree node;
        double startBeat = 1.0;
        double durationBeats = 4.0;
        double progress = 0.0;
    };

    DmxLiveEngine()
    {
        rawFrame.fill(0);
        channelRole.fill(ChannelRole::Other);
        channelParam.fill(0);
    }

    ~DmxLiveEngine()
    {
        stopOutput(true);
    }

    DmxOutputManager& getOutputManager() noexcept { return outputManager; }
    const DmxOutputManager& getOutputManager() const noexcept { return outputManager; }

    bool startOutput(std::unique_ptr<DmxOutputDriver> driver)
    {
        return outputManager.startOutput(std::move(driver));
    }

    void stopOutput(bool sendFinalBlackout = true)
    {
        outputManager.stopOutput(sendFinalBlackout);
    }

    bool isOutputActive() const noexcept { return outputManager.isOutputActive(); }
    juce::String getStatusText() const { return outputManager.getStatusText(); }
    uint64_t getTxFrameCount() const noexcept { return outputManager.getTxFrameCount(); }
    uint64_t getTxErrorCount() const noexcept { return outputManager.getTxErrorCount(); }

    void setMasterDimmer(float norm0to1)
    {
        masterDimmer.store(juce::jlimit(0.0f, 1.0f, norm0to1));
        reapplyMasterAndBlackout();
    }

    float getMasterDimmer() const noexcept { return masterDimmer.load(); }

    void setBlackout(bool enabled)
    {
        blackoutActive.store(enabled);
        reapplyMasterAndBlackout();
    }

    bool isBlackout() const noexcept { return blackoutActive.load(); }

    void setStandaloneCourtesyDimmer(bool enabled) noexcept
    {
        standaloneCourtesyDimmer.store(enabled);
    }

    bool getStandaloneCourtesyDimmer() const noexcept
    {
        return standaloneCourtesyDimmer.load();
    }

    void resetHeldPositions()
    {
        const juce::ScopedLock sl(stateLock);
        lastPanTiltByChannel.clear();
    }

    void renderLiveFrame(const juce::ValueTree& appState,
        const std::vector<LiveSceneInput>& activeScenes,
        double currentBeat,
        const BeatTimeMap::MusicalPosition& pos,
        bool isStandalonePreview)
    {
        if (fixtureTestActive.load() || manualTestActive.load())
            return;

        if (isStandalonePreview && !tab2LivePreviewEnabled.load())
            return;

        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        if (!venueNode.isValid())
            return;

        struct LiveFixture
        {
            juce::String roleName;
            StageRoleCatalog::Category category = StageRoleCatalog::Category::AirWashBack;
            int startAddress = 1;
            float normX = 0.5f;
            int roleSpatialIndex = 0;
            int roleSpatialTotal = 1;
            StageRoleCatalog::StrobeHardwareSpec strobeSpec;
            StageRoleCatalog::PanTiltHardwareSpec panTiltSpec;

            int dimOffset = 0, strOffset = 0;
            int redOffset = 0, greenOffset = 0, blueOffset = 0, whiteOffset = 0, uvOffset = 0;
            int panOffset = 0, tiltOffset = 0, geyserOffset = 0;
        };

        std::vector<LiveFixture> fixtures;
        for (int f = 0; f < venueNode.getNumChildren(); ++f)
        {
            auto fix = venueNode.getChild(f);
            if (!fix.hasType(StateKeys::Fixture)) continue;

            const int startAddr = static_cast<int>(fix.getProperty(StateKeys::StartAddress, -1));
            if (startAddr <= 0) continue;

            LiveFixture rf;
            rf.startAddress = startAddr;
            const juce::String fName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
            rf.roleName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
            if (rf.roleName.isEmpty()) rf.roleName = fName;
            rf.category = StageRoleCatalog::classifyRole(rf.roleName, fName);
            rf.strobeSpec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(fix);
            rf.panTiltSpec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(fix);

            rf.dimOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Dimmer"), 0));
            rf.strOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Strobe"), 0));
            rf.redOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Red"), 0));
            rf.greenOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Green"), 0));
            rf.blueOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Blue"), 0));
            rf.whiteOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("White"), 0));
            rf.uvOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("UV"), 0));
            rf.geyserOffset = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("GeyserBlast"), 0));

            const int rawPanOff = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Pan"), 0));
            const int rawTiltOff = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Tilt"), 0));
            rf.panOffset = rf.panTiltSpec.isSwapped() ? rawTiltOff : rawPanOff;
            rf.tiltOffset = rf.panTiltSpec.isSwapped() ? rawPanOff : rawTiltOff;

            double sumX = 0.0;
            int placedCount = 0;
            for (int u = 0; u < fix.getNumChildren(); ++u)
            {
                auto unit = fix.getChild(u);
                if (unit.hasType(StateKeys::FixtureUnit) &&
                    static_cast<bool>(unit.getProperty(StateKeys::IsPlaced, false)))
                {
                    sumX += static_cast<double>(unit.getProperty(StateKeys::PosX, 0.5));
                    ++placedCount;
                }
            }
            rf.normX = (placedCount > 0) ? static_cast<float>(sumX / static_cast<double>(placedCount)) : 0.5f;
            fixtures.push_back(rf);
        }

        std::sort(fixtures.begin(), fixtures.end(),
            [](const LiveFixture& a, const LiveFixture& b) { return a.normX < b.normX; });

        std::map<juce::String, std::vector<int>> roleUniqueAddresses;
        for (const auto& rf : fixtures)
        {
            auto& addrList = roleUniqueAddresses[rf.roleName];
            if (std::find(addrList.begin(), addrList.end(), rf.startAddress) == addrList.end())
                addrList.push_back(rf.startAddress);
        }
        for (auto& rf : fixtures)
        {
            const auto& addrList = roleUniqueAddresses[rf.roleName];
            auto it = std::find(addrList.begin(), addrList.end(), rf.startAddress);
            rf.roleSpatialIndex = static_cast<int>(std::distance(addrList.begin(), it));
            rf.roleSpatialTotal = juce::jmax(1, static_cast<int>(addrList.size()));
        }

        const double barLenQ = juce::jmax(0.25, pos.barLengthQ);
        const bool allowCourtesy = isStandalonePreview && standaloneCourtesyDimmer.load();

        std::array<uint8_t, 512> newRaw{};
        std::array<ChannelRole, 512> newRoles{};
        std::array<uint8_t, 512> newParams{};
        newRoles.fill(ChannelRole::Other);

        auto writeRaw = [&](int ch, int val, ChannelRole role = ChannelRole::Other, uint8_t param = 0)
            {
                if (ch < 1 || ch > 512) return;
                const size_t idx = static_cast<size_t>(ch - 1);
                newRaw[idx] = static_cast<uint8_t>(juce::jlimit(0, 255, val));
                newRoles[idx] = role;
                newParams[idx] = param;
            };

        const juce::ScopedLock sl(stateLock);

        for (const auto& rf : fixtures)
        {
            bool hasAnyCmd = false;
            bool hasDimmer = false;
            float dimmerVal = 0.0f;

            bool hasMusicalStrobe = false;
            float musicalStrobeHz = 0.0f;
            bool hasHwStrobe = false;
            float hwStrobeVal = 0.0f;

            bool hasColor = false;
            float redVal = 0.0f, greenVal = 0.0f, blueVal = 0.0f, whiteVal = 0.0f, uvVal = 0.0f;

            bool hasPan = false, hasTilt = false;
            float panVal = 0.5f, tiltVal = 0.5f;
            bool panIsStatic = false, tiltIsStatic = false;
            float motionPanFan = 0.0f, motionTiltFan = 0.0f;
            juce::String motionPhaseMode = "SymmetricalFan";
            bool blockedByMovePolicy = false;

            bool hasGeyser = false;
            float geyserVal = 0.0f;

            for (const auto& se : activeScenes)
            {
                if (!se.node.isValid()) continue;

                const auto& scene = se.node;
                const juce::String phaseMode = scene.getProperty(StateKeys::PhaseMode, "Sync").toString();

                double phaseOffsetCycles = 0.0;
                if (phaseMode.equalsIgnoreCase("OddEven"))
                    phaseOffsetCycles = (rf.roleSpatialIndex % 2 == 1) ? 0.5 : 0.0;
                else if (phaseMode.equalsIgnoreCase("LeftToRight") && rf.roleSpatialTotal > 1)
                    phaseOffsetCycles = static_cast<double>(rf.roleSpatialIndex) / static_cast<double>(rf.roleSpatialTotal);

                bool hasExactRole = false;
                for (int c = 0; c < scene.getNumChildren(); ++c)
                {
                    auto cmd = scene.getChild(c);
                    if (cmd.hasType(StateKeys::RoleCommand) &&
                        cmd.getProperty(StateKeys::TargetRole, "").toString().trim() == rf.roleName)
                    {
                        hasExactRole = true;
                        break;
                    }
                }

                const double clipElapsedBars = juce::jmax(0.0, currentBeat - se.startBeat) / barLenQ;

                bool locAny = false;
                bool locHasColor = false;
                float lR = -1.0f, lG = -1.0f, lB = -1.0f, lW = -1.0f, lUv = -1.0f;
                bool locHasDimmer = false;
                float lDim = 0.0f;
                bool locHasMusicalStrobe = false;
                double locStrobeHitsPerQ = 0.0;
                bool locHasHwStrobe = false;
                float lHwStr = 0.0f;
                bool locHasPan = false, locHasTilt = false;
                bool locPanStatic = false, locTiltStatic = false;
                float lPan = 0.5f, lTilt = 0.5f;
                bool reqPan = false, reqTilt = false;
                bool reqActivePan = false, reqActiveTilt = false;
                bool locHasGeyser = false;
                float lGeyser = 0.0f;

                for (int c = 0; c < scene.getNumChildren(); ++c)
                {
                    auto cmd = scene.getChild(c);
                    if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                    const juce::String cmdRole = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
                    const bool matches = hasExactRole
                        ? (cmdRole == rf.roleName)
                        : (StageRoleCatalog::classifyRole(cmdRole) == rf.category);
                    if (!matches) continue;

                    const juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
                    const float startNorm = static_cast<float>(juce::jlimit(0, 255, static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0)))) / 255.0f;
                    const float endNorm = static_cast<float>(juce::jlimit(0, 255, static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255)))) / 255.0f;
                    const juce::String curve = cmd.getProperty(StateKeys::CurveType, "Static").toString();

                    if (!isCommandAllowedBySceneMask(scene, param, curve))
                        continue;

                    const bool isPanOrTilt = param.equalsIgnoreCase("Pan") || param.equalsIgnoreCase("Tilt");
                    const float val = CurveEvaluator::evaluate(curve, startNorm, endNorm,
                        static_cast<float>(se.progress), pos, phaseOffsetCycles, isPanOrTilt, clipElapsedBars);

                    locAny = true;

                    if (param.equalsIgnoreCase("Dimmer"))
                    {
                        locHasDimmer = true;
                        lDim = val;
                        const double hitsPerQ = CurveEvaluator::getStrobeHitsPerQuarter(curve);
                        if (hitsPerQ > 0.0)
                        {
                            locHasMusicalStrobe = true;
                            locStrobeHitsPerQ = hitsPerQ;
                        }
                    }
                    else if (param.equalsIgnoreCase("Red")) { locHasColor = true; lR = val; }
                    else if (param.equalsIgnoreCase("Green")) { locHasColor = true; lG = val; }
                    else if (param.equalsIgnoreCase("Blue")) { locHasColor = true; lB = val; }
                    else if (param.equalsIgnoreCase("White")) { locHasColor = true; lW = val; }
                    else if (param.equalsIgnoreCase("UV")) { locHasColor = true; lUv = val; }
                    else if (param.equalsIgnoreCase("Strobe")) { locHasHwStrobe = true; lHwStr = val; }
                    else if (param.equalsIgnoreCase("GeyserBlast")) { locHasGeyser = true; lGeyser = val; }
                    else if (param.equalsIgnoreCase("Pan"))
                    {
                        reqPan = true;
                        if (!curve.equalsIgnoreCase("Static")) reqActivePan = true;
                        if (rf.panTiltSpec.hasPanChannel)
                        {
                            locHasPan = true;
                            lPan = val;
                            locPanStatic = curve.equalsIgnoreCase("Static");
                        }
                    }
                    else if (param.equalsIgnoreCase("Tilt"))
                    {
                        reqTilt = true;
                        if (!curve.equalsIgnoreCase("Static")) reqActiveTilt = true;
                        if (rf.panTiltSpec.hasTiltChannel)
                        {
                            locHasTilt = true;
                            lTilt = val;
                            locTiltStatic = curve.equalsIgnoreCase("Static");
                        }
                    }
                }

                if (!locAny) continue;
                hasAnyCmd = true;

                // 1. Colore -> LTP pulito
                if (locHasColor)
                {
                    hasColor = true;
                    redVal = (lR >= 0.0f) ? lR : 0.0f;
                    greenVal = (lG >= 0.0f) ? lG : 0.0f;
                    blueVal = (lB >= 0.0f) ? lB : 0.0f;
                    whiteVal = (lW >= 0.0f) ? lW : 0.0f;
                    uvVal = (lUv >= 0.0f) ? lUv : 0.0f;
                }

                // 2. Movimento Pan/Tilt -> LTP con filtro isFixedShape
                const bool isFixedShape = scene.getProperty(StateKeys::PresetMoveShape, "Fixed").toString().equalsIgnoreCase("Fixed");
                const float scPanFan = isFixedShape ? (static_cast<float>(static_cast<double>(scene.getProperty(StateKeys::PresetMovePanFan, 0))) / 100.0f) : 0.0f;
                const float scTiltFan = isFixedShape ? (static_cast<float>(static_cast<double>(scene.getProperty(StateKeys::PresetMoveTiltFan, 0))) / 100.0f) : 0.0f;

                if (locHasPan) { hasPan = true; panVal = lPan;  panIsStatic = locPanStatic;  motionPanFan = scPanFan;  motionPhaseMode = phaseMode; }
                if (locHasTilt) { hasTilt = true; tiltVal = lTilt; tiltIsStatic = locTiltStatic; motionTiltFan = scTiltFan; motionPhaseMode = phaseMode; }

                if (reqPan || reqTilt)
                {
                    const bool canMoveAny = (reqPan && rf.panTiltSpec.hasPanChannel) || (reqTilt && rf.panTiltSpec.hasTiltChannel);
                    const bool missingActive = (reqActivePan && !rf.panTiltSpec.hasPanChannel && !reqActiveTilt)
                        || (reqActiveTilt && !rf.panTiltSpec.hasTiltChannel && !reqActivePan);
                    blockedByMovePolicy = (!canMoveAny || missingActive);
                }

                // 3. Geyser -> HTP
                if (locHasGeyser)
                {
                    geyserVal = hasGeyser ? juce::jmax(geyserVal, lGeyser) : lGeyser;
                    hasGeyser = true;
                }

                // 4. Strobo (Override) / Dimmer (HTP)
                if (locHasMusicalStrobe)
                {
                    if (rf.strobeSpec.mode == 1 || rf.strobeSpec.mode == 2)
                    {
                        hasMusicalStrobe = true;
                        musicalStrobeHz = static_cast<float>((pos.localBpm / 60.0) * locStrobeHitsPerQ);
                    }
                    else if (rf.strobeSpec.mode == 0)
                    {
                        hasDimmer = true;
                        dimmerVal = lDim;
                    }
                }
                else if (locHasDimmer)
                {
                    dimmerVal = hasDimmer ? juce::jmax(dimmerVal, lDim) : lDim;
                    hasDimmer = true;
                }

                if (locHasHwStrobe)
                {
                    hwStrobeVal = hasHwStrobe ? juce::jmax(hwStrobeVal, lHwStr) : lHwStr;
                    hasHwStrobe = true;
                }
            }

            // Cortesia Preview Tab 2: accende il faro reale quando testi un preset solo Colore o solo Movimento
            if (allowCourtesy && hasAnyCmd && !hasDimmer && !hasMusicalStrobe && !blockedByMovePolicy && !activeScenes.empty())
            {
                const juce::String pool = activeScenes.back().node.getProperty(StateKeys::PresetPool, "Color").toString();
                const bool hasMove = hasPan || hasTilt;
                if (pool.equalsIgnoreCase("Color") || pool.equalsIgnoreCase("Movement") || hasColor || hasMove)
                {
                    hasDimmer = true;
                    dimmerVal = (hasMove && !hasColor) ? 0.85f : 1.0f;

                    // Se e' un preset di solo Movimento senza Colore, illumina in bianco per vederlo dal vivo
                    if (hasMove && !hasColor)
                    {
                        hasColor = true;
                        redVal = greenVal = blueVal = 1.0f;
                    }
                }
            }

            // --- SCRITTURA CANALI RAW (Master Dimmer e Blackout vengono applicati in uscita) ---

            // A. Dimmer e Strobo
            if (rf.strobeSpec.mode == 2)
            {
                const int sharedOff = (rf.strOffset > 0) ? rf.strOffset : rf.dimOffset;
                if (sharedOff > 0)
                {
                    const int sharedCh = rf.startAddress + sharedOff - 1;
                    if (hasMusicalStrobe)
                        writeRaw(sharedCh, rf.strobeSpec.hzToDmx(musicalStrobeHz), ChannelRole::Mode2Strobe, 0);
                    else if (hasHwStrobe && hwStrobeVal > 0.01f)
                    {
                        const float hz = rf.strobeSpec.minHz + hwStrobeVal * (rf.strobeSpec.maxHz - rf.strobeSpec.minHz);
                        writeRaw(sharedCh, rf.strobeSpec.hzToDmx(hz), ChannelRole::Mode2Strobe, 0);
                    }
                    else
                    {
                        const int rawDim = hasDimmer ? juce::roundToInt(juce::jlimit(0.0f, 1.0f, dimmerVal) * 255.0f) : 0;
                        writeRaw(sharedCh, rawDim, ChannelRole::Mode2Dimmer,
                            static_cast<uint8_t>(juce::jlimit(0, 255, rf.strobeSpec.getSafeDimmerMax())));
                    }
                }
            }
            else
            {
                if (rf.dimOffset > 0)
                {
                    const int dimCh = rf.startAddress + rf.dimOffset - 1;
                    int outDim = 0;
                    if (rf.strobeSpec.mode == 1 && (hasMusicalStrobe || (hasHwStrobe && hwStrobeVal > 0.01f)))
                        outDim = 255;
                    else if (hasDimmer)
                        outDim = juce::roundToInt(juce::jlimit(0.0f, 1.0f, dimmerVal) * 255.0f);

                    writeRaw(dimCh, outDim, ChannelRole::Intensity, 0);
                }

                if (rf.strobeSpec.mode == 1 && rf.strOffset > 0)
                {
                    const int strCh = rf.startAddress + rf.strOffset - 1;
                    const uint8_t openByte = static_cast<uint8_t>(juce::jlimit(0, 255, rf.strobeSpec.openVal));
                    if (hasMusicalStrobe)
                        writeRaw(strCh, rf.strobeSpec.hzToDmx(musicalStrobeHz), ChannelRole::StrobeSeparate, openByte);
                    else if (hasHwStrobe && hwStrobeVal > 0.01f)
                    {
                        const float hz = rf.strobeSpec.minHz + hwStrobeVal * (rf.strobeSpec.maxHz - rf.strobeSpec.minHz);
                        writeRaw(strCh, rf.strobeSpec.hzToDmx(hz), ChannelRole::StrobeSeparate, openByte);
                    }
                    else
                    {
                        writeRaw(strCh, openByte, ChannelRole::StrobeSeparate, openByte);
                    }
                }
            }

            // B. Colore
            if (hasColor)
            {
                if (rf.redOffset > 0) writeRaw(rf.startAddress + rf.redOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, redVal) * 255.0f));
                if (rf.greenOffset > 0) writeRaw(rf.startAddress + rf.greenOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, greenVal) * 255.0f));
                if (rf.blueOffset > 0) writeRaw(rf.startAddress + rf.blueOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, blueVal) * 255.0f));
                if (rf.whiteOffset > 0) writeRaw(rf.startAddress + rf.whiteOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, whiteVal) * 255.0f));
                if (rf.uvOffset > 0) writeRaw(rf.startAddress + rf.uvOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, uvVal) * 255.0f));
            }

            // C. Movimento Pan/Tilt (Hold ultima posizione quando nessuna scena muove il faro)
            float effPan = panVal, effTilt = tiltVal;
            if (panIsStatic || tiltIsStatic)
            {
                StageRoleCatalog::PanTiltHardwareSpec::applyPositionFan(
                    panVal, tiltVal,
                    panIsStatic ? motionPanFan : 0.0f,
                    tiltIsStatic ? motionTiltFan : 0.0f,
                    rf.roleSpatialIndex, rf.roleSpatialTotal, motionPhaseMode,
                    effPan, effTilt);
            }

            if (rf.panOffset > 0)
            {
                const int panCh = rf.startAddress + rf.panOffset - 1;
                if (hasPan)
                {
                    const bool mirrorPan = !panIsStatic
                        && motionPhaseMode.equalsIgnoreCase("SymmetricalFan")
                        && rf.roleSpatialTotal > 1
                        && rf.normX > 0.501f;
                    const int panDmx = rf.panTiltSpec.mapStagePanToDmx(effPan, mirrorPan);
                    lastPanTiltByChannel[panCh] = static_cast<uint8_t>(juce::jlimit(0, 255, panDmx));
                    writeRaw(panCh, panDmx);
                }
                else
                {
                    auto it = lastPanTiltByChannel.find(panCh);
                    const int heldPan = (it != lastPanTiltByChannel.end())
                        ? static_cast<int>(it->second)
                        : rf.panTiltSpec.mapStagePanToDmx(0.5f, false);
                    writeRaw(panCh, heldPan);
                }
            }

            if (rf.tiltOffset > 0)
            {
                const int tiltCh = rf.startAddress + rf.tiltOffset - 1;
                if (hasTilt)
                {
                    const int tiltDmx = rf.panTiltSpec.mapStageTiltToDmx(effTilt);
                    lastPanTiltByChannel[tiltCh] = static_cast<uint8_t>(juce::jlimit(0, 255, tiltDmx));
                    writeRaw(tiltCh, tiltDmx);
                }
                else
                {
                    auto it = lastPanTiltByChannel.find(tiltCh);
                    const int heldTilt = (it != lastPanTiltByChannel.end())
                        ? static_cast<int>(it->second)
                        : rf.panTiltSpec.mapStageTiltToDmx(0.5f);
                    writeRaw(tiltCh, heldTilt);
                }
            }

            // D. Geyser Blast
            if (rf.geyserOffset > 0)
            {
                const int gDmx = hasGeyser ? juce::roundToInt(juce::jlimit(0.0f, 1.0f, geyserVal) * 255.0f) : 0;
                writeRaw(rf.startAddress + rf.geyserOffset - 1, gDmx, ChannelRole::Intensity, 0);
            }
        }

        rawFrame = newRaw;
        channelRole = newRoles;
        channelParam = newParams;

        pushProcessedFrameLocked();
    }

    // --- Modalita' Test Faro (Soundcheck Tab 1) ---
    void startFixtureTest(const juce::ValueTree& fixtureNode)
    {
        if (!fixtureNode.isValid())
        {
            stopFixtureTest();
            return;
        }

        const int startAddr = static_cast<int>(fixtureNode.getProperty(StateKeys::StartAddress, -1));
        if (startAddr < 1 || startAddr > 512)
            return;

        const auto strobeSpec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(fixtureNode);
        const auto panTiltSpec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(fixtureNode);

        const int dimOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Dimmer"), 0));
        const int strOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Strobe"), 0));
        const int rOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Red"), 0));
        const int gOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Green"), 0));
        const int bOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Blue"), 0));
        const int wOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("White"), 0));
        const int uvOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("UV"), 0));
        const int rawPanOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Pan"), 0));
        const int rawTiltOff = static_cast<int>(fixtureNode.getProperty(StateKeys::getMappingKey("Tilt"), 0));
        const int panOff = panTiltSpec.isSwapped() ? rawTiltOff : rawPanOff;
        const int tiltOff = panTiltSpec.isSwapped() ? rawPanOff : rawTiltOff;

        const juce::ScopedLock sl(stateLock);
        rawFrame.fill(0);
        channelRole.fill(ChannelRole::Other);
        channelParam.fill(0);

        auto writeRaw = [&](int ch, int val, ChannelRole role = ChannelRole::Other, uint8_t param = 0)
            {
                if (ch < 1 || ch > 512) return;
                const size_t idx = static_cast<size_t>(ch - 1);
                rawFrame[idx] = static_cast<uint8_t>(juce::jlimit(0, 255, val));
                channelRole[idx] = role;
                channelParam[idx] = param;
            };

        // Dimmer & Shutter al 100% sicuro
        if (strobeSpec.mode == 2)
        {
            const int sharedOff = (strOff > 0) ? strOff : dimOff;
            if (sharedOff > 0)
                writeRaw(startAddr + sharedOff - 1, 255, ChannelRole::Mode2Dimmer,
                    static_cast<uint8_t>(juce::jlimit(0, 255, strobeSpec.getSafeDimmerMax())));
        }
        else
        {
            if (dimOff > 0)
                writeRaw(startAddr + dimOff - 1, 255, ChannelRole::Intensity, 0);
            if (strobeSpec.mode == 1 && strOff > 0)
            {
                const uint8_t openByte = static_cast<uint8_t>(juce::jlimit(0, 255, strobeSpec.openVal));
                writeRaw(startAddr + strOff - 1, openByte, ChannelRole::StrobeSeparate, openByte);
            }
        }

        // Bianco pieno (o UV se e' un faro solo UV) per identificarlo subito sul palco
        if (rOff > 0) writeRaw(startAddr + rOff - 1, 255);
        if (gOff > 0) writeRaw(startAddr + gOff - 1, 255);
        if (bOff > 0) writeRaw(startAddr + bOff - 1, 255);
        if (wOff > 0) writeRaw(startAddr + wOff - 1, 255);
        if (rOff == 0 && gOff == 0 && bOff == 0 && wOff == 0 && uvOff > 0)
            writeRaw(startAddr + uvOff - 1, 255);

        // Centra Pan/Tilt nella finestra calibrata
        if (panOff > 0)  writeRaw(startAddr + panOff - 1, panTiltSpec.mapStagePanToDmx(0.5f, false));
        if (tiltOff > 0) writeRaw(startAddr + tiltOff - 1, panTiltSpec.mapStageTiltToDmx(0.5f));

        fixtureTestActive.store(true);
        pushProcessedFrameLocked();
    }

    void stopFixtureTest()
    {
        fixtureTestActive.store(false);

        const juce::ScopedLock sl(stateLock);
        rawFrame.fill(0);
        channelRole.fill(ChannelRole::Other);
        channelParam.fill(0);
        pushProcessedFrameLocked();
    }

    bool isFixtureTestActive() const noexcept { return fixtureTestActive.load(); }

    // --- Modalita' Test Singolo Canale (Finestra Debug Eurolite) ---
    void setManualChannelOverride(int dmxChannel1to512, uint8_t value)
    {
        if (dmxChannel1to512 < 1 || dmxChannel1to512 > 512)
            return;

        const juce::ScopedLock sl(stateLock);
        manualTestActive.store(true);
        rawFrame[static_cast<size_t>(dmxChannel1to512 - 1)] = value;
        channelRole[static_cast<size_t>(dmxChannel1to512 - 1)] = ChannelRole::Other;
        pushProcessedFrameLocked();
    }

    void clearManualChannelOverride()
    {
        if (!manualTestActive.exchange(false))
            return;

        const juce::ScopedLock sl(stateLock);
        rawFrame.fill(0);
        channelRole.fill(ChannelRole::Other);
        channelParam.fill(0);
        pushProcessedFrameLocked();
    }

    bool isManualOverrideActive() const noexcept { return manualTestActive.load(); }

    void setTab2LivePreviewEnabled(bool enabled) noexcept
    {
        tab2LivePreviewEnabled.store(enabled);
    }

    bool isTab2LivePreviewEnabled() const noexcept
    {
        return tab2LivePreviewEnabled.load();
    }

    // Azzera in sicurezza tutti i 512 canali e disattiva eventuali test attivi
    void clearAllOutput()
    {
        fixtureTestActive.store(false);
        manualTestActive.store(false);

        const juce::ScopedLock sl(stateLock);
        rawFrame.fill(0);
        channelRole.fill(ChannelRole::Other);
        channelParam.fill(0);
        pushProcessedFrameLocked();
    }

private:
    enum class ChannelRole : uint8_t
    {
        Other = 0,          // RGB, Pan, Tilt (non toccati da Master Dimmer / Blackout)
        Intensity,          // Dimmer standard o Geyser
        Mode2Dimmer,        // Dimmer su canale condiviso (param = safeDimmerMax)
        Mode2Strobe,        // Strobo attivo su canale condiviso (va a 0 in Blackout)
        StrobeSeparate      // Canale strobo dedicato (torna a param = openVal in Blackout)
    };

    void reapplyMasterAndBlackout()
    {
        const juce::ScopedLock sl(stateLock);
        pushProcessedFrameLocked();
    }

    void pushProcessedFrameLocked()
    {
        const bool blackout = blackoutActive.load();
        const float master = blackout ? 0.0f : masterDimmer.load();

        std::array<uint8_t, 512> out{};
        for (size_t i = 0; i < 512; ++i)
        {
            const uint8_t raw = rawFrame[i];
            switch (channelRole[i])
            {
            case ChannelRole::Intensity:
                out[i] = (blackout || master <= 0.001f)
                    ? 0
                    : static_cast<uint8_t>(juce::jlimit(0, 255, juce::roundToInt(static_cast<float>(raw) * master)));
                break;

            case ChannelRole::Mode2Dimmer:
            {
                if (blackout || master <= 0.001f || raw == 0)
                {
                    out[i] = 0;
                }
                else
                {
                    const float scaled0to1 = (static_cast<float>(raw) / 255.0f) * master;
                    const int safeMax = static_cast<int>(channelParam[i]);
                    out[i] = static_cast<uint8_t>(juce::jlimit(0, safeMax, juce::roundToInt(scaled0to1 * static_cast<float>(safeMax))));
                }
                break;
            }

            case ChannelRole::Mode2Strobe:
                out[i] = (blackout || master <= 0.001f) ? 0 : raw;
                break;

            case ChannelRole::StrobeSeparate:
                out[i] = (blackout || master <= 0.001f) ? channelParam[i] : raw;
                break;

            case ChannelRole::Other:
            default:
                out[i] = raw;
                break;
            }
        }

        outputManager.getUniverseBuffer().writeUniverse(out);
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

    DmxOutputManager outputManager;
    std::atomic<float> masterDimmer{ 1.0f };
    std::atomic<bool> blackoutActive{ false };
    std::atomic<bool> standaloneCourtesyDimmer{ true };

    juce::CriticalSection stateLock;
    std::array<uint8_t, 512> rawFrame{};
    std::array<ChannelRole, 512> channelRole{};
    std::array<uint8_t, 512> channelParam{};
    std::map<int, uint8_t> lastPanTiltByChannel;

    std::atomic<bool> fixtureTestActive{ false };
    std::atomic<bool> manualTestActive{ false };
    std::atomic<bool> tab2LivePreviewEnabled{ false };
};