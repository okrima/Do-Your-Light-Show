#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include "MusicalContext.h"
#include "../Model/DataStateKeys.h"

class MidiRenderEngine
{
public:
    static juce::MidiMessageSequence renderShow(const juce::ValueTree& appState, const MusicalContext& context)
    {
        juce::MidiMessageSequence sequence;
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);

        if (!venueNode.isValid() || !showNode.isValid()) return sequence;

        int ticksPerBar = (context.ppq * 4 * context.timeSigNumerator) / context.timeSigDenominator;
        int resolutionTicks = context.ppq / 8; // CC ogni 32esimo

        // 1. CICLA SU TUTTE LE SCENE DELLA TIMELINE
        for (auto scene : showNode)
        {
            if (!scene.hasType(StateKeys::Scene)) continue;

            // Legge i quarti decimali invece delle battute intere
            double sceneStartBeat = scene.getProperty(StateKeys::StartBeat, 1.0);
            double sceneDurationBeats = scene.getProperty(StateKeys::DurationBeats, 4.0);

            // Calcolo diretto in tick: 1 Quarto = context.ppq (960 tick)
            long startTick = static_cast<long>((sceneStartBeat - 1.0) * context.ppq);
            long endTick = startTick + static_cast<long>(sceneDurationBeats * context.ppq);

            // 2. CICLA SUI COMANDI DENTRO LA SCENA
            for (auto cmd : scene)
            {
                if (!cmd.hasType(StateKeys::RoleCommand)) continue;

                juce::String targetRole = cmd.getProperty(StateKeys::TargetRole).toString();
                juce::String param = cmd.getProperty(StateKeys::TargetParam).toString();
                int startVal = cmd.getProperty(StateKeys::StartValue, 0);
                int endVal = cmd.getProperty(StateKeys::EndValue, 127);
                juce::String curve = cmd.getProperty(StateKeys::CurveType, "Step (Stato Fisso)").toString();

                // 3. TROVA TUTTE LE MACCHINE NEL LOCALE CHE HANNO QUESTO RUOLO
                for (auto fix : venueNode)
                {
                    if (fix.getProperty(StateKeys::RoleName).toString() == targetRole)
                    {
                        int startAddress = fix.getProperty(StateKeys::StartAddress);
                        int paramChannelOffset = fix.getProperty(StateKeys::getMappingKey(param), 0);

                        // Ignora se la macchina non ha questo parametro mappato
                        if (startAddress < 0 || paramChannelOffset <= 0) continue;

                        int dmxChannel = startAddress + (paramChannelOffset - 1);
                        int midiChannel = ((dmxChannel - 1) / 128) + 1;
                        int midiCc = (dmxChannel - 1) % 128;

                        // 4. GENERA AUTOMAZIONE PER QUESTA MACCHINA
                        if (curve.startsWith("Step"))
                        {
                            sequence.addEvent(juce::MidiMessage::controllerEvent(midiChannel, midiCc, startVal), startTick);
                            sequence.addEvent(juce::MidiMessage::controllerEvent(midiChannel, midiCc, endVal), endTick - 1);
                        }
                        else
                        {
                            for (long tick = startTick; tick <= endTick; tick += resolutionTicks)
                            {
                                double progress = static_cast<double>(tick - startTick) / (endTick - startTick);
                                int val = startVal;

                                if (curve.startsWith("LinearRamp"))
                                {
                                    val = startVal + static_cast<int>((endVal - startVal) * progress);
                                }
                                else if (curve.startsWith("Sine"))
                                {
                                    constexpr double localPI = 3.14159265358979323846;
                                    double sineEase = (std::sin(progress * localPI - (localPI / 2.0)) + 1.0) / 2.0;
                                    val = startVal + static_cast<int>((endVal - startVal) * sineEase);
                                }

                                val = juce::jlimit(0, 127, val);
                                sequence.addEvent(juce::MidiMessage::controllerEvent(midiChannel, midiCc, val), tick);
                            }
                        }
                    }
                } // Fine loop macchine
            } // Fine loop comandi
        } // Fine loop scene

        sequence.updateMatchedPairs();
        return sequence;
    }

    static bool exportToFile(const juce::MidiMessageSequence& sequence, const juce::File& file, int ppq)
    {
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(ppq);
        midiFile.addTrack(sequence);
        juce::FileOutputStream stream(file);
        if (stream.openedOk()) { stream.setPosition(0); stream.truncate(); return midiFile.writeTo(stream); }
        return false;
    }
};