#pragma once
#include <JuceHeader.h>
#include <vector>

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
};