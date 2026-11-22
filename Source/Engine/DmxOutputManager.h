#pragma once
#include "DmxOutputDriver.h"
#include "ArtNetDmxDriver.h"
#include "EnttecProDmxDriver.h"
#include "SacnDmxDriver.h"
#include <memory>

class DmxOutputManager : private juce::Thread
{
public:
    DmxOutputManager()
        : juce::Thread("DmxOutput40HzThread")
    {
    }

    ~DmxOutputManager() override
    {
        stopOutput(true);
    }

    DmxUniverseBuffer& getUniverseBuffer() noexcept { return universeBuffer; }
    const DmxUniverseBuffer& getUniverseBuffer() const noexcept { return universeBuffer; }

    // Avvia l'uscita con il driver scelto (Art-Net o ENTTEC Pro)
    bool startOutput(std::unique_ptr<DmxOutputDriver> driver)
    {
        stopOutput(false);

        if (driver == nullptr || !driver->open())
        {
            lastStatusText = (driver != nullptr) ? driver->getStatusText() : "Driver DMX non valido";
            return false;
        }

        {
            const juce::ScopedLock sl(driverLock);
            activeDriver = std::move(driver);
            lastStatusText = activeDriver->getStatusText();
        }

        txFrameCount.store(0);
        txErrorCount.store(0);
        consecutiveErrors.store(0);
        outputRunning.store(true);

        startThread(juce::Thread::Priority::high);
        return true;
    }

    // Ferma la trasmissione e chiude l'interfaccia (opzionalmente invia 1 frame a zero)
    void stopOutput(bool sendFinalBlackout = true)
    {
        outputRunning.store(false);
        signalThreadShouldExit();
        stopThread(1000);

        const juce::ScopedLock sl(driverLock);
        if (activeDriver != nullptr)
        {
            if (sendFinalBlackout && activeDriver->isOpen())
            {
                std::array<uint8_t, DmxUniverseBuffer::kNumChannels> zeroFrame{};
                activeDriver->sendFrame(zeroFrame.data());
            }
            activeDriver->close();
            lastStatusText = activeDriver->getStatusText();
            activeDriver.reset();
        }
    }

    bool isOutputActive() const noexcept { return outputRunning.load(); }
    uint64_t getTxFrameCount() const noexcept { return txFrameCount.load(); }
    uint64_t getTxErrorCount() const noexcept { return txErrorCount.load(); }

    juce::String getStatusText() const
    {
        const juce::ScopedLock sl(driverLock);
        return activeDriver != nullptr ? activeDriver->getStatusText() : lastStatusText;
    }

    void setRefreshRateHz(int hz) noexcept
    {
        refreshRateHz.store(juce::jlimit(10, 44, hz));
    }

    DmxOutputDriver::DebugSnapshot getDriverDebugSnapshot() const
    {
        const juce::ScopedLock sl(driverLock);
        if (activeDriver != nullptr)
            return activeDriver->getDebugSnapshot();
        return {};
    }

    juce::String runDriverDiagnosticProbe()
    {
        const juce::ScopedLock sl(driverLock);
        if (activeDriver != nullptr)
            return activeDriver->runDiagnosticProbe();
        return "Nessun driver DMX attivo.";
    }

private:
    void run() override
    {
        std::array<uint8_t, DmxUniverseBuffer::kNumChannels> localFrame{};
        double nextTickMs = juce::Time::getMillisecondCounterHiRes();

        while (!threadShouldExit())
        {
            const double periodMs = 1000.0 / static_cast<double>(refreshRateHz.load());
            nextTickMs += periodMs;

            universeBuffer.copyUniverseTo(localFrame);

            bool sentOk = false;
            {
                const juce::ScopedLock sl(driverLock);
                if (activeDriver != nullptr && activeDriver->isOpen())
                    sentOk = activeDriver->sendFrame(localFrame.data());
            }

            if (sentOk)
            {
                txFrameCount.fetch_add(1);
                consecutiveErrors.store(0);
            }
            else
            {
                txErrorCount.fetch_add(1);
                const int errs = consecutiveErrors.fetch_add(1) + 1;
                if (errs >= 40) // ~1 secondo di errori continui (es. USB scollegata)
                {
                    const juce::ScopedLock sl(driverLock);
                    lastStatusText = "Connessione DMX persa (controlla cavo / interfaccia)";
                }
            }

            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            const double remainingMs = nextTickMs - nowMs;

            if (remainingMs > 1.0)
                wait(static_cast<int>(remainingMs));
            else if (remainingMs < -100.0)
                nextTickMs = nowMs; // Evita raffiche di recupero dopo uno stallo del sistema
        }
    }

    DmxUniverseBuffer universeBuffer;
    mutable juce::CriticalSection driverLock;
    std::unique_ptr<DmxOutputDriver> activeDriver;
    juce::String lastStatusText{ "DMX disattivo" };

    std::atomic<bool> outputRunning{ false };
    std::atomic<int> refreshRateHz{ 40 };
    std::atomic<uint64_t> txFrameCount{ 0 };
    std::atomic<uint64_t> txErrorCount{ 0 };
    std::atomic<int> consecutiveErrors{ 0 };
};