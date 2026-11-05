# Chapter 11: Todos, Calendar & the On-Screen Keyboard

> **Firmware Version**: v3.1.0

The reader shows your todo list and calendar **without ever going online**. The NER Companion app on your phone fetches everything from the internet and passes it to the reader over Bluetooth whenever the two connect. The phone also sets the reader's clock at the same time.

| Source | What the reader gets | Editable on the reader |
| :--- | :--- | :--- |
| NootTask (`apps.padanalytics.co.uk`) | Todos, filtered by category | **Notes** (sent back to NootTask) |
| Google Tasks | Todos from the task lists you pick | No |
| Google Calendar | Events from the calendars you own and pick (7 days back to 90 days ahead) | No |

---

## 1. Setting up the companion app

Open **Device > Todo & calendar sync > Settings** in the NER Companion app.

- **NootTask**: enter your username and password and tap **Log in**. Only the tokens are stored; the password is not. The **Categories** list shows every NootTask category (as "Parent / Category") plus *Uncategorised*. Switch off any category you don't want on the reader. Its todos are then never sent.
- **Google**: tap **Connect Google account** and choose the account on the phone. You can then tick which **task lists** and which **calendars you own** to sync. The primary calendar is ticked by default.
- **Include completed items** applies to both todo sources.

Google access needs a one-time Google Cloud setup: an Android OAuth client for the app's package name and signing key. See `android_app/README.md`.

The **Todos** tab on the phone lists the same items the reader gets. It is read-only. Pull down to refresh it from the internet.

### When syncing happens

Every time the phone connects to the reader (tapping Connect, or the automatic reconnect when the app starts), the phone:

1. sets the reader's clock and time zone;
2. reads any note edits made on the reader and saves them to NootTask;
3. fetches NootTask todos and the chosen Google task lists, then sends them to the reader;
4. fetches the chosen calendars, then sends the events to the reader.

**Sync now** on the Device tab repeats steps 2-4. If the phone is offline, the reader gets the phone's last cached copy. Unsaved note edits stay on the reader until they reach NootTask.

---

## 2. Todos app

Open **Todos** on the home screen.

- The **All / NootTask / Google** buttons filter by source. Items are grouped by NootTask category or Google task list.
- **Show done / Hide done** (top right) shows or hides completed items.
- Each row shows the due date ("Today", "Tomorrow", "Overdue 4 Nov"), the priority, whether the item has notes, and **edited** when it has a note change that hasn't been sent yet.
- Tap an item to see its notes. Long notes page with **Prev / Next**.

### Editing notes (NootTask only)

On a NootTask item, tap **Edit notes**. The notes open in the editor with the on-screen keyboard.

- **Back (`<`)** keeps your changes. **Discard** throws them away. If the reader goes to sleep while you are editing, the changes are kept too.
- Your change shows straight away and is marked *edited here, sends on next sync*. On the next connection the phone saves it to NootTask, and the marker disappears once the reader has the server's copy.
- Clearing all the text removes the note from the todo.
- Notes the phone can't safely save are read-only on the reader: encrypted notes, and notes over 16 KB that had to be shortened for the transfer.

---

## 3. The on-screen keyboard

A portrait QWERTY keyboard sits at the bottom of the editor:

| Key | Action |
| :--- | :--- |
| ⇧ | Capitalise the next letter. Double-tap for caps lock. Sentences start with a capital automatically. |
| ⌫ | Delete the character before the cursor |
| `123` / `#+=` / `ABC` | Numbers and punctuation, more symbols, back to letters |
| ⏎ | New line |

Tap anywhere in the text to move the cursor. The text scrolls to follow the cursor, and black marks on the right edge show when there is more text above or below.

### Why it is fast

- Key presses only change the text in memory. The screen is redrawn once for each batch of presses. Taps made while the panel is still drawing are queued (up to 24) and never dropped, including a second finger landing before the first lifts.
- Only the changed text rows are redrawn, using a **direct-update waveform** (`DisplayHAL::updateScreenDirect()`). Changed pixels are pushed straight to black or white in three short frames, with no clearing flash and no greyscale. On the device this takes **about 50-70 ms** per update, against about 600 ms for a normal partial refresh.
- The letter keys always show capitals, so Shift only redraws the Shift key.
- Three seconds after you stop typing, the text area gets one clean greyscale refresh. This restores smooth font edges and clears any ghosting. After a keyboard layer switch, the keyboard is included in that refresh.

