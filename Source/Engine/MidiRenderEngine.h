#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <unordered_map>
#include <vector>
#include <string>

#include "../Model/VenuePatch.h"
#include "../Model/HardwareMidiMapper.h"
#include "../Model/Scene.h"
#include "MusicalContext.h"

class MidiRenderEngine
{
public:
    MidiRenderEngine(MusicalContext ctx, VenuePatch p, HardwareMidiMapper m)
        : context(ctx), patch(std::move(p)), mapper(m) {}

    // Genera la sequenza MIDI completa per una determinata Scena
    juce::MidiMessageSequence renderScene(const Scene& scene) const
    {
        juce::MidiMessageSequence sequence;
        
        long totalTicks = scene.getDurationBars() * context.ppq * context.timeSigNumerator;
        
        // Risoluzione di campionamento delle curve LFO: 
        // 24 ticks (con PPQ=960) = campionamento ogni 1/160 di nota.
        const int stepTicks = context.ppq / 40; 

        // Cache per la deduplicazione: [MidiChannel][CC_Index] -> LastValue
        std::unordered_map<int, std::unordered_map<int, uint8_t>> lastCcValues;
        
        // Cache per lo stato delle Note: [MidiChannel][Note_Index] -> isActive
        std::unordered_map<int, std::unordered_map<int, bool>> lastNoteStates;

        for (long currentTick = 0; currentTick <= totalTicks; currentTick += stepTicks)
        {
            for (const auto& fixture : patch.getFixtures())
            {
                const auto* instr = scene.getInstructionFor(fixture.getName());
                if (!instr) continue;

                // 1. Movimento (Pan & Tilt)
                if (instr->panAnim)
                    processCcParam(fixture, LogicalParam::Pan, instr->panAnim->getValueAtTick(currentTick, context), currentTick, sequence, lastCcValues);
                if (instr->tiltAnim)
                    processCcParam(fixture, LogicalParam::Tilt, instr->tiltAnim->getValueAtTick(currentTick, context), currentTick, sequence, lastCcValues);

                // 2. Colore (RGBWU)
                if (instr->colorAnim)
                {
                    DmxColor color = instr->colorAnim->getColorAtTick(currentTick, context);
                    processCcParam(fixture, LogicalParam::Red, color.r, currentTick, sequence, lastCcValues);
                    processCcParam(fixture, LogicalParam::Green, color.g, currentTick, sequence, lastCcValues);
                    processCcParam(fixture, LogicalParam::Blue, color.b, currentTick, sequence, lastCcValues);
                    processCcParam(fixture, LogicalParam::White, color.w, currentTick, sequence, lastCcValues);
                    processCcParam(fixture, LogicalParam::UV, color.uv, currentTick, sequence, lastCcValues);
                }

                // 3. Trigger Impulsivi (Geyser & Strobo)
                if (instr->geyserAnim)
                    processNoteTrigger(fixture, LogicalParam::GeyserBlast, instr->geyserAnim->isActiveAtTick(currentTick, context), currentTick, sequence, lastNoteStates);
                if (instr->strobeAnim)
                    processNoteTrigger(fixture, LogicalParam::Strobe, instr->strobeAnim->isActiveAtTick(currentTick, context), currentTick, sequence, lastNoteStates);
            }
        }

        // Chiusura di sicurezza per le note rimaste aperte a fine traccia
        closeAllActiveNotes(totalTicks, sequence, lastNoteStates);

        sequence.updateMatchedPairs();
        return sequence;
    }

    // Esporta la sequenza formattata su un file fisico .mid
    bool exportToFile(const juce::MidiMessageSequence& sequence, const juce::File& outputFile) const
    {
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(context.ppq);
        midiFile.addTrack(sequence);

        juce::FileOutputStream outputStream(outputFile);
        if (outputStream.openedOk())
        {
            return midiFile.writeTo(outputStream);
        }
        return false;
    }

private:
    MusicalContext context;
    VenuePatch patch;
    HardwareMidiMapper mapper;

    void processCcParam(const FixtureInstance& fixture, LogicalParam param, uint8_t value, 
                        long tick, juce::MidiMessageSequence& seq,
                        std::unordered_map<int, std::unordered_map<int, uint8_t>>& lastCcCache) const
    {
        auto absoluteDmx = fixture.getAbsoluteDmxAddress(param);
        if (!absoluteDmx.has_value()) return;

        auto target = mapper.getMidiForDmx(absoluteDmx.value(), param);
        if (!target.has_value() || target->type != MidiEventType::ControlChange) return;

        int ch = target->channel;
        int cc = target->index;

        // Deduplicazione: invia il messaggio solo se il valore è cambiato
        if (lastCcCache[ch].find(cc) == lastCcCache[ch].end() || lastCcCache[ch][cc] != value)
        {
            auto midiMsg = juce::MidiMessage::controllerEvent(ch, cc, value);
            seq.addEvent(midiMsg, static_cast<double>(tick));
            lastCcCache[ch][cc] = value;
        }
    }

    void processNoteTrigger(const FixtureInstance& fixture, LogicalParam param, bool shouldBeActive, 
                            long tick, juce::MidiMessageSequence& seq,
                            std::unordered_map<int, std::unordered_map<int, bool>>& lastNoteCache) const
    {
        auto absoluteDmx = fixture.getAbsoluteDmxAddress(param);
        if (!absoluteDmx.has_value()) return;

        auto target = mapper.getMidiForDmx(absoluteDmx.value(), param);
        if (!target.has_value() || target->type != MidiEventType::Note) return;

        int ch = target->channel;
        int noteNum = target->index;
        bool currentlyActive = lastNoteCache[ch][noteNum];

        if (shouldBeActive && !currentlyActive)
        {
            auto noteOn = juce::MidiMessage::noteOn(ch, noteNum, static_cast<juce::uint8>(127));
            seq.addEvent(noteOn, static_cast<double>(tick));
            lastNoteCache[ch][noteNum] = true;
        } 
        else if (!shouldBeActive && currentlyActive)
        {
            auto noteOff = juce::MidiMessage::noteOff(ch, noteNum, static_cast<juce::uint8>(0));
            seq.addEvent(noteOff, static_cast<double>(tick));
            lastNoteCache[ch][noteNum] = false;
        }
    }

    void closeAllActiveNotes(long finalTick, juce::MidiMessageSequence& seq,
                             std::unordered_map<int, std::unordered_map<int, bool>>& lastNoteCache) const
    {
        for (auto& chPair : lastNoteCache)
        {
            int ch = chPair.first;
            for (auto& notePair : chPair.second)
            {
                int noteNum = notePair.first;
                if (notePair.second)
                {
                    auto noteOff = juce::MidiMessage::noteOff(ch, noteNum, static_cast<juce::uint8>(0));
                    seq.addEvent(noteOff, static_cast<double>(finalTick));
                    notePair.second = false; // Reset dello stato
                }
            }
        }
    }
};