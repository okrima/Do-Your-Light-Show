#pragma once
#include <JuceHeader.h>
#include "../Model/DataStateKeys.h"
#include "../Model/BeatTimeMap.h"
#include "../Audio/StemAudioEngine.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

class AudioAnalysisEngine
{
public:
    enum class SectionMood
    {
        Calm,
        BuildUp,
        HighEnergy,
        Climax,
        Outro
    };

    struct BeatFeature
    {
        double beat = 1.0;
        double timeSec = 0.0;
        float fullMixRms = 0.0f;
        float brightness = 0.0f;
        float kickEnergy = 0.0f;
        float snareEnergy = 0.0f;
        float bassEnergy = 0.0f;
        float guitarEnergy = 0.0f;
    };

    struct SongSection
    {
        juce::String name;
        SectionMood mood = SectionMood::Calm;
        double startBeat = 1.0;
        double durationBeats = 16.0;
        float avgEnergy = 0.0f;
        float avgBrightness = 0.0f;
        float kickDensity = 0.0f;
        float snareDensity = 0.0f;
        float bassLevel = 0.0f;
        float guitarLevel = 0.0f;
    };

    struct AnalysisResult
    {
        bool success = false;
        bool usedClickTrack = false;
        double detectedBpm = 120.0;
        double firstDownbeatSec = 0.0;
        double totalDurationSec = 0.0;
        double totalBeats = 0.0;
        std::vector<BeatTimeMap::BeatAnchor> anchors;
        std::vector<BeatFeature> beats;
        std::vector<SongSection> sections;
    };

    struct StemPathsInput
    {
        juce::File fullMixFile;
        juce::File clickFile;
        juce::File drumsFile;
        juce::File bassFile;
        juce::File guitarsFile;
        double fallbackBpm = 120.0;
    };

    // --- TT-4.1: Thread in background con Barra di Avanzamento e tasto Annulla ---
    class AnalysisJob : public juce::ThreadWithProgressWindow
    {
    public:
        AnalysisJob(const juce::ValueTree& appState,
            StemAudioEngine& engine,
            std::function<void(const AnalysisResult&)> onComplete)
            : juce::ThreadWithProgressWindow("Analisi Audio AI in corso...", true, true, 10000, "Annulla"),
            audioEngine(engine),
            completionCallback(std::move(onComplete))
        {
            // Estrae i percorsi file in modo sicuro sul Message Thread prima di partire
            auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
            {
                input.fullMixFile = juce::File(stemsNode.getProperty(StateKeys::StemFullMix, "").toString());
                input.clickFile = juce::File(stemsNode.getProperty(StateKeys::StemClick, "").toString());
                input.drumsFile = juce::File(stemsNode.getProperty(StateKeys::StemDrums, "").toString());
                input.bassFile = juce::File(stemsNode.getProperty(StateKeys::StemBass, "").toString());
                input.guitarsFile = juce::File(stemsNode.getProperty(StateKeys::StemGuitars, "").toString());
            }

            auto beatMapNode = appState.getChildWithName(StateKeys::BeatMapNode);
            if (beatMapNode.isValid())
                input.fallbackBpm = static_cast<double>(beatMapNode.getProperty(StateKeys::BpmBase, 120.0));

            setStatusMessage("Preparazione lettori audio...");
            setProgress(0.0);
        }

        void run() override
        {
            auto progressFn = [this](double p, const juce::String& msg) -> bool {
                if (threadShouldExit())
                    return false;
                setProgress(juce::jlimit(0.0, 1.0, p));
                if (msg.isNotEmpty())
                    setStatusMessage(msg);
                return !threadShouldExit();
                };

            result = AudioAnalysisEngine::runOfflineAnalysis(input, audioEngine.getFormatManager(), progressFn);
        }

        // Chiamato automaticamente da JUCE sul Message Thread al termine di run()
        void threadComplete(bool userPressedCancel) override
        {
            if (!userPressedCancel && completionCallback)
                completionCallback(result);
        }

    private:
        StemAudioEngine& audioEngine;
        StemPathsInput input;
        AnalysisResult result;
        std::function<void(const AnalysisResult&)> completionCallback;
    };

