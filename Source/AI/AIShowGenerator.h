#pragma once
#include <JuceHeader.h>
#include "../Model/DataStateKeys.h"
#include "../Model/StageRoleCatalog.h"
#include "AudioAnalysisEngine.h"
#include <vector>
#include <set>
#include <cmath>
#include <algorithm>

class AIShowGenerator
{
public:
    struct GenerationStats
    {
        int scenesCreatedOrUpdated = 0;
        int commandsGenerated = 0;
        bool wasIncrementalLayering = false;
    };

    static GenerationStats generateShowFromAnalysis(juce::ValueTree& appState,
        const AudioAnalysisEngine::AnalysisResult& analysis)
    {
        GenerationStats stats;
        if (!appState.isValid() || !analysis.success)
            return stats;

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
        {
            showNode = juce::ValueTree(StateKeys::ShowNode);
            appState.addChild(showNode, -1, nullptr);
        }

        AestheticPalette palette = loadPaletteFromState(appState);
        std::vector<RoleProfile> roles = discoverVenueRoles(appState);

        const bool canLayer = palette.preserveExisting && (showNode.getNumChildren() > 0);
        stats.wasIncrementalLayering = canLayer;

        if (!canLayer)
        {
            // --- MODALITA' COMPLETA: Genera regia realistica con Drop/Impatto e Sviluppo ---
            showNode.removeAllChildren(nullptr);

            for (size_t i = 0; i < analysis.sections.size(); ++i)
            {
                const auto& sec = analysis.sections[i];

                // Controlla se la sezione successiva e' un Climax o HighEnergy (per pre-attivare il Fumo!)
                bool nextIsDrop = false;
                if (i + 1 < analysis.sections.size())
                {
                    auto nextMood = analysis.sections[i + 1].mood;
                    nextIsDrop = (nextMood == AudioAnalysisEngine::SectionMood::Climax ||
                        nextMood == AudioAnalysisEngine::SectionMood::HighEnergy);
                }

                // Se siamo in un Climax o HighEnergy >= 12 quarti, separiamo i primi 4 quarti di IMPATTO
                // (dove sparano Geyser e Blinder) dal resto della sezione (dove lavorano Aria e Terra)
                bool splitDropImpact = (sec.mood == AudioAnalysisEngine::SectionMood::Climax ||
                    sec.mood == AudioAnalysisEngine::SectionMood::HighEnergy) &&
                    (sec.durationBeats >= 12.0);

                if (splitDropImpact)
                {
                    double impactBeats = 4.0;
                    double sustainBeats = sec.durationBeats - impactBeats;

                    // 1. Scena Breve di Impatto (4 quarti: Burst Geyser + Colpo Blinder)
                    AudioAnalysisEngine::SongSection impactSec = sec;
                    impactSec.durationBeats = impactBeats;

                    juce::ValueTree impactScene(StateKeys::Scene);
                    impactScene.setProperty(StateKeys::SceneName, sec.name + " [IMPATTO]", nullptr);
                    impactScene.setProperty(StateKeys::StartBeat, sec.startBeat, nullptr);
                    impactScene.setProperty(StateKeys::DurationBeats, impactBeats, nullptr);

                    for (const auto& role : roles)
                    {
                        stats.commandsGenerated += generateCommandsForRole(
                            impactScene, role, impactSec, palette, true, false);
                    }
                    showNode.addChild(impactScene, -1, nullptr);
                    ++stats.scenesCreatedOrUpdated;

                    // 2. Scena di Sviluppo (Geyser e Blinder spenti, dialogo Aria/Terra)
                    AudioAnalysisEngine::SongSection sustainSec = sec;
                    sustainSec.startBeat = sec.startBeat + impactBeats;
                    sustainSec.durationBeats = sustainBeats;

                    juce::ValueTree sustainScene(StateKeys::Scene);
                    sustainScene.setProperty(StateKeys::SceneName, sec.name, nullptr);
                    sustainScene.setProperty(StateKeys::StartBeat, sustainSec.startBeat, nullptr);
                    sustainScene.setProperty(StateKeys::DurationBeats, sustainSec.durationBeats, nullptr);

                    for (const auto& role : roles)
                    {
                        stats.commandsGenerated += generateCommandsForRole(
                            sustainScene, role, sustainSec, palette, false, nextIsDrop);
                    }
                    showNode.addChild(sustainScene, -1, nullptr);
                    ++stats.scenesCreatedOrUpdated;
                }
                else
                {
                    juce::ValueTree sceneNode(StateKeys::Scene);
                    sceneNode.setProperty(StateKeys::SceneName, sec.name, nullptr);
                    sceneNode.setProperty(StateKeys::StartBeat, sec.startBeat, nullptr);
                    sceneNode.setProperty(StateKeys::DurationBeats, sec.durationBeats, nullptr);

                    for (const auto& role : roles)
                    {
                        stats.commandsGenerated += generateCommandsForRole(
                            sceneNode, role, sec, palette, false, nextIsDrop);
                    }
                    showNode.addChild(sceneNode, -1, nullptr);
                    ++stats.scenesCreatedOrUpdated;
                }
            }
        }
        else
        {
            // --- MODALITA' LAYERING: Aggiunge comandi SOLO per i nuovi fari del locale ---
            const int numScenes = showNode.getNumChildren();
            for (int i = 0; i < numScenes; ++i)
            {
                auto sceneNode = showNode.getChild(i);
                if (!sceneNode.isValid() || !sceneNode.hasType(StateKeys::Scene))
                    continue;

                double startB = static_cast<double>(sceneNode.getProperty(StateKeys::StartBeat, 1.0));
                double durB = static_cast<double>(sceneNode.getProperty(StateKeys::DurationBeats, 16.0));
                juce::String sName = sceneNode.getProperty(StateKeys::SceneName, "Scena").toString();

                std::set<juce::String> existingRoles;
                for (int c = 0; c < sceneNode.getNumChildren(); ++c)
                {
                    auto cmd = sceneNode.getChild(c);
                    if (cmd.hasType(StateKeys::RoleCommand))
                        existingRoles.insert(cmd.getProperty(StateKeys::TargetRole, "").toString());
                }

                AudioAnalysisEngine::SongSection synthSec = evaluateSectionForBeatRange(
                    analysis, startB, durB, sName);

                bool isImpactCue = (durB <= 4.5 && (sName.containsIgnoreCase("IMPATTO") ||
                    synthSec.mood == AudioAnalysisEngine::SectionMood::Climax));
                bool nextIsDrop = (i + 1 < numScenes);

                bool sceneModified = false;
                for (const auto& role : roles)
                {
                    if (existingRoles.find(role.roleName) == existingRoles.end())
                    {
                        stats.commandsGenerated += generateCommandsForRole(
                            sceneNode, role, synthSec, palette, isImpactCue, nextIsDrop);
                        sceneModified = true;
                    }
                }

                if (sceneModified)
                    ++stats.scenesCreatedOrUpdated;
            }
        }

        return stats;
    }

private:
    struct RoleProfile
    {
        juce::String roleName;
        StageRoleCatalog::Category category = StageRoleCatalog::Category::AirWashBack;
        bool hasDimmer = true;
        bool hasRGB = false;
        bool hasStrobe = false;
        bool hasPanTilt = false;
    };

