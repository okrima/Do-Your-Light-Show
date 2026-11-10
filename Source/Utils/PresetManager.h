#pragma once
#include <juce_core/juce_core.h>
#include "../../Model/DataStateKeys.h"

class PresetManager
{
public:
    // =========================================================================
    // SALVATAGGIO
    // =========================================================================
    static bool saveVenue(const juce::ValueTree& appState, const juce::File& file)
    {
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        return saveValueTreeToJson(venueNode, file);
    }

    static bool saveShow(const juce::ValueTree& appState, const juce::File& file)
    {
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        return saveValueTreeToJson(showNode, file);
    }

    // =========================================================================
    // CARICAMENTO
    // =========================================================================
    static bool loadVenue(juce::ValueTree& appState, const juce::File& file)
    {
        juce::ValueTree loadedNode = loadValueTreeFromJson(file);
        if (loadedNode.isValid() && loadedNode.hasType(StateKeys::VenueNode))
        {
            auto existingNode = appState.getChildWithName(StateKeys::VenueNode);
            updateNodePreservingListeners(existingNode, loadedNode);
            return true;
        }
        return false;
    }

    static bool loadShow(juce::ValueTree& appState, const juce::File& file)
    {
        juce::ValueTree loadedNode = loadValueTreeFromJson(file);
        if (loadedNode.isValid() && loadedNode.hasType(StateKeys::ShowNode))
        {
            auto existingNode = appState.getChildWithName(StateKeys::ShowNode);
            updateNodePreservingListeners(existingNode, loadedNode);
            return true;
        }
        return false;
    }

private:
    // =========================================================================
    // CORE LOGIC: ValueTree <-> JSON
    // =========================================================================

    // Aggiorna un nodo senza rimpiazzarlo in memoria per mantenere vivi i listener della GUI
    static void updateNodePreservingListeners(juce::ValueTree& targetNode, const juce::ValueTree& sourceNode)
    {
        targetNode.removeAllChildren(nullptr);
        targetNode.removeAllProperties(nullptr);

        for (int i = 0; i < sourceNode.getNumProperties(); ++i)
        {
            auto name = sourceNode.getPropertyName(i);
            targetNode.setProperty(name, sourceNode.getProperty(name), nullptr);
        }

        for (auto child : sourceNode)
            targetNode.addChild(child.createCopy(), -1, nullptr);
    }

    // --- Serializzazione (Salvataggio in JSON) ---
    static bool saveValueTreeToJson(const juce::ValueTree& vt, const juce::File& file)
    {
        if (!vt.isValid()) return false;

        juce::var jsonVar = valueTreeToVar(vt);
        juce::String jsonString = juce::JSON::toString(jsonVar);

        return file.replaceWithText(jsonString);
    }

    static juce::var valueTreeToVar(const juce::ValueTree& vt)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("nodeType", vt.getType().toString());

        for (int i = 0; i < vt.getNumProperties(); ++i)
        {
            auto name = vt.getPropertyName(i);
            obj->setProperty(name.toString(), vt.getProperty(name));
        }

        if (vt.getNumChildren() > 0)
        {
            juce::Array<juce::var> children;
            for (auto child : vt)
                children.add(valueTreeToVar(child));
            obj->setProperty("children", children);
        }

        return juce::var(obj);
    }

    // --- Deserializzazione (Caricamento da JSON) ---
    static juce::ValueTree loadValueTreeFromJson(const juce::File& file)
    {
        if (!file.existsAsFile()) return {};

        auto parsedJson = juce::JSON::parse(file);
        if (parsedJson.isVoid()) return {};

        return varToValueTree(parsedJson);
    }

    static juce::ValueTree varToValueTree(const juce::var& v)
    {
        if (auto* obj = v.getDynamicObject())
        {
            juce::ValueTree vt(juce::Identifier(obj->getProperty("nodeType").toString()));

            auto& props = obj->getProperties();
            for (auto& p : props)
            {
                // MSVC Fix: estraiamo esplicitamente la stringa per rimuovere l'ambiguità
                if (p.name.toString() == "nodeType" || p.name.toString() == "children") continue;

                vt.setProperty(p.name, p.value, nullptr);
            }

            if (obj->hasProperty("children"))
            {
                auto* children = obj->getProperty("children").getArray();
                if (children != nullptr)
                {
                    for (auto& c : *children)
                        vt.addChild(varToValueTree(c), -1, nullptr);
                }
            }
            return vt;
        }
        return juce::ValueTree();
    }
};