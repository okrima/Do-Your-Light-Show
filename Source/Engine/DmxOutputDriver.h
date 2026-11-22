#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// Buffer DMX512 thread-safe condiviso tra UI/Engine (scrittura) e Thread DMX 40 Hz (lettura)
class DmxUniverseBuffer
{
public:
    static constexpr int kNumChannels = 512;

    DmxUniverseBuffer()
    {
        channels.fill(0);
    }

    // Imposta un singolo canale DMX (1..512)
    void setChannel(int dmxChannel1to512, uint8_t value) noexcept
    {
        if (dmxChannel1to512 < 1 || dmxChannel1to512 > kNumChannels)
            return;

        const juce::SpinLock::ScopedLockType sl(lock);
        channels[static_cast<size_t>(dmxChannel1to512 - 1)] = value;
    }

    // Legge un singolo canale DMX (1..512) per il monitor UI
    uint8_t getChannel(int dmxChannel1to512) const noexcept
    {
        if (dmxChannel1to512 < 1 || dmxChannel1to512 > kNumChannels)
            return 0;

        const juce::SpinLock::ScopedLockType sl(lock);
        return channels[static_cast<size_t>(dmxChannel1to512 - 1)];
    }

    // Sovrascrive l'intero universo in un singolo lock atomico (usato dal DmxLiveEngine)
    void writeUniverse(const std::array<uint8_t, kNumChannels>& frame) noexcept
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        channels = frame;
    }

    // Copia l'intero universo verso il driver TX
    void copyUniverseTo(std::array<uint8_t, kNumChannels>& dest) const noexcept
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        dest = channels;
    }

    // Azzera tutti i 512 canali (emergenza / chiusura app)
    void clearAll() noexcept
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        channels.fill(0);
    }

private:
    mutable juce::SpinLock lock;
    std::array<uint8_t, kNumChannels> channels{};
};

// Interfaccia astratta per tutti i driver hardware/rete DMX
class DmxOutputDriver
{
public:
    virtual ~DmxOutputDriver() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Invia esattamente 512 byte DMX (canali 1..512)
    virtual bool sendFrame(const uint8_t* dmx512) = 0;

    virtual juce::String getDriverName() const = 0;
    virtual juce::String getStatusText() const = 0;

    struct DebugSnapshot
    {
        bool isSerialDriver = false;
        bool isOpen = false;
        juce::String portOrTarget;
        int baudRate = 0;
        uint64_t txPacketsSent = 0;
        uint64_t txPacketsSkipped = 0; // Frame identici saltati (keep-alive)
        uint64_t txBytesTotal = 0;
        uint64_t txErrors = 0;
        double lastTxDurationMs = 0.0;
        juce::String lastTxHexPreview;
        juce::String lastRxHex;
        juce::StringArray logLines;
    };

    virtual DebugSnapshot getDebugSnapshot() const
    {
        DebugSnapshot s;
        s.isOpen = isOpen();
        s.portOrTarget = getDriverName();
        return s;
    }

    // Invia un comando diagnostico (es. Get Widget Params / Serial su Enttec/Eurolite)
    virtual juce::String runDiagnosticProbe()
    {
        return "Diagnostica seriale disponibile solo per driver USB DMX Pro / Eurolite.";
    }
};