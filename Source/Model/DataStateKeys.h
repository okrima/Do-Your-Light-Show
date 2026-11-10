#pragma once
#include <juce_core/juce_core.h>

namespace StateKeys
{
    // Nodi Principali
    const juce::Identifier VenueNode{ "VENUE" };
    const juce::Identifier ShowNode{ "SHOW_TIMELINE" };

    // --- NUOVI NODI PRESET LIBRARY ---
    const juce::Identifier PresetLibNode{ "PRESET_LIBRARY" };
    const juce::Identifier ScenePreset{ "SCENE_PRESET" };

    // Nodi Figli Venue
    const juce::Identifier Fixture{ "FIXTURE" };
    const juce::Identifier FixtureName{ "name" };
    const juce::Identifier StartAddress{ "startAddress" };
    const juce::Identifier ChannelCount{ "channelCount" };
    const juce::Identifier RoleName{ "role" }; // Collante Logico

    inline juce::Identifier getMappingKey(const juce::String& paramName) {
        return juce::Identifier("map_" + paramName);
    }

    // --- NUOVI NODI SHOW TIMELINE ---

// 1. La Scena (Cue) sulla Timeline
    const juce::Identifier Scene{ "SCENE" };
    const juce::Identifier SceneName{ "sceneName" };
    const juce::Identifier StartBeat{ "startBeat" };     // Aggiornato a decimale (quarti)
    const juce::Identifier DurationBeats{ "durationBeats" }; // Aggiornato a decimale (quarti)

    // 2. I Comandi all'interno della Scena
    const juce::Identifier RoleCommand{ "ROLE_COMMAND" };
    const juce::Identifier TargetRole{ "roleName" }; // Deve corrispondere al RoleName della Fixture
    const juce::Identifier TargetParam{ "parameter" };
    const juce::Identifier StartValue{ "startValue" };
    const juce::Identifier EndValue{ "endValue" };
    const juce::Identifier CurveType{ "curve" };
}