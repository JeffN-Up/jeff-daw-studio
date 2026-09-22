# Audio/MIDI Foundation and Orba Mirror Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first runnable Windows foundation of Jeff DAW Studio with dependable audio/MIDI device handling, transport and metronome timing, normalized Orba/JT-4000 input, a MIDI monitor, and a live Orba 1/2 digital twin.

**Architecture:** A native JUCE application separates real-time audio/MIDI work from testable domain models and UI presentation state. Hardware messages are normalized into bounded `PerformanceEvent` values and cross into the UI through a single-producer/single-consumer queue; the audio callback never blocks, allocates, performs file I/O, or calls UI code. Device backends sit behind interfaces so reconnection, timing, and Orba visualization can be tested without physical hardware.

**Tech Stack:** C++20, JUCE 8.0.12, CMake 4.2 or newer, Catch2 3.15.2, Windows 10/11 x64, WASAPI, optional ASIO when available, Windows MIDI/WinRT MIDI as exposed by JUCE.

**Spec:** `docs/superpowers/specs/2026-09-22-jeff-daw-studio-design.md`

## Global Constraints

- The application is free and open source under AGPLv3; VST3 integration will use Steinberg's MIT-licensed SDK in a later milestone.
- The first supported release target is Windows 10/11 x64.
- Core operation requires no account, subscription, cloud service, or paid API.
- WASAPI is the default audio path; ASIO is optional when a compatible driver is installed.
- USB MIDI is preferred for latency; Bluetooth MIDI is supported when Windows exposes the paired device.
- The real-time audio callback must not allocate memory, block on locks, perform file I/O, scan plug-ins, or call UI code.
- The interface defaults to simple-to-moderately-advanced, with primary text at least 16 px equivalent and regular control text at least 14 px equivalent.
- The Orba Mirror must display the connected Orba 1 or Orba 2 profile, eight radial pads, simultaneous touches, pressure/velocity intensity, held state, supported expression, and playback-ready normalized events.
- Hardware-only verification may remain explicitly marked until it is performed on Jeff's ThinkCentre Mini PC; software tests must not pretend hardware was exercised.

## Review Focus

- **MIDI storms and queue saturation:** the newest note-off/control state must not leave a pad visually stuck; Task 2 pins bounded overflow behavior and Task 7 pins stale-state recovery.
- **Hot unplug/replug with duplicate display names:** stable identifiers must restore the exact prior route rather than the first same-named device; Task 5 tests duplicate-name reconnection.
- **Malformed or unusual MIDI/MPE values:** decoding must clamp normalized values and ignore unsupported messages without throwing; Task 3 tests extremes and unknown messages.
- **Audio configuration failure:** an unavailable sample rate or input must leave the app usable and show a plain-language error; Task 6 tests failed initialization and recovery.
- **UI lag during dense expression input:** visualization coalesces replaceable expression updates while retaining note transitions; Task 7 tests a 10,000-event burst and bounded repaint state.

---

## Planned File Structure

```text
CMakeLists.txt                         Project options, JUCE/Catch2 pins, targets
LICENSE                               AGPLv3 license text
README.md                             Build, run, device, and verification guidance
cmake/Dependencies.cmake              FetchContent declarations
src/app/AppMetadata.h                 Product constants and version
src/app/Main.cpp                      JUCE application entry point
src/app/MainWindow.h/.cpp             Native top-level window
src/app/RootComponent.h/.cpp          Main layout and service ownership
src/audio/AudioBackend.h              Test seam for audio device operations
src/audio/JuceAudioBackend.h/.cpp      JUCE AudioDeviceManager adapter
src/audio/AudioEngine.h/.cpp           Real-time callback, audition tone, metronome
src/midi/PerformanceEvent.h            Normalized MIDI/MPE event value type
src/midi/SpscEventQueue.h              Bounded real-time-safe event bridge
src/midi/DeviceIdentity.h              Stable MIDI device identity
src/midi/DeviceProfile.h               Hardware decoding interface
src/midi/OrbaProfile.h/.cpp            Orba 1/2 detection and decoding
src/midi/Jt4000Profile.h/.cpp          JT-4000 detection and decoding
src/midi/ProfileRegistry.h/.cpp        Profile selection
src/midi/MidiBackend.h                 Test seam for enumeration and open/close
src/midi/JuceMidiBackend.h/.cpp        JUCE MIDI adapter
src/midi/MidiDeviceService.h/.cpp      Discovery, routing, reconnect, callbacks
src/transport/TransportClock.h/.cpp    Sample-domain transport and metronome phase
src/ui/DeviceCenterModel.h/.cpp        Device status and diagnostics presentation
src/ui/DeviceCenterComponent.h/.cpp    Device selection and diagnostics controls
src/ui/MidiMonitorModel.h              Bounded human-readable MIDI activity list
src/ui/OrbaMirrorModel.h/.cpp          Eight-pad and expression visualization state
src/ui/OrbaMirrorComponent.h/.cpp      Original Orba-style radial rendering
src/ui/TransportComponent.h/.cpp       Play, stop, tempo, metronome controls
tests/CMakeLists.txt                   Catch2 test executable and discovery
tests/app/AppMetadataTests.cpp
tests/audio/AudioEngineTests.cpp
tests/midi/SpscEventQueueTests.cpp
tests/midi/DeviceProfileTests.cpp
tests/midi/MidiDeviceServiceTests.cpp
tests/transport/TransportClockTests.cpp
tests/ui/DeviceCenterModelTests.cpp
tests/ui/OrbaMirrorModelTests.cpp
tests/support/FakeAudioBackend.h
tests/support/FakeMidiBackend.h
.github/workflows/windows-foundation.yml Windows configure, build, test, artifact
```

