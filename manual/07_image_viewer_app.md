# Chapter 7: Pictures

> **App Version**: v3.0.0  
> **Firmware Version**: v3.0.0  
> **Last Verified**: 2026-10-26  

---

## Overview
**Pictures** shows the JPEG files in `/images` on the SD card.

- **List**: file name and size, paged.
- **Converting**: the first time a picture is opened it is decoded, fitted to the portrait screen with area-averaged scaling, brightened and dithered (Floyd-Steinberg) to the panel's 16 greys. A progress card is shown. The result is cached in `/data/imgcache/`, so it opens instantly next time.
- **Viewing**: the picture fills the screen. Tap anywhere to reveal the header and the **Prev / Rotate / Next** bar; tap the image area again to hide the controls. *Rotate* turns a landscape photo sideways so it fills the screen.

## Brightness
E-paper renders mid-greys darker than a phone screen, so pictures are lightened with a gamma curve. Choose the strength in **Settings > Display > Picture brightness**: Original, Light, Lighter (default) or Lightest. Changing it re-converts pictures automatically, and the same curve is applied to sleep-screen pictures.

## Limits
JPEG only. Pictures over 24 megapixels or larger than available memory show a clear error with a return to the picture list. Resize an image if it cannot be decoded.
