#pragma once
#include <JuceHeader.h>
#include "../../Model/BeatTimeMap.h"
#include "../../Model/DataStateKeys.h"
#include <cmath>
#include <vector>

class TimeMetricManagerComponent : public juce::Component,
    private juce::ValueTree::Listener
{
public:
    TimeMetricManagerComponent(juce::ValueTree& state, BeatTimeMap& map)
        : appState(state), beatTimeMap(map), tempoModel(*this), meterModel(*this)
    {
        setSize(860, 520);
        appState.addListener(this);

        // --- Barra superiore: Import MIDI + Downbeat Offset + Reset ---
        titleLabel.setText("Time & Metric Manager (Tempo Map BPM & Cambi Metrica)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe2e8f0));
        addAndMakeVisible(titleLabel);

        importMidiBtn.setButtonText("Importa MIDI Map...");
        importMidiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4c1d95));
        importMidiBtn.onClick = [this]() { promptImportMidi(); };
        addAndMakeVisible(importMidiBtn);

        downbeatLabel.setText("Downbeat Offset (s):", juce::dontSendNotification);
        downbeatLabel.setFont(juce::FontOptions(12.0f));
        downbeatLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(downbeatLabel);

        downbeatSlider.setSliderStyle(juce::Slider::LinearBar);
        downbeatSlider.setRange(0.0, 60.0, 0.001);
        downbeatSlider.setTextValueSuffix(" s");
        downbeatSlider.onValueChange = [this]() {
            if (isSyncingUI) return;
            beatTimeMap.setFirstDownbeatSeconds(downbeatSlider.getValue());
            refreshTables();
            };
        addAndMakeVisible(downbeatSlider);

        resetTempoBtn.setButtonText("Reset BPM Fisso");
        resetTempoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3f2626));
        resetTempoBtn.onClick = [this]() {
            beatTimeMap.setFixedBpm(beatTimeMap.getBaseBpm(), beatTimeMap.getFirstDownbeatSeconds());
            refreshTables();
            };
        addAndMakeVisible(resetTempoBtn);

        resetMeterBtn.setButtonText("Reset Metrica Fissa");
        resetMeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3f2626));
        resetMeterBtn.onClick = [this]() {
            beatTimeMap.setFixedMeter(beatTimeMap.getBaseTimeSigNum(), beatTimeMap.getBaseTimeSigDen());
            refreshTables();
            };
        addAndMakeVisible(resetMeterBtn);

        // --- Colonna Sinistra: Tabella Tempo Map (BPM) ---
        tempoHeaderLabel.setText("Tempo Map (Marker BPM)", juce::dontSendNotification);
        tempoHeaderLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
        tempoHeaderLabel.setColour(juce::Label::textColourId, juce::Colour(0xfffbbf24));
        addAndMakeVisible(tempoHeaderLabel);

        tempoTable.setModel(&tempoModel);
        tempoTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff14151d));
        tempoTable.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2d3142));
        tempoTable.setOutlineThickness(1);
        tempoTable.setRowHeight(24);
        tempoTable.getHeader().addColumn("#", 1, 34, 28, 50);
        tempoTable.getHeader().addColumn("Battuta", 2, 95, 70, 140);
        tempoTable.getHeader().addColumn("Quarto (Q)", 3, 86, 65, 130);
        tempoTable.getHeader().addColumn("Tempo", 4, 92, 70, 130);
        tempoTable.getHeader().addColumn("BPM", 5, 75, 60, 110);
        addAndMakeVisible(tempoTable);

        tempoBarLabel.setText("Battuta (M):", juce::dontSendNotification);
        tempoBarLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(tempoBarLabel);

        tempoBarEditor.setSliderStyle(juce::Slider::IncDecButtons);
        tempoBarEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 24);
        tempoBarEditor.setRange(1.0, 999.0, 1.0);
        tempoBarEditor.setNumDecimalPlacesToDisplay(0);
        tempoBarEditor.setValue(1.0, juce::dontSendNotification);
        tempoBarEditor.onValueChange = [this]() {
            if (isSyncingEditors) return;
            juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
            const double q = beatTimeMap.barToBeat(static_cast<int>(tempoBarEditor.getValue()), 0.0);
            tempoBeatEditor.setValue(q, juce::dontSendNotification);
            };
        addAndMakeVisible(tempoBarEditor);

        tempoBeatLabel.setText("Q:", juce::dontSendNotification);
        tempoBeatLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(tempoBeatLabel);

        tempoBeatEditor.setSliderStyle(juce::Slider::IncDecButtons);
        tempoBeatEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 72, 24);
        tempoBeatEditor.setRange(1.0, 4096.0, 0.03125); // precisione fino a 1/128
        tempoBeatEditor.setValue(1.0, juce::dontSendNotification);
        tempoBeatEditor.onValueChange = [this]() {
            if (isSyncingEditors) return;
            juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
            const auto pos = beatTimeMap.beatToMusicalPosition(tempoBeatEditor.getValue());
            tempoBarEditor.setValue(pos.barIndex, juce::dontSendNotification);
            };
        addAndMakeVisible(tempoBeatEditor);

        tempoBpmLabel.setText("BPM:", juce::dontSendNotification);
        tempoBpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(tempoBpmLabel);

        tempoBpmEditor.setSliderStyle(juce::Slider::IncDecButtons);
        tempoBpmEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 24);
        tempoBpmEditor.setRange(20.0, 300.0, 0.1);
        tempoBpmEditor.setValue(120.0, juce::dontSendNotification);
        addAndMakeVisible(tempoBpmEditor);

        addTempoBtn.setButtonText("Aggiungi");
        addTempoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff15803d));
        addTempoBtn.onClick = [this]() {
            const double targetQ = tempoBeatEditor.getValue();
            beatTimeMap.addOrUpdateTempoChange(targetQ, tempoBpmEditor.getValue());
            refreshTables();
            selectTempoRowNearBeat(targetQ);
            };
        addAndMakeVisible(addTempoBtn);

        applyTempoBtn.setButtonText("Aggiorna Sel.");
        applyTempoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d4ed8));
        applyTempoBtn.onClick = [this]() {
            const int row = tempoTable.getSelectedRow();
            if (row >= 0)
            {
                const double targetQ = (row == 0) ? 1.0 : tempoBeatEditor.getValue();
                beatTimeMap.updateTempoAnchor(row, targetQ, tempoBpmEditor.getValue());
                refreshTables();
                selectTempoRowNearBeat(targetQ);
            }
            };
        addAndMakeVisible(applyTempoBtn);

        delTempoBtn.setButtonText("Elimina");
        delTempoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff991b1b));
        delTempoBtn.onClick = [this]() {
            const int row = tempoTable.getSelectedRow();
            if (row > 0)
            {
                beatTimeMap.removeTempoAnchor(row);
                refreshTables();
            }
            };
        addAndMakeVisible(delTempoBtn);

        // --- Colonna Destra: Tabella Cambi Metrica ---
        meterHeaderLabel.setText("Cambi Metrica (Time Signatures)", juce::dontSendNotification);
        meterHeaderLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
        meterHeaderLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc4b5fd));
        addAndMakeVisible(meterHeaderLabel);

        meterTable.setModel(&meterModel);
        meterTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff14151d));
        meterTable.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff2d3142));
        meterTable.setOutlineThickness(1);
        meterTable.setRowHeight(24);
        meterTable.getHeader().addColumn("#", 1, 34, 28, 50);
        meterTable.getHeader().addColumn("Battuta", 2, 74, 55, 110);
        meterTable.getHeader().addColumn("Quarto (Q)", 3, 92, 70, 140);
        meterTable.getHeader().addColumn("Metrica", 4, 78, 60, 110);
        meterTable.getHeader().addColumn("Durata Bar", 5, 92, 70, 130);
        addAndMakeVisible(meterTable);

        meterBarLabel.setText("Battuta (M):", juce::dontSendNotification);
        meterBarLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(meterBarLabel);

        meterBarEditor.setSliderStyle(juce::Slider::IncDecButtons);
        meterBarEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 24);
        meterBarEditor.setRange(1.0, 999.0, 1.0);
        meterBarEditor.setNumDecimalPlacesToDisplay(0);
        meterBarEditor.setValue(1.0, juce::dontSendNotification);
        meterBarEditor.onValueChange = [this]() {
            if (isSyncingEditors) return;
            juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
            meterEditedByBar = true;
            const int selRow = meterTable.getSelectedRow();
            const int excludeIdx = (selRow > 0) ? selRow : -1;
            const double q = beatTimeMap.barToBeat(static_cast<int>(meterBarEditor.getValue()), 0.0, excludeIdx);
            meterBeatEditor.setValue(q, juce::dontSendNotification);
            };
        addAndMakeVisible(meterBarEditor);

        meterBeatLabel.setText("Q:", juce::dontSendNotification);
        meterBeatLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(meterBeatLabel);

        meterBeatEditor.setSliderStyle(juce::Slider::IncDecButtons);
        meterBeatEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 72, 24);
        meterBeatEditor.setRange(1.0, 4096.0, 0.03125); // precisione fino a 1/128
        meterBeatEditor.setValue(1.0, juce::dontSendNotification);
        meterBeatEditor.onValueChange = [this]() {
            if (isSyncingEditors) return;
            juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
            meterEditedByBar = false;
            const auto pos = beatTimeMap.beatToMusicalPosition(meterBeatEditor.getValue());
            meterBarEditor.setValue(pos.barIndex, juce::dontSendNotification);
            };
        addAndMakeVisible(meterBeatEditor);

        meterNumLabel.setText("Metrica:", juce::dontSendNotification);
        meterNumLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(meterNumLabel);

        meterNumEditor.setSliderStyle(juce::Slider::IncDecButtons);
        meterNumEditor.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
        meterNumEditor.setRange(1.0, 32.0, 1.0);
        meterNumEditor.setNumDecimalPlacesToDisplay(0);
        meterNumEditor.setValue(4.0, juce::dontSendNotification);
        addAndMakeVisible(meterNumEditor);

        meterSlashLabel.setText("/", juce::dontSendNotification);
        meterSlashLabel.setJustificationType(juce::Justification::centred);
        meterSlashLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e1));
        addAndMakeVisible(meterSlashLabel);

        meterDenCombo.addItem("2", 2);
        meterDenCombo.addItem("4", 4);
        meterDenCombo.addItem("8", 8);
        meterDenCombo.addItem("16", 16);
        meterDenCombo.setSelectedId(4, juce::dontSendNotification);
        addAndMakeVisible(meterDenCombo);

        addMeterBtn.setButtonText("Aggiungi");
        addMeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff15803d));
        addMeterBtn.onClick = [this]() {
            const double targetQ = meterEditedByBar
                ? beatTimeMap.barToBeat(static_cast<int>(meterBarEditor.getValue()), 0.0, -1)
                : meterBeatEditor.getValue();
            beatTimeMap.addOrUpdateMeterChange(
                targetQ,
                static_cast<int>(meterNumEditor.getValue()),
                meterDenCombo.getSelectedId());
            refreshTables();
            selectMeterRowNearBeat(targetQ);
            };
        addAndMakeVisible(addMeterBtn);

        applyMeterBtn.setButtonText("Aggiorna Sel.");
        applyMeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6d28d9));
        applyMeterBtn.onClick = [this]() {
            const int row = meterTable.getSelectedRow();
            if (row >= 0)
            {
                const double targetQ = (row == 0)
                    ? 1.0
                    : (meterEditedByBar
                        ? beatTimeMap.barToBeat(static_cast<int>(meterBarEditor.getValue()), 0.0, row)
                        : meterBeatEditor.getValue());

                beatTimeMap.updateMeterAnchor(
                    row,
                    targetQ,
                    static_cast<int>(meterNumEditor.getValue()),
                    meterDenCombo.getSelectedId());
                refreshTables();
                selectMeterRowNearBeat(targetQ);
            }
            };
        addAndMakeVisible(applyMeterBtn);

        delMeterBtn.setButtonText("Elimina");
        delMeterBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff991b1b));
        delMeterBtn.onClick = [this]() {
            const int row = meterTable.getSelectedRow();
            if (row > 0)
            {
                beatTimeMap.removeMeterAnchor(row);
                refreshTables();
            }
            };
        addAndMakeVisible(delMeterBtn);

        refreshTables();
    }

    ~TimeMetricManagerComponent() override
    {
        appState.removeListener(this);
        tempoTable.setModel(nullptr);
        meterTable.setModel(nullptr);
    }

    void refreshTables()
    {
        juce::ScopedValueSetter<bool> guard(isSyncingUI, true);
        beatTimeMap.bindToAppState(appState);

        tempoRows = beatTimeMap.getEditableTempoAnchors();
        meterRows = beatTimeMap.getMeterAnchors();
        downbeatSlider.setValue(beatTimeMap.getFirstDownbeatSeconds(), juce::dontSendNotification);

        tempoTable.updateContent();
        meterTable.updateContent();

        const int tRow = tempoTable.getSelectedRow();
        if (tRow >= 0 && tRow < static_cast<int>(tempoRows.size()))
            loadTempoRowIntoEditors(tRow);
        else if (!tempoRows.empty())
            tempoTable.selectRow(0);

        const int mRow = meterTable.getSelectedRow();
        if (mRow >= 0 && mRow < static_cast<int>(meterRows.size()))
            loadMeterRowIntoEditors(mRow);
        else if (!meterRows.empty())
            meterTable.selectRow(0);

        updateButtonStates();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101118));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);

        // Header
        auto topBar = area.removeFromTop(28);
        titleLabel.setBounds(topBar.removeFromLeft(370));
        resetMeterBtn.setBounds(topBar.removeFromRight(126));
        topBar.removeFromRight(6);
        resetTempoBtn.setBounds(topBar.removeFromRight(116));
        topBar.removeFromRight(6);
        importMidiBtn.setBounds(topBar.removeFromRight(126));

        area.removeFromTop(8);

        auto subBar = area.removeFromTop(24);
        downbeatLabel.setBounds(subBar.removeFromLeft(130));
        downbeatSlider.setBounds(subBar.removeFromLeft(110));

        area.removeFromTop(10);

        // Split 50/50: Tempo Map a SX, Cambi Metrica a DX
        const int halfW = (area.getWidth() - 14) / 2;
        auto leftCol = area.removeFromLeft(halfW);
        area.removeFromLeft(14);
        auto rightCol = area;

        // Colonna Sinistra (Tempo)
        tempoHeaderLabel.setBounds(leftCol.removeFromTop(22));
        leftCol.removeFromTop(4);
        auto tBtns = leftCol.removeFromBottom(26);
        leftCol.removeFromBottom(6);
        auto tRow2 = leftCol.removeFromBottom(26);
        leftCol.removeFromBottom(5);
        auto tRow1 = leftCol.removeFromBottom(26);
        leftCol.removeFromBottom(6);
        tempoTable.setBounds(leftCol);

        tempoBarLabel.setBounds(tRow1.removeFromLeft(78));
        tempoBarEditor.setBounds(tRow1.removeFromLeft(108));
        tRow1.removeFromLeft(12);
        tempoBeatLabel.setBounds(tRow1.removeFromLeft(22));
        tempoBeatEditor.setBounds(tRow1.removeFromLeft(132));

        tempoBpmLabel.setBounds(tRow2.removeFromLeft(78));
        tempoBpmEditor.setBounds(tRow2.removeFromLeft(124));

        const int tBtnW = (tBtns.getWidth() - 12) / 3;
        addTempoBtn.setBounds(tBtns.removeFromLeft(tBtnW));
        tBtns.removeFromLeft(6);
        applyTempoBtn.setBounds(tBtns.removeFromLeft(tBtnW));
        tBtns.removeFromLeft(6);
        delTempoBtn.setBounds(tBtns);

        // Colonna Destra (Metrica)
        meterHeaderLabel.setBounds(rightCol.removeFromTop(22));
        rightCol.removeFromTop(4);
        auto mBtns = rightCol.removeFromBottom(26);
        rightCol.removeFromBottom(6);
        auto mRow2 = rightCol.removeFromBottom(26);
        rightCol.removeFromBottom(5);
        auto mRow1 = rightCol.removeFromBottom(26);
        rightCol.removeFromBottom(6);
        meterTable.setBounds(rightCol);

        meterBarLabel.setBounds(mRow1.removeFromLeft(78));
        meterBarEditor.setBounds(mRow1.removeFromLeft(108));
        mRow1.removeFromLeft(12);
        meterBeatLabel.setBounds(mRow1.removeFromLeft(22));
        meterBeatEditor.setBounds(mRow1.removeFromLeft(132));

        meterNumLabel.setBounds(mRow2.removeFromLeft(78));
        meterNumEditor.setBounds(mRow2.removeFromLeft(96));
        meterSlashLabel.setBounds(mRow2.removeFromLeft(16));
        meterDenCombo.setBounds(mRow2.removeFromLeft(62));

        const int mBtnW = (mBtns.getWidth() - 12) / 3;
        addMeterBtn.setBounds(mBtns.removeFromLeft(mBtnW));
        mBtns.removeFromLeft(6);
        applyMeterBtn.setBounds(mBtns.removeFromLeft(mBtnW));
        mBtns.removeFromLeft(6);
        delMeterBtn.setBounds(mBtns);
    }