### Task 1: Bootstrap the Native Application and Test Harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/Dependencies.cmake`
- Create: `LICENSE`
- Create: `.gitignore`
- Create: `README.md`
- Create: `src/app/AppMetadata.h`
- Create: `src/app/Main.cpp`
- Create: `src/app/MainWindow.h`
- Create: `src/app/MainWindow.cpp`
- Create: `src/app/RootComponent.h`
- Create: `src/app/RootComponent.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/app/AppMetadataTests.cpp`

**Interfaces:**
- Consumes: The approved design specification only.
- Produces: `jeff::daw::AppMetadata::{name, version, company, projectExtension}`, the `JeffDawStudio` executable target, and the `JeffDawTests` test target used by every later task.

- [ ] **Step 1: Write the failing metadata test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/AppMetadata.h"

TEST_CASE("application identity is stable")
{
    REQUIRE(jeff::daw::AppMetadata::name == "Jeff DAW Studio");
    REQUIRE(jeff::daw::AppMetadata::version == "0.1.0");
    REQUIRE(jeff::daw::AppMetadata::company == "Jeffrey Chapin");
    REQUIRE(jeff::daw::AppMetadata::projectExtension == ".jds");
}
```

- [ ] **Step 2: Add CMake dependency and test targets, then verify the test cannot compile**

Pin JUCE to `8.0.12` and Catch2 to `v3.15.2` with `FetchContent`. Configure with:

```powershell
cmake -S . -B build -DJDS_BUILD_TESTS=ON
cmake --build build --config Debug --target JeffDawTests
```

Expected: build fails because `app/AppMetadata.h` does not exist.

- [ ] **Step 3: Implement product metadata and the smallest runnable window**

```cpp
#pragma once
#include <string_view>

namespace jeff::daw::AppMetadata
{
inline constexpr std::string_view name { "Jeff DAW Studio" };
inline constexpr std::string_view version { "0.1.0" };
inline constexpr std::string_view company { "Jeffrey Chapin" };
inline constexpr std::string_view projectExtension { ".jds" };
}
```

Create a JUCE `JUCEApplication` in `Main.cpp`, an owned `MainWindow`, and a `RootComponent` that displays the product name and the message `Connect an instrument to begin`. Set the window minimum to `960x640`, default to `1280x800`, and use a dark neutral background with text sizes matching the global constraints.

- [ ] **Step 4: Add repository fundamentals**

Copy the unmodified GNU Affero General Public License v3 text from `https://www.gnu.org/licenses/agpl-3.0.txt` into `LICENSE`. Ignore `/build/`, `/.vs/`, `/out/`, CMake user presets, IDE artifacts, and packaged binaries. Document these exact Windows prerequisites in `README.md`: Git, CMake 4.2+, Visual Studio 2026 Community with Desktop development with C++, and a Windows 10/11 x64 machine.

- [ ] **Step 5: Run the first green build and test**

```powershell
cmake -S . -B build -DJDS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `AppMetadataTests` passes and `JeffDawStudio.exe` is produced.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt cmake LICENSE .gitignore README.md src/app tests/CMakeLists.txt tests/app
git commit -m "build: bootstrap native JUCE application"
```