---

## 4. Calendar app

Open **Calendar** on the home screen. It is read-only.

- **Agenda**: today and every following day that has events. Tap a day heading to open that day.
- **Day**: one day's events. Use **< Previous day / Next day >**. If a busy day has more events than fit, tap **More n/m >** to see the rest.
- **Month**: a month grid with up to three dots per day for its events. Tap a day to open it. Use the arrows to change month.
- **Today** (top right) jumps back to today. Tap an event for its time, location, calendar and description.

### Calendar sleep screen

If the reader goes to sleep while the Calendar app is open (side button, the Sleep tile, or auto-sleep), the sleep screen shows the date and the coming agenda instead of the usual book cover. The reader wakes briefly just after midnight to redraw it, so "Today" stays correct. It then goes straight back to sleep. This costs one screen refresh per day. Pressing the side button wakes the reader into the Calendar app as usual.

---

## 5. Sync protocol

These are additions to companion protocol v2 ([Chapter 9](09_power_and_companion.md#2-companion-protocol-v2)). The phone only runs them when `HELLO` lists `"todos"` / `"calendar"` in `features`.

### Files the phone uploads

The phone uploads two files with the normal `FILE_BEGIN` / data frames / `FILE_END` commands, using `"dir":"data"`. Only `todos.jsonl` and `calendar.jsonl` are accepted in `data`. Each is written to `/data/.upload.part` first and renamed, so a failed transfer keeps the old copy. The format is UTF-8 JSON Lines, with no line over 16 KB.

`/data/todos.jsonl`:

```
{"k":"meta","v":1,"ts":1762345678}
{"k":"item","src":"nt","id":"<noot id>","list":"Pets / Fish","title":"Angelfish","done":false,"due":"2026-11-05","pri":"high","notes":"..."}
{"k":"item","src":"nt","id":"<noot id>","list":"Uncategorised","title":"Secret","done":false,"ro":true}
{"k":"item","src":"gt","id":"<task id>","list":"My Tasks","title":"Buy milk","done":false,"due":"2026-11-06"}
```

- `src` is `nt` for NootTask (notes editable unless `"ro":true`) or `gt` for Google Tasks (read-only).
- `due` is a local date. `pri`, `notes` and `ro` are optional.
- Items are already sorted by the phone: by source, list, not-done first, due date, then title.

`/data/calendar.jsonl`:

```
{"k":"meta","v":1,"ts":1762345678,"from":1761700000,"to":1770000000}
{"k":"ev","id":"<id>","cal":"Paul","title":"Dentist","start":1762351200,"end":1762354800,"loc":"High St","desc":"..."}
{"k":"ev","id":"<id>","cal":"Paul","title":"Holiday","allDay":true,"day":20261105,"endDay":20261107}
```

- Timed events use UTC epoch seconds and are shown in the reader's local time.
- All-day events give their first and last (inclusive) local dates as `yyyymmdd`.
- Descriptions are cut to 1000 characters, and the phone sends at most 1500 events.

### Note edits made on the reader

Each save on the reader appends a line to `/data/todo_edits.jsonl`. A later line for the same id replaces an earlier one:

```
{"seq":12,"src":"nt","id":"<noot id>","notes":"full new text","ts":1762345678}
```

The phone reads the file with `READ_FILE` (base64 chunks, following `next` until `-1`). It saves each todo with `GET /api/noots/:id`, replacing or adding its `note` activity, then `PUT`s the complete activity list. It then sends `TODO_EDITS_DONE` with the highest `seq` it saved. The reader drops those lines and keeps any newer ones. Edits that fail to save stay on the reader for the next sync.

### On-device storage

`src/sync/SyncStore.cpp` reads each file into one PSRAM buffer. Only a small index per line is kept in internal RAM, and lines are parsed when they are displayed. Open apps reload when `SyncStore::todosRevision()` / `calendarRevision()` change after an upload.
