#include "LightShowProcessor.h"
#include "../MainComponent.h"
#include <algorithm>
#include <cmath>
#include <vector>

#if DOYOURLIGHTSHOW_HAS_REAPER_API
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wshadow-field-in-constructor", "-Wnon-virtual-dtor")
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

namespace reaper
{
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wzero-as-null-pointer-constant", "-Wunused-parameter", "-Wnon-virtual-dtor")
        JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4100)

        using namespace Steinberg;
    using INT_PTR = juce::pointer_sized_int;
    using uint32 = Steinberg::uint32;

#include "reaper_vst3_interfaces.h"
    DEF_CLASS_IID(IReaperHostApplication)

        JUCE_END_IGNORE_WARNINGS_MSVC
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
}
#endif

struct LightShowProcessor::ReaperApiBridge
#if DOYOURLIGHTSHOW_HAS_REAPER_API
    : public juce::VST3ClientExtensions
#endif
{
    explicit ReaperApiBridge(LightShowProcessor & ownerRef) : owner(ownerRef) {}

    LightShowProcessor & owner;
    std::atomic<bool> available{ false };
    int lastProjectStateCount = -1;

    using FnCountTempoTimeSigMarkers = int(*)(void*);
    using FnGetTempoTimeSigMarker = bool(*)(void*, int, double*, int*, double*, double*, int*, int*, bool*);
    using FnTimeMap2_timeToQN = double(*)(void*, double);
    using FnTimeMap_GetTimeSigAtTime = void(*)(void*, double, int*, int*, double*);
    using FnGetProjectLength = double(*)(void*);
    using FnEnumProjectMarkers3 = int(*)(void*, int, bool*, double*, double*, const char**, int*, int*);
    using FnGetProjectStateChangeCnt = int(*)(void*);
    using FnColorFromNative = void(*)(int, int*, int*, int*);

    using FnGetPlayState = int(*)();
    using FnCSurf_OnPlay = void(*)();
    using FnCSurf_OnPause = void(*)();
    using FnCSurf_OnStop = void(*)();
    using FnSetEditCurPos = void(*)(double, bool, bool);
    using FnTimeMap2_QNToT = double(*)(void*, double);

    FnCountTempoTimeSigMarkers countTempoMarkers = nullptr;
    FnGetTempoTimeSigMarker    getTempoMarker = nullptr;
    FnTimeMap2_timeToQN        timeToQN = nullptr;
    FnTimeMap_GetTimeSigAtTime getTimeSigAtTime = nullptr;
    FnGetProjectLength         getProjectLength = nullptr;
    FnEnumProjectMarkers3      enumProjectMarkers3 = nullptr;
    FnGetProjectStateChangeCnt getStateChangeCount = nullptr;
    FnColorFromNative          colorFromNative = nullptr;

    FnGetPlayState   getPlayState = nullptr;
    FnCSurf_OnPlay   csurfOnPlay = nullptr;
    FnCSurf_OnPause  csurfOnPause = nullptr;
    FnCSurf_OnStop   csurfOnStop = nullptr;
    FnSetEditCurPos  setEditCurPos = nullptr;
    FnTimeMap2_QNToT qnToTime = nullptr;

#if DOYOURLIGHTSHOW_HAS_REAPER_API
    void setIHostApplication(Steinberg::FUnknown* ptr) override
    {
        if (ptr == nullptr)
            return;

        void* objPtr = nullptr;
        if (ptr->queryInterface(reaper::IReaperHostApplication::iid, &objPtr) == Steinberg::kResultOk
            && objPtr != nullptr)
        {
            auto* host = static_cast<reaper::IReaperHostApplication*>(objPtr);

            auto loadFn = [host](auto& target, const char* name)
            {
                target = reinterpret_cast<std::decay_t<decltype(target)>>(host->getReaperApi(name));
            };

            loadFn(countTempoMarkers,   "CountTempoTimeSigMarkers");
            loadFn(getTempoMarker,      "GetTempoTimeSigMarker");
            loadFn(timeToQN,            "TimeMap2_timeToQN");
            loadFn(getTimeSigAtTime,    "TimeMap_GetTimeSigAtTime");
            loadFn(getProjectLength,    "GetProjectLength");
            loadFn(enumProjectMarkers3, "EnumProjectMarkers3");
            loadFn(getStateChangeCount, "GetProjectStateChangeCount");
            loadFn(colorFromNative,     "ColorFromNative");
            loadFn(getPlayState, "GetPlayState");
            loadFn(csurfOnPlay, "CSurf_OnPlay");
            loadFn(csurfOnPause, "CSurf_OnPause");
            loadFn(csurfOnStop, "CSurf_OnStop");
            loadFn(setEditCurPos, "SetEditCurPos");
            loadFn(qnToTime, "TimeMap2_QNToTime");

            host->release();

            if (timeToQN != nullptr && getTimeSigAtTime != nullptr)
            {
                available.store(true);
                lastProjectStateCount = -1;

                auto* procPtr = &owner;
                juce::MessageManager::callAsync([procPtr]() {
                    procPtr->forceSyncFromReaper();
                });
            }
        }
    }
#endif
};

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

    reaperBridge = std::make_unique<ReaperApiBridge>(*this);

    tryAutoConnectDmxFromSettings();
}