### Task 2: Add Normalized Performance Events and the Real-Time Queue

**Files:**
- Create: `src/midi/PerformanceEvent.h`
- Create: `src/midi/SpscEventQueue.h`
- Create: `tests/midi/SpscEventQueueTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: C++20 and Catch2 test target from Task 1.
- Produces: `PerformanceEvent`, `PerformanceEventType`, `ExpressionKind`, and `SpscEventQueue<Capacity>::push/pop/droppedCount` for MIDI producers and UI consumers.

- [ ] **Step 1: Write failing FIFO, wraparound, and saturation tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "midi/SpscEventQueue.h"

using namespace jeff::daw;

TEST_CASE("performance events remain ordered across wraparound")
{
    SpscEventQueue<4> queue;
    REQUIRE(queue.push(PerformanceEvent::noteOn(0, 60, 0.5f, 10)));
    REQUIRE(queue.push(PerformanceEvent::noteOff(0, 60, 20)));
    REQUIRE(queue.pop()->sampleTime == 10);
    REQUIRE(queue.push(PerformanceEvent::noteOn(0, 61, 0.7f, 30)));
    REQUIRE(queue.pop()->sampleTime == 20);
    REQUIRE(queue.pop()->note == 61);
}

TEST_CASE("a full queue rejects without blocking and counts the drop")
{
    SpscEventQueue<2> queue;
    REQUIRE(queue.push(PerformanceEvent::noteOn(0, 60, 1.0f, 0)));
    REQUIRE_FALSE(queue.push(PerformanceEvent::noteOff(0, 60, 1)));
    REQUIRE(queue.droppedCount() == 1);
}
```

- [ ] **Step 2: Run the focused test and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*queue*"
```

Expected: compilation fails because the event and queue headers do not exist.

- [ ] **Step 3: Implement trivially copyable event values and a bounded SPSC ring**

`PerformanceEvent` must contain only fixed-size scalar values: type, expression kind, device slot, channel, note/pad index, normalized value, secondary normalized value, and `int64_t sampleTime`. Factory functions clamp normalized input to `[0.0f, 1.0f]`. `SpscEventQueue` uses atomic read/write indices with acquire/release ordering and `std::array<PerformanceEvent, Capacity>`; it performs no heap allocation.

```cpp
enum class PerformanceEventType : std::uint8_t
{
    noteOn, noteOff, expression, modeChange, gesture
};

enum class ExpressionKind : std::uint8_t
{
    pressure, pitch, timbre, radiate, vibrato, tilt, shake, spin, none
};

static_assert(std::is_trivially_copyable_v<PerformanceEvent>);
```

- [ ] **Step 4: Add a burst test that proves capacity stays bounded**

Push 10,000 expression events into `SpscEventQueue<256>` without consuming. Assert that accepted plus dropped equals 10,000, accepted never exceeds 255, and the call completes without retry loops.

- [ ] **Step 5: Run all tests**

```powershell
cmake --build build --config Debug --target JeffDawTests
ctest --test-dir build -C Debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/midi tests/CMakeLists.txt tests/midi
git commit -m "feat: add real-time performance event queue"
```

### Task 3: Decode Orba and JT-4000 MIDI Through Device Profiles

**Files:**
- Create: `src/midi/DeviceIdentity.h`
- Create: `src/midi/DeviceProfile.h`
- Create: `src/midi/OrbaProfile.h`
- Create: `src/midi/OrbaProfile.cpp`
- Create: `src/midi/Jt4000Profile.h`
- Create: `src/midi/Jt4000Profile.cpp`
- Create: `src/midi/ProfileRegistry.h`
- Create: `src/midi/ProfileRegistry.cpp`
- Create: `tests/midi/DeviceProfileTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PerformanceEvent` from Task 2 and `juce::MidiMessage`.
- Produces: `DeviceIdentity { juce::String stableId, name; }`, allocation-free `DeviceProfile::decode(const juce::MidiMessage&, std::int64_t) -> DecodedEventBatch`, `ProfileRegistry::match(const DeviceIdentity&)`, and profile kinds `generic`, `orba1`, `orba2`, `jt4000`.

- [ ] **Step 1: Write failing profile matching tests**

```cpp
TEST_CASE("known instrument names select their profiles")
{
    ProfileRegistry registry;
    REQUIRE(registry.match({ "usb-a", "Artiphon Orba" }).kind() == ProfileKind::orba1);
    REQUIRE(registry.match({ "usb-b", "Artiphon Orba 2" }).kind() == ProfileKind::orba2);
    REQUIRE(registry.match({ "usb-c", "JT-4000 MICRO" }).kind() == ProfileKind::jt4000);
    REQUIRE(registry.match({ "usb-d", "Unknown Keyboard" }).kind() == ProfileKind::generic);
}
```

- [ ] **Step 2: Write failing decode tests for notes and standard MPE dimensions**

Use JUCE message constructors for note-on, note-off, channel pressure, pitch wheel minimum/center/maximum, and CC74. Assert that values are clamped, unsupported system messages return an empty vector, and all returned sample timestamps match the supplied timestamp.

- [ ] **Step 3: Run the profile tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*profile*"
```

