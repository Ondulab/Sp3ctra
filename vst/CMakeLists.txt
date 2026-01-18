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
        
        # Settings tabs (Phase 1 - UI Architecture)
        source/settings/GeneralSettingsTab.cpp
        source/settings/GeneralSettingsTab.h
        source/settings/NetworkSettingsTab.cpp
        source/settings/NetworkSettingsTab.h
        source/settings/LuxStralSettingsTab.cpp
        source/settings/LuxStralSettingsTab.h
        
        # 🎵 PHASE 3: LuxStral Synthesis Engine (VST-adapted)
        source/luxstral/vst_adapters.cpp
        source/luxstral/vst_adapters.h
        source/luxstral/synth_luxstral.c
        source/luxstral/synth_luxstral.h
        source/luxstral/synth_luxstral_algorithms.c
        source/luxstral/synth_luxstral_algorithms.h
        source/luxstral/synth_luxstral_math.c
        source/luxstral/synth_luxstral_math.h
        source/luxstral/synth_luxstral_math_neon.c
        source/luxstral/synth_luxstral_runtime.c
        source/luxstral/synth_luxstral_runtime.h
        source/luxstral/synth_luxstral_state.c
        source/luxstral/synth_luxstral_state.h
        source/luxstral/synth_luxstral_stereo.c
        source/luxstral/synth_luxstral_stereo.h
        source/luxstral/synth_luxstral_threading.c
        source/luxstral/synth_luxstral_threading.h
        source/luxstral/synth_luxstral_threading_rt.c
        source/luxstral/wave_generation.c
        source/luxstral/wave_generation.h
        source/luxstral/pow_approx.c
        source/luxstral/pow_approx.h
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
        ../src/utils/rt_profiler.c
        
        # Threading (for udpThread and buffer initialization)
        ../src/threading/multithreading.c

        # Image preprocessing (required for VST path)
        ../src/processing/image_preprocessor.c

        # Pan system (required for stereo mode)
        ../src/audio/pan/lock_free_pan.c

        # KissFFT (required by image_preprocessor luxsynth path)
        ../src/synthesis/luxsynth/kissfft/kiss_fft.c
        ../src/synthesis/luxsynth/kissfft/kiss_fftr.c
        
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
        ../src/audio/pan
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
    NO_SFML=1              # VST does not use SFML (C code uses this for conditional compilation)
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

# ===== PROFIL RELEASE (OPTIMIZED FOR PRODUCTION) =====
# RT-SAFE OPTIMIZATION: Preserve memory ordering for atomics and barriers
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(Sp3ctraVST PRIVATE /O2 /fp:fast /W4)
    else()
        target_compile_options(Sp3ctraVST PRIVATE
            -O2                      # Moderate optimization (safer than -O3 for RT audio)
            -fno-strict-aliasing     # Prevent aggressive pointer aliasing
            -Wall                    # All warnings
            -Wextra                  # Extra warnings
            -fPIC                    # Position Independent Code
            -Wno-unused-parameter
            # RT-SAFE: NO -ffast-math (breaks IEEE754 and can affect atomics)
            # RT-SAFE: NO -Ofast (too aggressive, breaks memory ordering)
        )
    endif()
endif()

# ===== PROFIL DEBUG (WITH SANITIZERS) =====
# Enable memory and undefined behavior detection
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        target_compile_options(Sp3ctraVST PRIVATE /W4)
    else()
        target_compile_options(Sp3ctraVST PRIVATE
            -O0                        # No optimization
            -g                         # Full debug symbols
            -Wall
            -Wextra
            -Wno-unused-parameter
            -fsanitize=address         # AddressSanitizer (memory errors)
            -fsanitize=undefined       # UndefinedBehaviorSanitizer
            -fno-omit-frame-pointer    # Better stack traces
        )
        target_link_options(Sp3ctraVST PRIVATE
            -fsanitize=address
            -fsanitize=undefined
        )
    endif()
endif()
