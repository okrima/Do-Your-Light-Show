#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Engine/DmxLiveEngine.h"
#include <array>
#include <memory>

class DmxOutputControlPanel : public juce::Component,
    private juce::Timer
{
public:
    explicit DmxOutputControlPanel(DmxLiveEngine& engine)
        : dmxEngine(engine)
    {
        setOpaque(true);

        auto setupLbl = [this](juce::Label& l, const juce::String& txt, bool header = false, juce::Colour col = juce::Colours::lightgrey)
            {
                l.setText(txt, juce::dontSendNotification);
                l.setFont(juce::FontOptions(header ? 14.5f : 12.0f, header ? juce::Font::bold : juce::Font::plain));
                l.setColour(juce::Label::textColourId, col);
                addAndMakeVisible(l);
            };

        saveConfigBtn.setButtonText("Salva Config");
        saveConfigBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d));
        saveConfigBtn.setTooltip("Salva protocollo, IP/Universo, porta COM e opzioni Tab 6 su disco");
        saveConfigBtn.onClick = [this]() { saveSettingsToDisk(); };
        addAndMakeVisible(saveConfigBtn);

        auto setupEdit = [this](juce::TextEditor& e, const juce::String& defTxt)
            {
                e.setText(defTxt, juce::dontSendNotification);
                e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222430));
                e.setColour(juce::TextEditor::textColourId, juce::Colours::white);
                addAndMakeVisible(e);
            };

        setupLbl(titleLbl, "USCITA DMX NATIVA LIVE (40 Hz)", true, juce::Colour(0xff00d2ff));
        setupLbl(driverLbl, "Protocollo / Driver:");

        driverCombo.addItem("Art-Net 4 (UDP 6454)", 1);
        driverCombo.addItem("sACN E1.31 (UDP 5568)", 2);
        driverCombo.addItem("USB DMX Pro / Eurolite MK2 (COM)", 3);
        driverCombo.setSelectedId(1, juce::dontSendNotification);
        driverCombo.onChange = [this]() { updateDriverConfigVisibility(); markDirty(); };
        addAndMakeVisible(driverCombo);

        // --- Parametri Art-Net ---
        setupLbl(artNetIpLbl, "IP Target:");
        setupEdit(artNetIpEdit, "255.255.255.255");
        artNetLocalBtn.setButtonText("Localhost");
        artNetLocalBtn.setTooltip("Imposta 127.0.0.1 per testare in locale senza hardware");
        artNetLocalBtn.onClick = [this]() { artNetIpEdit.setText("127.0.0.1", juce::dontSendNotification); markDirty(); };
        artNetIpEdit.onTextChange = [this]() { markDirty(); };
        addAndMakeVisible(artNetLocalBtn);

        artNetBcastBtn.setButtonText("Broadcast");
        artNetBcastBtn.setTooltip("Imposta 255.255.255.255 per tutti i nodi Art-Net in rete");
        artNetBcastBtn.onClick = [this]() { artNetIpEdit.setText("255.255.255.255", juce::dontSendNotification); markDirty(); };
        addAndMakeVisible(artNetBcastBtn);

        setupLbl(artNetUniLbl, "Universo (0-15):");
        artNetUniSlider.setSliderStyle(juce::Slider::IncDecButtons);
        artNetUniSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 22);
        artNetUniSlider.setRange(0, 15, 1);
        artNetUniSlider.setValue(0, juce::dontSendNotification);
        artNetUniSlider.onValueChange = [this]() { markDirty(); };
        addAndMakeVisible(artNetUniSlider);

        setupLbl(artNetSubLbl, "Subnet (0-15):");
        artNetSubSlider.setSliderStyle(juce::Slider::IncDecButtons);
        artNetSubSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 22);
        artNetSubSlider.setRange(0, 15, 1);
        artNetSubSlider.setValue(0, juce::dontSendNotification);
        artNetSubSlider.onValueChange = [this]() { markDirty(); };
        addAndMakeVisible(artNetSubSlider);

        setupLbl(artNetNetLbl, "Net (0-127):");
        artNetNetSlider.setSliderStyle(juce::Slider::IncDecButtons);
        artNetNetSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 22);
        artNetNetSlider.setRange(0, 127, 1);
        artNetNetSlider.setValue(0, juce::dontSendNotification);
        artNetNetSlider.onValueChange = [this]() { markDirty(); };
        addAndMakeVisible(artNetNetSlider);

        // --- Parametri sACN E1.31 ---
        sacnMulticastToggle.setButtonText("Multicast E1.31 (239.255.x.x)");
        sacnMulticastToggle.setToggleState(true, juce::dontSendNotification);
        sacnMulticastToggle.onClick = [this]() { updateSacnTargetPreview(); markDirty(); };
        addAndMakeVisible(sacnMulticastToggle);

        setupLbl(sacnIpLbl, "IP Destinazione:");
        setupEdit(sacnIpEdit, "239.255.0.1");

        sacnIpEdit.onTextChange = [this]() { markDirty(); };

        setupLbl(sacnUniLbl, "Universo (1-63999):");
        sacnUniSlider.setSliderStyle(juce::Slider::IncDecButtons);
        sacnUniSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 58, 22);
        sacnUniSlider.setRange(1, 63999, 1);
        sacnUniSlider.setValue(1, juce::dontSendNotification);
        sacnUniSlider.onValueChange = [this]() { updateSacnTargetPreview(); markDirty(); };
        addAndMakeVisible(sacnUniSlider);

        setupLbl(sacnPrioLbl, "Priorita' (0-200):");
        sacnPrioSlider.setSliderStyle(juce::Slider::IncDecButtons);
        sacnPrioSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 22);
        sacnPrioSlider.setRange(0, 200, 1);
        sacnPrioSlider.setValue(100, juce::dontSendNotification);
        sacnPrioSlider.onValueChange = [this]() { markDirty(); };
        addAndMakeVisible(sacnPrioSlider);

        // --- Parametri ENTTEC Pro ---
        setupLbl(comPortLbl, "Porta COM:");
        comPortCombo.onChange = [this]() { markDirty(); };
        addAndMakeVisible(comPortCombo);
        refreshComBtn.setButtonText("Aggiorna COM");
        refreshComBtn.onClick = [this]() { refreshComPorts(); };
        addAndMakeVisible(refreshComBtn);

        debugSerialBtn.setButtonText("Debug Eurolite...");
        debugSerialBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4338ca));
        debugSerialBtn.setTooltip("Apre console di debug seriale 250k baud, hex dump pacchetti e test canale");
        debugSerialBtn.onClick = [this]() { openSerialDebugWindow(); };
        addAndMakeVisible(debugSerialBtn);

        refreshComPorts();

        // --- Azioni Connessione ---
        connectBtn.setButtonText("Avvia Uscita DMX (40 Hz)");
        connectBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f766e));
        connectBtn.onClick = [this]() { toggleConnection(); };
        addAndMakeVisible(connectBtn);

        miniConsoleBtn.setButtonText("Apri Mini-Console Mobile");
        miniConsoleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff334155));
        miniConsoleBtn.setTooltip("Apre una finestra galleggiante Always-on-Top con Master Dimmer e BLACKOUT");
        miniConsoleBtn.onClick = [this]() { openMiniConsole(); };
        addAndMakeVisible(miniConsoleBtn);

        setupLbl(statusLbl, "Stato: DMX disattivo", false, juce::Colour(0xff94a3b8));
        setupLbl(statsLbl, "TX: 0 frame | Errori: 0", false, juce::Colour(0xff64748b));

        // --- Sezione Live Safety (Master Dimmer + Blackout + Cortesia Tab 2) ---
        setupLbl(liveTitleLbl, "CONTROLLO LIVE & SICUREZZA PALCO", true, juce::Colour(0xfff59e0b));
        setupLbl(masterLbl, "Master Dimmer:");

        masterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        masterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22);
        masterSlider.setRange(0, 100, 1);
        masterSlider.setValue(100, juce::dontSendNotification);
        masterSlider.setTextValueSuffix("%");
        masterSlider.setTooltip("Scala l'intensita' di tutti i fari senza muovere Pan/Tilt");
        masterSlider.onValueChange = [this]() {
            dmxEngine.setMasterDimmer(static_cast<float>(masterSlider.getValue()) / 100.0f);
            markDirty();
            };
        addAndMakeVisible(masterSlider);

        blackoutBtn.setButtonText("BLACKOUT");
        blackoutBtn.setClickingTogglesState(true);
        blackoutBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff451a1a));
        blackoutBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdc2626));
        blackoutBtn.setTooltip("Spegne subito Dimmer, Strobo e Geyser mantenendo fermi i Pan/Tilt");
        blackoutBtn.onClick = [this]() {
            dmxEngine.setBlackout(blackoutBtn.getToggleState());
            blackoutBtn.setButtonText(blackoutBtn.getToggleState() ? "BLACKOUT ATTIVO" : "BLACKOUT");
            };
        addAndMakeVisible(blackoutBtn);

        tab2LiveToggle.setButtonText("Invia Anteprima Tab 2 su DMX");
        tab2LiveToggle.setToggleState(dmxEngine.isTab2LivePreviewEnabled(), juce::dontSendNotification);
        tab2LiveToggle.setTooltip("Se spento, aprire la Tab 2 non accende i fari fisici");
        tab2LiveToggle.onClick = [this]() {
            const bool on = tab2LiveToggle.getToggleState();
            dmxEngine.setTab2LivePreviewEnabled(on);
            courtesyToggle.setEnabled(on);
            if (!on && !dmxEngine.isFixtureTestActive())
                dmxEngine.clearAllOutput();
            markDirty();
            };
        addAndMakeVisible(tab2LiveToggle);

        courtesyToggle.setButtonText("Cortesia Dimmer Tab 2");
        courtesyToggle.setToggleState(dmxEngine.getStandaloneCourtesyDimmer(), juce::dontSendNotification);
        courtesyToggle.setEnabled(dmxEngine.isTab2LivePreviewEnabled());
        courtesyToggle.onClick = [this]() {
            dmxEngine.setStandaloneCourtesyDimmer(courtesyToggle.getToggleState());
            markDirty();
            };
        addAndMakeVisible(courtesyToggle);

        autoConnectToggle.setButtonText("Auto-connetti DMX all'avvio (Standalone & VST3)");
        autoConnectToggle.setTooltip("Avvia automaticamente l'interfaccia DMX salvata appena si apre l'app o il progetto Reaper");
        autoConnectToggle.onClick = [this]() { markDirty(); };
        addAndMakeVisible(autoConnectToggle);

        // --- Monitor 512 Canali ---
        setupLbl(monitorTitleLbl, "MONITOR UNIVERSO DMX (512 CANALI LIVE)", true, juce::Colour(0xff38bdf8));

        const char* pageNames[4] = { "Ch 1 - 128", "Ch 129 - 256", "Ch 257 - 384", "Ch 385 - 512" };
        for (int p = 0; p < 4; ++p)
        {
            auto btn = std::make_unique<juce::TextButton>(pageNames[p]);
            btn->onClick = [this, p]() {
                activeMonitorPage = p;
                updateMonitorPageButtons();
                repaint();
                };
            addAndMakeVisible(*btn);
            pageBtns.push_back(std::move(btn));
        }

        updateSacnTargetPreview();
        updateDriverConfigVisibility();
        updateMonitorPageButtons();

        loadSettingsFromDisk();

        refreshStatusUI();

        startTimerHz(60);
    }

    ~DmxOutputControlPanel() override
    {
        stopTimer();
        miniConsoleWindow.reset();
        serialDebugWindow.reset();
    }

    void visibilityChanged() override
    {
        if (isShowing())
        {
            std::array<uint8_t, 512> fresh{}, peaks{};
            dmxEngine.consumeMonitorSnapshot(fresh, peaks);
            monitorSnapshot = fresh;
            refreshStatusUI();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff14151c));

        // Card superiore configurazione + sicurezza
        g.setColour(juce::Colour(0xff1b1d28));
        g.fillRoundedRectangle(topCardRect.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff2c3042));
        g.drawRoundedRectangle(topCardRect.toFloat(), 6.0f, 1.0f);

        // LED TX 40 Hz
        const bool active = dmxEngine.isOutputActive();
        const bool blackout = dmxEngine.isBlackout();
        juce::Colour ledCol = !active ? juce::Colour(0xff475569)
            : (blackout ? juce::Colour(0xffef4444) : juce::Colour(0xff10b981));
        g.setColour(ledCol.withAlpha(0.28f));
        g.fillEllipse(ledRect.expanded(3.0f));
        g.setColour(ledCol);
        g.fillEllipse(ledRect);

        // Griglia Monitor 16x8 (128 canali della pagina attiva)
        drawChannelMonitorGrid(g, monitorGridRect);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(12);

        topCardRect = b.removeFromTop(194);



        auto card = topCardRect.reduced(12, 10);

        auto leftHalf = card.removeFromLeft(juce::roundToInt(card.getWidth() * 0.56f));
        card.removeFromLeft(16);
        auto rightHalf = card;

        // --- Sinistra: Configurazione Driver ---
        auto lTitle = leftHalf.removeFromTop(22);
        ledRect = juce::Rectangle<float>(static_cast<float>(lTitle.getX() + 2), static_cast<float>(lTitle.getCentreY() - 5), 10.0f, 10.0f);
        lTitle.removeFromLeft(18);
        saveConfigBtn.setBounds(lTitle.removeFromRight(105));
        lTitle.removeFromRight(8);
        titleLbl.setBounds(lTitle);
        leftHalf.removeFromTop(6);

        auto rowDrv = leftHalf.removeFromTop(24);
        driverLbl.setBounds(rowDrv.removeFromLeft(122));
        driverCombo.setBounds(rowDrv.removeFromLeft(220));
        rowDrv.removeFromLeft(10);
        connectBtn.setBounds(rowDrv);
        leftHalf.removeFromTop(8);

        // Area parametri contestuali (2 righe)
        auto cfgRow1 = leftHalf.removeFromTop(24);
        leftHalf.removeFromTop(6);
        auto cfgRow2 = leftHalf.removeFromTop(24);

        const int drvId = driverCombo.getSelectedId();
        if (drvId == 1) // Art-Net
        {
            artNetIpLbl.setBounds(cfgRow1.removeFromLeft(74));
            artNetIpEdit.setBounds(cfgRow1.removeFromLeft(160));
            cfgRow1.removeFromLeft(6);
            artNetLocalBtn.setBounds(cfgRow1.removeFromLeft(76));
            cfgRow1.removeFromLeft(4);
            artNetBcastBtn.setBounds(cfgRow1.removeFromLeft(76));

            artNetUniLbl.setBounds(cfgRow2.removeFromLeft(102));
            artNetUniSlider.setBounds(cfgRow2.removeFromLeft(88));
            cfgRow2.removeFromLeft(10);
            artNetSubLbl.setBounds(cfgRow2.removeFromLeft(86));
            artNetSubSlider.setBounds(cfgRow2.removeFromLeft(88));
            cfgRow2.removeFromLeft(10);
            artNetNetLbl.setBounds(cfgRow2.removeFromLeft(76));
            artNetNetSlider.setBounds(cfgRow2.removeFromLeft(88));
        }
        else if (drvId == 2) // sACN
        {
            sacnMulticastToggle.setBounds(cfgRow1.removeFromLeft(205));
            cfgRow1.removeFromLeft(8);
            sacnIpLbl.setBounds(cfgRow1.removeFromLeft(96));
            sacnIpEdit.setBounds(cfgRow1.removeFromLeft(155));

            sacnUniLbl.setBounds(cfgRow2.removeFromLeft(120));
            sacnUniSlider.setBounds(cfgRow2.removeFromLeft(106));
            cfgRow2.removeFromLeft(14);
            sacnPrioLbl.setBounds(cfgRow2.removeFromLeft(106));
            sacnPrioSlider.setBounds(cfgRow2.removeFromLeft(96));
        }
        else // USB DMX Pro / Eurolite MK2
        {
            comPortLbl.setBounds(cfgRow1.removeFromLeft(80));
            comPortCombo.setBounds(cfgRow1.removeFromLeft(280));
            cfgRow1.removeFromLeft(6);
            refreshComBtn.setBounds(cfgRow1.removeFromLeft(105));

            debugSerialBtn.setBounds(cfgRow2.removeFromLeft(160));
        }

        leftHalf.removeFromTop(8);
        auto stRow = leftHalf.removeFromTop(20);
        statusLbl.setBounds(stRow.removeFromLeft(juce::roundToInt(stRow.getWidth() * 0.62f)));
        statsLbl.setBounds(stRow);

        // --- Destra: Controllo Live & Sicurezza ---
        liveTitleLbl.setBounds(rightHalf.removeFromTop(22));
        rightHalf.removeFromTop(8);

        auto mRow = rightHalf.removeFromTop(26);
        masterLbl.setBounds(mRow.removeFromLeft(96));
        masterSlider.setBounds(mRow);
        rightHalf.removeFromTop(10);

        auto actRow = rightHalf.removeFromTop(38);
        blackoutBtn.setBounds(actRow.removeFromLeft(juce::roundToInt(actRow.getWidth() * 0.54f)).reduced(2));
        actRow.removeFromLeft(8);
        miniConsoleBtn.setBounds(actRow.reduced(2));

        rightHalf.removeFromTop(8);
        auto prevRow = rightHalf.removeFromTop(22);
        tab2LiveToggle.setBounds(prevRow.removeFromLeft(235));
        prevRow.removeFromLeft(8);
        courtesyToggle.setBounds(prevRow);

        rightHalf.removeFromTop(4);
        autoConnectToggle.setBounds(rightHalf.removeFromTop(22));

        // --- Parte Inferiore: Monitor 512 Canali ---
        b.removeFromTop(12);
        auto monHdr = b.removeFromTop(26);
        monitorTitleLbl.setBounds(monHdr.removeFromLeft(320));
        for (auto& btn : pageBtns)
            btn->setBounds(monHdr.removeFromLeft(104).reduced(2));

        b.removeFromTop(6);
        monitorGridRect = b;
    }

    std::function<void(bool)> onDirtyStateChanged;

    bool hasUnsavedChanges() const noexcept { return dirty; }

    static juce::File getSettingsFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DoYourLightShow")
            .getChildFile("dmx_settings.xml");
    }

    bool saveSettingsToDisk()
    {
        auto file = getSettingsFile();
        file.getParentDirectory().createDirectory();

        juce::XmlElement root("DMX_LIVE_SETTINGS");
        root.setAttribute("driverId", driverCombo.getSelectedId());

        root.setAttribute("artNetIp", artNetIpEdit.getText().trim());
        root.setAttribute("artNetUni", static_cast<int>(artNetUniSlider.getValue()));
        root.setAttribute("artNetSub", static_cast<int>(artNetSubSlider.getValue()));
        root.setAttribute("artNetNet", static_cast<int>(artNetNetSlider.getValue()));

        root.setAttribute("sacnMulticast", sacnMulticastToggle.getToggleState());
        root.setAttribute("sacnIp", sacnIpEdit.getText().trim());
        root.setAttribute("sacnUni", static_cast<int>(sacnUniSlider.getValue()));
        root.setAttribute("sacnPrio", static_cast<int>(sacnPrioSlider.getValue()));

        root.setAttribute("comPort", getSelectedComPortName());
        root.setAttribute("masterPct", static_cast<int>(masterSlider.getValue()));
        root.setAttribute("tab2Live", tab2LiveToggle.getToggleState());
        root.setAttribute("courtesyDim", courtesyToggle.getToggleState());

        root.setAttribute("autoConnect", autoConnectToggle.getToggleState());

        if (root.writeTo(file))
        {
            setDirty(false);
            return true;
        }
        return false;
    }

    void loadSettingsFromDisk()
    {
        auto file = getSettingsFile();
        if (!file.existsAsFile())
            return;

        auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || !xml->hasTagName("DMX_LIVE_SETTINGS"))
            return;

        juce::ScopedValueSetter<bool> guard(isLoadingSettings, true);

        driverCombo.setSelectedId(juce::jlimit(1, 3, xml->getIntAttribute("driverId", 1)), juce::dontSendNotification);

        artNetIpEdit.setText(xml->getStringAttribute("artNetIp", "255.255.255.255"), juce::dontSendNotification);
        artNetUniSlider.setValue(xml->getIntAttribute("artNetUni", 0), juce::dontSendNotification);
        artNetSubSlider.setValue(xml->getIntAttribute("artNetSub", 0), juce::dontSendNotification);
        artNetNetSlider.setValue(xml->getIntAttribute("artNetNet", 0), juce::dontSendNotification);

        sacnMulticastToggle.setToggleState(xml->getBoolAttribute("sacnMulticast", true), juce::dontSendNotification);
        sacnUniSlider.setValue(xml->getIntAttribute("sacnUni", 1), juce::dontSendNotification);
        sacnPrioSlider.setValue(xml->getIntAttribute("sacnPrio", 100), juce::dontSendNotification);
        sacnIpEdit.setText(xml->getStringAttribute("sacnIp", "239.255.0.1"), juce::dontSendNotification);
        updateSacnTargetPreview();

        const juce::String savedCom = xml->getStringAttribute("comPort", "").trim();
        if (savedCom.isNotEmpty())
        {
            for (size_t i = 0; i < detectedComPorts.size(); ++i)
            {
                if (detectedComPorts[i].portName.equalsIgnoreCase(savedCom))
                {
                    comPortCombo.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
                    break;
                }
            }
        }

        if (dmxEngine.isOutputActive())
        {
            const int liveMasterPct = juce::roundToInt(dmxEngine.getMasterDimmer() * 100.0f);
            masterSlider.setValue(liveMasterPct, juce::dontSendNotification);

            const bool liveTab2 = dmxEngine.isTab2LivePreviewEnabled();
            tab2LiveToggle.setToggleState(liveTab2, juce::dontSendNotification);

            const bool liveCourtesy = dmxEngine.getStandaloneCourtesyDimmer();
            courtesyToggle.setToggleState(liveCourtesy, juce::dontSendNotification);
            courtesyToggle.setEnabled(liveTab2);
        }
        else
        {
            const int masterPct = juce::jlimit(0, 100, xml->getIntAttribute("masterPct", 100));
            masterSlider.setValue(masterPct, juce::dontSendNotification);
            dmxEngine.setMasterDimmer(static_cast<float>(masterPct) / 100.0f);

            const bool tab2On = xml->getBoolAttribute("tab2Live", false);
            tab2LiveToggle.setToggleState(tab2On, juce::dontSendNotification);
            dmxEngine.setTab2LivePreviewEnabled(tab2On);

            const bool courtesyOn = xml->getBoolAttribute("courtesyDim", true);
            courtesyToggle.setToggleState(courtesyOn, juce::dontSendNotification);
            courtesyToggle.setEnabled(tab2On);
            dmxEngine.setStandaloneCourtesyDimmer(courtesyOn);
        }

        autoConnectToggle.setToggleState(xml->getBoolAttribute("autoConnect", false), juce::dontSendNotification);

        updateDriverConfigVisibility();
        setDirty(false);
    }

