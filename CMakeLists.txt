cmake_minimum_required(VERSION 3.22)

project(DoYourLightShow VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Download e integrazione automatica di JUCE via FetchContent
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(JUCE)

# 2. Sorgenti condivisi tra Standalone e VST3
set(COMMON_SOURCES
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/Plugin/LightShowProcessor.h
    Source/Plugin/LightShowProcessor.cpp
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
    Source/Model/CurveEvaluator.h

    # Engine
    Source/Engine/MusicalContext.h
    Source/Engine/LfoGenerator.h
    Source/Engine/ColorGenerator.h
    Source/Engine/TriggerGenerator.h
    Source/Engine/MidiRenderEngine.h
    Source/Engine/DmxOutputDriver.h
    Source/Engine/ArtNetDmxDriver.h
    Source/Engine/SacnDmxDriver.h
    Source/Engine/EnttecProDmxDriver.h
    Source/Engine/DmxOutputManager.h
    Source/Engine/DmxLiveEngine.h
    Source/Engine/DawTransportSync.h

    # UI Tabs & Components
    Source/UI/Tabs/VenuePatchTab.h
    Source/UI/Tabs/SceneBuilderTab.h
    Source/UI/Tabs/ShowBuilderTab.h
    Source/UI/Tabs/ExportEngineTab.h
    Source/UI/Components/PanTiltRangeSlider.h
    Source/UI/Components/TwoValueSliderAttachment.h
    Source/UI/Components/LfoWaveformPreview.h
    Source/UI/Components/MiniDawTimeline.h
    Source/UI/Components/PanTiltCalibrationWizard.h
    Source/UI/Components/TimeMetricManager.h
    Source/UI/Components/StageArtworkHelper.h
    Source/UI/Components/FileProgressOverlay.h
    Source/UI/Components/DmxOutputControlPanel.h

    # Utils
    Source/Utils/PresetManager.h
    Source/Utils/StateUndoManager.h
)

set(COMMON_JUCE_LIBS
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
)

# 3A. Target Standalone (GUI App nativa con finestra custom)
juce_add_gui_app(DoYourLightShow
    PRODUCT_NAME "Do Your Light Show"
    VERSION "1.0.0"
    COMPANY_NAME "MK - Void Sound Studio"
)

target_sources(DoYourLightShow
    PRIVATE
        Source/Main.cpp
        ${COMMON_SOURCES}
)

target_include_directories(DoYourLightShow
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(DoYourLightShow
    PRIVATE
        ${COMMON_JUCE_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(DoYourLightShow)

target_compile_definitions(DoYourLightShow
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# 3B. Target Plugin VST3 (per Reaper e altre DAW)
juce_add_plugin(DoYourLightShowVST3
    PRODUCT_NAME "Do Your Light Show"
    VERSION "1.0.0"
    COMPANY_NAME "MK - Void Sound Studio"
    PLUGIN_MANUFACTURER_CODE Mkvs
    PLUGIN_CODE Dyls
    FORMATS VST3
    VST3_CATEGORIES "Fx" "Tools"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DoYourLightShowVST3
    PRIVATE
        Source/Plugin/PluginEntry.cpp
        ${COMMON_SOURCES}
)

target_include_directories(DoYourLightShowVST3
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK"
        "${juce_SOURCE_DIR}/examples/Plugins/extern"
)

target_link_libraries(DoYourLightShowVST3
    PRIVATE
        ${COMMON_JUCE_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(DoYourLightShowVST3)

target_compile_definitions(DoYourLightShowVST3
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        DOYOURLIGHTSHOW_HAS_REAPER_API=1
)