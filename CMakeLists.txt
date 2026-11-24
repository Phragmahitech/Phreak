cmake_minimum_required(VERSION 3.22)

# --- Réglages à personnaliser -------------------------------------------------
set(PHREAK_PRODUCT_NAME "Phreak")          # nom du plugin dans le DAW, et nom du fichier .vst3
set(PHREAK_COMPANY_NAME "Phragma")          # nom affiché comme éditeur dans le DAW
set(PHREAK_BUNDLE_ID    "com.phragma.phreak")
# -----------------------------------------------------------------------------
# Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE : ce sont eux (et pas le nom)
# qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Phreak VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PHREAK_COPY_AFTER_BUILD "Copier le plugin dans le dossier VST3 du système après la compilation" OFF)
option(PHREAK_BUILD_TESTS "Compiler les tests" OFF)

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin ------------------------------------------------------------------
juce_add_plugin(Phreak
    PRODUCT_NAME                "${PHREAK_PRODUCT_NAME}"
    COMPANY_NAME                "${PHREAK_COMPANY_NAME}"
    BUNDLE_ID                   "${PHREAK_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Phrk
    FORMATS                     VST3
    VST3_CATEGORIES             Instrument Sampler
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${PHREAK_COPY_AFTER_BUILD})

target_sources(Phreak PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Police de l'interface (Chakra Petch, licence OFL : voir Assets/ChakraPetch-OFL.txt), embarquée dans le plugin
juce_add_binary_data(PhreakAssets SOURCES
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-Bold.ttf)
set_target_properties(PhreakAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(Phreak PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    PHREAK_VERSION="${PROJECT_VERSION}")

target_link_libraries(Phreak
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        PhreakAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(PHREAK_BUILD_TESTS)
    enable_testing()

    # Moteur seul, sans JUCE
    add_executable(PhreakEngineTests Tests/EngineTests.cpp)
    if(NOT MSVC)
        target_compile_options(PhreakEngineTests PRIVATE -O2)
    endif()
    target_include_directories(PhreakEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND PhreakEngineTests)

    # Plugin complet (paramètres, synchro tempo, état, interface)
    juce_add_console_app(PhreakPluginTests PRODUCT_NAME "PhreakPluginTests")
    target_sources(PhreakPluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(PhreakPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1
        PHREAK_VERSION="${PROJECT_VERSION}")
    target_link_libraries(PhreakPluginTests PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        PhreakAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND PhreakPluginTests)

endif()
