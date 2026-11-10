cmake_minimum_required(VERSION 3.22)

project(DoYourLightShow VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Download e integrazione automatica di JUCE via FetchContent
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0 # Versione JUCE raccomandata
)
FetchContent_MakeAvailable(JUCE)

# 2. Definizione dell'applicazione GUI
juce_add_gui_app(DoYourLightShow
    PRODUCT_NAME "Do Your Light Show"
    VERSION "1.0.0"
    COMPANY_NAME "MK - Void Sound Studio"
)

# 3. Inclusione dei sorgenti del progetto
target_sources(DoYourLightShow
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/MainComponent.h
        Source/UI/Tabs/VenuePatchTab.cpp
        Source/UI/Tabs/ShowBuilderTab.cpp
        Source/UI/Tabs/ExportEngineTab.cpp
        
        # Model
        Source/Model/LogicalParam.h
        Source/Model/FixtureProfile.h
        Source/Model/FixtureInstance.h
        Source/Model/HardwareMidiMapper.h
        Source/Model/VenuePatch.h
        Source/Model/Scene.h
        
        # Engine
        Source/Engine/MusicalContext.h
        Source/Engine/LfoGenerator.h
        Source/Engine/ColorGenerator.h
        Source/Engine/TriggerGenerator.h
        Source/Engine/MidiRenderEngine.h
        
        # UI Tabs & Components
        Source/UI/Tabs/VenuePatchTab.h
        Source/UI/Tabs/ShowBuilderTab.h
        Source/UI/Tabs/ExportEngineTab.h
        Source/UI/Components/PanTiltRangeSlider.h
        Source/UI/Components/TwoValueSliderAttachment.h
        Source/UI/Components/LfoWaveformPreview.h
        
        # Utils
        Source/Utils/PresetManager.h
)

# 4. Include directory
target_include_directories(DoYourLightShow
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# 5. Collegamento dei Moduli JUCE necessari
target_link_libraries(DoYourLightShow
    PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
)

# 6. Generazione automatica di JuceHeader.h per la compatibilità
juce_generate_juce_header(DoYourLightShow)

# 7. Disabilita warning superflui di JUCE su compilatori moderni
target_compile_definitions(DoYourLightShow
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
