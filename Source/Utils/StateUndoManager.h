#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Model/DataStateKeys.h"
#include <vector>
#include <functional>

class StateUndoManager : private juce::Timer
{
public:
    struct RestoreResult
    {
        bool venueChanged = false;
        bool presetLibChanged = false;
        bool showChanged = false;
        bool otherChanged = false;
    };

    explicit StateUndoManager(juce::ValueTree& rootState)
        : state(rootState)
    {
    }

    ~StateUndoManager() override
    {
        stopTimer();
    }

    std::function<void()> onHistoryChanged;
    std::function<void(const RestoreResult&)> onStateRestored;

    void resetHistory()
    {
        stopTimer();
        hasPendingChanges = false;
        pendingDescription.clear();
        history.clear();
        history.push_back({ state.createCopy(), "Stato iniziale" });
        currentIndex = 0;

        if (onHistoryChanged)
            onHistoryChanged();
    }

    void notifyStateChanged(const juce::String& description)
    {
        if (restoring)
            return;

        const bool wasPending = hasPendingChanges;
        hasPendingChanges = true;
        if (description.isNotEmpty())
            pendingDescription = description;

        startTimer(kDebounceMs);

        if (!wasPending && onHistoryChanged)
            onHistoryChanged();
    }

    void commitPendingNow(const juce::String& overrideDescription = {})
    {
        stopTimer();

        if (restoring)
            return;

        if (!hasPendingChanges && overrideDescription.isEmpty())
            return;

        hasPendingChanges = false;

        if (history.empty())
        {
            resetHistory();
            return;
        }

        if (state.isEquivalentTo(history[currentIndex].snapshot))
        {
            pendingDescription.clear();
            if (onHistoryChanged)
                onHistoryChanged();
            return;
        }

        if (currentIndex + 1 < history.size())
            history.resize(currentIndex + 1);

        juce::String desc = overrideDescription.isNotEmpty() ? overrideDescription : pendingDescription;
        if (desc.isEmpty())
            desc = "Modifica";

        history.push_back({ state.createCopy(), desc });
        pendingDescription.clear();

        if (history.size() > kMaxHistory)
            history.erase(history.begin());

        currentIndex = history.size() - 1;

        if (onHistoryChanged)
            onHistoryChanged();
    }

    bool canUndo() const noexcept
    {
        return (currentIndex > 0) || (hasPendingChanges && !history.empty());
    }

    bool canRedo() const noexcept
    {
        return !hasPendingChanges && (currentIndex + 1 < history.size());
    }

    bool isRestoring() const noexcept
    {
        return restoring;
    }

    juce::String getUndoDescription() const
    {
        if (hasPendingChanges && pendingDescription.isNotEmpty())
            return pendingDescription;
        if (currentIndex > 0 && currentIndex < history.size())
            return history[currentIndex].description;
        return {};
    }

    juce::String getRedoDescription() const
    {
        if (!hasPendingChanges && currentIndex + 1 < history.size())
            return history[currentIndex + 1].description;
        return {};
    }

    bool undo()
    {
        if (restoring)
            return false;

        if (hasPendingChanges)
            commitPendingNow();

        if (currentIndex == 0 || history.empty())
            return false;

        --currentIndex;
        applySnapshotAndNotify(history[currentIndex].snapshot);
        return true;
    }

    bool redo()
    {
        if (restoring || hasPendingChanges)
            return false;

        if (currentIndex + 1 >= history.size())
            return false;

        ++currentIndex;
        applySnapshotAndNotify(history[currentIndex].snapshot);
        return true;
    }

private:
    struct HistoryEntry
    {
        juce::ValueTree snapshot;
        juce::String description;
    };

    void timerCallback() override
    {
        commitPendingNow();
    }

    static void markChangedFlag(const juce::Identifier& nodeType, RestoreResult& res)
    {
        if (nodeType == StateKeys::VenueNode)
            res.venueChanged = true;
        else if (nodeType == StateKeys::PresetLibNode)
            res.presetLibChanged = true;
        else if (nodeType == StateKeys::ShowNode)
            res.showChanged = true;
        else
            res.otherChanged = true;
    }

    void applySnapshotAndNotify(const juce::ValueTree& snapshot)
    {
        RestoreResult res;
        {
            juce::ScopedValueSetter<bool> guard(restoring, true);

            for (int i = 0; i < snapshot.getNumProperties(); ++i)
            {
                const auto propName = snapshot.getPropertyName(i);
                const auto propVal = snapshot.getProperty(propName);
                if (state.getProperty(propName) != propVal)
                    state.setProperty(propName, propVal, nullptr);
            }

            // Aggiorna in-place ogni nodo figlio di primo livello per non staccare i listener
            for (int i = 0; i < snapshot.getNumChildren(); ++i)
            {
                auto snapChild = snapshot.getChild(i);
                auto liveChild = state.getChildWithName(snapChild.getType());

                if (liveChild.isValid())
                {
                    if (!liveChild.isEquivalentTo(snapChild))
                    {
                        liveChild.copyPropertiesAndChildrenFrom(snapChild, nullptr);
                        markChangedFlag(snapChild.getType(), res);
                    }
                }
                else
                {
                    state.addChild(snapChild.createCopy(), -1, nullptr);
                    markChangedFlag(snapChild.getType(), res);
                }
            }

            for (int i = state.getNumChildren() - 1; i >= 0; --i)
            {
                auto liveChild = state.getChild(i);
                if (!snapshot.getChildWithName(liveChild.getType()).isValid())
                {
                    const auto type = liveChild.getType();
                    state.removeChild(i, nullptr);
                    markChangedFlag(type, res);
                }
            }

            if (onStateRestored)
                onStateRestored(res);
        }

        if (onHistoryChanged)
            onHistoryChanged();
    }

    juce::ValueTree& state;
    std::vector<HistoryEntry> history;
    size_t currentIndex = 0;

    bool hasPendingChanges = false;
    bool restoring = false;
    juce::String pendingDescription;

    static constexpr size_t kMaxHistory = 60;
    static constexpr int kDebounceMs = 350;
};