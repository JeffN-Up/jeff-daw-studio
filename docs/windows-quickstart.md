# Start making sound in Jeff DAW Studio

1. Extract the complete Windows ZIP into a normal folder. Open **Jeff DAW Studio.exe** inside **Jeff DAW Studio**. Do not run it directly from the ZIP or a temporary preview folder.
2. Open **Audio Settings** and select the speakers/headphones you use on the ThinkCentre.
3. Return to **Stem Tracks**, click **Load Demo**, wait for the four real waveforms, and click **Play**. Volume/pan/M/S control the mix; **Pause** pauses and **Rewind** returns to the start.
4. Drag your extracted WAV, AIFF, FLAC or MP3 stems into the workspace, or use **Add Stems**. Import copies originals into the current local project folder and saves the imported state.
5. Click **Save** after mixer changes. **Project Folder** reveals `project.json` and the media folder. Use **Open** to reopen `project.json`. Keep the entire project folder when copying a project.
6. **Export Mix** writes a 48 kHz/24-bit stereo WAV. Choose a new filename; this build does not overwrite existing exports.
7. **Record Output** starts a live WAV take of the application's output. Play your stem mix, or switch to **Jam + Devices** and play the Orba Mirror or connected MIDI controller. Return to Stem Tracks and click **Finish Recording**. Find the take in the project's recordings folder. This records the app's audition synth, not the Orba/JT-4000's internal audio or microphone input.

Music projects live under **Documents / Jeff DAW Projects**. GitHub carries application source and builds; it is not a music-project backup. This version uses local project folders; portable ZIP checkpoints and Drive transfer are future work.

Before the ThinkCentre test is marked complete, verify app launch, audible demo playback, save/reopen, WAV export and output recording. Connect the physical Orba/JT-4000 separately and verify note input. Physical hardware has not been tested by the automated build.
