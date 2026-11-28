#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Model/DataStateKeys.h"
#include "../Model/BeatTimeMap.h"
#include "../Engine/DmxLiveEngine.h"
#include "../Engine/DawTransportSync.h"
#include "../Utils/StateUndoManager.h"
#include <functional>

class LightShowProcessor : public juce::AudioProcessor,
    private juce::ValueTree::Listener,
    private juce::Timer
{
public:
    explicit LightShowProcessor(bool runningInsideDaw = true);
    ~LightShowProcessor() override;

    // --- Accesso condiviso per Editor / MainComponent ---
    juce::ValueTree& getMainState() noexcept { return mainState; }
    BeatTimeMap& getBeatTimeMap() noexcept { return beatTimeMap; }
    DmxLiveEngine& getDmxLiveEngine() noexcept { return dmxLiveEngine; }
    StateUndoManager& getUndoManager() noexcept { return undoManager; }
    const LockFreeTransportBridge& getTransportBridge() const noexcept { return resolvedTransportBridge; }

    bool isRunningInDaw() const noexcept { return hostedInDaw; }

    bool isReaperApiAvailable() const noexcept;
    bool forceSyncFromReaper();
    bool reaperTogglePlayPause();
    bool reaperStopOrRewind();
    bool reaperSeekToBeat(double beat1Based);
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;

    bool hasInitializedSession() const noexcept { return sessionInitialized; }

    void setActiveTabIndex(int tabIndex) noexcept { activeTabIndex = tabIndex; }
    int getActiveTabIndex() const noexcept { return activeTabIndex; }

    void markTabDirty(int tabIndex, bool isDirty);
    bool isTabDirty(int tabIndex) const noexcept;
    void captureCleanSnapshotForTab(int tabIndex);
    void finalizeInitialSessionIfNeeded();

    void setEditorCallbacks(std::function<void()> onDirtyChanged,
        std::function<void()> onHistoryChanged,
        std::function<void(const StateUndoManager::RestoreResult&)> onRestored,
        std::function<void()> onExternalStateLoaded,
        std::function<void()> onBeatMapSynced = nullptr);

    // --- Interfaccia juce::AudioProcessor ---
    const juce::String getName() const override { return "Do Your Light Show"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override { handleStateTreeModified(tree); }
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override { handleStateTreeModified(child); juce::ignoreUnused(parent); }
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree&, int) override { handleStateTreeModified(parent); }
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override { handleStateTreeModified(parent); }
    void valueTreeParentChanged(juce::ValueTree&) override {}

    void handleStateTreeModified(const juce::ValueTree& tree);
    void timerCallback() override;
    void renderDawDmxFrame(const DawTransportState& transport);
    juce::ValueTree makeShowSubtreeSnapshot() const;

    void tryAutoConnectDmxFromSettings();

    static int getLayerPriority(const juce::ValueTree& node);

    bool syncFromReaperHost(bool forceNow);

    const bool hostedInDaw = true;
    bool sessionInitialized = false;
    bool restoringHostState = false;
    bool wasDawPlayingDmx = false;
    int activeTabIndex = 0;

    juce::ValueTree mainState{ "DO_YOUR_LIGHT_SHOW_STATE" };
    BeatTimeMap beatTimeMap;
    DmxLiveEngine dmxLiveEngine;
    StateUndoManager undoManager{ mainState };
    LockFreeTransportBridge transportBridge;
    DawTransportState updateResolvedTransportState();

    LockFreeTransportBridge resolvedTransportBridge;
    std::atomic<uint64_t> audioBlockCounter{ 0 };
    uint64_t lastSeenAudioBlocks = 0;
    double lastAudioBlockSeenMs = 0.0;

    juce::ValueTree cleanVenueSnap;
    juce::ValueTree cleanPresetLibSnap;
    juce::ValueTree cleanShowSnap;

    bool dirtyTab1 = false;
    bool dirtyTab2 = false;
    bool dirtyTab3 = false;
    bool dirtyTab6 = false;

    std::function<void()> editorOnDirtyChanged;
    std::function<void()> editorOnHistoryChanged;
    std::function<void(const StateUndoManager::RestoreResult&)> editorOnStateRestored;
    std::function<void()> editorOnExternalStateLoaded;

    struct ReaperApiBridge;
    std::unique_ptr<ReaperApiBridge> reaperBridge;
    bool syncingFromReaper = false;
    int reaperPollDivider = 0;

    std::function<void()> editorOnBeatMapSynced;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightShowProcessor)
};