#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

class TwoValueSliderAttachment : private juce::ValueTree::Listener,
                                 private juce::Slider::Listener
{
public:
    TwoValueSliderAttachment(juce::ValueTree treeToAttach,
                             const juce::Identifier& minProp,
                             const juce::Identifier& maxProp,
                             juce::Slider& sliderToControl,
                             juce::UndoManager* undoMgr = nullptr)
        : tree(treeToAttach),
          minProperty(minProp),
          maxProperty(maxProp),
          slider(sliderToControl),
          undoManager(undoMgr)
    {
        // 1. Registrazione dei Listener
        tree.addListener(this);
        slider.addListener(this);

        // 2. Sincronizzazione iniziale (dal ValueTree allo Slider)
        updateSliderFromTree();
    }

    ~TwoValueSliderAttachment() override
    {
        // Pulizia sicura in fase di distruzione
        slider.removeListener(this);
        tree.removeListener(this);
    }

private:
    // =========================================================================
    // SLIDER -> VALUE TREE (L'utente muove i cursori sulla UI)
    // =========================================================================
    
    void sliderValueChanged(juce::Slider* s) override
    {
        if (isUpdatingFromTree || s != &slider) return; // Evita loop infiniti

        juce::ScopedValueSetter<bool> svs(isUpdatingFromSlider, true);

        // Aggiorna le proprietà nel ValueTree passando l'UndoManager
        tree.setProperty(minProperty, slider.getMinValue(), undoManager);
        tree.setProperty(maxProperty, slider.getMaxValue(), undoManager);
    }

    void sliderDragStarted(juce::Slider*) override
    {
        // Quando l'utente clicca sul cursore, inizia una nuova transazione di Undo.
        // Tutto il trascinamento fino al rilascio farà parte di un unico step "Ctrl+Z".
        if (undoManager)
            undoManager->beginNewTransaction();
    }

    void sliderDragEnded(juce::Slider*) override
    {
        // Opzionale: può essere lasciato vuoto, il beginNewTransaction gestisce
        // automaticamente la chiusura al click successivo.
    }

    // =========================================================================
    // VALUE TREE -> SLIDER (Undo/Redo o caricamento Preset dal JSON)
    // =========================================================================
    
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override
    {
        if (treeWhosePropertyHasChanged == tree)
        {
            // Se cambia specificamente una delle due proprietà (Min o Max)
            if (property == minProperty || property == maxProperty)
            {
                updateSliderFromTree();
            }
        }
    }

    void updateSliderFromTree()
    {
        if (isUpdatingFromSlider) return; // Evita il loop inverso

        juce::ScopedValueSetter<bool> svs(isUpdatingFromTree, true);

        // Legge i valori con fallback ai limiti attuali dello slider se non esistono
        double minVal = tree.getProperty(minProperty, slider.getMinimum());
        double maxVal = tree.getProperty(maxProperty, slider.getMaximum());

        // dontSendNotification è fondamentale per non re-innescare sliderValueChanged
        slider.setMinAndMaxValues(minVal, maxVal, juce::dontSendNotification);
    }

    // --- Metodi virtuali di ValueTree::Listener obbligatori ma non utilizzati qui ---
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}
    void valueTreeRedirected(juce::ValueTree&) override {}

    // --- Membri Privati ---
    juce::ValueTree tree;
    juce::Identifier minProperty;
    juce::Identifier maxProperty;
    juce::Slider& slider;
    juce::UndoManager* undoManager;

    // Flag (Semafori) per la protezione contro le chiamate ricorsive (Feedback Loop)
    bool isUpdatingFromTree = false;
    bool isUpdatingFromSlider = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TwoValueSliderAttachment)
};