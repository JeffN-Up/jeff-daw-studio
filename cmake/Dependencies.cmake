include(FetchContent)
# Header-only pitch-preserving renderer; commits pin both the release and its
# FFT dependency. Local builds may supply FetchContent source overrides.
set(SIGNALSMITH_USE_ACCELERATE OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_IPP OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_PFFFT OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_PFFFT_DOUBLE OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_XSIMD OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_XSIMD_DISPATCH OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
set(SIGNALSMITH_USE_CMSISDSP OFF CACHE BOOL "Portable Signalsmith FFT" FORCE)
FetchContent_Declare(signalsmith-linear GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
  GIT_TAG de55e6a50ffcf6f8f43f649692d94691c7025151 GIT_SUBMODULES "")
FetchContent_MakeAvailable(signalsmith-linear)
if(MSVC)
  # The header templates compile in consumers, outside Linear's CMake scope.
  target_compile_options(signalsmith-linear INTERFACE /bigobj)
endif()
FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df GIT_SUBMODULES "")
FetchContent_MakeAvailable(signalsmith-stretch)
if(JDS_BUILD_TESTS)
  FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.15.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
endif()
if(JDS_BUILD_APP)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72 GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(nlohmann_json)
