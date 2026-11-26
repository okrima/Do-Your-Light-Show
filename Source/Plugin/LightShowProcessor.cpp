#include "LightShowProcessor.h"
#include "../MainComponent.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    class LightShowPluginEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit LightShowPluginEditor(LightShowProcessor& proc)
            : juce::AudioProcessorEditor(proc),
            mainComponent(proc)
        {
            addAndMakeVisible(mainComponent);
            setResizable(true, true);
            setResizeLimits(1200, 720, 4096, 2160);
            setSize(1440, 860);
        }

        void resized() override
        {
            mainComponent.setBounds(getLocalBounds());
        }

    private:
        MainComponent mainComponent;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LightShowPluginEditor)
    };
}

LightShowProcessor::LightShowProcessor(bool runningInsideDaw)
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    hostedInDaw(runningInsideDaw)
{
    beatTimeMap.bindToAppState(mainState);

    undoManager.onHistoryChanged = [this]()
        {
            if (editorOnHistoryChanged)
                editorOnHistoryChanged();
        };

    undoManager.onStateRestored = [this](const StateUndoManager::RestoreResult& res)
        {
            if (res.venueChanged)
            {
                auto v = mainState.getChildWithName(StateKeys::VenueNode);
                markTabDirty(0, !v.isEquivalentTo(cleanVenueSnap));
            }
            if (res.presetLibChanged)
            {
                auto p = mainState.getChildWithName(StateKeys::PresetLibNode);
                markTabDirty(1, !p.isEquivalentTo(cleanPresetLibSnap));
            }
            if (res.showChanged || res.otherChanged)
            {
                if (res.otherChanged)
                    beatTimeMap.bindToAppState(mainState);
                markTabDirty(2, !makeShowSubtreeSnapshot().isEquivalentTo(cleanShowSnap));
            }

            if (hostedInDaw)
                updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));

            if (editorOnStateRestored)
                editorOnStateRestored(res);
        };

    mainState.addListener(this);

    if (hostedInDaw)
        startTimerHz(60);
}

LightShowProcessor::~LightShowProcessor()
{
    stopTimer();
    mainState.removeListener(this);
    dmxLiveEngine.stopOutput(true);
}

void LightShowProcessor::setEditorCallbacks(
    std::function<void()> onDirtyChanged,
    std::function<void()> onHistoryChanged,
    std::function<void(const StateUndoManager::RestoreResult&)> onRestored,
    std::function<void()> onExternalStateLoaded)
{
    editorOnDirtyChanged = std::move(onDirtyChanged);
    editorOnHistoryChanged = std::move(onHistoryChanged);
    editorOnStateRestored = std::move(onRestored);
    editorOnExternalStateLoaded = std::move(onExternalStateLoaded);
}

void LightShowProcessor::finalizeInitialSessionIfNeeded()
{
    if (sessionInitialized)
        return;

    sessionInitialized = true;
    dirtyTab1 = false;
    dirtyTab2 = false;
    dirtyTab3 = false;

    captureCleanSnapshotForTab(0);
    captureCleanSnapshotForTab(1);
    captureCleanSnapshotForTab(2);
    undoManager.resetHistory();

    if (editorOnDirtyChanged)
        editorOnDirtyChanged();
    if (editorOnHistoryChanged)
        editorOnHistoryChanged();
}

void LightShowProcessor::markTabDirty(int tabIndex, bool isDirty)
{
    bool* flag = nullptr;
    if (tabIndex == 0) flag = &dirtyTab1;
    else if (tabIndex == 1) flag = &dirtyTab2;
    else if (tabIndex == 2) flag = &dirtyTab3;
    else if (tabIndex == 5) flag = &dirtyTab6;

    if (flag != nullptr && *flag != isDirty)
    {
        *flag = isDirty;
        if (editorOnDirtyChanged)
            editorOnDirtyChanged();
    }
}

bool LightShowProcessor::isTabDirty(int tabIndex) const noexcept
{
    if (tabIndex == 0) return dirtyTab1;
    if (tabIndex == 1) return dirtyTab2;
    if (tabIndex == 2) return dirtyTab3;
    if (tabIndex == 5) return dirtyTab6;
    return false;
}

