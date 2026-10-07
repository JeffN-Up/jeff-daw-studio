# Task 5 implementation checkpoint

Status: implementation checkpoint saved; independent re-review and native Windows validation remain pending. Do not treat Task 5 as complete until those gates pass.

## What changed

- Added immutable `PlaybackSnapshot` track settings around `PreparedAudio`, with validated tempo/channel/pan ranges and a reusable mixer. Mono tracks use equal-power pan; stereo stays unity at center with balance attenuation; wider channel layouts fold odd/even channels by averaging.
- Added `StemPlayback`, a background worker and fixed eight-slot stereo read-ahead ring. The worker performs prepared-cache reads, sample-rate conversion, mixing, and snapshot reclamation. The audio callback only reads ready fixed buffers, drops stale generation/rate/tempo/seek slots within the fixed ring bound, adds output samples, and counts underruns. It does not access prepared handles, allocate, perform file I/O, take locks, or destroy snapshot data.
- Snapshot publication exchanges one atomic latest-pending pointer. A superseded pending snapshot is destroyed by the publishing non-audio thread; accepted active snapshots and their `PreparedAudio` handles are owned/released by the worker. Ring contents carry snapshot generation and transport epochs, so a seek, tempo change, device-rate change, or replacement invalidates stale data.
- Grouped caches are read using the cache's full channel count, then each `PlaybackTrack` selects its validated `[firstChannel, firstChannel + channels)` slice. Snapshot construction rejects invalid slices before publication. Read-ahead source windows are capped at 16 source frames per device frame and 65,540 frames total (about 16 MiB at 64 channels); larger source/device/tempo ratios produce worker-visible status instead of unbounded allocation.
- Transport position is stored in beats. `seekBeats` queues finite nonnegative positions and applies them at the next processed block while playing or paused. Sample-rate changes preserve beat position, and tempo changes preserve the playhead beat. Existing metronome and audition output continue through `AudioEngine` alongside stem audio.
- Updated the tracked handoff note. Stem workspace and APK remain not ready; tasks 6–10 remain pending.

## Verification

- TDD RED: `. ..\toolchain\activate.ps1; ctest --test-dir build -R 'grouped prepared playback selects' --output-on-failure` — failed 0/1 as expected, reporting `Prepared playback read failed: Invalid prepared sample range or channel planes.` The disk-backed fixture contains a four-channel grouped cache and selects its second two-channel member.
- GREEN build: `. ..\toolchain\activate.ps1; cmake --build build --target JeffDawTests --parallel 4` — passed after the channel read/slice fix.
- Focused GREEN: `. ..\toolchain\activate.ps1; ctest --test-dir build -R 'grouped prepared playback selects|prepared playback follows paused|prepared playback applies mute solo|rapid snapshot replacement' --output-on-failure` — 4/4 passed. Cases render actual prepared disk caches and verify grouped channel selection/sample placement, paused seek output through a 44.1→48 kHz device-rate change, worker mute/solo/pan, rapid nonempty snapshot replacement during concurrent callback consumption, and cache retirement after replacement.
- The rate/seek case failed once because a worker already paused on a seek did not re-anchor its read cursor when playback resumed; the worker now starts the next read-ahead from the callback's current end beat on paused-to-playing transitions. The focused GREEN command above passed after this fix.
- The earlier full portable run passed 60/60 before these review-round changes. Per the checkpoint instruction, the full suite was not rerun after this focused fix. No native Windows build was available in this local toolchain.

## Files

`src/stems/PlaybackSnapshot.h/.cpp`, `src/stems/StemPlayback.h/.cpp`, `src/audio/AudioEngine.h/.cpp`, `src/transport/TransportClock.h/.cpp`, root and test CMake lists, `tests/StemPlaybackTests.cpp`, and `docs/stem-workflow-progress.md`.

## Self-review and limits

- `StemPlayback` destruction requires the device callback to be quiesced first. The app's current component declaration order destroys its audio backend before its engine; other owners must preserve that lifecycle rule.
- Prepared-cache playback now has end-to-end disk-backed fixture coverage. Runtime allocation/I/O instrumentation was not added; source review shows the callback reads fixed ring buffers and atomics only, while the worker performs cache reads and snapshot retirement.
- Project-to-snapshot assembly and user-facing playback controls are not integrated yet. A caller prepares a snapshot and supplies its track/channel mapping.
- Independent re-review and native Windows verification are still pending; no Android/native build is claimed.
