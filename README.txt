Cropper - Media Trimming & Cropping Tool
=========================================

Cropper is a simple, high-performance tool for batch processing images and videos. 
It allows you to crop, rotate, and trim media with a fast, interactive UI.

FEATURES
--------
- Visual cropping and rotation for images and videos.
- Frame-accurate trimming for videos.
- Deferred deletion: Mark original files for deletion and remove them during batch processing.
- Intelligent Batching: Only "touched" files (edited or marked for delete) are processed.
- GPU-accelerated video preview using libmpv.

DEPENDENCIES
------------
To build and run Cropper, you need the following:

1. GCC (C Compiler)
2. Raylib (UI and rendering)
3. libmpv (Video decoding and preview)
4. ffmpeg (CLI tool, used for the actual processing/exporting)

On Debian/Ubuntu:
   sudo apt install build-essential libraylib-dev libmpv-dev ffmpeg

BUILDING
--------
You can compile the project using the following command:

   gcc main.c media.c state.c -lraylib -lmpv -lm -o cropper

USAGE
-----
Run the application by passing a directory or file:
   ./cropper ./my_videos

Controls:
- [Mouse Wheel]   : Zoom in/out
- [Right Click]   : Pan view
- [H]             : Toggle Hand (Pan) mode
- [Space]         : Reset view (Fit to window)
- [I]             : Set video trim START point
- [O]             : Set video trim END point
- [Left/Right]    : Step frame (hold Shift for 10x)
- [Process Batch] : Starts processing all "touched" items into the 'processed' folder.
