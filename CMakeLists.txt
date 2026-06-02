cmake_minimum_required(VERSION 3.22)
project(MicTrans VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows only
if(NOT WIN32)
    message(FATAL_ERROR "MicTrans is Windows-only.")
endif()

# ── JUCE ──────────────────────────────────────────────────────────────────────
include(cmake/JUCE.cmake)

# ── ASIO SDK (Steinberg, manual placement) ────────────────────────────────────
set(ASIO_SDK_PATH "${CMAKE_SOURCE_DIR}/third_party/ASIO_SDK" CACHE PATH "Path to Steinberg ASIO SDK")
if(NOT EXISTS "${ASIO_SDK_PATH}/common/asio.h")
    message(WARNING "ASIO SDK not found at ${ASIO_SDK_PATH}. ASIO support disabled.")
    set(JUCE_ASIO_ENABLED 0)
else()
    set(JUCE_ASIO_ENABLED 1)
    include_directories("${ASIO_SDK_PATH}/common")
endif()

# ── VST3 SDK (git submodule) ──────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/vst3sdk/CMakeLists.txt")
    set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/vst3sdk EXCLUDE_FROM_ALL)
endif()

# ── App target ────────────────────────────────────────────────────────────────
juce_add_gui_app(MicTrans
    PRODUCT_NAME        "MicTrans"
    VERSION             "1.0.0"
    COMPANY_NAME        "MicTrans"
    BUNDLE_ID           "com.mictrans.app"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE
)

juce_generate_juce_header(MicTrans)

target_sources(MicTrans PRIVATE
    src/Main.cpp
    src/MainWindow.cpp

    src/engine/AudioEngine.cpp
    src/engine/DeviceThread.cpp
    src/engine/MixingGraph.cpp
    src/engine/ClockDriftCorrector.cpp
    src/engine/VirtualMicBridge.cpp

    src/nodes/InputDeviceNode.cpp
    src/nodes/OutputDeviceNode.cpp
    src/nodes/VST3PluginNode.cpp
    src/nodes/MixerNode.cpp
    src/nodes/SplitterNode.cpp
    src/nodes/VirtualMicOutputNode.cpp

    src/ui/GraphEditorComponent.cpp
    src/ui/NodeComponent.cpp
    src/ui/ConnectionComponent.cpp
    src/ui/DeviceManagerPanel.cpp
    src/ui/VST3BrowserPanel.cpp
)

target_include_directories(MicTrans PRIVATE src)

target_compile_definitions(MicTrans PRIVATE
    JUCE_WASAPI=1
    JUCE_ASIO=${JUCE_ASIO_ENABLED}
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(MicTrans PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp

    # Windows system libs for WASAPI / priority APIs
    avrt
    winmm
)

# MSVC-specific: enable highest optimization for audio code
if(MSVC)
    target_compile_options(MicTrans PRIVATE
        $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /GL>
    )
    target_link_options(MicTrans PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()
