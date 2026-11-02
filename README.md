# Contour Studio

A Windows tool for extracting and exploring image contours.

## What it does

Contour Studio extracts and displays image contours and can save their geometry in the compact CGR5 format. It is designed for exploring outlines and regions without retaining the original image inside the exported file.

## Features

- Drag in an image, zoom and pan, and adjust contour detection while viewing the result.
- Use **Easy** controls for common settings or **Advanced** for separate color/brightness, regions, joining and geometry options.
- Set analysis resolution from 512 to 8192 pixels or use a custom value.
- Export CGR5 geometry and inspect timing and encoded size.

## Get started

Download the Windows x64 ZIP from [Releases](https://github.com/FajkPes/Contour-Studio/releases/latest), extract it, and run `Spustit.bat`. Windows App Runtime 1.8 is required. Open or drag in an image, choose an analysis resolution and adjust sensitivity; use Advanced for more control.

English is the default and Czech is available. CGR5 contains geometry rather than the source image; older CGR2–CGR4 files are not supported. Detection is not semantic object or text recognition, so some textures and shadows can remain.

[Detailed documentation](README-DETAILS.md) · [All releases](https://github.com/FajkPes/Contour-Studio/releases)
