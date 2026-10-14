cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) et cible macOS minimale
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(RetroFX9030 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE : soit le dossier JUCE/ à côté de ce fichier, soit téléchargé automatiquement
# (ou -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE pour réutiliser une copie locale)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CORE_SOURCES
    Source/EffectDefs.cpp
    Source/Patch.cpp
    Source/Presets.cpp
    Source/Engine.cpp
    Source/Controller.cpp
    Source/dsp/Effects.cpp)

juce_add_plugin(RetroFX9030
    COMPANY_NAME "Carlos"
    BUNDLE_ID "ch.carlos.retrofx9030"
    PLUGIN_MANUFACTURER_CODE Crls
    PLUGIN_CODE Rf93
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "RetroFX 9030"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE        # copie dans ~/Library/Audio/Plug-Ins/
    VST3_CATEGORIES Fx Distortion Modulation Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

target_sources(RetroFX9030 PRIVATE
    ${CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(RetroFX9030 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RetroFX9030
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# Banc d'essai hors DAW : valide les presets, l'afficheur et la stabilité de chaque effet
juce_add_console_app(SelfTest PRODUCT_NAME "SelfTest")
target_sources(SelfTest PRIVATE ${CORE_SOURCES} Tests/SelfTest.cpp)
target_compile_definitions(SelfTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SelfTest
    PRIVATE juce::juce_audio_basics juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Captures d'écran de l'interface (développement)
juce_add_console_app(Snapshot PRODUCT_NAME "Snapshot")
target_sources(Snapshot PRIVATE ${CORE_SOURCES} Source/PluginProcessor.cpp Source/PluginEditor.cpp Tests/Snapshot.cpp)
target_compile_definitions(Snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="RetroFX 9030")
target_link_libraries(Snapshot
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
