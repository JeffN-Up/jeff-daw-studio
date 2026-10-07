include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniz)
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
