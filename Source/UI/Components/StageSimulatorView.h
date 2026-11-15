#pragma once
#include <JuceHeader.h>
#include "../../Model/DataStateKeys.h"
#include "../../Model/BeatTimeMap.h"
#include "../../Model/StageRoleCatalog.h"
#include "../../Audio/StemAudioEngine.h"
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

class StageSimulatorView : public juce::Component,
    private juce::Timer
{
public:
    std::function<void()> onToggleExpandRequested;

    StageSimulatorView(juce::ValueTree& state,
        StemAudioEngine& engine,
        BeatTimeMap& beatMap)
        : appState(state), audioEngine(engine), beatTimeMap(beatMap)
    {
        expandBtn.setButtonText("Ingrandisci Palco");
        expandBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff272732));
        expandBtn.onClick = [this]() {
            isExpanded = !isExpanded;
            expandBtn.setButtonText(isExpanded ? "Dividi Vista (Palco + Editor)" : "Ingrandisci Palco");
            if (onToggleExpandRequested)
                onToggleExpandRequested();
            };
        addAndMakeVisible(expandBtn);

        startTimerHz(60);
    }

    ~StageSimulatorView() override
    {
        stopTimer();
    }

    bool getIsExpanded() const noexcept { return isExpanded; }

    void resized() override
    {
        auto topBar = getLocalBounds().removeFromTop(28).reduced(8, 3);
        expandBtn.setBounds(topBar.removeFromRight(175));
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // 1. Sfondo generale del teatro / palco
        g.fillAll(juce::Colour(0xff09090d));

        auto stageRect = bounds.reduced(8.0f, 6.0f);
        stageRect.removeFromTop(24.0f); // Spazio barra info superiore

        // Intestazione con Scena Attiva, Quarto e Livello Nebbia
        drawHeaderHud(g, bounds.withHeight(26.0f));

        // 2. Fondale scuro con gradiente ambientale dovuto ai Wash e al Fumo
        drawStageBackdrop(g, stageRect);

        // 3. Struttura fisica: Americana superiore (Truss) e Palco (Floor)
        const float trussY = stageRect.getY() + stageRect.getHeight() * 0.11f;
        const float floorY = stageRect.getY() + stageRect.getHeight() * 0.86f;

        drawTrussAndFloorStructure(g, stageRect, trussY, floorY);

        // 4. Costruisce la lista dei fari da renderizzare (dalla VENUE o rig di fallback)
        std::vector<VisualFixture> fixtures = buildVisualFixtures(stageRect, trussY, floorY);

        // 5. Livello 1 Luci: Controluce Aria e Terra (dietro le sagome della band)
        for (const auto& vf : fixtures)
        {
            if (vf.category == StageRoleCatalog::Category::AirWashBack ||
                vf.category == StageRoleCatalog::Category::FloorBackSilhouette)
            {
                drawFixtureLightEffect(g, vf, stageRect, trussY, floorY);
            }
        }

        // 6. Sagome della Band (Batteria su pedana + 3 musicisti in front-line)
        //    Vengono illuminate frontalmente se AirParFront o FloorSideWash sono attivi
        drawBandSilhouettes(g, stageRect, floorY, fixtures);

        // 7. Livello 2 Luci: Fasci Teste Mobili, Tagli Laterali, Par Frontali, Geyser e Blinder
        for (const auto& vf : fixtures)
        {
            if (vf.category != StageRoleCatalog::Category::AirWashBack &&
                vf.category != StageRoleCatalog::Category::FloorBackSilhouette)
            {
                drawFixtureLightEffect(g, vf, stageRect, trussY, floorY);
            }
        }

        // 8. Atmosfera Nebbia / Hazer stratificata sul palco
        if (currentFogDensity > 0.01f)
            drawFogOverlay(g, stageRect, floorY);

        // 9. Corpo fisico dei fari (icone hardware e spie LED)
        for (const auto& vf : fixtures)
            drawFixtureHardwareBody(g, vf);

        // Cornice estetica del simulatore
        g.setColour(juce::Colour(0xff2a2a38));
        g.drawRoundedRectangle(stageRect, 6.0f, 1.5f);
    }

