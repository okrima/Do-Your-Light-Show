#pragma once
#include <JuceHeader.h>
#include "../Model/DataStateKeys.h"

class StemAudioEngine
{
public:
    StemAudioEngine()
    {
        formatManager.registerBasicFormats(); // Abilita WAV, AIFF, MP3, FLAC, OGG
        deviceManager.initialiseWithDefaultDevices(0, 2); // 0 input, 2 output stereo
        sourcePlayer.setSource(&transportSource);
        deviceManager.addAudioCallback(&sourcePlayer);
    }

    ~StemAudioEngine()
    {
        transportSource.stop();
        transportSource.setSource(nullptr);
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);
    }

    // Carica il Full Mix per l'ascolto in Mini-DAW e aggiorna il ValueTree
    bool loadFullMix(const juce::File& audioFile, juce::ValueTree& appState)
    {
        if (!audioFile.existsAsFile())
            return false;

        auto* reader = formatManager.createReaderFor(audioFile);
        if (reader == nullptr)
            return false;

        transportSource.stop();
        transportSource.setSource(nullptr);
        currentFullMixReaderSource.reset();

        currentFullMixReaderSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        transportSource.setSource(currentFullMixReaderSource.get(),
            32768, // Buffer di read-ahead fluido per lo scrubbing
            &readAheadThread,
            reader->sampleRate);

        currentFullMixFile = audioFile;

        // Salva il percorso nel nodo AUDIO_STEMS
        if (appState.isValid())
        {
            auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
                stemsNode.setProperty(StateKeys::StemFullMix, audioFile.getFullPathName(), nullptr);
        }

        return true;
    }

    // Registra il percorso di uno stem secondario (Click, Drums, Bass, Guitars) nel ValueTree
    bool setSecondaryStemFile(const juce::Identifier& stemKey, const juce::File& audioFile, juce::ValueTree& appState)
    {
        if (!audioFile.existsAsFile() || !appState.isValid())
            return false;

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        if (!stemsNode.isValid())
            return false;

        stemsNode.setProperty(stemKey, audioFile.getFullPathName(), nullptr);
        return true;
    }

    // Crea un reader per l'analisi AI offline di qualsiasi stem registrato
    std::unique_ptr<juce::AudioFormatReader> createReaderForStem(const juce::Identifier& stemKey,
        const juce::ValueTree& appState)
    {
        if (!appState.isValid()) return nullptr;

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        if (!stemsNode.isValid()) return nullptr;

        juce::String path = stemsNode.getProperty(stemKey, "").toString();
        if (path.isEmpty()) return nullptr;

        juce::File file(path);
        if (!file.existsAsFile()) return nullptr;

        return std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file));
    }

    // Controlli di trasporto
    void play() { transportSource.start(); }
    void pause() { transportSource.stop(); }

    void togglePlayPause()
    {
        if (transportSource.isPlaying())
            transportSource.stop();
        else
            transportSource.start();
    }

    void stopAndRewind()
    {
        transportSource.stop();
        transportSource.setPosition(0.0);
    }

    void setPositionSeconds(double timeSec)
    {
        double totalLen = getTotalLengthSeconds();
        if (totalLen > 0.0)
            transportSource.setPosition(juce::jlimit(0.0, totalLen, timeSec));
        else
            transportSource.setPosition(juce::jmax(0.0, timeSec));
    }

    double getCurrentPositionSeconds() const { return transportSource.getCurrentPosition(); }
    double getTotalLengthSeconds() const { return transportSource.getLengthInSeconds(); }
    bool isPlaying() const { return transportSource.isPlaying(); }
    bool hasFullMixLoaded() const { return currentFullMixReaderSource != nullptr; }
    juce::File getCurrentFullMixFile() const { return currentFullMixFile; }

    juce::AudioFormatManager& getFormatManager() { return formatManager; }
    juce::AudioTransportSource& getTransportSource() { return transportSource; }

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread{ "AudioReadAheadThread" };
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> currentFullMixReaderSource;
    juce::File currentFullMixFile;

public:
    void startBackgroundThread() { readAheadThread.startThread(juce::Thread::Priority::normal); }
};