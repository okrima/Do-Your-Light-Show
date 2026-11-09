#pragma once
#include <juce_core/juce_core.h>
#include <fstream>
#include <vector>
#include "../Model/VenuePatch.h"
#include "../Model/Scene.h"

class PresetManager
{
public:
    // =========================================================================
    // EXPORT: C++ -> juce::var -> File .json
    // =========================================================================
    static bool saveShowToFile(const juce::File& destinationFile, 
                               const std::string& venueName,
                               const VenuePatch& patch, 
                               const std::vector<Scene>& scenes)
    {
        auto rootObj = new juce::DynamicObject();
        rootObj->setProperty("version", 1);
        rootObj->setProperty("appName", "DoYourLightShow");

        // 1. Serializzazione del Venue Profile (Patch)
        rootObj->setProperty("venue", serializeVenue(venueName, patch));

        // 2. Serializzazione del pool di Scene
        juce::Array<juce::var> scenesArray;
        for (const auto& scene : scenes)
        {
            scenesArray.add(serializeScene(scene));
        }
        rootObj->setProperty("scenes", scenesArray);

        // 3. Conversione in stringa JSON e scrittura
        juce::var rootVar(rootObj);
        juce::String jsonString = juce::JSON::toString(rootVar, false, 2); // 2 spazi di indentazione

        return destinationFile.replaceWithText(jsonString);
    }

    // =========================================================================
    // IMPORT: File .json -> juce::var -> Parsing C++
    // =========================================================================
    static bool loadShowFromFile(const juce::File& sourceFile,
                                 std::string& outVenueName,
                                 VenuePatch& outPatch,
                                 std::vector<Scene>& outScenes)
    {
        if (!sourceFile.existsAsFile()) return false;

        juce::var parsedJson = juce::JSON::parse(sourceFile);
        if (!parsedJson.isObject()) return false;

        auto* rootObj = parsedJson.getDynamicObject();
        if (!rootObj) return false;

        outPatch.clear();
        outScenes.clear();

        // 1. Parsing Venue
        if (rootObj->hasProperty("venue"))
        {
            auto venueVar = rootObj->getProperty("venue");
            deserializeVenue(venueVar, outVenueName, outPatch);
        }

        // 2. Parsing Scene
        if (rootObj->hasProperty("scenes"))
        {
            if (auto* scenesArray = rootObj->getProperty("scenes").getArray())
            {
                for (const auto& sceneVar : *scenesArray)
                {
                    outScenes.push_back(deserializeScene(sceneVar));
                }
            }
        }

        return true;
    }

private:
    // --- HELPER SERIALIZZAZIONE ---
    static juce::var serializeVenue(const std::string& venueName, const VenuePatch& patch)
    {
        auto venueObj = new juce::DynamicObject();
        venueObj->setProperty("name", juce::String(venueName));

        juce::Array<juce::var> fixturesArray;
        for (const auto& fixture : patch.getFixtures())
        {
            auto fixObj = new juce::DynamicObject();
            fixObj->setProperty("name", juce::String(fixture.getName()));
            fixObj->setProperty("startAddress", fixture.getStartAddress());
            
            if (fixture.getProfile() != nullptr)
            {
                fixObj->setProperty("profileId", juce::String(fixture.getProfile()->id));
            }

            fixturesArray.add(juce::var(fixObj));
        }

        venueObj->setProperty("fixtures", fixturesArray);
        return juce::var(venueObj);
    }

    static juce::var serializeScene(const Scene& scene)
    {
        auto sceneObj = new juce::DynamicObject();
        sceneObj->setProperty("name", juce::String(scene.getName()));
        sceneObj->setProperty("durationBars", scene.getDurationBars());

        auto instructionsObj = new juce::DynamicObject();
        
        for (const auto& pair : scene.getAllInstructions())
        {
            const std::string& fixtureName = pair.first;
            const FixtureInstruction& instr = pair.second;
            
            auto fixInstrObj = new juce::DynamicObject();
            
            // Pan
            if (instr.panAnim)
            {
                auto panObj = new juce::DynamicObject();
                panObj->setProperty("active", true);
                // In una versione completa qui salveremmo shape, rate, min, max ecc.
                fixInstrObj->setProperty("pan", juce::var(panObj));
            }
            // Aggiungere qui la serializzazione di tiltAnim, colorAnim, geyserAnim...

            instructionsObj->setProperty(juce::String(fixtureName), juce::var(fixInstrObj));
        }

        sceneObj->setProperty("instructions", juce::var(instructionsObj));
        return juce::var(sceneObj);
    }

    // --- HELPER DESERIALIZZAZIONE ---
    static void deserializeVenue(const juce::var& venueVar, std::string& outVenueName, VenuePatch& outPatch)
    {
        auto* obj = venueVar.getDynamicObject();
        if (!obj) return;

        outVenueName = obj->getProperty("name").toString().toStdString();
        
        if (auto* fixturesArray = obj->getProperty("fixtures").getArray())
        {
            for (const auto& fixVar : *fixturesArray)
            {
                auto* fixObj = fixVar.getDynamicObject();
                if (!fixObj) continue;

                std::string name = fixObj->getProperty("name").toString().toStdString();
                uint16_t startAddr = static_cast<uint16_t>(static_cast<int>(fixObj->getProperty("startAddress")));
                
                // NOTA: In un'app reale, qui useremmo il "profileId" letto dal JSON 
                // per recuperare il FixtureProfile* da un ProfileManager/Database globale.
                // Per ora passiamo nullptr.
                outPatch.addFixture(FixtureInstance(name, nullptr, startAddr));
            }
        }
    }

    static Scene deserializeScene(const juce::var& sceneVar)
    {
        auto* obj = sceneVar.getDynamicObject();
        if (!obj) return Scene("Default", 4);

        std::string name = obj->getProperty("name").toString().toStdString();
        int bars = static_cast<int>(obj->getProperty("durationBars"));

        Scene loadedScene(name, bars);
        
        // Deserializzazione delle istruzioni LFO/Trigger/Color...
        // ... (Parsing dell'oggetto "instructions") ...

        return loadedScene;
    }
};