# Chapter 4: E-Reader Application

> **App Version**: v2.2.0  
> **Firmware Version**: v2.2.0  
> **Last Verified**: 2026-07-24  

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
3. **Reading Progress Persistence**: Current book path, page number, and completion percentage are saved automatically to SD card storage upon turning pages.

---

## Standby Mode & Reading State Restoration

- Pressing the **BOOT button** (`GPIO 0`) while reading puts the device into deep sleep standby mode and saves the active book path and page position to `/sd/data/sys_state.json`.
- Pressing the **BOOT button** again wakes the device and immediately re-opens the exact book and page position where you left off.