    struct AestheticPalette
    {
        juce::Colour primaryColor{ 0xff1e3a8a }; // Atmosfera
        juce::Colour secondaryColor{ 0xffd97706 }; // Melodia
        juce::Colour accentColor{ 0xffffffff }; // Flash / Impatto
        float strobeIntensity = 0.5f;
        bool preserveExisting = false;
    };

    static AestheticPalette loadPaletteFromState(const juce::ValueTree& appState)
    {
        AestheticPalette p;
        auto node = appState.getChildWithName(StateKeys::AestheticNode);
        if (node.isValid())
        {
            p.primaryColor = juce::Colour::fromString(node.getProperty(StateKeys::PrimaryColor, "ff1e3a8a").toString());
            p.secondaryColor = juce::Colour::fromString(node.getProperty(StateKeys::SecondaryColor, "ffd97706").toString());
            p.accentColor = juce::Colour::fromString(node.getProperty(StateKeys::AccentColor, "ffffffff").toString());
            p.strobeIntensity = static_cast<float>(static_cast<double>(node.getProperty(StateKeys::StrobeIntensity, 0.5)));
            p.preserveExisting = static_cast<bool>(node.getProperty(StateKeys::PreserveExistingRoles, false));
        }
        return p;
    }

    static std::vector<RoleProfile> discoverVenueRoles(const juce::ValueTree& appState)
    {
        std::vector<RoleProfile> result;
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);

