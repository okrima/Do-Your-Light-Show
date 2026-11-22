#include "VenuePatchTab.h"
#include "../Components/PanTiltCalibrationWizard.h"
#include "../Components/StageArtworkHelper.h"

namespace
{
    void configureCompactSlider(juce::Slider& s, double minV, double maxV, double stepV, int textW = 42)
    {
        s.setSliderStyle(juce::Slider::IncDecButtons);
        s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, textW, 20);
        s.setRange(minV, maxV, stepV);
    }

    void configureFieldLabel(juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.5f));
        l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }
}

// =============================================================================
// FixtureEditorPanel Implementation
// =============================================================================
FixtureEditorPanel::FixtureEditorPanel(juce::ValueTree& state) : appState(state)
{
    titleLabel.setText("1. Configurazione & Calibrazione Faro", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(16.5f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00d2ff));
    addAndMakeVisible(titleLabel);

    // -- NOME & RUOLO --
    configureFieldLabel(nameLabel, "Nome Faro:");
    addAndMakeVisible(nameLabel);

    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a32));
    nameEditor.onTextChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::FixtureName, nameEditor.getText(), nullptr);
        };
    addAndMakeVisible(nameEditor);

    configureFieldLabel(roleLabel, "Categoria:");
    addAndMakeVisible(roleLabel);

    StageRoleCatalog::populateComboBox(roleCombo, true);
    roleCombo.onChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid())
            return;
        juce::String newRole = StageRoleCatalog::getRoleFromCombo(roleCombo);
        currentFixtureNode.setProperty(StateKeys::RoleName, newRole, nullptr);

        // Default intelligente: sulle quinte laterali abilita Scambia Pan <-> Tilt se entrambi i canali sono mappati
        auto newCat = StageRoleCatalog::classifyRole(newRole,
            currentFixtureNode.getProperty(StateKeys::FixtureName, "").toString());
        int panCh = static_cast<int>(currentFixtureNode.getProperty(StateKeys::getMappingKey("Pan"), 0));
        int tiltCh = static_cast<int>(currentFixtureNode.getProperty(StateKeys::getMappingKey("Tilt"), 0));
        currentFixtureNode.setProperty(StateKeys::SwapPanTilt,
            (newCat == StageRoleCatalog::Category::FloorSideWash && panCh > 0 && tiltCh > 0), nullptr);
        updateUIFromNode();
        };

    roleCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(roleCombo);

    // -- START DMX, TOTALE CANALI & QUANTITA' FISICA (STESSO INDIRIZZO DMX) --
    configureFieldLabel(startAddrLabel, "Start DMX:");
    addAndMakeVisible(startAddrLabel);
    configureCompactSlider(startAddrSlider, 1, 512, 1, 45);
    startAddrSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::StartAddress, static_cast<int>(startAddrSlider.getValue()), nullptr);
        };
    addAndMakeVisible(startAddrSlider);

    configureFieldLabel(chCountLabel, "Tot. Canali:");
    addAndMakeVisible(chCountLabel);
    configureCompactSlider(chCountSlider, 1, 64, 1, 42);
    chCountSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::ChannelCount, static_cast<int>(chCountSlider.getValue()), nullptr);
        };
    addAndMakeVisible(chCountSlider);

    configureFieldLabel(qtyLabel, "Quantita':");
    addAndMakeVisible(qtyLabel);
    configureCompactSlider(qtySlider, 1, 32, 1, 40);
    qtySlider.setTooltip("Numero di fari fisici nel Simulatore/Light Case collegati a questo stesso indirizzo DMX");
    qtySlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
        {
            currentFixtureNode.setProperty(StateKeys::FixtureQuantity, static_cast<int>(qtySlider.getValue()), nullptr);
            VenueUnitHelper::syncUnitsForFixture(currentFixtureNode);
        }
        };
    addAndMakeVisible(qtySlider);

    // -- MAPPATURA CANALI --
    mapTitleLabel.setText("2. Mappatura Canali DMX (0 = Non Presente)", juce::dontSendNotification);
    mapTitleLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    mapTitleLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(mapTitleLabel);

    availableParams = { "Dimmer", "Pan", "Tilt", "Red", "Green", "Blue", "White", "UV", "Strobe", "GeyserBlast" };

    for (const auto& param : availableParams)
    {
        auto ctrl = std::make_unique<ParamControl>();
        configureFieldLabel(ctrl->label, param + " [Ch]:");
        addAndMakeVisible(ctrl->label);

        configureCompactSlider(ctrl->slider, 0, 64, 1, 38);
        ctrl->slider.onValueChange = [this, param, sPtr = &ctrl->slider]() {
            if (currentFixtureNode.isValid() && !isUpdatingUI)
                currentFixtureNode.setProperty(StateKeys::getMappingKey(param), static_cast<int>(sPtr->getValue()), nullptr);
            };
        addAndMakeVisible(ctrl->slider);
        paramControls.push_back(std::move(ctrl));
    }

    // -- CALIBRAZIONE STROBO DI FABBRICA --
    strobeSectionLabel.setText("3. Calibrazione Strobo Hardware & Frequenza (Hz)", juce::dontSendNotification);
    strobeSectionLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    strobeSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(strobeSectionLabel);

    configureFieldLabel(strobeModeLabel, "Tipo Canale:");
    addAndMakeVisible(strobeModeLabel);

    strobeModeCombo.addItem("Dimmer Flash (0 / 255)", 1);
    strobeModeCombo.addItem("Canale Strobo Separato (HW + Hz)", 2);
    strobeModeCombo.addItem("Condiviso con Dimmer (Mix + Hz)", 3);
    strobeModeCombo.addItem("Disabilitato (Ignora Strobo)", 4);
    strobeModeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a32));

    strobeModeCombo.onChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        int mode = strobeModeCombo.getSelectedId() - 1; // 0, 1, 2, 3
        currentFixtureNode.setProperty(StateKeys::StrobeMode, mode, nullptr);

        // Pre-imposta valori tipici sensati al cambio modalita'
        if (mode == 2) // Condiviso con Dimmer (es. 0-134 Dimmer, 135-239 Strobo)
        {
            int dimCh = static_cast<int>(currentFixtureNode.getProperty(StateKeys::getMappingKey("Dimmer"), 1));
            if (dimCh > 0)
                currentFixtureNode.setProperty(StateKeys::getMappingKey("Strobe"), dimCh, nullptr);
            if (static_cast<int>(currentFixtureNode.getProperty(StateKeys::DimmerMaxVal, 255)) == 255)
                currentFixtureNode.setProperty(StateKeys::DimmerMaxVal, 134, nullptr);
            if (static_cast<int>(currentFixtureNode.getProperty(StateKeys::StrobeMinVal, 20)) < 100)
                currentFixtureNode.setProperty(StateKeys::StrobeMinVal, 135, nullptr);
        }
        updateUIFromNode();
        };
    addAndMakeVisible(strobeModeCombo);

    configureFieldLabel(dimmerOrOpenLabel, "DMX Luce Fissa:");
    addAndMakeVisible(dimmerOrOpenLabel);
    configureCompactSlider(dimmerOrOpenSlider, 0, 255, 1, 40);

    dimmerOrOpenSlider.onValueChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        const int mode = static_cast<int>(currentFixtureNode.getProperty(StateKeys::StrobeMode, 1));
        int val = static_cast<int>(dimmerOrOpenSlider.getValue());

        if (mode == 2)
        {
            const int strobeLow = juce::jmin(
                static_cast<int>(currentFixtureNode.getProperty(StateKeys::StrobeMinVal, 135)),
                static_cast<int>(currentFixtureNode.getProperty(StateKeys::StrobeMaxVal, 239)));
            if (strobeLow > 1 && val >= strobeLow)
                val = strobeLow - 1;

            currentFixtureNode.setProperty(StateKeys::DimmerMaxVal, val, nullptr);
            if (val != static_cast<int>(dimmerOrOpenSlider.getValue()))
                updateUIFromNode();
        }
        else
        {
            currentFixtureNode.setProperty(StateKeys::StrobeOpenVal, val, nullptr);
        }
        };

    addAndMakeVisible(dimmerOrOpenSlider);

    configureFieldLabel(strobeStartDmxLabel, "Start DMX:");
    configureFieldLabel(strobeStartHzLabel, "Start Hz:");
    configureFieldLabel(strobeEndDmxLabel, "End DMX:");
    configureFieldLabel(strobeEndHzLabel, "End Hz:");
    addAndMakeVisible(strobeStartDmxLabel); addAndMakeVisible(strobeStartHzLabel);
    addAndMakeVisible(strobeEndDmxLabel);   addAndMakeVisible(strobeEndHzLabel);

    configureCompactSlider(strobeStartDmxSlider, 0, 255, 1, 40);
    configureCompactSlider(strobeEndDmxSlider, 0, 255, 1, 40);

    strobeStartHzSlider.setSliderStyle(juce::Slider::IncDecButtons);
    strobeStartHzSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 20);
    strobeStartHzSlider.setRange(0.5, 50.0, 0.5);
    strobeStartHzSlider.setTextValueSuffix(" Hz");

    strobeEndHzSlider.setSliderStyle(juce::Slider::IncDecButtons);
    strobeEndHzSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 20);
    strobeEndHzSlider.setRange(0.5, 50.0, 0.5);
    strobeEndHzSlider.setTextValueSuffix(" Hz");

    strobeStartDmxSlider.onValueChange = [this]() {
        if (!currentFixtureNode.isValid() || isUpdatingUI) return;
        const int startVal = static_cast<int>(strobeStartDmxSlider.getValue());
        currentFixtureNode.setProperty(StateKeys::StrobeMinVal, startVal, nullptr);

        const int mode = static_cast<int>(currentFixtureNode.getProperty(StateKeys::StrobeMode, 1));
        if (mode == 2 && startVal > 1)
        {
            const int curMaxDim = static_cast<int>(currentFixtureNode.getProperty(StateKeys::DimmerMaxVal, 134));
            if (curMaxDim >= startVal)
            {
                currentFixtureNode.setProperty(StateKeys::DimmerMaxVal, startVal - 1, nullptr);
                updateUIFromNode();
            }
        }
        };

    strobeStartHzSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::StrobeMinHz, strobeStartHzSlider.getValue(), nullptr);
        };
    strobeEndDmxSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::StrobeMaxVal, static_cast<int>(strobeEndDmxSlider.getValue()), nullptr);
        };
    strobeEndHzSlider.onValueChange = [this]() {
        if (currentFixtureNode.isValid() && !isUpdatingUI)
            currentFixtureNode.setProperty(StateKeys::StrobeMaxHz, strobeEndHzSlider.getValue(), nullptr);
        };

    addAndMakeVisible(strobeStartDmxSlider); addAndMakeVisible(strobeStartHzSlider);
    addAndMakeVisible(strobeEndDmxSlider);   addAndMakeVisible(strobeEndHzSlider);

    // -- 4. AREA DI MOVIMENTO SUL PALCO (4 LIMITI DMX + SHIFT + TOTEM) --
    ptSectionLabel.setText("4. Area di Movimento sul Palco (Limiti DMX 0-255)", juce::dontSendNotification);
    ptSectionLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    ptSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa78bfa));
    addAndMakeVisible(ptSectionLabel);

    ptWizardBtn.setButtonText("Taratura Guidata...");
    ptWizardBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5b21b6));
    ptWizardBtn.setTooltip("Apre il wizard passo-passo per tarare i limiti SX/DX/Giu/Su dal vivo");
    ptWizardBtn.onClick = [this]() { openCalibrationWizard(); };
    addAndMakeVisible(ptWizardBtn);

    auto setupSpreadBtn = [this](juce::TextButton& btn, const juce::String& txt, int id)
        {
            btn.setButtonText(txt);
            btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2640));
            btn.onClick = [this, id]() { applyQuickSpreadPreset(id); };
            addAndMakeVisible(btn);
        };
    setupSpreadBtn(ptSpreadNarrowBtn, "Stretto (Band)", 0);
    setupSpreadBtn(ptSpreadMedBtn, "Medio (Palco)", 1);
    setupSpreadBtn(ptSpreadWideBtn, "Ampio (Full)", 2);

    configureFieldLabel(panSxLabel, "Pan SX:");
    configureFieldLabel(panDxLabel, "DX:");
    configureFieldLabel(tiltLowLabel, "Tilt Giu:");
    configureFieldLabel(tiltHighLabel, "Su:");
    addAndMakeVisible(panSxLabel);   addAndMakeVisible(panDxLabel);
    addAndMakeVisible(tiltLowLabel); addAndMakeVisible(tiltHighLabel);

    configureCompactSlider(panLeftSlider, 0, 255, 1, 38);
    configureCompactSlider(panRightSlider, 0, 255, 1, 38);
    configureCompactSlider(tiltLowSlider, 0, 255, 1, 38);
    configureCompactSlider(tiltHighSlider, 0, 255, 1, 38);

    panLeftSlider.onValueChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        s.panLeft = static_cast<int>(panLeftSlider.getValue());
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    panRightSlider.onValueChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        s.panRight = static_cast<int>(panRightSlider.getValue());
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    tiltLowSlider.onValueChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        s.tiltLow = static_cast<int>(tiltLowSlider.getValue());
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    tiltHighSlider.onValueChange = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        s.tiltHigh = static_cast<int>(tiltHighSlider.getValue());
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };

    addAndMakeVisible(panLeftSlider);  addAndMakeVisible(panRightSlider);
    addAndMakeVisible(tiltLowSlider);  addAndMakeVisible(tiltHighSlider);

    swapPanDirBtn.setButtonText("Scambia SX/DX");
    swapPanDirBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
    swapPanDirBtn.onClick = [this]() {
        if (!currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        std::swap(s.panLeft, s.panRight);
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    addAndMakeVisible(swapPanDirBtn);

    swapTiltDirBtn.setButtonText("Scambia Giu/Su");
    swapTiltDirBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
    swapTiltDirBtn.onClick = [this]() {
        if (!currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        std::swap(s.tiltLow, s.tiltHigh);
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    addAndMakeVisible(swapTiltDirBtn);

    configureFieldLabel(panShiftLabel, "Shift Pan:");
    addAndMakeVisible(panShiftLabel);

    auto setupShiftBtn = [this](juce::TextButton& btn, const juce::String& txt, int delta)
        {
            btn.setButtonText(txt);
            btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e293b));
            btn.setTooltip("Trasla insieme SX e DX mantenendo invariata l'ampiezza (utile per basi ruotate di 90 gradi)");
            btn.onClick = [this, delta]() { shiftPanLimits(delta); };
            addAndMakeVisible(btn);
        };
    setupShiftBtn(panShiftM42Btn, "-42", -42);
    setupShiftBtn(panShiftM10Btn, "-10", -10);
    setupShiftBtn(panShiftP10Btn, "+10", 10);
    setupShiftBtn(panShiftP42Btn, "+42", 42);

    swapPanTiltToggle.setButtonText("Totem (Pan <-> Tilt)");
    swapPanTiltToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    swapPanTiltToggle.setTooltip("Attiva se il faro e' montato di taglio su un traliccio/totem verticale (scambia assi Pan e Tilt)");
    swapPanTiltToggle.onClick = [this]() {
        if (isUpdatingUI || !currentFixtureNode.isValid()) return;
        auto s = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
        s.swapPanTilt = swapPanTiltToggle.getToggleState();
        s.writeToFixtureNode(currentFixtureNode);
        updateUIFromNode();
        };
    addAndMakeVisible(swapPanTiltToggle);

    ptReadoutLabel.setFont(juce::FontOptions(11.5f));
    ptReadoutLabel.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));
    ptReadoutLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(ptReadoutLabel);

    setVisible(false);
}

