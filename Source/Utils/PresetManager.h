#pragma once
#include <juce_core/juce_core.h>
#include "../../Model/DataStateKeys.h"

class PresetManager
{
public:
    // =========================================================================
    // SALVATAGGIO (JSON ricorsivo: include VENUE -> FIXTURE -> FIXTURE_UNIT)
    // =========================================================================
    static bool saveVenue(const juce::ValueTree& appState, const juce::File& file)
    {
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);
        if (!venueNode.isValid())
            return false;

        return saveValueTreeToJson(venueNode, file);
    }

    static bool saveShow(const juce::ValueTree& appState, const juce::File& file)
    {
        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
            return false;

        juce::ValueTree package(StateKeys::ShowPackageNode);
        package.addChild(showNode.createCopy(), -1, nullptr);

        if (auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode); stemsNode.isValid())
            package.addChild(stemsNode.createCopy(), -1, nullptr);

        if (auto beatMapNode = appState.getChildWithName(StateKeys::BeatMapNode); beatMapNode.isValid())
            package.addChild(beatMapNode.createCopy(), -1, nullptr);

        if (auto aestheticNode = appState.getChildWithName(StateKeys::AestheticNode); aestheticNode.isValid())
            package.addChild(aestheticNode.createCopy(), -1, nullptr);

        return saveValueTreeToJson(package, file);
    }

    // =========================================================================
    // CARICAMENTO (Supporta JSON + fallback automatico XML per .venue)
    // =========================================================================
    static bool loadVenue(juce::ValueTree& appState, const juce::File& file)
    {
        if (!file.existsAsFile())
            return false;

        juce::ValueTree loadedNode = loadValueTreeFromJson(file);

        // Fallback nel caso il file .venue fosse stato salvato in XML
        if (!loadedNode.isValid())
        {
            if (auto xml = juce::XmlDocument::parse(file))
                loadedNode = juce::ValueTree::fromXml(*xml);
        }

        if (loadedNode.isValid() && loadedNode.hasType(StateKeys::VenueNode))
        {
            auto existingNode = appState.getChildWithName(StateKeys::VenueNode);
            if (!existingNode.isValid())
            {
                existingNode = juce::ValueTree(StateKeys::VenueNode);
                appState.addChild(existingNode, -1, nullptr);
            }

            updateNodePreservingListeners(existingNode, loadedNode);
            return true;
        }
        return false;
    }

    static bool loadShow(juce::ValueTree& appState, const juce::File& file)
    {
        if (!file.existsAsFile())
            return false;

        juce::ValueTree loadedNode = loadValueTreeFromJson(file);
        if (!loadedNode.isValid())
            return false;

        auto applyChildNode = [&](const juce::Identifier& nodeType, const juce::ValueTree& src)
            {
                if (!src.isValid())
                    return;

                auto dst = appState.getChildWithName(nodeType);
                if (!dst.isValid())
                {
                    dst = juce::ValueTree(nodeType);
                    appState.addChild(dst, -1, nullptr);
                }
                updateNodePreservingListeners(dst, src);
            };

        // Nuovo formato completo (Show + Tracce Audio + Tempo/Metrica + Impostazioni AI)
        if (loadedNode.hasType(StateKeys::ShowPackageNode))
        {
            applyChildNode(StateKeys::ShowNode, loadedNode.getChildWithName(StateKeys::ShowNode));
            applyChildNode(StateKeys::AudioStemsNode, loadedNode.getChildWithName(StateKeys::AudioStemsNode));
            applyChildNode(StateKeys::BeatMapNode, loadedNode.getChildWithName(StateKeys::BeatMapNode));
            applyChildNode(StateKeys::AestheticNode, loadedNode.getChildWithName(StateKeys::AestheticNode));
            return true;
        }

        // Fallback retrocompatibile per vecchi file .show contenenti solo SHOW_TIMELINE
        if (loadedNode.hasType(StateKeys::ShowNode))
        {
            applyChildNode(StateKeys::ShowNode, loadedNode);
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
                if (p.name.toString() == "nodeType" || p.name.toString() == "children")
                    continue;

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