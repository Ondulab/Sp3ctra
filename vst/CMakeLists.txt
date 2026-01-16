cmake_minimum_required(VERSION 3.15)

project(Sp3ctraVST VERSION 0.0.1)

# Add JUCE using FetchContent
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

# Define the plugin with sources directly
juce_add_plugin(Sp3ctraVST
    COMPANY_NAME "Ondulab"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Ondu"
    PLUGIN_CODE "Sp3c"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Sp3ctra"
)

# Source files for the plugin wrapper (C++)
target_sources(Sp3ctraVST
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginProcessor.h
        source/PluginEditor.cpp
        source/PluginEditor.h
        source/SettingsWindow.cpp
        source/SettingsWindow.h
        source/CisVisualizerComponent.cpp
        source/CisVisualizerComponent.h
        source/Sp3ctraCore.cpp
        source/Sp3ctraCore.h
        source/Sp3ctraConstants.h
        source/UdpReceiverThread.h
        source/global_stubs.c
)

# ✨ BRICK 1: UDP Reception - Add C sources
# Only include what's needed for UDP + buffers (no synthesis yet)
target_sources(Sp3ctraVST
    PRIVATE
        # UDP communication
        ../src/communication/network/udp.c
        
        # Configuration system
        ../src/config/config_loader.c
        
        # Logging system
        ../src/utils/logger.c
        ../src/utils/error.c
        
        # Threading (for udpThread and buffer initialization)
        ../src/threading/multithreading.c
        
        # Audio buffers (DoubleBuffer, AudioImageBuffers)
        ../src/audio/buffers/audio_image_buffers.c
        
        # Stubs for missing functions
        ../src/utils/stubs.c
)

# Add include directories for C headers
target_include_directories(Sp3ctraVST
    PRIVATE
        ../src
        ../src/core
        ../src/config
        ../src/utils
        ../src/communication/network
        ../src/audio/buffers
        ../src/audio/rtaudio
        ../src/threading
        ../src/processing
        ../src/synthesis/common
        ../src/synthesis/luxwave
        ../src/synthesis/luxstral
        ../src/synthesis/luxsynth
)

# Compile definitions
target_compile_definitions(Sp3ctraVST PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    NO_SFML=1
    VST_MODE=1
)

# Link libraries
target_link_libraries(Sp3ctraVST PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
)

# Standard settings
target_compile_features(Sp3ctraVST PRIVATE cxx_std_17 c_std_11)

# Enable warnings
if(MSVC)
    target_compile_options(Sp3ctraVST PRIVATE /W4)
else()
    target_compile_options(Sp3ctraVST PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