FixtureEditorPanel::~FixtureEditorPanel()
{
    if (currentFixtureNode.isValid())
        currentFixtureNode.removeListener(this);
}

void FixtureEditorPanel::setFixtureNode(juce::ValueTree fixtureNode)
{
    if (currentFixtureNode.isValid())
        currentFixtureNode.removeListener(this);

    currentFixtureNode = fixtureNode;

    if (currentFixtureNode.isValid())
    {
        VenueUnitHelper::syncUnitsForFixture(currentFixtureNode);
        roleCombo.setEnabled(true);
        currentFixtureNode.addListener(this);
        updateUIFromNode();
        setVisible(true);
    }
    else
    {
        roleCombo.setEnabled(false);
        roleCombo.setText("", juce::dontSendNotification);
        setVisible(false);
    }
}

void FixtureEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff18181f));
}

void FixtureEditorPanel::resized()
{
    auto bounds = getLocalBounds().reduced(12, 10);

    titleLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);

    // Riga 1: Nome + Categoria
    auto row1 = bounds.removeFromTop(24);
    nameLabel.setBounds(row1.removeFromLeft(75));
    nameEditor.setBounds(row1.removeFromLeft(135));
    row1.removeFromLeft(10);
    roleLabel.setBounds(row1.removeFromLeft(68));
    roleCombo.setBounds(row1);

    bounds.removeFromTop(6);

    // Riga 2: Start DMX + Tot. Canali + Quantita' (Simulatore)
    auto rowDmx = bounds.removeFromTop(24);
    startAddrLabel.setBounds(rowDmx.removeFromLeft(70));
    startAddrSlider.setBounds(rowDmx.removeFromLeft(90));
    rowDmx.removeFromLeft(8);
    chCountLabel.setBounds(rowDmx.removeFromLeft(72));
    chCountSlider.setBounds(rowDmx.removeFromLeft(85));
    rowDmx.removeFromLeft(8);
    qtyLabel.setBounds(rowDmx.removeFromLeft(65));
    qtySlider.setBounds(rowDmx.removeFromLeft(85));

    bounds.removeFromTop(12);
    mapTitleLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Griglia Mappatura (2 Colonne x 5 Righe)
    auto gridBounds = bounds.removeFromTop(135);
    int colWidth = gridBounds.getWidth() / 2;
    int rowHeight = 26;
    int count = static_cast<int>(paramControls.size());

    for (int i = 0; i < count; ++i)
    {
        int col = i % 2;
        int row = i / 2;
        auto cell = gridBounds.withTrimmedLeft(col * colWidth).withWidth(colWidth)
            .withTrimmedTop(row * rowHeight).withHeight(rowHeight);

        paramControls[static_cast<size_t>(i)]->label.setBounds(cell.removeFromLeft(95));
        paramControls[static_cast<size_t>(i)]->slider.setBounds(cell.removeFromLeft(85));
    }

	// Riga Strobo
    bounds.removeFromTop(10);
    strobeSectionLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Riga Strobo 1: Tipo Canale (Nessuno / Separato / Condiviso con Dimmer)
    auto sRow1 = bounds.removeFromTop(24);
    strobeModeLabel.setBounds(sRow1.removeFromLeft(80));
    strobeModeCombo.setBounds(sRow1);

    bounds.removeFromTop(4);

    // Riga Strobo 2: Max Dimmer (se condiviso) o DMX Luce Fissa (se separato) + Toggle Flash Dimmer
    auto sRow2 = bounds.removeFromTop(24);
    dimmerOrOpenLabel.setBounds(sRow2.removeFromLeft(110));
    dimmerOrOpenSlider.setBounds(sRow2.removeFromLeft(95));
    sRow2.removeFromLeft(8);

    bounds.removeFromTop(4);

    // Riga Strobo 3: Start DMX + Start Hz
    int halfStrobeW = bounds.getWidth() / 2;
    auto sRow3 = bounds.removeFromTop(24);
    auto sr3L = sRow3.removeFromLeft(halfStrobeW);
    strobeStartDmxLabel.setBounds(sr3L.removeFromLeft(75));
    strobeStartDmxSlider.setBounds(sr3L.removeFromLeft(90));
    strobeStartHzLabel.setBounds(sRow3.removeFromLeft(65));
    strobeStartHzSlider.setBounds(sRow3.removeFromLeft(105));

    bounds.removeFromTop(4);

    // Riga Strobo 4: End DMX + End Hz
    auto sRow4 = bounds.removeFromTop(24);
    auto sr4L = sRow4.removeFromLeft(halfStrobeW);
    strobeEndDmxLabel.setBounds(sr4L.removeFromLeft(75));
    strobeEndDmxSlider.setBounds(sr4L.removeFromLeft(90));
    strobeEndHzLabel.setBounds(sRow4.removeFromLeft(65));
    strobeEndHzSlider.setBounds(sRow4.removeFromLeft(105));

    // Sezione 4: Area di Movimento sul Palco (4 Limiti DMX + Shift + Totem)
    auto ptHeaderRow = bounds.removeFromTop(22);
    ptWizardBtn.setBounds(ptHeaderRow.removeFromRight(142));
    ptHeaderRow.removeFromRight(8);
    ptSectionLabel.setBounds(ptHeaderRow);
    bounds.removeFromTop(5);

    // Riga 1: 3 Preset Rapidi di ampiezza
    auto ptR1 = bounds.removeFromTop(23);
    int thirdW = ptR1.getWidth() / 3;
    ptSpreadNarrowBtn.setBounds(ptR1.removeFromLeft(thirdW).reduced(1, 0));
    ptSpreadMedBtn.setBounds(ptR1.removeFromLeft(thirdW).reduced(1, 0));
    ptSpreadWideBtn.setBounds(ptR1.reduced(1, 0));

    bounds.removeFromTop(6);

    // Riga 2: Pan SX -> DX + Scambia
    auto ptR2 = bounds.removeFromTop(24);
    panSxLabel.setBounds(ptR2.removeFromLeft(56));
    panLeftSlider.setBounds(ptR2.removeFromLeft(98));
    ptR2.removeFromLeft(8);
    panDxLabel.setBounds(ptR2.removeFromLeft(30));
    panRightSlider.setBounds(ptR2.removeFromLeft(98));
    ptR2.removeFromLeft(8);
    swapPanDirBtn.setBounds(ptR2);

    bounds.removeFromTop(5);

    // Riga 3: Tilt Giu -> Su + Scambia
    auto ptR3 = bounds.removeFromTop(24);
    tiltLowLabel.setBounds(ptR3.removeFromLeft(56));
    tiltLowSlider.setBounds(ptR3.removeFromLeft(98));
    ptR3.removeFromLeft(8);
    tiltHighLabel.setBounds(ptR3.removeFromLeft(30));
    tiltHighSlider.setBounds(ptR3.removeFromLeft(98));
    ptR3.removeFromLeft(8);
    swapTiltDirBtn.setBounds(ptR3);

    bounds.removeFromTop(6);

    // Riga 4: Shift Pan (-42 / -10 / +10 / +42) + Totem (Pan <-> Tilt)
    auto ptR4 = bounds.removeFromTop(23);
    panShiftLabel.setBounds(ptR4.removeFromLeft(62));
    panShiftM42Btn.setBounds(ptR4.removeFromLeft(38).reduced(1, 0));
    panShiftM10Btn.setBounds(ptR4.removeFromLeft(38).reduced(1, 0));
    panShiftP10Btn.setBounds(ptR4.removeFromLeft(38).reduced(1, 0));
    panShiftP42Btn.setBounds(ptR4.removeFromLeft(38).reduced(1, 0));
    ptR4.removeFromLeft(10);
    swapPanTiltToggle.setBounds(ptR4);

    bounds.removeFromTop(5);
    ptReadoutLabel.setBounds(bounds.removeFromTop(18));
}

