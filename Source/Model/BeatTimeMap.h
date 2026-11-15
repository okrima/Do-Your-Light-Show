#pragma once
#include <JuceHeader.h>
#include "DataStateKeys.h"
#include <vector>
#include <algorithm>

class BeatTimeMap
{
public:
    struct BeatAnchor
    {
        double beat = 1.0;
        double timeSec = 0.0;
        double localBpm = 120.0;
    };

    BeatTimeMap() = default;

    // Aggancia la mappa al ValueTree globale e ricostruisce la cache veloce
    void bindToAppState(juce::ValueTree& rootState)
    {
        if (!rootState.isValid()) return;

        beatMapNode = rootState.getChildWithName(StateKeys::BeatMapNode);
        if (!beatMapNode.isValid())
        {
            beatMapNode = juce::ValueTree(StateKeys::BeatMapNode);
            beatMapNode.setProperty(StateKeys::BpmBase, 120.0, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, 0.0, nullptr);
            rootState.addChild(beatMapNode, -1, nullptr);
        }

        rebuildCacheFromState();
    }

    // Ricostruisce il vettore in RAM per letture a 60 FPS nella Mini-DAW e nel Simulatore
    void rebuildCacheFromState()
    {
        anchors.clear();
        if (!beatMapNode.isValid()) return;

        baseBpm = static_cast<double>(beatMapNode.getProperty(StateKeys::BpmBase, 120.0));
        if (baseBpm <= 10.0) baseBpm = 120.0;

        firstDownbeatSec = static_cast<double>(beatMapNode.getProperty(StateKeys::FirstDownbeatSec, 0.0));
        if (firstDownbeatSec < 0.0) firstDownbeatSec = 0.0;

        const int numPoints = beatMapNode.getNumChildren();
        anchors.reserve(static_cast<size_t>(numPoints));

        for (int i = 0; i < numPoints; ++i)
        {
            auto child = beatMapNode.getChild(i);
            if (child.isValid() && child.hasType(StateKeys::BeatPointNode))
            {
                BeatAnchor a;
                a.beat = static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0));
                a.timeSec = static_cast<double>(child.getProperty(StateKeys::PointTimeSec, 0.0));
                a.localBpm = static_cast<double>(child.getProperty(StateKeys::PointBpm, baseBpm));
                anchors.push_back(a);
            }
        }

        std::sort(anchors.begin(), anchors.end(), [](const BeatAnchor& a, const BeatAnchor& b) {
            return a.timeSec < b.timeSec;
            });
    }

    // Imposta un BPM fisso (cancella eventuali punti variabili)
    void setFixedBpm(double newBpm, double downbeatOffsetSec = 0.0)
    {
        baseBpm = juce::jlimit(20.0, 300.0, newBpm);
        firstDownbeatSec = juce::jmax(0.0, downbeatOffsetSec);

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::BpmBase, baseBpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, firstDownbeatSec, nullptr);
            beatMapNode.removeAllChildren(nullptr);
        }
        anchors.clear();
    }

    // Converte i SECONDI dell'audio nel QUARTO DECIMALE (es. 1.25)
    double secondsToBeat(double timeSec) const
    {
        // Modalita' 1: BPM fisso
        if (anchors.size() < 2)
        {
            double deltaSec = timeSec - firstDownbeatSec;
            double beat = 1.0 + (deltaSec * (baseBpm / 60.0));
            return juce::jmax(1.0, beat);
        }

        // Modalita' 2: Mappa dinamica estratta dalla traccia Click
        if (timeSec <= anchors.front().timeSec)
        {
            double deltaSec = timeSec - anchors.front().timeSec;
            double beat = anchors.front().beat + (deltaSec * (anchors.front().localBpm / 60.0));
            return juce::jmax(1.0, beat);
        }

        if (timeSec >= anchors.back().timeSec)
        {
            double deltaSec = timeSec - anchors.back().timeSec;
            return anchors.back().beat + (deltaSec * (anchors.back().localBpm / 60.0));
        }

        // Ricerca binaria tra i punti della traccia Click
        auto it = std::upper_bound(anchors.begin(), anchors.end(), timeSec,
            [](double val, const BeatAnchor& a) { return val < a.timeSec; });

        const auto& p1 = *it;
        const auto& p0 = *(it - 1);

        double spanSec = p1.timeSec - p0.timeSec;
        if (spanSec <= 0.00001) return p0.beat;

        double fraction = (timeSec - p0.timeSec) / spanSec;
        return p0.beat + fraction * (p1.beat - p0.beat);
    }

    // Converte il QUARTO DECIMALE (es. 14.50) nei SECONDI esatti dell'audio
    double beatToSeconds(double beat) const
    {
        double safeBeat = juce::jmax(1.0, beat);

        // Modalita' 1: BPM fisso
        if (anchors.size() < 2)
        {
            return firstDownbeatSec + ((safeBeat - 1.0) * (60.0 / baseBpm));
        }

        // Modalita' 2: Mappa dinamica
        if (safeBeat <= anchors.front().beat)
        {
            return juce::jmax(0.0, anchors.front().timeSec - ((anchors.front().beat - safeBeat) * (60.0 / anchors.front().localBpm)));
        }

        if (safeBeat >= anchors.back().beat)
        {
            return anchors.back().timeSec + ((safeBeat - anchors.back().beat) * (60.0 / anchors.back().localBpm));
        }

        auto it = std::upper_bound(anchors.begin(), anchors.end(), safeBeat,
            [](double val, const BeatAnchor& a) { return val < a.beat; });

        const auto& p1 = *it;
        const auto& p0 = *(it - 1);

        double spanBeat = p1.beat - p0.beat;
        if (spanBeat <= 0.00001) return p0.timeSec;

        double fraction = (safeBeat - p0.beat) / spanBeat;
        return p0.timeSec + fraction * (p1.timeSec - p0.timeSec);
    }

    double getBaseBpm() const { return baseBpm; }

    // Helper di formattazione per l'interfaccia della Mini-DAW
    static juce::String formatTimecode(double timeSec)
    {
        if (timeSec < 0.0) timeSec = 0.0;
        int totalMs = static_cast<int>(std::round(timeSec * 1000.0));
        int minutes = totalMs / 60000;
        int seconds = (totalMs % 60000) / 1000;
        int millis = totalMs % 1000;
        return juce::String::formatted("%02d:%02d.%03d", minutes, seconds, millis);
    }

    static juce::String formatBeat(double beat)
    {
        return "Beat " + juce::String(beat, 2);
    }

private:
    juce::ValueTree beatMapNode;
    double baseBpm = 120.0;
    double firstDownbeatSec = 0.0;
    std::vector<BeatAnchor> anchors;
};