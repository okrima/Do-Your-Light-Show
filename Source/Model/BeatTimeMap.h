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
        int den = 4;            // Denominatore (es. 4, 8, 16)
        int startBar = 1;       // Numero di battuta calcolato automaticamente (1-based)

        double getBarLengthQ() const noexcept
        {
            const int safeNum = juce::jmax(1, num);
            const int safeDen = juce::jmax(1, den);
            return static_cast<double>(safeNum) * (4.0 / static_cast<double>(safeDen));
        }
    };

    struct MusicalPosition
    {
        double totalQuarters = 1.0;    // Posizione assoluta in Quarti (1-based, standard MIDI/Timeline)
        int    barIndex = 1;           // Numero battuta reale (1-based)
        double beatInBarQ = 0.0;       // Quarti trascorsi dall'ultimo battere [0 .. barLengthQ)
        double barLengthQ = 4.0;       // Lunghezza della misura corrente in Quarti
        int    timeSigNum = 4;
        int    timeSigDen = 4;
        double continuousBarPos = 0.0; // (barIndex - 1) + (beatInBarQ / barLengthQ)
        double localBpm = 120.0;

        // Indice del movimento nella metrica corrente (es. 1..4 in 4/4, 1..6 in 6/8, 1..7 in 7/8)
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
            beatMapNode.setProperty(StateKeys::TimeSigNum, 4, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, 4, nullptr);
            rootState.addChild(beatMapNode, -1, nullptr);
        }

        rebuildCacheFromState();
    }

    // Ricostruisce i vettori in RAM per letture a 60 FPS nella Mini-DAW e nel Simulatore
    void rebuildCacheFromState()
    {
        anchors.clear();
        meterAnchors.clear();
        if (!beatMapNode.isValid()) return;

        baseBpm = static_cast<double>(beatMapNode.getProperty(StateKeys::BpmBase, 120.0));
        if (baseBpm <= 10.0) baseBpm = 120.0;

        firstDownbeatSec = static_cast<double>(beatMapNode.getProperty(StateKeys::FirstDownbeatSec, 0.0));
        if (firstDownbeatSec < 0.0) firstDownbeatSec = 0.0;

        baseTimeSigNum = juce::jlimit(1, 32, static_cast<int>(beatMapNode.getProperty(StateKeys::TimeSigNum, 4)));
        baseTimeSigDen = sanitizeDenominator(static_cast<int>(beatMapNode.getProperty(StateKeys::TimeSigDen, 4)));

        const int numChildren = beatMapNode.getNumChildren();
        anchors.reserve(static_cast<size_t>(numChildren));

        for (int i = 0; i < numChildren; ++i)
        {
            auto child = beatMapNode.getChild(i);
            if (!child.isValid()) continue;

            if (child.hasType(StateKeys::BeatPointNode))
            {
                BeatAnchor a;
                a.beat = static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0));
                a.timeSec = static_cast<double>(child.getProperty(StateKeys::PointTimeSec, 0.0));
                a.localBpm = static_cast<double>(child.getProperty(StateKeys::PointBpm, baseBpm));
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
        }

        std::sort(anchors.begin(), anchors.end(), [](const BeatAnchor& a, const BeatAnchor& b) {
            return a.timeSec < b.timeSec;
            });

        finalizeMeterAnchors();
    }

    // Imposta un BPM fisso (rimuove solo i BEAT_POINT, preserva i METER_POINT)
    void setFixedBpm(double newBpm, double downbeatOffsetSec = 0.0)
    {
        baseBpm = juce::jlimit(20.0, 300.0, newBpm);
        firstDownbeatSec = juce::jmax(0.0, downbeatOffsetSec);

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::BpmBase, baseBpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, firstDownbeatSec, nullptr);
            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::BeatPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }
        }
        anchors.clear();
    }

    // Imposta una metrica fissa per tutto il brano
    void setFixedMeter(int num, int den)
    {
        baseTimeSigNum = juce::jlimit(1, 32, num);
        baseTimeSigDen = sanitizeDenominator(den);

        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::TimeSigNum, baseTimeSigNum, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, baseTimeSigDen, nullptr);
            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                if (beatMapNode.getChild(i).hasType(StateKeys::MeterPointNode))
                    beatMapNode.removeChild(i, nullptr);
            }
        }
        meterAnchors.clear();
        finalizeMeterAnchors();
    }

    // Aggiunge o aggiorna un cambio di metrica al quarto indicato (1-based)
    void addOrUpdateMeterChange(double startBeatQ, int num, int den)
    {
        double safeBeat = juce::jmax(1.0, startBeatQ);
        int safeNum = juce::jlimit(1, 32, num);
        int safeDen = sanitizeDenominator(den);

        if (beatMapNode.isValid())
        {
            for (int i = beatMapNode.getNumChildren() - 1; i >= 0; --i)
            {
                auto child = beatMapNode.getChild(i);
                if (child.hasType(StateKeys::MeterPointNode) &&
                    std::abs(static_cast<double>(child.getProperty(StateKeys::PointBeat, 1.0)) - safeBeat) < 1e-4)
                {
                    beatMapNode.removeChild(i, nullptr);
                }
            }

            juce::ValueTree mp(StateKeys::MeterPointNode);
            mp.setProperty(StateKeys::PointBeat, safeBeat, nullptr);
            mp.setProperty(StateKeys::TimeSigNum, safeNum, nullptr);
            mp.setProperty(StateKeys::TimeSigDen, safeDen, nullptr);
            beatMapNode.addChild(mp, -1, nullptr);

            if (safeBeat <= 1.0001)
            {
                beatMapNode.setProperty(StateKeys::TimeSigNum, safeNum, nullptr);
                beatMapNode.setProperty(StateKeys::TimeSigDen, safeDen, nullptr);
            }
        }

        rebuildCacheFromState();
    }

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

        // Ordina e deduplica eventuali eventi sullo stesso tick (tiene l'ultimo)
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

        // Integra i tempi in secondi con precisione analitica tra un cambio BPM e il successivo
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

        // Scrive nel ValueTree globale (cosi' si salva col progetto e aggiorna Timeline + Export)
        if (beatMapNode.isValid())
        {
            beatMapNode.removeAllChildren(nullptr);
            beatMapNode.setProperty(StateKeys::BpmBase, rawTempos.front().bpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, juce::jmax(0.0, offsetSec), nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigNum, rawMeters.front().num, nullptr);
            beatMapNode.setProperty(StateKeys::TimeSigDen, rawMeters.front().den, nullptr);

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

    // Converte i SECONDI dell'audio nel QUARTO DECIMALE (es. 1.25)
    double secondsToBeat(double timeSec) const
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

    // Converte il QUARTO DECIMALE (es. 14.50) nei SECONDI esatti dell'audio
    double beatToSeconds(double beat) const
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

    double getBpmAtBeat(double beat) const
    {
        if (anchors.empty())
            return baseBpm;

        if (beat <= anchors.front().beat)
            return anchors.front().localBpm;

        auto it = std::upper_bound(anchors.begin(), anchors.end(), beat,
            [](double val, const BeatAnchor& a) { return val < a.beat; });

        return (it - 1)->localBpm;
    }

    // Converte un quarto assoluto nella posizione musicale completa (battuta, metrica, battito locale)
    MusicalPosition beatToMusicalPosition(double beat) const
    {
        double safeBeat = juce::jmax(1.0, beat);
        double bpmNow = getBpmAtBeat(safeBeat);

        if (meterAnchors.empty())
            return makeFixedMeterPosition(safeBeat, baseTimeSigNum, baseTimeSigDen, bpmNow);

        // Trova l'ultimo cambio di metrica con startBeat <= safeBeat
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

    MusicalPosition secondsToMusicalPosition(double timeSec) const
    {
        return beatToMusicalPosition(secondsToBeat(timeSec));
    }

    // Helper statico per la Preview Standalone nello Scene Builder
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
    int getBaseTimeSigNum() const { return baseTimeSigNum; }
    int getBaseTimeSigDen() const { return baseTimeSigDen; }
    const std::vector<MeterAnchor>& getMeterAnchors() const { return meterAnchors; }

    static int sanitizeDenominator(int den) noexcept
    {
        if (den == 2 || den == 4 || den == 8 || den == 16) return den;
        return 4;
    }

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
    void finalizeMeterAnchors()
    {
        std::sort(meterAnchors.begin(), meterAnchors.end(), [](const MeterAnchor& a, const MeterAnchor& b) {
            return a.startBeat < b.startBeat;
            });

        // Deduplica cambi di metrica sullo stesso quarto (mantiene l'ultimo inserito)
        std::vector<MeterAnchor> deduped;
        for (const auto& m : meterAnchors)
        {
            if (!deduped.empty() && std::abs(deduped.back().startBeat - m.startBeat) < 1e-4)
                deduped.back() = m;
            else
                deduped.push_back(m);
        }
        meterAnchors = std::move(deduped);

        // Garantisce sempre una metrica di partenza al quarto 1.0
        if (meterAnchors.empty() || meterAnchors.front().startBeat > 1.0001)
        {
            MeterAnchor def;
            def.startBeat = 1.0;
            def.num = baseTimeSigNum;
            def.den = baseTimeSigDen;
            meterAnchors.insert(meterAnchors.begin(), def);
        }

        // Pre-calcola il numero di battuta di ogni cambio metrica
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

    juce::ValueTree beatMapNode;
    double baseBpm = 120.0;
    double firstDownbeatSec = 0.0;
    int baseTimeSigNum = 4;
    int baseTimeSigDen = 4;
    std::vector<BeatAnchor> anchors;
    std::vector<MeterAnchor> meterAnchors;
};