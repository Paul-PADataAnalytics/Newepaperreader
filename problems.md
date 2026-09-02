# Problems.md — The "Screen Stopped" Incident

This document records the major issues that caused the e-paper display to stop
updating (and the related sleep/wake instability) during the v2.2 → v2.3
development cycle, how each was root-caused, and how each was fixed. It exists
so a future session doesn't have to re-discover any of this the hard way.

The short version: **one pin (GPIO0) and one missing function call
(`epd_clear()`) caused almost all of it.** Everything else below is secondary
damage or unrelated bugs that surfaced once those two were fixed and code
paths (like sleep/wake) actually started running for the first time.

## Timeline

1. **v2.2 development (`dbe0538` and earlier)**: a "BOOT button (GPIO0)
   sleep/wake toggle" feature was added, reading GPIO0 as a manual
   sleep-toggle input.
2. Shortly after, the panel stopped updating entirely — no crashes, no
   errors, SD card and BLE worked fine, but the screen never visibly
   changed again.
3. A parallel/duplicate attempt at similar functionality (BOOT-button standby
   mode, session restore) was pushed independently to `origin/main`
   (`d899d55`, `7a0ead5`) along with two more EPD power-state HAL fixes
   (`3c87d3f`, `024af4b`) — this reintroduced the same GPIO0 pattern and was
   never reconciled with the fix below (see "Git History Divergence").
4. The local project was **rolled back** to the last known-good state
   (v2.0, tag-adjacent commit `1e3fea3`) and the display bug was root-caused
   and fixed from scratch (`e18b222` and follow-ups), this time never
   touching GPIO0 again.
5. Once the display worked again and sleep/wake could actually run
   end-to-end for the first time, several more **latent bugs** that had
   never been exercised on real hardware surfaced and were fixed in turn.

## Issue 1 — GPIO0 is not a free pin: it's the EPD driver's CFG_STR line

**Symptom**: firmware ran with zero crashes or errors — SD card mounted,
BLE started, `epd_clear()`/`display()` calls all returned normally — but the
e-paper panel never visibly updated again after the "BOOT button" feature was
added.

