#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include <map>
#include <algorithm>
#include "MusicalContext.h"
#include "../Model/DataStateKeys.h"
#include "../Model/BeatTimeMap.h"
#include "../Model/StageRoleCatalog.h"
#include "../Model/CurveEvaluator.h"

class MidiRenderEngine
{
public:
    static juce::MidiMessageSequence renderShow(const juce::ValueTree& appState, const MusicalContext& context)
    {
        juce::MidiMessageSequence sequence;
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);

        if (!venueNode.isValid() || !showNode.isValid())
            return sequence;

        const int ppq = juce::jmax(96, context.ppq);
        const int resolutionTicks = juce::jmax(1, ppq / 8); // Campionamento ogni 32esimo

        // 1. Ricostruisce la BeatTimeMap (BPM + Cambi Metrica) dallo stato dell'app
        BeatTimeMap beatMap;
        juce::ValueTree stateCopy = appState;
        beatMap.bindToAppState(stateCopy);

        // Scrive Meta-Eventi MIDI Tempo (0xFF 0x51) e Metrica (0xFF 0x58)
        writeTempoAndMeterMetaEvents(sequence, appState, beatMap, context, ppq);

        // 2. Costruisce la tabella delle Fixture con spazialita' DMX univoca per ruolo
        struct RenderFixture
        {
            juce::ValueTree node;
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

