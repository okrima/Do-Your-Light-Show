#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class DoYourLightShowApplication : public juce::JUCEApplication
{
public:
    DoYourLightShowApplication() {}

    const juce::String getApplicationName() override { return "DoYourLightShow"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        processor = std::make_unique<LightShowProcessor>(false);
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *processor);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        processor = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
        {
            if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent()))
            {
                mc->requestApplicationQuit([this]() { quit(); });
                return;
            }
        }
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name, LightShowProcessor& proc)
            : DocumentWindow(name,
                juce::Desktop::getInstance().getDefaultLookAndFeel()
                .findColour(juce::ResizableWindow::backgroundColourId),
                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(proc), true);

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
            setResizeLimits(1280, 768, 10000, 10000);
            centreWithSize(getWidth(), getHeight());
            setFullScreen(true);
#endif

            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<LightShowProcessor> processor;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DoYourLightShowApplication)