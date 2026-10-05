# Jeff DAW Studio: integrated stem workflow

Date: October 5, 2026
Status: Revised cross-device proposal for review; Google Drive checkpoints selected; implementation has not started.
Repository: JeffN-Up/jeff-daw-studio
Base: feature/audio-midi-foundation, commit 8182374

## Intended result

Download Treblo stems on Android, import them into Jeff DAW Studio on the phone, preserve/edit/sync their timing while away from home, and continue editing the same project on the Windows Jeff mini PC. Both clients use the same versioned project model, timing semantics, and audio rendering core. Treblo account integration is not required; import uses files the user downloads.

## Android editing and continuation at home

The Android client accepts downloaded audio using its system file picker or share/open action, and supports Preserve, Edit, Auto Sync, playback, project saving, and WAV export with touch controls. Windows retains drag-and-drop and Add Stems. Archives must initially be extracted through the device's file tools. Keep complete same-song stem sets together; filenames alone do not establish timing alignment.

A portable project checkpoint contains the project document plus all original media, timing maps, linked stem groups, track placement, gain/pan/mute settings, and saved playhead position. Opening it on either client restores the edit state and view position. Render caches can be rebuilt and do not substitute for original media. Device-specific audio/MIDI selections stay local. Unsupported desktop-only instruments or plug-ins must be reported and preserved rather than silently discarded; they are outside the mobile stem milestone.

The selected transfer method is Google Drive upload/download checkpoints. Export Project Checkpoint creates one self-contained project package. On Android, the user saves through the system document picker to Drive when its provider is available, or saves locally and uploads using the Drive app. On Windows, the user downloads that package through Drive and uses Open Project Checkpoint; the return trip uses the same export/upload/download/import steps. Device-local autosave continues offline. Saving locally is not reported as an upload, and the UI clearly distinguishes the last local save from the last exported checkpoint.

This first handoff uses the user's existing Drive tools and does not require built-in Google authentication or a Drive API connector. It is an explicit checkpoint workflow, not automatic live cloud sync. A future direct Drive integration can reuse the package/revision model but is not part of this implementation.

Every checkpoint records project identity, schema version, revision and parent revision, and media checksums. If both devices modify the same prior revision, preserve both copies and ask which to continue; never overwrite an unseen edit silently. Initial conflict handling is save-as-copy, not automatic merging of musical edits. Interrupted transfers are rejected as incomplete and leave the previous local project intact. Import validates package paths and media checksums before installation.

GitHub saves source code and build instructions/artifacts; the repository does not automatically store Jeff's stems or audio projects. Music-project transfer is a separate feature.

## Approach

Extend the existing foundation with a Stem Tracks workspace, a shared project model, a background import/timing service, an audio playback snapshot, an offline WAV renderer, and portable checkpoints. Add an Android client with touch/file-picker integration that uses the same core. Windows and Android have separate build and packaging targets; the existing Windows CMake setup alone does not establish Android support. This establishes the audio-track portion of Studio Mode without requiring the full MIDI editor or VST3 milestone.

An external stem utility would duplicate transport and project state. A complete Studio Mode build would delay this focused workflow. The integrated audio-track slice is the recommended approach.

## Drag-and-drop experience

The track area accepts multiple local WAV, AIFF, FLAC, and MP3 files through JUCE's supported decoders. An Add Stems button provides the same workflow. Unsupported or corrupt files show a per-file error, and valid files remain importable. ZIP extraction is outside this first slice; archives must be extracted first.

Import runs off the audio and UI threads and shows progress plus cancellation. Each file creates a named audio track with a waveform, volume, pan, mute, solo, and visible timing status. Dropping a set creates separate tracks, never replaces existing audio. The batch starts at the selected insertion position, defaulting to song start.

## Timing automation

Three timing options are available on selected clips or a linked stem group:

- **Preserve (default):** retain the original duration, internal silence, and relative offsets. Stems exported from one creation stay synchronized only when their files share a common origin; individually trimmed exports may require offset recovery. Never independently trim or snap their first transients automatically.
- **Edit:** move clips, trim their visible boundaries non-destructively, set source BPM and downbeat, and add or move timing markers to stretch selected sections while preserving pitch. Snap-to-grid can be toggled. Linked stems share the same timing map by default; explicit unlinking permits individual edits. Timing markers must stay monotonically ordered and cannot produce zero or negative durations. Reset Timing restores the imported timing without erasing mixer settings.
- **Auto Sync:** analyze tempo, beat locations, and a candidate downbeat, then propose an alignment to either the project tempo/grid or one user-selected reference audio track. This supports unrelated stems and newly added tracks. A linked batch uses one timing map derived from its chosen rhythmic guide; it does not independently warp every stem. Show the proposed offsets, BPM, stretch map, and confidence, and allow audition of original versus proposed timing before Apply. Cancellation leaves project state unchanged; Apply is one undoable command. Uncertain, beatless, or conflicting material requires manual BPM/downbeat correction before application.