void FixtureEditorPanel::updateUIFromNode()
{
    if (!currentFixtureNode.isValid()) return;
    juce::ScopedValueSetter<bool> setter(isUpdatingUI, true);

    nameEditor.setText(currentFixtureNode.getProperty(StateKeys::FixtureName, "Nuovo Faro").toString(), juce::dontSendNotification);

    juce::String savedRole = currentFixtureNode.getProperty(StateKeys::RoleName, "").toString();
    StageRoleCatalog::setComboSelectionFromRole(roleCombo, savedRole);

    startAddrSlider.setValue(static_cast<double>(currentFixtureNode.getProperty(StateKeys::StartAddress, 1)), juce::dontSendNotification);
    chCountSlider.setValue(static_cast<double>(currentFixtureNode.getProperty(StateKeys::ChannelCount, 8)), juce::dontSendNotification);
    qtySlider.setValue(static_cast<double>(currentFixtureNode.getProperty(StateKeys::FixtureQuantity, 1)), juce::dontSendNotification);

    for (int i = 0; i < availableParams.size(); ++i)
    {
        auto key = StateKeys::getMappingKey(availableParams[i]);
        int val = static_cast<int>(currentFixtureNode.getProperty(key, 0));
        paramControls[static_cast<size_t>(i)]->slider.setValue(static_cast<double>(val), juce::dontSendNotification);
    }

    auto spec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(currentFixtureNode);
    strobeModeCombo.setSelectedId(spec.mode + 1, juce::dontSendNotification);

    bool hasHwStrobe = (spec.mode == 1 || spec.mode == 2);
    if (spec.mode == 2)
    {
        dimmerOrOpenLabel.setText("Max Dimmer DMX:", juce::dontSendNotification);
        dimmerOrOpenSlider.setValue(spec.dimmerMaxVal, juce::dontSendNotification);
    }
    else
    {
        dimmerOrOpenLabel.setText("DMX Luce Fissa:", juce::dontSendNotification);
        dimmerOrOpenSlider.setValue(spec.openVal, juce::dontSendNotification);
    }

    dimmerOrOpenLabel.setEnabled(hasHwStrobe);
    dimmerOrOpenSlider.setEnabled(hasHwStrobe);
    strobeStartDmxLabel.setEnabled(hasHwStrobe);
    strobeStartDmxSlider.setEnabled(hasHwStrobe);
    strobeStartHzLabel.setEnabled(hasHwStrobe);
    strobeStartHzSlider.setEnabled(hasHwStrobe);
    strobeEndDmxLabel.setEnabled(hasHwStrobe);
    strobeEndDmxSlider.setEnabled(hasHwStrobe);
    strobeEndHzLabel.setEnabled(hasHwStrobe);
    strobeEndHzSlider.setEnabled(hasHwStrobe);

    strobeStartDmxSlider.setValue(spec.minVal, juce::dontSendNotification);
    strobeStartHzSlider.setValue(spec.minHz, juce::dontSendNotification);
    strobeEndDmxSlider.setValue(spec.maxVal, juce::dontSendNotification);
    strobeEndHzSlider.setValue(spec.maxHz, juce::dontSendNotification);

    auto ptSpec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
    const bool hasAnyPT = ptSpec.hasPanChannel || ptSpec.hasTiltChannel;
    const bool hasBothPT = ptSpec.hasPanChannel && ptSpec.hasTiltChannel;

    panLeftSlider.setValue(ptSpec.panLeft, juce::dontSendNotification);
    panRightSlider.setValue(ptSpec.panRight, juce::dontSendNotification);
    tiltLowSlider.setValue(ptSpec.tiltLow, juce::dontSendNotification);
    tiltHighSlider.setValue(ptSpec.tiltHigh, juce::dontSendNotification);
    swapPanTiltToggle.setToggleState(ptSpec.swapPanTilt, juce::dontSendNotification);

    ptSpreadNarrowBtn.setEnabled(hasAnyPT);
    ptWizardBtn.setEnabled(hasAnyPT);
    ptSpreadMedBtn.setEnabled(hasAnyPT);
    ptSpreadWideBtn.setEnabled(hasAnyPT);

    panSxLabel.setEnabled(ptSpec.hasPanChannel);
    panDxLabel.setEnabled(ptSpec.hasPanChannel);
    panLeftSlider.setEnabled(ptSpec.hasPanChannel);
    panRightSlider.setEnabled(ptSpec.hasPanChannel);
    swapPanDirBtn.setEnabled(ptSpec.hasPanChannel);
    panShiftLabel.setEnabled(ptSpec.hasPanChannel);
    panShiftM42Btn.setEnabled(ptSpec.hasPanChannel);
    panShiftM10Btn.setEnabled(ptSpec.hasPanChannel);
    panShiftP10Btn.setEnabled(ptSpec.hasPanChannel);
    panShiftP42Btn.setEnabled(ptSpec.hasPanChannel);

    tiltLowLabel.setEnabled(ptSpec.hasTiltChannel);
    tiltHighLabel.setEnabled(ptSpec.hasTiltChannel);
    tiltLowSlider.setEnabled(ptSpec.hasTiltChannel);
    tiltHighSlider.setEnabled(ptSpec.hasTiltChannel);
    swapTiltDirBtn.setEnabled(ptSpec.hasTiltChannel);

    swapPanTiltToggle.setEnabled(hasBothPT);

    if (!hasAnyPT)
    {
        ptReadoutLabel.setText("Faro statico (imposta Pan/Tilt Ch > 0 per attivare)", juce::dontSendNotification);
    }
    else
    {
        juce::String info = "Centro DMX: Pan " + juce::String(ptSpec.getPanCenter())
            + ", Tilt " + juce::String(ptSpec.getTiltCenter())
            + "  |  Corsa: Pan " + juce::String(ptSpec.getPanSpan())
            + ", Tilt " + juce::String(ptSpec.getTiltSpan());
        if (ptSpec.isSwapped())
            info += "  [Totem: assi scambiati]";
        ptReadoutLabel.setText(info, juce::dontSendNotification);
    }
}

void FixtureEditorPanel::applyQuickSpreadPreset(int presetId)
{
    if (!currentFixtureNode.isValid()) return;
    auto spec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
    spec.applyQuickSpread(presetId);
    spec.writeToFixtureNode(currentFixtureNode);
    updateUIFromNode();
}