void LightShowProcessor::captureCleanSnapshotForTab(int tabIndex)
{
    if (tabIndex == 0)
    {
        auto v = mainState.getChildWithName(StateKeys::VenueNode);
        cleanVenueSnap = v.isValid() ? v.createCopy() : juce::ValueTree(StateKeys::VenueNode);
    }
    else if (tabIndex == 1)
    {
        auto p = mainState.getChildWithName(StateKeys::PresetLibNode);
        cleanPresetLibSnap = p.isValid() ? p.createCopy() : juce::ValueTree(StateKeys::PresetLibNode);
    }
    else if (tabIndex == 2)
    {
        cleanShowSnap = makeShowSubtreeSnapshot();
    }
}

juce::ValueTree LightShowProcessor::makeShowSubtreeSnapshot() const
{
    juce::ValueTree snap("SHOW_SUBTREE_SNAP");
    for (int i = 0; i < mainState.getNumChildren(); ++i)
    {
        auto ch = mainState.getChild(i);
        if (!ch.hasType(StateKeys::VenueNode) && !ch.hasType(StateKeys::PresetLibNode))
            snap.addChild(ch.createCopy(), -1, nullptr);
    }
    return snap;
}

void LightShowProcessor::handleStateTreeModified(const juce::ValueTree& tree)
{
    if (!tree.isValid() || !sessionInitialized || restoringHostState || undoManager.isRestoring())
        return;

    juce::ValueTree top = tree;
    while (top.getParent().isValid() && top.getParent() != mainState)
        top = top.getParent();

    if (top == mainState)
        return;

    bool changed = false;

    if (top.hasType(StateKeys::VenueNode))
    {
        markTabDirty(0, true);
        undoManager.notifyStateChanged("Tab 1 - Venue Patch");
        changed = true;
    }
    else if (top.hasType(StateKeys::PresetLibNode))
    {
        markTabDirty(1, true);
        undoManager.notifyStateChanged("Tab 2 - Scene Builder");
        changed = true;
    }
    else if (top.hasType(StateKeys::ShowNode))
    {
        if (top.getNumChildren() > 0 || activeTabIndex == 2)
        {
            markTabDirty(2, true);
            undoManager.notifyStateChanged("Tab 3 - Pattern Creator");
            changed = true;
        }
    }
    else if (activeTabIndex == 2 || activeTabIndex == 3)
    {
        markTabDirty(2, true);
        undoManager.notifyStateChanged(activeTabIndex == 3 ? "Tab 4 - Time & Metric" : "Tab 3 - Pattern Creator");
        changed = true;
    }

    if (changed && hostedInDaw)
        updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
}

void LightShowProcessor::prepareToPlay(double, int) {}
void LightShowProcessor::releaseResources() {}

bool LightShowProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& outSet = layouts.getMainOutputChannelSet();
    const auto& inSet = layouts.getMainInputChannelSet();

    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;

    if (!inSet.isDisabled() && inSet != outSet)
        return false;

    return true;
}

void LightShowProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    // Pass-through audio pulito (nessuna modifica ai canali d'ingresso, azzera eventuali uscite extra)
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // Lettura 100% lock-free del transport DAW (sicura sul thread ASIO del Behringer XR18)
    if (auto* ph = getPlayHead())
    {
        if (const auto info = ph->getPosition())
        {
            DawTransportState ts;
            ts.isHostActive = true;
            ts.isPlaying = info->getIsPlaying();

            if (const auto sec = info->getTimeInSeconds())
                ts.timeSeconds = *sec;
            if (const auto ppq = info->getPpqPosition())
                ts.ppqPosition = *ppq;
            if (const auto barStart = info->getPpqPositionOfLastBarStart())
                ts.ppqBarStart = *barStart;
            if (const auto bpmVal = info->getBpm())
                ts.bpm = *bpmVal;
            if (const auto sig = info->getTimeSignature())
            {
                ts.timeSigNum = sig->numerator;
                ts.timeSigDen = sig->denominator;
            }
            if (const auto barIdx = info->getBarCount())
            {
                ts.barCount = *barIdx;
                ts.hasBarCount = true;
            }

            transportBridge.writeFromAudioThread(ts);
        }
    }
}