private:
    struct RoleRuntimeState
    {
        float dimmer = 0.0f; // 0.0 -> 1.0
        float red = -1.0f;
        float green = -1.0f;
        float blue = -1.0f;
        float strobe = 0.0f; // 0.0 -> 1.0
        float pan = 0.5f; // 0.0 -> 1.0 (0.5 = centro)
        float tilt = 0.5f; // 0.0 -> 1.0
        juce::Colour fallbackColor{ 0xff1e3a8a };

        juce::Colour getEffectiveColour() const
        {
            if (red >= 0.0f || green >= 0.0f || blue >= 0.0f)
            {
                juce::uint8 r = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>((red >= 0.0f ? red : 0.0f) * 255.0f)));
                juce::uint8 g = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>((green >= 0.0f ? green : 0.0f) * 255.0f)));
                juce::uint8 b = static_cast<juce::uint8>(juce::jlimit(0, 255, static_cast<int>((blue >= 0.0f ? blue : 0.0f) * 255.0f)));
                if (r > 5 || g > 5 || b > 5)
                    return juce::Colour(r, g, b);
            }
            return fallbackColor;
        }
    };

    struct VisualFixture
    {
        juce::String name;
        juce::String roleName;
        StageRoleCatalog::Category category;
        juce::Point<float> pos;
        int indexInCategory = 0;
        int totalInCategory = 1;
        RoleRuntimeState state;
    };

    void timerCallback() override
    {
        double posSec = audioEngine.getCurrentPositionSeconds();
        currentBeat = beatTimeMap.secondsToBeat(posSec);

        evaluateActiveSceneAtBeat(currentBeat);
        repaint();
    }

    // Valuta tutti i ROLE_COMMAND della scena attiva al quarto corrente
    void evaluateActiveSceneAtBeat(double beat)
    {
        roleStates.clear();
        activeSceneName = "Nessuna Scena Attiva (Blackout)";

        auto showNode = appState.getChildWithName(StateKeys::ShowNode);
        if (!showNode.isValid())
        {
            updateFogInertia(0.0f);
            return;
        }

        juce::ValueTree activeScene;
        double sceneProgress = 0.0;

        for (int i = 0; i < showNode.getNumChildren(); ++i)
        {
            auto scene = showNode.getChild(i);
            if (!scene.hasType(StateKeys::Scene))
                continue;

            double startB = static_cast<double>(scene.getProperty(StateKeys::StartBeat, 1.0));
            double durB = juce::jmax(0.25, static_cast<double>(scene.getProperty(StateKeys::DurationBeats, 4.0)));

            if (beat >= startB && beat < (startB + durB))
            {
                activeScene = scene;
                sceneProgress = juce::jlimit(0.0, 1.0, (beat - startB) / durB);
                activeSceneName = scene.getProperty(StateKeys::SceneName, "Scena").toString();
                break;
            }
        }

        if (!activeScene.isValid())
        {
            updateFogInertia(0.0f);
            return;
        }

        float smokeCommandOut = 0.0f;

        for (int c = 0; c < activeScene.getNumChildren(); ++c)
        {
            auto cmd = activeScene.getChild(c);
            if (!cmd.hasType(StateKeys::RoleCommand))
                continue;

            juce::String roleName = cmd.getProperty(StateKeys::TargetRole, "").toString().trim();
            juce::String param = cmd.getProperty(StateKeys::TargetParam, "Dimmer").toString().trim();
            float startVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::StartValue, 0))) / 255.0f;
            float endVal = static_cast<float>(static_cast<int>(cmd.getProperty(StateKeys::EndValue, 255))) / 255.0f;
            juce::String curve = cmd.getProperty(StateKeys::CurveType, "Linear").toString();
            juce::String colHex = cmd.getProperty(StateKeys::CommandColor, "ff1e3a8a").toString();

            float val = computeCurveValue(curve, startVal, endVal, static_cast<float>(sceneProgress), beat);

            auto& st = roleStates[roleName];
            st.fallbackColor = juce::Colour::fromString(colHex);

            if (param.equalsIgnoreCase("Dimmer"))       st.dimmer = val;
            else if (param.equalsIgnoreCase("Red"))     st.red = val;
            else if (param.equalsIgnoreCase("Green"))   st.green = val;
            else if (param.equalsIgnoreCase("Blue"))    st.blue = val;
            else if (param.equalsIgnoreCase("Strobe"))  st.strobe = val;
            else if (param.equalsIgnoreCase("Pan"))     st.pan = val;
            else if (param.equalsIgnoreCase("Tilt"))    st.tilt = val;

            if (StageRoleCatalog::classifyRole(roleName) == StageRoleCatalog::Category::FxSmokeHazer &&
                param.equalsIgnoreCase("Dimmer"))
            {
                smokeCommandOut = juce::jmax(smokeCommandOut, val);
            }
        }

        // Effetto shutter se un ruolo ha il canale Strobe attivo
        for (auto& pair : roleStates)
        {
            if (pair.second.strobe > 0.05f && pair.second.dimmer > 0.01f)
            {
                double freqHz = 4.0 + pair.second.strobe * 14.0;
                double phase = std::fmod(juce::Time::getMillisecondCounterHiRes() * 0.001 * freqHz, 1.0);
                if (phase > 0.35)
                    pair.second.dimmer *= 0.08f;
            }
        }

        updateFogInertia(smokeCommandOut);
    }

    static float computeCurveValue(const juce::String& curve,
        float startVal,
        float endVal,
        float progress,
        double currentBeatPos)
    {
        if (curve.equalsIgnoreCase("Step"))
            return (progress < 0.99f) ? startVal : endVal;

        if (curve.equalsIgnoreCase("Sine"))
        {
            // Pulsazione LFO sincronizzata al quarto musicale tra startVal ed endVal
            double beatPhase = std::fmod(juce::jmax(0.0, currentBeatPos - 1.0), 1.0);
            float lfo = 0.5f * (1.0f - static_cast<float>(std::cos(beatPhase * juce::MathConstants<double>::twoPi)));
            return startVal + (endVal - startVal) * lfo;
        }

        if (curve.equalsIgnoreCase("Square"))
        {
            // Flash ritmico in sedicesimi (4 lampi per quarto)
            double subBeat = std::fmod(juce::jmax(0.0, currentBeatPos - 1.0) * 4.0, 1.0);
            return (subBeat < 0.42) ? endVal : startVal;
        }

        // Default: Linear
        return startVal + (endVal - startVal) * progress;
    }

    // Simula l'inerzia fisica della nebbia sul palco (sale quando la macchina spara, scende lentamente)
    void updateFogInertia(float smokeMachineOutput)
    {
        if (smokeMachineOutput > 0.02f)
        {
            currentFogDensity = juce::jmin(1.0f, currentFogDensity + smokeMachineOutput * 0.0045f);
        }
        else
        {
            currentFogDensity = juce::jmax(0.08f, currentFogDensity - 0.0008f);
        }
    }

    std::vector<VisualFixture> buildVisualFixtures(const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY) const
    {
        struct RawItem
        {
            juce::String name;
            juce::String role;
            StageRoleCatalog::Category cat;
        };

        std::vector<RawItem> rawList;
        auto venueNode = appState.getChildWithName(StateKeys::VenueNode);

        if (venueNode.isValid() && venueNode.getNumChildren() > 0)
        {
            for (int i = 0; i < venueNode.getNumChildren(); ++i)
            {
                auto fix = venueNode.getChild(i);
                if (!fix.hasType(StateKeys::Fixture))
                    continue;

                juce::String fName = fix.getProperty(StateKeys::FixtureName, "Faro").toString();
                juce::String rName = fix.getProperty(StateKeys::RoleName, "").toString().trim();
                if (rName.isEmpty()) rName = fName;

                rawList.push_back({ fName, rName, StageRoleCatalog::classifyRole(rName, fName) });
            }
        }

        // Se la Tab 1 e' vuota, popola un rig simmetrico completo di default
        if (rawList.empty())
        {
            using Cat = StageRoleCatalog::Category;
            rawList = {
                { "Wash Back SX",   "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack },
                { "Wash Back CX",   "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack },
                { "Wash Back DX",   "Aria: Wash Controluce (Fondale)",    Cat::AirWashBack },
                { "Par Front SX",   "Aria: Par / Spot Frontali (Band)",   Cat::AirParFront },
                { "Par Front DX",   "Aria: Par / Spot Frontali (Band)",   Cat::AirParFront },
                { "Blinder SX",     "Impatto: Blinder Pubblico",          Cat::ImpactBlinder },
                { "Blinder DX",     "Impatto: Blinder Pubblico",          Cat::ImpactBlinder },
                { "Strobo Truss",   "Impatto: Strobo",                    Cat::ImpactStrobe },
                { "Floor Back SX",  "Terra: Controluce / Sotto-Batteria", Cat::FloorBackSilhouette },
                { "Floor Back DX",  "Terra: Controluce / Sotto-Batteria", Cat::FloorBackSilhouette },
                { "Geyser SX",      "FX: Geyser Verticale (Fumo + LED)",  Cat::FxGeyserBurst },
                { "Geyser DX",      "FX: Geyser Verticale (Fumo + LED)",  Cat::FxGeyserBurst },
                { "Hazer Palco",    "FX: Macchina del Fumo / Hazer",      Cat::FxSmokeHazer }
            };
        }

        // Separa fari in Aria, fari a Terra e Tagli Laterali per distribuirli con ordine sul palco
        std::vector<size_t> airIndices, floorIndices, sideIndices;
        for (size_t i = 0; i < rawList.size(); ++i)
        {
            auto c = rawList[i].cat;
            if (c == StageRoleCatalog::Category::FloorSideWash)
                sideIndices.push_back(i);
            else if (c == StageRoleCatalog::Category::FloorBackSilhouette ||
                c == StageRoleCatalog::Category::FloorMovingBeam ||
                c == StageRoleCatalog::Category::FxGeyserBurst ||
                c == StageRoleCatalog::Category::FxSmokeHazer)
                floorIndices.push_back(i);
            else
                airIndices.push_back(i);
        }

        std::vector<VisualFixture> result(rawList.size());

        auto assignState = [this](VisualFixture& vf) {
            auto it = roleStates.find(vf.roleName);
            if (it != roleStates.end())
            {
                vf.state = it->second;
                return;
            }
            // Fallback: cerca per categoria se i nomi hanno leggere differenze
            for (const auto& pair : roleStates)
            {
                if (StageRoleCatalog::classifyRole(pair.first) == vf.category)
                {
                    vf.state = pair.second;
                    return;
                }
            }
            };

        // Posizionamento sull'Americana (Aria)
        for (size_t k = 0; k < airIndices.size(); ++k)
        {
            size_t idx = airIndices[k];
            float ratio = static_cast<float>(k + 1) / static_cast<float>(airIndices.size() + 1);
            float x = stageRect.getX() + stageRect.getWidth() * (0.08f + 0.84f * ratio);

            VisualFixture vf;
            vf.name = rawList[idx].name;
            vf.roleName = rawList[idx].role;
            vf.category = rawList[idx].cat;
            vf.pos = { x, trussY };
            vf.indexInCategory = static_cast<int>(k);
            vf.totalInCategory = static_cast<int>(airIndices.size());
            assignState(vf);
            result[idx] = vf;
        }

        // Posizionamento a Terra (Pedana / Pavimento)
        for (size_t k = 0; k < floorIndices.size(); ++k)
        {
            size_t idx = floorIndices[k];
            float ratio = static_cast<float>(k + 1) / static_cast<float>(floorIndices.size() + 1);
            float x = stageRect.getX() + stageRect.getWidth() * (0.12f + 0.76f * ratio);

            VisualFixture vf;
            vf.name = rawList[idx].name;
            vf.roleName = rawList[idx].role;
            vf.category = rawList[idx].cat;
            vf.pos = { x, floorY };
            vf.indexInCategory = static_cast<int>(k);
            vf.totalInCategory = static_cast<int>(floorIndices.size());
            assignState(vf);
            result[idx] = vf;
        }

        // Posizionamento Tagli Laterali (Quinte SX e DX)
        for (size_t k = 0; k < sideIndices.size(); ++k)
        {
            size_t idx = sideIndices[k];
            bool isLeft = (k % 2 == 0);
            float x = isLeft ? (stageRect.getX() + 26.0f) : (stageRect.getRight() - 26.0f);
            float y = floorY - 35.0f - static_cast<float>(k / 2) * 45.0f;

            VisualFixture vf;
            vf.name = rawList[idx].name;
            vf.roleName = rawList[idx].role;
            vf.category = rawList[idx].cat;
            vf.pos = { x, y };
            vf.indexInCategory = static_cast<int>(k);
            vf.totalInCategory = static_cast<int>(sideIndices.size());
            assignState(vf);
            result[idx] = vf;
        }

        return result;
    }

    void drawHeaderHud(juce::Graphics& g, const juce::Rectangle<float>& hudRect)
    {
        g.setColour(juce::Colour(0xff94a3b8));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));

        juce::String status = "SIMULATORE PALCO 2D (60 FPS)   |   Quarto: "
            + juce::String(currentBeat, 2)
            + "   |   Scena: " + activeSceneName
            + "   |   Nebbia Palco: " + juce::String(static_cast<int>(currentFogDensity * 100.0f)) + "%";

        g.drawText(status, hudRect.reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void drawStageBackdrop(juce::Graphics& g, const juce::Rectangle<float>& stageRect)
    {
        juce::ColourGradient bgGrad(juce::Colour(0xff10121a), stageRect.getCentreX(), stageRect.getY(),
            juce::Colour(0xff07070b), stageRect.getCentreX(), stageRect.getBottom(), false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(stageRect, 6.0f);
    }

    void drawTrussAndFloorStructure(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        // Americana Superiore (Truss in alluminio scuro con traliccio)
        float leftX = stageRect.getX() + 20.0f;
        float rightX = stageRect.getRight() - 20.0f;

        g.setColour(juce::Colour(0xff2d3142));
        g.drawLine(leftX, trussY - 4.0f, rightX, trussY - 4.0f, 2.0f);
        g.drawLine(leftX, trussY + 4.0f, rightX, trussY + 4.0f, 2.0f);

        g.setColour(juce::Colour(0xff222533));
        for (float x = leftX; x < rightX - 18.0f; x += 18.0f)
        {
            g.drawLine(x, trussY - 4.0f, x + 9.0f, trussY + 4.0f, 1.0f);
            g.drawLine(x + 9.0f, trussY + 4.0f, x + 18.0f, trussY - 4.0f, 1.0f);
        }

        // Pavimento del Palco in prospettiva
        juce::Rectangle<float> floorDeck(stageRect.getX() + 4.0f, floorY,
            stageRect.getWidth() - 8.0f, stageRect.getBottom() - floorY - 4.0f);
        juce::ColourGradient floorGrad(juce::Colour(0xff181922), floorDeck.getCentreX(), floorY,
            juce::Colour(0xff0d0e14), floorDeck.getCentreX(), floorDeck.getBottom(), false);
        g.setGradientFill(floorGrad);
        g.fillRect(floorDeck);

        g.setColour(juce::Colour(0xff353849));
        g.drawLine(stageRect.getX() + 4.0f, floorY, stageRect.getRight() - 4.0f, floorY, 2.0f);
    }

    void drawBandSilhouettes(juce::Graphics& g,
        const juce::Rectangle<float>& stageRect,
        float floorY,
        const std::vector<VisualFixture>& fixtures)
    {
        // Calcola quanta luce frontale (AirParFront / FloorSideWash) sta illuminando i musicisti
        float frontIllumination = 0.0f;
        juce::Colour frontTint = juce::Colours::white;

        for (const auto& vf : fixtures)
        {
            if ((vf.category == StageRoleCatalog::Category::AirParFront ||
                vf.category == StageRoleCatalog::Category::FloorSideWash) &&
                vf.state.dimmer > frontIllumination)
            {
                frontIllumination = vf.state.dimmer;
                frontTint = vf.state.getEffectiveColour();
            }
        }

        juce::Colour baseSilhouette(0xff0b0c10);
        juce::Colour litBody = baseSilhouette.interpolatedWith(frontTint, frontIllumination * 0.45f);
        juce::Colour rimCol = juce::Colour(0xff2c3040).interpolatedWith(frontTint, frontIllumination * 0.7f);

        float cx = stageRect.getCentreX();
        float stageH = floorY - stageRect.getY();
        float scale = juce::jlimit(0.6f, 1.5f, stageH / 320.0f);

        // 1. Pedana Batteria centrale + Silhouette Batteria
        juce::Rectangle<float> drumRiser(cx - 55.0f * scale, floorY - 18.0f * scale, 110.0f * scale, 18.0f * scale);
        g.setColour(juce::Colour(0xff14161f));
        g.fillRect(drumRiser);
        g.setColour(rimCol.withAlpha(0.5f));
        g.drawRect(drumRiser, 1.0f);

        // Cassa batteria e batterista
        g.setColour(litBody);
        g.fillEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale);
        g.fillEllipse(cx - 9.0f * scale, drumRiser.getY() - 62.0f * scale, 18.0f * scale, 18.0f * scale);
        g.setColour(rimCol);
        g.drawEllipse(cx - 18.0f * scale, drumRiser.getY() - 34.0f * scale, 36.0f * scale, 34.0f * scale, 1.2f);

        // 2. Tre sagome front-line (SX, Centro, DX)
        float positionsX[3] = {
            stageRect.getX() + stageRect.getWidth() * 0.27f,
            cx,
            stageRect.getX() + stageRect.getWidth() * 0.73f
        };

        for (int i = 0; i < 3; ++i)
        {
            float px = positionsX[i];
            float bodyH = 54.0f * scale;
            float bodyW = 22.0f * scale;

            juce::Path memberPath;
            memberPath.addRoundedRectangle(px - bodyW * 0.5f, floorY - bodyH, bodyW, bodyH, 6.0f * scale);
            memberPath.addEllipse(px - 8.0f * scale, floorY - bodyH - 18.0f * scale, 16.0f * scale, 16.0f * scale);

            g.setColour(litBody);
            g.fillPath(memberPath);
            g.setColour(rimCol.withAlpha(0.65f));
            g.strokePath(memberPath, juce::PathStrokeType(1.2f));
        }
    }

    void drawFixtureLightEffect(juce::Graphics& g,
        const VisualFixture& vf,
        const juce::Rectangle<float>& stageRect,
        float trussY,
        float floorY)
    {
        const float dim = vf.state.dimmer;
        if (dim <= 0.01f)
            return;

        using Cat = StageRoleCatalog::Category;
        juce::Colour col = vf.state.getEffectiveColour();

        // La nebbia sul palco esalta la visibilita' volumetrica dei fasci luminosi
        float fogBoost = 0.75f + 0.45f * currentFogDensity;
        float beamAlpha = juce::jlimit(0.0f, 0.90f, dim * 0.55f * fogBoost);

        switch (vf.category)
        {
        case Cat::AirWashBack:
        case Cat::AirParFront:
        {
            float spreadW = (vf.category == Cat::AirWashBack)
                ? stageRect.getWidth() * 0.26f
                : stageRect.getWidth() * 0.16f;
            drawLightCone(g, vf.pos, { vf.pos.x, floorY }, 12.0f, spreadW, col, beamAlpha);
            break;
        }

        case Cat::FloorBackSilhouette:
        {
            // Cono largo dal pavimento verso l'alto (controluce drammatico da terra)
            float topY = trussY + (floorY - trussY) * 0.12f;
            float spreadW = stageRect.getWidth() * 0.24f;
            drawLightCone(g, vf.pos, { vf.pos.x, topY }, 14.0f, spreadW, col, beamAlpha * 1.15f);
            break;
        }

        case Cat::FloorSideWash:
        {
            // Taglio laterale radente che attraversa il palco orizzontalmente
            bool isLeft = (vf.pos.x < stageRect.getCentreX());
            float targetX = isLeft ? (stageRect.getX() + stageRect.getWidth() * 0.70f)
                : (stageRect.getX() + stageRect.getWidth() * 0.30f);
            drawLightCone(g, vf.pos, { targetX, floorY - 30.0f }, 10.0f, 95.0f, col, beamAlpha);
            break;
        }

        case Cat::AirMovingBeam:
        case Cat::FloorMovingBeam:
        {
            bool isFloor = (vf.category == Cat::FloorMovingBeam);
            // Oscilla dinamicamente in base a Pan/Tilt + sfasamento tra fari pari/dispari
            float panOffset = (vf.state.pan - 0.5f) * stageRect.getWidth() * 0.55f;
            if (vf.indexInCategory % 2 == 1)
                panOffset = -panOffset;

            float targetX = juce::jlimit(stageRect.getX() + 20.0f,
                stageRect.getRight() - 20.0f,
                vf.pos.x + panOffset);
            float targetY = isFloor ? trussY : floorY;
            drawLightCone(g, vf.pos, { targetX, targetY }, 6.0f, 52.0f, col, juce::jmin(0.95f, beamAlpha * 1.35f));
            break;
        }

        case Cat::ImpactBlinder:
        {
            // Accecatore frontale verso il pubblico: doppio alone radiale caldo/bianco
            juce::Colour blindCol = juce::Colour(0xfffffbeb).interpolatedWith(col, 0.3f);
            float radius = 95.0f * dim + 25.0f;
            juce::ColourGradient grad(blindCol.withAlpha(juce::jmin(1.0f, dim * 0.95f)), vf.pos.x, vf.pos.y,
                blindCol.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
            g.setGradientFill(grad);
            g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);

            // Raggi incrociati lens-flare
            g.setColour(juce::Colours::white.withAlpha(dim * 0.85f));
            g.drawLine(vf.pos.x - radius * 0.7f, vf.pos.y, vf.pos.x + radius * 0.7f, vf.pos.y, 2.5f);
            g.drawLine(vf.pos.x, vf.pos.y - radius * 0.7f, vf.pos.x, vf.pos.y + radius * 0.7f, 2.5f);
            break;
        }

        case Cat::ImpactStrobe:
        {
            // Flash strobo ad alta intensita' su tutto il palco
            float radius = 130.0f * dim;
            juce::ColourGradient grad(juce::Colours::white.withAlpha(dim * 0.90f), vf.pos.x, vf.pos.y,
                col.withAlpha(0.0f), vf.pos.x + radius, vf.pos.y, true);
            g.setGradientFill(grad);
            g.fillEllipse(vf.pos.x - radius, vf.pos.y - radius, radius * 2.0f, radius * 2.0f);

            if (dim > 0.6f)
            {
                g.setColour(juce::Colours::white.withAlpha((dim - 0.5f) * 0.22f));
                g.fillRoundedRectangle(stageRect, 6.0f);
            }
            break;
        }

        case Cat::FxGeyserBurst:
        {
            // Colonna di vapore CO2/fumo verticale ad alta pressione illuminata a LED
            float jetHeight = (floorY - trussY) * 0.82f * dim;
            int numPuffs = 9;
            for (int p = 0; p < numPuffs; ++p)
            {
                float ratio = static_cast<float>(p) / static_cast<float>(numPuffs);
                float py = floorY - ratio * jetHeight;
                float puffRadius = 10.0f + ratio * 34.0f * dim;
                float puffAlpha = (1.0f - ratio * 0.75f) * dim * 0.75f;

                // Il getto e' intensamente colorato alla base e diventa vapore bianco-grigio in cima
                juce::Colour puffCol = col.interpolatedWith(juce::Colour(0xffe2e8f0), ratio * 0.65f);
                g.setColour(puffCol.withAlpha(puffAlpha));
                g.fillEllipse(vf.pos.x - puffRadius, py - puffRadius * 0.7f, puffRadius * 2.0f, puffRadius * 1.4f);
            }
            break;
        }

        case Cat::FxSmokeHazer:
        {
            // Sbuffo locale all'uscita dell'ugello della macchina del fumo
            float sbuffW = 80.0f * dim;
            juce::ColourGradient smokeGrad(juce::Colour(0xffcbd5e1).withAlpha(dim * 0.45f), vf.pos.x, vf.pos.y - 10.0f,
                juce::Colour(0xffcbd5e1).withAlpha(0.0f), vf.pos.x + sbuffW, vf.pos.y - 35.0f, true);
            g.setGradientFill(smokeGrad);
            g.fillEllipse(vf.pos.x - sbuffW, vf.pos.y - 45.0f, sbuffW * 2.0f, 50.0f);
            break;
        }
        }
    }

    static void drawLightCone(juce::Graphics& g,
        juce::Point<float> origin,
        juce::Point<float> target,
        float startWidth,
        float endWidth,
        juce::Colour colour,
        float alpha)
    {
        float dx = target.x - origin.x;
        float dy = target.y - origin.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) return;

        // Vettore perpendicolare normalizzato per calcolare i bordi del cono
        float nx = -dy / len;
        float ny = dx / len;

        juce::Path cone;
        cone.startNewSubPath(origin.x + nx * (startWidth * 0.5f), origin.y + ny * (startWidth * 0.5f));
        cone.lineTo(origin.x - nx * (startWidth * 0.5f), origin.y - ny * (startWidth * 0.5f));
        cone.lineTo(target.x - nx * (endWidth * 0.5f), target.y - ny * (endWidth * 0.5f));
        cone.lineTo(target.x + nx * (endWidth * 0.5f), target.y + ny * (endWidth * 0.5f));
        cone.closeSubPath();

        juce::ColourGradient grad(colour.withAlpha(alpha), origin.x, origin.y,
            colour.withAlpha(0.02f), target.x, target.y, false);
        g.setGradientFill(grad);
        g.fillPath(cone);

        // Nucleo centrale piu' luminoso (hotspot del fascio)
        juce::Path core;
        core.startNewSubPath(origin.x + nx * (startWidth * 0.2f), origin.y + ny * (startWidth * 0.2f));
        core.lineTo(origin.x - nx * (startWidth * 0.2f), origin.y - ny * (startWidth * 0.2f));
        core.lineTo(target.x - nx * (endWidth * 0.18f), target.y - ny * (endWidth * 0.18f));
        core.lineTo(target.x + nx * (endWidth * 0.18f), target.y + ny * (endWidth * 0.18f));
        core.closeSubPath();

        juce::ColourGradient coreGrad(colour.brighter(0.6f).withAlpha(alpha * 0.65f), origin.x, origin.y,
            colour.withAlpha(0.0f), target.x, target.y, false);
        g.setGradientFill(coreGrad);
        g.fillPath(core);
    }

    void drawFogOverlay(juce::Graphics& g, const juce::Rectangle<float>& stageRect, float floorY)
    {
        float fogTop = stageRect.getY() + stageRect.getHeight() * 0.30f;
        juce::Rectangle<float> fogArea(stageRect.getX() + 6.0f, fogTop,
            stageRect.getWidth() - 12.0f, floorY - fogTop);

        float alpha = currentFogDensity * 0.22f;
        juce::ColourGradient fogGrad(juce::Colour(0xff94a3b8).withAlpha(0.0f), fogArea.getCentreX(), fogTop,
            juce::Colour(0xffcbd5e1).withAlpha(alpha), fogArea.getCentreX(), floorY, false);
        g.setGradientFill(fogGrad);
        g.fillRect(fogArea);
    }

    static void drawFixtureHardwareBody(juce::Graphics& g, const VisualFixture& vf)
    {
        juce::Rectangle<float> box(vf.pos.x - 8.0f, vf.pos.y - 6.0f, 16.0f, 12.0f);
        g.setColour(juce::Colour(0xff1e202b));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(juce::Colour(0xff475569));
        g.drawRoundedRectangle(box, 3.0f, 1.0f);

        // Lente LED accesa col colore e dimmer correnti del faro
        juce::Colour lensCol = vf.state.getEffectiveColour();
        float dim = vf.state.dimmer;
        g.setColour(dim > 0.02f ? lensCol.withAlpha(0.35f + 0.65f * dim) : juce::Colour(0xff0f172a));
        g.fillEllipse(vf.pos.x - 4.5f, vf.pos.y - 4.5f, 9.0f, 9.0f);

        // Etichetta compatta sotto/sopra il faro
        g.setColour(juce::Colour(0xff94a3b8).withAlpha(0.80f));
        g.setFont(juce::FontOptions(9.5f));
        float textY = (vf.pos.y < 150.0f) ? (vf.pos.y - 18.0f) : (vf.pos.y + 7.0f);
        g.drawText(vf.name, static_cast<int>(vf.pos.x - 42.0f), static_cast<int>(textY),
            84, 12, juce::Justification::centred, true);
    }

    juce::ValueTree& appState;
    StemAudioEngine& audioEngine;
    BeatTimeMap& beatTimeMap;

    juce::TextButton expandBtn;
    bool isExpanded = false;

    double currentBeat = 1.0;
    float currentFogDensity = 0.12f;
    juce::String activeSceneName{ "Pronto" };
    std::map<juce::String, RoleRuntimeState> roleStates;
};