LightShowProcessor::~LightShowProcessor()
{
    stopTimer();
    mainState.removeListener(this);
    dmxLiveEngine.stopOutput(true);
}

void LightShowProcessor::tryAutoConnectDmxFromSettings()
{
    auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DoYourLightShow")
        .getChildFile("dmx_settings.xml");

    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("DMX_LIVE_SETTINGS"))
        return;

    // Ripristina sempre parametri live di sicurezza anche a finestra VST3 chiusa
    const int masterPct = juce::jlimit(0, 100, xml->getIntAttribute("masterPct", 100));
    dmxLiveEngine.setMasterDimmer(static_cast<float>(masterPct) / 100.0f);
    dmxLiveEngine.setTab2LivePreviewEnabled(xml->getBoolAttribute("tab2Live", false));
    dmxLiveEngine.setStandaloneCourtesyDimmer(xml->getBoolAttribute("courtesyDim", true));

    if (!xml->getBoolAttribute("autoConnect", false))
        return;

    const int drvId = juce::jlimit(1, 3, xml->getIntAttribute("driverId", 1));
    std::unique_ptr<DmxOutputDriver> driver;

    if (drvId == 1)
    {
        ArtNetDmxDriver::Config cfg;
        cfg.targetIp = xml->getStringAttribute("artNetIp", "255.255.255.255").trim();
        if (cfg.targetIp.isEmpty()) cfg.targetIp = "255.255.255.255";
        cfg.universe = juce::jlimit(0, 15, xml->getIntAttribute("artNetUni", 0));
        cfg.subnet = juce::jlimit(0, 15, xml->getIntAttribute("artNetSub", 0));
        cfg.net = juce::jlimit(0, 127, xml->getIntAttribute("artNetNet", 0));
        driver = std::make_unique<ArtNetDmxDriver>(cfg);
    }
    else if (drvId == 2)
    {
        SacnDmxDriver::Config cfg;
        cfg.useMulticast = xml->getBoolAttribute("sacnMulticast", true);
        cfg.universe = juce::jlimit(1, 63999, xml->getIntAttribute("sacnUni", 1));
        cfg.priority = juce::jlimit(0, 200, xml->getIntAttribute("sacnPrio", 100));
        cfg.targetIp = cfg.useMulticast
            ? SacnDmxDriver::makeMulticastIp(cfg.universe)
            : xml->getStringAttribute("sacnIp", "239.255.0.1").trim();
        driver = std::make_unique<SacnDmxDriver>(cfg);
    }
    else
    {
        const juce::String savedCom = xml->getStringAttribute("comPort", "").trim();
        const auto ports = EnttecProDmxDriver::listDetailedComPorts();

        juce::String chosenPort;
        if (savedCom.isNotEmpty())
        {
            for (const auto& p : ports)
            {
                if (p.portName.equalsIgnoreCase(savedCom))
                {
                    chosenPort = p.portName;
                    break;
                }
            }
        }

        // Fallback live: se la porta USB ha cambiato numero COM, aggancia il primo FTDI presente
        if (chosenPort.isEmpty())
        {
            for (const auto& p : ports)
            {
                if (p.isFtdi)
                {
                    chosenPort = p.portName;
                    break;
                }
            }
        }

        if (chosenPort.isEmpty())
            return;

        driver = std::make_unique<EnttecProDmxDriver>(chosenPort);
    }

    dmxLiveEngine.getOutputManager().setRefreshRateHz(40);
    dmxLiveEngine.startOutput(std::move(driver));
}

