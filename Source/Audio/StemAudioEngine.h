#pragma once
#include <JuceHeader.h>
#include "../Model/DataStateKeys.h"
#include "../Model/BeatTimeMap.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

class StemAudioEngine
{
public:
    static constexpr int kNumStemSlots = 5;

    enum class StemSlot : int
    {
        FullMix = 0,
        Click = 1,
        Drums = 2,
        Bass = 3,
        Guitars = 4
    };

    static StemSlot slotFromPathKey(const juce::Identifier& key) noexcept
    {
        if (key == StateKeys::StemClick)   return StemSlot::Click;
        if (key == StateKeys::StemDrums)   return StemSlot::Drums;
        if (key == StateKeys::StemBass)    return StemSlot::Bass;
        if (key == StateKeys::StemGuitars) return StemSlot::Guitars;
        return StemSlot::FullMix;
    }

    static juce::Identifier pathKeyForSlot(StemSlot slot) noexcept
    {
        switch (slot)
        {
        case StemSlot::Click:   return StateKeys::StemClick;
        case StemSlot::Drums:   return StateKeys::StemDrums;
        case StemSlot::Bass:    return StateKeys::StemBass;
        case StemSlot::Guitars: return StateKeys::StemGuitars;
        case StemSlot::FullMix:
        default:                return StateKeys::StemFullMix;
        }
    }

    static juce::Identifier volumeKeyForSlot(StemSlot slot) noexcept
    {
        switch (slot)
        {
        case StemSlot::Click:   return StateKeys::StemClickVol;
        case StemSlot::Drums:   return StateKeys::StemDrumsVol;
        case StemSlot::Bass:    return StateKeys::StemBassVol;
        case StemSlot::Guitars: return StateKeys::StemGuitarsVol;
        case StemSlot::FullMix:
        default:                return StateKeys::StemFullMixVol;
        }
    }

    explicit StemAudioEngine(bool openAudioDevice = true)
        : masterSource(*this),
        audioDeviceEnabled(openAudioDevice)
    {
        for (auto& v : stemVolumes)
            v.store(1.0f);

        formatManager.registerBasicFormats(); // WAV, AIFF, MP3, FLAC, OGG

        if (audioDeviceEnabled)
        {
            deviceManager.initialiseWithDefaultDevices(0, 2); // 0 input, 2 output stereo
            sourcePlayer.setSource(&masterSource);
            deviceManager.addAudioCallback(&sourcePlayer);
        }
    }

    ~StemAudioEngine()
    {
        masterPlaying.store(false);
        beatTimeMapPtr.store(nullptr);

        if (audioDeviceEnabled)
        {
            sourcePlayer.setSource(nullptr);
            deviceManager.removeAudioCallback(&sourcePlayer);
            deviceManager.closeAudioDevice();
        }
    }

    void setBeatTimeMap(const BeatTimeMap* mapPtr) noexcept
    {
        beatTimeMapPtr.store(mapPtr);
    }

    bool hasActiveAudioDevice() const
    {
        return audioDeviceEnabled && deviceManager.getCurrentAudioDevice() != nullptr;
    }