        if (venueNode.isValid())
        {
            for (int i = 0; i < venueNode.getNumChildren(); ++i)
            {
                auto fix = venueNode.getChild(i);
                if (!fix.isValid() || !fix.hasType(StateKeys::Fixture))
                    continue;

                juce::String fName = fix.getProperty(StateKeys::FixtureName, "Fixture_" + juce::String(i + 1)).toString();
                juce::String rName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
                if (rName.isEmpty())
                    rName = fName;

                bool hasR = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Red"), 0)) > 0;
                bool hasG = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Green"), 0)) > 0;
                bool hasB = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Blue"), 0)) > 0;
                bool hasStr = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Strobe"), 0)) > 0;
                bool hasPan = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Pan"), 0)) > 0
                    || static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Tilt"), 0)) > 0;

                auto it = std::find_if(result.begin(), result.end(),
                    [&rName](const RoleProfile& rp) { return rp.roleName == rName; });

                if (it == result.end())
                {
                    RoleProfile rp;
                    rp.roleName = rName;
                    rp.category = StageRoleCatalog::classifyRole(rName, fName);
                    rp.hasDimmer = true;
                    rp.hasRGB = (hasR || hasG || hasB) || (rp.category == StageRoleCatalog::Category::FxGeyserBurst);
                    rp.hasStrobe = hasStr || (rp.category == StageRoleCatalog::Category::ImpactStrobe);
                    rp.hasPanTilt = hasPan || (rp.category == StageRoleCatalog::Category::AirMovingBeam ||
                        rp.category == StageRoleCatalog::Category::FloorMovingBeam);
                    result.push_back(rp);
                }
                else
                {
                    it->hasRGB = it->hasRGB || (hasR || hasG || hasB);
                    it->hasStrobe = it->hasStrobe || hasStr;
                    it->hasPanTilt = it->hasPanTilt || hasPan;
                }
            }
        }