juce::AudioProcessorEditor* LightShowProcessor::createEditor()
{
    return new LightShowPluginEditor(*this);
}

void LightShowProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    undoManager.commitPendingNow();
    if (auto xml = mainState.createXml())
        copyXmlToBinary(*xml, destData);
}

void LightShowProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto loaded = juce::ValueTree::fromXml(*xml);
        if (loaded.isValid() && loaded.hasType(mainState.getType()))
        {
            {
                juce::ScopedValueSetter<bool> guard(restoringHostState, true);
                mainState.copyPropertiesAndChildrenFrom(loaded, nullptr);
            }

            beatTimeMap.bindToAppState(mainState);
            sessionInitialized = true;
            dirtyTab1 = false;
            dirtyTab2 = false;
            dirtyTab3 = false;

            captureCleanSnapshotForTab(0);
            captureCleanSnapshotForTab(1);
            captureCleanSnapshotForTab(2);
            undoManager.resetHistory();

            if (editorOnExternalStateLoaded)
                editorOnExternalStateLoaded();
            if (editorOnDirtyChanged)
                editorOnDirtyChanged();
            if (editorOnHistoryChanged)
                editorOnHistoryChanged();
        }
    }
}

int LightShowProcessor::getLayerPriority(const juce::ValueTree& node)
{
    const juce::String layer = node.getProperty(StateKeys::LayerType, "Base").toString();
    if (layer.equalsIgnoreCase("OverlayFX")) return 2;
    if (layer.equalsIgnoreCase("Movement"))  return 1;
    return 0;
}

void LightShowProcessor::timerCallback()
{
    if (!hostedInDaw || !dmxLiveEngine.isOutputActive())
    {
        if (wasDawPlayingDmx)
        {
            wasDawPlayingDmx = false;
            dmxLiveEngine.setShowPlaybackActive(false);
        }
        return;
    }

    const auto transport = transportBridge.read();
    if (!transport.isHostActive)
        return;

    if (transport.isPlaying)
    {
        wasDawPlayingDmx = true;
        dmxLiveEngine.setShowPlaybackActive(true);
        renderDawDmxFrame(transport);
    }
    else if (wasDawPlayingDmx)
    {
        wasDawPlayingDmx = false;
        dmxLiveEngine.setShowPlaybackActive(false);
        dmxLiveEngine.clearAllOutput();
    }
}

void LightShowProcessor::renderDawDmxFrame(const DawTransportState& transport)
{
    const auto musicalPos = transport.toMusicalPosition();
    const double currentBeat = musicalPos.totalQuarters;

    struct ActiveSceneSortEntry
    {
        DmxLiveEngine::LiveSceneInput input;
        int orderIndex = 0;
        int layerPriority = 0;
    };

    std::vector<ActiveSceneSortEntry> active;
    auto showNode = mainState.getChildWithName(StateKeys::ShowNode);

    if (showNode.isValid())
    {
        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            if (!scene.hasType(StateKeys::Scene))
                continue;

            const double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            const double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));

            if (currentBeat >= startB && currentBeat < (startB + durB))
            {
                ActiveSceneSortEntry e;
                e.input.node = scene;
                e.input.startBeat = startB;
                e.input.durationBeats = durB;
                e.input.progress = juce::jlimit(0.0, 1.0, (currentBeat - startB) / durB);
                e.orderIndex = i;
                e.layerPriority = getLayerPriority(scene);
                active.push_back(std::move(e));
            }
        }
    }

    std::sort(active.begin(), active.end(), [](const ActiveSceneSortEntry& a, const ActiveSceneSortEntry& b) {
        if (a.layerPriority != b.layerPriority) return a.layerPriority < b.layerPriority;
        if (std::abs(a.input.startBeat - b.input.startBeat) > 1e-5) return a.input.startBeat < b.input.startBeat;
        return a.orderIndex < b.orderIndex;
        });

    std::vector<DmxLiveEngine::LiveSceneInput> liveInputs;
    liveInputs.reserve(active.size());
    for (auto& e : active)
        liveInputs.push_back(std::move(e.input));

    dmxLiveEngine.renderLiveFrame(mainState, liveInputs, currentBeat, musicalPos, false);
}