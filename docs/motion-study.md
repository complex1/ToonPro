# Motion Study

Open **Panels → Motion Study…** from any Toon Pro workspace, including Project Home. Each study is a separate resizable window. A project is needed only when inserting the study into a timeline.

## Choose and inspect motion

Import a short video using the file-plus button. Toon Pro reads video timestamps and prepares frame images in a background worker. Source FPS, retained frame count, duration and the displayed source-frame number appear above the preview. Import does not modify the video. The Cancel button cancels processing.

- **Ones** keeps every source frame.
- **Twos** keeps frames 1, 3, 5… and holds each until the next retained frame.
- **Threes** keeps frames 1, 4, 7… with correspondingly longer holds.
- Check or uncheck individual numbered frames to restore or skip them. Frame 1 stays retained so the study always has an initial image.
- Scrub with the slider or enter a source-frame number. Click a numbered frame to inspect it.
- **Study / Source** switches between the held result and the original frame at that time.
- Play previews the sequence in a loop using source timestamps. Audio is not included in this first study workflow.

The clip's duration stays unchanged. A 24 FPS clip on Twos has 12 distinct images per second but still runs on its original timebase. Variable-rate sources retain their individual frame timestamps; the displayed FPS is the video's reported average. Selecting Ones/Twos/Threes resets the manual frame choices to that pattern.

## Save and reuse

Save creates a new `.toonstudy` folder containing `study.json` and all decoded source-frame images, including skipped frames. Open folder restores the frame choices and timing without needing the original video or FFmpeg. Existing destinations are preserved; use a new folder name when saving a revised study in this first version. Move the entire folder when transferring a study. Closing a modified study asks before discarding it.

The two toolbar insertion actions target the active shot at its current playhead:

- **Add editable cels** creates an ordinary animation track. Each retained image is a separate drawing with a hold that can be changed using Timeline tools.
- **Add locked reference track** creates an annotation track for drawing over. It sits below existing artwork, is locked initially and is excluded from movie-frame exports, following Notes-layer behavior. These are independent image cels, not a live link to the source video or study folder.

Insertion converts elapsed times to the project's FPS and extends the shot if necessary. If several source changes fall within one destination frame, the last one at that frame wins. Very short timing differences cannot survive a lower project frame rate. Insertion is one Undo step and does not replace existing tracks. Reference annotations can still appear in production-sheet exports. Projects containing a reference animation track use format 39 and need this updated application to reopen; editable-cel insertion retains the format required by the other project features.

## First-version limits

- Video import currently requires the `ffmpeg` and `ffprobe` command-line executables on PATH; macOS Homebrew locations are also detected. They are not newly bundled with Toon Pro by this feature. Missing decoders produce an explanation. Saved studies do not require them.
- Import accepts up to **1,800 source frames**. Trim longer videos before importing.
- Study images fit within **640 × 640 pixels**, preserving aspect ratio. Timeline copies retain this study resolution and are centered on the canvas; they are not full-resolution video imports.
- A timeline insertion supports up to **256 distinct retained drawings**, subject to the existing track/layer/tile and artwork-storage limits. An over-budget insertion leaves the project unchanged.
- Full-resolution import, an in-app trim range, audio, thumbnail strips, optical-flow interpolation, live linked study clips and study-edit Undo remain future improvements. Frame selection does not synthesize in-between drawings or automatically improve the original motion.
