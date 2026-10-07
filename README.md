# Jeff DAW Studio

Free, open-source Windows DAW foundation for Artiphon Orba 1/2, Behringer JT-4000 Micro, Windows audio inputs, and virtual keyboard/pad performance.

## Current foundation

- Native JUCE/C++ application with WASAPI/optional ASIO device access.
- USB or Windows-exposed Bluetooth MIDI discovery with stable-ID reconnect.
- Live eight-pad Orba Mirror with pressure, pitch, timbre, mouse, touch, and `A S D F J K L ;` keyboard input.
- Sample-domain transport, metronome, audition synth, bounded MIDI monitor, and Device Center diagnostics.
- Local settings only; no account, subscription, cloud service, or paid API.

## Windows prerequisites

- Windows 10/11 x64
- Git
- CMake 4.2+
- Visual Studio 2026 Community with **Desktop development with C++**

## Build and run

```powershell
git clone <repository-url> jeff-daw-studio
cd jeff-daw-studio
cmake -S . -B build -DJDS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\JeffDawStudio_artefacts\Release\JeffDawStudio.exe
```

Install/stage the portable folder with:

```powershell
cmake --install build --config Release --prefix package
```

Bluetooth devices must first be paired in Windows. USB is recommended for lower latency. The Orba and JT-4000 headphone jacks carry audio, not MIDI; recording their built-in sounds cleanly requires a Windows-visible audio input, preferably a stereo USB audio interface. A USB-camera microphone can be selected as a mono input.

Hardware verification is intentionally marked **Not run** until tested on Jeff's ThinkCentre Mini PC; software simulation is never reported as physical-device evidence.
