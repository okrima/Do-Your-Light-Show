#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class FileProgressRunner
{
public:
    struct Step
    {
        juce::String statusText;
        double progressAfter = 1.0;
        std::function<bool()> action;
    };

    // 1. Esegue un lavoro I/O su thread separato (es. Salva Show / Salva Lib) con barra fluida e popup finale
    static void runBackground(const juce::String& windowTitle,
        const juce::String& initialStatus,
        std::function<bool(std::function<void(double, const juce::String&)>)> workerFn,
        const juce::String& successTitle,
        const juce::String& successMessage,
        const juce::String& errorTitle,
        const juce::String& errorMessage,
        std::function<void(bool)> onFinishedUi = {})
    {
        auto* content = new BackgroundProgressContent(initialStatus, std::move(workerFn),
            successTitle, successMessage, errorTitle, errorMessage, std::move(onFinishedUi));
        launchWindow(windowTitle, content);
    }

    // 2. Esegue operazioni che toccano ValueTree/UI sul Message Thread a step visibili (es. Carica Show / Carica Lib)
    static void runStepped(const juce::String& windowTitle,
        std::vector<Step> steps,
        const juce::String& successTitle,
        std::function<juce::String()> successMessageFn,
        const juce::String& errorTitle,
        const juce::String& errorMessage)
    {
        auto* content = new SteppedProgressContent(std::move(steps),
            successTitle, std::move(successMessageFn), errorTitle, errorMessage);
        launchWindow(windowTitle, content);
    }

private:
    class ProgressDialogWindow : public juce::DialogWindow
    {
    public:
        ProgressDialogWindow(const juce::String& title, juce::Component* contentComp)
            : juce::DialogWindow(title, juce::Colour(0xff14161f), false, true)
        {
            setUsingNativeTitleBar(true);
            setResizable(false, false);
            setContentOwned(contentComp, true);
            centreWithSize(getWidth(), getHeight());
            setAlwaysOnTop(true);
            setVisible(true);
        }

        // Impedisce di chiudere a meta' un salvataggio/caricamento in corso
        void closeButtonPressed() override {}
    };

    class BaseProgressContent : public juce::Component
    {
    public:
        explicit BaseProgressContent(const juce::String& initialStatus)
        {
            statusLabel.setText(initialStatus, juce::dontSendNotification);
            statusLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe2e8f0));
            statusLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(statusLabel);

            progressBar.setPercentageDisplay(true);
            progressBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff1e293b));
            progressBar.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff00d2ff));
            addAndMakeVisible(progressBar);

            setSize(400, 108);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff14161f));
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced(18, 16);
            statusLabel.setBounds(b.removeFromTop(26));
            b.removeFromTop(12);
            progressBar.setBounds(b.removeFromTop(26));
        }

    protected:
        void closeParentWindowAndNotify(bool ok,
            const juce::String& okTitle, const juce::String& okMsg,
            const juce::String& errTitle, const juce::String& errMsg,
            std::function<void(bool)> onFinished = {})
        {
            juce::Component::SafePointer<juce::DialogWindow> dlg(findParentComponentOfClass<juce::DialogWindow>());
            juce::MessageManager::callAsync([dlg, ok, okTitle, okMsg, errTitle, errMsg, onFinished]() {
                if (dlg != nullptr)
                    delete dlg.getComponent();

                if (onFinished)
                    onFinished(ok);

                juce::NativeMessageBox::showMessageBoxAsync(
                    ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    ok ? okTitle : errTitle,
                    ok ? okMsg : errMsg);
                });
        }

        double progressValue = 0.0;
        juce::Label statusLabel;
        juce::ProgressBar progressBar{ progressValue };
    };

    class BackgroundProgressContent : public BaseProgressContent,
        private juce::Thread,
        private juce::Timer
    {
    public:
        BackgroundProgressContent(const juce::String& initialStatus,
            std::function<bool(std::function<void(double, const juce::String&)>)> fn,
            const juce::String& okT, const juce::String& okM,
            const juce::String& errT, const juce::String& errM,
            std::function<void(bool)> onDone)
            : BaseProgressContent(initialStatus),
            juce::Thread("FileProgressWorker"),
            workerFn(std::move(fn)),
            successTitle(okT), successMessage(okM),
            errorTitle(errT), errorMessage(errM),
            onFinishedUi(std::move(onDone)),
            sharedStatus(initialStatus)
        {
            startThread();
            startTimerHz(30);
        }

        ~BackgroundProgressContent() override
        {
            stopTimer();
            stopThread(5000);
        }

    private:
        void run() override
        {
            auto report = [this](double p, const juce::String& msg) {
                atomicProgress.store(juce::jlimit(0.0, 1.0, p));
                const juce::ScopedLock sl(statusLock);
                sharedStatus = msg;
                };

            const bool ok = workerFn ? workerFn(report) : false;
            atomicSuccess.store(ok);
            atomicProgress.store(1.0);
            {
                const juce::ScopedLock sl(statusLock);
                sharedStatus = ok ? "Completato!" : "Operazione non riuscita.";
            }
            atomicDone.store(true);
        }

        void timerCallback() override
        {
            progressValue = atomicProgress.load();
            {
                const juce::ScopedLock sl(statusLock);
                statusLabel.setText(sharedStatus, juce::dontSendNotification);
            }
            repaint();

            if (atomicDone.load())
            {
                ++doneHoldTicks;
                if (doneHoldTicks >= 6) // ~200 ms a 100% per feedback visivo pulito
                {
                    stopTimer();
                    stopThread(2000);
                    closeParentWindowAndNotify(atomicSuccess.load(),
                        successTitle, successMessage, errorTitle, errorMessage, onFinishedUi);
                }
            }
        }

        std::function<bool(std::function<void(double, const juce::String&)>)> workerFn;
        juce::String successTitle, successMessage, errorTitle, errorMessage;
        std::function<void(bool)> onFinishedUi;

        std::atomic<double> atomicProgress{ 0.05 };
        std::atomic<bool> atomicDone{ false };
        std::atomic<bool> atomicSuccess{ false };
        juce::CriticalSection statusLock;
        juce::String sharedStatus;
        int doneHoldTicks = 0;
    };

    class SteppedProgressContent : public BaseProgressContent,
        private juce::Timer
    {
    public:
        SteppedProgressContent(std::vector<Step> s,
            const juce::String& okT,
            std::function<juce::String()> okMsgFn,
            const juce::String& errT,
            const juce::String& errM)
            : BaseProgressContent(s.empty() ? "Elaborazione..." : s.front().statusText),
            steps(std::move(s)),
            successTitle(okT),
            successMessageFn(std::move(okMsgFn)),
            errorTitle(errT),
            errorMessage(errM)
        {
            progressValue = 0.05;
            startTimer(45);
        }

        ~SteppedProgressContent() override { stopTimer(); }

    private:
        void timerCallback() override
        {
            if (finished)
            {
                ++doneHoldTicks;
                if (doneHoldTicks >= 4)
                {
                    stopTimer();
                    const juce::String finalOkMsg = successMessageFn ? successMessageFn() : "Operazione completata.";
                    closeParentWindowAndNotify(allOk, successTitle, finalOkMsg, errorTitle, errorMessage);
                }
                return;
            }

            if (currentStepIndex >= steps.size())
            {
                finished = true;
                progressValue = 1.0;
                statusLabel.setText(allOk ? "Completato!" : "Errore durante l'operazione.", juce::dontSendNotification);
                repaint();
                return;
            }

            if (!paintedStepLabel)
            {
                statusLabel.setText(steps[currentStepIndex].statusText, juce::dontSendNotification);
                paintedStepLabel = true;
                repaint();
                return; // Lascia un frame di repaint prima di eseguire lo step pesante
            }

            const bool ok = steps[currentStepIndex].action ? steps[currentStepIndex].action() : true;
            if (!ok)
            {
                allOk = false;
                finished = true;
                progressValue = 1.0;
                statusLabel.setText("Operazione non riuscita.", juce::dontSendNotification);
                repaint();
                return;
            }

            progressValue = juce::jlimit(0.0, 1.0, steps[currentStepIndex].progressAfter);
            ++currentStepIndex;
            paintedStepLabel = false;

            if (currentStepIndex >= steps.size())
            {
                finished = true;
                progressValue = 1.0;
                statusLabel.setText("Completato!", juce::dontSendNotification);
            }
            else
            {
                statusLabel.setText(steps[currentStepIndex].statusText, juce::dontSendNotification);
            }
            repaint();
        }

        std::vector<Step> steps;
        juce::String successTitle, errorTitle, errorMessage;
        std::function<juce::String()> successMessageFn;
        size_t currentStepIndex = 0;
        bool paintedStepLabel = false;
        bool finished = false;
        bool allOk = true;
        int doneHoldTicks = 0;
    };

    static void launchWindow(const juce::String& title, juce::Component* content)
    {
        new ProgressDialogWindow(title, content);
    }
};