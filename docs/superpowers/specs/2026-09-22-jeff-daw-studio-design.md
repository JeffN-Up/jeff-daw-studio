# Jeff DAW Studio — Product and Technical Design

**Date:** September 22, 2026  
**Status:** Approved design  
**Repository:** `jeff-daw-studio`  
**Target:** Windows 10/11 x64 desktop

## 1. Purpose

Jeff DAW Studio is a free, open-source personal digital audio workstation built around Jeff's Artiphon Orba 1, Artiphon Orba 2, and Behringer JT-4000 Micro. It must make it easy to begin playing and looping immediately while still providing the editing, mixing, plug-in, and export tools expected from a moderately advanced desktop DAW.

The application has two coordinated workspaces:

- **Jam Mode** for immediate performance, looping, and idea capture.
- **Studio Mode** for detailed arrangement, editing, mixing, automation, and export.

Both modes use the same project, transport, audio graph, MIDI data, and undo history. Moving between them never renders or duplicates the song.

## 2. Success Criteria

The first complete release succeeds when Jeff can:

1. Install the application on his Lenovo ThinkCentre Mini PC.
2. Connect Orba 1, Orba 2, and JT-4000 through supported Windows MIDI connections.
3. See the correct Orba model on screen and see its corresponding pads respond to physical performance input.
4. Play built-in instruments or a VST3 instrument from either controller.
5. Record MIDI, MPE expression, and audio from an available Windows input.
6. Build loops in Jam Mode and continue editing the same material in Studio Mode.
7. Arrange, mix, save, reopen, recover, collect, and export a project without internet access.
8. Clone the public GitHub repository and build or install a version on the home Mini PC.

## 3. Licensing and Cost

The project will remain free and open source.

- The application uses JUCE under the AGPLv3 option.
- VST3 integration uses the Steinberg VST3 SDK under its MIT terms.
- The repository is public and the application source is distributed under AGPLv3.
- Core operation requires no account, subscription, cloud service, or paid API.
- Third-party plug-ins and user-installed sound libraries retain their own licenses.

## 4. Architecture

### 4.1 Application framework

The application is a native C++ desktop program built with JUCE and CMake. The first supported release target is Windows 10/11 x64. The architecture must preserve a path to other desktop platforms without compromising the Windows-first implementation.

### 4.2 Core boundaries

The codebase is divided into independently testable modules:

- **Audio Engine:** owns the real-time processing graph, audio device callbacks, transport synchronization, monitoring, and rendering.
- **MIDI Engine:** owns device discovery, routing, timestamp normalization, conventional MIDI, and MPE expression.
- **Project Model:** owns tracks, clips, tempo, routing, automation, plug-in state, media references, serialization, and migrations.
- **Command and Undo Layer:** is the only mutation path used by the UI for editable project state.
- **Jam Workspace:** provides performance-first looping and the Orba Mirror.
- **Studio Workspace:** provides timeline, editors, mixer, automation, and detailed routing.
- **Plug-in Service:** scans, validates, catalogs, instantiates, and persists VST3 plug-ins.
- **Media Service:** records, imports, manages, collects, and exports audio and MIDI assets.
- **Device Center:** presents audio/MIDI selection, calibration, latency, activity, reconnection, and diagnostics.

The real-time audio callback must not allocate memory, block on locks, perform file I/O, scan plug-ins, or call UI code.

### 4.3 Windows device support

- WASAPI is the default audio path.
- ASIO is available when the user installs a compatible audio interface and driver.
- The current USB-camera microphone is supported as a mono Windows audio input.
- Future stereo USB audio interfaces appear through the same Device Center.
- USB MIDI devices are discovered automatically.
- Bluetooth MIDI is supported when the device is paired and exposed by Windows.
- USB is identified in the interface as the preferred low-latency connection.

The Orba and JT-4000 headphone outputs carry audio rather than MIDI. Recording those hardware sounds requires a Windows-visible audio input, preferably a future stereo USB audio interface.

