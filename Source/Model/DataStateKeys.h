#pragma once
#include <juce_core/juce_core.h>

namespace StateKeys
{
    // Nodi Principali
    const juce::Identifier VenueNode{ "VENUE" };
    const juce::Identifier ShowNode{ "SHOW_TIMELINE" };
    const juce::Identifier ShowPackageNode{ "SHOW_PACKAGE" };

    // --- NODI PRESET LIBRARY ---
    const juce::Identifier PresetLibNode{ "PRESET_LIBRARY" };
    const juce::Identifier ScenePreset{ "SCENE_PRESET" };
    const juce::Identifier PresetName{ "presetName" };
    const juce::Identifier PresetId{ "presetId" };
    const juce::Identifier PresetAliases{ "presetAliases" };
    const juce::Identifier PresetPool{ "presetPool" };       // "Color" | "Dimmer" | "Strobe" | "Movement" | "ImpactFX"
    const juce::Identifier LayerType{ "layerType" };         // "Base" | "Movement" | "OverlayFX"
    const juce::Identifier IsOneShot{ "isOneShot" };         // bool (true = Blinder/Geyser one-shot)
    const juce::Identifier PhaseMode{ "phaseMode" };         // "Sync" | "OddEven" | "LeftToRight" | "SymmetricalFan"
    const juce::Identifier TargetRolesCsv{ "targetRolesCsv" };

    // Metadati Editor del Preset (per ricaricare i controlli UI 1:1)
    const juce::Identifier PresetColor1{ "presetColor1" };
    const juce::Identifier PresetColor2{ "presetColor2" };
    const juce::Identifier PresetDimmerMin{ "presetDimmerMin" };
    const juce::Identifier PresetDimmerMax{ "presetDimmerMax" };
    const juce::Identifier PresetCurve{ "presetCurve" };
    const juce::Identifier PresetStrobeSubdiv{ "presetStrobeSubdiv" };       // "1/8" | "1/16"
    const juce::Identifier PresetStrobeUseHardware{ "presetStrobeUseHw" };   // bool
    const juce::Identifier PresetMoveShape{ "presetMoveShape" };             // "Fixed" | "PanSweep" | "TiltWave" | "Circle" | "Figure8"
    const juce::Identifier PresetMoveSpeed{ "presetMoveSpeed" };             // "Sine_4Bars" | "Sine_2Bars" | "Sine_1Bar"
    const juce::Identifier PresetMoveAmpPan{ "presetMoveAmpPan" };           // 0..100 %
    const juce::Identifier PresetMoveAmpTilt{ "presetMoveAmpTilt" };         // 0..100 %
    const juce::Identifier PresetMovePanCenter{ "presetMovePanCenter" };
    const juce::Identifier PresetMoveTiltCenter{ "presetMoveTiltCenter" };
    const juce::Identifier PresetMovePanFan{ "presetMovePanFan" };
    const juce::Identifier PresetMoveTiltFan{ "presetMoveTiltFan" };
    const juce::Identifier PresetUseColor{ "presetUseColor" };
    const juce::Identifier PresetUseDimmer{ "presetUseDimmer" };
    const juce::Identifier PresetUseMove{ "presetUseMove" };
    const juce::Identifier PresetUseStrobe{ "presetUseStrobe" };

    // --- NODI RADICE ---
    const juce::Identifier AudioStemsNode{ "AUDIO_STEMS" };
    const juce::Identifier AestheticNode{ "AESTHETIC_CONFIG" };
    const juce::Identifier BeatMapNode{ "BEAT_MAP" };

    // --- PROPRIETA' AUDIO_STEMS ---
    const juce::Identifier StemFullMix{ "fullMixPath" };
    const juce::Identifier StemFullMixOffset{ "fullMixOffsetSec" };
    const juce::Identifier StemClick{ "clickPath" };
    const juce::Identifier StemDrums{ "drumsPath" };
    const juce::Identifier StemBass{ "bassPath" };
    const juce::Identifier StemGuitars{ "guitarsPath" };
    const juce::Identifier StemFullMixVol{ "fullMixVol" };
    const juce::Identifier StemClickVol{ "clickVol" };
    const juce::Identifier StemDrumsVol{ "drumsVol" };
    const juce::Identifier StemBassVol{ "bassVol" };
    const juce::Identifier StemGuitarsVol{ "guitarsVol" };

    // --- PROPRIETA' AESTHETIC_CONFIG ---
    const juce::Identifier PrimaryColor{ "primaryColor" };
    const juce::Identifier SecondaryColor{ "secondaryColor" };
    const juce::Identifier AccentColor{ "accentColor" };
    const juce::Identifier MoodProfile{ "moodProfile" };
    const juce::Identifier StrobeIntensity{ "strobeIntensity" };
    const juce::Identifier PreserveExistingRoles{ "preserveExistingRoles" };

    // --- PROPRIETA' E FIGLI BEAT_MAP ---
    const juce::Identifier BpmBase{ "bpmBase" };
    const juce::Identifier FirstDownbeatSec{ "firstDownbeatSec" };
    const juce::Identifier BeatPointNode{ "BEAT_POINT" };
    const juce::Identifier PointBeat{ "beat" };
    const juce::Identifier PointTimeSec{ "timeSec" };
    const juce::Identifier PointBpm{ "localBpm" };

    // --- PROPRIETA' COLORE SUI COMANDI LUCI ---
    const juce::Identifier CommandColor{ "commandColor" };

    // --- NODI E PROPRIETA' VENUE & FIXTURE ---
    const juce::Identifier Fixture{ "FIXTURE" };
    const juce::Identifier FixtureName{ "name" };
    const juce::Identifier StartAddress{ "startAddress" };
    const juce::Identifier ChannelCount{ "channelCount" };
    const juce::Identifier RoleName{ "role" };

