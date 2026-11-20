#pragma once
#include <JuceHeader.h>
#include <vector>

#include "DataStateKeys.h"

class StageRoleCatalog
{
public:
    enum class Category
    {
        AirWashBack,         // Aria: Wash Controluce (Tappeto base + Basso)
        AirParFront,         // Aria: Par/Spot Frontali (Melodia + Chitarre, illumina i musicisti)
        AirMovingBeam,       // Aria: Teste Mobili / Beam superiori
        FloorBackSilhouette, // Terra: Controluce pavimento / dietro batteria (Pulsazione Cassa/Basso)
        FloorSideWash,       // Terra: Tagli laterali radenti (Contrasto cromatico con l'aria)
        FloorMovingBeam,     // Terra: Teste Mobili su pedana/pavimento
        ImpactBlinder,       // Impatto: Blinder pubblico (Solo accenti brevi sul Drop)
        ImpactStrobe,        // Impatto: Strobo (Solo su densita' alta di Batteria/Blast)
        FxSmokeHazer,        // FX: Macchina del Fumo / Hazer (Pre-carica prima dei Drop/Climax)
        FxGeyserBurst        // FX: Geyser verticale LED (Solo burst brevi da 4 quarti sui Drop)
    };

    struct RoleDescriptor
    {
        Category category;
        juce::String standardId;
        juce::String displayName;
        bool isFloorUnit;
    };

    static const std::vector<RoleDescriptor>& getAllRoles()
    {
        static const std::vector<RoleDescriptor> catalog = {
            { Category::AirWashBack,         "Air - Wash Back",         "Aria: Wash Controluce (Fondale)",      false },
            { Category::AirParFront,         "Air - Par Front",         "Aria: Par / Spot Frontali (Band)",     false },
            { Category::AirMovingBeam,       "Air - Moving Beam",       "Aria: Teste Mobili / Beam",            false },
            { Category::FloorBackSilhouette, "Floor - Back Silhouette", "Terra: Controluce / Sotto-Batteria",   true  },
            { Category::FloorSideWash,       "Floor - Side Wash",       "Terra: Par / Barre Laterali (Taglio)", true  },
            { Category::FloorMovingBeam,     "Floor - Moving Beam",     "Terra: Teste Mobili a Terra",          true  },
            { Category::ImpactBlinder,       "Impact - Blinder",        "Impatto: Blinder Pubblico",            false },
            { Category::ImpactStrobe,        "Impact - Strobe",         "Impatto: Strobo",                      false },
            { Category::FxSmokeHazer,        "FX - Smoke Machine",      "FX: Macchina del Fumo / Hazer",        true  },
            { Category::FxGeyserBurst,       "FX - Geyser Burst",       "FX: Geyser Verticale (Fumo + LED)",    true  }
        };
        return catalog;
    }

    // 1. Popola un ComboBox in Tab 1 (o Tab 2) con le 10 categorie fisiche
    static void populateComboBox(juce::ComboBox& box, bool allowCustomSubgroups = true)
    {
        box.clear(juce::dontSendNotification);
        const auto& roles = getAllRoles();
        for (size_t i = 0; i < roles.size(); ++i)
            box.addItem(roles[i].displayName, static_cast<int>(i + 1));

        // Permette anche di rinominare un sottogruppo (es. "FX: Geyser Verticale DX")
        box.setEditableText(allowCustomSubgroups);
    }

    // 2. Sincronizza il ComboBox quando selezioni una Fixture nella Tab 1
    static void setComboSelectionFromRole(juce::ComboBox& box, const juce::String& savedRoleName)
    {
        juce::String trimmed = savedRoleName.trim();
        if (trimmed.isEmpty())
        {
            box.setSelectedId(1, juce::dontSendNotification);
            return;
        }

        const auto& roles = getAllRoles();
        for (size_t i = 0; i < roles.size(); ++i)
        {
            if (trimmed.equalsIgnoreCase(roles[i].displayName) ||
                trimmed.equalsIgnoreCase(roles[i].standardId))
            {
                box.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification);
                return;
            }
        }

