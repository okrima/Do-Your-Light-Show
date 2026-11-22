#pragma once
#include "DmxOutputDriver.h"

class SacnDmxDriver : public DmxOutputDriver
{
public:
    struct Config
    {
        bool useMulticast = true;              // true = 239.255.Hi.Lo automatico, false = targetIp Unicast
        juce::String targetIp{ "127.0.0.1" };  // Usato solo se useMulticast == false
        int port = 5568;                       // Porta standard ANSI E1.31 (sACN)
        int universe = 1;                      // Range standard sACN: 1..63999
        int priority = 100;                    // Priorita' HTP/LTP in rete: 0..200 (default 100)
        juce::String sourceName{ "Do Your Light Show" };
        bool previewData = false;              // true = marca i pacchetti come Preview (bit 7 Options)
    };

    explicit SacnDmxDriver(Config cfg = {})
        : config(std::move(cfg))
    {
        config.universe = juce::jlimit(1, 63999, config.universe);
        config.priority = juce::jlimit(0, 200, config.priority);
        resolvedTargetIp = config.useMulticast ? makeMulticastIp(config.universe) : config.targetIp.trim();
        initPacketHeader();
    }

    ~SacnDmxDriver() override
    {
        close();
    }

    // Calcola l'indirizzo Multicast standard ANSI E1.31 per l'universo (1..63999)
    static juce::String makeMulticastIp(int universe1to63999)
    {
        const int u = juce::jlimit(1, 63999, universe1to63999);
        const int hi = (u >> 8) & 0xFF;
        const int lo = u & 0xFF;
        return "239.255." + juce::String(hi) + "." + juce::String(lo);
    }

    bool open() override
    {
        close();

        resolvedTargetIp = config.useMulticast
            ? makeMulticastIp(config.universe)
            : config.targetIp.trim();

        if (resolvedTargetIp.isEmpty())
        {
            statusText = "Indirizzo IP sACN non valido";
            return false;
        }

        // Bind su porta effimera (0): evita conflitti se sul PC gira un monitor sACN (es. sACNView) sulla 5568
        if (!socket.bindToPort(0))
        {
            statusText = "Errore bind socket UDP sACN";
            return false;
        }

        sequenceCounter = 0;
        initPacketHeader();
        opened = true;

        statusText = "sACN attivo -> " + resolvedTargetIp + ":" + juce::String(config.port)
            + " (U:" + juce::String(config.universe)
            + ", P:" + juce::String(config.priority) + ")";
        return true;
    }

    void close() override
    {
        if (opened)
        {
            // ANSI E1.31 §6.2.6: invia 3 pacchetti con Stream_Terminated (0x40) per rilascio immediato
            packet[112] = static_cast<uint8_t>((config.previewData ? 0x80 : 0x00) | 0x40);
            for (int i = 0; i < 3; ++i)
            {
                packet[111] = sequenceCounter++;
                socket.write(resolvedTargetIp, config.port, packet.data(), static_cast<int>(packet.size()));
            }
            packet[112] = static_cast<uint8_t>(config.previewData ? 0x80 : 0x00);

            socket.shutdown();
            opened = false;
        }
        statusText = "sACN disconnesso";
    }

    bool isOpen() const override
    {
        return opened;
    }

    bool sendFrame(const uint8_t* dmx512) override
    {
        if (!opened || dmx512 == nullptr)
            return false;

        // Sequence Number (0..255 wrap-around) e DMX Start Code (0x00)
        packet[111] = sequenceCounter++;
        packet[112] = static_cast<uint8_t>(config.previewData ? 0x80 : 0x00);
        packet[125] = 0x00;

        std::memcpy(packet.data() + 126, dmx512, 512);

        const int sent = socket.write(resolvedTargetIp, config.port,
            packet.data(), static_cast<int>(packet.size()));

        return sent == static_cast<int>(packet.size());
    }

    juce::String getDriverName() const override
    {
        return "sACN E1.31 (" + juce::String(config.useMulticast ? "Multicast" : "Unicast") + ")";
    }