void LightShowProcessor::setEditorCallbacks(
    std::function<void()> onDirtyChanged,
    std::function<void()> onHistoryChanged,
    std::function<void(const StateUndoManager::RestoreResult&)> onRestored,
    std::function<void()> onExternalStateLoaded,
    std::function<void()> onBeatMapSynced)
{
    editorOnDirtyChanged = std::move(onDirtyChanged);
    editorOnHistoryChanged = std::move(onHistoryChanged);
    editorOnStateRestored = std::move(onRestored);
    editorOnExternalStateLoaded = std::move(onExternalStateLoaded);
    editorOnBeatMapSynced = std::move(onBeatMapSynced);
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
    if (!tree.isValid() || !sessionInitialized || restoringHostState || syncingFromReaper || undoManager.isRestoring())
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
    if (hostedInDaw && reaperBridge != nullptr && reaperBridge->available.load())
    {
        if (++reaperPollDivider >= 30)
        {
            reaperPollDivider = 0;
            syncFromReaperHost(false);
        }
    }

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

bool LightShowProcessor::isReaperApiAvailable() const noexcept
{
    return reaperBridge != nullptr && reaperBridge->available.load();
}

bool LightShowProcessor::forceSyncFromReaper()
{
    return syncFromReaperHost(true);
}

juce::VST3ClientExtensions* LightShowProcessor::getVST3ClientExtensions()
{
#if DOYOURLIGHTSHOW_HAS_REAPER_API
    return reaperBridge.get();
#else
    return nullptr;
#endif
}

bool LightShowProcessor::syncFromReaperHost(bool forceNow)
{
    if (reaperBridge == nullptr || !reaperBridge->available.load())
        return false;

    auto& api = *reaperBridge;
    if (api.timeToQN == nullptr || api.getTimeSigAtTime == nullptr)
        return false;

    if (!forceNow && api.getStateChangeCount != nullptr)
    {
        const int curCount = api.getStateChangeCount(nullptr);
        if (curCount == api.lastProjectStateCount)
            return false;
        api.lastProjectStateCount = curCount;
    }

    // 1. BPM e metrica iniziali al secondo 0.0
    int initNum = 4, initDen = 4;
    double initBpm = 120.0;
    api.getTimeSigAtTime(nullptr, 0.0, &initNum, &initDen, &initBpm);

    std::vector<BeatTimeMap::BeatAnchor> tempos;
    std::vector<BeatTimeMap::MeterAnchor> meters;
    std::vector<BeatTimeMap::ProjectMarker> markers;

    tempos.push_back({ 1.0, 0.0, juce::jlimit(20.0, 300.0, initBpm) });
    meters.push_back({ 1.0, juce::jlimit(1, 32, initNum), BeatTimeMap::sanitizeDenominator(initDen), 1 });

    // 2. Tutti i Tempo / Time Signature Marker del progetto Reaper
    if (api.countTempoMarkers != nullptr && api.getTempoMarker != nullptr)
    {
        const int numPts = api.countTempoMarkers(nullptr);
        for (int i = 0; i < numPts; ++i)
        {
            double tPos = 0.0, beatPos = 0.0, bpm = 0.0;
            int measurePos = 0, tsNum = -1, tsDen = -1;
            bool linearTempo = false;

            if (!api.getTempoMarker(nullptr, i, &tPos, &measurePos, &beatPos, &bpm, &tsNum, &tsDen, &linearTempo))
                continue;

            const double safeTime = juce::jmax(0.0, tPos);
            const double qn = juce::jmax(0.0, api.timeToQN(nullptr, safeTime));
            const double beat1Based = 1.0 + qn;

            if (bpm > 1.0)
            {
                if (!tempos.empty() && std::abs(tempos.back().beat - beat1Based) < 1e-4)
                    tempos.back() = { beat1Based, safeTime, juce::jlimit(20.0, 300.0, bpm) };
                else
                    tempos.push_back({ beat1Based, safeTime, juce::jlimit(20.0, 300.0, bpm) });
            }

            if (tsNum > 0 && tsDen > 0)
            {
                const int sNum = juce::jlimit(1, 32, tsNum);
                const int sDen = BeatTimeMap::sanitizeDenominator(tsDen);
                if (!meters.empty() && std::abs(meters.back().startBeat - beat1Based) < 1e-4)
                    meters.back() = { beat1Based, sNum, sDen, 1 };
                else
                    meters.push_back({ beat1Based, sNum, sDen, 1 });
            }
        }
    }

    // 3. Tutti i Project Marker e le Region della scaletta
    double maxBeatFound = tempos.empty() ? 1.0 : tempos.back().beat;

    if (api.enumProjectMarkers3 != nullptr)
    {
        int idx = 0;
        for (int guard = 0; guard < 4096; ++guard)
        {
            bool isRgn = false;
            double posSec = 0.0, rgnEndSec = 0.0;
            const char* rawName = nullptr;
            int numIdx = 0, nativeCol = 0;

            idx = api.enumProjectMarkers3(nullptr, idx, &isRgn, &posSec, &rgnEndSec, &rawName, &numIdx, &nativeCol);
            if (idx <= 0)
                break;

            const double startQ = 1.0 + juce::jmax(0.0, api.timeToQN(nullptr, juce::jmax(0.0, posSec)));
            const double endQ = isRgn
                ? (1.0 + juce::jmax(0.0, api.timeToQN(nullptr, juce::jmax(posSec, rgnEndSec))))
                : startQ;

            BeatTimeMap::ProjectMarker pm;
            pm.beat = startQ;
            pm.endBeat = juce::jmax(startQ, endQ);
            pm.timeSec = juce::jmax(0.0, posSec);
            pm.isRegion = isRgn;
            pm.indexNumber = numIdx;
            pm.name = (rawName != nullptr && rawName[0] != '\0')
                ? juce::String::fromUTF8(rawName)
                : ((isRgn ? "R" : "M") + juce::String(numIdx));

            if (nativeCol != 0)
            {
                int r = 16, g = 185, b = 129;
                if (api.colorFromNative != nullptr)
                {
                    api.colorFromNative(nativeCol, &r, &g, &b);
                }
                else
                {
                    r = nativeCol & 0xFF;
                    g = (nativeCol >> 8) & 0xFF;
                    b = (nativeCol >> 16) & 0xFF;
                }
                pm.colour = juce::Colour(static_cast<uint8_t>(juce::jlimit(0, 255, r)),
                    static_cast<uint8_t>(juce::jlimit(0, 255, g)),
                    static_cast<uint8_t>(juce::jlimit(0, 255, b)));
            }
            else
            {
                pm.colour = isRgn ? juce::Colour(0xff0ea5e9) : juce::Colour(0xff10b981);
            }

            maxBeatFound = juce::jmax(maxBeatFound, pm.endBeat);
            markers.push_back(std::move(pm));
        }
    }

    // 4. Lunghezza totale progetto Reaper
    if (api.getProjectLength != nullptr)
    {
        const double projLenSec = juce::jmax(0.0, api.getProjectLength(nullptr));
        if (projLenSec > 0.1)
            maxBeatFound = juce::jmax(maxBeatFound, 1.0 + juce::jmax(0.0, api.timeToQN(nullptr, projLenSec)));
    }

    bool changed = false;
    {
        juce::ScopedValueSetter<bool> guard(syncingFromReaper, true);
        changed = beatTimeMap.applyExternalDawMap(std::move(tempos), std::move(meters), std::move(markers), maxBeatFound + 8.0);
    }

    if (changed && editorOnBeatMapSynced)
        editorOnBeatMapSynced();

    return changed;
}

bool LightShowProcessor::reaperTogglePlayPause()
{
    if (reaperBridge == nullptr || !reaperBridge->available.load())
        return false;

    auto& api = *reaperBridge;
    const int st = (api.getPlayState != nullptr) ? api.getPlayState() : 0;
    const bool isPlayingNow = (st & 1) != 0;

    if (isPlayingNow)
    {
        if (api.csurfOnPause != nullptr)
        {
            api.csurfOnPause();
            return true;
        }
        if (api.csurfOnStop != nullptr)
        {
            api.csurfOnStop();
            return true;
        }
    }
    else if (api.csurfOnPlay != nullptr)
    {
        api.csurfOnPlay();
        return true;
    }

    return false;
}

bool LightShowProcessor::reaperStopOrRewind()
{
    if (reaperBridge == nullptr || !reaperBridge->available.load())
        return false;

    auto& api = *reaperBridge;
    const int st = (api.getPlayState != nullptr) ? api.getPlayState() : 0;
    const bool isPlayingOrPaused = (st & 3) != 0;

    if (isPlayingOrPaused && api.csurfOnStop != nullptr)
    {
        api.csurfOnStop();
        return true;
    }

    if (api.setEditCurPos != nullptr)
    {
        api.setEditCurPos(0.0, true, false);
        return true;
    }

    return false;
}

bool LightShowProcessor::reaperSeekToBeat(double beat1Based)
{
    if (reaperBridge == nullptr || !reaperBridge->available.load())
        return false;

    auto& api = *reaperBridge;
    if (api.setEditCurPos == nullptr)
        return false;

    const double qn0Based = juce::jmax(0.0, beat1Based - 1.0);
    const double targetSec = (api.qnToTime != nullptr)
        ? juce::jmax(0.0, api.qnToTime(nullptr, qn0Based))
        : beatTimeMap.beatToSeconds(juce::jmax(1.0, beat1Based));

    api.setEditCurPos(targetSec, true, true);
    return true;
}