    // Carica e decodifica in RAM uno qualsiasi dei 5 slot (FullMix, Click, Drums, Bass, Guitars)
    bool loadStem(StemSlot slot, const juce::File& audioFile, juce::ValueTree& appState, bool preserveSavedOffset = false)
    {
        if (!audioFile.existsAsFile())
            return false;

        auto newClip = decodeAudioFileToRam(audioFile);
        if (newClip == nullptr)
            return false;

        const size_t idx = static_cast<size_t>(slot);
        if (slot == StemSlot::FullMix)
            masterPlaying.store(false);

        std::shared_ptr<const DecodedAudioClip> oldClip;
        {
            const juce::SpinLock::ScopedLockType sl(clipLock);
            oldClip = std::move(stemClips[idx]);
            stemClips[idx] = std::move(newClip);
            stemFiles[idx] = audioFile;
        }
        oldClip.reset();

        double restoredOffset = fullMixOffsetSec.load();
        float restoredVol = stemVolumes[idx].load();

        if (appState.isValid())
        {
            auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
            {
                stemsNode.setProperty(pathKeyForSlot(slot), audioFile.getFullPathName(), nullptr);

                const auto volKey = volumeKeyForSlot(slot);
                if (stemsNode.hasProperty(volKey))
                    restoredVol = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(stemsNode.getProperty(volKey, 1.0))));
                else
                    stemsNode.setProperty(volKey, static_cast<double>(restoredVol), nullptr);

                if (slot == StemSlot::FullMix)
                {
                    if (preserveSavedOffset)
                        restoredOffset = static_cast<double>(stemsNode.getProperty(StateKeys::StemFullMixOffset, 0.0));
                    else
                    {
                        restoredOffset = 0.0;
                        stemsNode.setProperty(StateKeys::StemFullMixOffset, 0.0, nullptr);
                    }
                }
            }
        }

        stemVolumes[idx].store(restoredVol);

        if (slot == StemSlot::FullMix)
        {
            fullMixOffsetSec.store(restoredOffset);
            setPositionSeconds(0.0);
        }

        return true;
    }

    // Rimuove la traccia dallo slot in RAM e (opzionalmente) dal ValueTree
    void clearStem(StemSlot slot, juce::ValueTree* appStateToUpdate = nullptr)
    {
        const size_t idx = static_cast<size_t>(slot);
        if (slot == StemSlot::FullMix)
            masterPlaying.store(false);

        std::shared_ptr<const DecodedAudioClip> oldClip;
        {
            const juce::SpinLock::ScopedLockType sl(clipLock);
            oldClip = std::move(stemClips[idx]);
            stemClips[idx].reset();
            stemFiles[idx] = juce::File();
        }
        oldClip.reset();

        if (appStateToUpdate != nullptr && appStateToUpdate->isValid())
        {
            auto stemsNode = appStateToUpdate->getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
                stemsNode.setProperty(pathKeyForSlot(slot), "", nullptr);
        }
    }

    void clearStem(const juce::Identifier& stemKey, juce::ValueTree* appStateToUpdate = nullptr)
    {
        clearStem(slotFromPathKey(stemKey), appStateToUpdate);
    }

    // Retrocompatibilita': carica il Full Mix
    bool loadFullMix(const juce::File& audioFile, juce::ValueTree& appState, bool preserveSavedOffset = false)
    {
        return loadStem(StemSlot::FullMix, audioFile, appState, preserveSavedOffset);
    }

    // Carica uno stem secondario (Click, Drums, Bass, Guitars) sia per AI sia per playback
    bool setSecondaryStemFile(const juce::Identifier& stemKey, const juce::File& audioFile, juce::ValueTree& appState)
    {
        return loadStem(slotFromPathKey(stemKey), audioFile, appState, true);
    }

    // Sincronizza tutti e 5 gli slot, offset, volumi e metronomo dopo un Load Show
    void syncAllStemsFromState(juce::ValueTree& appState)
    {
        if (!appState.isValid())
            return;

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        if (stemsNode.isValid())
        {
            const double savedOffset = static_cast<double>(stemsNode.getProperty(StateKeys::StemFullMixOffset, 0.0));
            fullMixOffsetSec.store(juce::jlimit(-600.0, 600.0, savedOffset));

            for (int i = 0; i < kNumStemSlots; ++i)
            {
                const auto slot = static_cast<StemSlot>(i);
                const auto volKey = volumeKeyForSlot(slot);
                const float savedVol = juce::jlimit(0.0f, 1.0f,
                    static_cast<float>(static_cast<double>(stemsNode.getProperty(volKey, 1.0))));
                stemVolumes[static_cast<size_t>(i)].store(savedVol);

                const juce::String path = stemsNode.getProperty(pathKeyForSlot(slot), "").toString();
                if (path.isEmpty())
                {
                    if (isStemLoaded(slot))
                        clearStem(slot, nullptr);
                    continue;
                }

                juce::File targetFile(path);
                if (!targetFile.existsAsFile())
                {
                    if (isStemLoaded(slot))
                        clearStem(slot, nullptr);
                    continue;
                }

                if (targetFile != getStemFile(slot))
                    loadStem(slot, targetFile, appState, true);
            }
        }

        auto beatNode = appState.getChildWithName(StateKeys::BeatMapNode);
        if (beatNode.isValid())
        {
            metronomeEnabled.store(static_cast<bool>(beatNode.getProperty(StateKeys::MetronomeEnabled, false)));
            metronomeGain.store(juce::jlimit(0.0f, 1.0f,
                static_cast<float>(static_cast<double>(beatNode.getProperty(StateKeys::MetronomeGain, 0.65)))));
        }
    }

    // --- Volumi indipendenti per traccia (0.0 .. 1.0) ---
    void setStemVolume(StemSlot slot, float gain0to1, juce::ValueTree* appStateToUpdate = nullptr)
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, gain0to1);
        stemVolumes[static_cast<size_t>(slot)].store(clamped);

        if (appStateToUpdate != nullptr && appStateToUpdate->isValid())
        {
            auto stemsNode = appStateToUpdate->getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
                stemsNode.setProperty(volumeKeyForSlot(slot), static_cast<double>(clamped), nullptr);
        }
    }

    void setStemVolume(const juce::Identifier& stemKey, float gain0to1, juce::ValueTree* appStateToUpdate = nullptr)
    {
        setStemVolume(slotFromPathKey(stemKey), gain0to1, appStateToUpdate);
    }

    float getStemVolume(StemSlot slot) const noexcept
    {
        return stemVolumes[static_cast<size_t>(slot)].load();
    }

    float getStemVolume(const juce::Identifier& stemKey) const noexcept
    {
        return getStemVolume(slotFromPathKey(stemKey));
    }

    bool isStemLoaded(StemSlot slot) const
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        const auto& c = stemClips[static_cast<size_t>(slot)];
        return c != nullptr && c->numSamples > 0;
    }

    juce::File getStemFile(StemSlot slot) const
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        return stemFiles[static_cast<size_t>(slot)];
    }

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

    // --- Offset Audio rispetto alla griglia ---
    void setFullMixOffsetSeconds(double offsetSec, juce::ValueTree* appStateToUpdate = nullptr)
    {
        const double clamped = juce::jlimit(-600.0, 600.0, offsetSec);
        fullMixOffsetSec.store(clamped);

        if (appStateToUpdate != nullptr && appStateToUpdate->isValid())
        {
            auto stemsNode = appStateToUpdate->getChildWithName(StateKeys::AudioStemsNode);
            if (stemsNode.isValid())
                stemsNode.setProperty(StateKeys::StemFullMixOffset, clamped, nullptr);
        }
    }

    double getFullMixOffsetSeconds() const noexcept { return fullMixOffsetSec.load(); }

    // --- Controlli Metronomo ---
    void setMetronomeEnabled(bool enabled) noexcept { metronomeEnabled.store(enabled); }
    bool isMetronomeEnabled() const noexcept { return metronomeEnabled.load(); }

    void setMetronomeGain(float gain0to1) noexcept { metronomeGain.store(juce::jlimit(0.0f, 1.0f, gain0to1)); }
    float getMetronomeGain() const noexcept { return metronomeGain.load(); }

    // --- Rilevamento Transienti sul Full Mix in RAM ---
    bool detectFirstTransientSeconds(double& outFileSec, double maxScanSec = 60.0)
    {
        std::shared_ptr<const DecodedAudioClip> clip;
        {
            const juce::SpinLock::ScopedLockType sl(clipLock);
            clip = stemClips[static_cast<size_t>(StemSlot::FullMix)];
        }
        if (clip == nullptr || clip->numSamples <= 0 || clip->sampleRate <= 0.0)
            return false;

        const double sr = clip->sampleRate;
        const int hopSamples = juce::jmax(64, juce::roundToInt(sr * 0.005));
        const int maxSamples = juce::jmin(clip->numSamples, static_cast<int>(maxScanSec * sr));
        const int numFrames = maxSamples / hopSamples;
        if (numFrames < 4)
            return false;

        std::vector<float> rms(static_cast<std::size_t>(numFrames), 0.0f);
        float globalPeakRms = 0.0f;

        for (int f = 0; f < numFrames; ++f)
        {
            const int startIdx = f * hopSamples;
            const int count = juce::jmin(hopSamples, maxSamples - startIdx);
            double sumSq = 0.0;

            for (int c = 0; c < clip->numChannels; ++c)
            {
                const float* ptr = clip->buffer.getReadPointer(c) + startIdx;
                for (int i = 0; i < count; ++i)
                    sumSq += static_cast<double>(ptr[i]) * static_cast<double>(ptr[i]);
            }

            const float frameRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(juce::jmax(1, count * clip->numChannels))));
            rms[static_cast<std::size_t>(f)] = frameRms;
            if (frameRms > globalPeakRms)
                globalPeakRms = frameRms;
        }

        if (globalPeakRms < 0.005f)
            return false;

        const float triggerThresh = juce::jmax(0.012f, globalPeakRms * 0.14f);
        int hitFrame = -1;
        for (int f = 0; f < numFrames; ++f)
        {
            if (rms[static_cast<std::size_t>(f)] >= triggerThresh)
            {
                hitFrame = f;
                break;
            }
        }

        if (hitFrame < 0)
            return false;

        const float onsetFloor = rms[static_cast<std::size_t>(hitFrame)] * 0.22f;
        int onsetFrame = hitFrame;
        const int minBack = juce::jmax(0, hitFrame - 8);
        while (onsetFrame > minBack && rms[static_cast<std::size_t>(onsetFrame - 1)] > onsetFloor)
            --onsetFrame;

        outFileSec = (static_cast<double>(onsetFrame) * static_cast<double>(hopSamples)) / sr;
        return true;
    }

    bool detectTransientNearFileTime(double targetFileSec, double windowRadiusSec, double& outFileSec)
    {
        std::shared_ptr<const DecodedAudioClip> clip;
        {
            const juce::SpinLock::ScopedLockType sl(clipLock);
            clip = stemClips[static_cast<size_t>(StemSlot::FullMix)];
        }
        if (clip == nullptr || clip->numSamples <= 0 || clip->sampleRate <= 0.0)
            return false;

        const double sr = clip->sampleRate;
        const double fileLenSec = static_cast<double>(clip->numSamples) / sr;
        const double startSec = juce::jlimit(0.0, fileLenSec, targetFileSec - windowRadiusSec);
        const double endSec = juce::jlimit(0.0, fileLenSec, targetFileSec + windowRadiusSec);
        if (endSec - startSec < 0.02)
            return false;

        const int hopSamples = juce::jmax(32, juce::roundToInt(sr * 0.002));
        const int startSample = juce::jlimit(0, clip->numSamples - 1, static_cast<int>(startSec * sr));
        const int totalSamples = juce::jmin(clip->numSamples - startSample, static_cast<int>((endSec - startSec) * sr));
        const int numFrames = totalSamples / hopSamples;
        if (numFrames < 4)
            return false;

        std::vector<float> rms(static_cast<std::size_t>(numFrames), 0.0f);
        for (int f = 0; f < numFrames; ++f)
        {
            const int pos = startSample + f * hopSamples;
            const int count = juce::jmin(hopSamples, clip->numSamples - pos);
            double sumSq = 0.0;

            for (int c = 0; c < clip->numChannels; ++c)
            {
                const float* ptr = clip->buffer.getReadPointer(c) + pos;
                for (int i = 0; i < count; ++i)
                    sumSq += static_cast<double>(ptr[i]) * static_cast<double>(ptr[i]);
            }
            rms[static_cast<std::size_t>(f)] = static_cast<float>(std::sqrt(sumSq / static_cast<double>(juce::jmax(1, count * clip->numChannels))));
        }

        float bestFlux = 0.0f;
        int bestFrame = -1;
        for (int f = 1; f < numFrames; ++f)
        {
            const float prevAvg = (f >= 3)
                ? (rms[static_cast<std::size_t>(f - 1)] + rms[static_cast<std::size_t>(f - 2)] + rms[static_cast<std::size_t>(f - 3)]) / 3.0f
                : rms[static_cast<std::size_t>(f - 1)];
            const float flux = rms[static_cast<std::size_t>(f)] - prevAvg;
            if (flux > bestFlux)
            {
                bestFlux = flux;
                bestFrame = f;
            }
        }

        if (bestFrame < 0 || bestFlux < 0.004f)
            return false;

        outFileSec = startSec + (static_cast<double>(bestFrame) * static_cast<double>(hopSamples)) / sr;
        return true;
    }

    // --- Controlli di trasporto master ---
    void play()
    {
        if (!audioDeviceEnabled)
            return;

        if (!masterPlaying.load())
        {
            justStarted.store(true);
            masterPlaying.store(true);
        }
    }

    void pause() { masterPlaying.store(false); }

    void togglePlayPause()
    {
        if (masterPlaying.load()) pause();
        else                      play();
    }

    void stopAndRewind()
    {
        masterPlaying.store(false);
        setPositionSeconds(0.0);
    }

    void setPositionSeconds(double timeSec)
    {
        const double clamped = juce::jmax(0.0, timeSec);
        pendingSeekSec.store(clamped);
        timelinePosSec.store(clamped);
    }

    double getCurrentPositionSeconds() const noexcept { return timelinePosSec.load(); }

    double getTotalLengthSeconds() const
    {
        const juce::SpinLock::ScopedLockType sl(clipLock);
        double maxLen = 0.0;
        for (const auto& c : stemClips)
        {
            if (c != nullptr && c->sampleRate > 0.0)
                maxLen = juce::jmax(maxLen, static_cast<double>(c->numSamples) / c->sampleRate);
        }
        return maxLen;
    }

    bool isPlaying() const noexcept { return masterPlaying.load(); }
    bool hasFullMixLoaded() const { return isStemLoaded(StemSlot::FullMix); }
    juce::File getCurrentFullMixFile() const { return getStemFile(StemSlot::FullMix); }

    juce::AudioFormatManager& getFormatManager() { return formatManager; }
    juce::AudioTransportSource& getTransportSource() { return legacyTransportSource; }
    void startBackgroundThread() {}