void FixtureEditorPanel::shiftPanLimits(int delta)
{
    if (!currentFixtureNode.isValid()) return;
    auto spec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(currentFixtureNode);
    spec.shiftPan(delta);
    spec.writeToFixtureNode(currentFixtureNode);
    updateUIFromNode();
}

void FixtureEditorPanel::openCalibrationWizard()
{
    if (!currentFixtureNode.isValid())
        return;

    auto* wizard = new PanTiltCalibrationWizard(currentFixtureNode);
    juce::Component::SafePointer<FixtureEditorPanel> safeThis(this);
    wizard->onCalibrationSaved = [safeThis]() {
        if (safeThis != nullptr)
            safeThis->updateUIFromNode();
        };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(wizard);
    opts.dialogTitle = "Taratura Guidata Pan/Tilt - "
        + currentFixtureNode.getProperty(StateKeys::FixtureName, "Faro").toString();
    opts.dialogBackgroundColour = juce::Colour(0xff11121a);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void FixtureEditorPanel::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&)
{
    updateUIFromNode();
}

// =============================================================================
// StagePatchAndCasePanel Implementation ("Light Case" + Smart Masking Stage)
// =============================================================================
StagePatchAndCasePanel::StagePatchAndCasePanel(juce::ValueTree& state) : appState(state)
{
    // Ascolta appState direttamente: intercetta creazione VENUE, nuovi fari, rinomine e Load Venue
    appState.addListener(this);

    venueNode = appState.getChildWithName(StateKeys::VenueNode);
    if (venueNode.isValid())
        venueNode.addListener(this);

    autoPlaceBtn.setButtonText("Piazza Tutto Auto");
    autoPlaceBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e3a5f));
    autoPlaceBtn.onClick = [this]() { autoPlaceAllUnits(); };
    addAndMakeVisible(autoPlaceBtn);

    clearStageBtn.setButtonText("Rimetti Tutto nel Case");
    clearStageBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3f2e3e));
    clearStageBtn.onClick = [this]() { returnAllUnitsToCase(); };
    addAndMakeVisible(clearStageBtn);

    rebuildUnitCache();
}

StagePatchAndCasePanel::~StagePatchAndCasePanel()
{
    appState.removeListener(this);

    if (venueNode.isValid())
        venueNode.removeListener(this);
}

void StagePatchAndCasePanel::handleVenueTreeChange(const juce::ValueTree& tree)
{
    if (isRebuilding)
        return;

    if (tree.hasType(StateKeys::VenueNode) ||
        tree.hasType(StateKeys::Fixture) ||
        tree.hasType(StateKeys::FixtureUnit))
    {
        rebuildUnitCache();
        repaint();
    }
}

void StagePatchAndCasePanel::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    handleVenueTreeChange(tree);
}

void StagePatchAndCasePanel::valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child)
{
    handleVenueTreeChange(child.hasType(StateKeys::VenueNode) ? child : parent);
}

void StagePatchAndCasePanel::valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int)
{
    handleVenueTreeChange(child.hasType(StateKeys::VenueNode) ? child : parent);
}

void StagePatchAndCasePanel::valueTreeChildOrderChanged(juce::ValueTree& parent, int, int)
{
    handleVenueTreeChange(parent);
}

StagePatchAndCasePanel::ValidZone StagePatchAndCasePanel::getValidZoneForCategory(StageRoleCatalog::Category cat)
{
    using Cat = StageRoleCatalog::Category;
    switch (cat)
    {
    case Cat::AirWashBack:
    case Cat::AirParFront:
    case Cat::AirMovingBeam:
    case Cat::ImpactBlinder:
    case Cat::ImpactStrobe:
        return ValidZone::AirTruss;

    case Cat::FloorSideWash:
        return ValidZone::SideWings;

    case Cat::FloorBackSilhouette:
    case Cat::FloorMovingBeam:
    case Cat::FxGeyserBurst:
    case Cat::FxSmokeHazer:
        return ValidZone::FloorStage;
    }
    return ValidZone::AirTruss;
}

void StagePatchAndCasePanel::resized()
{
    auto b = getLocalBounds().toFloat().reduced(8.0f);

    auto topHeader = getLocalBounds().removeFromTop(32).reduced(14, 4);
    clearStageBtn.setBounds(topHeader.removeFromRight(155));
    topHeader.removeFromRight(6);
    autoPlaceBtn.setBounds(topHeader.removeFromRight(135));

    lightCaseArea = b.removeFromTop(118.0f);
    b.removeFromTop(8.0f);
    stageCanvasArea = b;

    // Rettangolo palco effettivo identico a StageSimulatorView::getStageRect() (esclude i 24 px del titolo)
    stageRect = stageCanvasArea.withTrimmedTop(24.0f);

    airZoneRect = StageArtworkHelper::getAirZoneRect(stageRect);
    floorZoneRect = StageArtworkHelper::getFloorZoneRect(stageRect);
    leftWingRect = StageArtworkHelper::getLeftWingRect(stageRect);
    rightWingRect = StageArtworkHelper::getRightWingRect(stageRect);

    rebuildUnitCache();
}

void StagePatchAndCasePanel::rebuildUnitCache()
{
    if (isRebuilding)
        return;

    juce::ScopedValueSetter<bool> guard(isRebuilding, true);

    unplacedUnits.clear();
    placedUnits.clear();
    hoveredPlacedIndex = -1;

    venueNode = appState.getChildWithName(StateKeys::VenueNode);
    if (!venueNode.isValid())
        return;

    VenueUnitHelper::ensureVenueUnitsInitialized(venueNode);

    for (int f = 0; f < venueNode.getNumChildren(); ++f)
    {
        auto fix = venueNode.getChild(f);
        if (!fix.hasType(StateKeys::Fixture))
            continue;

        juce::String baseName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
        juce::String roleName = fix.getProperty(StateKeys::RoleName, "").toString();
        auto cat = StageRoleCatalog::classifyRole(roleName, baseName);
        int sharedDmxAddr = static_cast<int>(fix.getProperty(StateKeys::StartAddress, 1));

        int unitCounter = 0;
        for (int u = 0; u < fix.getNumChildren(); ++u)
        {
            auto unitNode = fix.getChild(u);
            if (!unitNode.hasType(StateKeys::FixtureUnit))
                continue;

            ++unitCounter;
            VisualUnitItem item;
            item.fixtureIdx = f;
            item.unitIdxInChild = u;
            item.unitNumber = unitCounter;
            item.dmxAddress = sharedDmxAddr;
            item.label = baseName + " #" + juce::String(unitCounter);
            item.roleName = roleName;
            item.category = cat;
            item.isPlaced = static_cast<bool>(unitNode.getProperty(StateKeys::IsPlaced, false));
            item.unitNode = unitNode;

            if (item.isPlaced)
            {
                float normX = static_cast<float>(static_cast<double>(unitNode.getProperty(StateKeys::PosX, 0.5)));
                float normY = static_cast<float>(static_cast<double>(unitNode.getProperty(StateKeys::PosY, 0.14)));
                float px = stageRect.getX() + normX * stageRect.getWidth();
                float py = stageRect.getY() + normY * stageRect.getHeight();
                item.center = { px, py };
                // Area di presa comoda (36x28) attorno all'icona hardware 16x12
                item.screenBounds = juce::Rectangle<float>(px - 18.0f, py - 14.0f, 36.0f, 28.0f);
                placedUnits.push_back(item);
            }
            else
            {
                unplacedUnits.push_back(item);
            }
        }
    }

    float caseX = lightCaseArea.getX() + 10.0f;
    float caseY = lightCaseArea.getY() + 32.0f;
    float tileW = 86.0f;
    float tileH = 36.0f;
    float gap = 6.0f;
    float maxRight = lightCaseArea.getRight() - 10.0f;

    for (auto& item : unplacedUnits)
    {
        if (caseX + tileW > maxRight)
        {
            caseX = lightCaseArea.getX() + 10.0f;
            caseY += tileH + gap;
        }
        item.screenBounds = juce::Rectangle<float>(caseX, caseY, tileW, tileH);
        item.center = item.screenBounds.getCentre();
        caseX += tileW + gap;
    }
}

void StagePatchAndCasePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12131a));

    auto getZoneColour = [](ValidZone z) -> juce::Colour
        {
            if (z == ValidZone::FloorStage) return juce::Colour(0xfff59e0b);
            if (z == ValidZone::SideWings)  return juce::Colour(0xffa855f7);
            return juce::Colour(0xff38bdf8);
        };

    // --- 1. BOX "LIGHT CASE" ---
    g.setColour(juce::Colour(0xff1c1e29));
    g.fillRoundedRectangle(lightCaseArea, 6.0f);
    g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.5f));
    g.drawRoundedRectangle(lightCaseArea, 6.0f, 1.5f);

    g.setColour(juce::Colour(0xff38bdf8));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("LIGHT CASE (Trascina le luci sul Palco)",
        static_cast<int>(lightCaseArea.getX() + 12.0f),
        static_cast<int>(lightCaseArea.getY() + 6.0f),
        340, 22, juce::Justification::centredLeft, true);

    auto drawCaseTile = [&](const VisualUnitItem& item, juce::Colour zoneCol)
        {
            const bool hasSelection = (selectedFixtureIdx >= 0);
            const bool isSelectedGroup = (hasSelection && item.fixtureIdx == selectedFixtureIdx);
            const float contentAlpha = (hasSelection && !isSelectedGroup) ? 0.55f : 1.0f;
            const auto& tileRect = item.screenBounds;

            if (isSelectedGroup)
            {
                g.setColour(juce::Colour(0xfffacc15).withAlpha(0.28f));
                g.fillRoundedRectangle(tileRect.expanded(3.0f), 7.0f);
            }

            g.setColour(isSelectedGroup ? juce::Colour(0xff2b3146)
                : (hasSelection ? juce::Colour(0xff151924) : juce::Colour(0xff1e2436)));
            g.fillRoundedRectangle(tileRect, 5.0f);

            g.setColour(isSelectedGroup ? juce::Colour(0xfffacc15) : zoneCol.withAlpha(contentAlpha));
            g.drawRoundedRectangle(tileRect, 5.0f, isSelectedGroup ? 2.2f : 1.3f);

            g.setColour((isSelectedGroup ? juce::Colour(0xfffef08a) : juce::Colours::white).withAlpha(contentAlpha));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(item.label, tileRect.withHeight(18.0f).reduced(3.0f, 1.0f),
                juce::Justification::centred, true);

            g.setColour((isSelectedGroup ? juce::Colour(0xfffde047) : zoneCol.brighter(0.3f)).withAlpha(contentAlpha));
            g.setFont(juce::FontOptions(9.5f));
            g.drawText("DMX " + juce::String(item.dmxAddress),
                tileRect.withTrimmedTop(17.0f),
                juce::Justification::centred, true);
        };

    if (unplacedUnits.empty())
    {
        g.setColour(juce::Colour(0xff64748b));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Tutte le luci sono montate sul palco! (Tasto destro su una luce per rimetterla nel Case)",
            lightCaseArea.withTrimmedTop(26.0f), juce::Justification::centred, true);
    }
    else
    {
        for (const auto& item : unplacedUnits)
            drawCaseTile(item, getZoneColour(getValidZoneForCategory(item.category)));
    }

    // --- 2. PALCO 2D UNIFICATO (Identico a Tab 2 e Tab 3) ---
    g.setColour(juce::Colour(0xff94a3b8));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("MAPPA FISICA PALCO  |  Trascina i fari su Americana, Quinte o Pavimento (Tasto DX = Case)",
        static_cast<int>(stageCanvasArea.getX() + 6.0f),
        static_cast<int>(stageCanvasArea.getY() + 2.0f),
        static_cast<int>(stageCanvasArea.getWidth() - 12.0f),
        20, juce::Justification::centredLeft, true);

    StageArtworkHelper::drawStageBackdrop(g, stageRect);

    const float trussY = stageRect.getY() + stageRect.getHeight() * StageArtworkHelper::kTrussNormY;
    const float floorY = stageRect.getY() + stageRect.getHeight() * StageArtworkHelper::kFloorNormY;

    StageArtworkHelper::drawTrussAndFloorStructure(g, stageRect, trussY, floorY);
    StageArtworkHelper::drawBandSilhouettes(g, stageRect, floorY);

    // Zone colorate trasparenti sempre visibili come guida di posizionamento
    auto drawZoneBox = [&g](const juce::Rectangle<float>& r,
        const juce::String& title,
        juce::Colour col,
        juce::Justification labelJust,
        bool isActiveTarget)
        {
            g.setColour(col.withAlpha(isActiveTarget ? 0.14f : 0.06f));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(col.withAlpha(isActiveTarget ? 0.85f : 0.34f));
            g.drawRoundedRectangle(r, 5.0f, isActiveTarget ? 2.0f : 1.0f);

            g.setColour(col.withAlpha(isActiveTarget ? 0.95f : 0.65f));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(title, r.reduced(6.0f, 3.0f), labelJust, true);
        };

    drawZoneBox(airZoneRect, "ZONA ARIA (Americana: Wash, Par, Beam, Blinder, Strobo)",
        juce::Colour(0xff38bdf8), juce::Justification::bottomLeft,
        isDraggingUnit && activeDragZone == ValidZone::AirTruss);

    drawZoneBox(floorZoneRect, "ZONA TERRA (Pavimento: Controluce, Beam Terra, Geyser, Hazer)",
        juce::Colour(0xfff59e0b), juce::Justification::bottomLeft,
        isDraggingUnit && activeDragZone == ValidZone::FloorStage);

    drawZoneBox(leftWingRect, "QUINTA SX",
        juce::Colour(0xffa855f7), juce::Justification::topLeft,
        isDraggingUnit && activeDragZone == ValidZone::SideWings);

    drawZoneBox(rightWingRect, "QUINTA DX",
        juce::Colour(0xffa855f7), juce::Justification::topRight,
        isDraggingUnit && activeDragZone == ValidZone::SideWings);

    // --- 3. FARI MONTATI (Stessa icona hardware di Tab 2 e Tab 3) ---
    for (size_t i = 0; i < placedUnits.size(); ++i)
    {
        const auto& item = placedUnits[i];
        const auto zoneCol = getZoneColour(getValidZoneForCategory(item.category));
        const bool hasSelection = (selectedFixtureIdx >= 0);
        const bool isSelectedGroup = (hasSelection && item.fixtureIdx == selectedFixtureIdx);
        const bool isHovered = (static_cast<int>(i) == hoveredPlacedIndex);
        const float dimAlpha = (hasSelection && !isSelectedGroup && !isHovered) ? 0.62f : 1.0f;

        juce::Rectangle<float> box(item.center.x - 8.0f, item.center.y - 6.0f, 16.0f, 12.0f);

        if (isSelectedGroup || isHovered)
        {
            juce::Colour hiCol = isSelectedGroup ? juce::Colour(0xfffacc15) : juce::Colour(0xff38bdf8);
            g.setColour(hiCol.withAlpha(isSelectedGroup ? 0.30f : 0.22f));
            g.fillRoundedRectangle(box.expanded(4.0f), 5.0f);
        }

        g.setColour(juce::Colour(0xff1e202b));
        g.fillRoundedRectangle(box, 3.0f);

        juce::Colour borderCol = isSelectedGroup ? juce::Colour(0xfffacc15)
            : (isHovered ? juce::Colour(0xff38bdf8) : zoneCol.withAlpha(0.75f * dimAlpha));
        g.setColour(borderCol);
        g.drawRoundedRectangle(box, 3.0f, (isSelectedGroup || isHovered) ? 2.0f : 1.2f);

        // Lente interna colorata per categoria/selezione
        juce::Colour lensCol = isSelectedGroup ? juce::Colour(0xfffacc15) : zoneCol;
        g.setColour(lensCol.withAlpha((isSelectedGroup ? 0.85f : 0.35f) * dimAlpha));
        g.fillEllipse(item.center.x - 4.5f, item.center.y - 4.5f, 9.0f, 9.0f);

        // Etichetta nome + DMX compatta (sopra per i fari su/sopra americana, sotto per gli altri)
        const bool labelAbove = (item.center.y <= trussY + 4.0f);
        const float nameY = labelAbove ? (item.center.y - 25.0f) : (item.center.y + 8.0f);
        const float dmxY = labelAbove ? (item.center.y - 15.0f) : (item.center.y + 18.0f);

        g.setColour((isSelectedGroup ? juce::Colour(0xfffef08a) : juce::Colour(0xffcbd5e1)).withAlpha(dimAlpha));
        g.setFont(juce::FontOptions(9.5f, isSelectedGroup ? juce::Font::bold : juce::Font::plain));
        g.drawText(item.label,
            static_cast<int>(item.center.x - 46.0f), static_cast<int>(nameY),
            92, 11, juce::Justification::centred, true);

        g.setColour((isSelectedGroup ? juce::Colour(0xfffde047) : zoneCol.brighter(0.25f)).withAlpha(0.85f * dimAlpha));
        g.setFont(juce::FontOptions(8.5f));
        g.drawText("DMX " + juce::String(item.dmxAddress),
            static_cast<int>(item.center.x - 36.0f), static_cast<int>(dmxY),
            72, 10, juce::Justification::centred, true);
    }

    // --- 4. SMART MASKING SEMI-TRASPARENTE DURANTE IL DRAG & DROP ---
    if (isDraggingUnit && activeDragZone != ValidZone::None)
    {
        juce::Path maskPath;
        maskPath.addRoundedRectangle(stageRect, 6.0f);
        maskPath.setUsingNonZeroWinding(false);

        if (activeDragZone == ValidZone::AirTruss)
            maskPath.addRoundedRectangle(airZoneRect, 5.0f);
        else if (activeDragZone == ValidZone::FloorStage)
            maskPath.addRoundedRectangle(floorZoneRect, 5.0f);
        else if (activeDragZone == ValidZone::SideWings)
        {
            maskPath.addRoundedRectangle(leftWingRect, 5.0f);
            maskPath.addRoundedRectangle(rightWingRect, 5.0f);
        }

        // Trasparenza bilanciata (0.68): scurisce le zone non valide ma lascia intravedere traliccio e band
        g.setColour(juce::Colours::black.withAlpha(0.68f));
        g.fillPath(maskPath);

        g.setColour(juce::Colour(0xff10b981));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        if (activeDragZone == ValidZone::AirTruss)
        {
            g.drawRoundedRectangle(airZoneRect, 5.0f, 2.4f);
            g.drawText("RILASCIA IN AMERICANA: " + activeDragLabel,
                airZoneRect.reduced(8.0f, 4.0f), juce::Justification::topRight, true);
        }
        else if (activeDragZone == ValidZone::FloorStage)
        {
            g.drawRoundedRectangle(floorZoneRect, 5.0f, 2.4f);
            g.drawText("RILASCIA A TERRA: " + activeDragLabel,
                floorZoneRect.reduced(8.0f, 4.0f), juce::Justification::topRight, true);
        }
        else if (activeDragZone == ValidZone::SideWings)
        {
            g.drawRoundedRectangle(leftWingRect, 5.0f, 2.4f);
            g.drawRoundedRectangle(rightWingRect, 5.0f, 2.4f);
        }
    }

    g.setColour(juce::Colour(0xff2a2a38));
    g.drawRoundedRectangle(stageRect, 6.0f, 1.5f);
}

