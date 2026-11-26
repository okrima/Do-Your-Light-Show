#pragma once
#include "DmxOutputDriver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")
#endif

class EnttecProDmxDriver : public DmxOutputDriver
{
public:
    struct ComPortInfo
    {
        juce::String portName;     // es. "COM3"
        juce::String friendlyName; // es. "USB Serial Port (COM3)"
        juce::String hardwareId;   // es. "FTDIBUS\\COMPORT&VID_0403&PID_6001"
        bool isFtdi = false;

        juce::String getDisplayLabel() const
        {
            juce::String label = portName;
            if (friendlyName.isNotEmpty())
                label += " - " + friendlyName;
            if (isFtdi)
                label += " [FTDI / Eurolite]";
            return label;
        }
    };

    explicit EnttecProDmxDriver(juce::String portName)
        : comPortName(portName.trim().toUpperCase())
    {
        initPacketFrame();
    }

    ~EnttecProDmxDriver() override
    {
        close();
    }

    // Scansiona le porte COM con rilevamento automatico FTDI (VID_0403 / FTDIBUS)
    static std::vector<ComPortInfo> listDetailedComPorts()
    {
        std::vector<ComPortInfo> result;

#if JUCE_WINDOWS
        static constexpr GUID kPortsClassGuid = {
            0x4d36e978, 0xe325, 0x11ce,{ 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 }
        };

        HDEVINFO devInfo = ::SetupDiGetClassDevsW(&kPortsClassGuid, nullptr, nullptr, DIGCF_PRESENT);
        if (devInfo != INVALID_HANDLE_VALUE)
        {
            SP_DEVINFO_DATA devData{};
            devData.cbSize = sizeof(SP_DEVINFO_DATA);

            for (DWORD idx = 0; ::SetupDiEnumDeviceInfo(devInfo, idx, &devData); ++idx)
            {
                juce::String portName;
                HKEY hKey = ::SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                if (hKey != INVALID_HANDLE_VALUE)
                {
                    wchar_t buf[64]{};
                    DWORD type = 0;
                    DWORD size = sizeof(buf);
                    if (::RegQueryValueExW(hKey, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS)
                        portName = juce::String(buf).trim().toUpperCase();
                    ::RegCloseKey(hKey);
                }

                if (!portName.startsWithIgnoreCase("COM"))
                    continue;

                auto readProp = [&](DWORD prop) -> juce::String
                    {
                        wchar_t buf[512]{};
                        DWORD reqSize = 0;
                        if (::SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, prop, nullptr,
                            reinterpret_cast<PBYTE>(buf), sizeof(buf) - sizeof(wchar_t), &reqSize))
                        {
                            return juce::String(buf).trim();
                        }
                        return {};
                    };

                ComPortInfo info;
                info.portName = portName;
                info.friendlyName = readProp(SPDRP_FRIENDLYNAME);
                info.hardwareId = readProp(SPDRP_HARDWAREID);
                const juce::String mfg = readProp(SPDRP_MFG);

                info.isFtdi = info.hardwareId.containsIgnoreCase("VID_0403")
                    || info.hardwareId.containsIgnoreCase("FTDIBUS")
                    || mfg.containsIgnoreCase("FTDI")
                    || info.friendlyName.containsIgnoreCase("FTDI");

                result.push_back(std::move(info));
            }
            ::SetupDiDestroyDeviceInfoList(devInfo);
        }

        if (result.empty())
        {
            wchar_t targetPath[256]{};
            for (int i = 1; i <= 64; ++i)
            {
                const juce::String candidate = "COM" + juce::String(i);
                if (::QueryDosDeviceW(candidate.toWideCharPointer(), targetPath, 256) > 0)
                {
                    ComPortInfo info;
                    info.portName = candidate;
                    result.push_back(std::move(info));
                }
            }
        }

        std::sort(result.begin(), result.end(), [](const ComPortInfo& a, const ComPortInfo& b)
            {
                if (a.isFtdi != b.isFtdi)
                    return a.isFtdi;
                return a.portName.substring(3).getIntValue() < b.portName.substring(3).getIntValue();
            });
#endif

        return result;
    }

    static juce::StringArray listAvailableComPorts()
    {
        juce::StringArray out;
        for (const auto& info : listDetailedComPorts())
            out.add(info.portName);
        return out;
    }

