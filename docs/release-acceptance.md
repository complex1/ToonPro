# Desktop candidate acceptance

Use a copy of a real production project and the platform's candidate package. Record the application version, operating system, display scaling, tablet model and driver version alongside each result. Automated package smoke checks cover save/reopen, drawing/Undo, Movie playback, codec export/decode and supported 3D rendering; they do not measure physical pen feel.

## Install and reopen

1. Verify the download against its `SHA256SUMS` entry. Extract the portable ZIP/tar.gz, or copy the app from the Mac DMG into an Applications folder.
2. Open it from that location, with no development SDK environment configured. Keep a running production session separate from the test candidate.
3. Create an Illustration project and an Anime project. Save each, close the candidate, reopen it and verify drawings, colors, layers, timings and references.

## Drawing and tablet

- Cross dock dividers onto the canvas with the hovering pen. Verify the drawing cursor returns and pressure still works.
- Draw fast circles and slow curves using a small ink brush, then large sampled/connected brushes. Check for missing segments, jumps, delayed pen-up or visible seams.
- Use barrel-button/right-click, eraser tip if available, Undo/Redo and dock controls. Check that tooltips disappear while drawing and canvas focus returns after dialogs.
- Test one external display or display scaling change, sleep/wake, window minimize/restore, and a small workspace drawer.

## Production project

- Open the copied project, switch shots, edit a drawing and undo it. Compare the saved/reopened result.
- Review Scene/Movie playback with dialogue across cuts, pause, seek and loop. Listen on the real output device for drift or gaps.
- Export a bounded movie with audio and review it in an independent player. Compare frame boundaries, color, audio duration and the export range.
- Test 4K effects previews and representative brushes. Record responsiveness and peak memory on the intended computer.
- On Apple Silicon Mac, import/render a representative 3D reference. Other current platform packages do not include a 3D renderer.

## Production-signing gate

Current candidates are ad-hoc signed on Mac and unsigned on Windows. They are not notarized/App Store/SmartScreen-approved releases. Before a signed production release, finish baseline-OS/clean-machine acceptance, physical tablet acceptance, dependency-source/replacement review, application distribution terms and signing setup. Preserve the candidate report and original failure logs when recording any later result.
