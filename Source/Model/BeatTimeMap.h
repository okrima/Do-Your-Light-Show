#pragma once
#include <JuceHeader.h>
#include "DataStateKeys.h"
#include <vector>
#include <algorithm>
#include <cmath>

class BeatTimeMap
{
public:
    struct BeatAnchor
    {
        double beat = 1.0;
        double timeSec = 0.0;
        double localBpm = 120.0;
    };

    struct MeterAnchor
    {
        double startBeat = 1.0; // Posizione in Quarti (1-based) in cui entra la nuova metrica
        int num = 4;            // Numeratore (es. 4, 3, 6, 5, 7, 12)
        int den = 4;            // Denominatore (es. 2, 4, 8, 16)
        int startBar = 1;       // Numero di battuta calcolato automaticamente (1-based)

        double getBarLengthQ() const noexcept
        {
            const int safeNum = juce::jmax(1, num);
            const int safeDen = juce::jmax(1, den);
            return static_cast<double>(safeNum) * (4.0 / static_cast<double>(safeDen));
        }
    };

    struct ProjectMarker
    {
        double beat = 1.0;        // Posizione in Quarti (1-based)
        double endBeat = 1.0;     // > beat se e' una Region
        double timeSec = 0.0;
        bool isRegion = false;
        int indexNumber = 1;
        juce::String name;
        juce::Colour colour{ 0xff10b981 };
    };

    static inline const juce::Identifier kProjectMarkerNode{ "PROJECT_MARKER" };

    struct MusicalPosition
    {
        double totalQuarters = 1.0;    // Posizione assoluta in Quarti (1-based)
        int    barIndex = 1;           // Numero battuta reale (1-based)
        double beatInBarQ = 0.0;       // Quarti trascorsi dall'ultimo battere [0 .. barLengthQ)
        double barLengthQ = 4.0;       // Lunghezza della misura corrente in Quarti
        int    timeSigNum = 4;
        int    timeSigDen = 4;
        double continuousBarPos = 0.0; // (barIndex - 1) + (beatInBarQ / barLengthQ)
        double localBpm = 120.0;

        double getSubdivisionInBar() const noexcept
        {
            return 1.0 + beatInBarQ * (static_cast<double>(timeSigDen) / 4.0);
        }

        juce::String formatBarString() const
        {
            int wholeSub = juce::jlimit(1, juce::jmax(1, timeSigNum),
                static_cast<int>(std::floor(getSubdivisionInBar() + 1e-6)));
            return "M" + juce::String(barIndex) + "." + juce::String(wholeSub)
                + " (" + juce::String(timeSigNum) + "/" + juce::String(timeSigDen) + ")";
        }
    };

    struct MetronomeState
    {
        int barIndex = 0;       // 0 = prima di firstDownbeatSec, >= 1 = battuta attiva
        int subdivIndex = 0;    // 1..timeSigNum (1 = battere accentato)
        double fracInSub = 0.0; // [0..1) progresso nella suddivisione corrente
    };

    struct MidiMapImportResult
    {
        bool success = false;
        int tempoChangesImported = 0;
        int meterChangesImported = 0;
        double initialBpm = 120.0;
        int initialNum = 4;
        int initialDen = 4;
        juce::String errorMessage;
    };

    BeatTimeMap() = default;

    void bindToAppState(juce::ValueTree& rootState)
    {
        if (!rootState.isValid()) return;

        beatMapNode = rootState.getChildWithName(StateKeys::BeatMapNode);
        if (!beatMapNode.isValid())
        {
            beatMapNode = juce::ValueTree(StateKeys::BeatMapNode);
            beatMapNode.setProperty(StateKeys::BpmBase, 120.0, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, 0.0, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigNum, 4, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, 4, nullptr);
            rootState.addChild(beatMapNode, -1, nullptr);
        }

        rebuildCacheFromState();
    }