**Root cause**: GPIO0 is hard-wired on the T5-EPD47-S3 board to the
74HCT4094D `CFG_STR` line (`#define CFG_STR GPIO_NUM_0` in `ed047tc1.h`,
marked `Free: NO` in the LilyGo-EPD47 driver's own GPIO table). It's used
internally by `epd_poweron()`/`epd_poweroff()` to strobe the panel's
power/config shift register. Reconfiguring it as `INPUT_PULLUP` for the
BOOT-button feature silently broke the driver's ability to drive that line —
with no visible error anywhere, because the driver has no way to detect that
its own control pin has been repurposed out from under it.

**How it was found**: `git bisect` between the confirmed-working v2.0 tag and
the non-working v2.2 tag isolated the exact breaking commit (`dbe0538`), then
cross-referencing the driver's vendored GPIO reservation table confirmed why.

**Fix** (`e18b222`): removed all `GPIO_NUM_0` usage (`pinMode`,
`gpio_pullup_en`, `digitalRead`, `gpio_wakeup_enable`) from application code
entirely. The BOOT-button sleep toggle was removed — GPIO0 cannot safely be
read as a general input on this board, ever. It was later reimplemented on a
genuinely free pin instead (see Issue 6).

**Rule going forward**: never touch `GPIO_NUM_0`/`GPIO0` anywhere in
application code. When bisecting a hardware display bug, always cross-check
the board's GPIO reservation table in the vendored driver before repurposing
any pin — "looks unused in app code" does not mean "unused by the underlying
hardware driver."

## Issue 2 — Missing `epd_clear()` before every draw

**Symptom** (found during the post-rollback recovery): the panel showed
ghosting/grey smears instead of clean frames, and early debugging assumed the
panel was hardware-worn or needed grayscale thresholding.

**Root cause**: `epd_draw_grayscale_image()` assumes it's drawing onto a
known-white panel and picks per-pixel drive times based on that assumption.
If called without a preceding `epd_clear()`, it draws on top of whatever was
already there.

**How it was found**: a 3-stage on-device differential self-test
(clear+draw vs poweron+clear+draw vs draw-with-no-preceding-clear) proved the
missing clear was the exact trigger.

**Fix**: `DisplayHAL::display()` now calls `epd_clear()` itself, immediately
after `epd_poweron()` and before drawing, so no caller can skip this step.

## Issue 3 — EPD renderer starved by higher-priority tasks / GPIO ISR storms

**Symptom**: the screen would render perfectly in a bare-metal hardware test
(no touch, no BLE) but silently fail to update in the full firmware — no
error, framebuffer confirmed non-empty (~55% non-white bytes), but the panel
stayed blank/unchanged.

**Root cause**: `epd_draw_grayscale_image()` spawns internal rendering tasks
at **priority 10**. Any application task pinned to the same core running at
equal or higher priority (e.g. the touch reader task at priority 24)
preempted the renderer mid-DMA-transfer and silently corrupted the frame
timing. Separately, GPIO interrupts preempt tasks at *any* priority — the
GT911 touch controller fires INT pulses at ~10–100 Hz even with no finger on
the panel unless explicitly configured for falling-edge-only, and every pulse
could preempt the renderer.

**Fix**:
- Any custom task pinned to core 1 must run at priority < 10 (touch reader
  task set to priority 5).
- `touch.setInterruptMode(FALLING)` so the touch controller only pulses on
  touch-down, not on release/heartbeat noise.
- Touch task creation and GPIO ISR installation deferred until after the
  first `drawMenu()` completes, so the first frame renders in a quiet system.

## Issue 4 — Panel-wear grayscale threshold

**Symptom**: on this specific hardware unit (mild bottom-right panel
degradation), multi-frame grayscale content was only visible for pixel
values near pure black; mid-tone grays never appeared.

**Root cause**: the shorter drive-time frames in the 15-frame grayscale ramp
don't accumulate enough charge on a worn current pump to flip mid-tone
pixels, while uniform full-strength pulses (`epd_clear`, `epd_push_pixels`)
still work reliably.

**Fix**: `DisplayHAL::display()` thresholds each pixel nibble (below 8 →
black, at/above → white) before handing the buffer to
`epd_draw_grayscale_image()`. UI text loses anti-aliased edges but becomes
fully visible.

## Issue 5 — Light-sleep wake path: interrupt storm and dead-loop bugs

Once the display was working again and the 30-second inactivity sleep timer
could actually trigger for the first time, two bugs in the never-before-run
wake path surfaced:

1. **Interrupt watchdog crash loop**: arming `TOUCH_INT` for
   level-triggered wakeup (`gpio_wakeup_enable(..., GPIO_INTR_LOW_LEVEL)`)
   silently overwrote the pin's shared interrupt-type register from
   NEGEDGE (used by the always-attached touch ISR handler) to LOW_LEVEL,
   with no restore. If the line read low afterward (real touch *or* routine
   GT911 noise), the level interrupt refired continuously, starving CPU1's
   idle task and tripping the ESP32 interrupt watchdog — a full crash-reboot
   loop. Fix: fully detach the edge ISR handler before arming the
   level-triggered wakeup, and only reattach NEGEDGE mode after servicing
   the touch controller at task level post-wake.
2. **Silent infinite sleep/wake loop**: a "noise glitch" branch that called
   `powerOff()` and looped back into `esp_light_sleep_start()` forever if a
   post-wake recheck found no confirmed touch — this could hang the device
   in a silent sleep/wake loop with zero redraws and no way out. Fixed by
   reverting to an unconditional wake-then-redraw pattern, then later (once
   understood properly) replaced with an explicit real-touch/real-button
   check that only re-arms sleep instead of looping blindly.
3. **GT911 noise pulses waking the device from sleep** with no real touch
   present — filtered by checking `getTouch()` actually returns a valid
   point before treating a wake as real.

**Lesson**: a bug that prevents a code path from ever running (sleep never
triggering) can be hiding its own separate, never-exercised bugs underneath.
Fixing the first bug doesn't mean the feature works until the newly-reachable
path has actually been exercised on real hardware.

## Issue 6 — Redesigning the sleep/wake button without GPIO0

Once GPIO0 was permanently off-limits (Issue 1), the manual sleep/wake toggle
was redesigned on `BUTTON_1` (GPIO21) — a genuinely free, dedicated pin on
this board, not shared with SD/I2C/touch/CFG_STR, and with no ISR ever
attached to it (poll-only design). This avoided all of the shared-interrupt-
register risk described in Issue 5. Later still, the whole mechanism was
replaced again with a true deep-sleep "lock mode" (real
`esp_deep_sleep_start()` + `ext0` RTC wakeup on `BUTTON_1`, full chip reset on
wake, breadcrumb-based resume back to the exact app/book/page) — see
`HANDOVER_LOG.md` and `manual/04_ereader_app.md` for the current design.

## Issue 7 — Calculator digit-entry corruption bars (related, not the same root cause)

**Symptom**: the Calculator app showed full-height corruption bars on
hardware during digit entry, confirmed via photo.

**Root cause**: `DisplayHAL::updateScreenFast()` used the driver's manual
1-bit fast-draw pipeline (`epd_push_pixels()` + `epd_draw_frame_1bit()`) plus
a hand-rolled byte-swap "fix," introduced alongside portrait-rotation support
in v2.0.0. This pipeline had no other proven usage anywhere in the codebase
and was never reliably working. (A byte-swap-only theory was tested and
empirically made things *worse* — ruled out.)

**Fix**: rewrote `updateScreenFast()` to reuse the same proven 4bpp
grayscale pipeline that `DisplayHAL::display()` uses for full-screen redraws
(`epd_clear_area()` + `epd_draw_grayscale_image()`), scoped to just the small
diff bounding box, with a reduced erase-cycle count for speed. No more manual
bit-packing or byte-swapping.

## Git History Divergence (the "rollback" split into two branches)

While the local rollback-and-redo (Issues 1–7 above) was happening, a
**separate 4-commit history had already been pushed to `origin/main`**
before the rollback (`d899d55`, `7a0ead5`, `3c87d3f`, `024af4b`), all dated
2026-07-24, ~12:37–15:13. That pushed history included the same
GPIO0-as-button pattern this document describes as the root cause of the
display failure (Issue 1) — it was never reconciled with the fix, so
`origin/main` was left diverged from local `main` for two days: local ahead
by 17 corrected commits, origin ahead by 4 stale pre-rollback commits.

This was discovered and resolved on 2026-07-26: confirmed via commit
timestamps and diff review that local `main`'s history was the corrected,
hardware-verified continuation and origin's 4 commits were the abandoned
pre-rollback path (including the very GPIO0 bug that caused the whole
incident). `origin/main` was force-pushed to match local `main`
(`git push --force-with-lease`), discarding the stale commits. Both are now
in sync at `03f3e88` (v2.3.0).

**Lesson**: after a local rollback, remember to reconcile the remote too —
otherwise a stale, already-abandoned branch can sit on the remote
indefinitely and get accidentally pulled back in later.

## Current Status

As of v2.3.0 (2026-07-26): display renders reliably, no sleep/wake crash
loops, Calculator no longer corrupts, and `origin/main` matches local `main`.
See `HANDOVER_LOG.md` for the current architecture summary and `TODO.md` for
the session log.
