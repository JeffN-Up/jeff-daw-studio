# Cross-device Stem Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import, preserve/edit/sync, and export stems on Android and Windows, continuing the same project through Google Drive checkpoints.

**Architecture:** Both clients share project commands, timing maps, prepared playback, and offline rendering. Platform adapters own file access and device settings. Self-contained checkpoint packages carry original media and edit state between devices.

**Tech Stack:** C++20, existing JUCE 8.0.12, CMake 4.2+, Catch2, JUCE Projucer Android exporter, Signalsmith Stretch (MIT; pin an audited upstream commit with its dependencies when integrating).

**Spec:** `docs/superpowers/specs/2026-10-05-stem-workflow-design.md` (approved October 5, 2026).

## Global Constraints

- Preserve original media and leading silence; Preserve is the default.
- Linked stems share timing maps unless explicitly unlinked.
- Automatic analysis is an assistive suggestion, not a guarantee of musical compatibility.
- Audio callbacks perform no file I/O, allocation, or blocking locks.
- Google Drive uses explicit upload/download checkpoints; local save is not upload.
- GitHub stores source/build artifacts, not automatic music-project backups.
- Keep existing Orba, MIDI, metronome, and device functionality available on Windows.
- Windows and Android use separate packaging targets and local device settings.

## Review Focus

1. Files sharing a filename must receive distinct media identities (Task 2).
2. A tempo/reference change during analysis must not apply a stale proposal (Task 4).
3. Device sample-rate changes must preserve musical placement (Task 5).
4. Android provider streams may lack filesystem paths or fail mid-read/write (Task 9).
5. A newer unsupported schema must never overwrite the currently open project (Task 7).

## File and interface conventions

New units live in `src/stems/`, `src/project/`, `src/checkpoint/`, and `src/platform/`; UI stays in `src/ui/`. New tests are separate files under `tests/`. Modify root `CMakeLists.txt`, `cmake/Dependencies.cmake`, and `tests/CMakeLists.txt` as each unit is integrated. Existing foundation tests remain unchanged unless an interface extension requires updating their fixtures.

Use `Result<T>` (value or typed error/message), `CancellationToken` (atomic cancel flag), string `Id`, and signed 64-bit `Frame`. Define these in `src/project/Types.h`. Audio positions in persistent documents are seconds or beats with explicit units, never implicit device frames. `AudioAsset` stores media ID, relative path, checksum, channels, source rate, and frame count. `TimingMap` is monotonically increasing pairs of source seconds and destination beats. `Project` contains schema version, project/revision/parent IDs, tempo, tracks, assets, groups, and playhead beats. Unknown extension fields survive round trips.

## Verification commands

For each native unit, configure/build the `JeffDawTests` target and run its Catch2 tag through CTest:
`cmake -S . -B build -DJDS_BUILD_TESTS=ON -DJDS_BUILD_APP=ON`
`cmake --build build --config Release`
`ctest --test-dir build -C Release --output-on-failure`
Use `ctest -R <test-name>` for the failing/passing cycle; all tests run at release verification. Test names below must be registered by Catch2 discovery. An unavailable compiler or dependency is a reported blocker, not a passing test.

### Task 1: Project, timing maps, and undoable commands

**Files:** Create `src/project/{Types,Project,ProjectCommands}.h/.cpp`, `src/stems/TimingMap.h/.cpp`, `tests/ProjectTests.cpp`.
**Interfaces:** Produce `validate(const Project&) -> Result<void>`, `mapTime(const TimingMap&, double sourceSeconds) -> double destinationBeats`, and `ProjectHistory::apply/undo/redo`; commands operate on value snapshots. Define `Track` with clip bounds, gain, pan, mute/solo, asset ID, and timing-group ID.

- [ ] Add failing `ProjectPreserveUndo`: two stems with entries at 0.5s and 1.5s retain a 1s separation after shared movement; undo restores both. `TimingRejectsCrossing` rejects reversed, repeated, negative, or nonfinite markers.
- [ ] Run those named tests and confirm the expected missing-interface failures.
- [ ] Implement validated maps, group edits/unlink, non-destructive trim, and Reset Timing preserving mixer state.
- [ ] Run tests; commit `feat: add shared stem project and timing commands`.

### Task 2: Background stem import and media ownership

**Files:** Create `src/stems/StemImporter.h/.cpp`, `src/project/MediaStore.h/.cpp`, `tests/StemImportTests.cpp`.
**Interfaces:** Consume project types. Produce `StemImporter::import(vector<InputStreamFactory>, MediaStore&, CancellationToken&, ProgressCallback) -> ImportReport`; report per-file assets/errors and no project mutation. `MediaStore` stages copies, computes SHA-256, and commits validated media using unique IDs.

- [ ] Add failing `StemImportPreservesSilence`, `DuplicateNamesRemainDistinct`, `ImportCancelNoPartialAssets`, and `MixedValidCorruptImport`; assert original source bytes stay unchanged and mono/stereo sample rates remain explicit.
- [ ] Run these tests and confirm failure.
- [ ] Implement streamed WAV/AIFF/FLAC/MP3 decoding through supported JUCE readers, waveform summaries, bounded jobs, storage checks, per-file errors, and batch-track creation through Task 1 commands.
- [ ] Run tests; commit `feat: import stems with portable original media`.