    // Scrive i risultati estratti dentro BEAT_MAP (da chiamare sul Message Thread)
    static void applyResultToState(const AnalysisResult& result,
        juce::ValueTree& appState,
        BeatTimeMap& beatTimeMap)
    {
        auto beatMapNode = appState.getChildWithName(StateKeys::BeatMapNode);
        if (beatMapNode.isValid())
        {
            beatMapNode.setProperty(StateKeys::BpmBase, result.detectedBpm, nullptr);
            beatMapNode.setProperty(StateKeys::FirstDownbeatSec, result.firstDownbeatSec, nullptr);
            beatMapNode.removeAllChildren(nullptr);

            for (const auto& a : result.anchors)
            {
                juce::ValueTree pt(StateKeys::BeatPointNode);
                pt.setProperty(StateKeys::PointBeat, a.beat, nullptr);
                pt.setProperty(StateKeys::PointTimeSec, a.timeSec, nullptr);
                pt.setProperty(StateKeys::PointBpm, a.localBpm, nullptr);
                beatMapNode.addChild(pt, -1, nullptr);
            }
        }
        beatTimeMap.bindToAppState(appState);
    }

private:
    using ProgressCallback = std::function<bool(double, const juce::String&)>;

    static std::unique_ptr<juce::AudioFormatReader> openReader(juce::AudioFormatManager& fm, const juce::File& f)
    {
        if (!f.existsAsFile()) return nullptr;
        return std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(f));
    }

    static AnalysisResult runOfflineAnalysis(const StemPathsInput& input,
        juce::AudioFormatManager& formatManager,
        const ProgressCallback& reportProgress)
    {
        AnalysisResult result;
        result.detectedBpm = input.fallbackBpm;

        auto fullMixReader = openReader(formatManager, input.fullMixFile);
        if (fullMixReader == nullptr || fullMixReader->sampleRate <= 0.0 || fullMixReader->lengthInSamples <= 0)
            return result;

        result.totalDurationSec = static_cast<double>(fullMixReader->lengthInSamples) / fullMixReader->sampleRate;

        // 1. Fase Cadenza / BPM (0% -> 25%)
        auto clickReader = openReader(formatManager, input.clickFile);
        if (clickReader != nullptr && clickReader->lengthInSamples > 0)
        {
            if (!analyzeClickTrack(*clickReader, result, reportProgress))
                return result;
        }
        else
        {
            if (!estimateTempoFromFullMix(*fullMixReader, result, reportProgress))
                return result;
        }

        // Costruisce una BeatTimeMap temporanea locale al thread per convertire secondi <-> quarti
        juce::ValueTree localRoot("TEMP_ROOT");
        juce::ValueTree localBeatMapNode(StateKeys::BeatMapNode);
        localBeatMapNode.setProperty(StateKeys::BpmBase, result.detectedBpm, nullptr);
        localBeatMapNode.setProperty(StateKeys::FirstDownbeatSec, result.firstDownbeatSec, nullptr);
        for (const auto& a : result.anchors)
        {
            juce::ValueTree pt(StateKeys::BeatPointNode);
            pt.setProperty(StateKeys::PointBeat, a.beat, nullptr);
            pt.setProperty(StateKeys::PointTimeSec, a.timeSec, nullptr);
            pt.setProperty(StateKeys::PointBpm, a.localBpm, nullptr);
            localBeatMapNode.addChild(pt, -1, nullptr);
        }
        localRoot.addChild(localBeatMapNode, -1, nullptr);

        BeatTimeMap localBeatMap;
        localBeatMap.bindToAppState(localRoot);
        result.totalBeats = localBeatMap.secondsToBeat(result.totalDurationSec);

        // 2. Fase Estrazione Feature per ogni quarto (25% -> 92%)
        auto drumsReader = openReader(formatManager, input.drumsFile);
        auto bassReader = openReader(formatManager, input.bassFile);
        auto guitarsReader = openReader(formatManager, input.guitarsFile);

        if (!extractBeatFeatures(*fullMixReader,
            drumsReader.get(),
            bassReader.get(),
            guitarsReader.get(),
            localBeatMap,
            result,
            reportProgress))
        {
            return result;
        }

        // 3. Fase Segmentazione Musicale (92% -> 100%)
        if (!reportProgress(0.94, "Segmentazione sezioni musicali e mood..."))
            return result;

        segmentSongIntoSections(result);
        reportProgress(1.0, "Analisi completata!");

        result.success = !result.sections.empty();
        return result;
    }

    // --- TT-4.2A: Rilevamento picchi su traccia Click ---
    static bool analyzeClickTrack(juce::AudioFormatReader& clickReader,
        AnalysisResult& result,
        const ProgressCallback& reportProgress)
    {
        if (!reportProgress(0.02, "Analisi traccia Click per griglia BPM..."))
            return false;

        const double sr = clickReader.sampleRate;
        const juce::int64 totalSamples = clickReader.lengthInSamples;
        const int blockSize = 4096;

        juce::AudioBuffer<float> buffer(2, blockSize);
        std::vector<double> onsetTimesSec;

        const double minPeakDistanceSec = 0.18;
        double lastPeakSec = -1.0;
        float runningPeak = 0.05f;
        int blockCounter = 0;

        for (juce::int64 pos = 0; pos < totalSamples; pos += blockSize)
        {
            if ((++blockCounter % 64) == 0)
            {
                double prog = 0.02 + 0.22 * (static_cast<double>(pos) / static_cast<double>(totalSamples));
                if (!reportProgress(prog, ""))
                    return false;
            }

            int numToRead = static_cast<int>(juce::jmin<juce::int64>(blockSize, totalSamples - pos));
            clickReader.read(&buffer, 0, numToRead, pos, true, true);
            const float* data = buffer.getReadPointer(0);

            for (int i = 0; i < numToRead; ++i)
            {
                float absSample = std::abs(data[i]);
                if (absSample > runningPeak)
                    runningPeak = absSample;
                else
                    runningPeak = runningPeak * 0.99995f + 0.00005f * 0.05f;

                float threshold = juce::jmax(0.08f, runningPeak * 0.45f);
                if (absSample > threshold)
                {
                    double currentSec = static_cast<double>(pos + i) / sr;
                    if (currentSec - lastPeakSec >= minPeakDistanceSec)
                    {
                        onsetTimesSec.push_back(currentSec);
                        lastPeakSec = currentSec;
                    }
                }
            }
        }

        if (onsetTimesSec.size() >= 8)
        {
            std::vector<double> intervals;
            intervals.reserve(onsetTimesSec.size() - 1);
            for (size_t i = 1; i < onsetTimesSec.size(); ++i)
            {
                double dt = onsetTimesSec[i] - onsetTimesSec[i - 1];
                if (dt >= 0.22 && dt <= 1.2)
                    intervals.push_back(dt);
            }

            if (!intervals.empty())
            {
                std::sort(intervals.begin(), intervals.end());
                double medianInterval = intervals[intervals.size() / 2];
                double bpm = 60.0 / medianInterval;

                while (bpm < 70.0)  bpm *= 2.0;
                while (bpm > 210.0) bpm *= 0.5;

                result.detectedBpm = std::round(bpm * 2.0) / 2.0;
                result.firstDownbeatSec = onsetTimesSec.front();
                result.usedClickTrack = true;

                for (size_t i = 0; i < onsetTimesSec.size(); i += 4)
                {
                    double localBpm = result.detectedBpm;
                    if (i + 4 < onsetTimesSec.size())
                    {
                        double spanSec = onsetTimesSec[i + 4] - onsetTimesSec[i];
                        if (spanSec > 0.5)
                            localBpm = juce::jlimit(50.0, 250.0, (4.0 * 60.0) / spanSec);
                    }
                    result.anchors.push_back({ static_cast<double>(i + 1), onsetTimesSec[i], localBpm });
                }
            }
        }
        return true;
    }

    // --- TT-4.2B: Stima BPM dal Full Mix ---
    static bool estimateTempoFromFullMix(juce::AudioFormatReader& fullMixReader,
        AnalysisResult& result,
        const ProgressCallback& reportProgress)
    {
        if (!reportProgress(0.02, "Stima cadenza e BPM dal Full Mix..."))
            return false;

        const double sr = fullMixReader.sampleRate;
        const int hopSize = static_cast<int>(sr * 0.02);
        const juce::int64 maxSamplesToScan = juce::jmin<juce::int64>(
            fullMixReader.lengthInSamples, static_cast<juce::int64>(sr * 90.0));

        juce::AudioBuffer<float> buffer(2, hopSize);
        std::vector<double> onsetTimes;
        float prevEnergy = 0.0f;
        float avgFlux = 0.01f;
        double lastOnsetSec = -1.0;
        int stepCount = 0;

        for (juce::int64 pos = 0; pos + hopSize < maxSamplesToScan; pos += hopSize)
        {
            if ((++stepCount % 100) == 0)
            {
                double prog = 0.02 + 0.22 * (static_cast<double>(pos) / static_cast<double>(maxSamplesToScan));
                if (!reportProgress(prog, ""))
                    return false;
            }

            fullMixReader.read(&buffer, 0, hopSize, pos, true, true);
            float rms = (buffer.getRMSLevel(0, 0, hopSize) + buffer.getRMSLevel(1, 0, hopSize)) * 0.5f;
            float flux = juce::jmax(0.0f, rms - prevEnergy);
            prevEnergy = rms;

            avgFlux = avgFlux * 0.96f + flux * 0.04f;
            double sec = static_cast<double>(pos) / sr;

            if (flux > avgFlux * 1.8f && rms > 0.03f && (sec - lastOnsetSec) >= 0.24)
            {
                onsetTimes.push_back(sec);
                lastOnsetSec = sec;
            }
        }

        if (onsetTimes.size() >= 10)
        {
            result.firstDownbeatSec = onsetTimes.front();
            std::vector<int> bpmBins(220, 0);
            for (size_t i = 1; i < onsetTimes.size(); ++i)
            {
                double dt = onsetTimes[i] - onsetTimes[i - 1];
                if (dt > 0.25 && dt < 1.5)
                {
                    double candBpm = 60.0 / dt;
                    while (candBpm < 80.0)  candBpm *= 2.0;
                    while (candBpm > 190.0) candBpm *= 0.5;
                    int bin = juce::jlimit(60, 210, static_cast<int>(std::round(candBpm)));
                    bpmBins[static_cast<size_t>(bin)] += 2;
                    bpmBins[static_cast<size_t>(bin - 1)] += 1;
                    bpmBins[static_cast<size_t>(bin + 1)] += 1;
                }
            }

            auto maxIt = std::max_element(bpmBins.begin() + 70, bpmBins.begin() + 200);
            if (*maxIt >= 6)
                result.detectedBpm = static_cast<double>(std::distance(bpmBins.begin(), maxIt));
        }

        result.usedClickTrack = false;
        return true;
    }

    // --- TT-4.3 & TT-4.4: Analisi spettrale e ritmica quarto per quarto ---
    static bool extractBeatFeatures(juce::AudioFormatReader& fullMixReader,
        juce::AudioFormatReader* drumsReader,
        juce::AudioFormatReader* bassReader,
        juce::AudioFormatReader* guitarsReader,
        const BeatTimeMap& beatTimeMap,
        AnalysisResult& result,
        const ProgressCallback& reportProgress)
    {
        const int numBeats = juce::jmax(1, static_cast<int>(std::floor(result.totalBeats)));
        result.beats.reserve(static_cast<size_t>(numBeats));

        const double mixSr = fullMixReader.sampleRate;
        juce::AudioBuffer<float> mixBuf;
        juce::AudioBuffer<float> stemBuf;

        float maxRms = 0.001f, maxBright = 0.001f;
        float maxKick = 0.001f, maxSnare = 0.001f, maxBass = 0.001f, maxGuit = 0.001f;

        for (int b = 1; b <= numBeats; ++b)
        {
            if ((b % 8) == 0 || b == 1)
            {
                double prog = 0.25 + 0.67 * (static_cast<double>(b) / static_cast<double>(numBeats));
                juce::String msg = "Analisi timbrica e ritmica: quarto " + juce::String(b) + " di " + juce::String(numBeats);
                if (!reportProgress(prog, msg))
                    return false;
            }

            double startSec = beatTimeMap.beatToSeconds(static_cast<double>(b));
            double endSec = beatTimeMap.beatToSeconds(static_cast<double>(b + 1));
            double durSec = juce::jmax(0.05, endSec - startSec);

            BeatFeature bf;
            bf.beat = static_cast<double>(b);
            bf.timeSec = startSec;

            // 1. Analisi Full Mix (2 canali)
            int mixSamples = static_cast<int>(durSec * mixSr);
            juce::int64 mixStart = static_cast<juce::int64>(startSec * mixSr);
            if (mixStart + mixSamples <= fullMixReader.lengthInSamples && mixSamples > 0)
            {
                mixBuf.setSize(2, mixSamples, false, false, true);
                fullMixReader.read(&mixBuf, 0, mixSamples, mixStart, true, true);
                const float* dataL = mixBuf.getReadPointer(0);
                const float* dataR = mixBuf.getReadPointer(1);

                bf.fullMixRms = (mixBuf.getRMSLevel(0, 0, mixSamples) + mixBuf.getRMSLevel(1, 0, mixSamples)) * 0.5f;

                int zeroCrossings = 0;
                float lowState = 0.0f;
                float subEnergySum = 0.0f;
                float highPeak = 0.0f;
                const float lpAlpha = static_cast<float>(juce::jlimit(0.01, 0.5, (2.0 * 3.14159265 * 130.0) / mixSr));

                float prevMono = (dataL[0] + dataR[0]) * 0.5f;
                for (int i = 1; i < mixSamples; ++i)
                {
                    float mono = (dataL[i] + dataR[i]) * 0.5f;
                    if ((mono >= 0.0f && prevMono < 0.0f) || (mono < 0.0f && prevMono >= 0.0f))
                        ++zeroCrossings;

                    lowState += lpAlpha * (mono - lowState);
                    subEnergySum += lowState * lowState;

                    float hp = std::abs(mono - prevMono);
                    if (hp > highPeak) highPeak = hp;
                    prevMono = mono;
                }

                bf.brightness = static_cast<float>(zeroCrossings) / static_cast<float>(mixSamples);
                float fallbackLowRms = std::sqrt(subEnergySum / static_cast<float>(mixSamples));

                bf.kickEnergy = fallbackLowRms;
                bf.snareEnergy = highPeak;
                bf.bassEnergy = fallbackLowRms;
                bf.guitarEnergy = bf.fullMixRms * (0.5f + bf.brightness);
            }

            // 2. Stem BATTERIA (2 canali)
            if (drumsReader != nullptr)
            {
                int nSamp = static_cast<int>(durSec * drumsReader->sampleRate);
                juce::int64 sPos = static_cast<juce::int64>(startSec * drumsReader->sampleRate);
                if (sPos + nSamp <= drumsReader->lengthInSamples && nSamp > 0)
                {
                    stemBuf.setSize(2, nSamp, false, false, true);
                    drumsReader->read(&stemBuf, 0, nSamp, sPos, true, true);
                    const float* dL = stemBuf.getReadPointer(0);
                    const float* dR = stemBuf.getReadPointer(1);

                    float lp = 0.0f, kickPeak = 0.0f, snarePeak = 0.0f;
                    const float alpha = static_cast<float>(juce::jlimit(0.01, 0.5, (2.0 * 3.14159265 * 120.0) / drumsReader->sampleRate));
                    for (int i = 1; i < nSamp; ++i)
                    {
                        float mono = (dL[i] + dR[i]) * 0.5f;
                        lp += alpha * (mono - lp);
                        if (std::abs(lp) > kickPeak) kickPeak = std::abs(lp);

                        float hp = std::abs(mono - lp);
                        if (hp > snarePeak) snarePeak = hp;
                    }
                    bf.kickEnergy = kickPeak;
                    bf.snareEnergy = snarePeak;
                }
            }

            // 3. Stem BASSO (2 canali)
            if (bassReader != nullptr)
            {
                int nSamp = static_cast<int>(durSec * bassReader->sampleRate);
                juce::int64 sPos = static_cast<juce::int64>(startSec * bassReader->sampleRate);
                if (sPos + nSamp <= bassReader->lengthInSamples && nSamp > 0)
                {
                    stemBuf.setSize(2, nSamp, false, false, true);
                    bassReader->read(&stemBuf, 0, nSamp, sPos, true, true);
                    bf.bassEnergy = (stemBuf.getRMSLevel(0, 0, nSamp) + stemBuf.getRMSLevel(1, 0, nSamp)) * 0.5f;
                }
            }

            // 4. Stem CHITARRE / MELODIA (2 canali)
            if (guitarsReader != nullptr)
            {
                int nSamp = static_cast<int>(durSec * guitarsReader->sampleRate);
                juce::int64 sPos = static_cast<juce::int64>(startSec * guitarsReader->sampleRate);
                if (sPos + nSamp <= guitarsReader->lengthInSamples && nSamp > 0)
                {
                    stemBuf.setSize(2, nSamp, false, false, true);
                    guitarsReader->read(&stemBuf, 0, nSamp, sPos, true, true);
                    bf.guitarEnergy = (stemBuf.getRMSLevel(0, 0, nSamp) + stemBuf.getRMSLevel(1, 0, nSamp)) * 0.5f;
                }
            }

            maxRms = juce::jmax(maxRms, bf.fullMixRms);
            maxBright = juce::jmax(maxBright, bf.brightness);
            maxKick = juce::jmax(maxKick, bf.kickEnergy);
            maxSnare = juce::jmax(maxSnare, bf.snareEnergy);
            maxBass = juce::jmax(maxBass, bf.bassEnergy);
            maxGuit = juce::jmax(maxGuit, bf.guitarEnergy);

            result.beats.push_back(bf);
        }

        for (auto& bf : result.beats)
        {
            bf.fullMixRms /= maxRms;
            bf.brightness /= maxBright;
            bf.kickEnergy /= maxKick;
            bf.snareEnergy /= maxSnare;
            bf.bassEnergy /= maxBass;
            bf.guitarEnergy /= maxGuit;
        }
        return true;
    }

    // --- TT-4.3: Segmentazione del brano in Sezioni Musicali ---
    static void segmentSongIntoSections(AnalysisResult& result)
    {
        if (result.beats.empty()) return;

        const size_t totalBeats = result.beats.size();
        const size_t phraseLen = 16;

        int sectionCounter = 1;
        for (size_t i = 0; i < totalBeats; i += phraseLen)
        {
            size_t endIdx = juce::jmin(totalBeats, i + phraseLen);
            size_t count = endIdx - i;
            if (count == 0) break;

            SongSection sec;
            sec.startBeat = result.beats[i].beat;
            sec.durationBeats = static_cast<double>(count);

            for (size_t k = i; k < endIdx; ++k)
            {
                const auto& b = result.beats[k];
                sec.avgEnergy += b.fullMixRms;
                sec.avgBrightness += b.brightness;
                sec.kickDensity += b.kickEnergy;
                sec.snareDensity += b.snareEnergy;
                sec.bassLevel += b.bassEnergy;
                sec.guitarLevel += b.guitarEnergy;
            }

            float invCount = 1.0f / static_cast<float>(count);
            sec.avgEnergy *= invCount;
            sec.avgBrightness *= invCount;
            sec.kickDensity *= invCount;
            sec.snareDensity *= invCount;
            sec.bassLevel *= invCount;
            sec.guitarLevel *= invCount;

            bool isFirst = (i == 0);
            bool isLast = (endIdx == totalBeats);

            if (isLast && sec.avgEnergy < 0.45f)
            {
                sec.mood = SectionMood::Outro;
                sec.name = juce::String(sectionCounter++) + " - Outro Dissolvenza";
            }
            else if (sec.avgEnergy < 0.38f)
            {
                sec.mood = SectionMood::Calm;
                sec.name = juce::String(sectionCounter++) + (isFirst ? " - Intro Atmosferico" : " - Quiete / Arpeggio");
            }
            else if (sec.avgEnergy >= 0.78f && (sec.kickDensity > 0.65f || sec.snareDensity > 0.65f))
            {
                sec.mood = SectionMood::Climax;
                sec.name = juce::String(sectionCounter++) + " - Climax / Blast";
            }
            else if (sec.avgEnergy >= 0.58f)
            {
                sec.mood = SectionMood::HighEnergy;
                sec.name = juce::String(sectionCounter++) + " - Riff / Apertura";
            }
            else
            {
                sec.mood = SectionMood::BuildUp;
                sec.name = juce::String(sectionCounter++) + " - Strofa / Build-Up";
            }

            if (!result.sections.empty() &&
                result.sections.back().mood == sec.mood &&
                result.sections.back().durationBeats < 32.0)
            {
                auto& prev = result.sections.back();
                prev.durationBeats += sec.durationBeats;
                prev.avgEnergy = (prev.avgEnergy + sec.avgEnergy) * 0.5f;
                prev.avgBrightness = (prev.avgBrightness + sec.avgBrightness) * 0.5f;
                prev.kickDensity = (prev.kickDensity + sec.kickDensity) * 0.5f;
                prev.snareDensity = (prev.snareDensity + sec.snareDensity) * 0.5f;
                prev.bassLevel = (prev.bassLevel + sec.bassLevel) * 0.5f;
                prev.guitarLevel = (prev.guitarLevel + sec.guitarLevel) * 0.5f;
            }
            else
            {
                result.sections.push_back(sec);
            }
        }
    }
};