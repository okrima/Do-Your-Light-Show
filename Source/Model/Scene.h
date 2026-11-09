#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include "../Engine/LfoGenerator.h"
#include "../Engine/ColorGenerator.h"
#include "../Engine/TriggerGenerator.h"

struct FixtureInstruction
{
    std::optional<LfoGenerator> panAnim;
    std::optional<LfoGenerator> tiltAnim;
    std::optional<LfoGenerator> dimmerAnim;

    std::optional<ColorGenerator> colorAnim;

    std::optional<TriggerGenerator> geyserAnim;
    std::optional<TriggerGenerator> strobeAnim;
};

class Scene
{
public:
    Scene(std::string sceneName = "Default Scene", int durationInBars = 4)
        : name(std::move(sceneName)), durationBars(durationInBars) {}

    void setFixtureInstruction(const std::string& fixtureName, const FixtureInstruction& instr)
    {
        instructions[fixtureName] = instr;
    }

    const FixtureInstruction* getInstructionFor(const std::string& fixtureName) const
    {
        auto it = instructions.find(fixtureName);
        if (it != instructions.end())
        {
            return &(it->second);
        }
        return nullptr;
    }

    std::string getName() const { return name; }
    void setName(const std::string& newName) { name = newName; }

    int getDurationBars() const { return durationBars; }
    void setDurationBars(int bars) { durationBars = bars; }

    const std::unordered_map<std::string, FixtureInstruction>& getAllInstructions() const { return instructions; }

private:
    std::string name;
    int durationBars;
    std::unordered_map<std::string, FixtureInstruction> instructions;
};