### Task 3: Pitch-preserving stretch and manual editing

**Files:** Create `src/stems/StretchRenderer.h/.cpp`, `tests/StretchTests.cpp`; update dependency manifests and license notices.
**Interfaces:** Produce `StretchRenderer::prepare(const AudioAsset&, const TimingMap&, double targetBpm, MediaStore&, CancellationToken&) -> Result<PreparedAudio>`. `PreparedAudio` references bounded disk-backed render data and channel/rate metadata. Tasks 5 and 6 consume this type.

- [ ] Add failing `StretchDurationPitch`: 440Hz fixture stretched from 120 to 90 BPM has expected duration within one analysis block and measured pitch within 1%; linked impulse stems retain relative offsets within one output frame after shared compensation.
- [ ] Run tests to establish failure.
- [ ] Integrate a pinned Signalsmith Stretch version and dependencies, compensate algorithm latency, preserve group origin, and render piecewise timing maps off-thread. Preserve mode bypasses stretching; validate invalid ratios before work.
- [ ] Run pitch, transient/group, cancellation, and repeated-edit tests; commit `feat: add non-destructive stem timing rendering`.

### Task 4: Auto Sync proposals and preview

**Files:** Create `src/stems/{BeatAnalyzer,SyncProposal,SyncService}.h/.cpp`, `tests/SyncTests.cpp`.
**Interfaces:** `BeatAnalyzer::analyze(const AudioAsset&, CancellationToken&) -> Result<BeatEstimate>` (tempo, beat times, candidate downbeat, confidence); `SyncService::propose(const Project&, Id group, optional<Id> reference, optional<double> manualBpm, CancellationToken&) -> Result<SyncProposal>`. Proposal carries source revision, reference identity, and group timing map. `applyProposal` rejects stale state.

- [ ] Add failing `SyncKnownOffset` for synthetic 120/90 BPM percussion against a reference, `SyncAmbiguousNeedsCorrection` for half/double-tempo and beatless fixtures, and `SyncStaleProposalRejected` after reference/tempo edits.
- [ ] Run tests and confirm failure.
- [ ] Implement local onset envelope/autocorrelation, ranked tempo candidates and user-confirmable downbeat. Use one rhythmic guide per group. Low-confidence results require manual BPM/downbeat; audition uses a temporary snapshot and Apply is one history command.
- [ ] Run tests including preview cancel and deleted-reference retention; commit `feat: propose and preview stem synchronization`.

### Task 5: Shared prepared playback and transport

**Files:** Create `src/stems/{PlaybackSnapshot,StemPlayback}.h/.cpp`; modify `src/audio/AudioEngine.h/.cpp`, `src/transport/TransportClock.h/.cpp`; test `tests/StemPlaybackTests.cpp`.
**Interfaces:** `StemPlayback::publish(unique_ptr<PlaybackSnapshot>)` transfers prepared data through a bounded handoff; `render(float* const*, int channels, int frames, const TransportBlock&) noexcept`. Snapshot construction and reclamation occur outside the callback. Extend clock with queued seek-in-beats commands.

- [ ] Add failing `PlaybackMatchesPositions`, `PlaybackMuteSoloPan`, and `DeviceRateChangeRetainsPlacement` (44.1/48kHz). Check paused/seek behavior and concurrent rapid snapshot changes.
- [ ] Run tests to confirm failure.
- [ ] Implement preallocated read-ahead buffers with silent underrun behavior; publish only ready snapshots, reclaim retired state off-thread, and mix alongside existing audition/metronome. Avoid unbounded in-memory song buffers.
- [ ] Run tests and foundation regressions; commit `feat: play stem tracks on shared transport`.

### Task 6: Mix and aligned-track export

**Files:** Create `src/stems/WavExporter.h/.cpp`, `tests/ExportTests.cpp`.
**Interfaces:** `exportMix/exportTracks(const PlaybackSnapshot&, ExportRange, OutputTarget&, CancellationToken&, ProgressCallback) -> Result<ExportReport>`; use the Task 5 render rules with metronome/audition disabled.

- [ ] Add failing `ExportReimportsAligned` comparing impulse positions and PCM content; mix respects mute/solo, track export respects mute and ignores solo. Test filename collisions and output failure.
- [ ] Run tests to confirm failure.
- [ ] Implement stereo WAV export at 48kHz/24-bit, common range for all tracks, atomic staged output, cancellation cleanup, clipping indication, and explicit UI overwrite consent.
- [ ] Run tests; commit `feat: export stem mixes and aligned WAV tracks`.

### Task 7: Save, recovery, and portable checkpoints

**Files:** Create `src/project/ProjectSerializer.h/.cpp`, `src/checkpoint/CheckpointService.h/.cpp`, `tests/CheckpointTests.cpp`.
**Interfaces:** `save/loadProject(path) -> Result<Project>` with atomic save; `exportCheckpoint(const Project&, MediaStore&, OutputTarget&, CancellationToken&) -> Result<CheckpointInfo>`; `importCheckpoint(InputStream&, LocalProjectStore&, CancellationToken&) -> Result<ImportDecision>`. Define safe ZIP package `.jdsproject`, JSON schema v1, original media, SHA-256 manifest, revision UUID and parent UUID; rebuild caches.