Expected: compilation fails because profile types do not exist.

- [ ] **Step 4: Implement conservative device matching and decoding**

Matching is case-insensitive and treats `Orba 2` before generic `Orba`. Decode universally supported note and MPE messages without inventing proprietary gesture mappings. Store Orba-specific CC mappings in a fixed table owned by `OrbaProfile`; label mappings `verified` only after captured hardware fixtures confirm them. Until verified, unknown CC values produce a generic timbre expression for the MIDI monitor but do not drive a named gesture animation.

```cpp
class DeviceProfile
{
public:
    virtual ~DeviceProfile() = default;
    virtual ProfileKind kind() const noexcept = 0;
    virtual DecodedEventBatch decode(
        const juce::MidiMessage& message,
        std::int64_t sampleTime) const = 0;
};

struct DecodedEventBatch
{
    std::array<PerformanceEvent, 4> events {};
    std::uint8_t size { 0 };
};
```

`DecodedEventBatch` is returned by value and never allocates; one MIDI message may therefore emit at most four normalized events.

- [ ] **Step 5: Add malformed-value and unknown-message coverage**

Test pitch wheel values `0`, `8192`, and `16383`; note velocities `0.0f` and `1.0f`; aftertouch extremes; an active-sensing message; a SysEx packet; and device names containing mixed case and leading/trailing whitespace. Expected: no exceptions, normalized output stays within `[0,1]`, and unsupported messages return no performance event.

- [ ] **Step 6: Run all tests**

```powershell
cmake --build build --config Debug --target JeffDawTests
ctest --test-dir build -C Debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/midi tests/CMakeLists.txt tests/midi
git commit -m "feat: add Orba and JT-4000 MIDI profiles"
```

### Task 4: Implement Sample-Domain Transport and Metronome Timing

**Files:**
- Create: `src/transport/TransportClock.h`
- Create: `src/transport/TransportClock.cpp`
- Create: `tests/transport/TransportClockTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: sample rate and block sizes supplied by the later audio engine.
- Produces: `TransportClock::prepare(double)`, `setTempo(double)`, `setPlaying(bool)`, `setMetronomeEnabled(bool)`, `process(int) -> TransportBlock`, and `reset()`.

- [ ] **Step 1: Write failing timing tests**

```cpp
TEST_CASE("metronome emits quarter notes at 120 bpm")
{
    TransportClock clock;
    clock.prepare(48000.0);
    clock.setTempo(120.0);
    clock.setPlaying(true);
    clock.setMetronomeEnabled(true);
    const auto block = clock.process(48001);
    REQUIRE(block.clickCount == 3);
    REQUIRE(block.clickOffsets[0] == 0);
    REQUIRE(block.clickOffsets[1] == 24000);
    REQUIRE(block.clickOffsets[2] == 48000);
    REQUIRE(block.endSample == 48001);
}

TEST_CASE("invalid tempo is clamped to the supported range")
{
    TransportClock clock;
    clock.setTempo(-5.0);
    REQUIRE(clock.tempo() == 20.0);
    clock.setTempo(999.0);
    REQUIRE(clock.tempo() == 300.0);
}
```

- [ ] **Step 2: Run the tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*metronome*"
```

- [ ] **Step 3: Implement transport using integer sample positions**

`TransportBlock` contains `startSample`, `endSample`, `isPlaying`, `std::array<int, 16> clickOffsets`, and `std::uint8_t clickCount`. Tempo supports `20.0–300.0 BPM`. `process()` advances only while playing, preserves fractional beat phase across blocks, and returns click offsets within the current block without allocation. If an unusually large block contains more than 16 clicks, it retains the first 16 and increments an atomic timing-warning counter.

- [ ] **Step 4: Add block-boundary, stop/resume, reset, and sample-rate tests**