    // Quantita' e Modalita' Patch DMX
    const juce::Identifier FixtureQuantity{ "quantity" };         // Int: numero di fari di questo tipo (1..32)

    // Calibrazione Strobo Hardware & Frequenza (Hz)
    // strobeMode: 0 = Nessuno (Solo Dimmer 0/255), 1 = Canale Separato, 2 = Condiviso con Dimmer
    const juce::Identifier StrobeMode{ "strobeMode" };
    const juce::Identifier DimmerMaxVal{ "dimmerMaxVal" };           // Per canale condiviso: limite max dimmer (es. 134)
    const juce::Identifier StrobeOpenVal{ "strobeOpenVal" };         // Valore DMX a strobo spento / shutter aperto (es. 0 o 255)
    const juce::Identifier StrobeMinVal{ "strobeMinVal" };           // Valore DMX inizio strobo lento (es. 16 o 135)
    const juce::Identifier StrobeMinHz{ "strobeMinHz" };             // Frequenza reale in Hz a StrobeMinVal (es. 1.0)
    const juce::Identifier StrobeMaxVal{ "strobeMaxVal" };           // Valore DMX fine strobo veloce (es. 245 o 239)
    const juce::Identifier StrobeMaxHz{ "strobeMaxHz" };             // Frequenza reale in Hz a StrobeMaxVal (es. 20.0)
    const juce::Identifier AllowDimmerStrobe{ "allowDimmerStrobe" }; // Bool: consente anche flash 0/255 sul Dimmer

    // Calibrazione Posizione a Riposo (Home), Offset e Range Pan/Tilt
    const juce::Identifier PanHome{ "panHome" };       // 0..255 (Default 128 = centro)
    const juce::Identifier TiltHome{ "tiltHome" };     // 0..255 (Default 90 Aria, 170 Terra)
    const juce::Identifier PanOffset{ "panOffset" };   // -127..+127 (Default 0)
    const juce::Identifier TiltOffset{ "tiltOffset" }; // -127..+127 (Default 0)
    const juce::Identifier PanRange{ "panRange" };     // 10..255 (Ampiezza max movimento Pan, default 120)
    const juce::Identifier TiltRange{ "tiltRange" };   // 10..255 (Ampiezza max movimento Tilt, default 90)
    const juce::Identifier PanTiltProfile{ "ptProfile" }; // 0=540/270, 1=540/180, 2=630/270, 3=SoloTilt 180
    const juce::Identifier MountMode{ "mountMode" };      // 0=Americana Fronte, 1=Americana Retro, 2=Terra Fronte, 3=Terra Retro
    const juce::Identifier PanMin{ "panMin" };            // 0..255
    const juce::Identifier PanMax{ "panMax" };            // 0..255
    const juce::Identifier TiltMin{ "tiltMin" };          // 0..255
    const juce::Identifier TiltMax{ "tiltMax" };          // 0..255
    const juce::Identifier SwapPanTilt{ "swapPanTilt" }; // bool: montaggio verticale/totem (scambia Pan <-> Tilt)
    const juce::Identifier InvertPan{ "invertPan" };      // bool
    const juce::Identifier InvertTilt{ "invertTilt" };    // bool

    // Singola Unita' Fisica (nel Light Case o piazzata sul Palco 2D)
    const juce::Identifier FixtureUnit{ "FIXTURE_UNIT" };
    const juce::Identifier UnitIndex{ "unitIndex" };   // 1..N
    const juce::Identifier IsPlaced{ "isPlaced" };     // false = nel Light Case, true = sul Palco
    const juce::Identifier PosX{ "posX" };             // Double 0.0 -> 1.0 (coordinata X normalizzata palco)
    const juce::Identifier PosY{ "posY" };             // Double 0.0 -> 1.0 (coordinata Y normalizzata palco)

    inline juce::Identifier getMappingKey(const juce::String& paramName) {
        return juce::Identifier("map_" + paramName);
    }

    // --- NODI SHOW TIMELINE ---
    const juce::Identifier Scene{ "SCENE" };
    const juce::Identifier SceneName{ "sceneName" };
    const juce::Identifier StartBeat{ "startBeat" };
    const juce::Identifier DurationBeats{ "durationBeats" };

    const juce::Identifier RoleCommand{ "ROLE_COMMAND" };
    const juce::Identifier TargetRole{ "roleName" };
    const juce::Identifier TargetParam{ "parameter" };
    const juce::Identifier StartValue{ "startValue" };
    const juce::Identifier EndValue{ "endValue" };
    const juce::Identifier CurveType{ "curve" };

    // --- PROPRIETA' E FIGLI BEAT_MAP ---:
    const juce::Identifier MeterPointNode{ "METER_POINT" };
    const juce::Identifier TimeSigNum{ "timeSigNum" }; // es. 4, 3, 6, 7, 12
    const juce::Identifier TimeSigDen{ "timeSigDen" }; // es. 4, 8, 16
    const juce::Identifier MetronomeEnabled{ "metronomeEnabled" };
    const juce::Identifier MetronomeGain{ "metronomeGain" };

    struct MusicalPosition
    {
        double totalQuarters = 1.0;       // Posizione assoluta in Q (per MIDI e Timeline)
        int    barIndex = 1;              // Numero battuta reale (1-based)
        double beatInBarQ = 0.0;          // Quarti trascorsi dall'ultimo battere (0.0 .. barLengthQ)
        double barLengthQ = 4.0;          // Lunghezza della misura corrente in Q
        int    timeSigNum = 4;
        int    timeSigDen = 4;
        double continuousBarPos = 0.0;    // (barIndex - 1) + (beatInBarQ / barLengthQ)
    };
}