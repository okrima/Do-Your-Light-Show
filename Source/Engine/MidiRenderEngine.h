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

            float panHomeNorm = 0.5f;
            float tiltHomeNorm = 0.35f;
            float panOffsetNorm = 0.0f;
            float tiltOffsetNorm = 0.0f;
            float panRangeNorm = 0.47f;
            float tiltRangeNorm = 0.35f;
        };

        std::vector<RenderFixture> fixtures;
        for (int f = 0; f < venueNode.getNumChildren(); ++f)
        {
            auto fix = venueNode.getChild(f);
            if (!fix.hasType(StateKeys::Fixture)) continue;

            int startAddr = static_cast<int>(fix.getProperty(StateKeys::StartAddress, -1));
            if (startAddr <= 0) continue;

            RenderFixture rf;
            rf.node = fix;
            rf.startAddress = startAddr;
            juce::String fName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
            rf.roleName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
            if (rf.roleName.isEmpty()) rf.roleName = fName;
            rf.category = StageRoleCatalog::classifyRole(rf.roleName, fName);
            rf.strobeSpec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(fix);

            rf.panHomeNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::PanHome, 128))) / 255.0f;
            rf.tiltHomeNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::TiltHome, 90))) / 255.0f;
            rf.panOffsetNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::PanOffset, 0))) / 255.0f;
            rf.tiltOffsetNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::TiltOffset, 0))) / 255.0f;
            rf.panRangeNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::PanRange, 120))) / 255.0f;
            rf.tiltRangeNorm = static_cast<float>(static_cast<int>(fix.getProperty(StateKeys::TiltRange, 90))) / 255.0f;

            // Media della PosX delle unita' piazzate (per ordinamento SX->DX e Ventaglio Simmetrico)
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

        // Helper emissione CC con conversione DMX (0..255) -> MIDI CC (0..127) e deduplicazione
        auto emitDmxCc = [&](int dmxChannel, int dmxVal0to255, long tick, std::map<int, int>& lastSentCc)
            {
                if (dmxChannel < 1 || dmxChannel > 512) return;
                int midiChannel = ((dmxChannel - 1) / 128) + 1;
                int midiCc = (dmxChannel - 1) % 128;
                int ccVal = juce::jlimit(0, 127, juce::roundToInt((static_cast<float>(juce::jlimit(0, 255, dmxVal0to255)) / 255.0f) * 127.0f));

                auto it = lastSentCc.find(dmxChannel);
                if (it != lastSentCc.end() && it->second == ccVal)
                    return;

                lastSentCc[dmxChannel] = ccVal;
                sequence.addEvent(juce::MidiMessage::controllerEvent(midiChannel, midiCc, ccVal), static_cast<double>(tick));
            };

        // 3. CICLA SU TUTTE LE SCENE DELLA TIMELINE
        for (int s = 0; s < showNode.getNumChildren(); ++s)
        {
            auto scene = showNode.getChild(s);
            if (!scene.hasType(StateKeys::Scene)) continue;

            double sceneStartBeat = juce::jmax(1.0, static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0)));
            double sceneDurationQ = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));
            juce::String phaseMode = scene.getProperty(StateKeys::PhaseMode, "Sync").toString();

            long startTick = static_cast<long>(std::llround((sceneStartBeat - 1.0) * static_cast<double>(ppq)));
            long endTick = startTick + static_cast<long>(std::llround(sceneDurationQ * static_cast<double>(ppq)));
            if (endTick <= startTick) continue;

            // Per ogni fixture, valuta tutti i comandi che la riguardano
            for (const auto& rf : fixtures)
            {
                // Determina se la scena ha comandi con nome ruolo esatto o fallback per categoria
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

                double phaseOffsetCycles = 0.0;
                if (phaseMode.equalsIgnoreCase("OddEven"))
                    phaseOffsetCycles = (rf.roleSpatialIndex % 2 == 1) ? 0.5 : 0.0;
                else if (phaseMode.equalsIgnoreCase("LeftToRight") && rf.roleSpatialTotal > 1)
                    phaseOffsetCycles = static_cast<double>(rf.roleSpatialIndex) / static_cast<double>(rf.roleSpatialTotal);

                std::map<int, int> lastSentCc;

                for (int c = 0; c < scene.getNumChildren(); ++c)
                {
                    auto cmd = scene.getChild(c);
                    if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                    juce::String cmdRole = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
                    bool matches = hasExactRole
                        ? (cmdRole == rf.roleName)
                        : (StageRoleCatalog::classifyRole(cmdRole) == rf.category);
                    if (!matches) continue;

                    juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
                    float startNorm = static_cast<float>(juce::jlimit(0, 255, static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0)))) / 255.0f;
                    float endNorm = static_cast<float>(juce::jlimit(0, 255, static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255)))) / 255.0f;
                    juce::String curve = cmd.getProperty(StateKeys::CurveType, "Static").toString();

                    int dimOffset = static_cast<int>(rf.node.getProperty(StateKeys::getMappingKey("Dimmer"), 0));
                    int strOffset = static_cast<int>(rf.node.getProperty(StateKeys::getMappingKey("Strobe"), 0));
                    int parOffset = static_cast<int>(rf.node.getProperty(StateKeys::getMappingKey(param), 0));

                    double strobeHitsPerQ = param.equalsIgnoreCase("Dimmer")
                        ? CurveEvaluator::getStrobeHitsPerQuarter(curve)
                        : 0.0;

                    // --- CASO A: STROBO MUSICALE CON AUTO-RESOLVE HARDWARE PER SINGOLO FARO ---
                    if (strobeHitsPerQ > 0.0)
                    {
                        if (rf.strobeSpec.mode == 1 && strOffset > 0)
                        {
                            // Canale Strobo separato: Dimmer a 255 + Strobe = hzToDmx(targetHz)
                            int dimCh = (dimOffset > 0) ? (rf.startAddress + dimOffset - 1) : -1;
                            int strCh = rf.startAddress + strOffset - 1;

                            for (long tick = startTick; tick < endTick; tick += resolutionTicks)
                            {
                                double beatNow = 1.0 + (static_cast<double>(tick) / static_cast<double>(ppq));
                                double bpmNow = beatMap.getBpmAtBeat(beatNow);
                                float targetHz = static_cast<float>((bpmNow / 60.0) * strobeHitsPerQ);

                                if (dimCh > 0) emitDmxCc(dimCh, 255, tick, lastSentCc);
                                emitDmxCc(strCh, rf.strobeSpec.hzToDmx(targetHz), tick, lastSentCc);
                            }
                            // A fine scena ripristina shutter aperto
                            emitDmxCc(strCh, rf.strobeSpec.openVal, endTick - 1, lastSentCc);
                            continue;
                        }
                        else if (rf.strobeSpec.mode == 2 && (strOffset > 0 || dimOffset > 0))
                        {
                            // Canale condiviso Dimmer/Strobo: scrive direttamente nel sub-range strobo
                            int sharedOffset = (strOffset > 0) ? strOffset : dimOffset;
                            int sharedCh = rf.startAddress + sharedOffset - 1;

                            for (long tick = startTick; tick < endTick; tick += resolutionTicks)
                            {
                                double beatNow = 1.0 + (static_cast<double>(tick) / static_cast<double>(ppq));
                                double bpmNow = beatMap.getBpmAtBeat(beatNow);
                                float targetHz = static_cast<float>((bpmNow / 60.0) * strobeHitsPerQ);
                                emitDmxCc(sharedCh, rf.strobeSpec.hzToDmx(targetHz), tick, lastSentCc);
                            }
                            emitDmxCc(sharedCh, rf.strobeSpec.openVal, endTick - 1, lastSentCc);
                            continue;
                        }
                        else if (!rf.strobeSpec.allowDimmerStrobe)
                        {
                            // Faro senza strobo hardware e Dimmer Flash disabilitato in Tab 1
                            continue;
                        }
                        // Se mode == 0 && allowDimmerStrobe == true: prosegue col Dimmer Flash 0/255 sotto
                    }

                    if (parOffset <= 0)
                        continue;

                    int dmxChannel = rf.startAddress + (parOffset - 1);

                    // --- CASO B: TUTTI GLI ALTRI PARAMETRI (RGB, Dimmer, Pan/Tilt, Fog, Blinder, ecc.) ---
                    for (long tick = startTick; tick <= endTick; tick += resolutionTicks)
                    {
                        long clampedTick = juce::jmin(tick, endTick - 1);
                        double progress = static_cast<double>(clampedTick - startTick) / static_cast<double>(endTick - startTick);
                        double beatNow = 1.0 + (static_cast<double>(clampedTick) / static_cast<double>(ppq));
                        auto pos = beatMap.beatToMusicalPosition(beatNow);

                        float normVal = CurveEvaluator::evaluate(curve, startNorm, endNorm,
                            static_cast<float>(progress), pos, phaseOffsetCycles);

                        int outDmx = juce::roundToInt(juce::jlimit(0.0f, 1.0f, normVal) * 255.0f);

                        if (param.equalsIgnoreCase("Dimmer") && strobeHitsPerQ <= 0.0)
                        {
                            // Evita di sconfinare nel sub-range strobo sui fari con canale condiviso (mode == 2)
                            outDmx = rf.strobeSpec.scaleNormalDimmer(outDmx);
                        }
                        else if (param.equalsIgnoreCase("Pan"))
                        {
                            float delta = (normVal - 0.5f) * rf.panRangeNorm;
                            if (phaseMode.equalsIgnoreCase("SymmetricalFan") &&
                                rf.roleSpatialTotal > 1 && rf.normX > 0.501f)
                            {
                                delta = -delta;
                            }
                            float finalPan = juce::jlimit(0.0f, 1.0f, rf.panHomeNorm + rf.panOffsetNorm + delta);
                            outDmx = juce::roundToInt(finalPan * 255.0f);
                        }
                        else if (param.equalsIgnoreCase("Tilt"))
                        {
                            float delta = (normVal - 0.5f) * rf.tiltRangeNorm;
                            float finalTilt = juce::jlimit(0.0f, 1.0f, rf.tiltHomeNorm + rf.tiltOffsetNorm + delta);
                            outDmx = juce::roundToInt(finalTilt * 255.0f);
                        }

                        emitDmxCc(dmxChannel, outDmx, clampedTick, lastSentCc);
                        if (tick == endTick - 1) break;
                    }
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

        // Scrive tutti i cambi di metrica (0xFF 0x58)
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