void StagePatchAndCasePanel::mouseMove(const juce::MouseEvent& e)
{
    int newHover = -1;
    for (size_t i = 0; i < placedUnits.size(); ++i)
    {
        if (placedUnits[i].screenBounds.contains(e.position))
        {
            newHover = static_cast<int>(i);
            break;
        }
    }

    if (newHover != hoveredPlacedIndex)
    {
        hoveredPlacedIndex = newHover;
        repaint();
    }

    bool overAnyUnit = (newHover >= 0);
    if (!overAnyUnit)
    {
        for (const auto& u : unplacedUnits)
        {
            if (u.screenBounds.contains(e.position))
            {
                overAnyUnit = true;
                break;
            }
        }
    }
    setMouseCursor(overAnyUnit ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void StagePatchAndCasePanel::mouseExit(const juce::MouseEvent&)
{
    if (hoveredPlacedIndex != -1)
    {
        hoveredPlacedIndex = -1;
        repaint();
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void StagePatchAndCasePanel::mouseDown(const juce::MouseEvent& e)
{
    hitItemIndex = -1;
    hitWasPlaced = false;
    auto pt = e.position;

    for (size_t i = 0; i < placedUnits.size(); ++i)
    {
        if (placedUnits[i].screenBounds.contains(pt))
        {
            if (e.mods.isPopupMenu())
            {
                placedUnits[i].unitNode.setProperty(StateKeys::IsPlaced, false, nullptr);
                rebuildUnitCache();
                repaint();
                return;
            }

            hitItemIndex = static_cast<int>(i);
            hitWasPlaced = true;
            if (hitItemIndex >= 0)
            {
                const auto& list = hitWasPlaced ? placedUnits : unplacedUnits;
                if (hitItemIndex < static_cast<int>(list.size()))
                {
                    int clickedFixIdx = list[static_cast<size_t>(hitItemIndex)].fixtureIdx;
                    setSelectedFixtureIndex(clickedFixIdx);
                    if (onFixtureSelectedFromMap)
                        onFixtureSelectedFromMap(clickedFixIdx);
                }
            }
            return;
        }
    }

    for (size_t i = 0; i < unplacedUnits.size(); ++i)
    {
        if (unplacedUnits[i].screenBounds.contains(pt))
        {
            hitItemIndex = static_cast<int>(i);
            hitWasPlaced = false;
            return;
        }
    }
}

void StagePatchAndCasePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (hitItemIndex < 0 || e.getDistanceFromDragStart() < 4)
        return;

    if (!isDraggingUnit)
    {
        const auto& list = hitWasPlaced ? placedUnits : unplacedUnits;
        if (hitItemIndex < static_cast<int>(list.size()))
        {
            const auto& item = list[static_cast<size_t>(hitItemIndex)];
            activeDragZone = getValidZoneForCategory(item.category);
            activeDragLabel = item.label;
            isDraggingUnit = true;

            // Ghost compatto centrato esattamente sull'icona hardware 16x12 del faro
            const int imgW = 96;
            const int imgH = 40;
            const float cx = imgW * 0.5f;
            const float cy = 12.0f;
            juce::Image dragImg(juce::Image::ARGB, imgW, imgH, true);
            {
                juce::Graphics g(dragImg);
                juce::Colour accentCol(0xff38bdf8);
                if (activeDragZone == ValidZone::FloorStage)     accentCol = juce::Colour(0xfff59e0b);
                else if (activeDragZone == ValidZone::SideWings) accentCol = juce::Colour(0xffa855f7);

                juce::Rectangle<float> box(cx - 8.0f, cy - 6.0f, 16.0f, 12.0f);
                g.setColour(accentCol.withAlpha(0.32f));
                g.fillRoundedRectangle(box.expanded(3.5f), 5.0f);
                g.setColour(juce::Colour(0xff1e202b));
                g.fillRoundedRectangle(box, 3.0f);
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(box, 3.0f, 1.6f);
                g.setColour(accentCol);
                g.fillEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);

                g.setColour(juce::Colour(0xff0b0c12).withAlpha(0.85f));
                g.fillRoundedRectangle(4.0f, 22.0f, static_cast<float>(imgW - 8), 15.0f, 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(item.label, 6, 22, imgW - 12, 15, juce::Justification::centred, true);
            }

            juce::Point<int> imgOffset(-imgW / 2, -static_cast<int>(cy));
            juce::String dragDesc = juce::String(item.fixtureIdx) + ":" + juce::String(item.unitIdxInChild);

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
                container->startDragging(dragDesc, this, juce::ScaledImage(dragImg), false, &imgOffset);

            repaint();
        }
    }
}

void StagePatchAndCasePanel::mouseUp(const juce::MouseEvent&)
{
    isDraggingUnit = false;
    activeDragZone = ValidZone::None;
    hitItemIndex = -1;
    repaint();
}

bool StagePatchAndCasePanel::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
{
    return dragSourceDetails.description.toString().contains(":");
}

StagePatchAndCasePanel::ValidZone StagePatchAndCasePanel::getZoneForDragDescription(
    const juce::String& desc, juce::String& outLabel) const
{
    int fixIdx = desc.upToFirstOccurrenceOf(":", false, false).getIntValue();
    int unitIdx = desc.fromFirstOccurrenceOf(":", false, false).getIntValue();

    auto vNode = appState.getChildWithName(StateKeys::VenueNode);
    if (!vNode.isValid() || fixIdx < 0 || fixIdx >= vNode.getNumChildren())
        return ValidZone::None;

    auto fixNode = vNode.getChild(fixIdx);
    if (!fixNode.isValid())
        return ValidZone::None;

    juce::String baseName = fixNode.getProperty(StateKeys::FixtureName, "Faro").toString();
    juce::String roleName = fixNode.getProperty(StateKeys::RoleName, "").toString();

    int unitNum = 1;
    if (unitIdx >= 0 && unitIdx < fixNode.getNumChildren())
        unitNum = static_cast<int>(fixNode.getChild(unitIdx).getProperty(StateKeys::UnitIndex, unitIdx + 1));

    outLabel = baseName + " #" + juce::String(unitNum);
    return getValidZoneForCategory(StageRoleCatalog::classifyRole(roleName, baseName));
}

void StagePatchAndCasePanel::itemDragEnter(const SourceDetails& dragSourceDetails)
{
    activeDragZone = getZoneForDragDescription(dragSourceDetails.description.toString(), activeDragLabel);
    isDraggingUnit = (activeDragZone != ValidZone::None);
    repaint();
}

void StagePatchAndCasePanel::itemDragMove(const SourceDetails& dragSourceDetails)
{
    if (activeDragZone == ValidZone::None)
        activeDragZone = getZoneForDragDescription(dragSourceDetails.description.toString(), activeDragLabel);

    isDraggingUnit = (activeDragZone != ValidZone::None);
    repaint();
}

void StagePatchAndCasePanel::itemDragExit(const SourceDetails&)
{
    isDraggingUnit = false;
    activeDragZone = ValidZone::None;
    repaint();
}

bool StagePatchAndCasePanel::isPointInsideZone(juce::Point<float> pt, ValidZone zone) const
{
    if (zone == ValidZone::AirTruss)   return airZoneRect.expanded(12.0f).contains(pt);
    if (zone == ValidZone::FloorStage) return floorZoneRect.expanded(12.0f).contains(pt);
    if (zone == ValidZone::SideWings)  return leftWingRect.expanded(12.0f).contains(pt) || rightWingRect.expanded(12.0f).contains(pt);
    return false;
}

juce::Point<float> StagePatchAndCasePanel::clampPointToZone(juce::Point<float> pt, ValidZone zone) const
{
    juce::Rectangle<float> target = airZoneRect;
    if (zone == ValidZone::FloorStage)
        target = floorZoneRect;
    else if (zone == ValidZone::SideWings)
        target = (pt.x < stageRect.getCentreX()) ? leftWingRect : rightWingRect;

    const float padX = juce::jmin(10.0f, target.getWidth() * 0.25f);
    const float padY = juce::jmin(8.0f, target.getHeight() * 0.25f);
    const auto inner = target.reduced(padX, padY);

    return {
        juce::jlimit(inner.getX(), juce::jmax(inner.getX(), inner.getRight()), pt.x),
        juce::jlimit(inner.getY(), juce::jmax(inner.getY(), inner.getBottom()), pt.y)
    };
}

void StagePatchAndCasePanel::itemDropped(const SourceDetails& dragSourceDetails)
{
    isDraggingUnit = false;
    activeDragZone = ValidZone::None;

    juce::String desc = dragSourceDetails.description.toString();
    juce::String dummyLabel;
    ValidZone targetZone = getZoneForDragDescription(desc, dummyLabel);
    if (targetZone == ValidZone::None)
        return;

    int fixIdx = desc.upToFirstOccurrenceOf(":", false, false).getIntValue();
    int unitIdx = desc.fromFirstOccurrenceOf(":", false, false).getIntValue();

    venueNode = appState.getChildWithName(StateKeys::VenueNode);
    if (!venueNode.isValid() || fixIdx < 0 || fixIdx >= venueNode.getNumChildren())
        return;

    auto fixNode = venueNode.getChild(fixIdx);
    if (!fixNode.isValid() || unitIdx < 0 || unitIdx >= fixNode.getNumChildren())
        return;

    auto unitNode = fixNode.getChild(unitIdx);
    auto dropPt = dragSourceDetails.localPosition.toFloat();

    if (lightCaseArea.contains(dropPt))
    {
        unitNode.setProperty(StateKeys::IsPlaced, false, nullptr);
        rebuildUnitCache();
        repaint();
        return;
    }

    if (stageRect.expanded(8.0f).contains(dropPt) && stageRect.getWidth() > 1.0f && stageRect.getHeight() > 1.0f)
    {
        auto clampedPt = clampPointToZone(dropPt, targetZone);
        double normX = juce::jlimit(0.03, 0.97, static_cast<double>((clampedPt.x - stageRect.getX()) / stageRect.getWidth()));
        double normY = juce::jlimit(0.05, 0.95, static_cast<double>((clampedPt.y - stageRect.getY()) / stageRect.getHeight()));

        unitNode.setProperty(StateKeys::IsPlaced, true, nullptr);
        unitNode.setProperty(StateKeys::PosX, normX, nullptr);
        unitNode.setProperty(StateKeys::PosY, normY, nullptr);
    }

    rebuildUnitCache();
    repaint();
}

void StagePatchAndCasePanel::autoPlaceAllUnits()
{
    if (!venueNode.isValid()) return;
    VenueUnitHelper::ensureVenueUnitsInitialized(venueNode);

    std::vector<juce::ValueTree> airUnits, floorUnits, sideUnits;

    for (int f = 0; f < venueNode.getNumChildren(); ++f)
    {
        auto fix = venueNode.getChild(f);
        if (!fix.hasType(StateKeys::Fixture)) continue;

        auto cat = StageRoleCatalog::classifyRole(
            fix.getProperty(StateKeys::RoleName, "").toString(),
            fix.getProperty(StateKeys::FixtureName, "").toString());
        auto zone = getValidZoneForCategory(cat);

        for (int u = 0; u < fix.getNumChildren(); ++u)
        {
            auto unit = fix.getChild(u);
            if (!unit.hasType(StateKeys::FixtureUnit)) continue;
            if (zone == ValidZone::AirTruss)        airUnits.push_back(unit);
            else if (zone == ValidZone::FloorStage) floorUnits.push_back(unit);
            else                                    sideUnits.push_back(unit);
        }
    }

    for (size_t i = 0; i < airUnits.size(); ++i)
    {
        double ratio = static_cast<double>(i + 1) / static_cast<double>(airUnits.size() + 1);
        airUnits[i].setProperty(StateKeys::IsPlaced, true, nullptr);
        airUnits[i].setProperty(StateKeys::PosX, 0.08 + 0.84 * ratio, nullptr);
        airUnits[i].setProperty(StateKeys::PosY, static_cast<double>(StageArtworkHelper::kTrussNormY), nullptr);
    }

    for (size_t i = 0; i < floorUnits.size(); ++i)
    {
        double ratio = static_cast<double>(i + 1) / static_cast<double>(floorUnits.size() + 1);
        floorUnits[i].setProperty(StateKeys::IsPlaced, true, nullptr);
        floorUnits[i].setProperty(StateKeys::PosX, 0.16 + 0.68 * ratio, nullptr);
        floorUnits[i].setProperty(StateKeys::PosY, static_cast<double>(StageArtworkHelper::kFloorNormY), nullptr);
    }

    for (size_t i = 0; i < sideUnits.size(); ++i)
    {
        bool isLeft = (i % 2 == 0);
        double yRatio = 0.50 + 0.14 * static_cast<double>(i / 2);
        sideUnits[i].setProperty(StateKeys::IsPlaced, true, nullptr);
        sideUnits[i].setProperty(StateKeys::PosX, isLeft ? 0.065 : 0.935, nullptr);
        sideUnits[i].setProperty(StateKeys::PosY, juce::jmin(0.90, yRatio), nullptr);
    }

    rebuildUnitCache();
    repaint();
}

void StagePatchAndCasePanel::returnAllUnitsToCase()
{
    if (!venueNode.isValid()) return;
    for (int f = 0; f < venueNode.getNumChildren(); ++f)
    {
        auto fix = venueNode.getChild(f);
        for (int u = 0; u < fix.getNumChildren(); ++u)
        {
            auto unit = fix.getChild(u);
            if (unit.hasType(StateKeys::FixtureUnit))
                unit.setProperty(StateKeys::IsPlaced, false, nullptr);
        }
    }
    rebuildUnitCache();
    repaint();
}



// =============================================================================
// VenuePatchTab Implementation
// =============================================================================
VenuePatchTab::VenuePatchTab(juce::ValueTree& state)
    : appState(state), editorPanel(state), stageDesignerPanel(state)
{
    stageDesignerPanel.onFixtureSelectedFromMap = [this](int fixIdx) {
        if (fixIdx >= 0 && fixIdx < venueNode.getNumChildren())
            fixtureList.selectRow(fixIdx);
        };

    venueNode = appState.getChildWithName(StateKeys::VenueNode);
    if (!venueNode.isValid())
    {
        venueNode = juce::ValueTree(StateKeys::VenueNode);
        appState.addChild(venueNode, -1, nullptr);
    }
    VenueUnitHelper::ensureVenueUnitsInitialized(venueNode);
    venueNode.addListener(this);

    fixtureList.setModel(this);
    fixtureList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141417));
    fixtureList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2a2a32));
    addAndMakeVisible(fixtureList);

    addFixtureBtn.setButtonText("+ Aggiungi Faro");
    addFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2563eb));
    addFixtureBtn.onClick = [this] { addNewFixtureToState(); };
    addAndMakeVisible(addFixtureBtn);

    removeFixtureBtn.setButtonText("- Rimuovi Selezionato");
    removeFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f1d1d));
    removeFixtureBtn.onClick = [this] { removeSelectedFixture(); };
    addAndMakeVisible(removeFixtureBtn);

    duplicateFixtureBtn.setButtonText("Duplica Faro");
    duplicateFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff334155));
    duplicateFixtureBtn.setTooltip("Clona il faro selezionato con mappatura, calibrazione e primo indirizzo DMX libero");
    duplicateFixtureBtn.onClick = [this] { duplicateSelectedFixture(); };
    addAndMakeVisible(duplicateFixtureBtn);

    testFixtureBtn.setButtonText("Test Faro");
    testFixtureBtn.setClickingTogglesState(true);
    testFixtureBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e3a5f));
    testFixtureBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff59e0b));
    testFixtureBtn.setTooltip("Accende al 100% bianco e centra Pan/Tilt SOLO sul faro selezionato (richiede DMX attivo in Tab 6)");
    testFixtureBtn.onClick = [this] { updateFixtureTestOutput(); };
    addAndMakeVisible(testFixtureBtn);

    loadBtn.setButtonText("Carica Venue...");
    loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d29));
    loadBtn.onClick = [this] { loadVenue(); };
    addAndMakeVisible(loadBtn);

    saveBtn.setButtonText("Salva Venue...");
    saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff293d3d));
    saveBtn.onClick = [this] { saveVenue(); };
    addAndMakeVisible(saveBtn);

    addAndMakeVisible(editorPanel);
    addAndMakeVisible(stageDesignerPanel);

    if (venueNode.getNumChildren() > 0)
        fixtureList.selectRow(0);
}