    juce::String getStatusText() const override
    {
        return statusText;
    }

private:
    void initPacketHeader()
    {
        packet.fill(0);

        // --- 1. ROOT LAYER (bytes 0..37) ---
        packet[0] = 0x00; packet[1] = 0x10; // Preamble Size = 0x0010
        packet[2] = 0x00; packet[3] = 0x00; // Post-amble Size = 0x0000

        // ACN Packet Identifier: "ASC-E1.17\0\0\0" (bytes 4..15)
        static constexpr uint8_t kAcnId[12] = {
            0x41, 0x53, 0x43, 0x2D, 0x45, 0x31, 0x2E, 0x31, 0x37, 0x00, 0x00, 0x00
        };
        std::memcpy(packet.data() + 4, kAcnId, 12);

        // Flags (0x7) + Length (638 - 16 = 622 = 0x026E)
        packet[16] = 0x72; packet[17] = 0x6E;

        // Vector = VECTOR_ROOT_E131_DATA (0x00000004)
        packet[18] = 0x00; packet[19] = 0x00; packet[20] = 0x00; packet[21] = 0x04;

        // CID (16-byte UUID, bytes 22..37)
        std::memcpy(packet.data() + 22, sourceCid.getRawData(), 16);

        // --- 2. E1.31 FRAMING LAYER (bytes 38..114) ---
        // Flags (0x7) + Length (638 - 38 = 600 = 0x0258)
        packet[38] = 0x72; packet[39] = 0x58;

        // Vector = VECTOR_E131_DATA_PACKET (0x00000002)
        packet[40] = 0x00; packet[41] = 0x00; packet[42] = 0x00; packet[43] = 0x02;

        // Source Name (64 byte UTF-8 null-terminated, bytes 44..107)
        const auto utf8Name = config.sourceName.toRawUTF8();
        const size_t copyLen = juce::jmin<size_t>(63, std::strlen(utf8Name));
        std::memcpy(packet.data() + 44, utf8Name, copyLen);
        packet[44 + copyLen] = 0x00;

        // Priority (byte 108)
        packet[108] = static_cast<uint8_t>(juce::jlimit(0, 200, config.priority));

        // Synchronization Address = 0 (bytes 109..110)
        packet[109] = 0x00; packet[110] = 0x00;

        // Sequence Number (byte 111) e Options (byte 112)
        packet[111] = 0x00;
        packet[112] = static_cast<uint8_t>(config.previewData ? 0x80 : 0x00);

        // Universe (Big Endian, 1..63999, bytes 113..114)
        const uint16_t u = static_cast<uint16_t>(juce::jlimit(1, 63999, config.universe));
        packet[113] = static_cast<uint8_t>((u >> 8) & 0xFF);
        packet[114] = static_cast<uint8_t>(u & 0xFF);

        // --- 3. DMP LAYER (bytes 115..637) ---
        // Flags (0x7) + Length (638 - 115 = 523 = 0x020B)
        packet[115] = 0x72; packet[116] = 0x0B;

        // Vector = VECTOR_DMP_SET_PROPERTY (0x02), Address & Data Type (0xA1)
        packet[117] = 0x02;
        packet[118] = 0xA1;

        // First Property Address = 0x0000, Address Increment = 0x0001
        packet[119] = 0x00; packet[120] = 0x00;
        packet[121] = 0x00; packet[122] = 0x01;

        // Property Value Count = 513 (1 byte Start Code + 512 canali DMX = 0x0201)
        packet[123] = 0x02; packet[124] = 0x01;

        // DMX Start Code = 0x00 (byte 125)
        packet[125] = 0x00;
    }

    Config config;
    juce::String resolvedTargetIp;
    juce::Uuid sourceCid;
    juce::DatagramSocket socket{ false };
    std::array<uint8_t, 638> packet{};
    uint8_t sequenceCounter = 0;
    bool opened = false;
    juce::String statusText{ "sACN pronto" };
};