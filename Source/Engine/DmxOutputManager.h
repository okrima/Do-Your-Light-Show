#pragma once
#include "DmxOutputDriver.h"
#include "ArtNetDmxDriver.h"
#include "SacnDmxDriver.h"
#include "EnttecProDmxDriver.h"
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

    bool startOutput(std::unique_ptr<DmxOutputDriver> driver)
    {
        stopOutput(false);

        if (driver == nullptr || !driver->open())
        {
            const juce::String errText = (driver != nullptr) ? driver->getStatusText() : "Driver DMX non valido";
            const auto errSnap = (driver != nullptr) ? driver->getDebugSnapshot() : DmxOutputDriver::DebugSnapshot{};
            const juce::ScopedLock sl(statusLock);
            lastStatusText = errText;
            cachedDebugSnapshot = errSnap;
            return false;
        }

        const juce::String openedStatus = driver->getStatusText();
        const auto initialSnap = driver->getDebugSnapshot();

        {
            const juce::ScopedLock sl(driverLock);
            activeDriver = std::move(driver);
        }
        {
            const juce::ScopedLock sl(statusLock);
            lastStatusText = openedStatus;
            cachedDebugSnapshot = initialSnap;
        }

        txFrameCount.store(0);
        txErrorCount.store(0);
        consecutiveErrors.store(0);
        outputRunning.store(true);

        // Priority::normal evita che il thread TX sottragga cicli a UI e audio
        startThread(juce::Thread::Priority::normal);
        return true;
    }

    void stopOutput(bool sendFinalBlackout = true)
    {
        outputRunning.store(false);
        signalThreadShouldExit();
        stopThread(1000);

        juce::String closedStatus = "DMX disattivo";
        {
            const juce::ScopedLock sl(driverLock);
            if (activeDriver != nullptr)
            {
                if (sendFinalBlackout && activeDriver->isOpen())
                {
                    std::array<uint8_t, DmxUniverseBuffer::kNumChannels> zeroFrame{};
                    activeDriver->sendFrame(zeroFrame.data());
                }
                activeDriver->close();
                closedStatus = activeDriver->getStatusText();
                activeDriver.reset();
            }
        }
        {
            const juce::ScopedLock sl(statusLock);
            lastStatusText = closedStatus;
            cachedDebugSnapshot = {};
        }
    }

    bool isOutputActive() const noexcept { return outputRunning.load(); }
    uint64_t getTxFrameCount() const noexcept { return txFrameCount.load(); }
    uint64_t getTxErrorCount() const noexcept { return txErrorCount.load(); }

    // Legge SOLO statusLock: mai bloccato durante WriteFile / socket.write
    juce::String getStatusText() const
    {
        const juce::ScopedLock sl(statusLock);
        return lastStatusText;
    }

    DmxOutputDriver::DebugSnapshot getDriverDebugSnapshot() const
    {
        const juce::ScopedLock sl(statusLock);
        return cachedDebugSnapshot;
    }

    juce::String runDriverDiagnosticProbe()
    {
        juce::String res = "Nessun driver DMX attivo.";
        DmxOutputDriver::DebugSnapshot snap;
        bool hasSnap = false;
        {
            const juce::ScopedLock sl(driverLock);
            if (activeDriver != nullptr)
            {
                res = activeDriver->runDiagnosticProbe();
                snap = activeDriver->getDebugSnapshot();
                hasSnap = true;
            }
        }
        if (hasSnap)
        {
            const juce::ScopedLock sl(statusLock);
            cachedDebugSnapshot = std::move(snap);
        }
        return res;
    }

    void setRefreshRateHz(int hz) noexcept
    {
        refreshRateHz.store(juce::jlimit(10, 44, hz));
    }

private:
    void run() override
    {
        std::array<uint8_t, DmxUniverseBuffer::kNumChannels> localFrame{};
        double nextTickMs = juce::Time::getMillisecondCounterHiRes();
        double lastSnapMs = 0.0;

        while (!threadShouldExit())
        {
            const double periodMs = 1000.0 / static_cast<double>(refreshRateHz.load());
            nextTickMs += periodMs;

            universeBuffer.copyUniverseTo(localFrame);

            bool sentOk = false;
            bool needSnapUpdate = false;
            DmxOutputDriver::DebugSnapshot snap;
            const double nowBeforeSnap = juce::Time::getMillisecondCounterHiRes();

            {
                const juce::ScopedLock sl(driverLock);
                if (activeDriver != nullptr && activeDriver->isOpen())
                {
                    sentOk = activeDriver->sendFrame(localFrame.data());
                    if ((nowBeforeSnap - lastSnapMs) >= 150.0)
                    {
                        snap = activeDriver->getDebugSnapshot();
                        needSnapUpdate = true;
                        lastSnapMs = nowBeforeSnap;
                    }
                }
            }

            if (needSnapUpdate)
            {
                const juce::ScopedLock sl(statusLock);
                cachedDebugSnapshot = std::move(snap);
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
                if (errs == 40)
                {
                    const juce::ScopedLock sl(statusLock);
                    lastStatusText = "Connessione DMX persa (controlla cavo / interfaccia)";
                }
            }

            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            const double remainingMs = nextTickMs - nowMs;

            if (remainingMs > 1.0)
                wait(static_cast<int>(remainingMs));
            else if (remainingMs < -100.0)
                nextTickMs = nowMs;
        }
    }

    DmxUniverseBuffer universeBuffer;
    mutable juce::CriticalSection driverLock;
    mutable juce::CriticalSection statusLock;

    std::unique_ptr<DmxOutputDriver> activeDriver;
    juce::String lastStatusText{ "DMX disattivo" };
    DmxOutputDriver::DebugSnapshot cachedDebugSnapshot;

    std::atomic<bool> outputRunning{ false };
    std::atomic<int> refreshRateHz{ 40 };
    std::atomic<uint64_t> txFrameCount{ 0 };
    std::atomic<uint64_t> txErrorCount{ 0 };
    std::atomic<int> consecutiveErrors{ 0 };
};