- [ ] Add failing `CheckpointRoundTrip`, `UnsupportedSchemaDoesNotReplace`, `CheckpointConflictKeepsBoth`, `PackageTraversalRejected`, and checksum/missing-media/interrupted-write tests.
- [ ] Run tests to confirm failure.
- [ ] Implement validation before installation, extraction byte/entry bounds, atomic promotion, rotating recovery, unknown-field preservation, and save-as-copy conflict decisions. Record playhead and all timing/group/mixer data; keep hardware settings local.
- [ ] Run tests; commit `feat: save portable recoverable music checkpoints`.

### Task 8: Windows Stem Tracks workspace

**Files:** Create `src/ui/{StemWorkspaceComponent,TrackLaneComponent,TimingEditorComponent,SyncPreviewComponent,ProjectActionsComponent}.h/.cpp`; modify `src/app/RootComponent.h/.cpp`; test `tests/StemWorkspaceModelTests.cpp`.
**Interfaces:** Components consume `ProjectHistory` and Tasks 2–7 services through a `StemWorkspaceController`; async completion uses weak UI lifetime tokens. Keep domain edits out of paint/audio callbacks.

- [ ] Add failing controller tests for multi-file import, preserve default, undo, marker constraints, canceled proposal, and close-during-job safety.
- [ ] Run tests to confirm failure.
- [ ] Implement timeline, waveform controls, group linking, three timing modes, original/proposed audition, source BPM/downbeat corrections, export/checkpoint actions, progress/cancel, and accessible labels. Maintain Jam/device workspace navigation.
- [ ] Run tests and manually exercise drop/import/edit/sync/save/reopen/export on Windows; commit `feat: integrate stem workspace into Jeff DAW Studio`.

### Task 9: Android client and provider-safe project handoff

**Files:** Create `platform/android/JeffDawStudio.jucer`, `src/platform/AndroidDocuments.h/.cpp`, `src/ui/MobileStemWorkspaceComponent.h/.cpp`, `tests/DocumentStreamTests.cpp`, `docs/android-build.md`; platform-guard root initialization.
**Interfaces:** Android document adapter implements Task 2 stream factories and Task 6/7 output targets using content URI streams. Mobile controls dispatch the same commands/services; phone audio configuration is local.

- [ ] Add failing fake-provider tests for no local path, revoked permission, canceled picker, truncated read, failed write, and resume after process death with committed autosave.
- [ ] Run tests to confirm failure.
- [ ] Configure JUCE Projucer Android exporter with shared source list, pin its generated Gradle/SDK/NDK toolchain, target arm64-v8a, and use JUCE-supported minimum API. Build with `gradlew assembleDebug`; record actual toolchain versions in docs. Implement picker and audio share intent ingestion, touch timing markers, local staging, and lifecycle recovery. Never claim provider save succeeded before stream close succeeds.
- [ ] Verify APK build, phone import/edit/play/export, Drive-provider checkpoint save (local fallback when absent), and Windows↔Android round trip. Keep physical checks marked unrun if no phone is available. Commit `feat: edit shared stem projects on Android`.

### Task 10: CI, Drive handoff guide, and GitHub delivery

**Files:** Modify `.github/workflows/windows-foundation.yml`, `README.md`; create `.github/workflows/android-stems.yml`, `docs/project-handoff.md`, `docs/stem-release-checklist.md`.

- [ ] Add a release checklist proving Android edit → exported package → Drive upload/download → Windows continuation → return checkpoint. Include conflicting edits and interrupted transfers.
- [ ] Extend CI to feature-branch pushes, run Windows tests/build, generate Android project with pinned Projucer tooling, build APK, and upload distinct platform artifacts. Debug APK is clearly labeled; do not create/store release signing secrets without user setup.
- [ ] Document install/build steps, explicit local-save/export/upload distinctions, file formats, uncertainty/manual sync correction, and GitHub-versus-Drive roles.
- [ ] Run full checks, inspect artifact contents and source diff, record hardware limits, commit, and push `feature/stem-import-workflow` without force. Verify remote commit and CI. Create/attach a draft PR with concrete validation if needed for review; do not merge without authorization.

## Sources and self-review

Stretch dependency/licensing: https://github.com/Signalsmith-Audio/signalsmith-stretch
Android exporter: https://juce.com/tutorials/tutorial_android_studio/
All approved-spec requirements map to Tasks 1–10. The five Review Focus conditions have named tests. Timing data uses explicit units; stream adapters avoid assuming Android files have local paths. No task depends on cloud authentication or falsely equates export with upload. This is a plan, not a verified implementation.

## Execution handoff

Recommended: native execution in this session, because the shared types and render rules span most tasks and one implementation context reduces interface drift. Subagent-driven execution is available if the user prefers independent per-task review. Review this plan and choose the execution method before product code changes.
