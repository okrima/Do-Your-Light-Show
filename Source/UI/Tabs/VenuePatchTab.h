#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Model/LogicalParam.h"
#include "../../Model/DataStateKeys.h"
#include "../../Utils/PresetManager.h"
#include "../../Model/StageRoleCatalog.h"

#include <vector>
#include <memory>

struct ParamControl {
    juce::Label label;
    juce::Slider slider;
};

// =============================================================================
// Helper statico per sincronizzare i nodi FIXTURE_UNIT con la proprieta' Quantity
// =============================================================================
struct VenueUnitHelper
{
    static void syncUnitsForFixture(juce::ValueTree& fixNode)
    {
        if (!fixNode.isValid() || !fixNode.hasType(StateKeys::Fixture))
            return;

        int targetQty = juce::jlimit(1, 32, static_cast<int>(fixNode.getProperty(StateKeys::FixtureQuantity, 1)));

        // Conta le unita' attuali
        std::vector<juce::ValueTree> currentUnits;
        for (int i = 0; i < fixNode.getNumChildren(); ++i)
        {
            auto child = fixNode.getChild(i);
            if (child.hasType(StateKeys::FixtureUnit))
                currentUnits.push_back(child);
        }

        // Aggiunge unita' mancanti (parcheggiate nel Light Case: isPlaced = false)
        while (static_cast<int>(currentUnits.size()) < targetQty)
        {
            int nextIdx = static_cast<int>(currentUnits.size()) + 1;
            juce::ValueTree unit(StateKeys::FixtureUnit);
            unit.setProperty(StateKeys::UnitIndex, nextIdx, nullptr);
            unit.setProperty(StateKeys::IsPlaced, false, nullptr);
            unit.setProperty(StateKeys::PosX, 0.5, nullptr);
            unit.setProperty(StateKeys::PosY, 0.12, nullptr);
            fixNode.addChild(unit, -1, nullptr);
            currentUnits.push_back(unit);
        }

        // Rimuove eventuali unita' in eccesso dalla coda
        while (static_cast<int>(currentUnits.size()) > targetQty)
        {
            fixNode.removeChild(currentUnits.back(), nullptr);
            currentUnits.pop_back();
        }
    }

    static void ensureVenueUnitsInitialized(juce::ValueTree& venueNode)
    {
        if (!venueNode.isValid()) return;
        for (int i = 0; i < venueNode.getNumChildren(); ++i)
        {
            auto fix = venueNode.getChild(i);
            if (fix.hasType(StateKeys::Fixture))
                syncUnitsForFixture(fix);
        }
    }
};

// =============================================================================
// FixtureEditorPanel (Con Quantita', Strobo di Fabbrica e Calibrazione Pan/Tilt)
// =============================================================================
class FixtureEditorPanel : public juce::Component,
    private juce::ValueTree::Listener
{
public:
    FixtureEditorPanel(juce::ValueTree& state);
    ~FixtureEditorPanel() override;

    void setFixtureNode(juce::ValueTree fixtureNode);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void updateUIFromNode();
    void applyQuickSpreadPreset(int presetId);
    void shiftPanLimits(int delta);
    void openCalibrationWizard();

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree currentFixtureNode;
    bool isUpdatingUI = false;

    // 1. Controlli Base & Quantita'
    juce::Label titleLabel;
    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    juce::Label roleLabel;
    juce::ComboBox roleCombo;

    juce::Label qtyLabel;
    juce::Slider qtySlider;

    juce::Label startAddrLabel;
    juce::Slider startAddrSlider;

    juce::Label chCountLabel;
    juce::Slider chCountSlider;

    // 2. Mappatura Canali DMX
    juce::Label mapTitleLabel;
    juce::StringArray availableParams;
    std::vector<std::unique_ptr<ParamControl>> paramControls;

    // 3. Calibrazione Strobo Hardware & Frequenza (Hz)
    juce::Label strobeSectionLabel;
    juce::Label strobeModeLabel;
    juce::ComboBox strobeModeCombo;

    juce::Label dimmerOrOpenLabel;
    juce::Slider dimmerOrOpenSlider;

    juce::Label strobeStartDmxLabel, strobeStartHzLabel;
    juce::Slider strobeStartDmxSlider, strobeStartHzSlider;

    juce::Label strobeEndDmxLabel, strobeEndHzLabel;
    juce::Slider strobeEndDmxSlider, strobeEndHzSlider;

    // 4. Area di Movimento sul Palco (4 Limiti DMX + Shift Pan + Totem)
    juce::Label ptSectionLabel;
    juce::TextButton ptWizardBtn;
    juce::TextButton ptSpreadNarrowBtn, ptSpreadMedBtn, ptSpreadWideBtn;

    juce::Label panSxLabel, panDxLabel;
    juce::Slider panLeftSlider, panRightSlider;
    juce::TextButton swapPanDirBtn;

    juce::Label tiltLowLabel, tiltHighLabel;
    juce::Slider tiltLowSlider, tiltHighSlider;
    juce::TextButton swapTiltDirBtn;

    juce::Label panShiftLabel;
    juce::TextButton panShiftM42Btn, panShiftM10Btn, panShiftP10Btn, panShiftP42Btn;
    juce::ToggleButton swapPanTiltToggle;

    juce::Label ptReadoutLabel;
};

