#pragma once
#include <JuceHeader.h>
#include "DataStateKeys.h"

class ShowArchiveManager
{
public:
    // Assicura che tutti i nodi radice dell'app esistano e abbiano proprieta' valide
    static void ensureValidAppStateStructure(juce::ValueTree& appState)
    {
        if (!appState.isValid()) return;

        if (!appState.getChildWithName(StateKeys::VenueNode).isValid())
            appState.addChild(juce::ValueTree(StateKeys::VenueNode), -1, nullptr);

        if (!appState.getChildWithName(StateKeys::ShowNode).isValid())
            appState.addChild(juce::ValueTree(StateKeys::ShowNode), -1, nullptr);

        if (!appState.getChildWithName(StateKeys::PresetLibNode).isValid())
            appState.addChild(juce::ValueTree(StateKeys::PresetLibNode), -1, nullptr);

        // Inizializzazione sicura nodo AUDIO_STEMS
        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        if (!stemsNode.isValid())
        {
            stemsNode = juce::ValueTree(StateKeys::AudioStemsNode);
            stemsNode.setProperty(StateKeys::StemFullMix, "", nullptr);
            stemsNode.setProperty(StateKeys::StemClick, "", nullptr);
            stemsNode.setProperty(StateKeys::StemDrums, "", nullptr);
            stemsNode.setProperty(StateKeys::StemBass, "", nullptr);
            stemsNode.setProperty(StateKeys::StemGuitars, "", nullptr);
            appState.addChild(stemsNode, -1, nullptr);
        }

        // Inizializzazione sicura nodo AESTHETIC_CONFIG ("Quali colori vuoi?")
        auto aestheticNode = appState.getChildWithName(StateKeys::AestheticNode);
        if (!aestheticNode.isValid())
        {
            aestheticNode = juce::ValueTree(StateKeys::AestheticNode);
            aestheticNode.setProperty(StateKeys::PrimaryColor, "ff1e3a8a", nullptr);   // Blu Notte/Ghiaccio
            aestheticNode.setProperty(StateKeys::SecondaryColor, "ffd97706", nullptr); // Ambra/Oro
            aestheticNode.setProperty(StateKeys::AccentColor, "ffffffff", nullptr);    // Bianco Puro
            aestheticNode.setProperty(StateKeys::MoodProfile, "IceAndFog", nullptr);
            aestheticNode.setProperty(StateKeys::StrobeIntensity, 0.5, nullptr);
            aestheticNode.setProperty(StateKeys::PreserveExistingRoles, false, nullptr);
            appState.addChild(aestheticNode, -1, nullptr);
        }

        // Inizializzazione sicura nodo BEAT_MAP
        auto beatMapNode = appState.getChildWithName(StateKeys::BeatMapNode);
        if (!beatMapNode.isValid())
        {
            beatMapNode = juce::ValueTree(StateKeys::BeatMapNode);
            beatMapNode.setProperty(StateKeys::BpmBase, 120.0, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, 0.0, nullptr);
            appState.addChild(beatMapNode, -1, nullptr);
        }
    }

    // Restituisce la cartella AppData/DoYourLightShow/SavedShows
    static juce::File getSavedShowsDirectory()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DoYourLightShow")
            .getChildFile("SavedShows");
        dir.createDirectory();
        return dir;
    }

    // Salva l'intero spettacolo in AppData col nome scelto dall'utente
    static bool saveShowToAppData(const juce::ValueTree& appState, const juce::String& showName)
    {
        if (!appState.isValid() || showName.trim().isEmpty()) return false;

        juce::String safeName = juce::File::createLegalFileName(showName.trim());
        if (safeName.isEmpty()) safeName = "Untitled_Show";

        auto targetFile = getSavedShowsDirectory().getChildFile(safeName + ".dyls");

        std::unique_ptr<juce::XmlElement> xml(appState.createXml());
        if (xml != nullptr)
            return xml->writeTo(targetFile);

        return false;
    }

    // Restituisce la lista di tutti gli show salvati in AppData (senza estensione)
    static juce::StringArray getAvailableShowsInAppData()
    {
        juce::StringArray list;
        auto dir = getSavedShowsDirectory();
        auto files = dir.findChildFiles(juce::File::findFiles, false, "*.dyls");

        for (const auto& f : files)
            list.add(f.getFileNameWithoutExtension());

        list.sort(true);
        return list;
    }

    // Carica uno show da AppData dentro appState preservando la libreria globale dei Preset
    static bool loadShowFromAppData(juce::ValueTree& appState, const juce::String& showName)
    {
        auto targetFile = getSavedShowsDirectory().getChildFile(showName + ".dyls");
        if (!targetFile.existsAsFile()) return false;

        std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(targetFile));
        if (xml == nullptr) return false;

        auto loadedTree = juce::ValueTree::fromXml(*xml);
        if (!loadedTree.isValid()) return false;

        // Sostituisce i nodi di progetto mantenendo i listener attivi sui nodi principali
        replaceChildNodeContent(appState, loadedTree, StateKeys::ShowNode);
        replaceChildNodeContent(appState, loadedTree, StateKeys::AudioStemsNode);
        replaceChildNodeContent(appState, loadedTree, StateKeys::AestheticNode);
        replaceChildNodeContent(appState, loadedTree, StateKeys::BeatMapNode);

        // Se il file contiene anche una Venue salvata e la Venue attuale e' vuota, la ripristina
        auto currentVenue = appState.getChildWithName(StateKeys::VenueNode);
        if (currentVenue.isValid() && currentVenue.getNumChildren() == 0)
            replaceChildNodeContent(appState, loadedTree, StateKeys::VenueNode);

        ensureValidAppStateStructure(appState);
        return true;
    }

    // Elimina uno show salvato in AppData
    static bool deleteShowFromAppData(const juce::String& showName)
    {
        auto targetFile = getSavedShowsDirectory().getChildFile(showName + ".dyls");
        if (targetFile.existsAsFile())
            return targetFile.deleteFile();
        return false;
    }

private:
    // Copia in modo sicuro proprieta' e figli dentro il nodo esistente senza sganciare i Listener UI
    static void replaceChildNodeContent(juce::ValueTree& targetRoot,
        const juce::ValueTree& sourceRoot,
        const juce::Identifier& nodeType)
    {
        auto sourceNode = sourceRoot.getChildWithName(nodeType);
        if (!sourceNode.isValid()) return;

        auto targetNode = targetRoot.getChildWithName(nodeType);
        if (!targetNode.isValid())
        {
            targetNode = juce::ValueTree(nodeType);
            targetRoot.addChild(targetNode, -1, nullptr);
        }

        targetNode.removeAllChildren(nullptr);
        targetNode.removeAllProperties(nullptr);

        for (int i = 0; i < sourceNode.getNumProperties(); ++i)
        {
            auto propName = sourceNode.getPropertyName(i);
            if (propName.isValid())
                targetNode.setProperty(propName, sourceNode.getProperty(propName), nullptr);
        }

        for (int i = 0; i < sourceNode.getNumChildren(); ++i)
        {
            auto child = sourceNode.getChild(i);
            if (child.isValid())
                targetNode.addChild(child.createCopy(), -1, nullptr);
        }
    }
};