Cover clicks exactly on block boundaries, several irregular block sizes that total one beat, stopped blocks that do not advance, resume without an unintended reset, explicit reset to sample zero, and re-prepare from 44.1 kHz to 48 kHz.

- [ ] **Step 5: Run all tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/transport tests/CMakeLists.txt tests/transport
git commit -m "feat: add sample-accurate transport clock"
```

### Task 5: Add MIDI Enumeration, Routing, and Stable Reconnection

**Files:**
- Create: `src/midi/MidiBackend.h`
- Create: `src/midi/JuceMidiBackend.h`
- Create: `src/midi/JuceMidiBackend.cpp`
- Create: `src/midi/MidiDeviceService.h`
- Create: `src/midi/MidiDeviceService.cpp`
- Create: `tests/support/FakeMidiBackend.h`
- Create: `tests/midi/MidiDeviceServiceTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `DeviceIdentity`, `ProfileRegistry`, `PerformanceEvent`, and `SpscEventQueue<4096>`.
- Produces: `MidiDeviceService::refresh()`, `devices()`, `connect(stableId)`, `disconnect(stableId)`, `selectedDeviceIds()`, `statusFor(stableId)`, and normalized events pushed from JUCE MIDI callbacks.

- [ ] **Step 1: Write failing connection and reconnection tests with a fake backend**

```cpp
TEST_CASE("service reconnects the same stable id after hot plug")
{
    FakeMidiBackend backend({ { "id-a", "Orba 2" } });
    SpscEventQueue<4096> events;
    MidiDeviceService service(backend, events);
    REQUIRE(service.connect("id-a"));
    backend.setDevices({});
    service.refresh();
    REQUIRE(service.statusFor("id-a") == DeviceStatus::disconnected);
    backend.setDevices({ { "id-a", "Orba 2" } });
    service.refresh();
    REQUIRE(service.statusFor("id-a") == DeviceStatus::connected);
}
```

- [ ] **Step 2: Add the duplicate-name failure case**

Start with IDs `orba-left` and `orba-right`, both named `Orba 2`; select only `orba-right`; unplug both; re-add in reversed order. Assert only `orba-right` reconnects.

- [ ] **Step 3: Run focused tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*reconnect*"
```

- [ ] **Step 4: Implement backend seam and JUCE adapter**

`MidiBackend` returns stable identifiers and opens an input with a callback token owned by the service. `JuceMidiBackend` wraps `juce::MidiInput::getAvailableDevices()` and `juce::MidiInput::openDevice(identifier, callback)`. `MidiDeviceService` retains desired IDs independently of current enumeration, decodes callback messages through the matched profile, and pushes values to the queue without retrying when it is full.

- [ ] **Step 5: Add callback and saturation tests**

Emit a note-on through `FakeMidiBackend` and assert the queue receives the corresponding event. Fill a small injected queue, emit 100 messages, and assert the callback returns while the dropped counter increases. Emit from an unselected device and assert no event is queued.

- [ ] **Step 6: Run all tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/midi tests/CMakeLists.txt tests/midi tests/support
git commit -m "feat: add resilient MIDI device service"
```

### Task 6: Add Audio Device Setup and the Real-Time Foundation Engine

**Files:**
- Create: `src/audio/AudioBackend.h`
- Create: `src/audio/JuceAudioBackend.h`
- Create: `src/audio/JuceAudioBackend.cpp`
- Create: `src/audio/AudioEngine.h`
- Create: `src/audio/AudioEngine.cpp`
- Create: `tests/support/FakeAudioBackend.h`
- Create: `tests/audio/AudioEngineTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TransportClock` and user audio configuration.
- Produces: `AudioEngine::prepare(sampleRate, maximumBlockSize, outputChannels)`, `processBlock(juce::AudioBuffer<float>&)`, transport controls, metronome output, `AudioStatus`, and `JuceAudioBackend::initialiseDefaultDevice()`.

- [ ] **Step 1: Write failing deterministic render tests**

```cpp
TEST_CASE("playing audition tone produces bounded non-zero audio")
{
    AudioEngine engine;
    engine.prepare(48000.0, 512, 2);
    engine.setAuditionNote(69, 0.25f);
    juce::AudioBuffer<float> block(2, 512);
    block.clear();
    engine.processBlock(block);
    REQUIRE(block.getMagnitude(0, 0, 512) > 0.01f);
    REQUIRE(block.getMagnitude(0, 0, 512) <= 0.25f);
    REQUIRE(block.getMagnitude(1, 0, 512) == Catch::Approx(block.getMagnitude(0, 0, 512)));
}
```

