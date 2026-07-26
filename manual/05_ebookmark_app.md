# Chapter 5: eBookmark Companion App

> **App Version**: v2.3.0  
> **Firmware Version**: v2.3.0  
> **Last Verified**: 2026-07-26  

---

## Overview
The **eBookmark Companion App** provides physical reading tracking and interfaces wirelessly with external smart bookmark hardware or companion mobile apps over Bluetooth Low Energy (BLE).

![eBookmark App Screen](images/ebookmark-sync.png)

---

## Screen Features & Breakdown

### 1. Active Reading Tracking
- **Physical Book Progress**: Tracks page number, total pages, reading speed (pages/hour), and estimated time remaining.
- **Reading Streak Analytics**: Displays current reading streak (days) and weekly goal progress.

### 2. Bluetooth Low Energy (BLE) Sync Server
- **Server Name**: Advertises as `LilyGo-eReader-BLE`.
- **Two-Way Bookmark Sync**: Automatically synchronizes reading progress between the physical device and connected companion apps when in range.
- **Wireless EPUB Transfer**: Allows pushing new `.epub` books wirelessly from a smartphone directly to the `/sd/books/` directory on the E-Reader.

---

## Navigation

- **Exit to Launcher**: Tap `Exit to Launcher` on the right sidebar or tap the top-left escape corner ($x \le 60, y \le 60$).
- **Settings Access**: Tap the top-right settings gear icon ($x \ge 900, y \le 60$).