        std::vector<RenderFixture> fixtures;
        for (int f = 0; f < venueNode.getNumChildren(); ++f)
        {
            auto fix = venueNode.getChild(f);
            if (!fix.hasType(StateKeys::Fixture)) continue;

            const int startAddr = static_cast<int>(fix.getProperty(StateKeys::StartAddress, -1));
            if (startAddr <= 0) continue;

            RenderFixture rf;
            rf.node = fix;
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

        if (fixtures.empty())
            return sequence;

        // Ordina da SX a DX e assegna indici spaziali SOLO per indirizzi DMX distinti
        std::sort(fixtures.begin(), fixtures.end(),
            [](const RenderFixture& a, const RenderFixture& b) { return a.normX < b.normX; });

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

        // 3. Indicizza le scene della timeline e calcola la durata complessiva
        struct SceneEntry
        {
            juce::ValueTree node;
            double startBeat = 1.0;
            double durationQ = 4.0;
            double endBeat = 5.0;
            int orderIndex = 0;
            int layerPriority = 0;
        };

        std::vector<SceneEntry> allScenes;
        double maxShowEndBeat = 1.0;

        for (int s = 0; s < showNode.getNumChildren(); ++s)
        {
            auto scene = showNode.getChild(s);
            if (!scene.hasType(StateKeys::Scene)) continue;

            SceneEntry se;
            se.node = scene;
            se.startBeat = juce::jmax(1.0, static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0)));
            se.durationQ = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));
            se.endBeat = se.startBeat + se.durationQ;
            se.orderIndex = s;

            const juce::String layer = scene.getProperty(StateKeys::LayerType, "Base").toString();
            se.layerPriority = layer.equalsIgnoreCase("OverlayFX") ? 2 : (layer.equalsIgnoreCase("Movement") ? 1 : 0);

            maxShowEndBeat = juce::jmax(maxShowEndBeat, se.endBeat);
            allScenes.push_back(se);
        }

        if (allScenes.empty())
            return sequence;

        // Ordine di mixing HTP/LTP identico a StageSimulatorView: Base -> Movement -> OverlayFX, poi startBeat
        std::sort(allScenes.begin(), allScenes.end(),
            [](const SceneEntry& a, const SceneEntry& b) {
                if (a.layerPriority != b.layerPriority) return a.layerPriority < b.layerPriority;
                if (std::abs(a.startBeat - b.startBeat) > 1e-5) return a.startBeat < b.startBeat;
                return a.orderIndex < b.orderIndex;
            });

        const long maxEndTick = static_cast<long>(std::llround((maxShowEndBeat - 1.0) * static_cast<double>(ppq)));

        // Helper emissione CC con conversione DMX (0..255) -> MIDI CC (0..127) e deduplicazione globale
        // truncateDown = true garantisce che sul confine Dimmer/Strobo (mode == 2) il CC non arrotondi mai nello strobo
        std::map<int, int> lastSentCc;
        auto emitDmxCc = [&](int dmxChannel, int dmxVal0to255, long tick, bool truncateDown = false)
            {
                if (dmxChannel < 1 || dmxChannel > 512) return;
                const int midiChannel = ((dmxChannel - 1) / 128) + 1;
                const int midiCc = (dmxChannel - 1) % 128;
                const double scaled = (static_cast<double>(juce::jlimit(0, 255, dmxVal0to255)) / 255.0) * 127.0;
                const int ccVal = juce::jlimit(0, 127, truncateDown
                    ? static_cast<int>(std::floor(scaled + 1e-6))
                    : juce::roundToInt(scaled));

                auto it = lastSentCc.find(dmxChannel);
                if (it != lastSentCc.end() && it->second == ccVal)
                    return;

                lastSentCc[dmxChannel] = ccVal;
                sequence.addEvent(juce::MidiMessage::controllerEvent(midiChannel, midiCc, ccVal), static_cast<double>(tick));
            };

        // 4. RENDERING TICK-BY-TICK CON MIXING PARALLELO HTP/LTP
        std::vector<const SceneEntry*> activeScenes;
        activeScenes.reserve(allScenes.size());

        for (long tick = 0; tick <= maxEndTick; tick += resolutionTicks)
        {
            const double beatNow = 1.0 + (static_cast<double>(tick) / static_cast<double>(ppq));
            const auto pos = beatMap.beatToMusicalPosition(beatNow);
            const double barLenQ = juce::jmax(0.25, pos.barLengthQ);

            activeScenes.clear();
            for (const auto& se : allScenes)
            {
                if (beatNow >= se.startBeat - 1e-6 && beatNow < se.endBeat - 1e-6)
                    activeScenes.push_back(&se);
            }

            for (const auto& rf : fixtures)
            {
                bool hasDimmer = false;
                float dimmerVal = 0.0f;

                bool hasMusicalStrobe = false;
                float musicalStrobeHz = 0.0f;
                bool hasHwStrobe = false;
                float hwStrobeVal = 0.0f;

                bool hasRed = false, hasGreen = false, hasBlue = false, hasWhite = false, hasUv = false;
                float redVal = 0.0f, greenVal = 0.0f, blueVal = 0.0f, whiteVal = 0.0f, uvVal = 0.0f;

                bool hasPan = false, hasTilt = false;
                float panVal = 0.5f, tiltVal = 0.5f;
                bool panIsStatic = false, tiltIsStatic = false;
                float motionPanFan = 0.0f, motionTiltFan = 0.0f;
                juce::String motionPhaseMode = "SymmetricalFan";

                bool hasGeyser = false;
                float geyserVal = 0.0f;

                for (const auto* se : activeScenes)
                {
                    bool locPanStatic = false, locTiltStatic = false;

                    const auto& scene = se->node;
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

                    const float progress = static_cast<float>(juce::jlimit(0.0, 1.0, (beatNow - se->startBeat) / se->durationQ));
                    const double clipElapsedBars = juce::jmax(0.0, beatNow - se->startBeat) / barLenQ;

                    bool locHasColor = false;
                    float lR = -1.0f, lG = -1.0f, lB = -1.0f, lW = -1.0f, lUv = -1.0f;
                    bool locHasDimmer = false;
                    float lDim = 0.0f;
                    bool locHasMusicalStrobe = false;
                    double locStrobeHitsPerQ = 0.0;
                    bool locHasHwStrobe = false;
                    float lHwStr = 0.0f;
                    bool locHasPan = false, locHasTilt = false;
                    float lPan = 0.5f, lTilt = 0.5f;
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
                            progress, pos, phaseOffsetCycles, isPanOrTilt, clipElapsedBars);

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
                        else if (param.equalsIgnoreCase("Pan") && rf.panTiltSpec.hasPanChannel)
                        {
                            locHasPan = true;
                            lPan = val;
                            locPanStatic = curve.equalsIgnoreCase("Static");
                        }
                        else if (param.equalsIgnoreCase("Tilt") && rf.panTiltSpec.hasTiltChannel)
                        {
                            locHasTilt = true;
                            lTilt = val;
                            locTiltStatic = curve.equalsIgnoreCase("Static");
                        }
                    }

                    // Colore -> LTP
                    if (locHasColor)
                    {
                        if (lR >= 0.0f) { hasRed = true; redVal = lR; }
                        if (lG >= 0.0f) { hasGreen = true; greenVal = lG; }
                        if (lB >= 0.0f) { hasBlue = true; blueVal = lB; }
                        if (lW >= 0.0f) { hasWhite = true; whiteVal = lW; }
                        if (lUv >= 0.0f) { hasUv = true; uvVal = lUv; }
                    }

                    // Movimento Pan/Tilt -> LTP
                    const bool isFixedShape = scene.getProperty(StateKeys::PresetMoveShape, "Fixed").toString().equalsIgnoreCase("Fixed");
                    const float scPanFan = isFixedShape ? (static_cast<float>(static_cast<double>(scene.getProperty(StateKeys::PresetMovePanFan, 0))) / 100.0f) : 0.0f;
                    const float scTiltFan = isFixedShape ? (static_cast<float>(static_cast<double>(scene.getProperty(StateKeys::PresetMoveTiltFan, 0))) / 100.0f) : 0.0f;
                    if (locHasPan) { hasPan = true; panVal = lPan;  panIsStatic = locPanStatic;  motionPanFan = scPanFan;  motionPhaseMode = phaseMode; }
                    if (locHasTilt) { hasTilt = true; tiltVal = lTilt; tiltIsStatic = locTiltStatic; motionTiltFan = scTiltFan; motionPhaseMode = phaseMode; }

                    // Geyser -> HTP
                    if (locHasGeyser)
                    {
                        geyserVal = hasGeyser ? juce::jmax(geyserVal, lGeyser) : lGeyser;
                        hasGeyser = true;
                    }

                    // Strobo (Override) / Dimmer (HTP)
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
                            dimmerVal = lDim; // Dimmer Flash prevale sul Dimmer base
                        }
                        // mode == 3: strobo ignorato, mantiene eventuale Dimmer base
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

                // --- EMISSIONE CANALI DMX CON SCALING SICURO PER HARDWARE ---

                // A. Dimmer e Strobo (rispetta rigorosamente mode 0/1/2/3)
                if (rf.strobeSpec.mode == 2)
                {
                    // Canale condiviso Dimmer + Strobo
                    const int sharedOff = (rf.strOffset > 0) ? rf.strOffset : rf.dimOffset;
                    if (sharedOff > 0)
                    {
                        const int sharedCh = rf.startAddress + sharedOff - 1;
                        if (hasMusicalStrobe)
                        {
                            emitDmxCc(sharedCh, rf.strobeSpec.hzToDmx(musicalStrobeHz), tick, false);
                        }
                        else if (hasHwStrobe && hwStrobeVal > 0.01f)
                        {
                            const float hz = rf.strobeSpec.minHz + hwStrobeVal * (rf.strobeSpec.maxHz - rf.strobeSpec.minHz);
                            emitDmxCc(sharedCh, rf.strobeSpec.hzToDmx(hz), tick, false);
                        }
                        else
                        {
                            const int rawDim = hasDimmer ? juce::roundToInt(juce::jlimit(0.0f, 1.0f, dimmerVal) * 255.0f) : 0;
                            const int safeDim = rf.strobeSpec.scaleNormalDimmer(rawDim);
                            // truncateDown = true: il MIDI CC riconvertito in DMX resta sempre <= getSafeDimmerMax()
                            emitDmxCc(sharedCh, safeDim, tick, true);
                        }
                    }
                }
                else
                {
                    // Canale Dimmer standard
                    if (rf.dimOffset > 0)
                    {
                        const int dimCh = rf.startAddress + rf.dimOffset - 1;
                        int outDim = 0;
                        if (rf.strobeSpec.mode == 1 && (hasMusicalStrobe || (hasHwStrobe && hwStrobeVal > 0.01f)))
                            outDim = 255;
                        else if (hasDimmer)
                            outDim = juce::roundToInt(juce::jlimit(0.0f, 1.0f, dimmerVal) * 255.0f);

                        emitDmxCc(dimCh, outDim, tick, false);
                    }

                    // Canale Strobo separato (mode == 1)
                    if (rf.strobeSpec.mode == 1 && rf.strOffset > 0)
                    {
                        const int strCh = rf.startAddress + rf.strOffset - 1;
                        if (hasMusicalStrobe)
                        {
                            emitDmxCc(strCh, rf.strobeSpec.hzToDmx(musicalStrobeHz), tick, false);
                        }
                        else if (hasHwStrobe && hwStrobeVal > 0.01f)
                        {
                            const float hz = rf.strobeSpec.minHz + hwStrobeVal * (rf.strobeSpec.maxHz - rf.strobeSpec.minHz);
                            emitDmxCc(strCh, rf.strobeSpec.hzToDmx(hz), tick, false);
                        }
                        else
                        {
                            emitDmxCc(strCh, rf.strobeSpec.openVal, tick, false);
                        }
                    }
                }

                // B. Colore (LTP)
                if (hasRed && rf.redOffset > 0) emitDmxCc(rf.startAddress + rf.redOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, redVal) * 255.0f), tick);
                if (hasGreen && rf.greenOffset > 0) emitDmxCc(rf.startAddress + rf.greenOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, greenVal) * 255.0f), tick);
                if (hasBlue && rf.blueOffset > 0) emitDmxCc(rf.startAddress + rf.blueOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, blueVal) * 255.0f), tick);
                if (hasWhite && rf.whiteOffset > 0) emitDmxCc(rf.startAddress + rf.whiteOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, whiteVal) * 255.0f), tick);
                if (hasUv && rf.uvOffset > 0) emitDmxCc(rf.startAddress + rf.uvOffset - 1, juce::roundToInt(juce::jlimit(0.0f, 1.0f, uvVal) * 255.0f), tick);

                // C. Movimento Pan/Tilt (LTP + calibrazione e totem swap)
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

                if (hasPan && rf.panOffset > 0)
                {
                    const bool mirrorPan = !panIsStatic
                        && motionPhaseMode.equalsIgnoreCase("SymmetricalFan")
                        && rf.roleSpatialTotal > 1
                        && rf.normX > 0.501f;
                    const int panDmx = rf.panTiltSpec.mapStagePanToDmx(effPan, mirrorPan);
                    emitDmxCc(rf.startAddress + rf.panOffset - 1, panDmx, tick);
                }
                if (hasTilt && rf.tiltOffset > 0)
                {
                    const int tiltDmx = rf.panTiltSpec.mapStageTiltToDmx(effTilt);
                    emitDmxCc(rf.startAddress + rf.tiltOffset - 1, tiltDmx, tick);
                }

                // D. Geyser Blast (HTP, torna a 0 fuori scena)
                if (rf.geyserOffset > 0)
                {
                    const int gDmx = hasGeyser ? juce::roundToInt(juce::jlimit(0.0f, 1.0f, geyserVal) * 255.0f) : 0;
                    emitDmxCc(rf.startAddress + rf.geyserOffset - 1, gDmx, tick);
                }
            }
        }

        sequence.sort();
        sequence.updateMatchedPairs();
        return sequence;
    }

    static bool exportToFile(const juce::MidiMessageSequence& sequence, const juce::File& file, int ppq)
    {
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(ppq);
        midiFile.addTrack(sequence);
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
        {
            stream.setPosition(0);
            stream.truncate();
            return midiFile.writeTo(stream);
        }
        return false;
    }

