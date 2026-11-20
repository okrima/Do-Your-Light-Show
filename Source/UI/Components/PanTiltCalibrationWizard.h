#pragma once
#include <JuceHeader.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/StageRoleCatalog.h"
#include <array>
#include <cmath>
#include <functional>
#include <vector>

class PanTiltCalibrationWizard : public juce::Component,
    private juce::Timer
{
public:
    using DmxUniverse = std::array<uint8_t, 512>;

    // Hook pronto per lo Step 3 (Driver ENTTEC Pro / Open DMX / Art-Net)
    std::function<void(const DmxUniverse&)> onDmxFrameOut;
    std::function<void()> onCalibrationSaved;

    explicit PanTiltCalibrationWizard(juce::ValueTree fixtureNode)
        : fixture(fixtureNode)
    {
        setSize(640, 520);
        setWantsKeyboardFocus(true);

        loadHardwareInfo();
        initStepOrder();
        setupUI();
        enterStepIndex(0);

        startTimerHz(30);
    }

    ~PanTiltCalibrationWizard() override
    {
        stopTimer();
        // Blackout pulito del faro alla chiusura della finestra
        if (onDmxFrameOut)
        {
            DmxUniverse blackout{};
            blackout.fill(0);
            onDmxFrameOut(blackout);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11121a));

        auto area = getLocalBounds().reduced(14);
        drawStepperHeader(g, area.removeFromTop(34));

        area.removeFromTop(8);
        auto instrBox = area.removeFromTop(48);
        g.setColour(juce::Colour(0xff191b27));
        g.fillRoundedRectangle(instrBox.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff3b3f58));
        g.drawRoundedRectangle(instrBox.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colour(0xffe2e8f0));
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.drawText(getStepTitle(getCurrentStep()),
            instrBox.reduced(10, 4).removeFromTop(18),
            juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(juce::FontOptions(11.5f));
        g.drawText(getStepInstruction(getCurrentStep()),
            instrBox.reduced(10, 4).withTrimmedTop(20),
            juce::Justification::centredLeft, true);

        // Separatore prima della barra DMX
        g.setColour(juce::Colour(0xff262938));
        g.drawHorizontalLine(getHeight() - 86, 14.0f, static_cast<float>(getWidth() - 14));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        area.removeFromTop(34 + 8 + 48 + 10); // Stepper + istruzioni

        // Footer pulsanti
        auto footer = area.removeFromBottom(32);
        cancelBtn.setBounds(footer.removeFromLeft(96));
        nextFinishBtn.setBounds(footer.removeFromRight(155));
        footer.removeFromRight(8);
        backBtn.setBounds(footer.removeFromRight(105));

        area.removeFromBottom(8);

        // Barra Monitor DMX Live
        auto dmxBar = area.removeFromBottom(32);
        dmxMonitorLabel.setBounds(dmxBar);

        area.removeFromBottom(10);

        // Corpo centrale: Pad Motori a sinistra, Controlli a destra
        auto leftCol = area.removeFromLeft(248);
        motorPad.setBounds(leftCol.withHeight(juce::jmin(248, leftCol.getHeight())));

        area.removeFromLeft(14);
        auto rightCol = area;

        // -- Pannello Controlli Motori (Steps 1..4) --
        auto ctrl = rightCol;
        auto panTitleRow = ctrl.removeFromTop(20);
        panChLabel.setBounds(panTitleRow);
        panSlider.setBounds(ctrl.removeFromTop(26));
        auto panNudgeRow = ctrl.removeFromTop(24);
        layoutNudgeRow(panNudgeRow, panM10Btn, panM1Btn, panP1Btn, panP10Btn);

        ctrl.removeFromTop(10);

        auto tiltTitleRow = ctrl.removeFromTop(20);
        tiltChLabel.setBounds(tiltTitleRow);
        tiltSlider.setBounds(ctrl.removeFromTop(26));
        auto tiltNudgeRow = ctrl.removeFromTop(24);
        layoutNudgeRow(tiltNudgeRow, tiltM10Btn, tiltM1Btn, tiltP1Btn, tiltP10Btn);

        ctrl.removeFromTop(10);
        auto quickRow = ctrl.removeFromTop(24);
        centerMotorsBtn.setBounds(quickRow.removeFromLeft(quickRow.getWidth() / 2 - 4));
        quickRow.removeFromLeft(8);
        resetStepBtn.setBounds(quickRow);

        // -- Pannello Verifica Finale (Step 5, stessa area dei controlli motori) --
        auto ver = rightCol;
        verifySummaryLabel.setBounds(ver.removeFromTop(96));
        ver.removeFromTop(6);
        totemToggle.setBounds(ver.removeFromTop(24));
        totemHintLabel.setBounds(ver.removeFromTop(18));
        ver.removeFromTop(10);
        auto testBtnRow = ver.removeFromTop(28);
        testPerimeterBtn.setBounds(testBtnRow.removeFromLeft(testBtnRow.getWidth() / 2 - 4));
        testBtnRow.removeFromLeft(8);
        goCenterBtn.setBounds(testBtnRow);

        // -- Box Luce Taratura (Locate) fisso nella parte bassa della colonna destra --
        auto locateBox = rightCol.removeFromBottom(92);
        locateToggle.setBounds(locateBox.removeFromTop(24));
        locateBox.removeFromTop(4);
        auto dimRow = locateBox.removeFromTop(26);
        locateDimLabel.setBounds(dimRow.removeFromLeft(95));
        locateDimSlider.setBounds(dimRow);
        locateBox.removeFromTop(4);
        auto shtRow = locateBox.removeFromTop(26);
        locateShutterLabel.setBounds(shtRow.removeFromLeft(95));
        locateShutterSlider.setBounds(shtRow);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (getCurrentStep() == Step::Verify)
        {
            if (key == juce::KeyPress::returnKey)
            {
                saveAndClose();
                return true;
            }
            return false;
        }

        const int delta = key.getModifiers().isShiftDown() ? 1 : 5;
        if (key == juce::KeyPress::leftKey && baseSpec.hasPanChannel)
        {
            setRawPanTilt(curPanRaw - delta, curTiltRaw);
            return true;
        }
        if (key == juce::KeyPress::rightKey && baseSpec.hasPanChannel)
        {
            setRawPanTilt(curPanRaw + delta, curTiltRaw);
            return true;
        }
        if (key == juce::KeyPress::upKey && baseSpec.hasTiltChannel)
        {
            setRawPanTilt(curPanRaw, curTiltRaw - delta);
            return true;
        }
        if (key == juce::KeyPress::downKey && baseSpec.hasTiltChannel)
        {
            setRawPanTilt(curPanRaw, curTiltRaw + delta);
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            goNextStep();
            return true;
        }
        return false;
    }