## 5. Orba Mirror

Jam Mode includes a live digital twin named **Orba Mirror**.

### 5.1 Visual behavior

- The display uses an original software rendering of the connected Orba 1 or Orba 2 control layout.
- Eight pads are placed in the same radial orientation as the hardware.
- The active Drum, Bass, Chord, or Lead mode is visible.
- Incoming pad presses illuminate the corresponding on-screen pad.
- Illumination brightness reflects velocity or pressure where the incoming data supports it.
- Held and simultaneous notes remain visibly active.
- Slide, radiate, pitch bend, vibrato, and supported motion expression receive distinct animations.
- Recorded performances animate the display during playback.
- The on-screen pads can also be played by mouse, keyboard, or touch.

### 5.2 Data behavior

- The device profile maps incoming MIDI notes, channels, MPE dimensions, and controller messages into normalized performance events.
- Raw source data is retained when necessary for lossless playback and future mapping improvements.
- MPE is recorded as per-note expression and is not collapsed into channel-wide automation.
- Unsupported or unavailable gestures fail silently as expression dimensions; ordinary note performance continues.
- The Device Center includes a learn-and-test view for firmware or mapping variations.

## 6. Jam Mode

Jam Mode opens directly onto the playable surface rather than a marketing or setup screen. If no device is configured, a compact setup card appears without blocking the virtual controls.

Jam Mode includes:

- Record, play, stop, loop, tempo, count-in, and metronome controls.
- Eight color-coded clip slots with visible recording and playback states.
- Orba Mirror when an Orba is selected.
- A conventional virtual piano and drum pads for other input modes.
- Built-in piano-style, bass, drum, pad, lead, and subtractive-synth presets.
- Chord assistance, scale lock, arpeggiator, note repeat, swing, and quantization.
- Quick-access reverb, delay, chorus, distortion, filter, and compressor.
- Non-destructive movement of every Jam clip into the Studio timeline.

The first release's piano sound may use a redistributable open-source sample set or an original built-in synthesis preset. Any bundled samples must have a license compatible with public distribution.

## 7. Studio Mode

Studio Mode includes:

- Audio, MIDI, instrument, drum, return, and master tracks.
- A multitrack timeline with snapping, selection, drag, trim, split, duplicate, loop, fades, and clip gain.
- Piano-roll editing with velocity and per-note expression lanes.
- Waveform display and non-destructive audio editing.
- Mixer strips with volume, pan, mute, solo, arm, input monitoring, inserts, and sends.
- VST3 instruments and effects.
- MIDI input filtering, channel selection, and routing.
- Track and plug-in parameter automation.
- Tempo and time-signature editing.
- Unified undo/redo across project edits.

## 8. Recording, Saving, and Export

### 8.1 Recording

- Audio is streamed to a recoverable temporary recording file while capture is active.
- MIDI and MPE events retain timestamps at the engine's musical time resolution.
- Count-in, punch, loop recording, input monitoring, and basic take management are supported.
- An interrupted recording is offered for recovery on the next launch.

### 8.2 Project format

A project is a folder containing a versioned project document plus media and recovery data. It stores:

- Tracks, clips, tempo, meter, markers, routing, and automation.
- MIDI and MPE performance data.
- Mixer settings and built-in processor state.
- VST3 identity and serialized state.
- Media references using paths relative to the project when possible.

Autosave uses rotating recovery versions. Saving is atomic: a completed temporary document replaces the prior project document only after validation.

**Collect Project** copies referenced recordings and samples into the project folder and rewrites references, making the folder portable to the home computer.

### 8.3 Export

- Stereo master export to WAV and FLAC.
- MIDI file export from selected MIDI tracks or the complete arrangement.
- Real-time export is available when a plug-in cannot render reliably offline.

## 9. VST3 Hosting

