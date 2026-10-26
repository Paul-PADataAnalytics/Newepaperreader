# Chapter 8: Calculator & Settings

> **App Version**: v3.0.0  
> **Firmware Version**: v3.0.0  
> **Last Verified**: 2026-10-26  

---

## 1. Calculator
A display card (expression above, result below, shrinking to fit) and a 4 x 5 keypad: digits, `+ - x /`, `=`, `C` and backspace. Only the display refreshes as you type.

## 2. Settings
Settings are saved to `/data/settings.json` and survive sleep and reboots. Four tabs:

| Tab | Setting | Notes |
| :--- | :--- | :--- |
| Display | Dark mode | Inverts the whole screen |
| | Text size | Reading font size (also adjustable inside a book) |
| | Picture brightness | Original / Light / Lighter / Lightest (Chapter 7) |
| | Full page refresh | How often a page turn does the full black/white clean refresh: every page, 3, 6 (default) or 12. Other turns use the fast refresh (~0.8 s instead of ~1.8 s, no flash, very faint ghosting) |
| Power | Auto-sleep | 1, 2, 5 (default), 10, 30 min or Never |
| | Stay awake on USB | No auto-sleep while plugged into a computer or charger |
| | Always on | Developer option: never auto-sleep |
| | Sleep screen | Pictures from `/images` or a book & battery card |
| | Battery | Voltage, percentage and USB state |
| | Sleep now | Same as pressing the side button |
| Bluetooth | Bluetooth sync | Companion app link on/off, live status and transfer progress |
| About | | Firmware, memory, SD card space, uptime |

See Chapter 9 for how sleep and the companion link work.