private:
    enum class Step { PanLeft = 0, PanRight = 1, TiltLow = 2, TiltHigh = 3, Verify = 4 };

    // -------------------------------------------------------------------------
    // Pad XY Grezzo Motori (0..255) con marker dei punti gia' tarati
    // -------------------------------------------------------------------------
    class RawMotorPad : public juce::Component
    {
    public:
        std::function<void(int panRaw, int tiltRaw)> onDragPosition;

        void setState(int curP, int curT, bool hasP, bool hasT, bool verifyMode,
            const std::vector<std::pair<juce::String, juce::Point<int>>>& markers,
            const StageRoleCatalog::PanTiltHardwareSpec& previewSpec)
        {
            panVal = curP;
            tiltVal = curT;
            hasPan = hasP;
            hasTilt = hasT;
            inVerify = verifyMode;
            savedMarkers = markers;
            spec = previewSpec;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            g.setColour(juce::Colour(0xff0b0c12));
            g.fillRoundedRectangle(r, 6.0f);

            // Griglia 4x4
            g.setColour(juce::Colour(0xff1d2030));
            for (int i = 1; i < 4; ++i)
            {
                float fx = r.getX() + (r.getWidth() * static_cast<float>(i)) / 4.0f;
                float fy = r.getY() + (r.getHeight() * static_cast<float>(i)) / 4.0f;
                g.drawLine(fx, r.getY() + 2.0f, fx, r.getBottom() - 2.0f, (i == 2) ? 1.4f : 0.8f);
                g.drawLine(r.getX() + 2.0f, fy, r.getRight() - 2.0f, fy, (i == 2) ? 1.4f : 0.8f);
            }

            // Etichette assi DMX grezzi
            g.setColour(juce::Colour(0xff64748b));
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText("Tilt Ch 0", r.reduced(6.0f, 3.0f), juce::Justification::centredTop, false);
            g.drawText("Tilt Ch 255", r.reduced(6.0f, 3.0f), juce::Justification::centredBottom, false);
            g.drawText("Pan 0", r.reduced(4.0f, 0.0f), juce::Justification::centredLeft, false);
            g.drawText("Pan 255", r.reduced(4.0f, 0.0f), juce::Justification::centredRight, false);

            auto toPt = [&](int p, int t) -> juce::Point<float>
                {
                    float x = hasPan ? (r.getX() + (static_cast<float>(p) / 255.0f) * r.getWidth()) : r.getCentreX();
                    float y = hasTilt ? (r.getY() + (static_cast<float>(t) / 255.0f) * r.getHeight()) : r.getCentreY();
                    return { x, y };
                };

            // In Step Verifica disegna il rettangolo dell'area tarata
            if (inVerify)
            {
                int pA = spec.isSwapped() ? spec.tiltLow : spec.panLeft;
                int pB = spec.isSwapped() ? spec.tiltHigh : spec.panRight;
                int tA = spec.isSwapped() ? spec.panLeft : spec.tiltLow;
                int tB = spec.isSwapped() ? spec.panRight : spec.tiltHigh;

                auto pt1 = toPt(juce::jmin(pA, pB), juce::jmin(tA, tB));
                auto pt2 = toPt(juce::jmax(pA, pB), juce::jmax(tA, tB));
                juce::Rectangle<float> box(pt1.x, pt1.y,
                    juce::jmax(6.0f, pt2.x - pt1.x), juce::jmax(6.0f, pt2.y - pt1.y));

                g.setColour(juce::Colour(0xff22c55e).withAlpha(0.16f));
                g.fillRoundedRectangle(box, 3.0f);
                g.setColour(juce::Colour(0xff4ade80));
                g.drawRoundedRectangle(box, 3.0f, 1.5f);
            }

            // Marker dei punti gia' catturati
            for (const auto& m : savedMarkers)
            {
                auto p = toPt(m.second.x, m.second.y);
                g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.85f));
                g.fillEllipse(p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(m.first, static_cast<int>(p.x + 6.0f), static_cast<int>(p.y - 7.0f),
                    36, 14, juce::Justification::centredLeft, false);
            }

            // Cursore live corrente
            auto cur = toPt(panVal, tiltVal);
            g.setColour(juce::Colour(0xfffacc15).withAlpha(0.35f));
            g.fillEllipse(cur.x - 10.0f, cur.y - 10.0f, 20.0f, 20.0f);
            g.setColour(juce::Colour(0xfffacc15));
            g.drawLine(cur.x - 8.0f, cur.y, cur.x + 8.0f, cur.y, 1.5f);
            g.drawLine(cur.x, cur.y - 8.0f, cur.x, cur.y + 8.0f, 1.5f);
            g.fillEllipse(cur.x - 3.0f, cur.y - 3.0f, 6.0f, 6.0f);

            g.setColour(juce::Colour(0xff334155));
            g.drawRoundedRectangle(r, 6.0f, 1.3f);
        }

        void mouseDown(const juce::MouseEvent& e) override { handlePointer(e.position); }
        void mouseDrag(const juce::MouseEvent& e) override { handlePointer(e.position); }

    private:
        void handlePointer(juce::Point<float> pt)
        {
            if (inVerify) return;
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            if (r.getWidth() <= 1.0f || r.getHeight() <= 1.0f) return;

            int newP = hasPan
                ? juce::jlimit(0, 255, juce::roundToInt(((pt.x - r.getX()) / r.getWidth()) * 255.0f))
                : panVal;
            int newT = hasTilt
                ? juce::jlimit(0, 255, juce::roundToInt(((pt.y - r.getY()) / r.getHeight()) * 255.0f))
                : tiltVal;

            if (onDragPosition)
                onDragPosition(newP, newT);
        }

        int panVal = 128;
        int tiltVal = 128;
        bool hasPan = true;
        bool hasTilt = true;
        bool inVerify = false;
        std::vector<std::pair<juce::String, juce::Point<int>>> savedMarkers;
        StageRoleCatalog::PanTiltHardwareSpec spec;
    };

    // -------------------------------------------------------------------------
    // Caricamento configurazione hardware del faro
    // -------------------------------------------------------------------------
    void loadHardwareInfo()
    {
        fixtureName = fixture.getProperty(StateKeys::FixtureName, "Faro").toString();
        startAddr = juce::jlimit(1, 512, static_cast<int>(fixture.getProperty(StateKeys::StartAddress, 1)));

        panCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Pan"), 0));
        tiltCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Tilt"), 0));
        dimmerCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Dimmer"), 0));
        redCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Red"), 0));
        greenCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Green"), 0));
        blueCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Blue"), 0));
        whiteCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("White"), 0));
        strobeCh = static_cast<int>(fixture.getProperty(StateKeys::getMappingKey("Strobe"), 0));

        baseSpec = StageRoleCatalog::PanTiltHardwareSpec::fromFixtureNode(fixture);
        strobeSpec = StageRoleCatalog::StrobeHardwareSpec::fromFixtureNode(fixture);
        totemSwap = baseSpec.swapPanTilt;

        // Pre-popola i 4 punti raw partendo dai limiti attuali del faro
        if (baseSpec.isSwapped())
        {
            sxRawPan = baseSpec.getTiltCenter();
            sxRawTilt = baseSpec.panLeft;
            dxRawPan = baseSpec.getTiltCenter();
            dxRawTilt = baseSpec.panRight;
            lowRawPan = baseSpec.tiltLow;
            lowRawTilt = baseSpec.getPanCenter();
            highRawPan = baseSpec.tiltHigh;
            highRawTilt = baseSpec.getPanCenter();
        }
        else
        {
            sxRawPan = baseSpec.panLeft;
            sxRawTilt = baseSpec.getTiltCenter();
            dxRawPan = baseSpec.panRight;
            dxRawTilt = baseSpec.getTiltCenter();
            lowRawPan = baseSpec.getPanCenter();
            lowRawTilt = baseSpec.tiltLow;
            highRawPan = baseSpec.getPanCenter();
            highRawTilt = baseSpec.tiltHigh;
        }
    }

    void initStepOrder()
    {
        stepOrder.clear();
        if (baseSpec.hasPanChannel)
        {
            stepOrder.push_back(Step::PanLeft);
            stepOrder.push_back(Step::PanRight);
        }
        if (baseSpec.hasTiltChannel)
        {
            stepOrder.push_back(Step::TiltLow);
            stepOrder.push_back(Step::TiltHigh);
        }
        stepOrder.push_back(Step::Verify);
    }

    Step getCurrentStep() const noexcept
    {
        if (stepOrder.empty()) return Step::Verify;
        return stepOrder[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(stepOrder.size()) - 1, currentStepIdx))];
    }

    static juce::String getStepShortLabel(Step s)
    {
        switch (s)
        {
        case Step::PanLeft:  return "1. SX";
        case Step::PanRight: return "2. DX";
        case Step::TiltLow:  return "3. Giu";
        case Step::TiltHigh: return "4. Su";
        case Step::Verify:   return "5. Verifica";
        }
        return "";
    }

    static juce::String getStepTitle(Step s)
    {
        switch (s)
        {
        case Step::PanLeft:  return "Step 1: Bordo Sinistro del Palco (SX)";
        case Step::PanRight: return "Step 2: Bordo Destro del Palco (DX)";
        case Step::TiltLow:  return "Step 3: Limite Basso / Band (Giu)";
        case Step::TiltHigh: return "Step 4: Limite Alto / Pubblico (Su)";
        case Step::Verify:   return "Step 5: Test Gabbia & Salvataggio";
        }
        return "";
    }

    static juce::String getStepInstruction(Step s)
    {
        switch (s)
        {
        case Step::PanLeft:
            return "Porta il fascio sul bordo SINISTRO del palco. Usa Pad, slider o Frecce (Shift+Frecce = +/-1).";
        case Step::PanRight:
            return "Ora porta il fascio sul bordo DESTRO del palco. L'app rileva in automatico inversioni e montaggio Totem.";
        case Step::TiltLow:
            return "Abbassa il fascio sul punto INFERIORE sicuro (es. musicisti / bordo palco).";
        case Step::TiltHigh:
            return "Alza il fascio fino al punto SUPERIORE voluto (es. pubblico / aria, senza colpire le americane).";
        case Step::Verify:
            return "Avvia il Test Perimetro per verificare che il faro resti nei limiti sicuri, poi salva nel Venue.";
        }
        return "";
    }

    void setupUI()
    {
        motorPad.onDragPosition = [this](int p, int t) { setRawPanTilt(p, t); };
        addAndMakeVisible(motorPad);

        auto setupAxisLabel = [this](juce::Label& l, const juce::String& txt)
            {
                l.setText(txt, juce::dontSendNotification);
                l.setFont(juce::FontOptions(12.0f, juce::Font::bold));
                l.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
                addAndMakeVisible(l);
            };

        setupAxisLabel(panChLabel, panCh > 0 ? ("Canale Pan (DMX Ch " + juce::String(startAddr + panCh - 1) + ")") : "Canale Pan (non mappato)");
        setupAxisLabel(tiltChLabel, tiltCh > 0 ? ("Canale Tilt (DMX Ch " + juce::String(startAddr + tiltCh - 1) + ")") : "Canale Tilt (non mappato)");

        auto setupMotorSlider = [this](juce::Slider& s, bool isPan)
            {
                s.setSliderStyle(juce::Slider::LinearHorizontal);
                s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 22);
                s.setRange(0.0, 255.0, 1.0);
                s.onValueChange = [this, isPan]() {
                    if (isUpdatingControls) return;
                    if (isPan) setRawPanTilt(static_cast<int>(panSlider.getValue()), curTiltRaw);
                    else       setRawPanTilt(curPanRaw, static_cast<int>(tiltSlider.getValue()));
                    };
                addAndMakeVisible(s);
            };
        setupMotorSlider(panSlider, true);
        setupMotorSlider(tiltSlider, false);

        auto setupNudge = [this](juce::TextButton& b, const juce::String& txt, bool isPan, int delta)
            {
                b.setButtonText(txt);
                b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff232736));
                b.onClick = [this, isPan, delta]() {
                    if (isPan) setRawPanTilt(curPanRaw + delta, curTiltRaw);
                    else       setRawPanTilt(curPanRaw, curTiltRaw + delta);
                    };
                addAndMakeVisible(b);
            };
        setupNudge(panM10Btn, "-10", true, -10);
        setupNudge(panM1Btn, "-1", true, -1);
        setupNudge(panP1Btn, "+1", true, 1);
        setupNudge(panP10Btn, "+10", true, 10);
        setupNudge(tiltM10Btn, "-10", false, -10);
        setupNudge(tiltM1Btn, "-1", false, -1);
        setupNudge(tiltP1Btn, "+1", false, 1);
        setupNudge(tiltP10Btn, "+10", false, 10);

        centerMotorsBtn.setButtonText("Centra (128)");
        centerMotorsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2640));
        centerMotorsBtn.onClick = [this]() {
            setRawPanTilt(baseSpec.hasPanChannel ? 128 : curPanRaw,
                baseSpec.hasTiltChannel ? 128 : curTiltRaw);
            };
        addAndMakeVisible(centerMotorsBtn);

        resetStepBtn.setButtonText("Ripristina Salvato");
        resetStepBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        resetStepBtn.onClick = [this]() { loadStepStoredPosition(getCurrentStep()); };
        addAndMakeVisible(resetStepBtn);

        // -- Controlli Verifica (Step 5) --
        verifySummaryLabel.setFont(juce::FontOptions(12.0f));
        verifySummaryLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe2e8f0));
        verifySummaryLabel.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(verifySummaryLabel);

        totemToggle.setButtonText("Totem / Montaggio Laterale (Scambia Pan <-> Tilt)");
        totemToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        totemToggle.setEnabled(baseSpec.hasPanChannel && baseSpec.hasTiltChannel);
        totemToggle.onClick = [this]() {
            totemSwap = totemToggle.getToggleState();
            totemAutoDetected = false;
            refreshVerifySummary();
            updateMotorPadAndDmx();
            };
        addAndMakeVisible(totemToggle);

        totemHintLabel.setFont(juce::FontOptions(11.0f));
        totemHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
        addAndMakeVisible(totemHintLabel);

        testPerimeterBtn.setButtonText("Avvia Test Perimetro");
        testPerimeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d4ed8));
        testPerimeterBtn.onClick = [this]() {
            isRunningPerimeterTest = !isRunningPerimeterTest;
            perimeterPhase = 0.0f;
            testPerimeterBtn.setButtonText(isRunningPerimeterTest ? "Ferma Test Perimetro" : "Avvia Test Perimetro");
            testPerimeterBtn.setColour(juce::TextButton::buttonColourId,
                isRunningPerimeterTest ? juce::Colour(0xffb45309) : juce::Colour(0xff1d4ed8));
            if (!isRunningPerimeterTest)
                moveToCalibratedNormalized(0.5f, 0.5f);
            };
        addAndMakeVisible(testPerimeterBtn);

        goCenterBtn.setButtonText("Vai al Centro");
        goCenterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        goCenterBtn.onClick = [this]() {
            isRunningPerimeterTest = false;
            testPerimeterBtn.setButtonText("Avvia Test Perimetro");
            testPerimeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d4ed8));
            moveToCalibratedNormalized(0.5f, 0.5f);
            };
        addAndMakeVisible(goCenterBtn);

        // -- Box Locate (Accensione sicura del faro durante la taratura) --
        locateToggle.setButtonText("Luce Taratura ON (Locate bianco)");
        locateToggle.setToggleState(true, juce::dontSendNotification);
        locateToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfffef08a));
        locateToggle.onClick = [this]() { emitCurrentDmxFrame(); };
        addAndMakeVisible(locateToggle);

        locateDimLabel.setText("Locate Dimmer:", juce::dontSendNotification);
        locateDimLabel.setFont(juce::FontOptions(11.5f));
        locateDimLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(locateDimLabel);

        locateDimSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        locateDimSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
        locateDimSlider.setRange(0.0, 255.0, 1.0);
        // Se il canale dimmer e' condiviso con lo strobo (mode 1), parte a 128 per restare fuori fascia strobo
        locateDimSlider.setValue(strobeSpec.mode == 1 ? 128.0 : 255.0, juce::dontSendNotification);
        locateDimSlider.setEnabled(dimmerCh > 0);
        locateDimSlider.onValueChange = [this]() { emitCurrentDmxFrame(); };
        addAndMakeVisible(locateDimSlider);

        locateShutterLabel.setText("Shutter Open:", juce::dontSendNotification);
        locateShutterLabel.setFont(juce::FontOptions(11.5f));
        locateShutterLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(locateShutterLabel);

        locateShutterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        locateShutterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
        locateShutterSlider.setRange(0.0, 255.0, 1.0);
        locateShutterSlider.setValue(255.0, juce::dontSendNotification);
        const bool hasDedicatedShutter = (strobeCh > 0 && strobeSpec.mode == 2);
        locateShutterLabel.setEnabled(hasDedicatedShutter);
        locateShutterSlider.setEnabled(hasDedicatedShutter);
        locateShutterSlider.onValueChange = [this]() { emitCurrentDmxFrame(); };
        addAndMakeVisible(locateShutterSlider);

        // -- Monitor DMX --
        dmxMonitorLabel.setFont(juce::FontOptions(11.0f));
        dmxMonitorLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0b0c12));
        dmxMonitorLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff262938));
        dmxMonitorLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
        dmxMonitorLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dmxMonitorLabel);

        // -- Footer --
        cancelBtn.setButtonText("Annulla");
        cancelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272736));
        cancelBtn.onClick = [this]() { closeDialog(); };
        addAndMakeVisible(cancelBtn);

        backBtn.setButtonText("<- Indietro");
        backBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a38));
        backBtn.onClick = [this]() { goPreviousStep(); };
        addAndMakeVisible(backBtn);

        nextFinishBtn.setButtonText("Avanti ->");
        nextFinishBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7c3aed));
        nextFinishBtn.onClick = [this]() {
            if (getCurrentStep() == Step::Verify) saveAndClose();
            else                                  goNextStep();
            };
        addAndMakeVisible(nextFinishBtn);
    }

    static void layoutNudgeRow(juce::Rectangle<int> row,
        juce::TextButton& b1, juce::TextButton& b2,
        juce::TextButton& b3, juce::TextButton& b4)
    {
        int w = row.getWidth() / 4;
        b1.setBounds(row.removeFromLeft(w).reduced(1, 0));
        b2.setBounds(row.removeFromLeft(w).reduced(1, 0));
        b3.setBounds(row.removeFromLeft(w).reduced(1, 0));
        b4.setBounds(row.reduced(1, 0));
    }

    void drawStepperHeader(juce::Graphics& g, juce::Rectangle<int> row) const
    {
        const int count = static_cast<int>(stepOrder.size());
        if (count <= 0) return;

        const int pillW = row.getWidth() / count;
        for (int i = 0; i < count; ++i)
        {
            auto pill = row.removeFromLeft(i == count - 1 ? row.getWidth() : pillW).reduced(3, 2).toFloat();
            const bool isCurrent = (i == currentStepIdx);
            const bool isDone = (i < currentStepIdx);

            juce::Colour bg = isCurrent ? juce::Colour(0xff5b21b6)
                : (isDone ? juce::Colour(0xff14532d)
                    : juce::Colour(0xff1c1e2b));
            juce::Colour border = isCurrent ? juce::Colour(0xffa78bfa)
                : (isDone ? juce::Colour(0xff22c55e)
                    : juce::Colour(0xff2d3142));

            g.setColour(bg);
            g.fillRoundedRectangle(pill, 5.0f);
            g.setColour(border);
            g.drawRoundedRectangle(pill, 5.0f, isCurrent ? 1.5f : 1.0f);

            g.setColour(isCurrent || isDone ? juce::Colours::white : juce::Colour(0xff94a3b8));
            g.setFont(juce::FontOptions(11.5f, isCurrent ? juce::Font::bold : juce::Font::plain));
            g.drawText(getStepShortLabel(stepOrder[static_cast<std::size_t>(i)]),
                pill, juce::Justification::centred, true);
        }
    }

    void storeCurrentStepValue()
    {
        switch (getCurrentStep())
        {
        case Step::PanLeft:  sxRawPan = curPanRaw;   sxRawTilt = curTiltRaw;   capturedSX = true;   break;
        case Step::PanRight: dxRawPan = curPanRaw;   dxRawTilt = curTiltRaw;   capturedDX = true;   break;
        case Step::TiltLow:  lowRawPan = curPanRaw;  lowRawTilt = curTiltRaw;  capturedLow = true;  break;
        case Step::TiltHigh: highRawPan = curPanRaw; highRawTilt = curTiltRaw; capturedHigh = true; break;
        case Step::Verify:   break;
        }
    }

    void loadStepStoredPosition(Step s)
    {
        switch (s)
        {
        case Step::PanLeft:  setRawPanTilt(sxRawPan, sxRawTilt);   break;
        case Step::PanRight: setRawPanTilt(dxRawPan, dxRawTilt);   break;
        case Step::TiltLow:  setRawPanTilt(lowRawPan, lowRawTilt);  break;
        case Step::TiltHigh: setRawPanTilt(highRawPan, highRawTilt); break;
        case Step::Verify:   moveToCalibratedNormalized(0.5f, 0.5f); break;
        }
    }

    void autoDetectTotemOrientation()
    {
        if (!baseSpec.hasPanChannel || !baseSpec.hasTiltChannel)
        {
            totemSwap = false;
            totemAutoDetected = false;
            return;
        }

        const int horizPanDelta = std::abs(dxRawPan - sxRawPan);
        const int horizTiltDelta = std::abs(dxRawTilt - sxRawTilt);
        const int vertPanDelta = std::abs(highRawPan - lowRawPan);
        const int vertTiltDelta = std::abs(highRawTilt - lowRawTilt);

        const int normalScore = horizPanDelta + vertTiltDelta;
        const int swappedScore = horizTiltDelta + vertPanDelta;

        if (swappedScore > normalScore + 8)
        {
            totemSwap = true;
            totemAutoDetected = true;
        }
        else if (normalScore > swappedScore + 8)
        {
            totemSwap = false;
            totemAutoDetected = false;
        }
    }

    StageRoleCatalog::PanTiltHardwareSpec buildResultSpec() const
    {
        StageRoleCatalog::PanTiltHardwareSpec res = baseSpec;
        res.swapPanTilt = totemSwap && res.hasPanChannel && res.hasTiltChannel;

        if (res.isSwapped())
        {
            // Montaggio su Totem: SX/DX mossi dal canale fisico Tilt, Giu/Su mossi dal canale fisico Pan
            res.panLeft = sxRawTilt;
            res.panRight = dxRawTilt;
            res.tiltLow = lowRawPan;
            res.tiltHigh = highRawPan;
        }
        else
        {
            res.panLeft = sxRawPan;
            res.panRight = dxRawPan;
            res.tiltLow = lowRawTilt;
            res.tiltHigh = highRawTilt;
        }
        return res;
    }

    void enterStepIndex(int idx)
    {
        currentStepIdx = juce::jlimit(0, static_cast<int>(stepOrder.size()) - 1, idx);
        const Step st = getCurrentStep();
        const bool isVerify = (st == Step::Verify);

        if (!isVerify)
        {
            isRunningPerimeterTest = false;
            testPerimeterBtn.setButtonText("Avvia Test Perimetro");
            testPerimeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d4ed8));
        }
        else
        {
            autoDetectTotemOrientation();
            totemToggle.setToggleState(totemSwap, juce::dontSendNotification);
            refreshVerifySummary();
        }

        // Visibilita' pannello motori vs pannello verifica
        panChLabel.setVisible(!isVerify);
        panSlider.setVisible(!isVerify);
        panM10Btn.setVisible(!isVerify); panM1Btn.setVisible(!isVerify);
        panP1Btn.setVisible(!isVerify);  panP10Btn.setVisible(!isVerify);

        tiltChLabel.setVisible(!isVerify);
        tiltSlider.setVisible(!isVerify);
        tiltM10Btn.setVisible(!isVerify); tiltM1Btn.setVisible(!isVerify);
        tiltP1Btn.setVisible(!isVerify);  tiltP10Btn.setVisible(!isVerify);

        centerMotorsBtn.setVisible(!isVerify);
        resetStepBtn.setVisible(!isVerify);

        panSlider.setEnabled(baseSpec.hasPanChannel);
        panM10Btn.setEnabled(baseSpec.hasPanChannel); panM1Btn.setEnabled(baseSpec.hasPanChannel);
        panP1Btn.setEnabled(baseSpec.hasPanChannel);  panP10Btn.setEnabled(baseSpec.hasPanChannel);

        tiltSlider.setEnabled(baseSpec.hasTiltChannel);
        tiltM10Btn.setEnabled(baseSpec.hasTiltChannel); tiltM1Btn.setEnabled(baseSpec.hasTiltChannel);
        tiltP1Btn.setEnabled(baseSpec.hasTiltChannel);  tiltP10Btn.setEnabled(baseSpec.hasTiltChannel);

        verifySummaryLabel.setVisible(isVerify);
        totemToggle.setVisible(isVerify);
        totemHintLabel.setVisible(isVerify);
        testPerimeterBtn.setVisible(isVerify);
        goCenterBtn.setVisible(isVerify);

        backBtn.setEnabled(currentStepIdx > 0);
        nextFinishBtn.setButtonText(isVerify ? "Salva nel Venue" : "Avanti ->");
        nextFinishBtn.setColour(juce::TextButton::buttonColourId,
            isVerify ? juce::Colour(0xff15803d) : juce::Colour(0xff7c3aed));

        loadStepStoredPosition(st);
        repaint();
    }

    void goNextStep()
    {
        storeCurrentStepValue();
        if (currentStepIdx + 1 < static_cast<int>(stepOrder.size()))
            enterStepIndex(currentStepIdx + 1);
    }

    void goPreviousStep()
    {
        if (getCurrentStep() != Step::Verify)
            storeCurrentStepValue();

        if (currentStepIdx > 0)
            enterStepIndex(currentStepIdx - 1);
    }

    void refreshVerifySummary()
    {
        auto s = buildResultSpec();
        juce::String txt;
        txt << "Faro: " << fixtureName << " (DMX Base " << startAddr << ")\n";

        if (s.hasPanChannel)
        {
            txt << "Pan Palco (SX -> DX): " << s.panLeft << " -> " << s.panRight
                << "  (Centro: " << s.getPanCenter() << ", Corsa: " << s.getPanSpan() << ")\n";
        }
        if (s.hasTiltChannel)
        {
            txt << "Tilt Palco (Giu -> Su): " << s.tiltLow << " -> " << s.tiltHigh
                << "  (Centro: " << s.getTiltCenter() << ", Corsa: " << s.getTiltSpan() << ")\n";
        }
        if (s.isSwapped())
            txt << "Modalita': TOTEM (assi Pan/Tilt scambiati in uscita DMX/MIDI)";
        else
            txt << "Modalita': Standard (Pan -> Pan, Tilt -> Tilt)";

        verifySummaryLabel.setText(txt, juce::dontSendNotification);
        totemHintLabel.setText(totemAutoDetected
            ? "Rilevato montaggio laterale (Totem) dallo spostamento SX -> DX."
            : "", juce::dontSendNotification);
    }

    void setRawPanTilt(int panRaw, int tiltRaw)
    {
        curPanRaw = juce::jlimit(0, 255, panRaw);
        curTiltRaw = juce::jlimit(0, 255, tiltRaw);

        isUpdatingControls = true;
        panSlider.setValue(curPanRaw, juce::dontSendNotification);
        tiltSlider.setValue(curTiltRaw, juce::dontSendNotification);
        isUpdatingControls = false;

        updateMotorPadAndDmx();
    }

    void moveToCalibratedNormalized(float uPan, float uTilt)
    {
        auto s = buildResultSpec();
        int stagePanDmx = s.mapStagePanToDmx(uPan, false);
        int stageTiltDmx = s.mapStageTiltToDmx(uTilt);

        if (s.isSwapped())
            setRawPanTilt(stageTiltDmx, stagePanDmx);
        else
            setRawPanTilt(stagePanDmx, stageTiltDmx);
    }

    void updateMotorPadAndDmx()
    {
        std::vector<std::pair<juce::String, juce::Point<int>>> markers;
        if (capturedSX)   markers.push_back({ "SX",{ sxRawPan,   sxRawTilt } });
        if (capturedDX)   markers.push_back({ "DX",{ dxRawPan,   dxRawTilt } });
        if (capturedLow)  markers.push_back({ "Giu",{ lowRawPan,  lowRawTilt } });
        if (capturedHigh) markers.push_back({ "Su",{ highRawPan, highRawTilt } });

        motorPad.setState(curPanRaw, curTiltRaw, baseSpec.hasPanChannel, baseSpec.hasTiltChannel,
            getCurrentStep() == Step::Verify, markers, buildResultSpec());
        emitCurrentDmxFrame();
    }

    void emitCurrentDmxFrame()
    {
        DmxUniverse uni{};
        uni.fill(0);

        auto writeRelCh = [&](int relOffset, int val)
            {
                if (relOffset <= 0) return;
                const int absCh = startAddr + relOffset - 1;
                if (absCh >= 1 && absCh <= 512)
                    uni[static_cast<std::size_t>(absCh - 1)] = static_cast<uint8_t>(juce::jlimit(0, 255, val));
            };

        if (baseSpec.hasPanChannel)  writeRelCh(panCh, curPanRaw);
        if (baseSpec.hasTiltChannel) writeRelCh(tiltCh, curTiltRaw);

        const bool locateOn = locateToggle.getToggleState();
        const int dimOut = locateOn ? static_cast<int>(locateDimSlider.getValue()) : 0;
        if (dimmerCh > 0) writeRelCh(dimmerCh, dimOut);

        if (strobeCh > 0 && strobeSpec.mode == 2)
            writeRelCh(strobeCh, locateOn ? static_cast<int>(locateShutterSlider.getValue()) : 0);

        if (locateOn)
        {
            if (whiteCh > 0)
            {
                writeRelCh(whiteCh, 255);
            }
            else
            {
                if (redCh > 0)   writeRelCh(redCh, 255);
                if (greenCh > 0) writeRelCh(greenCh, 255);
                if (blueCh > 0)  writeRelCh(blueCh, 255);
            }
        }

        if (onDmxFrameOut)
            onDmxFrameOut(uni);

        // Aggiorna la barra monitor DMX
        juce::String mon = onDmxFrameOut ? "  DMX OUT LIVE  |  " : "  DMX MONITOR (Driver in Step 3)  |  ";
        if (panCh > 0)    mon << "CH" << (startAddr + panCh - 1) << "(Pan):" << curPanRaw << "  ";
        if (tiltCh > 0)   mon << "CH" << (startAddr + tiltCh - 1) << "(Tilt):" << curTiltRaw << "  ";
        if (dimmerCh > 0) mon << "CH" << (startAddr + dimmerCh - 1) << "(Dim):" << dimOut << "  ";
        if (strobeCh > 0 && strobeSpec.mode == 2)
            mon << "CH" << (startAddr + strobeCh - 1) << "(Sht):" << (locateOn ? static_cast<int>(locateShutterSlider.getValue()) : 0);

        dmxMonitorLabel.setText(mon, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (getCurrentStep() == Step::Verify && isRunningPerimeterTest)
        {
            // Ciclo continuo 4 lati del rettangolo: SX-Giu -> DX-Giu -> DX-Su -> SX-Su
            perimeterPhase = std::fmod(perimeterPhase + (1.0f / 120.0f), 1.0f); // 4 secondi a giro
            float seg = perimeterPhase * 4.0f;
            float uPan = 0.5f, uTilt = 0.5f;

            if (seg < 1.0f) { uPan = seg;          uTilt = 0.0f; }
            else if (seg < 2.0f) { uPan = 1.0f;         uTilt = seg - 1.0f; }
            else if (seg < 3.0f) { uPan = 3.0f - seg;   uTilt = 1.0f; }
            else { uPan = 0.0f;         uTilt = 4.0f - seg; }

            moveToCalibratedNormalized(uPan, uTilt);
        }
        else if (onDmxFrameOut)
        {
            // Mantiene vivo il refresh DMX a 30 Hz quando il driver hardware e' connesso
            emitCurrentDmxFrame();
        }
    }

    void saveAndClose()
    {
        auto finalSpec = buildResultSpec();
        finalSpec.writeToFixtureNode(fixture);

        if (onCalibrationSaved)
            onCalibrationSaved();

        closeDialog();
    }

    void closeDialog()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    }

    juce::ValueTree fixture;
    juce::String fixtureName;
    int startAddr = 1;
    int panCh = 0, tiltCh = 0, dimmerCh = 0;
    int redCh = 0, greenCh = 0, blueCh = 0, whiteCh = 0, strobeCh = 0;

    StageRoleCatalog::PanTiltHardwareSpec baseSpec;
    StageRoleCatalog::StrobeHardwareSpec strobeSpec;

    std::vector<Step> stepOrder;
    int currentStepIdx = 0;
    bool isUpdatingControls = false;

    int curPanRaw = 128, curTiltRaw = 128;
    int sxRawPan = 85, sxRawTilt = 128;
    int dxRawPan = 170, dxRawTilt = 128;
    int lowRawPan = 128, lowRawTilt = 128;
    int highRawPan = 128, highRawTilt = 195;

    bool capturedSX = false, capturedDX = false, capturedLow = false, capturedHigh = false;
    bool totemSwap = false;
    bool totemAutoDetected = false;
    bool isRunningPerimeterTest = false;
    float perimeterPhase = 0.0f;

    // UI
    RawMotorPad motorPad;
    juce::Label panChLabel, tiltChLabel;
    juce::Slider panSlider, tiltSlider;
    juce::TextButton panM10Btn, panM1Btn, panP1Btn, panP10Btn;
    juce::TextButton tiltM10Btn, tiltM1Btn, tiltP1Btn, tiltP10Btn;
    juce::TextButton centerMotorsBtn, resetStepBtn;

    juce::Label verifySummaryLabel, totemHintLabel;
    juce::ToggleButton totemToggle;
    juce::TextButton testPerimeterBtn, goCenterBtn;

    juce::ToggleButton locateToggle;
    juce::Label locateDimLabel, locateShutterLabel;
    juce::Slider locateDimSlider, locateShutterSlider;

    juce::Label dmxMonitorLabel;
    juce::TextButton cancelBtn, backBtn, nextFinishBtn;
};