VenuePatchTab::~VenuePatchTab()
{
    if (dmxLiveEngine != nullptr)
        dmxLiveEngine->stopFixtureTest();

    venueNode.removeListener(this);
}

int VenuePatchTab::getNumRows()
{
    return venueNode.getNumChildren();
}

void VenuePatchTab::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff00d2ff).withAlpha(0.22f));

    if (rowNumber < venueNode.getNumChildren())
    {
        auto fixNode = venueNode.getChild(rowNumber);
        auto name = fixNode.getProperty(StateKeys::FixtureName, "Senza Nome").toString();
        auto role = fixNode.getProperty(StateKeys::RoleName, "NESSUNO").toString();

        int qty = static_cast<int>(fixNode.getProperty(StateKeys::FixtureQuantity, 1));
        int startAddr = static_cast<int>(fixNode.getProperty(StateKeys::StartAddress, 1));
        int chCount = static_cast<int>(fixNode.getProperty(StateKeys::ChannelCount, 1));
        int endAddr = startAddr + chCount - 1; // Tutte le N unita' rispondono allo stesso range DMX

        juce::String labelText = name + " (x" + juce::String(qty) + " in Sim)  |  DMX: "
            + juce::String(startAddr) + "-" + juce::String(endAddr)
            + "  [" + role + "]";

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(labelText, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }
}

void VenuePatchTab::selectedRowsChanged(int lastRowSelected)
{
    stageDesignerPanel.setSelectedFixtureIndex(lastRowSelected);

    if (lastRowSelected >= 0 && lastRowSelected < venueNode.getNumChildren())
        editorPanel.setFixtureNode(venueNode.getChild(lastRowSelected));
    else
        editorPanel.setFixtureNode(juce::ValueTree());

    if (testFixtureBtn.getToggleState())
        updateFixtureTestOutput();
}

void VenuePatchTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1e));
}

void VenuePatchTab::resized()
{
    auto bounds = getLocalBounds();

    auto leftPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.23f));
    auto bottomArea = leftPanel.removeFromBottom(174).reduced(8, 4);

    saveBtn.setBounds(bottomArea.removeFromBottom(28));
    bottomArea.removeFromBottom(4);
    loadBtn.setBounds(bottomArea.removeFromBottom(28));
    bottomArea.removeFromBottom(4);
    removeFixtureBtn.setBounds(bottomArea.removeFromBottom(28));
    bottomArea.removeFromBottom(4);

    auto dupTestRow = bottomArea.removeFromBottom(28);
    const int halfW = dupTestRow.getWidth() / 2;
    duplicateFixtureBtn.setBounds(dupTestRow.removeFromLeft(halfW).withTrimmedRight(2));
    testFixtureBtn.setBounds(dupTestRow.withTrimmedLeft(2));
    bottomArea.removeFromBottom(4);

    addFixtureBtn.setBounds(bottomArea.removeFromBottom(28));

    fixtureList.setBounds(leftPanel.reduced(8));

    auto centerPanel = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.45f));
    editorPanel.setBounds(centerPanel.reduced(4));

    stageDesignerPanel.setBounds(bounds.reduced(4));
}

void VenuePatchTab::addNewFixtureToState()
{
    juce::ValueTree newFix(StateKeys::Fixture);
    int newIndex = venueNode.getNumChildren() + 1;
    newFix.setProperty(StateKeys::FixtureName, "Faro " + juce::String(newIndex), nullptr);
    newFix.setProperty(StateKeys::FixtureQuantity, 2, nullptr);
    newFix.setProperty(StateKeys::StartAddress, (newIndex - 1) * 10 + 1, nullptr);
    newFix.setProperty(StateKeys::ChannelCount, 8, nullptr);
    newFix.setProperty(StateKeys::RoleName, StageRoleCatalog::getAllRoles()[0].displayName, nullptr);

    newFix.setProperty(StateKeys::getMappingKey("Dimmer"), 1, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Red"), 2, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Green"), 3, nullptr);
    newFix.setProperty(StateKeys::getMappingKey("Blue"), 4, nullptr);

    
    newFix.setProperty(StateKeys::StrobeMode, 1, nullptr);      // 1 = Canale Separato
    newFix.setProperty(StateKeys::DimmerMaxVal, 255, nullptr);
    newFix.setProperty(StateKeys::StrobeOpenVal, 0, nullptr);
    newFix.setProperty(StateKeys::StrobeMinVal, 20, nullptr);
    newFix.setProperty(StateKeys::StrobeMinHz, 1.0, nullptr);
    newFix.setProperty(StateKeys::StrobeMaxVal, 245, nullptr);
    newFix.setProperty(StateKeys::StrobeMaxHz, 20.0, nullptr);

    StageRoleCatalog::PanTiltHardwareSpec defPt;
    defPt.applyQuickSpread(1); // Default: Medio (Palco)
    defPt.writeToFixtureNode(newFix);

    VenueUnitHelper::syncUnitsForFixture(newFix);

    venueNode.addChild(newFix, -1, nullptr);
    fixtureList.updateContent();
    fixtureList.selectRow(venueNode.getNumChildren() - 1);
}

