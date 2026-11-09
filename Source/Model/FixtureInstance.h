#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include "FixtureProfile.h"

class FixtureInstance
{
public:
    FixtureInstance() = default;

    FixtureInstance(std::string name, const FixtureProfile* profile, uint16_t startAddr)
        : customName(std::move(name)), profileTemplate(profile), startAddress(startAddr) {}

    // Ritorna l'indirizzo DMX assoluto (1-512) per il parametro richiesto
    std::optional<uint16_t> getAbsoluteDmxAddress(LogicalParam param) const
    {
        if (!profileTemplate) return std::nullopt;

        auto it = profileTemplate->channelOffsets.find(param);
        if (it != profileTemplate->channelOffsets.end())
        {
            return startAddress + it->second - 1;
        }
        return std::nullopt;
    }

    std::string getName() const { return customName; }
    void setName(const std::string& name) { customName = name; }

    uint16_t getStartAddress() const { return startAddress; }
    void setStartAddress(uint16_t addr) { startAddress = addr; }

    const FixtureProfile* getProfile() const { return profileTemplate; }
    void setProfile(const FixtureProfile* profile) { profileTemplate = profile; }

private:
    std::string customName;
    const FixtureProfile* profileTemplate = nullptr;
    uint16_t startAddress = 1;
};