// =============================================================================
// StagePatchAndCasePanel: "Light Case" + Palco 2D Drag & Drop con Smart Masking
// =============================================================================
class StagePatchAndCasePanel : public juce::Component,
    public juce::DragAndDropTarget,
    private juce::ValueTree::Listener
{
public:
    StagePatchAndCasePanel(juce::ValueTree& state);
    ~StagePatchAndCasePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // DragAndDropTarget interface
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    void refreshFromState()
    {
        rebuildUnitCache();
        repaint();
    }

    void setSelectedFixtureIndex(int fixtureIdx)
    {
        if (selectedFixtureIdx != fixtureIdx)
        {
            selectedFixtureIdx = fixtureIdx;
            repaint();
        }
    }

    std::function<void(int)> onFixtureSelectedFromMap;

private:

    enum class ValidZone
    {
        None,
        AirTruss,   // Americana superiore
        FloorStage, // Pavimento / Pedana terra
        SideWings   // Quinte laterali SX / DX
    };

    struct VisualUnitItem
    {
        int fixtureIdx = 0;
        int unitIdxInChild = 0;
        int unitNumber = 1;
        int dmxAddress = 1;
        juce::String label;
        juce::String roleName;
        StageRoleCatalog::Category category;
        bool isPlaced = false;
        juce::Rectangle<float> screenBounds;
        juce::ValueTree unitNode;
        juce::Point<float> center;
    };

    void rebuildUnitCache();
    static ValidZone getValidZoneForCategory(StageRoleCatalog::Category cat);
    ValidZone getZoneForDragDescription(const juce::String& desc, juce::String& outLabel) const;
    bool isPointInsideZone(juce::Point<float> pt, ValidZone zone) const;
    juce::Point<float> clampPointToZone(juce::Point<float> pt, ValidZone zone) const;

    void autoPlaceAllUnits();
    void returnAllUnitsToCase();
    void handleVenueTreeChange(const juce::ValueTree& tree);

    // SOSTITUISCI CON LE SOLE DICHIARAZIONI:
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override;
    void valueTreeParentChanged(juce::ValueTree&) override {}
    void applyQuickPanTiltPreset(int presetId);

    bool isRebuilding = false;
    int selectedFixtureIdx = -1;

    juce::ValueTree& appState;
    juce::ValueTree venueNode;

    juce::TextButton autoPlaceBtn;
    juce::TextButton clearStageBtn;

    juce::Rectangle<float> lightCaseArea;
    juce::Rectangle<float> stageCanvasArea;

    // Aree geometriche delle 3 zone sul palco
    juce::Rectangle<float> airZoneRect;
    juce::Rectangle<float> floorZoneRect;
    juce::Rectangle<float> leftWingRect;
    juce::Rectangle<float> rightWingRect;

    std::vector<VisualUnitItem> unplacedUnits;
    std::vector<VisualUnitItem> placedUnits;

    bool isDraggingUnit = false;
    ValidZone activeDragZone = ValidZone::None;
    juce::String activeDragLabel;
    int hitItemIndex = -1;
    bool hitWasPlaced = false;

    juce::Rectangle<float> stageRect;
    int hoveredPlacedIndex = -1;
};

// =============================================================================
// VenuePatchTab (Con DragAndDropContainer integrato)
// =============================================================================
class VenuePatchTab : public juce::Component,
    public juce::DragAndDropContainer,
    public juce::ListBoxModel,
    private juce::ValueTree::Listener
{
public:
    VenuePatchTab(juce::ValueTree& state);
    ~VenuePatchTab() override;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void addNewFixtureToState();
    void removeSelectedFixture();
    void saveVenue();
    void loadVenue();

    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    juce::ValueTree venueNode;

    juce::ListBox fixtureList;
    juce::TextButton addFixtureBtn;
    juce::TextButton removeFixtureBtn;
    juce::TextButton saveBtn;
    juce::TextButton loadBtn;

    FixtureEditorPanel editorPanel;
    StagePatchAndCasePanel stageDesignerPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VenuePatchTab)
};