# Chapter 7: Image Viewer Application

> **App Version**: v2.3.0  
> **Firmware Version**: v2.3.0  
> **Last Verified**: 2026-07-26  

---

## Overview
The **Image Viewer Application** browses and renders JPEG and `.raw` images stored in `/sd/images`, using 16-level grayscale dithering optimized for E-Paper displays.

![Image Viewer Screen](images/imageviewer-gallery.png)

---

## Screen Features & Breakdown

### 1. High-Contrast Image Rendering
- **Grayscale Conversion**: Uses Floyd-Steinberg dithering algorithms to convert 24-bit color images into 16 distinct E-Paper grayscale levels.
- **Aspect Ratio Scaling**: Fits images to the $960 \times 540$ display canvas while preserving aspect ratio.

### 2. Navigation Sidebar Controls
- **`Prev` / `Next`**: Cycle through images in `/sd/images`.
- **`Zoom +` / `Zoom -`**: Zoom in or out on image details.
- **`Rotate`**: Rotate image orientation in $90^\circ$ increments.
- **`Exit`**: Return to the Launcher home screen.