private:
    class MiniConsoleContent : public juce::Component,
        private juce::Timer
    {
    public:
        explicit MiniConsoleContent(DmxLiveEngine& engine, std::function<void()> onMasterEdited = nullptr)
            : dmxEngine(engine), masterEditedCallback(std::move(onMasterEdited))
        {
            statusLbl.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            statusLbl.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
            addAndMakeVisible(statusLbl);

            masterLbl.setText("Master:", juce::dontSendNotification);
            masterLbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible(masterLbl);

            masterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            masterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
            masterSlider.setRange(0, 100, 1);
            masterSlider.setTextValueSuffix("%");
            masterSlider.onValueChange = [this]() {
                if (!syncing)
                {
                    dmxEngine.setMasterDimmer(static_cast<float>(masterSlider.getValue()) / 100.0f);
                    if (masterEditedCallback)
                        masterEditedCallback();
                }
                };
            addAndMakeVisible(masterSlider);

            blackoutBtn.setButtonText("BLACKOUT");
            blackoutBtn.setClickingTogglesState(true);
            blackoutBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff451a1a));
            blackoutBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdc2626));
            blackoutBtn.onClick = [this]() {
                if (!syncing)
                    dmxEngine.setBlackout(blackoutBtn.getToggleState());
                };
            addAndMakeVisible(blackoutBtn);

            setSize(320, 118);
            syncFromEngine();
            startTimerHz(8);
        }

        ~MiniConsoleContent() override { stopTimer(); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff14151c));
            const bool active = dmxEngine.isOutputActive();
            const bool bo = dmxEngine.isBlackout();
            juce::Colour c = !active ? juce::Colour(0xff475569)
                : (bo ? juce::Colour(0xffef4444) : juce::Colour(0xff10b981));
            g.setColour(c);
            g.fillEllipse(12.0f, 15.0f, 10.0f, 10.0f);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced(10, 10);
            auto r1 = b.removeFromTop(20);
            r1.removeFromLeft(18);
            statusLbl.setBounds(r1);
            b.removeFromTop(8);
            auto r2 = b.removeFromTop(24);
            masterLbl.setBounds(r2.removeFromLeft(54));
            masterSlider.setBounds(r2);
            b.removeFromTop(8);
            blackoutBtn.setBounds(b.removeFromTop(34));
        }

    private:
        void timerCallback() override
        {
            if (!isShowing())
                return;
            if (syncFromEngine())
                repaint();
        }

        bool syncFromEngine()
        {
            juce::ScopedValueSetter<bool> g(syncing, true);
            const bool active = dmxEngine.isOutputActive();
            const bool bo = dmxEngine.isBlackout();
            const juce::String st = dmxEngine.getStatusText();
            const int pct = juce::roundToInt(dmxEngine.getMasterDimmer() * 100.0f);

            if (st != lastStatus)
            {
                lastStatus = st;
                statusLbl.setText(st, juce::dontSendNotification);
            }
            if (juce::roundToInt(masterSlider.getValue()) != pct)
                masterSlider.setValue(pct, juce::dontSendNotification);
            if (blackoutBtn.getToggleState() != bo)
            {
                blackoutBtn.setToggleState(bo, juce::dontSendNotification);
                blackoutBtn.setButtonText(bo ? "BLACKOUT ATTIVO" : "BLACKOUT");
            }

            const bool ledChanged = (active != lastActive || bo != lastBlackout);
            lastActive = active;
            lastBlackout = bo;
            return ledChanged;
        }

        DmxLiveEngine& dmxEngine;
        bool syncing = false;
        juce::Label statusLbl, masterLbl;
        juce::Slider masterSlider;
        juce::TextButton blackoutBtn;

        bool lastActive = false, lastBlackout = false; 
        juce::String lastStatus;

        std::function<void()> masterEditedCallback;
    };

    class MiniConsoleWindow : public juce::DialogWindow
    {
    public:
        explicit MiniConsoleWindow(DmxLiveEngine& engine, std::function<void()> onMasterEdited = nullptr)
            : juce::DialogWindow("DMX Live Mini-Console", juce::Colour(0xff14151c), true, true)
        {
            setUsingNativeTitleBar(true);
            setResizable(false, false);
            setAlwaysOnTop(true);
            setContentOwned(new MiniConsoleContent(engine, std::move(onMasterEdited)), true);
            centreWithSize(getWidth(), getHeight());
        }

        void closeButtonPressed() override { setVisible(false); }
    };

    class SerialDebugContent : public juce::Component,
        private juce::Timer
    {
    public:
        explicit SerialDebugContent(DmxLiveEngine& engine)
            : dmxEngine(engine)
        {
            auto cfgLbl = [this](juce::Label& l, const juce::String& t, juce::Colour col = juce::Colours::lightgrey, bool bold = false)
                {
                    l.setText(t, juce::dontSendNotification);
                    l.setFont(juce::FontOptions(bold ? 13.0f : 11.5f, bold ? juce::Font::bold : juce::Font::plain));
                    l.setColour(juce::Label::textColourId, col);
                    addAndMakeVisible(l);
                };

            cfgLbl(statsLbl, "Stato: in attesa di connessione COM...", juce::Colour(0xff38bdf8), true);
            cfgLbl(hexLbl, "Ultimo TX: -", juce::Colour(0xffa7f3d0));
            cfgLbl(rxLbl, "Ultimo RX: -", juce::Colour(0xfffde68a));

            probeBtn.setButtonText("Ping Interfaccia (0x03 / 0x0A)");
            probeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0284c7));
            probeBtn.onClick = [this]() { dmxEngine.getOutputManager().runDriverDiagnosticProbe(); };
            addAndMakeVisible(probeBtn);

            // Test rapido singolo canale (1..512)
            cfgLbl(pokeChLbl, "Test Canale DMX:");
            pokeChSlider.setSliderStyle(juce::Slider::IncDecButtons);
            pokeChSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 54, 22);
            pokeChSlider.setRange(1, 512, 1);
            pokeChSlider.setValue(1, juce::dontSendNotification);
            addAndMakeVisible(pokeChSlider);

            cfgLbl(pokeValLbl, "Valore (0-255):");
            pokeValSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            pokeValSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
            pokeValSlider.setRange(0, 255, 1);
            pokeValSlider.setValue(0, juce::dontSendNotification);
            pokeValSlider.onValueChange = [this]() {
                const int ch = static_cast<int>(pokeChSlider.getValue());
                const uint8_t val = static_cast<uint8_t>(pokeValSlider.getValue());
                dmxEngine.setManualChannelOverride(ch, val);
                };
            addAndMakeVisible(pokeValSlider);

            clearTestBtn.setButtonText("Stop Test Canale");
            clearTestBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f1d1d));
            clearTestBtn.onClick = [this]() {
                dmxEngine.clearManualChannelOverride();
                pokeValSlider.setValue(0, juce::dontSendNotification);
                };
            addAndMakeVisible(clearTestBtn);

            logBox.setMultiLine(true);
            logBox.setReadOnly(true);
            logBox.setScrollbarsShown(true);
            logBox.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
            logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0b0d13));
            logBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xffcbd5e1));
            addAndMakeVisible(logBox);

            setSize(760, 420);
            startTimerHz(6);
        }

        ~SerialDebugContent() override
        {
            stopTimer();
            dmxEngine.clearManualChannelOverride();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff14151c));
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced(12);

            auto rTop = b.removeFromTop(26);
            probeBtn.setBounds(rTop.removeFromRight(220));
            statsLbl.setBounds(rTop);

            b.removeFromTop(6);
            hexLbl.setBounds(b.removeFromTop(20));
            rxLbl.setBounds(b.removeFromTop(20));

            b.removeFromTop(8);
            auto rPoke = b.removeFromTop(26);
            pokeChLbl.setBounds(rPoke.removeFromLeft(105));
            pokeChSlider.setBounds(rPoke.removeFromLeft(105));
            rPoke.removeFromLeft(10);
            pokeValLbl.setBounds(rPoke.removeFromLeft(95));
            clearTestBtn.setBounds(rPoke.removeFromRight(130));
            rPoke.removeFromRight(8);
            pokeValSlider.setBounds(rPoke);

            b.removeFromTop(8);
            logBox.setBounds(b);
        }



    private:
        void timerCallback() override
        {
            if (!isShowing())
                return;

            const auto snap = dmxEngine.getOutputManager().getDriverDebugSnapshot();

            if (!snap.isSerialDriver)
            {
                statsLbl.setText("Driver seriale non attivo (avvia 'USB DMX Pro / Eurolite MK2' in Tab 6).", juce::dontSendNotification);
                return;
            }

            statsLbl.setText(snap.portOrTarget + " @ " + juce::String(snap.baudRate) + " 8N2"
                + " | TX: " + juce::String(snap.txPacketsSent)
                + " (Invariati: " + juce::String(snap.txPacketsSkipped) + ")"
                + " | Err: " + juce::String(snap.txErrors)
                + " | Write: " + juce::String(snap.lastTxDurationMs, 2) + " ms",
                juce::dontSendNotification);

            hexLbl.setText("TX: " + (snap.lastTxHexPreview.isNotEmpty() ? snap.lastTxHexPreview : "-"),
                juce::dontSendNotification);
            rxLbl.setText("RX: " + (snap.lastRxHex.isNotEmpty() ? snap.lastRxHex : "(nessun byte ricevuto)"),
                juce::dontSendNotification);

            const juce::String joinedLog = snap.logLines.joinIntoString("\n");
            if (joinedLog != lastLogText)
            {
                lastLogText = joinedLog;
                logBox.setText(joinedLog, false);
                logBox.moveCaretToEnd();
            }
        }

        DmxLiveEngine& dmxEngine;
        juce::Label statsLbl, hexLbl, rxLbl, pokeChLbl, pokeValLbl;
        juce::TextButton probeBtn, clearTestBtn;
        juce::Slider pokeChSlider, pokeValSlider;
        juce::TextEditor logBox;
        juce::String lastLogText;
    };

    class SerialDebugWindow : public juce::DialogWindow
    {
    public:
        explicit SerialDebugWindow(DmxLiveEngine& engine)
            : juce::DialogWindow("Debug Eurolite USB-DMX512 PRO MK2", juce::Colour(0xff14151c), true, true)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, false);
            setResizeLimits(640, 340, 1100, 680);
            setContentOwned(new SerialDebugContent(engine), true);
            centreWithSize(getWidth(), getHeight());
        }

        void closeButtonPressed() override { setVisible(false); }
    };

    void openSerialDebugWindow()
    {
        if (serialDebugWindow == nullptr)
            serialDebugWindow = std::make_unique<SerialDebugWindow>(dmxEngine);

        serialDebugWindow->setVisible(true);
        serialDebugWindow->toFront(true);
    }

    void openMiniConsole()
    {
        if (miniConsoleWindow == nullptr)
            miniConsoleWindow = std::make_unique<MiniConsoleWindow>(dmxEngine, [this]() { markDirty(); });

        miniConsoleWindow->setVisible(true);
        miniConsoleWindow->toFront(true);
    }

    void refreshComPorts()
    {
        const juce::String prevPort = getSelectedComPortName();
        comPortCombo.clear(juce::dontSendNotification);
        detectedComPorts = EnttecProDmxDriver::listDetailedComPorts();

        int indexToSelect = -1;
        for (size_t i = 0; i < detectedComPorts.size(); ++i)
        {
            comPortCombo.addItem(detectedComPorts[i].getDisplayLabel(), static_cast<int>(i) + 1);
            if (prevPort.isNotEmpty() && detectedComPorts[i].portName.equalsIgnoreCase(prevPort))
                indexToSelect = static_cast<int>(i);
        }

        // Se non c'era una selezione precedente, seleziona automaticamente la prima porta FTDI
        if (indexToSelect < 0)
        {
            for (size_t i = 0; i < detectedComPorts.size(); ++i)
            {
                if (detectedComPorts[i].isFtdi)
                {
                    indexToSelect = static_cast<int>(i);
                    break;
                }
            }
        }

        if (indexToSelect >= 0)
            comPortCombo.setSelectedId(indexToSelect + 1, juce::dontSendNotification);
        else if (!detectedComPorts.empty())
            comPortCombo.setSelectedId(1, juce::dontSendNotification);
        else
            comPortCombo.setText("Nessuna porta COM trovata", juce::dontSendNotification);
    }

    juce::String getSelectedComPortName() const
    {
        const int idx = comPortCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(detectedComPorts.size()))
            return detectedComPorts[static_cast<size_t>(idx)].portName;
        return {};
    }

    void updateSacnTargetPreview()
    {
        const bool isMc = sacnMulticastToggle.getToggleState();
        sacnIpEdit.setEnabled(!isMc && !dmxEngine.isOutputActive());
        if (isMc)
        {
            const int uni = static_cast<int>(sacnUniSlider.getValue());
            sacnIpEdit.setText(SacnDmxDriver::makeMulticastIp(uni), juce::dontSendNotification);
        }
    }

    void updateDriverConfigVisibility()
    {
        const int id = driverCombo.getSelectedId();
        const bool isArt = (id == 1);
        const bool isSacn = (id == 2);
        const bool isCom = (id == 3);

        artNetIpLbl.setVisible(isArt);   artNetIpEdit.setVisible(isArt);
        artNetLocalBtn.setVisible(isArt); artNetBcastBtn.setVisible(isArt);
        artNetUniLbl.setVisible(isArt);  artNetUniSlider.setVisible(isArt);
        artNetSubLbl.setVisible(isArt);  artNetSubSlider.setVisible(isArt);
        artNetNetLbl.setVisible(isArt);  artNetNetSlider.setVisible(isArt);

        sacnMulticastToggle.setVisible(isSacn);
        sacnIpLbl.setVisible(isSacn);    sacnIpEdit.setVisible(isSacn);
        sacnUniLbl.setVisible(isSacn);   sacnUniSlider.setVisible(isSacn);
        sacnPrioLbl.setVisible(isSacn);  sacnPrioSlider.setVisible(isSacn);

        comPortLbl.setVisible(isCom);    comPortCombo.setVisible(isCom);
        refreshComBtn.setVisible(isCom);

        debugSerialBtn.setVisible(isCom);

        resized();
    }

    void updateMonitorPageButtons()
    {
        for (size_t i = 0; i < pageBtns.size(); ++i)
        {
            const bool active = (static_cast<int>(i) == activeMonitorPage);
            pageBtns[i]->setColour(juce::TextButton::buttonColourId,
                active ? juce::Colour(0xff0284c7) : juce::Colour(0xff262936));
        }
    }

    void toggleConnection()
    {
        if (dmxEngine.isOutputActive())
        {
            dmxEngine.stopOutput(true);
            monitorSnapshot.fill(0);
            refreshStatusUI();
            repaint();
            return;
        }

        std::unique_ptr<DmxOutputDriver> driver;
        const int drvId = driverCombo.getSelectedId();

        if (drvId == 1)
        {
            ArtNetDmxDriver::Config cfg;
            cfg.targetIp = artNetIpEdit.getText().trim();
            if (cfg.targetIp.isEmpty()) cfg.targetIp = "255.255.255.255";
            cfg.universe = static_cast<int>(artNetUniSlider.getValue());
            cfg.subnet = static_cast<int>(artNetSubSlider.getValue());
            cfg.net = static_cast<int>(artNetNetSlider.getValue());
            driver = std::make_unique<ArtNetDmxDriver>(cfg);
        }
        else if (drvId == 2)
        {
            SacnDmxDriver::Config cfg;
            cfg.useMulticast = sacnMulticastToggle.getToggleState();
            cfg.targetIp = sacnIpEdit.getText().trim();
            cfg.universe = static_cast<int>(sacnUniSlider.getValue());
            cfg.priority = static_cast<int>(sacnPrioSlider.getValue());
            driver = std::make_unique<SacnDmxDriver>(cfg);
        }
        else
        {
            const juce::String port = getSelectedComPortName();
            if (port.isEmpty())
            {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Nessuna Porta COM",
                    "Collega l'Eurolite USB-DMX512 PRO MK2 e premi 'Aggiorna COM'.");
                return;
            }
            driver = std::make_unique<EnttecProDmxDriver>(port);
        }

        dmxEngine.getOutputManager().setRefreshRateHz(40);



        if (!dmxEngine.startOutput(std::move(driver)))
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Errore Connessione DMX",
                dmxEngine.getStatusText());
        }

        refreshStatusUI();
    }

    void setConfigControlsLocked(bool locked)
    {
        const bool canEdit = !locked;

        driverCombo.setEnabled(canEdit);

        // Art-Net
        artNetIpEdit.setEnabled(canEdit);
        artNetLocalBtn.setEnabled(canEdit);
        artNetBcastBtn.setEnabled(canEdit);
        artNetUniSlider.setEnabled(canEdit);
        artNetSubSlider.setEnabled(canEdit);
        artNetNetSlider.setEnabled(canEdit);

        // sACN E1.31 (rispetta anche il toggle Multicast allo sblocco)
        sacnMulticastToggle.setEnabled(canEdit);
        sacnIpEdit.setEnabled(canEdit && !sacnMulticastToggle.getToggleState());
        sacnUniSlider.setEnabled(canEdit);
        sacnPrioSlider.setEnabled(canEdit);

        // USB DMX Pro / Eurolite MK2 (COM)
        comPortCombo.setEnabled(canEdit);
        refreshComBtn.setEnabled(canEdit);
    }

    bool refreshStatusUI()
    {
        const bool active = dmxEngine.isOutputActive();
        const bool bo = dmxEngine.isBlackout();
        const bool ledChanged = (!uiStateInitialized || active != lastUiActive || bo != lastUiBlackout);

        if (!uiStateInitialized || active != lastUiActive)
        {
            connectBtn.setButtonText(active ? "Ferma Uscita DMX" : "Avvia Uscita DMX (40 Hz)");
            connectBtn.setColour(juce::TextButton::buttonColourId,
                active ? juce::Colour(0xffb91c1c) : juce::Colour(0xff0f766e));
            statusLbl.setColour(juce::Label::textColourId,
                active ? juce::Colour(0xff34d399) : juce::Colour(0xff94a3b8));
            setConfigControlsLocked(active);
            lastUiActive = active;
        }

        const juce::String newStatus = "Stato: " + dmxEngine.getStatusText();
        if (newStatus != lastUiStatusText)
        {
            lastUiStatusText = newStatus;
            statusLbl.setText(newStatus, juce::dontSendNotification);
        }

        const uint64_t txFrames = dmxEngine.getTxFrameCount();
        const uint64_t txErrors = dmxEngine.getTxErrorCount();
        if (!uiStateInitialized || txFrames != lastUiTxFrames || txErrors != lastUiTxErrors)
        {
            lastUiTxFrames = txFrames;
            lastUiTxErrors = txErrors;
            statsLbl.setText("TX: " + juce::String(txFrames)
                + " frame | Errori: " + juce::String(txErrors),
                juce::dontSendNotification);
        }

        const int engineMasterPct = juce::roundToInt(dmxEngine.getMasterDimmer() * 100.0f);
        if (juce::roundToInt(masterSlider.getValue()) != engineMasterPct)
            masterSlider.setValue(engineMasterPct, juce::dontSendNotification);

        if (!uiStateInitialized || blackoutBtn.getToggleState() != bo)
        {
            blackoutBtn.setToggleState(bo, juce::dontSendNotification);
            blackoutBtn.setButtonText(bo ? "BLACKOUT ATTIVO" : "BLACKOUT");
            lastUiBlackout = bo;
        }

        uiStateInitialized = true;
        return ledChanged;
    }

    void timerCallback() override
    {
        if (!isShowing())
            return;

        std::array<uint8_t, 512> fresh{}, peaks{};
        dmxEngine.consumeMonitorSnapshot(fresh, peaks);

        const size_t pageOffset = static_cast<size_t>(activeMonitorPage * 128);

        for (size_t ch = 0; ch < 512; ++ch)
        {
            const uint8_t prevVal = monitorSnapshot[ch];
            const uint8_t nextVal = (fresh[ch] == 0 && prevVal == 0 && peaks[ch] > 0)
                ? peaks[ch]
                : fresh[ch];

            if (nextVal != prevVal)
            {
                monitorSnapshot[ch] = nextVal;
                if (ch >= pageOffset && ch < pageOffset + 128)
                {
                    const int cellIdx = static_cast<int>(ch - pageOffset);
                    repaint(getMonitorCellRect(cellIdx).expanded(2.0f).getSmallestIntegerContainer());
                }
            }
        }

        if (++statusTickDivider >= 6)
        {
            statusTickDivider = 0;
            if (refreshStatusUI())
                repaint(ledRect.expanded(5.0f).toNearestInt());
        }
    }

    static const std::array<juce::String, 513>& getCachedNumberStrings()
    {
        static const auto table = []() {
            std::array<juce::String, 513> arr;
            for (int i = 0; i <= 512; ++i)
                arr[static_cast<size_t>(i)] = juce::String(i);
            return arr;
            }();
        return table;
    }

    juce::Rectangle<float> getMonitorCellRect(int cellIndex0to127) const
    {
        auto inner = monitorGridRect.reduced(6);
        constexpr int cols = 16;
        constexpr int rows = 8;
        const float cellW = static_cast<float>(inner.getWidth()) / static_cast<float>(cols);
        const float cellH = static_cast<float>(inner.getHeight()) / static_cast<float>(rows);

        const int col = cellIndex0to127 % cols;
        const int row = cellIndex0to127 / cols;

        return {
            inner.getX() + col * cellW + 1.5f,
            inner.getY() + row * cellH + 1.5f,
            cellW - 3.0f,
            cellH - 3.0f
        };
    }

    void drawChannelMonitorGrid(juce::Graphics& g, const juce::Rectangle<int>& area)
    {
        if (area.getWidth() < 100 || area.getHeight() < 80)
            return;

        g.setColour(juce::Colour(0xff0e1017));
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff2a2d3d));
        g.drawRoundedRectangle(area.toFloat(), 6.0f, 1.0f);

        const auto& numStrings = getCachedNumberStrings();
        const juce::Font chFont(juce::FontOptions(9.0f));
        const juce::Font valFontPlain(juce::FontOptions(11.5f, juce::Font::plain));
        const juce::Font valFontBold(juce::FontOptions(11.5f, juce::Font::bold));
        const auto clipBounds = g.getClipBounds().toFloat();
        const int baseCh = activeMonitorPage * 128;

        for (int i = 0; i < 128; ++i)
        {
            const auto cell = getMonitorCellRect(i);
            if (!clipBounds.intersects(cell.expanded(2.0f)))
                continue;

            const int chIdx = baseCh + i;
            const uint8_t val = monitorSnapshot[static_cast<size_t>(chIdx)];

            g.setColour(val > 0 ? juce::Colour(0xff172233) : juce::Colour(0xff141620));
            g.fillRoundedRectangle(cell, 3.0f);

            if (val > 0)
            {
                const float ratio = static_cast<float>(val) / 255.0f;
                const float fillH = (cell.getHeight() - 2.0f) * ratio;
                juce::Rectangle<float> bar(cell.getX() + 1.0f, cell.getBottom() - 1.0f - fillH,
                    cell.getWidth() - 2.0f, fillH);
                g.setColour(juce::Colour(0xff00d2ff).withAlpha(0.28f));
                g.fillRoundedRectangle(bar, 2.0f);
            }

            g.setColour(val > 0 ? juce::Colour(0xff38bdf8).withAlpha(0.65f) : juce::Colour(0xff252938));
            g.drawRoundedRectangle(cell, 3.0f, 1.0f);

            const auto textBox = cell.reduced(2.0f, 1.0f).toNearestInt();

            g.setColour(juce::Colour(0xff64748b));
            g.setFont(chFont);
            g.drawText(numStrings[static_cast<size_t>(chIdx + 1)], textBox,
                juce::Justification::topLeft, false);

            g.setColour(val > 0 ? juce::Colours::white : juce::Colour(0xff475569));
            g.setFont(val > 0 ? valFontBold : valFontPlain);
            g.drawText(numStrings[static_cast<size_t>(val)], textBox,
                juce::Justification::centredBottom, false);
        }
    }

    void markDirty()
    {
        if (!isLoadingSettings)
            setDirty(true);
    }

    void setDirty(bool shouldBeDirty)
    {
        if (dirty != shouldBeDirty)
        {
            dirty = shouldBeDirty;
            saveConfigBtn.setButtonText(dirty ? "Salva Config *" : "Salva Config");
            saveConfigBtn.setColour(juce::TextButton::buttonColourId,
                dirty ? juce::Colour(0xff0f766e) : juce::Colour(0xff293d3d));
            if (onDirtyStateChanged)
                onDirtyStateChanged(dirty);
        }
    }

    juce::TextButton saveConfigBtn;
    bool isLoadingSettings = false;
    bool dirty = false;

    DmxLiveEngine& dmxEngine;
    std::array<uint8_t, 512> monitorSnapshot{};
    int activeMonitorPage = 0;

    juce::Rectangle<int> topCardRect;
    juce::Rectangle<int> monitorGridRect;
    juce::Rectangle<float> ledRect;

    juce::Label titleLbl, driverLbl, statusLbl, statsLbl;
    juce::ComboBox driverCombo;
    juce::TextButton connectBtn, miniConsoleBtn;

    // Art-Net
    juce::Label artNetIpLbl, artNetUniLbl, artNetSubLbl, artNetNetLbl;
    juce::TextEditor artNetIpEdit;
    juce::TextButton artNetLocalBtn, artNetBcastBtn;
    juce::Slider artNetUniSlider, artNetSubSlider, artNetNetSlider;

    // sACN
    juce::ToggleButton sacnMulticastToggle;
    juce::Label sacnIpLbl, sacnUniLbl, sacnPrioLbl;
    juce::TextEditor sacnIpEdit;
    juce::Slider sacnUniSlider, sacnPrioSlider;

    // ENTTEC Pro
    juce::Label comPortLbl;
    juce::ComboBox comPortCombo;
    juce::TextButton refreshComBtn;

    // Live Safety
    juce::Label liveTitleLbl, masterLbl;
    juce::Slider masterSlider;
    juce::TextButton blackoutBtn;
    juce::ToggleButton courtesyToggle;
    juce::ToggleButton tab2LiveToggle;

    juce::ToggleButton autoConnectToggle;

    // Monitor 512 ch
    juce::Label monitorTitleLbl;
    std::vector<std::unique_ptr<juce::TextButton>> pageBtns;
    std::unique_ptr<MiniConsoleWindow> miniConsoleWindow;

    std::vector<EnttecProDmxDriver::ComPortInfo> detectedComPorts;
    juce::TextButton debugSerialBtn;
    std::unique_ptr<SerialDebugWindow> serialDebugWindow;

    bool uiStateInitialized = false;
    int statusTickDivider = 0;
    bool lastUiActive = false;
    bool lastUiBlackout = false;
    juce::String lastUiStatusText;
    uint64_t lastUiTxFrames = 0;
    uint64_t lastUiTxErrors = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DmxOutputControlPanel)
};