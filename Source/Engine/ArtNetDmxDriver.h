#pragma once
#include "DmxOutputDriver.h"

class ArtNetDmxDriver : public DmxOutputDriver
{
public:
    struct Config
    {
        juce::String targetIp{ "255.255.255.255" }; // Broadcast, IP nodo unicast o "127.0.0.1" per test locali
        int port = 6454;                            // Porta standard Art-Net UDP
        int net = 0;                                // 0..127
        int subnet = 0;                             // 0..15
        int universe = 0;                           // 0..15
    };

    explicit ArtNetDmxDriver(Config cfg = {})
        : config(std::move(cfg))
    {
        initPacketHeader();
    }

    ~ArtNetDmxDriver() override
    {
        close();
    }

    bool open() override
    {
        close();

        // Bind su porta effimera (0): evita conflitti se sullo stesso PC gira un ricevitore Art-Net sulla 6454
        if (!socket.bindToPort(0))
        {
            statusText = "Errore bind socket UDP Art-Net";
            return false;
        }

        initPacketHeader();
        opened = true;
        statusText = "Art-Net attivo -> " + config.targetIp + ":" + juce::String(config.port)
            + " (U:" + juce::String(config.universe) + ")";
        return true;
    }

    void close() override
    {
        if (opened)
        {
            socket.shutdown();
            opened = false;
        }
        statusText = "Art-Net disconnesso";
    }

    bool isOpen() const override
    {
        return opened;
    }

    bool sendFrame(const uint8_t* dmx512) override
    {
        if (!opened || dmx512 == nullptr)
            return false;

        // Sequence counter Art-Net: 1..255 (0 disabilita il controllo sequenza)
        sequenceCounter = static_cast<uint8_t>((sequenceCounter % 255) + 1);
        packet[12] = sequenceCounter;

        std::memcpy(packet.data() + 18, dmx512, 512);

        const int sent = socket.write(config.targetIp, config.port,
            packet.data(), static_cast<int>(packet.size()));

        return sent == static_cast<int>(packet.size());
    }

    juce::String getDriverName() const override { return "Art-Net 4 (UDP)"; }
    juce::String getStatusText() const override { return statusText; }

private:
    void initPacketHeader()
    {
        packet.fill(0);

        // ID "Art-Net\0" (bytes 0..7)
        static constexpr char kHeader[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', '\0' };
        std::memcpy(packet.data(), kHeader, 8);

        // OpCode OpOutput / OpDmx = 0x5000 (Little Endian, bytes 8..9)
        packet[8] = 0x00;
        packet[9] = 0x50;

        // ProtVer = 14 (Big Endian, bytes 10..11)
        packet[10] = 0x00;
        packet[11] = 0x0E;

        // Sequence (12) e Physical (13)
        packet[12] = 0x01;
        packet[13] = 0x00;

        // Port-Address (15-bit, Little Endian, bytes 14..15)
        const uint8_t subUni = static_cast<uint8_t>(((config.subnet & 0x0F) << 4) | (config.universe & 0x0F));
        const uint8_t netByte = static_cast<uint8_t>(config.net & 0x7F);
        packet[14] = subUni;
        packet[15] = netByte;

        // Length = 512 (Big Endian, bytes 16..17)
        packet[16] = 0x02;
        packet[17] = 0x00;
    }

    Config config;
    juce::DatagramSocket socket{ true }; // true = abilita SO_BROADCAST
    std::array<uint8_t, 530> packet{};
    uint8_t sequenceCounter = 0;
    bool opened = false;
    juce::String statusText{ "Art-Net pronto" };
};