    bool open() override
    {
        close();

#if JUCE_WINDOWS
        if (comPortName.isEmpty())
        {
            setStatusText("Nessuna porta COM selezionata");
            appendLog("ERRORE: Nessuna porta COM selezionata.");
            return false;
        }

        {
            const juce::ScopedLock sl(debugLock);
            txPacketsSent = 0;
            txPacketsSkipped = 0;
            txBytesTotal = 0;
            txErrors = 0;
            lastTxDurationMs = 0.0;
            hasTxPreview = false;
            lastRxHex.clear();
        }

        if (!openSerialHandle(false))
            return false;

        sessionActive = true;
        consecutiveWriteErrors = 0;
        lastReconnectAttemptMs = 0.0;
        return true;
#else
        setStatusText("Driver COM Win32 disponibile solo su Windows");
        return false;
#endif
    }

    void close() override
    {
        sessionActive = false;
        consecutiveWriteErrors = 0;

#if JUCE_WINDOWS
        if (serialHandle != INVALID_HANDLE_VALUE)
        {
            ::PurgeComm(serialHandle, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
            ::CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
            appendLog("CHIUSO " + comPortName);
        }
#endif
        setStatusText("USB DMX Pro / Eurolite disconnesso");
    }

    bool isOpen() const override
    {
#if JUCE_WINDOWS
        return sessionActive && serialHandle != INVALID_HANDLE_VALUE;
#else
        return false;
#endif
    }

    bool sendFrame(const uint8_t* dmx512) override
    {
#if JUCE_WINDOWS
        if (!sessionActive || dmx512 == nullptr)
            return false;

        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        // Auto-riconnessione trasparente se la porta e' caduta durante il live
        if (serialHandle == INVALID_HANDLE_VALUE)
        {
            if ((nowMs - lastReconnectAttemptMs) < kReconnectIntervalMs)
                return false;

            lastReconnectAttemptMs = nowMs;
            if (!openSerialHandle(true))
                return false;

            consecutiveWriteErrors = 0;
        }

        const bool dataChanged = !hasSentInitialFrame
            || (std::memcmp(lastSentDmx.data(), dmx512, 512) != 0);

        if (!dataChanged && (nowMs - lastTxTimeMs) < kKeepaliveIntervalMs)
        {
            const juce::ScopedLock sl(debugLock);
            ++txPacketsSkipped;
            return true;
        }

        packet[4] = 0x00;
        std::memcpy(packet.data() + 5, dmx512, 512);

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        DWORD bytesWritten = 0;
        const BOOL ok = ::WriteFile(
            serialHandle,
            packet.data(),
            static_cast<DWORD>(packet.size()),
            &bytesWritten,
            nullptr);
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        const double elapsedMs = t1 - t0;

        if (ok != FALSE && bytesWritten == static_cast<DWORD>(packet.size()))
        {
            std::memcpy(lastSentDmx.data(), dmx512, 512);
            hasSentInitialFrame = true;
            lastTxTimeMs = t1;
            consecutiveWriteErrors = 0;

            const juce::ScopedLock sl(debugLock);
            ++txPacketsSent;
            txBytesTotal += bytesWritten;
            lastTxDurationMs = elapsedMs;
            std::memcpy(lastTxPreviewBytes.data(), packet.data(), lastTxPreviewBytes.size());
            hasTxPreview = true;
            return true;
        }

        const DWORD err = ::GetLastError();
        DWORD commErrs = 0;
        COMSTAT commStat{};
        ::ClearCommError(serialHandle, &commErrs, &commStat);
        ::PurgeComm(serialHandle, PURGE_TXABORT | PURGE_TXCLEAR);

        ++consecutiveWriteErrors;
        {
            const juce::ScopedLock sl(debugLock);
            ++txErrors;
            lastTxDurationMs = elapsedMs;
        }

        if (consecutiveWriteErrors <= 3 || (consecutiveWriteErrors % 20) == 0)
        {
            appendLog("ERRORE TX WriteFile: scritti " + juce::String(static_cast<int>(bytesWritten))
                + "/518 byte (Win32 err " + juce::String(static_cast<int>(err)) + ")");
        }

        if (consecutiveWriteErrors >= kErrorsBeforeReconnect)
        {
            ::CloseHandle(serialHandle);
            serialHandle = INVALID_HANDLE_VALUE;
            hasSentInitialFrame = false;
            lastReconnectAttemptMs = t1;
            setStatusText("Connessione persa su " + comPortName + " - riconnessione...");
            appendLog("PORTA PERSA su " + comPortName + ": avvio auto-riconnessione ogni 750 ms...");
        }

        return false;
#else
        juce::ignoreUnused(dmx512);
        return false;
#endif
    }

    DebugSnapshot getDebugSnapshot() const override
    {
        const juce::ScopedLock sl(debugLock);
        DebugSnapshot s;
        s.isSerialDriver = true;
#if JUCE_WINDOWS
        s.isOpen = sessionActive && (serialHandle != INVALID_HANDLE_VALUE);
#else
        s.isOpen = false;
#endif
        s.portOrTarget = comPortName;
        s.baudRate = kBaudRate;
        s.txPacketsSent = txPacketsSent;
        s.txPacketsSkipped = txPacketsSkipped;
        s.txBytesTotal = txBytesTotal;
        s.txErrors = txErrors;
        s.lastTxDurationMs = lastTxDurationMs;
        s.lastTxHexPreview = hasTxPreview ? formatPacketPreview(lastTxPreviewBytes.data(), 24) : juce::String();
        s.lastRxHex = lastRxHex;
        for (const auto& line : logHistory)
            s.logLines.add(line);
        return s;
    }

    juce::String runDiagnosticProbe() override
    {
#if JUCE_WINDOWS
        if (!sessionActive || serialHandle == INVALID_HANDLE_VALUE)
        {
            appendLog("PROBE: porta non aperta.");
            return "Apri prima la porta COM per interrogare l'interfaccia.";
        }

        ::PurgeComm(serialHandle, PURGE_RXCLEAR);

        // 1. Label 0x03: Get Widget Parameters Request
        const uint8_t reqParams[7] = { 0x7E, 0x03, 0x02, 0x00, 0x00, 0x00, 0xE7 };
        DWORD written = 0;
        ::WriteFile(serialHandle, reqParams, sizeof(reqParams), &written, nullptr);
        appendLog("PROBE TX [0x03 GetParams]: " + bytesToHex(reqParams, sizeof(reqParams)));

        uint8_t rxBuf[128]{};
        DWORD bytesRead = 0;
        ::ReadFile(serialHandle, rxBuf, sizeof(rxBuf), &bytesRead, nullptr);

        juce::String summary;
        if (bytesRead > 0)
        {
            const juce::String hexStr = bytesToHex(rxBuf, static_cast<size_t>(bytesRead));
            {
                const juce::ScopedLock sl(debugLock);
                lastRxHex = hexStr;
            }
            appendLog("PROBE RX (" + juce::String(static_cast<int>(bytesRead)) + " byte): " + hexStr);
            summary = "Risposta ricevuta (" + juce::String(static_cast<int>(bytesRead)) + " byte): " + hexStr;

            if (bytesRead >= 10 && rxBuf[0] == 0x7E && rxBuf[1] == 0x03)
            {
                const int fwLsb = rxBuf[4];
                const int fwMsb = rxBuf[5];
                const int breakUs = static_cast<int>(std::round(rxBuf[6] * 10.67));
                const int mabUs = static_cast<int>(std::round(rxBuf[7] * 10.67));
                const int rateHz = rxBuf[8];
                appendLog("PARSED -> FW " + juce::String(fwMsb) + "." + juce::String(fwLsb)
                    + " | Break: " + juce::String(breakUs) + " us | MAB: " + juce::String(mabUs)
                    + " us | Rate: " + juce::String(rateHz) + " Hz");
            }
        }
        else
        {
            appendLog("PROBE RX [0x03]: nessuna risposta (normale sui firmware Eurolite solo-TX).");
            summary = "Nessuna risposta RX al comando 0x03 (normale sui firmware Eurolite solo-TX).";
        }

        // 2. Label 0x0A: Get Widget Serial Number Request
        const uint8_t reqSerial[5] = { 0x7E, 0x0A, 0x00, 0x00, 0xE7 };
        written = 0;
        ::WriteFile(serialHandle, reqSerial, sizeof(reqSerial), &written, nullptr);
        appendLog("PROBE TX [0x0A GetSerial]: " + bytesToHex(reqSerial, sizeof(reqSerial)));

        bytesRead = 0;
        ::ReadFile(serialHandle, rxBuf, sizeof(rxBuf), &bytesRead, nullptr);
        if (bytesRead > 0)
        {
            const juce::String hexStr = bytesToHex(rxBuf, static_cast<size_t>(bytesRead));
            {
                const juce::ScopedLock sl(debugLock);
                lastRxHex = hexStr;
            }
            appendLog("PROBE RX [0x0A] (" + juce::String(static_cast<int>(bytesRead)) + " byte): " + hexStr);
            summary += "\nSerial RX: " + hexStr;
        }

        return summary;
#else
        return "Non supportato su questo OS.";
#endif
    }

    juce::String getDriverName() const override { return "USB DMX Pro / Eurolite MK2 (" + comPortName + ")"; }
    juce::String getStatusText() const override
    {
        const juce::ScopedLock sl(debugLock);
        return statusText;
    }

private:
    static constexpr int kBaudRate = 250000;
    static constexpr double kKeepaliveIntervalMs = 150.0;
    static constexpr double kReconnectIntervalMs = 750.0;
    static constexpr int kErrorsBeforeReconnect = 3;

#if JUCE_WINDOWS
    bool openSerialHandle(bool isReconnect)
    {
        const juce::String devicePath = "\\\\.\\" + comPortName;
        HANDLE h = ::CreateFileW(
            devicePath.toWideCharPointer(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD err = ::GetLastError();
            if (!isReconnect)
            {
                setStatusText("Impossibile aprire " + comPortName + " (err " + juce::String(static_cast<int>(err)) + ")");
                appendLog("ERRORE CreateFile(" + comPortName + "): codice Win32 " + juce::String(static_cast<int>(err)));
            }
            return false;
        }

        ::SetupComm(h, 4096, 4096);

        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);
        if (!::GetCommState(h, &dcb))
        {
            ::CloseHandle(h);
            if (!isReconnect)
            {
                setStatusText("Errore lettura configurazione " + comPortName);
                appendLog("ERRORE GetCommState su " + comPortName);
            }
            return false;
        }

        dcb.BaudRate = kBaudRate;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = TWOSTOPBITS;
        dcb.fBinary = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fAbortOnError = FALSE;

        if (!::SetCommState(h, &dcb))
        {
            ::CloseHandle(h);
            if (!isReconnect)
            {
                setStatusText("Errore impostazione 250000 8N2 su " + comPortName);
                appendLog("ERRORE SetCommState(250000 8N2) su " + comPortName);
            }
            return false;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 80;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 45; // 518 byte @ 250k baud 8N2 = ~22.8 ms
        ::SetCommTimeouts(h, &timeouts);

        ::PurgeComm(h, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

        serialHandle = h;
        hasSentInitialFrame = false;
        lastTxTimeMs = 0.0;
        setStatusText("USB DMX Pro / Eurolite connesso su " + comPortName);
        appendLog((isReconnect ? "RICONNESSO " : "APERTO ") + comPortName
            + " @ 250000 baud (8N2, Buf 4KB, DTR/RTS ON, Timeout TX 45 ms)");
        return true;
    }
#endif

    void setStatusText(const juce::String& text)
    {
        const juce::ScopedLock sl(debugLock);
        statusText = text;
    }

    static juce::String bytesToHex(const uint8_t* data, size_t len)
    {
        juce::String s;
        s.preallocateBytes(len * 3);
        for (size_t i = 0; i < len; ++i)
        {
            if (i > 0) s += ' ';
            s += juce::String::toHexString(static_cast<int>(data[i])).paddedLeft('0', 2).toUpperCase();
        }
        return s;
    }

    static juce::String formatPacketPreview(const uint8_t* pkt29, int numDmxChannels)
    {
        juce::String out = "HDR[" + bytesToHex(pkt29, 5) + "]  CH1.." + juce::String(numDmxChannels) + "[";
        out += bytesToHex(pkt29 + 5, static_cast<size_t>(numDmxChannels));
        out += "] ... END[E7]";
        return out;
    }

    void appendLog(const juce::String& msg)
    {
        const juce::String stamped = "[" + juce::Time::getCurrentTime().formatted("%H:%M:%S") + "] " + msg;
        const juce::ScopedLock sl(debugLock);
        logHistory.push_back(stamped);
        while (logHistory.size() > 100)
            logHistory.pop_front();
    }

    void initPacketFrame()
    {
        packet.fill(0);
        packet[0] = 0x7E;
        packet[1] = 0x06;
        packet[2] = 0x01;
        packet[3] = 0x02;
        packet[4] = 0x00;
        packet[517] = 0xE7;
    }

    juce::String comPortName;
    std::array<uint8_t, 518> packet{};
    std::array<uint8_t, 512> lastSentDmx{};
    bool hasSentInitialFrame = false;
    double lastTxTimeMs = 0.0;
    double lastReconnectAttemptMs = 0.0;
    int consecutiveWriteErrors = 0;
    bool sessionActive = false;

    mutable juce::CriticalSection debugLock;
    juce::String statusText{ "USB DMX Pro / Eurolite pronto" };
    uint64_t txPacketsSent = 0;
    uint64_t txPacketsSkipped = 0;
    uint64_t txBytesTotal = 0;
    uint64_t txErrors = 0;
    double lastTxDurationMs = 0.0;
    std::array<uint8_t, 29> lastTxPreviewBytes{};
    bool hasTxPreview = false;
    juce::String lastRxHex;
    std::deque<juce::String> logHistory;

#if JUCE_WINDOWS
    HANDLE serialHandle = INVALID_HANDLE_VALUE;
#endif
};