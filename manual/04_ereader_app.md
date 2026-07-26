# Chapter 4: E-Reader Application

> **App Version**: v2.3.0  
> **Firmware Version**: v2.3.0  
> **Last Verified**: 2026-07-26  

---

## Overview
The **E-Reader App** is the core reading application. It performs deep directory scanning of `/sd/books` for `.epub` and `.txt` documents, manages reading progress, and renders clean anti-aliased text via a custom typography engine.

---

## Screen 1: Library View

Upon launching, the app displays the **Library View** listing all scanned books alongside reading completion percentages and sorting controls.

![E-Reader Library Screen](images/ereader-library.png)

### Library Features
1. **Deep Scanning & Progress Bar**: Automatically scans nested directories (e.g. `/books/classic`, `/books/scifi`). If scanning takes longer than 250ms, a non-intrusive progress bar overlay is displayed.
2. **Library Sorting Controls**:
   - **Sort: Author**: Sorts books alphabetically by author name.
   - **Sort: Genre**: Groups books by sub-directory folder / genre.
   - **Sort: Prog %**: Sorts books by completion percentage.
3. **List Navigation**: Tap `^` (Up) or `V` (Down) on the left sidebar to scroll through library pages.
4. **Top Bar Status**: Displays current time, battery percentage, remaining SD storage space, and the refined top-right **Settings Gear Icon**.

---

## Screen 2: Reading View

Tapping any book in the library opens the **Reading View**:

![E-Reader Reading Screen](images/ereader-reading.png)

### Reading Features & Controls
1. **Typography Engine**: Renders crisp, anti-aliased text using `/sd/data/Roboto-Regular.ttf` with customizable font size ($16\text{pt} - 34\text{pt}$), line height, and margin padding.
2. **Pagination Controls**:
   - **Next Page**: Tap the **right half** of the screen ($x \ge 480$).
   - **Previous Page**: Tap the **left half** of the screen ($x < 480$).
3. **Reading Progress Persistence**: The exact reading position is saved automatically to a per-book bookmark file (`<book>.bmk`) alongside the book on SD card every time you turn a page. For EPUB books (which are split into many chapter sub-files), the chapter index is saved along with the in-chapter offset, so resuming always reopens the correct chapter - not just the correct byte offset within whichever chapter happens to load first. The library view's completion percentage is derived from this: text/RTF/Markdown books use `offset / file size`; EPUB books use `chapter index / chapter count` (chapter-level granularity).

---

## Lock Mode (Deep Sleep) & Reading State Restoration

Pressing **User Button 1** (`GPIO 21`) at any time immediately locks the device:

- The active app (and, if you're reading a book, the exact book path, chapter, and page position) is saved to `/sd/data/system_state.txt`.
- The display shows a "Locked" screen, then the ESP32-S3 enters **true deep sleep** with only `BUTTON_1` armed as a wake source - touch input does **not** wake the device while locked, and no other GPIO can wake it.
- Pressing `BUTTON_1` again fully resets the chip (deep sleep has no return path - the whole board reboots). After the short reboot delay, the firmware detects the deep-sleep wake cause and automatically relaunches the same app; if you were reading, it jumps straight back into the exact book, chapter, and page you left, bypassing the library view entirely.

**30-second auto-lock**: If the device is untouched for 30 seconds, it locks automatically the same way - *except* while you are actively looking at a reading page, where auto-lock is suspended so it won't interrupt you mid-page. The manual `BUTTON_1` lock always works immediately, even while reading.

> Non-reading apps (e.g. eBookmark, File Browser) only resume to their default view after waking, not a specific sub-screen - only the E-Reader app restores an exact in-book position.
