add_executable(JeffDawTests FoundationTests.cpp ProjectTests.cpp StemImportTests.cpp StretchTests.cpp SyncTests.cpp ${JDS_CORE_SOURCES})
target_include_directories(JeffDawTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(JeffDawTests PRIVATE Catch2::Catch2WithMain signalsmith-stretch)
include(Catch)
catch_discover_tests(JeffDawTests)

if(JDS_BUILD_APP)
  add_executable(JeffDawJuceImportTests StemImportTests.cpp JuceStemDecoderTests.cpp StretchTests.cpp
    ${PROJECT_SOURCE_DIR}/src/stems/JuceStemDecoder.cpp ${PROJECT_SOURCE_DIR}/src/stems/JuceFlacIntegrity.cpp ${JDS_CORE_SOURCES})
  target_include_directories(JeffDawJuceImportTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(JeffDawJuceImportTests PRIVATE JDS_TEST_JUCE_IMPORT=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JDS_JUCE_MP3_FIXTURE="${juce_SOURCE_DIR}/examples/Assets/Notifications/sounds/served.mp3")
  target_link_libraries(JeffDawJuceImportTests PRIVATE Catch2::Catch2WithMain signalsmith-stretch
    juce::juce_audio_formats juce::juce_recommended_config_flags)
  catch_discover_tests(JeffDawJuceImportTests TEST_PREFIX "JUCE:")
endif()
