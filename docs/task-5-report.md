# Task 5 implementation checkpoint

Status: DONE_WITH_CONCERNS — shared prepared playback and transport are implemented. Independent review and native Windows validation remain pending.

## What changed

- Added immutable `PlaybackSnapshot` track settings around `PreparedAudio`, with validated tempo/channel/pan ranges and a reusable mixer. Mono tracks use equal-power pan; stereo stays unity at center with balance attenuation; wider channel layouts fold odd/even channels by averaging.
- Added `StemPlayback`, a background worker and fixed eight-slot stereo read-ahead ring. The worker performs prepared-cache reads, sample-rate conversion, mixing, and snapshot reclamation. The audio callback only reads ready fixed buffers, drops stale generation/rate/tempo/seek slots within the fixed ring bound, adds output samples, and counts underruns. It does not access prepared handles, allocate, perform file I/O, take locks, or destroy snapshot data.
- Snapshot publication exchanges one atomic latest-pending pointer. A superseded pending snapshot is destroyed by the publishing non-audio thread; accepted active snapshots and their `PreparedAudio` handles are owned/released by the worker. Ring contents carry snapshot generation and transport epochs, so a seek, tempo change, device-rate change, or replacement invalidates stale data.
- Transport position is stored in beats. `seekBeats` queues finite nonnegative positions and applies them at the next processed block while playing or paused. Sample-rate changes preserve beat position, and tempo changes preserve the playhead beat. Existing metronome and audition output continue through `AudioEngine` alongside stem audio.
- Updated the tracked handoff note. Stem workspace and APK remain not ready; tasks 6–10 remain pending.

## Verification

- Build: `. ..\toolchain\activate.ps1; cmake --build build --target JeffDawTests --parallel 4` — passed after implementation and again after final playback bounds/rate adjustments.
- Focused checks: `ctest --test-dir build -R 'playback mix|rapid snapshot|transport keeps beat|tempo changes and paused' --output-on-failure` — 4/4 passed after the final source changes. Coverage checks 44.1→48 kHz beat placement, tempo change, paused seek, equal-power mono/stereo balance, and concurrent rapid replacement while a callback thread drains bounded slots.
- Full portable suite: `ctest --test-dir build --output-on-failure` — 60/60 passed before the final two localized playback adjustments; the focused four checks and build passed after them. No native Windows build was available in this local toolchain.
- TDD RED evidence was not captured before implementation. Tests were added with the completed behavior and then run; do not treat this checkpoint as RED/GREEN TDD evidence.

## Files

`src/stems/PlaybackSnapshot.h/.cpp`, `src/stems/StemPlayback.h/.cpp`, `src/audio/AudioEngine.h/.cpp`, `src/transport/TransportClock.h/.cpp`, root and test CMake lists, `tests/StemPlaybackTests.cpp`, and `docs/stem-workflow-progress.md`.

## Self-review and limits

- `StemPlayback` destruction requires the device callback to be quiesced first. The app's current component declaration order destroys its audio backend before its engine; other owners must preserve that lifecycle rule.
- This checkpoint tests transport placement and mixer behavior, plus concurrent snapshot handoff, but does not yet contain an end-to-end prepared-file playback fixture or instrument callback allocations/I/O at runtime. The callback path is structurally limited to fixed buffers and atomic state; independent review should inspect those invariants.
- Project-to-snapshot assembly and user-facing playback controls are not integrated yet. A caller prepares a snapshot and supplies its track/channel mapping.
- No Android/native target verification is claimed.