- [ ] **Step 2: Write failing initialization-recovery tests**

Configure `FakeAudioBackend` to reject 96 kHz, assert the model reports `AudioStatus::configurationFailed` with a non-empty plain-language message, then accept 48 kHz and assert status becomes `ready` without restarting the service.

- [ ] **Step 3: Run focused tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*audio*"
```

- [ ] **Step 4: Implement the backend and engine**

Use `juce::AudioDeviceManager` in `JuceAudioBackend`, requesting zero inputs and two outputs by default while allowing the user to enable the camera microphone. `AudioEngine` owns preallocated oscillator and metronome state. `processBlock` clears output, obtains click offsets from `TransportClock`, renders short click envelopes, renders the audition oscillator when active, applies a fixed safety gain, and updates atomic peak/underrun counters.

- [ ] **Step 5: Add real-time contract checks**

In Debug builds, install JUCE allocation assertions or a test allocation counter around 1,000 prepared `processBlock` calls and assert zero allocations after warm-up. Verify silence while stopped with no audition note, finite output at all times, click placement from Task 4, mono-output safety, and a zero-channel buffer that returns without error.

- [ ] **Step 6: Run all tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/audio tests/CMakeLists.txt tests/audio tests/support
git commit -m "feat: add Windows audio engine foundation"
```

### Task 7: Build the Orba Mirror State Model

**Files:**
- Create: `src/ui/OrbaMirrorModel.h`
- Create: `src/ui/OrbaMirrorModel.cpp`
- Create: `tests/ui/OrbaMirrorModelTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: normalized `PerformanceEvent` values from Task 2.
- Produces: `OrbaMirrorModel::consume(event)`, `consumePending(queue, maxEvents)`, `tick(nowMilliseconds)`, `setProfile(ProfileKind)`, and immutable `OrbaMirrorSnapshot` containing model label, mode, eight pad states, expression indicators, dropped-event warning, and revision.

- [ ] **Step 1: Write failing pad-state tests**

```cpp
TEST_CASE("note activity lights and releases the matching radial pad")
{
    OrbaMirrorModel model;
    model.setProfile(ProfileKind::orba2);
    model.consume(PerformanceEvent::noteOn(0, 2, 0.8f, 10));
    REQUIRE(model.snapshot().pads[2].held);
    REQUIRE(model.snapshot().pads[2].intensity == Catch::Approx(0.8f));
    model.consume(PerformanceEvent::noteOff(0, 2, 20));
    REQUIRE_FALSE(model.snapshot().pads[2].held);
}
```

- [ ] **Step 2: Add simultaneous touch, pressure, and model-label tests**

Hold pads 1, 3, and 7 together; update pressure only on pad 3; release pad 1. Assert the other pads remain held. Assert profile changes label the model `Orba 1`, `Orba 2`, or `Generic MIDI` without clearing unrelated diagnostics.

- [ ] **Step 3: Run focused tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*Orba Mirror*"
```

- [ ] **Step 4: Implement bounded state consumption and visual decay**

`consumePending` handles at most 512 events per UI tick. It always applies note-on, note-off, and mode transitions in sequence; replaceable expression updates for the same pad and dimension are coalesced to their newest value within the batch. Released pad intensity decays over 180 ms. If queue drops were observed, pads still held without fresh activity for 2,000 ms are released and the snapshot exposes `Input overflow recovered` until the next clean second.

- [ ] **Step 5: Pin burst and stuck-pad recovery behavior**

Feed 10,000 alternating pressure/timbre values plus note transitions through a queue. Assert one `consumePending` call processes no more than 512, the snapshot revision increases once per batch rather than once per replaceable expression message, and `tick()` releases a stale held pad after overflow recovery without releasing a normally held pad when no drop occurred.

- [ ] **Step 6: Run all tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/ui tests/CMakeLists.txt tests/ui
git commit -m "feat: model live Orba performance state"
```

### Task 8: Render the Orba Mirror and Foundation Controls

**Files:**
- Create: `src/ui/OrbaMirrorComponent.h`
- Create: `src/ui/OrbaMirrorComponent.cpp`
- Create: `src/ui/TransportComponent.h`
- Create: `src/ui/TransportComponent.cpp`
- Create: `src/ui/MidiMonitorModel.h`
- Modify: `src/app/RootComponent.h`
- Modify: `src/app/RootComponent.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `OrbaMirrorSnapshot`, `AudioEngine` transport controls, and normalized performance events.
- Produces: the recognizable first product surface: live radial instrument, transport bar, connection status, MIDI activity, and mouse/touch-generated note events.