    void rebuildCacheFromState()
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);

        anchors.clear();
        meterAnchors.clear();
        projectMarkers.clear();

        if (!beatMapNode.isValid()) return;

        baseBpm = static_cast<double>(beatMapNode.getProperty(StateKeys::BpmBase, 120.0));
        if (baseBpm <= 10.0) baseBpm = 120.0;

        firstDownbeatSec = static_cast<double>(beatMapNode.getProperty(StateKeys::FirstDownbeatSec, 0.0));
        if (firstDownbeatSec < 0.0) firstDownbeatSec = 0.0;

        baseTimeSigNum = juce::jlimit(1, 32, static_cast<int>(beatMapNode.getProperty(StateKeys::TimeSigNum, 4)));
        baseTimeSigDen = sanitizeDenominator(static_cast<int>(beatMapNode.getProperty(StateKeys::TimeSigDen, 4)));
        midiEndBeatCache = juce::jmax(1.0,
            static_cast<double>(beatMapNode.getProperty(juce::Identifier("midiEndBeat"), 1.0)));

        const int numChildren = beatMapNode.getNumChildren();
        anchors.reserve(static_cast<size_t>(numChildren));

        for (int i = 0; i < numChildren; ++i)
        {
            auto child = beatMapNode.getChild(i);
            if (!child.isValid()) continue;

            if (child.hasType(StateKeys::BeatPointNode))
            {
                BeatAnchor a;
                a.beat = juce::jmax(1.0, static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0)));
                a.timeSec = juce::jmax(0.0, static_cast<double>(child.getProperty(StateKeys::PointTimeSec, 0.0)));
                a.localBpm = juce::jlimit(20.0, 300.0, static_cast<double>(child.getProperty(StateKeys::PointBpm, baseBpm)));
                anchors.push_back(a);
            }
            else if (child.hasType(StateKeys::MeterPointNode))
            {
                MeterAnchor m;
                m.startBeat = juce::jmax(1.0, static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0)));
                m.num = juce::jlimit(1, 32, static_cast<int>(child.getProperty(StateKeys::TimeSigNum, baseTimeSigNum)));
                m.den = sanitizeDenominator(static_cast<int>(child.getProperty(StateKeys::TimeSigDen, baseTimeSigDen)));
                meterAnchors.push_back(m);
            }
            else if (child.hasType(kProjectMarkerNode))
            {
                ProjectMarker pm;
                pm.beat = juce::jmax(1.0, static_cast<double>(child.getProperty("pointBeat", 1.0)));
                pm.endBeat = juce::jmax(pm.beat, static_cast<double>(child.getProperty("endBeat", pm.beat)));
                pm.timeSec = juce::jmax(0.0, static_cast<double>(child.getProperty("pointTimeSec", 0.0)));
                pm.isRegion = static_cast<bool>(child.getProperty("isRegion", false));
                pm.indexNumber = static_cast<int>(child.getProperty("markerIndex", 1));
                pm.name = child.getProperty("markerName", "").toString();
                pm.colour = juce::Colour::fromString(child.getProperty("markerColour", "ff10b981").toString());
                projectMarkers.push_back(std::move(pm));
            }
        }

        std::sort(anchors.begin(), anchors.end(), [](const BeatAnchor& a, const BeatAnchor& b) {
            return a.beat < b.beat;
            });

        std::sort(projectMarkers.begin(), projectMarkers.end(), [](const ProjectMarker& a, const ProjectMarker& b) {
            if (std::abs(a.beat - b.beat) > 1e-4) return a.beat < b.beat;
            return a.isRegion < b.isRegion;
            });

        finalizeMeterAnchors();
    }

    // Imposta un BPM fisso (rimuove solo i BEAT_POINT, preserva i METER_POINT)
    void setFixedBpm(double newBpm, double downbeatOffsetSec = 0.0)
    {
        const double safeBpm = juce::jlimit(20.0, 300.0, newBpm);
        const double safeOff = juce::jmax(0.0, downbeatOffsetSec);

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::BpmBase, safeBpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, safeOff, nullptr);
            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::BeatPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }
        }
        rebuildCacheFromState();
    }

    void setFirstDownbeatSeconds(double newOffsetSec)
    {
        const double safeOff = juce::jmax(0.0, newOffsetSec);
        if (beatMapNode.isValid())
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, safeOff, nullptr);

        auto list = getEditableTempoAnchors();
        if (list.size() > 1)
            reintegrateAndPersistTempoAnchors(std::move(list), safeOff);
        else
            rebuildCacheFromState();
    }

    // --- CRUD Tempo Anchors (BPM) con re-integrazione automatica di timeSec ---
    std::vector<BeatAnchor> getEditableTempoAnchors() const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        if (!anchors.empty())
            return anchors;

        BeatAnchor a;
        a.beat = 1.0;
        a.timeSec = firstDownbeatSec;
        a.localBpm = baseBpm;
        return { a };
    }

    void addOrUpdateTempoChange(double beatQ, double bpm)
    {
        auto list = getEditableTempoAnchors();
        const double safeBeat = juce::jmax(1.0, beatQ);
        const double safeBpm = juce::jlimit(20.0, 300.0, bpm);

        bool updated = false;
        for (auto& a : list)
        {
            if (std::abs(a.beat - safeBeat) < 1e-4)
            {
                a.localBpm = safeBpm;
                updated = true;
                break;
            }
        }
        if (!updated)
            list.push_back({ safeBeat, 0.0, safeBpm });

        reintegrateAndPersistTempoAnchors(std::move(list), firstDownbeatSec);
    }

    void updateTempoAnchor(int index, double newBeatQ, double newBpm)
    {
        auto list = getEditableTempoAnchors();
        if (index < 0 || index >= static_cast<int>(list.size()))
            return;

        list[static_cast<size_t>(index)].beat = (index == 0) ? 1.0 : juce::jmax(1.03125, newBeatQ);
        list[static_cast<size_t>(index)].localBpm = juce::jlimit(20.0, 300.0, newBpm);
        reintegrateAndPersistTempoAnchors(std::move(list), firstDownbeatSec);
    }

    void moveTempoAnchor(int index, double newBeatQ)
    {
        auto list = getEditableTempoAnchors();
        if (index <= 0 || index >= static_cast<int>(list.size()))
            return;

        list[static_cast<size_t>(index)].beat = juce::jmax(1.03125, newBeatQ);
        reintegrateAndPersistTempoAnchors(std::move(list), firstDownbeatSec);
    }

    void removeTempoAnchor(int index)
    {
        auto list = getEditableTempoAnchors();
        if (index <= 0 || index >= static_cast<int>(list.size()))
            return;

        list.erase(list.begin() + index);
        reintegrateAndPersistTempoAnchors(std::move(list), firstDownbeatSec);
    }

    // --- CRUD Meter Anchors (Cambi Metrica) ---
    void setFixedMeter(int num, int den)
    {
        const int safeNum = juce::jlimit(1, 32, num);
        const int safeDen = sanitizeDenominator(den);

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::TimeSigNum, safeNum, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, safeDen, nullptr);
            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::MeterPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }
        }
        rebuildCacheFromState();
    }

    void addOrUpdateMeterChange(double startBeatQ, int num, int den)
    {
        auto list = getMeterAnchorsCopy();
        const double safeBeat = juce::jmax(1.0, startBeatQ);
        const int safeNum = juce::jlimit(1, 32, num);
        const int safeDen = sanitizeDenominator(den);

        bool updated = false;
        for (auto& m : list)
        {
            if (std::abs(m.startBeat - safeBeat) < 1e-4)
            {
                m.num = safeNum;
                m.den = safeDen;
                updated = true;
                break;
            }
        }
        if (!updated)
            list.push_back({ safeBeat, safeNum, safeDen, 1 });

        persistMeterAnchors(std::move(list));
    }

    void updateMeterAnchor(int index, double newBeatQ, int num, int den)
    {
        auto list = getMeterAnchorsCopy();
        if (index < 0 || index >= static_cast<int>(list.size()))
            return;

        list[static_cast<size_t>(index)].startBeat = (index == 0) ? 1.0 : juce::jmax(1.03125, newBeatQ);
        list[static_cast<size_t>(index)].num = juce::jlimit(1, 32, num);
        list[static_cast<size_t>(index)].den = sanitizeDenominator(den);
        persistMeterAnchors(std::move(list));
    }

    void moveMeterAnchor(int index, double newBeatQ)
    {
        auto list = getMeterAnchorsCopy();
        if (index <= 0 || index >= static_cast<int>(list.size()))
            return;

        list[static_cast<size_t>(index)].startBeat = juce::jmax(1.03125, newBeatQ);
        persistMeterAnchors(std::move(list));
    }

    void removeMeterAnchor(int index)
    {
        auto list = getMeterAnchorsCopy();
        if (index <= 0 || index >= static_cast<int>(list.size()))
            return;

        list.erase(list.begin() + index);
        persistMeterAnchors(std::move(list));
    }

    void removeMeterChangeNear(double beatQ, double toleranceQ = 0.5)
    {
        if (!beatMapNode.isValid() || beatQ <= 1.05)
            return;

        bool removed = false;
        for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = beatMapNode.getChild(i);
            if (!child.hasType(StateKeys::MeterPointNode))
                continue;

            double ptB = static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0));
            if (ptB > 1.05 && std::abs(ptB - beatQ) <= toleranceQ)
            {
                beatMapNode.removeChild(i, nullptr);
                removed = true;
            }
        }

        if (removed)
            rebuildCacheFromState();
    }

    // Importa Tempo Map (0xFF 0x51) e cambi di Metrica (0xFF 0x58) da un file .mid (es. Reaper)
    MidiMapImportResult importMidiTempoAndMeterMap(const juce::File& midiFile, double offsetSec = 0.0)
    {
        MidiMapImportResult res;
        if (!midiFile.existsAsFile())
        {
            res.errorMessage = "File MIDI non trovato.";
            return res;
        }

        juce::FileInputStream stream(midiFile);
        if (!stream.openedOk())
        {
            res.errorMessage = "Impossibile aprire il file MIDI.";
            return res;
        }

        juce::MidiFile mf;
        if (!mf.readFrom(stream))
        {
            res.errorMessage = "Formato file MIDI non valido.";
            return res;
        }

        const short timeFormat = mf.getTimeFormat();
        if (timeFormat <= 0)
        {
            res.errorMessage = "Formato SMPTE non supportato (esporta in PPQ/Ticks da Reaper).";
            return res;
        }

        const double tpq = static_cast<double>(timeFormat);

        struct RawTempo { double tick; double bpm; };
        struct RawMeter { double tick; int num; int den; };
        std::vector<RawTempo> rawTempos;
        std::vector<RawMeter> rawMeters;
        double maxTickFound = 0.0;

        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            if (seq == nullptr) continue;

            for (int e = 0; e < seq->getNumEvents(); ++e)
            {
                const auto* holder = seq->getEventPointer(e);
                if (holder == nullptr) continue;

                const auto& msg = holder->message;
                const double tick = juce::jmax(0.0, msg.getTimeStamp());
                if (tick > maxTickFound)
                    maxTickFound = tick;

                if (msg.isTempoMetaEvent())
                {
                    const double secPerQ = msg.getTempoSecondsPerQuarterNote();
                    if (secPerQ > 0.0001)
                    {
                        double bpm = juce::jlimit(20.0, 300.0, 60.0 / secPerQ);
                        rawTempos.push_back({ tick, bpm });
                    }
                }
                else if (msg.isTimeSignatureMetaEvent())
                {
                    int num = 4, den = 4;
                    msg.getTimeSignatureInfo(num, den);
                    rawMeters.push_back({ tick, juce::jlimit(1, 32, num), sanitizeDenominator(den) });
                }
            }
        }

        auto sortDedup = [](auto& vec)
            {
                std::stable_sort(vec.begin(), vec.end(),
                    [](const auto& a, const auto& b) { return a.tick < b.tick; });
                std::decay_t<decltype(vec)> out;
                for (const auto& item : vec)
                {
                    if (!out.empty() && std::abs(out.back().tick - item.tick) < 1e-3)
                        out.back() = item;
                    else
                        out.push_back(item);
                }
                vec = std::move(out);
            };

        sortDedup(rawTempos);
        sortDedup(rawMeters);

        if (rawTempos.empty())
            rawTempos.push_back({ 0.0, baseBpm });
        else if (rawTempos.front().tick > 1e-3)
            rawTempos.insert(rawTempos.begin(), { 0.0, rawTempos.front().bpm });

        if (rawMeters.empty())
            rawMeters.push_back({ 0.0, baseTimeSigNum, baseTimeSigDen });
        else if (rawMeters.front().tick > 1e-3)
            rawMeters.insert(rawMeters.begin(), { 0.0, rawMeters.front().num, rawMeters.front().den });

        std::vector<BeatAnchor> newAnchors;
        newAnchors.reserve(rawTempos.size());

        double curTick = 0.0;
        double curSec = juce::jmax(0.0, offsetSec);
        double curBpm = rawTempos.front().bpm;

        for (const auto& rt : rawTempos)
        {
            const double deltaQ = (rt.tick - curTick) / tpq;
            curSec += deltaQ * (60.0 / curBpm);
            curTick = rt.tick;
            curBpm = rt.bpm;

            BeatAnchor a;
            a.beat = 1.0 + (curTick / tpq);
            a.timeSec = curSec;
            a.localBpm = curBpm;
            newAnchors.push_back(a);
        }

        if (beatMapNode.isValid())
        {
            beatMapNode.removeAllChildren(nullptr);
            beatMapNode.setProperty(StateKeys::BpmBase, rawTempos.front().bpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, juce::jmax(0.0, offsetSec), nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigNum, rawMeters.front().num, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, rawMeters.front().den, nullptr);

            const double lastEventBeat = 1.0 + (maxTickFound / tpq);
            const double lastAnchorBeat = newAnchors.empty() ? 1.0 : newAnchors.back().beat;
            const double mapEndBeat = juce::jmax(lastEventBeat, lastAnchorBeat) + 16.0;
            beatMapNode.setProperty(juce::Identifier("midiEndBeat"), mapEndBeat, nullptr);

            for (const auto& a : newAnchors)
            {
                juce::ValueTree bp(StateKeys::BeatPointNode);
                bp.setProperty(StateKeys::PointBeat, a.beat, nullptr);
                bp.setProperty(StateKeys::PointTimeSec, a.timeSec, nullptr);
                bp.setProperty(StateKeys::PointBpm, a.localBpm, nullptr);
                beatMapNode.addChild(bp, -1, nullptr);
            }

            for (const auto& rm : rawMeters)
            {
                juce::ValueTree mp(StateKeys::MeterPointNode);
                mp.setProperty(StateKeys::PointBeat, 1.0 + (rm.tick / tpq), nullptr);
                mp.setProperty(StateKeys::TimeSigNum, rm.num, nullptr);
                mp.setProperty(StateKeys::TimeSigDen, rm.den, nullptr);
                beatMapNode.addChild(mp, -1, nullptr);
            }
        }

        rebuildCacheFromState();

        res.success = true;
        res.tempoChangesImported = static_cast<int>(rawTempos.size());
        res.meterChangesImported = static_cast<int>(rawMeters.size());
        res.initialBpm = rawTempos.front().bpm;
        res.initialNum = rawMeters.front().num;
        res.initialDen = rawMeters.front().den;
        return res;
    }

    MetronomeState queryMetronomeState(double timeSec) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);

        MetronomeState st;
        if (timeSec < firstDownbeatSec - 1e-4)
            return st;

        const double beat = secondsToBeatUnlocked(timeSec);
        const auto pos = beatToMusicalPositionUnlocked(beat);
        const double subFloat = pos.getSubdivisionInBar();
        const int wholeSub = juce::jlimit(1, juce::jmax(1, pos.timeSigNum),
            static_cast<int>(std::floor(subFloat + 1e-6)));

        st.barIndex = pos.barIndex;
        st.subdivIndex = wholeSub;
        st.fracInSub = juce::jlimit(0.0, 0.999999, subFloat - static_cast<double>(wholeSub));
        return st;
    }

    double secondsToBeat(double timeSec) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return secondsToBeatUnlocked(timeSec);
    }

    double beatToSeconds(double beat) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return beatToSecondsUnlocked(beat);
    }

    double getBpmAtBeat(double beat) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return getBpmAtBeatUnlocked(beat);
    }

    MusicalPosition beatToMusicalPosition(double beat) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return beatToMusicalPositionUnlocked(beat);
    }

    MusicalPosition secondsToMusicalPosition(double timeSec) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return beatToMusicalPositionUnlocked(secondsToBeatUnlocked(timeSec));
    }

    // Converte una Battuta (1-based) nel Quarto assoluto (Q, 1-based).
    // Se excludeMeterIndex > 0, calcola la battuta senza quel cambio metrica (per spostarlo sul battere esatto).
    double barToBeat(int barIndex, double beatInBarQ = 0.0, int excludeMeterIndex = -1) const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        const int safeBar = juce::jmax(1, barIndex);
        const double safeSubQ = juce::jmax(0.0, beatInBarQ);

        std::vector<MeterAnchor> list = meterAnchors;
        if (list.empty())
            list.push_back({ 1.0, baseTimeSigNum, baseTimeSigDen, 1 });

        if (excludeMeterIndex > 0 && excludeMeterIndex < static_cast<int>(list.size()))
        {
            list.erase(list.begin() + excludeMeterIndex);
            int runningBar = 1;
            for (size_t i = 0; i < list.size(); ++i)
            {
                list[i].startBar = runningBar;
                if (i + 1 < list.size())
                {
                    const double spanQ = juce::jmax(0.0, list[i + 1].startBeat - list[i].startBeat);
                    const double barQ = list[i].getBarLengthQ();
                    const int barsElapsed = static_cast<int>(std::ceil((spanQ / barQ) - 1e-6));
                    runningBar += juce::jmax(1, barsElapsed);
                }
            }
        }

        auto it = std::upper_bound(list.begin(), list.end(), safeBar,
            [](int targetBar, const MeterAnchor& m) { return targetBar < m.startBar; });

        const auto& active = (it == list.begin()) ? list.front() : *(it - 1);
        const double barLenQ = active.getBarLengthQ();
        const double q = active.startBeat
            + static_cast<double>(safeBar - active.startBar) * barLenQ
            + juce::jmin(safeSubQ, juce::jmax(0.0, barLenQ - 1e-4));
        return juce::jmax(1.0, q);
    }

    static MusicalPosition makeFixedMeterPosition(double totalQuarters, int num, int den, double bpm)
    {
        const int safeNum = juce::jlimit(1, 32, num);
        const int safeDen = sanitizeDenominator(den);
        const double barLenQ = static_cast<double>(safeNum) * (4.0 / static_cast<double>(safeDen));
        const double safeQ = juce::jmax(1.0, totalQuarters);
        const double offsetQ = safeQ - 1.0;

        int barsZeroBased = static_cast<int>(std::floor((offsetQ / barLenQ) + 1e-9));
        double beatInBar = offsetQ - static_cast<double>(barsZeroBased) * barLenQ;
        if (beatInBar < 0.0) beatInBar = 0.0;

        MusicalPosition pos;
        pos.totalQuarters = safeQ;
        pos.barIndex = barsZeroBased + 1;
        pos.beatInBarQ = beatInBar;
        pos.barLengthQ = barLenQ;
        pos.timeSigNum = safeNum;
        pos.timeSigDen = safeDen;
        pos.continuousBarPos = static_cast<double>(barsZeroBased) + (beatInBar / barLenQ);
        pos.localBpm = juce::jlimit(20.0, 300.0, bpm);
        return pos;
    }

    double getBaseBpm() const { return baseBpm; }
    double getFirstDownbeatSeconds() const noexcept { return firstDownbeatSec; }
    int getBaseTimeSigNum() const { return baseTimeSigNum; }
    int getBaseTimeSigDen() const { return baseTimeSigDen; }
    const std::vector<MeterAnchor>& getMeterAnchors() const { return meterAnchors; }
    const std::vector<BeatAnchor>& getBeatAnchors() const { return anchors; }

    const std::vector<ProjectMarker>& getProjectMarkers() const { return projectMarkers; }

    // Applica Tempo Map, cambi di metrica e Marker/Region ricevuti da Reaper.
    // Restituisce true solo se i dati sono effettivamente cambiati (evita scritture e repaint inutili).
    bool applyExternalDawMap(std::vector<BeatAnchor> newTempos,
        std::vector<MeterAnchor> newMeters,
        std::vector<ProjectMarker> newMarkers,
        double projectEndBeat)
    {
        if (newTempos.empty())
            newTempos.push_back({ 1.0, 0.0, baseBpm });
        if (newMeters.empty())
            newMeters.push_back({ 1.0, baseTimeSigNum, baseTimeSigDen, 1 });

        std::sort(newTempos.begin(), newTempos.end(), [](const BeatAnchor& a, const BeatAnchor& b) {
            return a.beat < b.beat;
            });
        std::sort(newMeters.begin(), newMeters.end(), [](const MeterAnchor& a, const MeterAnchor& b) {
            return a.startBeat < b.startBeat;
            });
        std::sort(newMarkers.begin(), newMarkers.end(), [](const ProjectMarker& a, const ProjectMarker& b) {
            if (std::abs(a.beat - b.beat) > 1e-4) return a.beat < b.beat;
            return a.isRegion < b.isRegion;
            });

        const double targetEndBeat = juce::jmax(1.0, projectEndBeat);

        // Confronto diff veloce sotto lock
        bool changed = false;
        {
            const juce::SpinLock::ScopedLockType sl(cacheLock);
            if (std::abs(midiEndBeatCache - targetEndBeat) > 0.25
                || anchors.size() != newTempos.size()
                || meterAnchors.size() != newMeters.size()
                || projectMarkers.size() != newMarkers.size())
            {
                changed = true;
            }
            else
            {
                for (size_t i = 0; i < anchors.size() && !changed; ++i)
                {
                    if (std::abs(anchors[i].beat - newTempos[i].beat) > 1e-3
                        || std::abs(anchors[i].timeSec - newTempos[i].timeSec) > 1e-3
                        || std::abs(anchors[i].localBpm - newTempos[i].localBpm) > 1e-2)
                        changed = true;
                }
                for (size_t i = 0; i < meterAnchors.size() && !changed; ++i)
                {
                    if (std::abs(meterAnchors[i].startBeat - newMeters[i].startBeat) > 1e-3
                        || meterAnchors[i].num != newMeters[i].num
                        || meterAnchors[i].den != newMeters[i].den)
                        changed = true;
                }
                for (size_t i = 0; i < projectMarkers.size() && !changed; ++i)
                {
                    if (std::abs(projectMarkers[i].beat - newMarkers[i].beat) > 1e-3
                        || std::abs(projectMarkers[i].endBeat - newMarkers[i].endBeat) > 1e-3
                        || projectMarkers[i].isRegion != newMarkers[i].isRegion
                        || projectMarkers[i].indexNumber != newMarkers[i].indexNumber
                        || projectMarkers[i].name != newMarkers[i].name
                        || projectMarkers[i].colour != newMarkers[i].colour)
                        changed = true;
                }
            }
        }

        if (!changed)
            return false;

        if (beatMapNode.isValid())
        {
            beatMapNode.removeAllChildren(nullptr);
            beatMapNode.setProperty(StateKeys::BpmBase, juce::jlimit(20.0, 300.0, newTempos.front().localBpm), nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, 0.0, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigNum, juce::jlimit(1, 32, newMeters.front().num), nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, sanitizeDenominator(newMeters.front().den), nullptr);
            beatMapNode.setProperty(juce::Identifier("midiEndBeat"), targetEndBeat, nullptr);

            for (const auto& a : newTempos)
            {
                juce::ValueTree bp(StateKeys::BeatPointNode);
                bp.setProperty(StateKeys::PointBeat, juce::jmax(1.0, a.beat), nullptr);
                bp.setProperty(StateKeys::PointTimeSec, juce::jmax(0.0, a.timeSec), nullptr);
                bp.setProperty(StateKeys::PointBpm, juce::jlimit(20.0, 300.0, a.localBpm), nullptr);
                beatMapNode.addChild(bp, -1, nullptr);
            }

            for (const auto& m : newMeters)
            {
                juce::ValueTree mp(StateKeys::MeterPointNode);
                mp.setProperty(StateKeys::PointBeat, juce::jmax(1.0, m.startBeat), nullptr);
                mp.setProperty(StateKeys::TimeSigNum, juce::jlimit(1, 32, m.num), nullptr);
                mp.setProperty(StateKeys::TimeSigDen, sanitizeDenominator(m.den), nullptr);
                beatMapNode.addChild(mp, -1, nullptr);
            }

            for (const auto& pm : newMarkers)
            {
                juce::ValueTree mk(kProjectMarkerNode);
                mk.setProperty("pointBeat", juce::jmax(1.0, pm.beat), nullptr);
                mk.setProperty("endBeat", juce::jmax(pm.beat, pm.endBeat), nullptr);
                mk.setProperty("pointTimeSec", juce::jmax(0.0, pm.timeSec), nullptr);
                mk.setProperty("isRegion", pm.isRegion, nullptr);
                mk.setProperty("markerIndex", pm.indexNumber, nullptr);
                mk.setProperty("markerName", pm.name, nullptr);
                mk.setProperty("markerColour", pm.colour.toString(), nullptr);
                beatMapNode.addChild(mk, -1, nullptr);
            }
        }

        rebuildCacheFromState();
        return true;
    }

    bool hasVariableTempoMap() const noexcept { return anchors.size() > 1; }
    bool hasImportedMidiMap() const noexcept { return !anchors.empty() || midiEndBeatCache > 1.1; }

    double getMapEndBeat() const noexcept
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        return getMapEndBeatUnlocked();
    }

    double getMapEndSeconds() const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        const double endB = getMapEndBeatUnlocked();
        return (endB > 1.1) ? beatToSecondsUnlocked(endB) : 0.0;
    }

    static int sanitizeDenominator(int den) noexcept
    {
        if (den == 2 || den == 4 || den == 8 || den == 16) return den;
        return 4;
    }

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
    std::vector<MeterAnchor> getMeterAnchorsCopy() const
    {
        const juce::SpinLock::ScopedLockType sl(cacheLock);
        if (!meterAnchors.empty())
            return meterAnchors;
        return { { 1.0, baseTimeSigNum, baseTimeSigDen, 1 } };
    }

    void reintegrateAndPersistTempoAnchors(std::vector<BeatAnchor> list, double downbeatSec)
    {
        std::sort(list.begin(), list.end(), [](const BeatAnchor& a, const BeatAnchor& b) {
            return a.beat < b.beat;
            });

        std::vector<BeatAnchor> clean;
        for (const auto& item : list)
        {
            if (!clean.empty() && std::abs(clean.back().beat - item.beat) < 1e-4)
                clean.back().localBpm = item.localBpm;
            else
                clean.push_back(item);
        }

        if (clean.empty())
            clean.push_back({ 1.0, downbeatSec, baseBpm });
        else if (clean.front().beat > 1.0001)
            clean.insert(clean.begin(), { 1.0, downbeatSec, clean.front().localBpm });

        double curSec = juce::jmax(0.0, downbeatSec);
        clean[0].beat = 1.0;
        clean[0].timeSec = curSec;
        clean[0].localBpm = juce::jlimit(20.0, 300.0, clean[0].localBpm);

        for (size_t i = 1; i < clean.size(); ++i)
        {
            clean[i].localBpm = juce::jlimit(20.0, 300.0, clean[i].localBpm);
            const double deltaQ = juce::jmax(0.0, clean[i].beat - clean[i - 1].beat);
            curSec += deltaQ * (60.0 / clean[i - 1].localBpm);
            clean[i].timeSec = curSec;
        }

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::BpmBase, clean[0].localBpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, juce::jmax(0.0, downbeatSec), nullptr);

            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::BeatPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }

            for (const auto& a : clean)
            {
                juce::ValueTree bp(StateKeys::BeatPointNode);
                bp.setProperty(StateKeys::PointBeat, a.beat, nullptr);
                bp.setProperty(StateKeys::PointTimeSec, a.timeSec, nullptr);
                bp.setProperty(StateKeys::PointBpm, a.localBpm, nullptr);
                beatMapNode.addChild(bp, -1, nullptr);
            }
        }

        rebuildCacheFromState();
    }

    void persistMeterAnchors(std::vector<MeterAnchor> list)
    {
        std::sort(list.begin(), list.end(), [](const MeterAnchor& a, const MeterAnchor& b) {
            return a.startBeat < b.startBeat;
            });

        std::vector<MeterAnchor> clean;
        for (const auto& m : list)
        {
            if (!clean.empty() && std::abs(clean.back().startBeat - m.startBeat) < 1e-4)
                clean.back() = m;
            else
                clean.push_back(m);
        }

        if (clean.empty())
            clean.push_back({ 1.0, baseTimeSigNum, baseTimeSigDen, 1 });
        else if (clean.front().startBeat > 1.0001)
            clean.insert(clean.begin(), { 1.0, baseTimeSigNum, baseTimeSigDen, 1 });

        clean[0].startBeat = 1.0;

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::TimeSigNum, clean[0].num, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, clean[0].den, nullptr);

            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::MeterPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }

            for (const auto& m : clean)
            {
                juce::ValueTree mp(StateKeys::MeterPointNode);
                mp.setProperty(StateKeys::PointBeat, m.startBeat, nullptr);
                mp.setProperty(StateKeys::TimeSigNum, m.num, nullptr);
                mp.setProperty(StateKeys::TimeSigDen, m.den, nullptr);
                beatMapNode.addChild(mp, -1, nullptr);
            }
        }

        rebuildCacheFromState();
    }

    double secondsToBeatUnlocked(double timeSec) const
    {
        if (anchors.size() < 2)
        {
            double deltaSec = timeSec - firstDownbeatSec;
            double beat = 1.0 + (deltaSec * (baseBpm / 60.0));
            return juce::jmax(1.0, beat);
        }

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

        auto it = std::upper_bound(anchors.begin(), anchors.end(), timeSec,
            [](double val, const BeatAnchor& a) { return val < a.timeSec; });

        const auto& p1 = *it;
        const auto& p0 = *(it - 1);

        double spanSec = p1.timeSec - p0.timeSec;
        if (spanSec <= 0.00001) return p0.beat;

        double fraction = (timeSec - p0.timeSec) / spanSec;
        return p0.beat + fraction * (p1.beat - p0.beat);
    }

    double beatToSecondsUnlocked(double beat) const
    {
        double safeBeat = juce::jmax(1.0, beat);

        if (anchors.size() < 2)
            return firstDownbeatSec + ((safeBeat - 1.0) * (60.0 / baseBpm));

        if (safeBeat <= anchors.front().beat)
            return juce::jmax(0.0, anchors.front().timeSec - ((anchors.front().beat - safeBeat) * (60.0 / anchors.front().localBpm)));

        if (safeBeat >= anchors.back().beat)
            return anchors.back().timeSec + ((safeBeat - anchors.back().beat) * (60.0 / anchors.back().localBpm));

        auto it = std::upper_bound(anchors.begin(), anchors.end(), safeBeat,
            [](double val, const BeatAnchor& a) { return val < a.beat; });

        const auto& p1 = *it;
        const auto& p0 = *(it - 1);

        double spanBeat = p1.beat - p0.beat;
        if (spanBeat <= 0.00001) return p0.timeSec;

        double fraction = (safeBeat - p0.beat) / spanBeat;
        return p0.timeSec + fraction * (p1.timeSec - p0.timeSec);
    }

    double getBpmAtBeatUnlocked(double beat) const
    {
        if (anchors.empty())
            return baseBpm;

        if (beat <= anchors.front().beat)
            return anchors.front().localBpm;

        auto it = std::upper_bound(anchors.begin(), anchors.end(), beat,
            [](double val, const BeatAnchor& a) { return val < a.beat; });

        return (it - 1)->localBpm;
    }

    MusicalPosition beatToMusicalPositionUnlocked(double beat) const
    {
        double safeBeat = juce::jmax(1.0, beat);
        double bpmNow = getBpmAtBeatUnlocked(safeBeat);

        if (meterAnchors.empty())
            return makeFixedMeterPosition(safeBeat, baseTimeSigNum, baseTimeSigDen, bpmNow);

        auto it = std::upper_bound(meterAnchors.begin(), meterAnchors.end(), safeBeat,
            [](double val, const MeterAnchor& m) { return val < m.startBeat; });

        const auto& active = (it == meterAnchors.begin()) ? meterAnchors.front() : *(it - 1);
        double barLenQ = active.getBarLengthQ();
        double offsetQ = juce::jmax(0.0, safeBeat - active.startBeat);

        int barsAfterAnchor = static_cast<int>(std::floor((offsetQ / barLenQ) + 1e-9));
        double beatInBar = offsetQ - static_cast<double>(barsAfterAnchor) * barLenQ;
        if (beatInBar < 0.0) beatInBar = 0.0;

        MusicalPosition pos;
        pos.totalQuarters = safeBeat;
        pos.barIndex = active.startBar + barsAfterAnchor;
        pos.beatInBarQ = beatInBar;
        pos.barLengthQ = barLenQ;
        pos.timeSigNum = active.num;
        pos.timeSigDen = active.den;
        pos.continuousBarPos = static_cast<double>(pos.barIndex - 1) + (beatInBar / barLenQ);
        pos.localBpm = bpmNow;
        return pos;
    }

    double getMapEndBeatUnlocked() const noexcept
    {
        double endB = midiEndBeatCache;
        if (!anchors.empty())
            endB = juce::jmax(endB, anchors.back().beat + 16.0);
        if (!meterAnchors.empty())
            endB = juce::jmax(endB, meterAnchors.back().startBeat + meterAnchors.back().getBarLengthQ() * 4.0);
        return juce::jmax(1.0, endB);
    }

    void finalizeMeterAnchors()
    {
        std::sort(meterAnchors.begin(), meterAnchors.end(), [](const MeterAnchor& a, const MeterAnchor& b) {
            return a.startBeat < b.startBeat;
            });

        std::vector<MeterAnchor> deduped;
        for (const auto& m : meterAnchors)
        {
            if (!deduped.empty() && std::abs(deduped.back().startBeat - m.startBeat) < 1e-4)
                deduped.back() = m;
            else
                deduped.push_back(m);
        }
        meterAnchors = std::move(deduped);

        if (meterAnchors.empty() || meterAnchors.front().startBeat > 1.0001)
        {
            MeterAnchor def;
            def.startBeat = 1.0;
            def.num = baseTimeSigNum;
            def.den = baseTimeSigDen;
            meterAnchors.insert(meterAnchors.begin(), def);
        }

        int runningBar = 1;
        for (size_t i = 0; i < meterAnchors.size(); ++i)
        {
            meterAnchors[i].startBar = runningBar;
            if (i + 1 < meterAnchors.size())
            {
                double spanQ = juce::jmax(0.0, meterAnchors[i + 1].startBeat - meterAnchors[i].startBeat);
                double barQ = meterAnchors[i].getBarLengthQ();
                int barsElapsed = static_cast<int>(std::ceil((spanQ / barQ) - 1e-6));
                runningBar += juce::jmax(1, barsElapsed);
            }
        }
    }

    mutable juce::SpinLock cacheLock;
    juce::ValueTree beatMapNode;
    double baseBpm = 120.0;
    double firstDownbeatSec = 0.0;
    int baseTimeSigNum = 4;
    int baseTimeSigDen = 4;
    double midiEndBeatCache = 1.0;
    std::vector<BeatAnchor> anchors;
    std::vector<MeterAnchor> meterAnchors;

    std::vector<ProjectMarker> projectMarkers;
};