        // (Allineato ai DisplayName ufficiali del ComboBox di Tab 1):
        if (result.empty())
        {
            using Cat = StageRoleCatalog::Category;
            result.push_back({ "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack,         true, true,  false, false });
            result.push_back({ "Aria: Par / Spot Frontali (Band)",   Cat::AirParFront,         true, true,  false, false });
            result.push_back({ "Terra: Controluce / Sotto-Batteria", Cat::FloorBackSilhouette, true, true,  false, false });
            result.push_back({ "Impatto: Blinder Pubblico",          Cat::ImpactBlinder,       true, false, false, false });
            result.push_back({ "Impatto: Strobo",                    Cat::ImpactStrobe,        true, false, true,  false });
            result.push_back({ "FX: Macchina del Fumo / Hazer",      Cat::FxSmokeHazer,        true, false, false, false });
            result.push_back({ "FX: Geyser Verticale (Fumo + LED)",  Cat::FxGeyserBurst,       true, true,  false, false });
        }

        return result;
    }

    static void addCommand(juce::ValueTree& sceneNode,
        const juce::String& roleName,
        const juce::String& paramName,
        int startVal,
        int endVal,
        const juce::String& curveName,
        juce::Colour commandCol)
    {
        juce::ValueTree cmd(StateKeys::RoleCommand);
        cmd.setProperty(StateKeys::TargetRole, roleName, nullptr);
        cmd.setProperty(StateKeys::TargetParam, paramName, nullptr);
        cmd.setProperty(StateKeys::StartValue, juce::jlimit(0, 255, startVal), nullptr);
        cmd.setProperty(StateKeys::EndValue, juce::jlimit(0, 255, endVal), nullptr);
        cmd.setProperty(StateKeys::CurveType, curveName, nullptr);
        cmd.setProperty(StateKeys::CommandColor, commandCol.toString(), nullptr);
        sceneNode.addChild(cmd, -1, nullptr);
    }

    static int addRgbCommandsIfMapped(juce::ValueTree& sceneNode,
        const RoleProfile& role,
        juce::Colour startCol,
        juce::Colour endCol,
        const juce::String& curveName)
    {
        if (!role.hasRGB) return 0;

        addCommand(sceneNode, role.roleName, "Red", startCol.getRed(), endCol.getRed(), curveName, endCol);
        addCommand(sceneNode, role.roleName, "Green", startCol.getGreen(), endCol.getGreen(), curveName, endCol);
        addCommand(sceneNode, role.roleName, "Blue", startCol.getBlue(), endCol.getBlue(), curveName, endCol);
        return 3;
    }

    // Regia specifica per ciascuna delle 10 categorie fisiche
    static int generateCommandsForRole(juce::ValueTree& sceneNode,
        const RoleProfile& role,
        const AudioAnalysisEngine::SongSection& sec,
        const AestheticPalette& palette,
        bool isImpactBurstCue,
        bool nextSectionIsDrop)
    {
        using Mood = AudioAnalysisEngine::SectionMood;
        using Cat = StageRoleCatalog::Category;
        int count = 0;

        // Colori complementari Aria vs Terra per dare tridimensionalita' al palco
        juce::Colour airColor = palette.primaryColor;
        juce::Colour floorColor = palette.secondaryColor;

        if (sec.mood == Mood::HighEnergy)
        {
            airColor = palette.primaryColor.interpolatedWith(palette.secondaryColor, 0.65f);
            floorColor = palette.secondaryColor.interpolatedWith(palette.primaryColor, 0.35f);
        }
        else if (sec.mood == Mood::Climax)
        {
            airColor = isImpactBurstCue ? palette.accentColor : palette.secondaryColor;
            floorColor = palette.primaryColor.interpolatedWith(palette.accentColor, 0.50f);
        }
        else if (sec.mood == Mood::Calm || sec.mood == Mood::Outro)
        {
            airColor = palette.primaryColor;
            floorColor = palette.primaryColor.darker(0.3f);
        }

        switch (role.category)
        {
            // 1. ARIA - WASH CONTROLUCE (Tappeto atmosferico continuo)
        case Cat::AirWashBack:
        {
            int sDim = 50, eDim = 100;
            juce::String curve = "Linear";

            if (isImpactBurstCue) { sDim = 255; eDim = 190; curve = "Linear"; }
            else if (sec.mood == Mood::Calm) { sDim = 45;  eDim = static_cast<int>(70.0f + sec.avgEnergy * 60.0f); }
            else if (sec.mood == Mood::BuildUp) { sDim = 70;  eDim = 185; }
            else if (sec.mood == Mood::HighEnergy) { sDim = 110; eDim = 220; curve = "Sine"; }
            else if (sec.mood == Mood::Climax) { sDim = 150; eDim = 255; curve = "Sine"; }
            else { sDim = 90;  eDim = 0; }

            addCommand(sceneNode, role.roleName, "Dimmer", sDim, eDim, curve, airColor);
            ++count;
            count += addRgbCommandsIfMapped(sceneNode, role, palette.primaryColor, airColor, "Linear");
            break;
        }

        // 2. ARIA - PAR / SPOT FRONTALI (Seguono Chitarre/Melodia, bassi nelle intro per effetto silhouette)
        case Cat::AirParFront:
        {
            int sDim = 0, eDim = 0;
            juce::String curve = "Linear";

            if (sec.mood == Mood::Calm)
            {
                // Nelle intro calme restano quasi spenti per esaltare il controluce da terra!
                sDim = 0;
                eDim = static_cast<int>(sec.guitarLevel * 45.0f);
            }
            else if (sec.mood == Mood::BuildUp)
            {
                sDim = 30;
                eDim = static_cast<int>(100.0f + sec.guitarLevel * 110.0f);
            }
            else if (sec.mood == Mood::HighEnergy || sec.mood == Mood::Climax)
            {
                sDim = static_cast<int>(140.0f + sec.guitarLevel * 60.0f);
                eDim = 245;
                curve = "Sine";
            }
            else // Outro
            {
                sDim = 60; eDim = 0;
            }

            addCommand(sceneNode, role.roleName, "Dimmer", sDim, eDim, curve, palette.secondaryColor);
            ++count;
            count += addRgbCommandsIfMapped(sceneNode, role, palette.secondaryColor, airColor, "Linear");
            break;
        }

        // 3. TERRA - CONTROLUCE / SOTTO-BATTERIA (Pulsazione potente su Basso e Cassa dal pavimento)
        case Cat::FloorBackSilhouette:
        {
            int sDim = 60, eDim = 120;
            juce::String curve = "Sine";

            float lowDrive = juce::jmax(sec.bassLevel, sec.kickDensity);
            if (isImpactBurstCue)
            {
                sDim = 255; eDim = 160; curve = "Linear";
            }
            else if (sec.mood == Mood::Calm)
            {
                // Nelle parti calme crea la sagoma dal basso
                sDim = static_cast<int>(50.0f + lowDrive * 70.0f);
                eDim = static_cast<int>(90.0f + lowDrive * 80.0f);
                curve = "Sine";
            }
            else if (sec.mood == Mood::BuildUp)
            {
                sDim = 80;
                eDim = static_cast<int>(170.0f + lowDrive * 85.0f);
                curve = "Linear";
            }
            else if (sec.mood == Mood::HighEnergy || sec.mood == Mood::Climax)
            {
                sDim = 90;
                eDim = 255;
                curve = "Sine"; // Pompa sul ritmo di cassa e basso
            }
            else
            {
                sDim = 100; eDim = 0; curve = "Linear";
            }

            addCommand(sceneNode, role.roleName, "Dimmer", sDim, eDim, curve, floorColor);
            ++count;
            count += addRgbCommandsIfMapped(sceneNode, role, floorColor, palette.secondaryColor, "Linear");
            break;
        }

        // 4. TERRA - TAGLIO LATERALE (Risponde alle aperture di Chitarra in contrasto con l'Aria)
        case Cat::FloorSideWash:
        {
            int sDim = 20, eDim = 80;
            juce::String curve = "Linear";

            if (sec.mood == Mood::Calm) { sDim = 15; eDim = static_cast<int>(30.0f + sec.guitarLevel * 60.0f); }
            else if (sec.mood == Mood::BuildUp) { sDim = 40; eDim = 160; }
            else if (sec.mood == Mood::HighEnergy) { sDim = 100; eDim = 230; curve = "Sine"; }
            else if (sec.mood == Mood::Climax) { sDim = 140; eDim = 255; curve = "Sine"; }
            else { sDim = 50; eDim = 0; }

            addCommand(sceneNode, role.roleName, "Dimmer", sDim, eDim, curve, palette.secondaryColor);
            ++count;
            count += addRgbCommandsIfMapped(sceneNode, role, palette.secondaryColor, floorColor, "Linear");
            break;
        }

        // 5 & 6. TESTE MOBILI ARIA E TERRA (Movimenti Pan/Tilt differenziati)
        case Cat::AirMovingBeam:
        case Cat::FloorMovingBeam:
        {
            bool isFloor = (role.category == Cat::FloorMovingBeam);
            juce::Colour beamCol = isFloor ? floorColor : airColor;

            int sDim = (sec.mood == Mood::Calm || sec.mood == Mood::Outro) ? 35 : 130;
            int eDim = (sec.mood == Mood::Climax) ? 255 : static_cast<int>(100.0f + sec.avgEnergy * 145.0f);
            if (sec.mood == Mood::Outro) eDim = 0;

            addCommand(sceneNode, role.roleName, "Dimmer", sDim, eDim, "Linear", beamCol);
            ++count;

            if (role.hasPanTilt)
            {
                int panStart = isFloor ? 80 : 40;
                int panEnd = (sec.mood == Mood::Climax || sec.mood == Mood::HighEnergy) ? 220 : 125;
                int tiltStart = isFloor ? 140 : 50; // Da terra partono puntate verso l'alto
                int tiltEnd = isFloor ? 240 : 190;

                addCommand(sceneNode, role.roleName, "Pan", panStart, panEnd, "Sine", beamCol);
                addCommand(sceneNode, role.roleName, "Tilt", tiltStart, tiltEnd, "Sine", beamCol);
                count += 2;
            }
            count += addRgbCommandsIfMapped(sceneNode, role, beamCol, palette.accentColor, "Linear");
            break;
        }

        // 7. IMPATTO - BLINDER PUBBLICO (Colpo accecante in decay SOLO sugli attacchi dei Drop!)
        case Cat::ImpactBlinder:
        {
            if (isImpactBurstCue)
            {
                // Botta a 255 che scende morbida a 35 nei 4 quarti del Drop
                addCommand(sceneNode, role.roleName, "Dimmer", 255, 35, "Linear", palette.accentColor);
                ++count;
            }
            else if (sec.mood == Mood::Climax && sec.durationBeats < 12.0)
            {
                addCommand(sceneNode, role.roleName, "Dimmer", 220, 0, "Linear", palette.accentColor);
                ++count;
            }
            else
            {
                // Nel resto della canzone resta spento per non accecare il pubblico continuamente
                addCommand(sceneNode, role.roleName, "Dimmer", 0, 0, "Step", palette.accentColor);
                ++count;
            }
            break;
        }

        // 8. IMPATTO - STROBO (Attivo sui Blast Beat / Climax e rullate intense)
        case Cat::ImpactStrobe:
        {
            float drumDrive = juce::jmax(sec.kickDensity, sec.snareDensity);
            if (sec.mood == Mood::Climax && palette.strobeIntensity > 0.05f && !isImpactBurstCue)
            {
                int peak = static_cast<int>(130.0f + 125.0f * palette.strobeIntensity * drumDrive);
                addCommand(sceneNode, role.roleName, "Dimmer", 0, peak, "Square", palette.accentColor);
                ++count;

                if (role.hasStrobe)
                {
                    int spd = static_cast<int>(150.0f + 100.0f * palette.strobeIntensity);
                    addCommand(sceneNode, role.roleName, "Strobe", spd, spd, "Step", palette.accentColor);
                    ++count;
                }
            }
            else
            {
                addCommand(sceneNode, role.roleName, "Dimmer", 0, 0, "Step", palette.accentColor);
                ++count;
            }
            break;
        }

        // 9. FX - MACCHINA DEL FUMO / HAZER (Logica Predittiva: pre-carica prima del Drop!)
        case Cat::FxSmokeHazer:
        {
            int smokeOut = 0;
            if (sec.mood == Mood::BuildUp || nextSectionIsDrop)
            {
                // Eroga fumo prima che arrivi il Climax cosi' i fasci saranno visibili
                smokeOut = 185;
            }
            else if (sec.mood == Mood::Calm && sec.startBeat <= 8.0)
            {
                // Leggera base di fumo ad inizio brano
                smokeOut = 110;
            }

            addCommand(sceneNode, role.roleName, "Dimmer", smokeOut, 0, "Linear", palette.primaryColor);
            ++count;
            break;
        }

        // 10. FX - GEYSER VERTICALE (Esplosione di 4 quarti SOLO sull'impatto del Drop!)
        case Cat::FxGeyserBurst:
        {
            if (isImpactBurstCue)
            {
                // Spara il getto verticale sul Drop e si chiude subito
                addCommand(sceneNode, role.roleName, "Dimmer", 255, 0, "Linear", palette.accentColor);
                ++count;
                count += addRgbCommandsIfMapped(sceneNode, role, palette.accentColor, palette.secondaryColor, "Linear");
            }
            else
            {
                // Rigorosamente chiuso nel resto del brano
                addCommand(sceneNode, role.roleName, "Dimmer", 0, 0, "Step", palette.accentColor);
                ++count;
            }
            break;
        }
        }

        return count;
    }

    static AudioAnalysisEngine::SongSection evaluateSectionForBeatRange(
        const AudioAnalysisEngine::AnalysisResult& analysis,
        double startBeat,
        double durationBeats,
        const juce::String& name)
    {
        AudioAnalysisEngine::SongSection sec;
        sec.name = name;
        sec.startBeat = startBeat;
        sec.durationBeats = durationBeats;

        if (analysis.beats.empty())
            return sec;

        int count = 0;
        double endBeat = startBeat + durationBeats;
        for (const auto& bf : analysis.beats)
        {
            if (bf.beat >= startBeat && bf.beat < endBeat)
            {
                sec.avgEnergy += bf.fullMixRms;
                sec.avgBrightness += bf.brightness;
                sec.kickDensity += bf.kickEnergy;
                sec.snareDensity += bf.snareEnergy;
                sec.bassLevel += bf.bassEnergy;
                sec.guitarLevel += bf.guitarEnergy;
                ++count;
            }
        }

        if (count > 0)
        {
            float inv = 1.0f / static_cast<float>(count);
            sec.avgEnergy *= inv;
            sec.avgBrightness *= inv;
            sec.kickDensity *= inv;
            sec.snareDensity *= inv;
            sec.bassLevel *= inv;
            sec.guitarLevel *= inv;
        }

        if (sec.avgEnergy < 0.38f)
            sec.mood = AudioAnalysisEngine::SectionMood::Calm;
        else if (sec.avgEnergy >= 0.78f && (sec.kickDensity > 0.65f || sec.snareDensity > 0.65f))
            sec.mood = AudioAnalysisEngine::SectionMood::Climax;
        else if (sec.avgEnergy >= 0.58f)
            sec.mood = AudioAnalysisEngine::SectionMood::HighEnergy;
        else
            sec.mood = AudioAnalysisEngine::SectionMood::BuildUp;

        return sec;
    }
};