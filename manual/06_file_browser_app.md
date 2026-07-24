# Chapter 6: File Browser Application

> **App Version**: v2.2.0  
> **Firmware Version**: v2.2.0  
> **Last Verified**: 2026-07-24  

---

## Overview
The **File Browser Application** allows full management of files and directories stored on the MicroSD card, featuring folder navigation, file operations (Move, Copy, Delete), and confirmation modals.

---

## Screen 1: File List View

Displays files and sub-directories within the current path (e.g. `/books/classic`).

![File Browser List Screen](images/filebrowser-list.png)

### Controls & Sidebar
- **Directory Navigation**: Tap any directory marked `[DIR]` to open it.
- **`Up` Button**: Navigates to parent directory.
- **`SD Root` Button**: Instantly jumps to `/sd/` root directory.
- **`Refresh` Button**: Re-scans current directory contents.
- **`/\` / `\/` Buttons**: Scroll list up or down.

---

## Screen 2: File Options Action Modal

Tapping any file in the browser opens the **File Options** action modal with 4 centered, labeled action buttons:

![File Options Modal](images/filebrowser-modal.png)

### Action Buttons
1. **`Move`**: Moves the selected file to a destination folder on the SD card.
2. **`Copy`**: Duplicates the selected file on the SD card.
3. **`Delete`**: Opens the Delete Confirmation Modal.
4. **`Cancel`**: Dismisses the action modal and returns to the file list.

---

## Screen 3: Delete Confirmation Modal

Tapping **Delete** opens a safety confirmation modal to prevent accidental file deletion:

![Delete Confirmation Modal](images/filebrowser-delete.png)

- **`Delete`**: Permanently removes the file from the SD card.
- **`Cancel`**: Cancels deletion and returns to the file list.
