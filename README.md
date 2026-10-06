# Jeff DAW Studio

Free, open-source Windows DAW foundation for Artiphon Orba 1/2, Behringer JT-4000 Micro, Windows audio inputs, and virtual keyboard/pad performance.

## Windows workspace (October 6, 2026)

The `feature/usable-windows-workspace` branch connects the stem services to the native app. Start with **Load Demo**, then **Play** to hear four generated tracks. Use **Audio Settings** to select the output device. **Jam + Devices** retains the playable Orba mirror and hardware MIDI audition.

- Import WAV/AIFF/FLAC/MP3 files with real waveform previews and playable tracks.
- Adjust volume, pan, mute and solo. Click **Save** after mix changes.
- **Open** restores a saved `project.json` with original media verified by SHA-256.
- **Project Folder** opens the local project directory under Documents/Jeff DAW Projects. Keep the complete folder, including media, when moving a project. This is a local folder workflow, not a portable checkpoint ZIP or Drive upload.
- **Export Mix** writes a 48 kHz/24-bit stereo WAV. Choose a new filename.
- **Record Output** captures the app output to the project recordings folder; click **Finish Recording** to finalize the WAV. It records the audition instrument and mixed stems, not microphone input. A capture overflow or device-rate change rejects the partial take.

Advanced timing controls, Android, microphone recording, MIDI clips, effects, looping and recovery/autosave remain pending. Native Windows CI and ThinkCentre hardware checks must run before this branch is called a Windows release.

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
git clone --branch feature/usable-windows-workspace https://github.com/JeffN-Up/jeff-daw-studio.git
cd jeff-daw-studio
cmake -S . -B build -DJDS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& ".\build\JeffDawStudio_artefacts\Release\Jeff DAW Studio.exe"
```

Install/stage the portable folder with:

```powershell
cmake --install build --config Release --prefix package
```

Bluetooth devices must first be paired in Windows. USB is recommended for lower latency. The Orba and JT-4000 headphone jacks carry audio, not MIDI; recording their built-in sounds cleanly requires a Windows-visible audio input, preferably a stereo USB audio interface. A USB-camera microphone can be selected as a mono input.

Hardware verification is intentionally marked **Not run** until tested on Jeff's ThinkCentre Mini PC; software simulation is never reported as physical-device evidence.