Automatic analysis is an assistive suggestion, not a guarantee that different songs' phrases, keys, chords, or sections will fit musically. Harmonic matching, generative rearrangement, and a cloud AI service are outside this slice. Local beat analysis supplies the first Auto Sync implementation; the interface must not advertise an AI model unless one is actually integrated and verified.

A pitch-preserving stretch processor uses the same source origin and mapping for all linked stems. Its library must be compatible with the application's AGPL distribution and Windows build. Analysis and stretch settings are non-destructive and source files remain unchanged. Different-song batches have independent source BPM and insertion positions. A timing summary displays the chosen mode, source and target tempos, stretch ratio, reference track, and unresolved timing information. Tempo matching does not silently occur in Preserve mode.

## Playback and persistence

The project owns track names, source media, clip placement, source BPM/downbeat, trim boundaries, timing markers, linked group identities, sync reference identifiers, stretch settings, and mixer state. Import, removal, and timing edits support undo/redo. A versioned project document saves these settings; imported media is copied into a project media folder for portability. Reopening restores the same alignment. Deleting a reference track preserves the applied timing map and marks that reference unavailable. Autosave uses atomic writes and preserves the previous valid save on failure.

Decode and stretch work happens in background jobs. The audio callback consumes a prepared immutable playback snapshot, with no file I/O, allocation, or blocking locks. Transport time and device sample rate determine playback position. The metronome and existing audition instrument share the same output.

## Export

Export Mix saves a stereo WAV of the selected song range. Export Tracks saves one WAV per track using a common start and duration, preserving alignment when imported elsewhere. Track export includes each track's current timing, gain, pan, and mute state; solo is an audition control and does not remove tracks from this export. The mix export respects mute and solo.

Exports use an offline renderer, a background progress display, and cancellation. They use the same playback and timing rules as live audio. Existing files require overwrite confirmation. Safe filenames prevent collisions; failures leave no apparently finished partial file. These are local downloadable/savable audio files, not cloud links.

## Validation and delivery

Tests cover common-origin stereo stems with leading silence, differing sample rates, track placement, shared tempo ratios, corrupt inputs, cancellation, save/reopen, undo/redo, and WAV headers/sample content. Preserve must keep silence gaps and relative timing. Edit must reject crossing markers and recover original timing after reset. Auto Sync fixtures cover known tempo and downbeat offsets against a new reference track, half/double-tempo ambiguity, beatless audio, preview cancellation, linked-group consistency, and reference deletion. A known synthetic rhythmic batch verifies timing and pitch preservation after stretching. Exported individual tracks must remain aligned after reimport.

Windows and Android builds and the existing foundation tests must pass. Cross-device fixtures verify Android-save to Windows-open and Windows-save to Android-open preserve track positions, markers, groups, mixer state, and media; round-trip edits must not alter untouched project data. Tests cover conflicting revisions, missing media, invalid package paths/checksums, and interrupted transfer. Manual verification covers multi-file dropping on Windows, file-picker/share import on Android, touch timing edits, playback, saving, reopening, and export. Android memory/storage constraints require bounded background work and readable file-size errors. Treblo-specific files, Android device performance, and physical mini-PC devices remain unverified until tested.

Deliver in stages: shared stem/project engine and Windows workflow; Android editing client and portable checkpoint round trips; then Google Drive handoff instructions and device verification. After implementation and verification, commit and push the feature branch to GitHub, with checkout/build instructions and Windows/Android artifacts where CI can produce them. Android installation/signing must be explicitly documented and a tested APK distinguished from unverified source. Do not describe a pushed specification as a working feature. No force push or replacement of main is required.

## Review point

Confirm Android and Windows clients sharing a portable project through explicit Google Drive upload/download checkpoints, with Preserve, Edit, and Auto Sync to the project or a reference track, preview/editable markers, and mix/individual WAV exports. Approval of this written specification is followed by an implementation plan before product code changes.