- Plug-in discovery and validation occur in a separate scanner process.
- A scanner crash or timeout records the plug-in in a quarantine list without crashing the main application.
- Users can retry, ignore, or permanently exclude quarantined plug-ins.
- The catalog records vendor, category, I/O layout, instrument/effect role, and scan status.
- Plug-in state is saved with the project.
- Missing plug-ins preserve their slot and state reference while the rest of the project opens.
- Plug-in editors open in managed windows that remain associated with their owning track.

Runtime isolation of every active plug-in is not required for the first release. The engine must, however, contain plug-in failures at safe boundaries where JUCE and the operating system permit it, and autosave must protect the project from a plug-in crash.

## 10. Failure Handling and Diagnostics

- Removed audio and MIDI devices transition to a disconnected state without deleting routing.
- Reappearing devices reconnect automatically when their stable identifier matches.
- Audio initialization failures fall back to a device-selection screen with a plain-language explanation.
- Buffer underruns, sample rate, buffer size, measured latency, MIDI activity, and device state are visible in Device Center.
- Corrupt project documents do not overwrite valid autosaves.
- Unknown future project fields are preserved when practical; explicit migrations handle older formats.
- Destructive operations remain undoable unless they concern external files, in which case the app asks before deletion.

## 11. Accessibility and Interaction

- The default interface is simple to moderately advanced.
- Jam Mode keeps the first viewport focused on playing and recording.
- Studio Mode reveals detail through inspectors and expandable panels instead of showing every control at once.
- Core transport and editing actions have keyboard shortcuts and accessible names.
- Minimum regular control text is 14 px equivalent; primary text is 16 px equivalent or larger.
- Controls remain distinguishable without relying on color alone.
- The application supports scaling suitable for a Mini PC connected to common desktop displays.

## 12. Verification Strategy

Automated tests cover:

- Project serialization, migrations, autosave rotation, and recovery.
- Command execution and undo/redo invariants.
- MIDI timestamping, routing, quantization, and MPE normalization.
- Orba profile mappings using captured fixture data.
- Audio graph construction and deterministic offline rendering.
- Export validity for WAV, FLAC, and MIDI.
- Plug-in catalog persistence, scanner timeouts, and quarantine behavior.

Manual release checks cover:

- Orba 1 via USB and Windows Bluetooth MIDI.
- Orba 2 via USB and Windows Bluetooth MIDI.
- JT-4000 Micro via USB MIDI.
- USB-camera microphone recording.
- WASAPI playback/recording.
- ASIO playback/recording when compatible hardware is available.
- At least one VST3 instrument and one VST3 effect.
- Install, upgrade, uninstall, crash recovery, collect, and move-to-second-PC workflows.

Hardware-dependent checks that cannot run in the build environment remain clearly marked until Jeff performs them on the ThinkCentre Mini PC.

## 13. Delivery Milestones

1. **Audio/MIDI Foundation** — application shell, audio engine, Device Center, transport, metronome, MIDI monitor, Orba profiles, and live Orba Mirror.
2. **Jam Mode** — clip loops, built-in instruments, quick effects, and MIDI/MPE/audio recording.
3. **Studio Mode** — timeline, piano roll, waveform editing, mixer, routing, and automation.
4. **Plug-ins and Export** — isolated scanning, VST3 hosting, project portability, and WAV/FLAC/MIDI export.
5. **Windows Release** — installer, hardware setup guide, full recovery checks, and versioned release package.

Each milestone receives focused tests, documentation, and a Git commit. Stable checkpoints are pushed regularly so development can be cloned and resumed on the home Mini PC without depending on a single conversation or machine.

## 14. Deferred Capabilities

These ideas fit the architecture but are intentionally excluded from the first complete release:

- AI composition and arrangement.
- Stem separation.
- Automatic vocal pitch correction.
- Online collaboration and cloud project synchronization.
- Mobile companion applications.
- Cross-platform installers beyond Windows.
- Full runtime process isolation for every active third-party plug-in.

Deferring them keeps the first release centered on dependable playing, recording, arranging, mixing, saving, and exporting.