void VenuePatchTab::removeSelectedFixture()
{
    int sel = fixtureList.getSelectedRow();
    if (sel >= 0 && sel < venueNode.getNumChildren())
    {
        venueNode.removeChild(sel, nullptr);
        fixtureList.updateContent();
        if (venueNode.getNumChildren() > 0)
            fixtureList.selectRow(juce::jmin(sel, venueNode.getNumChildren() - 1));
    }
    if (testFixtureBtn.getToggleState())
        updateFixtureTestOutput();
}

void VenuePatchTab::saveVenue()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Salva Configurazione Venue",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NuovoLocale.venue"),
        "*.venue"
    );

    auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            PresetManager::saveVenue(appState, file);
        });
}

void VenuePatchTab::loadVenue()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Carica Configurazione Venue",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.venue"
    );

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            if (PresetManager::loadVenue(appState, file))
            {
                // Riallinea il riferimento nel caso PresetManager abbia ricreato il nodo VENUE
                auto currentVenue = appState.getChildWithName(StateKeys::VenueNode);
                if (currentVenue.isValid() && currentVenue != venueNode)
                {
                    if (venueNode.isValid())
                        venueNode.removeListener(this);
                    venueNode = currentVenue;
                    venueNode.addListener(this);
                }

                // Retrocompatibilita': se il file e' vecchio crea i FIXTURE_UNIT mancanti;
                // se il file e' nuovo mantiene intatti isPlaced, posX e posY caricati da disco
                VenueUnitHelper::ensureVenueUnitsInitialized(venueNode);

                fixtureList.updateContent();
                if (venueNode.getNumChildren() > 0)
                {
                    fixtureList.selectRow(0);
                    editorPanel.setFixtureNode(venueNode.getChild(0));
                }
                else
                {
                    editorPanel.setFixtureNode(juce::ValueTree());
                }

                stageDesignerPanel.setSelectedFixtureIndex(venueNode.getNumChildren() > 0 ? 0 : -1);

                // Ridisegna Light Case e fari piazzati nelle esatte posizioni salvate
                stageDesignerPanel.refreshFromState();
            }
        }
        });
}

void VenuePatchTab::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree&)
{
    if (parentTree == venueNode)
        fixtureList.updateContent();
    else
        fixtureList.repaint();
}

void VenuePatchTab::valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree&, int)
{
    if (parentTree == venueNode)
    {
        fixtureList.updateContent();
        if (venueNode.getNumChildren() == 0)
            editorPanel.setFixtureNode(juce::ValueTree());
    }
    else
    {
        // E' stata rimossa solo un'unita' FIXTURE_UNIT (cambio Quantita'): aggiorna solo il testo "(xN in Sim)"
        fixtureList.repaint();
    }
}

void VenuePatchTab::valueTreeChildOrderChanged(juce::ValueTree&, int, int) { fixtureList.updateContent(); }

void VenuePatchTab::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    fixtureList.repaint();

    if (isShowing() && testFixtureBtn.getToggleState() && tree.hasType(StateKeys::Fixture))
    {
        const int sel = fixtureList.getSelectedRow();
        if (sel >= 0 && sel < venueNode.getNumChildren() && venueNode.getChild(sel) == tree)
            updateFixtureTestOutput();
    }
}

void VenuePatchTab::forceStopFixtureTest()
{
    testFixtureBtn.setToggleState(false, juce::dontSendNotification);
    testFixtureBtn.setButtonText("Test Faro");

    if (dmxLiveEngine != nullptr && dmxLiveEngine->isFixtureTestActive())
        dmxLiveEngine->stopFixtureTest();
}

void VenuePatchTab::visibilityChanged()
{
    if (!isVisible() || dmxLiveEngine == nullptr)
        return;

    const bool stillTesting = dmxLiveEngine->isFixtureTestActive()
        && dmxLiveEngine->isOutputActive();

    if (!stillTesting && dmxLiveEngine->isFixtureTestActive())
        dmxLiveEngine->stopFixtureTest();

    testFixtureBtn.setToggleState(stillTesting, juce::dontSendNotification);
    testFixtureBtn.setButtonText(stillTesting ? "STOP Test" : "Test Faro");
}

int VenuePatchTab::computeFixtureDmxFootprint(const juce::ValueTree& fix)
{
    int span = juce::jmax(1, static_cast<int>(fix.getProperty(StateKeys::ChannelCount, 1)));

    static const char* params[] = {
        "Dimmer", "Strobe", "Red", "Green", "Blue", "White", "UV", "Pan", "Tilt", "GeyserBlast"
    };
    for (const char* p : params)
        span = juce::jmax(span, static_cast<int>(fix.getProperty(StateKeys::getMappingKey(p), 0)));

    return span;
}

int VenuePatchTab::findNextFreeDmxAddress(int preferredStart, int neededChannels) const
{
    if (!venueNode.isValid())
        return juce::jlimit(1, 512, preferredStart);

    std::array<bool, 513> occupied{};
    for (int i = 0; i < venueNode.getNumChildren(); ++i)
    {
        auto fix = venueNode.getChild(i);
        if (!fix.hasType(StateKeys::Fixture))
            continue;

        const int start = static_cast<int>(fix.getProperty(StateKeys::StartAddress, 0));
        if (start < 1 || start > 512)
            continue;

        const int span = computeFixtureDmxFootprint(fix);
        for (int ch = start; ch < start + span && ch <= 512; ++ch)
            occupied[static_cast<size_t>(ch)] = true;
    }

    auto rangeIsFree = [&](int start) -> bool
        {
            if (start < 1 || start + neededChannels - 1 > 512)
                return false;
            for (int ch = start; ch < start + neededChannels; ++ch)
                if (occupied[static_cast<size_t>(ch)])
                    return false;
            return true;
        };

    for (int cand = juce::jlimit(1, 512, preferredStart); cand <= 512; ++cand)
        if (rangeIsFree(cand))
            return cand;

    for (int cand = 1; cand < preferredStart; ++cand)
        if (rangeIsFree(cand))
            return cand;

    return juce::jlimit(1, 512, preferredStart);
}

void VenuePatchTab::duplicateSelectedFixture()
{
    const int sel = fixtureList.getSelectedRow();
    if (!venueNode.isValid() || sel < 0 || sel >= venueNode.getNumChildren())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Nessun Faro Selezionato",
            "Seleziona prima un faro dalla lista da duplicare.");
        return;
    }

    auto srcFix = venueNode.getChild(sel);
    if (!srcFix.isValid())
        return;

    auto copy = srcFix.createCopy();

    const int srcStart = static_cast<int>(srcFix.getProperty(StateKeys::StartAddress, 1));
    const int footprint = computeFixtureDmxFootprint(srcFix);
    const int nextAddr = findNextFreeDmxAddress(srcStart + footprint, footprint);

    const juce::String baseName = srcFix.getProperty(StateKeys::FixtureName, "Faro").toString();
    copy.setProperty(StateKeys::FixtureName, baseName + " (Copia)", nullptr);
    copy.setProperty(StateKeys::StartAddress, nextAddr, nullptr);

    // Sposta leggermente eventuali unita' piazzate sul palco 2D per non sovrapporle all'originale
    for (int u = 0; u < copy.getNumChildren(); ++u)
    {
        auto unit = copy.getChild(u);
        if (unit.hasType(StateKeys::FixtureUnit) &&
            static_cast<bool>(unit.getProperty(StateKeys::IsPlaced, false)))
        {
            double nx = static_cast<double>(unit.getProperty(StateKeys::PosX, 0.5));
            nx = (nx + 0.07 <= 0.95) ? (nx + 0.07) : juce::jmax(0.05, nx - 0.07);
            unit.setProperty(StateKeys::PosX, nx, nullptr);
        }
    }

    venueNode.addChild(copy, -1, nullptr);
    fixtureList.updateContent();
    fixtureList.selectRow(venueNode.getNumChildren() - 1);
    fixtureList.repaint();
}

void VenuePatchTab::updateFixtureTestOutput()
{
    if (dmxLiveEngine == nullptr)
        return;

    if (!testFixtureBtn.getToggleState())
    {
        testFixtureBtn.setButtonText("Test Faro");
        dmxLiveEngine->stopFixtureTest();
        return;
    }

    const int sel = fixtureList.getSelectedRow();
    if (!venueNode.isValid() || sel < 0 || sel >= venueNode.getNumChildren())
    {
        testFixtureBtn.setToggleState(false, juce::dontSendNotification);
        testFixtureBtn.setButtonText("Test Faro");
        dmxLiveEngine->stopFixtureTest();
        return;
    }

    if (!dmxLiveEngine->isOutputActive())
    {
        testFixtureBtn.setToggleState(false, juce::dontSendNotification);
        testFixtureBtn.setButtonText("Test Faro");
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "DMX Non Attivo",
            "Vai in Tab 6 (DMX Live), seleziona la porta COM dell'Eurolite e premi 'Avvia Uscita DMX' prima di testare il faro.");
        return;
    }

    testFixtureBtn.setButtonText("STOP Test");
    dmxLiveEngine->startFixtureTest(venueNode.getChild(sel));
}