private:
    struct DecodedAudioClip
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        int numChannels = 2;
        int numSamples = 0;
    };

    std::shared_ptr<DecodedAudioClip> decodeAudioFileToRam(const juce::File& audioFile)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return nullptr;

        const juce::int64 maxAllowedSamples = static_cast<juce::int64>(reader->sampleRate * 3600.0);
        const int targetSamples = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, maxAllowedSamples));
        if (targetSamples <= 0)
            return nullptr;

        const int numCh = (reader->numChannels >= 2) ? 2 : 1;
        auto newClip = std::make_shared<DecodedAudioClip>();
        newClip->sampleRate = reader->sampleRate;
        newClip->numChannels = numCh;
        newClip->buffer.setSize(numCh, targetSamples, false, true, false);

        constexpr int chunkSize = 65536;
        int samplesLoaded = 0;
        while (samplesLoaded < targetSamples)
        {
            const int toRead = juce::jmin(chunkSize, targetSamples - samplesLoaded);
            if (!reader->read(&newClip->buffer, samplesLoaded, toRead, samplesLoaded, true, numCh >= 2))
                break;
            samplesLoaded += toRead;
        }

        if (samplesLoaded <= 0)
            return nullptr;

        newClip->numSamples = samplesLoaded;
        return newClip;
    }

    class MasterTimelineSource : public juce::AudioSource
    {
    public:
        explicit MasterTimelineSource(StemAudioEngine& ownerRef) : owner(ownerRef) {}

        void prepareToPlay(int, double sr) override
        {
            sampleRate = (sr > 1000.0) ? sr : 44100.0;
            clickSamplesRemaining = 0;
            lastTickBar = -1;
            lastTickSub = -1;
        }

        void releaseResources() override {}

        void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
        {
            bufferToFill.clearActiveBufferRegion();

            if (!owner.masterPlaying.load())
            {
                clickSamplesRemaining = 0;
                lastTickBar = -1;
                lastTickSub = -1;
                return;
            }

            const int numSamples = bufferToFill.numSamples;
            if (numSamples <= 0 || sampleRate <= 0.0)
                return;

            double tPos = owner.timelinePosSec.load();
            const bool startedNow = owner.justStarted.exchange(false);
            const double reqSeek = owner.pendingSeekSec.exchange(-1.0);

            if (reqSeek >= 0.0)
            {
                tPos = reqSeek;
                lastTickBar = -1;
                lastTickSub = -1;
            }
            else if (startedNow)
            {
                lastTickBar = -1;
                lastTickSub = -1;
            }

            const double invSr = 1.0 / sampleRate;
            const double blockDurSec = static_cast<double>(numSamples) * invSr;
            const int outChCount = bufferToFill.buffer->getNumChannels();
            const int fadeSamples = (startedNow || reqSeek >= 0.0)
                ? juce::jmin(numSamples, juce::roundToInt(sampleRate * 0.002))
                : 0;

            // 1. Mix sample-exact di tutte le tracce caricate con il rispettivo volume
            {
                const juce::SpinLock::ScopedTryLockType sl(owner.clipLock);
                if (sl.isLocked())
                {
                    const double offset = owner.fullMixOffsetSec.load();
                    float* dstL = (outChCount >= 1) ? bufferToFill.buffer->getWritePointer(0) : nullptr;
                    float* dstR = (outChCount >= 2) ? bufferToFill.buffer->getWritePointer(1) : nullptr;

                    for (size_t s = 0; s < static_cast<size_t>(kNumStemSlots); ++s)
                    {
                        const auto& clipPtr = owner.stemClips[s];
                        if (clipPtr == nullptr || clipPtr->numSamples <= 1)
                            continue;

                        const float vol = owner.stemVolumes[s].load();
                        if (vol <= 0.0001f)
                            continue;

                        const auto& clip = *clipPtr;
                        const double srcSr = clip.sampleRate;
                        const double maxIdx = static_cast<double>(clip.numSamples - 1);
                        const float* srcL = clip.buffer.getReadPointer(0);
                        const float* srcR = clip.buffer.getReadPointer(clip.numChannels > 1 ? 1 : 0);

                        for (int i = 0; i < numSamples; ++i)
                        {
                            const double sampleTimeSec = tPos + static_cast<double>(i) * invSr;
                            const double srcPos = (sampleTimeSec - offset) * srcSr;

                            if (srcPos >= 0.0 && srcPos < maxIdx)
                            {
                                const int idx0 = static_cast<int>(srcPos);
                                const float frac = static_cast<float>(srcPos - static_cast<double>(idx0));
                                const float gain = vol * ((i < fadeSamples)
                                    ? (static_cast<float>(i + 1) / static_cast<float>(fadeSamples))
                                    : 1.0f);

                                const float valL = (srcL[idx0] + frac * (srcL[idx0 + 1] - srcL[idx0])) * gain;
                                const float valR = (srcR[idx0] + frac * (srcR[idx0 + 1] - srcR[idx0])) * gain;

                                const int dstIdx = bufferToFill.startSample + i;
                                if (dstL != nullptr) dstL[dstIdx] += valL;
                                if (dstR != nullptr) dstR[dstIdx] += valR;
                            }
                        }
                    }
                }
            }

            // 2. Metronomo sample-accurate sullo stesso identico clock tPos
            const auto* beatMap = owner.beatTimeMapPtr.load();
            if (owner.metronomeEnabled.load() && beatMap != nullptr)
            {
                renderMetronomeBlock(bufferToFill, *beatMap, tPos, blockDurSec);
            }
            else
            {
                clickSamplesRemaining = 0;
                lastTickBar = -1;
                lastTickSub = -1;
            }

            // 3. Protezione clipping sul bus master
            for (int ch = 0; ch < outChCount; ++ch)
            {
                float* dst = bufferToFill.buffer->getWritePointer(ch) + bufferToFill.startSample;
                for (int i = 0; i < numSamples; ++i)
                    dst[i] = juce::jlimit(-1.0f, 1.0f, dst[i]);
            }

            // 4. Avanza il clock master
            if (owner.pendingSeekSec.load() < 0.0)
                owner.timelinePosSec.store(tPos + blockDurSec);
        }

    private:
        void triggerClick(bool isDownbeat) noexcept
        {
            clickTotalSamples = juce::roundToInt((isDownbeat ? 0.034 : 0.026) * sampleRate);
            clickSamplesRemaining = clickTotalSamples;
            clickFreqHz = isDownbeat ? 1320.0 : 880.0;
            clickAmp = isDownbeat ? 0.80f : 0.50f;
            clickPhase = 0.0;
        }

        void renderMetronomeBlock(const juce::AudioSourceChannelInfo& bufferToFill,
            const BeatTimeMap& beatMap,
            double blockStartSec,
            double blockDurSec)
        {
            const int numSamples = bufferToFill.numSamples;
            int triggerSampleIdx = -1;
            bool triggerIsAccent = false;

            const auto stStart = beatMap.queryMetronomeState(blockStartSec);

            if (lastTickBar < 0)
            {
                lastTickBar = stStart.barIndex;
                lastTickSub = stStart.subdivIndex;
                if (stStart.barIndex >= 1 && stStart.fracInSub < 0.05)
                {
                    triggerSampleIdx = 0;
                    triggerIsAccent = (stStart.subdivIndex == 1);
                }
            }

            const auto stEnd = beatMap.queryMetronomeState(blockStartSec + blockDurSec);
            if (stEnd.barIndex >= 1 &&
                (stEnd.barIndex != lastTickBar || stEnd.subdivIndex != lastTickSub))
            {
                int lo = 0;
                int hi = numSamples - 1;
                while (lo < hi)
                {
                    const int mid = (lo + hi) / 2;
                    const double tMid = blockStartSec + (static_cast<double>(mid) / sampleRate);
                    const auto stMid = beatMap.queryMetronomeState(tMid);
                    if (stMid.barIndex == lastTickBar && stMid.subdivIndex == lastTickSub)
                        lo = mid + 1;
                    else
                        hi = mid;
                }

                const auto stHit = beatMap.queryMetronomeState(blockStartSec + (static_cast<double>(lo) / sampleRate));
                if (stHit.barIndex >= 1)
                {
                    triggerSampleIdx = lo;
                    triggerIsAccent = (stHit.subdivIndex == 1);
                    lastTickBar = stHit.barIndex;
                    lastTickSub = stHit.subdivIndex;
                }
            }

            const float masterGain = owner.metronomeGain.load();
            const int numCh = bufferToFill.buffer->getNumChannels();
            const double twoPi = juce::MathConstants<double>::twoPi;

            for (int i = 0; i < numSamples; ++i)
            {
                if (i == triggerSampleIdx)
                    triggerClick(triggerIsAccent);

                if (clickSamplesRemaining > 0)
                {
                    const int elapsed = clickTotalSamples - clickSamplesRemaining;
                    const double tSec = static_cast<double>(elapsed) / sampleRate;
                    const float attack = juce::jmin(1.0f, static_cast<float>(elapsed) / juce::jmax(1.0f, static_cast<float>(sampleRate * 0.0012)));
                    const float decay = static_cast<float>(std::exp(-tSec / 0.0085));
                    const float sampleVal = static_cast<float>(std::sin(clickPhase)) * clickAmp * masterGain * attack * decay;

                    clickPhase += (twoPi * clickFreqHz) / sampleRate;
                    --clickSamplesRemaining;

                    const int dstIdx = bufferToFill.startSample + i;
                    for (int ch = 0; ch < numCh; ++ch)
                        bufferToFill.buffer->getWritePointer(ch)[dstIdx] += sampleVal;
                }
            }
        }

        StemAudioEngine& owner;
        double sampleRate = 44100.0;

        int lastTickBar = -1;
        int lastTickSub = -1;
        int clickTotalSamples = 0;
        int clickSamplesRemaining = 0;
        double clickPhase = 0.0;
        double clickFreqHz = 880.0;
        float clickAmp = 0.6f;
    };

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource legacyTransportSource;
    MasterTimelineSource masterSource;

    mutable juce::SpinLock clipLock;
    std::array<std::shared_ptr<const DecodedAudioClip>, kNumStemSlots> stemClips;
    std::array<juce::File, kNumStemSlots> stemFiles;
    std::array<std::atomic<float>, kNumStemSlots> stemVolumes;

    std::atomic<const BeatTimeMap*> beatTimeMapPtr{ nullptr };
    std::atomic<bool> masterPlaying{ false };
    std::atomic<bool> justStarted{ false };
    std::atomic<double> timelinePosSec{ 0.0 };
    std::atomic<double> pendingSeekSec{ -1.0 };
    std::atomic<double> fullMixOffsetSec{ 0.0 };
    std::atomic<bool> metronomeEnabled{ false };
    std::atomic<float> metronomeGain{ 0.65f };

    bool audioDeviceEnabled = true;
};