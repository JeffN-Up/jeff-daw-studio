cmake_minimum_required(VERSION 4.2)
project(JeffDawStudio VERSION 0.1.0 LANGUAGES CXX)
option(JDS_BUILD_TESTS "Build tests" ON)
if(WIN32)
  option(JDS_BUILD_APP "Build native JUCE app" ON)
else()
  option(JDS_BUILD_APP "Build native JUCE app" OFF)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/Dependencies.cmake)

set(JDS_CORE_SOURCES
  src/audio/AudioEngine.cpp src/midi/DeviceProfile.cpp src/midi/MidiDeviceService.cpp
  src/project/Types.cpp src/project/Project.cpp src/project/ProjectCommands.cpp
  src/project/MediaStore.cpp src/stems/StemImporter.cpp
  src/stems/TimingMap.cpp src/stems/StretchRenderer.cpp
  src/transport/TransportClock.cpp src/ui/OrbaLayout.cpp src/ui/OrbaMirrorModel.cpp
  src/ui/DeviceCenterModel.cpp)
list(TRANSFORM JDS_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")

if(JDS_BUILD_APP)
  juce_add_gui_app(JeffDawStudio PRODUCT_NAME "Jeff DAW Studio" COMPANY_NAME "Jeffrey Chapin" VERSION ${PROJECT_VERSION})
  target_sources(JeffDawStudio PRIVATE src/app/Main.cpp src/app/MainWindow.cpp src/app/RootComponent.cpp
    src/audio/JuceAudioBackend.cpp src/midi/JuceMidiBackend.cpp src/stems/JuceStemDecoder.cpp src/stems/JuceFlacIntegrity.cpp src/ui/OrbaMirrorComponent.cpp
    src/ui/TransportComponent.cpp src/ui/DeviceCenterComponent.cpp ${JDS_CORE_SOURCES})
  target_include_directories(JeffDawStudio PRIVATE src)
  target_compile_definitions(JeffDawStudio PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(JeffDawStudio PRIVATE signalsmith-stretch juce::juce_audio_utils juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  install(TARGETS JeffDawStudio RUNTIME DESTINATION "Jeff DAW Studio" BUNDLE DESTINATION "Jeff DAW Studio")
  install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION "Jeff DAW Studio")
endif()
if(JDS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