private:
    class TempoTableModel : public juce::TableListBoxModel
    {
    public:
        explicit TempoTableModel(TimeMetricManagerComponent& o) : owner(o) {}

        int getNumRows() override { return static_cast<int>(owner.tempoRows.size()); }

        void paintRowBackground(juce::Graphics& g, int row, int, int, bool isSelected) override
        {
            if (isSelected) g.fillAll(juce::Colour(0xff3b2f14));
            else            g.fillAll((row % 2 == 0) ? juce::Colour(0xff161822) : juce::Colour(0xff1a1d29));
        }

        void paintCell(juce::Graphics& g, int row, int colId, int w, int h, bool) override
        {
            if (row < 0 || row >= static_cast<int>(owner.tempoRows.size())) return;
            const auto& a = owner.tempoRows[static_cast<size_t>(row)];
            juce::String txt;

            if (colId == 1) txt = juce::String(row + 1);
            else if (colId == 2) txt = owner.beatTimeMap.beatToMusicalPosition(a.beat).formatBarString();
            else if (colId == 3) txt = juce::String(a.beat, 3);
            else if (colId == 4) txt = BeatTimeMap::formatTimecode(a.timeSec);
            else if (colId == 5) txt = juce::String(a.localBpm, 2);

            g.setColour(colId == 5 ? juce::Colour(0xfffef08a) : juce::Colour(0xffe2e8f0));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(txt, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged(int lastRowSelected) override
        {
            owner.loadTempoRowIntoEditors(lastRowSelected);
            owner.updateButtonStates();
        }

    private:
        TimeMetricManagerComponent& owner;
    };

    class MeterTableModel : public juce::TableListBoxModel
    {
    public:
        explicit MeterTableModel(TimeMetricManagerComponent& o) : owner(o) {}

        int getNumRows() override { return static_cast<int>(owner.meterRows.size()); }

        void paintRowBackground(juce::Graphics& g, int row, int, int, bool isSelected) override
        {
            if (isSelected) g.fillAll(juce::Colour(0xff2e1c4a));
            else            g.fillAll((row % 2 == 0) ? juce::Colour(0xff161822) : juce::Colour(0xff1a1d29));
        }

        void paintCell(juce::Graphics& g, int row, int colId, int w, int h, bool) override
        {
            if (row < 0 || row >= static_cast<int>(owner.meterRows.size())) return;
            const auto& m = owner.meterRows[static_cast<size_t>(row)];
            juce::String txt;

            if (colId == 1) txt = juce::String(row + 1);
            else if (colId == 2) txt = "M" + juce::String(m.startBar);
            else if (colId == 3) txt = juce::String(m.startBeat, 3);
            else if (colId == 4) txt = juce::String(m.num) + "/" + juce::String(m.den);
            else if (colId == 5) txt = juce::String(m.getBarLengthQ(), 2) + " Q";

            g.setColour(colId == 4 ? juce::Colour(0xffc4b5fd) : juce::Colour(0xffe2e8f0));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(txt, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged(int lastRowSelected) override
        {
            owner.loadMeterRowIntoEditors(lastRowSelected);
            owner.updateButtonStates();
        }

    private:
        TimeMetricManagerComponent& owner;
    };

    void loadTempoRowIntoEditors(int row)
    {
        if (row < 0 || row >= static_cast<int>(tempoRows.size())) return;
        juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
        const auto& a = tempoRows[static_cast<size_t>(row)];
        const auto pos = beatTimeMap.beatToMusicalPosition(a.beat);
        tempoBarEditor.setValue(pos.barIndex, juce::dontSendNotification);
        tempoBeatEditor.setValue(a.beat, juce::dontSendNotification);
        tempoBpmEditor.setValue(a.localBpm, juce::dontSendNotification);
    }

    void loadMeterRowIntoEditors(int row)
    {
        if (row < 0 || row >= static_cast<int>(meterRows.size())) return;
        juce::ScopedValueSetter<bool> guard(isSyncingEditors, true);
        const auto& m = meterRows[static_cast<size_t>(row)];
        meterBarEditor.setValue(m.startBar, juce::dontSendNotification);
        meterBeatEditor.setValue(m.startBeat, juce::dontSendNotification);
        meterNumEditor.setValue(m.num, juce::dontSendNotification);
        meterDenCombo.setSelectedId(m.den, juce::dontSendNotification);
        meterEditedByBar = false;
    }

    void selectTempoRowNearBeat(double targetQ)
    {
        for (int i = 0; i < static_cast<int>(tempoRows.size()); ++i)
        {
            if (std::abs(tempoRows[static_cast<size_t>(i)].beat - targetQ) < 1e-3)
            {
                tempoTable.selectRow(i);
                return;
            }
        }
    }

    void selectMeterRowNearBeat(double targetQ)
    {
        for (int i = 0; i < static_cast<int>(meterRows.size()); ++i)
        {
            if (std::abs(meterRows[static_cast<size_t>(i)].startBeat - targetQ) < 1e-3)
            {
                meterTable.selectRow(i);
                return;
            }
        }
    }

    void updateButtonStates()
    {
        const int tRow = tempoTable.getSelectedRow();
        applyTempoBtn.setEnabled(tRow >= 0 && tRow < static_cast<int>(tempoRows.size()));
        delTempoBtn.setEnabled(tRow > 0 && tRow < static_cast<int>(tempoRows.size()));

        const int mRow = meterTable.getSelectedRow();
        applyMeterBtn.setEnabled(mRow >= 0 && mRow < static_cast<int>(meterRows.size()));
        delMeterBtn.setEnabled(mRow > 0 && mRow < static_cast<int>(meterRows.size()));
    }

    void promptImportMidi()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Importa Tempo Map e Metrica da file MIDI (.mid)",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.mid;*.midi");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        juce::Component::SafePointer<TimeMetricManagerComponent> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto f = fc.getResult();
            if (!f.existsAsFile()) return;

            auto res = safeThis->beatTimeMap.importMidiTempoAndMeterMap(f, safeThis->beatTimeMap.getFirstDownbeatSeconds());
            safeThis->refreshTables();
            if (!res.success)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Errore Importazione MIDI", res.errorMessage);
            }
            });
    }

    void handleTreeChange(const juce::ValueTree& node)
    {
        if (isSyncingUI) return;
        if (node.hasType(StateKeys::BeatMapNode) ||
            node.hasType(StateKeys::BeatPointNode) ||
            node.hasType(StateKeys::MeterPointNode))
        {
            juce::Component::SafePointer<TimeMetricManagerComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis]() {
                if (safeThis != nullptr)
                    safeThis->refreshTables();
                });
        }
    }

    void valueTreePropertyChanged(juce::ValueTree& t, const juce::Identifier&) override { handleTreeChange(t); }
    void valueTreeChildAdded(juce::ValueTree& p, juce::ValueTree& c) override { handleTreeChange(p); handleTreeChange(c); }
    void valueTreeChildRemoved(juce::ValueTree& p, juce::ValueTree& c, int) override { handleTreeChange(p); handleTreeChange(c); }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    BeatTimeMap& beatTimeMap;
    bool isSyncingUI = false;
    bool isSyncingEditors = false;
    bool meterEditedByBar = false;

    std::vector<BeatTimeMap::BeatAnchor> tempoRows;
    std::vector<BeatTimeMap::MeterAnchor> meterRows;

    TempoTableModel tempoModel;
    MeterTableModel meterModel;

    juce::Label titleLabel, downbeatLabel;
    juce::Slider downbeatSlider;
    juce::TextButton importMidiBtn, resetTempoBtn, resetMeterBtn;

    juce::Label tempoHeaderLabel, tempoBarLabel, tempoBeatLabel, tempoBpmLabel;
    juce::TableListBox tempoTable;
    juce::Slider tempoBarEditor, tempoBeatEditor, tempoBpmEditor;
    juce::TextButton addTempoBtn, applyTempoBtn, delTempoBtn;

    juce::Label meterHeaderLabel, meterBarLabel, meterBeatLabel, meterNumLabel, meterSlashLabel;
    juce::TableListBox meterTable;
    juce::Slider meterBarEditor, meterBeatEditor, meterNumEditor;
    juce::ComboBox meterDenCombo;
    juce::TextButton addMeterBtn, applyMeterBtn, delMeterBtn;

    std::unique_ptr<juce::FileChooser> fileChooser;
};