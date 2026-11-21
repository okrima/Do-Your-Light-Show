#pragma once
#include <JuceHeader.h>
#include "../../Model/DataStateKeys.h"
#include "../../Audio/StemAudioEngine.h"
#include <array>

class AIWizardPanel : public juce::Component,
    public juce::FileDragAndDropTarget,
    private juce::ValueTree::Listener
{
public:
    std::function<void()> onFullMixUpdated;
    std::function<void()> onGenerateShowRequested;

    AIWizardPanel(juce::ValueTree& state, StemAudioEngine& engine)
        : appState(state), audioEngine(engine)
    {
        appState.addListener(this);

        stemSlots = {
            { StateKeys::StemFullMix, StateKeys::StemFullMixVol, "1. Full Mix *",     "Obbligatorio (Struttura)",{} },
            { StateKeys::StemClick,   StateKeys::StemClickVol,   "2. Click / BPM",    "Cadenza e Griglia Quarti",{} },
            { StateKeys::StemDrums,   StateKeys::StemDrumsVol,   "3. Batteria",       "Accenti Cassa / Rullante",{} },
            { StateKeys::StemBass,    StateKeys::StemBassVol,    "4. Basso",          "Pulsazione Dimmer / Wash",{} },
            { StateKeys::StemGuitars, StateKeys::StemGuitarsVol, "5. Chitarre / Mel", "Aperture e Colori LFO",{} }
        };

        // Slider Volume dedicato per ciascuna delle 5 tracce audio
        for (size_t i = 0; i < stemVolSliders.size(); ++i)
        {
            auto& sl = stemVolSliders[i];
            sl.setSliderStyle(juce::Slider::LinearBar);
            sl.setRange(0.0, 100.0, 1.0);
            sl.setValue(100.0, juce::dontSendNotification);
            sl.setTextValueSuffix("%");
            sl.setWantsKeyboardFocus(false);
            sl.setColour(juce::Slider::trackColourId, juce::Colour(0xff10b981).withAlpha(0.55f));
            sl.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff101816));
            sl.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
            sl.setTooltip("Volume traccia " + stemSlots[i].title);
            sl.onValueChange = [this, i]() {
                if (isUpdatingUI) return;
                juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);
                const float gain = static_cast<float>(stemVolSliders[i].getValue() / 100.0);
                audioEngine.setStemVolume(stemSlots[i].key, gain, &appState);
                };
            addChildComponent(sl);
        }

        // Mood Preset rapidi
        moodCombo.addItem("Gelo & Nebbia (Blu / Ciano / Bianco)", 1);
        moodCombo.addItem("Fuoco & Rame (Rosso / Ambra / Oro)", 2);
        moodCombo.addItem("Crepuscolo Antico (Viola / Verde / Ambra)", 3);
        moodCombo.addItem("Abisso & Strobo (Indaco / Rosso / Bianco)", 4);
        moodCombo.addItem("Personalizzato", 5);
        moodCombo.onChange = [this]() { applySelectedMoodPreset(); };
        addAndMakeVisible(moodCombo);

        strobeSlider.setSliderStyle(juce::Slider::LinearBar);
        strobeSlider.setRange(0.0, 100.0, 1.0);
        strobeSlider.setTextValueSuffix(" % Strobo");
        strobeSlider.onValueChange = [this]() {
            if (isUpdatingUI) return;
            auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
            if (aesthetic.isValid())
                aesthetic.setProperty(StateKeys::StrobeIntensity, strobeSlider.getValue() / 100.0, nullptr);
            };
        addAndMakeVisible(strobeSlider);

        layeringToggle.setButtonText("Solo nuovi fari (Layering)");
        layeringToggle.onClick = [this]() {
            if (isUpdatingUI) return;
            auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
            if (aesthetic.isValid())
                aesthetic.setProperty(StateKeys::PreserveExistingRoles, layeringToggle.getToggleState(), nullptr);
            };
        addAndMakeVisible(layeringToggle);

        generateBtn.setButtonText("Genera Show con AI");
        generateBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7c3aed));
        generateBtn.onClick = [this]() { handleGenerateClick(); };
        addAndMakeVisible(generateBtn);

        syncUIFromState();
    }

    ~AIWizardPanel() override
    {
        appState.removeListener(this);
    }

    void syncUIFromState()
    {
        juce::ScopedValueSetter<bool> guard(isUpdatingUI, true);

        auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
        if (aesthetic.isValid())
        {
            double strobeVal = static_cast<double>(aesthetic.getProperty(StateKeys::StrobeIntensity, 0.5));
            strobeSlider.setValue(strobeVal * 100.0, juce::dontSendNotification);

            bool preserve = static_cast<bool>(aesthetic.getProperty(StateKeys::PreserveExistingRoles, false));
            layeringToggle.setToggleState(preserve, juce::dontSendNotification);

            juce::String profile = aesthetic.getProperty(StateKeys::MoodProfile, "IceAndFog").toString();
            if (profile == "IceAndFog")           moodCombo.setSelectedId(1, juce::dontSendNotification);
            else if (profile == "FireAndCopper")  moodCombo.setSelectedId(2, juce::dontSendNotification);
            else if (profile == "AncientDusk")    moodCombo.setSelectedId(3, juce::dontSendNotification);
            else if (profile == "AbyssAndStrobe") moodCombo.setSelectedId(4, juce::dontSendNotification);
            else                                  moodCombo.setSelectedId(5, juce::dontSendNotification);
        }

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        for (size_t i = 0; i < stemSlots.size(); ++i)
        {
            juce::String path = stemsNode.isValid() ? stemsNode.getProperty(stemSlots[i].key, "").toString() : "";
            const bool isLoaded = (path.isNotEmpty() && juce::File(path).existsAsFile());

            double savedVol = stemsNode.isValid()
                ? static_cast<double>(stemsNode.getProperty(stemSlots[i].volKey, 1.0))
                : 1.0;

            stemVolSliders[i].setValue(juce::jlimit(0.0, 100.0, savedVol * 100.0), juce::dontSendNotification);
            stemVolSliders[i].setVisible(isLoaded);
        }

        repaint();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);

        leftColBounds = bounds.removeFromLeft(juce::roundToInt(bounds.getWidth() * 0.50f));
        bounds.removeFromLeft(6);
        rightColBounds = bounds.removeFromRight(juce::roundToInt(bounds.getWidth() * 0.36f));
        bounds.removeFromRight(6);
        centerColBounds = bounds;

        // 1. Layout dei 5 Slot Stem (3 sulla riga alta, 2 sulla riga bassa) con spazio per Volume
        auto stemArea = leftColBounds.reduced(6);
        stemArea.removeFromTop(18);
        const int rowH = juce::jmax(36, (stemArea.getHeight() - 5) / 2);
        auto topRow = stemArea.removeFromTop(rowH);
        stemArea.removeFromTop(5);
        auto botRow = stemArea.removeFromTop(rowH);

        int topSlotW = (topRow.getWidth() - 10) / 3;
        stemSlots[0].bounds = topRow.removeFromLeft(topSlotW); topRow.removeFromLeft(5);
        stemSlots[1].bounds = topRow.removeFromLeft(topSlotW); topRow.removeFromLeft(5);
        stemSlots[2].bounds = topRow;

        int botSlotW = (botRow.getWidth() - 5) / 2;
        stemSlots[3].bounds = botRow.removeFromLeft(botSlotW); botRow.removeFromLeft(5);
        stemSlots[4].bounds = botRow;

        for (size_t i = 0; i < stemSlots.size(); ++i)
        {
            auto r = stemSlots[i].bounds;
            stemVolSliders[i].setBounds(r.getX() + 4, r.getBottom() - 16, juce::jmax(20, r.getWidth() - 8), 14);
        }

        // 2. Layout Sezione "Quali colori vuoi?"
        auto colorArea = centerColBounds.reduced(8);
        colorArea.removeFromTop(20);

        auto moodRow = colorArea.removeFromTop(26);
        moodCombo.setBounds(moodRow.removeFromLeft(juce::roundToInt(moodRow.getWidth() * 0.62f)));
        moodRow.removeFromLeft(6);
        strobeSlider.setBounds(moodRow);

        colorArea.removeFromTop(8);
        auto swatchRow = colorArea.removeFromTop(44);
        int swatchW = (swatchRow.getWidth() - 12) / 3;
        colorSwatchBounds[0] = swatchRow.removeFromLeft(swatchW); swatchRow.removeFromLeft(6);
        colorSwatchBounds[1] = swatchRow.removeFromLeft(swatchW); swatchRow.removeFromLeft(6);
        colorSwatchBounds[2] = swatchRow;

        // 3. Layout Sezione Generazione & Layering
        auto actionArea = rightColBounds.reduced(8);
        actionArea.removeFromTop(20);
        layeringToggle.setBounds(actionArea.removeFromTop(28));
        actionArea.removeFromTop(8);
        generateBtn.setBounds(actionArea.removeFromTop(42));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141418));

        auto drawPanelBox = [&g](const juce::Rectangle<int>& r, const juce::String& title) {
            g.setColour(juce::Colour(0xff1b1b22));
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
            g.setColour(juce::Colour(0xff2a2a35));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);

            g.setColour(juce::Colour(0xff94a3b8));
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(title, r.getX() + 10, r.getY() + 3, r.getWidth() - 20, 16, juce::Justification::centredLeft, false);
            };

        drawPanelBox(leftColBounds, "TRACCE AUDIO & VOLUMI (Clicca o Trascina file)");
        drawPanelBox(centerColBounds, "QUALI COLORI VUOI? (Mood & Palette Brano)");
        drawPanelBox(rightColBounds, "MOTORE AI LOCALE");

        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        for (size_t i = 0; i < stemSlots.size(); ++i)
        {
            const auto& slot = stemSlots[i];
            juce::String path = stemsNode.isValid() ? stemsNode.getProperty(slot.key, "").toString() : "";
            juce::File f(path);
            bool isLoaded = (path.isNotEmpty() && f.existsAsFile());

            g.setColour(isLoaded ? juce::Colour(0xff1e3a2f) : juce::Colour(0xff23232c));
            g.fillRoundedRectangle(slot.bounds.toFloat(), 4.0f);

            g.setColour(isLoaded ? juce::Colour(0xff10b981) : juce::Colour(0xff383846));
            g.drawRoundedRectangle(slot.bounds.toFloat(), 4.0f, 1.0f);

            g.setColour(isLoaded ? juce::Colour(0xff10b981) : juce::Colour(0xff64748b));
            g.fillEllipse(static_cast<float>(slot.bounds.getX() + 6),
                static_cast<float>(slot.bounds.getY() + 6), 6.0f, 6.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(slot.title, slot.bounds.getX() + 16, slot.bounds.getY() + 1,
                slot.bounds.getWidth() - 34, 14, juce::Justification::centredLeft, true);

            g.setColour(isLoaded ? juce::Colour(0xffa7f3d0) : juce::Colour(0xff808090));
            g.setFont(juce::FontOptions(10.0f));
            juce::String subText = isLoaded ? f.getFileName() : slot.subtitle;
            g.drawText(subText, slot.bounds.getX() + 6, slot.bounds.getY() + 14,
                slot.bounds.getWidth() - 22, 13, juce::Justification::centredLeft, true);

            if (isLoaded)
            {
                auto clearRect = getClearRectForSlot(slot.bounds);
                g.setColour(juce::Colour(0xffef4444));
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText("X", clearRect, juce::Justification::centred, false);
            }
        }

        const char* colorLabels[3] = { "1. Atmosfera", "2. Melodia", "3. Flash" };
        const juce::Identifier colorKeys[3] = {
            StateKeys::PrimaryColor, StateKeys::SecondaryColor, StateKeys::AccentColor
        };

        auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
        for (int i = 0; i < 3; ++i)
        {
            auto r = colorSwatchBounds[i];
            juce::Colour c = getColourFromState(aesthetic, colorKeys[i]);

            g.setColour(c);
            g.fillRoundedRectangle(r.toFloat(), 4.0f);

            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.drawRoundedRectangle(r.toFloat(), 4.0f, 1.0f);

            auto labelBar = r.withTop(r.getBottom() - 16);
            g.setColour(juce::Colours::black.withAlpha(0.65f));
            g.fillRoundedRectangle(labelBar.toFloat(), 3.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(colorLabels[i], labelBar, juce::Justification::centred, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (size_t i = 0; i < stemSlots.size(); ++i)
        {
            if (stemSlots[i].bounds.contains(e.getPosition()))
            {
                auto clearRect = getClearRectForSlot(stemSlots[i].bounds);
                if (clearRect.contains(e.getPosition()))
                    clearStemSlot(i);
                else
                    promptForStemFile(i);
                return;
            }
        }

        const juce::Identifier colorKeys[3] = {
            StateKeys::PrimaryColor, StateKeys::SecondaryColor, StateKeys::AccentColor
        };
        for (int i = 0; i < 3; ++i)
        {
            if (colorSwatchBounds[i].contains(e.getPosition()))
            {
                openColourPickerPopup(colorKeys[i], colorSwatchBounds[i]);
                return;
            }
        }
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& path : files)
        {
            if (path.endsWithIgnoreCase(".wav") || path.endsWithIgnoreCase(".mp3") ||
                path.endsWithIgnoreCase(".flac") || path.endsWithIgnoreCase(".aiff") ||
                path.endsWithIgnoreCase(".ogg"))
                return true;
        }
        return false;
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        if (files.isEmpty()) return;
        juce::Point<int> dropPt(x, y);

        for (size_t i = 0; i < stemSlots.size(); ++i)
        {
            if (stemSlots[i].bounds.contains(dropPt))
            {
                assignFileToStemSlot(i, juce::File(files[0]));
                return;
            }
        }
    }

private:
    struct StemSlotInfo
    {
        juce::Identifier key;
        juce::Identifier volKey;
        juce::String title;
        juce::String subtitle;
        juce::Rectangle<int> bounds;
    };

    class ColourPickerPopup : public juce::Component,
        public juce::ChangeListener
    {
    public:
        ColourPickerPopup(juce::Colour initialColour, std::function<void(juce::Colour)> onChanged)
            : callback(std::move(onChanged)),
            selector(juce::ColourSelector::showColourAtTop |
                juce::ColourSelector::showSliders |
                juce::ColourSelector::showColourspace)
        {
            selector.setCurrentColour(initialColour);
            selector.addChangeListener(this);
            addAndMakeVisible(selector);
            setSize(260, 280);
        }

        ~ColourPickerPopup() override { selector.removeChangeListener(this); }
        void resized() override { selector.setBounds(getLocalBounds()); }
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            if (callback) callback(selector.getCurrentColour());
        }

    private:
        std::function<void(juce::Colour)> callback;
        juce::ColourSelector selector;
    };

    juce::Rectangle<int> getClearRectForSlot(const juce::Rectangle<int>& slotBounds) const
    {
        return juce::Rectangle<int>(slotBounds.getRight() - 18, slotBounds.getY() + 1, 16, 15);
    }

    static juce::Colour getColourFromState(const juce::ValueTree& aesthetic, const juce::Identifier& key)
    {
        if (!aesthetic.isValid()) return juce::Colours::dodgerblue;
        juce::String hex = aesthetic.getProperty(key, "ff1e3a8a").toString();
        return juce::Colour::fromString(hex);
    }

    void openColourPickerPopup(const juce::Identifier& colorKey, const juce::Rectangle<int>& targetBounds)
    {
        auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
        juce::Colour initial = getColourFromState(aesthetic, colorKey);

        auto picker = std::make_unique<ColourPickerPopup>(initial, [this, colorKey](juce::Colour newCol) {
            auto node = appState.getChildWithName(StateKeys::AestheticNode);
            if (node.isValid())
            {
                node.setProperty(colorKey, newCol.toString(), nullptr);
                node.setProperty(StateKeys::MoodProfile, "Custom", nullptr);
                moodCombo.setSelectedId(5, juce::dontSendNotification);
                repaint();
            }
            });

        juce::CallOutBox::launchAsynchronously(std::move(picker), targetBounds, this);
    }

    void applySelectedMoodPreset()
    {
        if (isUpdatingUI) return;
        auto aesthetic = appState.getChildWithName(StateKeys::AestheticNode);
        if (!aesthetic.isValid()) return;

        int id = moodCombo.getSelectedId();
        if (id == 1)
        {
            aesthetic.setProperty(StateKeys::MoodProfile, "IceAndFog", nullptr);
            aesthetic.setProperty(StateKeys::PrimaryColor, "ff1e3a8a", nullptr);
            aesthetic.setProperty(StateKeys::SecondaryColor, "ff06b6d4", nullptr);
            aesthetic.setProperty(StateKeys::AccentColor, "ffffffff", nullptr);
            strobeSlider.setValue(45.0, juce::sendNotificationSync);
        }
        else if (id == 2)
        {
            aesthetic.setProperty(StateKeys::MoodProfile, "FireAndCopper", nullptr);
            aesthetic.setProperty(StateKeys::PrimaryColor, "ff7f1d1d", nullptr);
            aesthetic.setProperty(StateKeys::SecondaryColor, "ffd97706", nullptr);
            aesthetic.setProperty(StateKeys::AccentColor, "fffef08a", nullptr);
            strobeSlider.setValue(55.0, juce::sendNotificationSync);
        }
        else if (id == 3)
        {
            aesthetic.setProperty(StateKeys::MoodProfile, "AncientDusk", nullptr);
            aesthetic.setProperty(StateKeys::PrimaryColor, "ff3b0764", nullptr);
            aesthetic.setProperty(StateKeys::SecondaryColor, "ff15803d", nullptr);
            aesthetic.setProperty(StateKeys::AccentColor, "fff59e0b", nullptr);
            strobeSlider.setValue(30.0, juce::sendNotificationSync);
        }
        else if (id == 4)
        {
            aesthetic.setProperty(StateKeys::MoodProfile, "AbyssAndStrobe", nullptr);
            aesthetic.setProperty(StateKeys::PrimaryColor, "ff172554", nullptr);
            aesthetic.setProperty(StateKeys::SecondaryColor, "ff991b1b", nullptr);
            aesthetic.setProperty(StateKeys::AccentColor, "ffffffff", nullptr);
            strobeSlider.setValue(85.0, juce::sendNotificationSync);
        }
        repaint();
    }

    void promptForStemFile(size_t slotIndex)
    {
        if (slotIndex >= stemSlots.size()) return;

        fileChooser = std::make_unique<juce::FileChooser>(
            "Seleziona traccia per " + stemSlots[slotIndex].title,
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        juce::Component::SafePointer<AIWizardPanel> safeThis(this);
        fileChooser->launchAsync(chooserFlags, [safeThis, slotIndex](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
                safeThis->assignFileToStemSlot(slotIndex, file);
            });
    }

    void assignFileToStemSlot(size_t slotIndex, const juce::File& file)
    {
        if (slotIndex >= stemSlots.size() || !file.existsAsFile()) return;

        const auto& key = stemSlots[slotIndex].key;
        if (key == StateKeys::StemFullMix)
        {
            if (audioEngine.loadFullMix(file, appState) && onFullMixUpdated)
                onFullMixUpdated();
        }
        else
        {
            audioEngine.setSecondaryStemFile(key, file, appState);
        }
        syncUIFromState();
    }

    void clearStemSlot(size_t slotIndex)
    {
        if (slotIndex >= stemSlots.size()) return;
        const auto& key = stemSlots[slotIndex].key;
        audioEngine.clearStem(key, &appState);
        if (key == StateKeys::StemFullMix && onFullMixUpdated)
            onFullMixUpdated();
        syncUIFromState();
    }

    void handleGenerateClick()
    {
        auto stemsNode = appState.getChildWithName(StateKeys::AudioStemsNode);
        juce::String fullMixPath = stemsNode.isValid() ? stemsNode.getProperty(StateKeys::StemFullMix, "").toString() : "";

        if (fullMixPath.isEmpty() || !juce::File(fullMixPath).existsAsFile())
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Full Mix Mancante",
                "Carica almeno la traccia '1. Full Mix *' prima di avviare la generazione AI dello spettacolo.");
            return;
        }

        if (onGenerateShowRequested)
            onGenerateShowRequested();
    }

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override
    {
        if (!isUpdatingUI)
            syncUIFromState();
    }

    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override
    {
        if (!isUpdatingUI)
            syncUIFromState();
    }

    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree& appState;
    StemAudioEngine& audioEngine;

    std::vector<StemSlotInfo> stemSlots;
    std::array<juce::Slider, StemAudioEngine::kNumStemSlots> stemVolSliders;
    juce::Rectangle<int> colorSwatchBounds[3];
    juce::Rectangle<int> leftColBounds, centerColBounds, rightColBounds;

    juce::ComboBox moodCombo;
    juce::Slider strobeSlider;
    juce::ToggleButton layeringToggle;
    juce::TextButton generateBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isUpdatingUI = false;
};