        // Se era stato salvato un nome personalizzato, lo mostra comunque nel box
        box.setText(trimmed, juce::dontSendNotification);
    }

    // 3. Restituisce la stringa ruolo da salvare in StateKeys::RoleName
    static juce::String getRoleFromCombo(const juce::ComboBox& box)
    {
        int selId = box.getSelectedId();
        const auto& roles = getAllRoles();
        if (selId >= 1 && static_cast<size_t>(selId) <= roles.size())
            return roles[static_cast<size_t>(selId - 1)].displayName;

        juce::String customText = box.getText().trim();
        return customText.isNotEmpty() ? customText : roles[0].displayName;
    }

    // Identifica la categoria fisica precisa a partire dal RoleName o dal FixtureName
    static Category classifyRole(const juce::String& roleName, const juce::String& fixtureName = "")
    {
        for (const auto& desc : getAllRoles())
        {
            if (roleName.equalsIgnoreCase(desc.standardId) || roleName.equalsIgnoreCase(desc.displayName))
                return desc.category;
        }

        juce::String combined = (roleName + " " + fixtureName).toLowerCase();
        bool isFloor = combined.contains("floor") || combined.contains("terra") ||
            combined.contains("pedana") || combined.contains("basso") ||
            combined.contains("sotto") || combined.contains("ground");

        if (combined.contains("geyser") || combined.contains("co2") || combined.contains("jet"))
            return Category::FxGeyserBurst;

        if (combined.contains("fumo") || combined.contains("smoke") || combined.contains("fog") || combined.contains("haze"))
            return Category::FxSmokeHazer;

        if (combined.contains("blind") || combined.contains("accec") || combined.contains("audience") || combined.contains("crowd"))
            return Category::ImpactBlinder;

        if (combined.contains("strobo") || combined.contains("strobe") || combined.contains("flash"))
            return Category::ImpactStrobe;

        if (combined.contains("mov") || combined.contains("beam") || combined.contains("head") || combined.contains("scan"))
            return isFloor ? Category::FloorMovingBeam : Category::AirMovingBeam;

        if (isFloor)
        {
            if (combined.contains("side") || combined.contains("lat") || combined.contains("taglio") || combined.contains("bar"))
                return Category::FloorSideWash;
            return Category::FloorBackSilhouette;
        }

        if (combined.contains("front") || combined.contains("par") || combined.contains("spot") ||
            combined.contains("key") || combined.contains("ribalta"))
            return Category::AirParFront;

        return Category::AirWashBack;
    }

    struct StrobeHardwareSpec
    {
        int mode = 1;            // 0 = Dimmer Flash (0/255), 1 = Canale Separato (HW), 2 = Condiviso con Dimmer (Mix), 3 = Disabilitato
        int dimmerMaxVal = 255;  // Limite superiore del Dimmer se mode == 2 (es. 134)
        int openVal = 0;         // Valore DMX quando lo strobo e' spento (shutter aperto / no strobo)
        int minVal = 20;         // Valore DMX inizio strobo lento
        float minHz = 1.0f;      // Frequenza in Hz al valore minVal
        int maxVal = 245;        // Valore DMX fine strobo veloce
        float maxHz = 20.0f;     // Frequenza in Hz al valore maxVal
        

        static StrobeHardwareSpec fromFixtureNode(const juce::ValueTree& fix)
        {
            StrobeHardwareSpec s;
            if (!fix.isValid()) return s;

            int dimCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Dimmer"), 0));
            int strCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Strobe"), 0));

            int defaultMode = (strCh > 0 && strCh == dimCh) ? 2 : (strCh > 0 ? 1 : 0);
            s.mode = juce::jlimit(0, 3, static_cast<int>(fix.getProperty(StateKeys::StrobeMode, defaultMode)));
            s.dimmerMaxVal = static_cast<int>(fix.getProperty(StateKeys::DimmerMaxVal, s.mode == 2 ? 134 : 255));
            s.openVal = static_cast<int>(fix.getProperty(StateKeys::StrobeOpenVal, 0));
            s.minVal = static_cast<int>(fix.getProperty(StateKeys::StrobeMinVal, s.mode == 2 ? 135 : 20));
            s.minHz = static_cast<float>(static_cast<double>(fix.getProperty(StateKeys::StrobeMinHz, 1.0)));
            s.maxVal = static_cast<int>(fix.getProperty(StateKeys::StrobeMaxVal, s.mode == 2 ? 239 : 245));
            s.maxHz = static_cast<float>(static_cast<double>(fix.getProperty(StateKeys::StrobeMaxHz, 20.0)));
            return s;
        }

        // Converte una frequenza desiderata in Hz nel valore DMX esatto del faro
        int hzToDmx(float targetHz) const
        {
            if ((mode != 1 && mode != 2) || targetHz <= 0.01f)
                return openVal;

            float lowHz = juce::jmin(minHz, maxHz);
            float highHz = juce::jmax(minHz, maxHz);
            float clampedHz = juce::jlimit(lowHz, highHz, targetHz);

            float spanHz = juce::jmax(0.01f, maxHz - minHz);
            float ratio = juce::jlimit(0.0f, 1.0f, (clampedHz - minHz) / spanHz);

            int dmx = juce::roundToInt(static_cast<float>(minVal) + ratio * static_cast<float>(maxVal - minVal));
            return juce::jlimit(0, 255, dmx);
        }

        // Converte un valore DMX (0..255) nella frequenza reale in Hz per il Simulatore 2D
        float dmxToHz(int dmxVal) const
        {
            if (mode != 1 && mode != 2)
                return 0.0f;

            int lowDmx = juce::jmin(minVal, maxVal);
            int highDmx = juce::jmax(minVal, maxVal);
            if (dmxVal < lowDmx || dmxVal > highDmx)
                return 0.0f;

            float spanDmx = static_cast<float>(juce::jmax(1, std::abs(maxVal - minVal)));
            float ratio = static_cast<float>(std::abs(dmxVal - minVal)) / spanDmx;
            return minHz + ratio * (maxHz - minHz);
        }

        // Scala un dimmer normale (0..255) nel range consentito (evita di sconfinare nello strobo se mode == 2)
        int scaleNormalDimmer(int rawDimmer0to255) const
        {
            int maxOut = (mode == 2) ? juce::jlimit(1, 255, dimmerMaxVal) : 255;
            return juce::roundToInt((static_cast<float>(juce::jlimit(0, 255, rawDimmer0to255)) / 255.0f) * static_cast<float>(maxOut));
        }
    };

    struct PanTiltHardwareSpec
    {
        int panLeft = 85;      // Limite SX palco (DMX 0..255, puo' essere > panRight per invertire)
        int panRight = 170;    // Limite DX palco (DMX 0..255)
        int tiltLow = 128;     // Limite Giu / Band (DMX 0..255, puo' essere > tiltHigh per invertire)
        int tiltHigh = 195;    // Limite Su / Pubblico (DMX 0..255)
        bool swapPanTilt = false; // Montaggio su totem verticale: scambia assi Pan <-> Tilt
        bool hasPanChannel = false;
        bool hasTiltChannel = false;

        bool isSwapped() const noexcept { return swapPanTilt && hasPanChannel && hasTiltChannel; }

        int getPanCenter() const noexcept { return (panLeft + panRight) / 2; }
        int getTiltCenter() const noexcept { return (tiltLow + tiltHigh) / 2; }
        int getPanSpan() const noexcept { return std::abs(panRight - panLeft); }
        int getTiltSpan() const noexcept { return std::abs(tiltHigh - tiltLow); }

        // Mappa un movimento orizzontale normalizzato (0=SX, 0.5=Centro, 1=DX) nel valore DMX
        int mapStagePanToDmx(float norm0to1, bool mirrorPan = false) const noexcept
        {
            float u = juce::jlimit(0.0f, 1.0f, norm0to1);
            if (mirrorPan) u = 1.0f - u;
            return juce::jlimit(0, 255,
                juce::roundToInt(static_cast<float>(panLeft) + u * static_cast<float>(panRight - panLeft)));
        }

        // Mappa un movimento verticale normalizzato (0=Giu/Band, 0.5=Centro, 1=Su/Pubblico) nel valore DMX
        int mapStageTiltToDmx(float norm0to1) const noexcept
        {
            float u = juce::jlimit(0.0f, 1.0f, norm0to1);
            return juce::jlimit(0, 255,
                juce::roundToInt(static_cast<float>(tiltLow) + u * static_cast<float>(tiltHigh - tiltLow)));
        }

        // Trasla SX e DX insieme mantenendo identica l'ampiezza (es. +-42 step per base ruotata di 90 gradi)
        void shiftPan(int delta) noexcept
        {
            int minV = juce::jmin(panLeft, panRight);
            int maxV = juce::jmax(panLeft, panRight);
            int clampedDelta = juce::jlimit(-minV, 255 - maxV, delta);
            panLeft += clampedDelta;
            panRight += clampedDelta;
        }

        // 3 Preset rapidi di ampiezza (0=Stretto/Band, 1=Medio/Palco, 2=Ampio/Full)
        void applyQuickSpread(int presetId) noexcept
        {
            if (presetId == 0) { panLeft = 105; panRight = 150; tiltLow = 128; tiltHigh = 165; }
            else if (presetId == 1) { panLeft = 85;  panRight = 170; tiltLow = 128; tiltHigh = 195; }
            else { panLeft = 58;  panRight = 198; tiltLow = 128; tiltHigh = 228; }
        }

        static bool hasPanOrTiltChannels(const juce::ValueTree& fix)
        {
            if (!fix.isValid()) return false;
            int panCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Pan"), 0));
            int tiltCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Tilt"), 0));
            return (panCh > 0 || tiltCh > 0);
        }

        static PanTiltHardwareSpec fromFixtureNode(const juce::ValueTree& fix)
        {
            PanTiltHardwareSpec s;
            if (!fix.isValid()) return s;

            int panCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Pan"), 0));
            int tiltCh = static_cast<int>(fix.getProperty(StateKeys::getMappingKey("Tilt"), 0));
            s.hasPanChannel = (panCh > 0);
            s.hasTiltChannel = (tiltCh > 0);

            juce::String rName = fix.getProperty(StateKeys::RoleName, "").toString();
            juce::String fName = fix.getProperty(StateKeys::FixtureName, "").toString();
            auto cat = classifyRole(rName, fName);

            const bool defaultSwap = (cat == Category::FloorSideWash) && s.hasPanChannel && s.hasTiltChannel;
            s.swapPanTilt = fix.hasProperty(StateKeys::SwapPanTilt)
                ? static_cast<bool>(fix.getProperty(StateKeys::SwapPanTilt, false))
                : defaultSwap;

            if (fix.hasProperty(StateKeys::PanMin) && fix.hasProperty(StateKeys::PanMax) &&
                fix.hasProperty(StateKeys::TiltMin) && fix.hasProperty(StateKeys::TiltMax))
            {
                s.panLeft = juce::jlimit(0, 255, static_cast<int>(fix.getProperty(StateKeys::PanMin, 85)));
                s.panRight = juce::jlimit(0, 255, static_cast<int>(fix.getProperty(StateKeys::PanMax, 170)));
                s.tiltLow = juce::jlimit(0, 255, static_cast<int>(fix.getProperty(StateKeys::TiltMin, 128)));
                s.tiltHigh = juce::jlimit(0, 255, static_cast<int>(fix.getProperty(StateKeys::TiltMax, 195)));

                // Retrocompatibilita': se il vecchio .venue aveva InvertPan/InvertTilt attivi, li incorpora negli estremi
                if (static_cast<bool>(fix.getProperty(StateKeys::InvertPan, false)))
                    std::swap(s.panLeft, s.panRight);
                if (static_cast<bool>(fix.getProperty(StateKeys::InvertTilt, false)))
                    std::swap(s.tiltLow, s.tiltHigh);
            }
            else
            {
                // Retrocompatibilita' da file .venue ancora piu' vecchi (Home / Offset / Range)
                int pCenter = static_cast<int>(fix.getProperty(StateKeys::PanHome, 128))
                    + static_cast<int>(fix.getProperty(StateKeys::PanOffset, 0));
                int pHalf = juce::jmax(10, static_cast<int>(fix.getProperty(StateKeys::PanRange, 85))) / 2;
                s.panLeft = juce::jlimit(0, 255, pCenter - pHalf);
                s.panRight = juce::jlimit(0, 255, pCenter + pHalf);

                int tCenter = static_cast<int>(fix.getProperty(StateKeys::TiltHome, 160))
                    + static_cast<int>(fix.getProperty(StateKeys::TiltOffset, 0));
                int tHalf = juce::jmax(10, static_cast<int>(fix.getProperty(StateKeys::TiltRange, 66))) / 2;
                s.tiltLow = juce::jlimit(0, 255, tCenter - tHalf);
                s.tiltHigh = juce::jlimit(0, 255, tCenter + tHalf);
            }

            return s;
        }

        void writeToFixtureNode(juce::ValueTree& fix) const
        {
            if (!fix.isValid()) return;
            fix.setProperty(StateKeys::PanMin, juce::jlimit(0, 255, panLeft), nullptr);
            fix.setProperty(StateKeys::PanMax, juce::jlimit(0, 255, panRight), nullptr);
            fix.setProperty(StateKeys::TiltMin, juce::jlimit(0, 255, tiltLow), nullptr);
            fix.setProperty(StateKeys::TiltMax, juce::jlimit(0, 255, tiltHigh), nullptr);
            fix.setProperty(StateKeys::SwapPanTilt, swapPanTilt, nullptr);
            // Azzera i vecchi flag di inversione per evitare doppie inversioni ai caricamenti successivi
            fix.setProperty(StateKeys::InvertPan, false, nullptr);
            fix.setProperty(StateKeys::InvertTilt, false, nullptr);
        }
    };
};