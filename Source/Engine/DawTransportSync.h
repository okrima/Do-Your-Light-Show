#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Model/BeatTimeMap.h"
#include <atomic>
#include <cmath>
#include <cstdint>

struct DawTransportState
{
    bool isHostActive = false;
    bool isPlaying = false;
    double timeSeconds = 0.0;
    double ppqPosition = 0.0;
    double ppqBarStart = -1.0;
    double bpm = 120.0;
    int timeSigNum = 4;
    int timeSigDen = 4;
    int64_t barCount = 0;
    bool hasBarCount = false;

    BeatTimeMap::MusicalPosition toMusicalPosition() const noexcept
    {
        const double safeBpm = (bpm > 1.0 && bpm < 500.0) ? bpm : 120.0;
        const int safeNum = (timeSigNum >= 1 && timeSigNum <= 32) ? timeSigNum : 4;
        const int safeDen = BeatTimeMap::sanitizeDenominator(timeSigDen);

        // In Reaper/VST3 Bar 1 Beat 1 = 0.0 PPQ; nell'app i quarti partono da 1.0
        const double totalQ = 1.0 + juce::jmax(0.0, ppqPosition);

        auto pos = BeatTimeMap::makeFixedMeterPosition(totalQ, safeNum, safeDen, safeBpm);
        const double barLenQ = juce::jmax(0.25, pos.barLengthQ);

        if (ppqBarStart >= 0.0 && ppqPosition >= ppqBarStart - 1e-6)
        {
            pos.beatInBarQ = juce::jmax(0.0, ppqPosition - ppqBarStart);
            if (hasBarCount && barCount >= 0)
                pos.continuousBarPos = static_cast<double>(barCount) + (pos.beatInBarQ / barLenQ);
        }

        pos.localBpm = safeBpm;
        return pos;
    }
};

class LockFreeTransportBridge
{
public:
    // Chiamato SOLO sul thread Audio (wait-free, zero lock, zero allocazioni)
    void writeFromAudioThread(const DawTransportState& next) noexcept
    {
        const uint32_t seq = sequence.load(std::memory_order_relaxed);
        sequence.store(seq + 1u, std::memory_order_release);
        snapshot = next;
        sequence.store(seq + 2u, std::memory_order_release);
    }

    // Chiamato sul Message Thread / Timer DMX
    DawTransportState read() const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const uint32_t s1 = sequence.load(std::memory_order_acquire);
            if ((s1 & 1u) != 0u)
                continue;

            const DawTransportState copy = snapshot;
            std::atomic_thread_fence(std::memory_order_acquire);

            const uint32_t s2 = sequence.load(std::memory_order_acquire);
            if (s1 == s2)
                return copy;
        }
        return snapshot;
    }

private:
    std::atomic<uint32_t> sequence{ 0u };
    DawTransportState snapshot;
};