- [ ] **Step 1: Add a component geometry seam before painting**

Define `OrbaLayout calculateOrbaLayout(juce::Rectangle<float> bounds)` returning a center, body radius, and eight pad paths. Add a Catch2 test inside `tests/ui/OrbaMirrorModelTests.cpp` that verifies all pad centers are within the body, adjacent centers differ, and a `320x320` layout has no overlapping pad hit regions.

- [ ] **Step 2: Run the geometry test and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*layout*"
```

- [ ] **Step 3: Implement an original radial rendering**

Draw a dark circular device body, eight wedge-shaped pads, a central mode/status area, and compact labels. Use no copied Artiphon artwork, logo, or product photography. Map pad intensity to luminance and a subtle outer glow; use an additional outline pattern for held state so activity is not color-only. Draw pressure, pitch, and timbre indicators from the snapshot. Set accessible titles such as `Orba pad 1, held, intensity 80 percent`.

- [ ] **Step 4: Add pointer and keyboard playability**

Hit-test mouse/touch coordinates against the eight pad paths. Pointer down emits normalized note-on, drag updates pressure/timbre when supported, and pointer up/cancel emits note-off. Map computer keys `A S D F J K L ;` to pads 0–7 while the component has focus; suppress auto-repeat note-ons.

- [ ] **Step 5: Implement the transport and bounded MIDI monitor**

Transport controls expose Play/Stop, metronome toggle, and an editable `20–300 BPM` field. `MidiMonitorModel` retains only the newest 128 display rows and formats device, channel, event, note/pad, value, and timestamp without being called on the audio thread.

- [ ] **Step 6: Wire the first runnable product slice**

`RootComponent` owns the event queue, profile registry, MIDI service, audio backend/engine, mirror model, and a 60 Hz UI timer. The first viewport places transport at top, Orba Mirror centrally, MIDI activity to the right on wide screens and below on narrow screens, and a compact `Set up audio & MIDI` action when devices are unavailable.

- [ ] **Step 7: Build, launch, and perform the non-hardware interaction check**

```powershell
cmake --build build --config Debug --target JeffDawStudio
build\JeffDawStudio_artefacts\Debug\JeffDawStudio.exe
```

Expected: the app opens at `1280x800`; keyboard and pointer presses illuminate the correct radial pad; simultaneous pointer/keyboard state does not stick; Play advances transport; metronome sounds; resizing to `960x640` does not clip controls.

- [ ] **Step 8: Run tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/app src/ui
git commit -m "feat: render interactive Orba Mirror workspace"
```

### Task 9: Add Device Center, Diagnostics, and Local Preferences

**Files:**
- Create: `src/ui/DeviceCenterModel.h`
- Create: `src/ui/DeviceCenterModel.cpp`
- Create: `src/ui/DeviceCenterComponent.h`
- Create: `src/ui/DeviceCenterComponent.cpp`
- Create: `tests/ui/DeviceCenterModelTests.cpp`
- Modify: `src/app/RootComponent.h`
- Modify: `src/app/RootComponent.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: audio backend status, MIDI service device/status lists, engine peak/underrun counters, queue drop count, and JUCE application properties.
- Produces: selectable inputs/outputs, sample rate and buffer size controls, reconnect actions, persisted stable IDs, and plain-language diagnostics.

- [ ] **Step 1: Write failing presentation-model tests**

Create states for ready audio, unavailable microphone, failed 96 kHz configuration, connected USB Orba 2, disconnected Bluetooth Orba 1, duplicate device names, queue drops, and buffer underruns. Assert each becomes a stable row keyed by ID, selection is ID-based, and error messages recommend a concrete action without exposing raw exceptions.

- [ ] **Step 2: Run focused tests and confirm failure**

```powershell
cmake --build build --config Debug --target JeffDawTests
build\tests\Debug\JeffDawTests.exe "*Device Center*"
```

- [ ] **Step 3: Implement model and preference persistence**

Persist selected audio device type, input/output stable IDs, sample rate, buffer size, selected MIDI IDs, and preferred profile override using `juce::PropertiesFile`. Read preferences before device initialization; if a saved device is absent, retain the desired ID for reconnection and choose a safe temporary output without overwriting the saved choice.

- [ ] **Step 4: Implement the Device Center component**

Use labeled combo boxes for audio driver/device, input, output, sample rate, buffer size, and MIDI inputs. Display `USB recommended for lowest latency` beside USB MIDI and `Bluetooth response depends on Windows` beside Bluetooth devices when identifiable. Include live input/output meters, underrun count, MIDI activity, queue overflow status, and Retry buttons.

- [ ] **Step 5: Exercise failure and recovery manually with software controls**

Select an invalid or unavailable audio configuration and confirm the main UI remains responsive, the message explains how to choose another device, and retrying a valid configuration clears the error. Remove and restore a virtual MIDI device if available; otherwise verify the fake-backend automated case and label physical hot-plug verification as pending in `README.md`.

- [ ] **Step 6: Run tests and commit**

```powershell
ctest --test-dir build -C Debug --output-on-failure
git add CMakeLists.txt src/app src/ui tests/CMakeLists.txt tests/ui README.md
git commit -m "feat: add audio and MIDI Device Center"
```

### Task 10: Add Windows CI, Portable Artifact, and Hardware Verification Guide

**Files:**
- Create: `.github/workflows/windows-foundation.yml`
- Create: `docs/hardware/orba-jt4000-foundation-checklist.md`
- Modify: `README.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: all foundation targets and tests from Tasks 1–9.
- Produces: a repeatable Windows Release build, test result, zipped portable artifact, and an honest checklist for Jeff's ThinkCentre hardware validation.

