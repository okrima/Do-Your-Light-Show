#pragma once
#include "DmxOutputDriver.h"
#include "ArtNetDmxDriver.h"
#include "SacnDmxDriver.h"
#include "EnttecProDmxDriver.h"
#include <cmath>
#include <memory>

#if JUCE_WINDOWS
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

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

    // Sveglia immediatamente il thread TX quando i dati DMX cambiano (latenza < 1 ms)
    void notifyFrameUpdated() noexcept
    {
        if (outputRunning.load(std::memory_order_acquire))
        {
            if (!frameDirty.exchange(true, std::memory_order_acq_rel))
                notify();
        }
    }

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
        frameDirty.store(true, std::memory_order_release);
        outputRunning.store(true, std::memory_order_release);

        // Priority::high riduce il jitter UI senza mai interferire con il thread Audio real-time
        startThread(juce::Thread::Priority::high);
        return true;
    }

    void stopOutput(bool sendFinalBlackout = true)
    {
        outputRunning.store(false, std::memory_order_release);
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

    bool isOutputActive() const noexcept { return outputRunning.load(std::memory_order_acquire); }
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
#if JUCE_WINDOWS
        struct ScopedWinHiResTimer
        {
            ScopedWinHiResTimer() { ::timeBeginPeriod(1); }
            ~ScopedWinHiResTimer() { ::timeEndPeriod(1); }
        } hiResTimerGuard;
#endif

        std::array<uint8_t, DmxUniverseBuffer::kNumChannels> localFrame{};
        double nextPeriodicTickMs = juce::Time::getMillisecondCounterHiRes();
        double earliestDirtySendMs = nextPeriodicTickMs;
        double lastSnapMs = 0.0;

        while (!threadShouldExit())
        {
            const double periodMs = 1000.0 / static_cast<double>(refreshRateHz.load());
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            const bool isDirty = frameDirty.load(std::memory_order_acquire);

            const double targetMs = isDirty
                ? juce::jmin(nextPeriodicTickMs, earliestDirtySendMs)
                : nextPeriodicTickMs;

            if (nowMs < targetMs - 0.5)
            {
                const int waitMs = juce::jmax(1, static_cast<int>(std::floor(targetMs - nowMs)));
                wait(waitMs);
                continue;
            }

            const bool wasDirty = frameDirty.exchange(false, std::memory_order_acq_rel);
            universeBuffer.copyUniverseTo(localFrame);

            bool sentOk = false;
            bool needSnapUpdate = false;
            DmxOutputDriver::DebugSnapshot snap;
            juce::String updatedDriverStatus;

            const double tSendStart = juce::Time::getMillisecondCounterHiRes();

            {
                const juce::ScopedLock sl(driverLock);
                if (activeDriver != nullptr)
                {
                    sentOk = activeDriver->sendFrame(localFrame.data());
                    if ((tSendStart - lastSnapMs) >= 150.0 || !sentOk)
                    {
                        snap = activeDriver->getDebugSnapshot();
                        updatedDriverStatus = activeDriver->getStatusText();
                        needSnapUpdate = true;
                        lastSnapMs = tSendStart;
                    }
                }
            }

            const double tSendEnd = juce::Time::getMillisecondCounterHiRes();
            const double sendDurationMs = tSendEnd - tSendStart;

            if (needSnapUpdate)
            {
                const juce::ScopedLock sl(statusLock);
                cachedDebugSnapshot = std::move(snap);
                if (updatedDriverStatus.isNotEmpty())
                    lastStatusText = updatedDriverStatus;
            }

            if (sentOk)
            {
                txFrameCount.fetch_add(1);
                consecutiveErrors.store(0);
            }
            else
            {
                txErrorCount.fetch_add(1);
                consecutiveErrors.fetch_add(1);
            }

            nextPeriodicTickMs = tSendStart + periodMs;

            // Se c'e' stato un vero invio su filo (dato cambiato o keepalive seriale),
            // rispetta il periodo minimo; se il driver ha fatto skip (< 2 ms), resta pronto a svegliarsi subito
            if (wasDirty || sendDurationMs >= 2.0)
                earliestDirtySendMs = tSendStart + periodMs;
        }
    }

    DmxUniverseBuffer universeBuffer;
    mutable juce::CriticalSection driverLock;
    mutable juce::CriticalSection statusLock;

    std::unique_ptr<DmxOutputDriver> activeDriver;
    juce::String lastStatusText{ "DMX disattivo" };
    DmxOutputDriver::DebugSnapshot cachedDebugSnapshot;

    std::atomic<bool> outputRunning{ false };
    std::atomic<bool> frameDirty{ false };
    std::atomic<int> refreshRateHz{ 40 };
    std::atomic<uint64_t> txFrameCount{ 0 };
    std::atomic<uint64_t> txErrorCount{ 0 };
    std::atomic<int> consecutiveErrors{ 0 };
};