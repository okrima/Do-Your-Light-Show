#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include "LogicalParam.h"

enum class MidiEventType
{
    ControlChange,
    Note
};

struct MidiTarget
{
    uint8_t channel; // 1-16
    MidiEventType type;
    uint8_t index;   // CC (0-127) o Nota MIDI (0-127)
};

class HardwareMidiMapper
{
public:
    HardwareMidiMapper(bool forceSingleChannel = true, uint8_t baseChannel = 1)
        : forceSingle(forceSingleChannel), baseMidiChannel(baseChannel) {}

    std::optional<MidiTarget> getMidiForDmx(uint16_t absoluteDmx, LogicalParam paramType) const
    {
        if (absoluteDmx < 1 || absoluteDmx > 512) return std::nullopt;

        MidiTarget target;
        uint8_t ccOrNoteIndex = static_cast<uint8_t>((absoluteDmx - 1) % 128);

        if (forceSingle)
        {
            target.channel = baseMidiChannel;
        }
        else
        {
            target.channel = static_cast<uint8_t>(baseMidiChannel + ((absoluteDmx - 1) / 128));
        }

        if (paramType == LogicalParam::GeyserBlast || paramType == LogicalParam::Strobe)
        {
            target.type = MidiEventType::Note;
        }
        else
        {
            target.type = MidiEventType::ControlChange;
        }

        target.index = ccOrNoteIndex;
        return target;
    }

    void setForceSingleChannel(bool force) { forceSingle = force; }
    bool isSingleChannelForced() const { return forceSingle; }

    void setBaseChannel(uint8_t ch) { baseMidiChannel = ch; }
    uint8_t getBaseChannel() const { return baseMidiChannel; }

private:
    bool forceSingle = true;
    uint8_t baseMidiChannel = 1;
};