- [ ] **Step 1: Add a Windows workflow that configures, builds, and tests**

```yaml
name: Windows Foundation
on:
  push:
    branches: [main]
  pull_request:
jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: lukka/get-cmake@latest
        with:
          cmakeVersion: 4.4.0
      - name: Configure
        run: cmake -S . -B build -DJDS_BUILD_TESTS=ON
      - name: Build
        run: cmake --build build --config Release
      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure
      - name: Stage portable app
        shell: pwsh
        run: cmake --install build --config Release --prefix package
      - uses: actions/upload-artifact@v4
        with:
          name: Jeff-DAW-Studio-Windows-foundation
          path: package
```

- [ ] **Step 2: Add deterministic install rules and verify locally**

Install the executable and required JUCE runtime assets into `Jeff DAW Studio/`. Run:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix package
```

Expected: `package/Jeff DAW Studio/JeffDawStudio.exe` exists and starts without source-tree-relative files.

- [ ] **Step 3: Write the hardware checklist with explicit pass/fail evidence**

The checklist contains separate USB and Bluetooth rows for Orba 1 and Orba 2, USB MIDI for JT-4000, camera-microphone capture visibility, default WASAPI output, optional ASIO, eight-pad correspondence, simultaneous touches, pressure, pitch, timbre, named gestures only when verified, unplug/replug, 15-minute idle, 15-minute dense performance, and measured round-trip perception. Every row records date, Windows build, firmware, connection, expected behavior, actual behavior, pass/fail, and notes.

- [ ] **Step 4: Document home-PC setup**

Add exact instructions to clone the repository, configure a Release build, run tests, launch the app, and download the GitHub Actions portable artifact. Explain that Bluetooth pairing occurs in Windows first and that the Orba/JT-4000 headphone output requires a stereo audio interface for clean hardware-audio recording.

- [ ] **Step 5: Run the complete verification set**

```powershell
cmake -S . -B build -DJDS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix package
```

Expected: all automated tests pass and the portable package is complete. Hardware rows remain marked `Not run` until exercised on Jeff's ThinkCentre; they are never marked passed from simulated tests.

- [ ] **Step 6: Commit the completed foundation milestone**

```bash
git add .github CMakeLists.txt README.md docs/hardware
git commit -m "ci: package Windows audio MIDI foundation"
```

## Completion Gate

The milestone is complete only when:

- Debug and Release builds succeed on Windows.
- All Catch2 tests pass.
- The portable package launches outside the build tree.
- Pointer and keyboard input drive the correct Orba Mirror pads.
- The Device Center survives invalid audio configuration and exposes recovery.
- GitHub Actions publishes the Windows foundation artifact.
- Hardware-dependent checks are either evidenced on the ThinkCentre or visibly marked `Not run`.
- The branch is reviewed against the design specification before merge or release tagging.

After this foundation lands, create separate implementation plans for Jam Mode, Studio Mode, VST3 hosting/export, and the final Windows installer/release. Those plans consume the interfaces defined here rather than reopening the architecture.
