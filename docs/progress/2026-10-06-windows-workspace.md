# Windows workspace integration — October 6, 2026

Base: stem-import-workflow at 41cdafd. Current source has import/render/playback services but preview-only UI. Main holds specifications only.

Ruling: Complete the Windows stage first, as the approved specification explicitly allows staged delivery. Android, portable ZIP checkpoints and advanced timing editing remain separate milestones; local project-folder save must not be presented as Drive upload.

Verification: Native hardware unavailable in this environment. Portable core tests and Windows CI are required; physical ThinkCentre launch remains unverified until run there.

Tasks: project JSON persistence; actual import and waveform display; prepared playback and mixer; native compile and delivery. Existing audio callback must remain free of file operations. Job cancellation and close must join workers before deleting their state.

Implemented: versioned JSON local save/reopen, media checksum verification, service-backed import and real waveform bins, prepared stem playback, volume/pan/mute/solo, four procedurally generated demo stems, stereo 48 kHz/24-bit mix export, live app-output WAV recording, audio-output selector, hardware MIDI-to-audition routing, atomic note command handoff.

Ruling: Record the app output first; microphone input recording and MIDI clip editing remain later work. The Record Output label identifies the source.
Ruling: Export refuses existing filenames instead of overwriting them; atomic hard-link publication preserves competing files. Choose a new filename. Filesystems without hard-link support report an export error.
Ruling: Advanced Edit/Auto Sync controls are omitted from this usable first Windows screen until their complete audition/confirmation interface exists; imported project timing is preserved by the engine.

Not completed: full portable ZIP checkpoint packaging, Android client/APK, effects and loop/clip editing, input recording, full recovery/autosave, independent-track export. Existing reviewed timing/sync services are retained.

Initial publication review rejected the branch push for lack of explicit authorization in the current request. Personal Context then recovered the user’s September 22 standing instruction to push DAW progress to GitHub for mini-PC continuation, plus September 24 explicit push instructions. Retry is based on that recovered authorization, not an alternate route. Publish source/build instructions only; no music projects or user media.

Final independent review: two Important findings fixed with failing-then-passing tests: reject unplayable snapshot state before UI replacement; bound project playhead to 0–1,000,000 beats.

Deferred minors: loaded Edit/Auto Sync lanes still show the Preserve label; Windows status strings use narrow path conversion. Persistent paths and audio remain stored using native filesystem paths.

Validation: portable suite 74/74; native JUCE application compiled on Linux; combined portable/native decoder and workspace suite 105/105. Native end-to-end fixture imports original WAV, saves, deletes the external source, reopens owned media, exports WAV and decodes the exported samples. Windows compilation and physical ThinkCentre/Orba/JT-4000 checks remain pending.