private:
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

    static void writeTempoAndMeterMetaEvents(juce::MidiMessageSequence& seq,
        const juce::ValueTree& appState,
        const BeatTimeMap& beatMap,
        const MusicalContext& context,
        int ppq)
    {
        auto beatMapNode = appState.getChildWithName(StateKeys::BeatMapNode);
        bool wroteTempo = false;

        if (beatMapNode.isValid())
        {
            for (int i = 0; i < beatMapNode.getNumChildren(); ++i)
            {
                auto child = beatMapNode.getChild(i);
                if (child.hasType(StateKeys::BeatPointNode))
                {
                    double beat = juce::jmax(1.0, static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0)));
                    double bpm = juce::jlimit(20.0, 300.0, static_cast<double>(child.getProperty(StateKeys::PointBpm, beatMap.getBaseBpm())));
                    int microsPerQ = static_cast<int>(std::llround(60000000.0 / bpm));
                    double tick = (beat - 1.0) * static_cast<double>(ppq);
                    seq.addEvent(juce::MidiMessage::tempoMetaEvent(microsPerQ), tick);
                    wroteTempo = true;
                }
            }
        }

        if (!wroteTempo)
        {
            double bpm = beatMapNode.isValid() ? beatMap.getBaseBpm() : context.bpm;
            int microsPerQ = static_cast<int>(std::llround(60000000.0 / juce::jlimit(20.0, 300.0, bpm)));
            seq.addEvent(juce::MidiMessage::tempoMetaEvent(microsPerQ), 0.0);
        }

        const auto& meters = beatMap.getMeterAnchors();
        if (!meters.empty())
        {
            for (const auto& m : meters)
            {
                double tick = juce::jmax(0.0, (m.startBeat - 1.0) * static_cast<double>(ppq));
                seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(m.num, m.den), tick);
            }
        }
        else
        {
            seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(context.timeSigNumerator, context.timeSigDenominator), 0.0);
        }
    }
};