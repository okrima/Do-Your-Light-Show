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

    // --- NUOVI NODI RADICE (EPIC 1) ---
    const juce::Identifier AudioStemsNode{ "AUDIO_STEMS" };
    const juce::Identifier AestheticNode{ "AESTHETIC_CONFIG" };
    const juce::Identifier BeatMapNode{ "BEAT_MAP" };

    // --- PROPRIETA' AUDIO_STEMS (Percorsi File Audio Categorizzati) ---
    const juce::Identifier StemFullMix{ "fullMixPath" };   // Obbligatorio per analisi macro e waveform
    const juce::Identifier StemClick{ "clickPath" };     // Opzionale: estrazione griglia BPM
    const juce::Identifier StemDrums{ "drumsPath" };     // Opzionale: transienti cassa/rullante
    const juce::Identifier StemBass{ "bassPath" };      // Opzionale: inviluppo basse frequenze
    const juce::Identifier StemGuitars{ "guitarsPath" };   // Opzionale: apertura melodica e centroide

    // --- PROPRIETA' AESTHETIC_CONFIG ("Quali colori vuoi?" & Layering) ---
    const juce::Identifier PrimaryColor{ "primaryColor" };          // Hex ARGB es. "ff1e3a8a" (Atmosfera)
    const juce::Identifier SecondaryColor{ "secondaryColor" };        // Hex ARGB es. "ffd97706" (Melodia/Apertura)
    const juce::Identifier AccentColor{ "accentColor" };           // Hex ARGB es. "ffffffff" (Impatto/Flash)
    const juce::Identifier MoodProfile{ "moodProfile" };           // Nome preset es. "IceAndFog", "FireAndCopper"
    const juce::Identifier StrobeIntensity{ "strobeIntensity" };       // Double 0.0 -> 1.0
    const juce::Identifier PreserveExistingRoles{ "preserveExistingRoles" }; // Bool: true = genera solo per nuovi fari

    // --- PROPRIETA' E FIGLI BEAT_MAP (Conversione Secondi <-> Quarti) ---
    const juce::Identifier BpmBase{ "bpmBase" };               // Double: BPM base (default 120.0)
    const juce::Identifier FirstDownbeatSec{ "firstDownbeatSec" };      // Double: offset in secondi del Beat 1.0
    const juce::Identifier BeatPointNode{ "BEAT_POINT" };            // Nodo figlio per griglia dinamica da Click
    const juce::Identifier PointBeat{ "beat" };                  // Double: quarto (es. 1.0, 2.0, 3.0)
    const juce::Identifier PointTimeSec{ "timeSec" };               // Double: posizione in secondi nell'audio
    const juce::Identifier PointBpm{ "localBpm" };              // Double: BPM istantaneo in quel punto

    // --- PROPRIETA' COLORE SUI COMANDI LUCI (Per Simulatore 2D e DMX) ---
    const juce::Identifier CommandColor{ "commandColor" };          // Hex ARGB assegnato al singolo comando

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