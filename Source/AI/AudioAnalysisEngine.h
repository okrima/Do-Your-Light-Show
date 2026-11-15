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

        auto clickReader = openReader(formatManager, input.clickFile);
        auto drumsReader = openReader(formatManager, input.drumsFile);
        auto bassReader = openReader(formatManager, input.bassFile);
        auto guitarsReader = openReader(formatManager, input.guitarsFile);

        // 1. Fase Cadenza / BPM (0% -> 30%)
        // Priorita': 1) Click Track -> 2) Stem Batteria -> 3) Full Mix
        if (clickReader != nullptr && clickReader->lengthInSamples > 0)
        {
            if (!analyzeClickTrack(*clickReader, result, reportProgress))
                return result;
        }
        else if (drumsReader != nullptr && drumsReader->lengthInSamples > 0)
        {
            if (!estimateTempoAutocorrelation(*drumsReader, true, result, reportProgress))
                return result;
        }
        else
        {
            if (!estimateTempoAutocorrelation(*fullMixReader, false, result, reportProgress))
                return result;
        }

        // Costruisce la BeatTimeMap locale al thread
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

        // 2. Fase Estrazione Feature quarto per quarto (30% -> 92%)
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

    // --- TT-4.2A: Rilevamento ad alta precisione su traccia Click ---
    static bool analyzeClickTrack(juce::AudioFormatReader& clickReader,
        AnalysisResult& result,
        const ProgressCallback& reportProgress)
    {
        if (!reportProgress(0.02, "Analisi traccia Click per griglia BPM..."))
            return false;

        const double sr = clickReader.sampleRate;
        const int hopSamples = juce::jmax(64, static_cast<int>(std::round(sr * 0.004))); // Finestre da 4 ms
        const juce::int64 totalSamples = clickReader.lengthInSamples;

        juce::AudioBuffer<float> buffer(2, hopSamples);
        std::vector<float> env;
        env.reserve(static_cast<size_t>(totalSamples / hopSamples) + 1);

        float maxPeak = 0.001f;
        int stepCount = 0;

        for (juce::int64 pos = 0; pos + hopSamples <= totalSamples; pos += hopSamples)
        {
            if ((++stepCount % 500) == 0)
            {
                double prog = 0.02 + 0.18 * (static_cast<double>(pos) / static_cast<double>(totalSamples));
                if (!reportProgress(prog, ""))
                    return false;
            }

            clickReader.read(&buffer, 0, hopSamples, pos, true, true);
            float pk = juce::jmax(buffer.getMagnitude(0, 0, hopSamples),
                buffer.getMagnitude(1, 0, hopSamples));
            env.push_back(pk);
            if (pk > maxPeak) maxPeak = pk;
        }

        // Soglia tarata per catturare sia l'accento forte (1) sia i quarti deboli (2, 3, 4)
        const float threshold = juce::jmax(0.02f, maxPeak * 0.18f);
        const int minDistanceFrames = static_cast<int>(0.18 / 0.004); // Minimo 180 ms tra due click
        int lastPeakFrame = -minDistanceFrames;

        std::vector<double> onsetTimesSec;
        for (size_t i = 1; i + 1 < env.size(); ++i)
        {
            if (env[i] > threshold && env[i] >= env[i - 1] && env[i] >= env[i + 1])
            {
                if (static_cast<int>(i) - lastPeakFrame >= minDistanceFrames)
                {
                    onsetTimesSec.push_back(static_cast<double>(i * hopSamples) / sr);
                    lastPeakFrame = static_cast<int>(i);
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
                if (dt >= 0.22 && dt <= 1.20)
                    intervals.push_back(dt);
            }

            if (!intervals.empty())
            {
                std::sort(intervals.begin(), intervals.end());
                double medianInterval = intervals[intervals.size() / 2];

                // Media pesata attorno alla mediana per precisione sub-millisecondo
                double sumInterval = 0.0;
                int countValid = 0;
                for (double dt : intervals)
                {
                    if (std::abs(dt - medianInterval) <= medianInterval * 0.08)
                    {
                        sumInterval += dt;
                        ++countValid;
                    }
                }

                double refinedInterval = (countValid > 0) ? (sumInterval / countValid) : medianInterval;
                double bpm = 60.0 / refinedInterval;

                while (bpm < 70.0)  bpm *= 2.0;
                while (bpm > 205.0) bpm *= 0.5;

                // Arrotonda al mezzo BPM (es. 120.0, 120.5)
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
                            localBpm = std::round(juce::jlimit(50.0, 250.0, (4.0 * 60.0) / spanSec) * 2.0) / 2.0;
                    }
                    result.anchors.push_back({ static_cast<double>(i + 1), onsetTimesSec[i], localBpm });
                }
            }
        }
        return true;
    }

    // --- TT-4.2B: Stima BPM via Autocorrelazione Multi-Banda + Comb Filter a 200 Hz (5 ms) ---
    static bool estimateTempoAutocorrelation(juce::AudioFormatReader& reader,
        bool isDrumStem,
        AnalysisResult& result,
        const ProgressCallback& reportProgress)
    {
        juce::String sourceName = isDrumStem ? "dallo Stem Batteria..." : "dal Full Mix...";
        if (!reportProgress(0.02, "Estrazione inviluppo ritmico " + sourceName))
            return false;

        const double sr = reader.sampleRate;
        const double envRate = 200.0; // 200 frame al secondo (risoluzione 5 ms)
        const int hopSamples = juce::jmax(64, static_cast<int>(std::round(sr / envRate)));
        const double actualEnvRate = sr / static_cast<double>(hopSamples);

        // Analizziamo fino a 180 secondi per saltare eventuali intro atmosferici lunghi
        const juce::int64 maxSamples = juce::jmin<juce::int64>(
            reader.lengthInSamples, static_cast<juce::int64>(sr * 180.0));

        const size_t numFrames = static_cast<size_t>(maxSamples / hopSamples);
        if (numFrames < 400)
            return true;

        juce::AudioBuffer<float> buffer(2, hopSamples);
        std::vector<float> rawOnset(numFrames, 0.0f);
        std::vector<float> frameRms(numFrames, 0.0f);

        // Coefficienti filtro 1-polo per isolare Cassa (< 135 Hz) e Rullante/Attacco (> 1200 Hz)
        const float lpAlpha = static_cast<float>(juce::jlimit(0.005, 0.4, (2.0 * 3.14159265 * 135.0) / sr));
        const float hpAlpha = static_cast<float>(juce::jlimit(0.05, 0.85, (2.0 * 3.14159265 * 1200.0) / sr));

        float lpState = 0.0f;
        float midState = 0.0f;
        float prevLowLog = 0.0f;
        float prevHighLog = 0.0f;
        float maxTrackRms = 0.0001f;

        for (size_t f = 0; f < numFrames; ++f)
        {
            if ((f % 600) == 0)
            {
                double prog = 0.02 + 0.16 * (static_cast<double>(f) / static_cast<double>(numFrames));
                if (!reportProgress(prog, ""))
                    return false;
            }

            juce::int64 pos = static_cast<juce::int64>(f) * hopSamples;
            reader.read(&buffer, 0, hopSamples, pos, true, true);
            const float* dL = buffer.getReadPointer(0);
            const float* dR = buffer.getReadPointer(1);

            float lowSqSum = 0.0f;
            float highSqSum = 0.0f;
            float totSqSum = 0.0f;

            for (int i = 0; i < hopSamples; ++i)
            {
                float mono = (dL[i] + dR[i]) * 0.5f;
                totSqSum += mono * mono;

                // Passa-basso Cassa
                lpState += lpAlpha * (mono - lpState);
                lowSqSum += lpState * lpState;

                // Passa-alto Rullante / Transienti
                midState += hpAlpha * (mono - midState);
                float highSample = mono - midState;
                highSqSum += highSample * highSample;
            }

            float invN = 1.0f / static_cast<float>(hopSamples);
            float totRms = std::sqrt(totSqSum * invN);
            float lowRms = std::sqrt(lowSqSum * invN);
            float highRms = std::sqrt(highSqSum * invN);

            frameRms[f] = totRms;
            if (totRms > maxTrackRms) maxTrackRms = totRms;

            // Compressione logaritmica per rilevare i transienti anche nei mix molto compressi
            float lowLog = std::log10(1.0f + 35.0f * lowRms);
            float highLog = std::log10(1.0f + 35.0f * highRms);

            float dLow = juce::jmax(0.0f, lowLog - prevLowLog);
            float dHigh = juce::jmax(0.0f, highLog - prevHighLog);

            prevLowLog = lowLog;
            prevHighLog = highLog;

            rawOnset[f] = (dLow * 1.45f) + (dHigh * 1.0f);
        }

        if (!reportProgress(0.20, "Calcolo autocorrelazione ritmica e griglia BPM..."))
            return false;

        // Sottrae la media mobile locale (finestra di ~250 ms) per esaltare solo i picchi ritmici netti
        std::vector<float> cleanOnset(numFrames, 0.0f);
        const int halfWin = 25;
        for (size_t f = 0; f < numFrames; ++f)
        {
            size_t startW = (f > static_cast<size_t>(halfWin)) ? (f - halfWin) : 0;
            size_t endW = juce::jmin(numFrames - 1, f + halfWin);
            float localMean = 0.0f;
            for (size_t k = startW; k <= endW; ++k)
                localMean += rawOnset[k];
            localMean /= static_cast<float>(endW - startW + 1);

            // Ignora i frame di silenzio o di pura quiete ambientale (< 12% del volume massimo)
            if (frameRms[f] > maxTrackRms * 0.12f)
                cleanOnset[f] = juce::jmax(0.0f, rawOnset[f] - localMean);
        }

        // Calcola l'Autocorrelazione per i lag da 35 BPM a 400 BPM
        const int minLag = juce::jmax(10, static_cast<int>(std::floor((60.0 * actualEnvRate) / 400.0)));
        const int maxLag = juce::jmin(static_cast<int>(numFrames / 2),
            static_cast<int>(std::ceil((60.0 * actualEnvRate) / 35.0)));

        std::vector<double> acf(static_cast<size_t>(maxLag + 2), 0.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;
            size_t limit = numFrames - static_cast<size_t>(lag);
            for (size_t n = 0; n < limit; ++n)
                sum += static_cast<double>(cleanOnset[n]) * static_cast<double>(cleanOnset[n + static_cast<size_t>(lag)]);

            acf[static_cast<size_t>(lag)] = sum / static_cast<double>(limit);
        }

        // Funzione helper per leggere l'ACF con interpolazione lineare sui sub-frame
        auto sampleAcf = [&acf, minLag, maxLag](double exactLag) -> double {
            int idx = static_cast<int>(std::floor(exactLag));
            if (idx < minLag || idx + 1 > maxLag)
                return 0.0;
            double frac = exactLag - static_cast<double>(idx);
            // Media anche sui 2 frame adiacenti per tollerare micro-fluttuazioni umane
            double center = (1.0 - frac) * acf[static_cast<size_t>(idx)] + frac * acf[static_cast<size_t>(idx + 1)];
            double left = (idx - 1 >= minLag) ? acf[static_cast<size_t>(idx - 1)] : center;
            double right = (idx + 2 <= maxLag) ? acf[static_cast<size_t>(idx + 2)] : center;
            return 0.60 * center + 0.20 * left + 0.20 * right;
            };

        // Comb Filter sui candidati BPM da 68.0 a 195.0 (step di 0.25 BPM)
        double bestBpm = result.detectedBpm;
        double bestScore = -1.0;
        std::vector<std::pair<double, double>> bpmScores;

        for (double candBpm = 68.0; candBpm <= 195.0; candBpm += 0.25)
        {
            double baseLag = (60.0 * actualEnvRate) / candBpm;

            // Somma armonica: 1 quarto + 2 quarti + 4 quarti (1 battuta) + 1/2 quarto (ottavi)
            double combScore = 1.00 * sampleAcf(baseLag)
                + 0.55 * sampleAcf(baseLag * 2.0)
                + 0.35 * sampleAcf(baseLag * 4.0)
                + 0.25 * sampleAcf(baseLag * 0.5);

            // Finestra percettiva gaussiana centrata a 126 BPM per evitare dimezzamenti/raddoppi errati
            double diff = (candBpm - 126.0) / 48.0;
            double perceptualWeight = std::exp(-0.5 * diff * diff);

            double totalScore = combScore * (0.65 + 0.35 * perceptualWeight);
            bpmScores.push_back({ candBpm, totalScore });

            if (totalScore > bestScore)
            {
                bestScore = totalScore;
                bestBpm = candBpm;
            }
        }

        // Aggancio intelligente al BPM intero (es. 120.0, 140.0) se il punteggio e' quasi identico
        double roundedIntBpm = std::round(bestBpm);
        for (const auto& pair : bpmScores)
        {
            if (std::abs(pair.first - roundedIntBpm) < 0.01 && pair.second >= bestScore * 0.96)
            {
                bestBpm = roundedIntBpm;
                break;
            }
        }

        result.detectedBpm = bestBpm;
        result.usedClickTrack = false;

        // --- Allineamento di Fase (ricerca del primo Downbeat reale) ---
        double beatPeriodFrames = (60.0 * actualEnvRate) / result.detectedBpm;
        int periodInt = juce::jmax(1, static_cast<int>(std::round(beatPeriodFrames)));

        int bestPhaseFrame = 0;
        double bestPhaseEnergy = -1.0;
        for (int phase = 0; phase < periodInt; ++phase)
        {
            double phaseSum = 0.0;
            for (size_t f = static_cast<size_t>(phase); f < numFrames; f += static_cast<size_t>(periodInt))
                phaseSum += static_cast<double>(cleanOnset[f]);

            if (phaseSum > bestPhaseEnergy)
            {
                bestPhaseEnergy = phaseSum;
                bestPhaseFrame = phase;
            }
        }

        // Trova il primo quarto attivo del brano allineato alla fase migliore
        size_t firstActiveFrame = static_cast<size_t>(bestPhaseFrame);
        for (size_t f = static_cast<size_t>(bestPhaseFrame); f < numFrames; f += static_cast<size_t>(periodInt))
        {
            if (frameRms[f] > maxTrackRms * 0.15f && cleanOnset[f] > 0.01f)
            {
                firstActiveFrame = f;
                break;
            }
        }

        result.firstDownbeatSec = static_cast<double>(firstActiveFrame * hopSamples) / sr;
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
                double prog = 0.30 + 0.62 * (static_cast<double>(b) / static_cast<double>(numBeats));
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