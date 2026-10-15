# In-game reminder

Optional. A small background program (a sysmodule) that reminds players to sign out while they are in a game. While someone's
saves are loaded, a note slides in over the game for about 6 seconds: titled with their name, "Don't forget to sign out when
you're done playing!", with a white sign-out arrow. The game keeps running and keeps the controls.

## When it comes

- First when the saves have been loaded for the **Sign-out reminder** time (Admin → Console; 3 hours by default), then again, and again, for as long as the same person stays loaded: as often as that time, but never less often than every hour. So 3 hours reminds at 3, 4, 5 hours and on; 45 minutes at 45, 90, 135 minutes; 1 minute every minute (for testing).
- Answering **Yes, still me** to "Is this still you?" starts the count again.
- Never while nobody is loaded, while the reminder is set to off, or while a swap is half done.
- A note missed while the console slept is not caught up; only the latest one shows.

It checks every 30 seconds, so a note can come up to half a minute after it is due.

## What it needs

- Atmosphère, which starts it with the console.
- [Ultrahand](https://github.com/ppkantorski/Ultrahand-Overlay), the overlay menu, which draws the note. Install it with its own
  instructions (it replaces Tesla Menu). Without Ultrahand's `config/ultrahand/notifications/` folder, the reminder writes
  nothing.

## Install

1. Install Ultrahand.
2. [Build](building.md#build-the-in-game-reminder) `sysmodule/washi-reminder.nsp`, or use a copy someone already built.
3. Copy it to the SD card as `atmosphere/contents/4200000000534C4B/exefs.nsp`.
4. Create an empty file `atmosphere/contents/4200000000534C4B/flags/boot2.flag`.
5. Restart the console.
6. [Check that it works](#check-that-it-works).

To remove it, delete `atmosphere/contents/4200000000534C4B` and restart. If the console stops booting into Atmosphère after
installing it, delete that folder from the SD card on a PC.

## Check that it works

Admin → Console → **Test the in-game reminder** offers two tests:

| Test | What happens |
|---|---|
| **Show a test note now** | The app asks the reminder for a note and waits up to 12 seconds. **Test reminder sent**: the note pops up. **The reminder isn't running**: it didn't start; restart the console. **Ultrahand isn't set up**: the reminder runs, but Ultrahand's notifications folder is missing. |
| **Repeat every 30 seconds** | A switch (dashed tape guide with a paper knob: off is empty, on has a strip of tape stuck down and a check on the knob). Flipping it leaves or removes a file on the SD card and shows what the card really holds, so it never lies; the Test row says "notes every 30 s" while it is on. While it is on a note comes every 30 seconds, whoever is loaded and whatever the reminder's setting, **until you switch it off**. It keeps going when Washi is closed: close Washi, start a game, and the note comes over it. The note says "Washi test". |

## What it touches

It only reads Washi's files and writes the note. Nothing else.

| Reads | Writes |
|---|---|
| `switch/washi/state.json` (who is loaded, since when, whether a swap is half done) | `config/ultrahand/notifications/washi-<time>.notify` (one note; an earlier one of its own that wasn't shown yet is removed) |
| `switch/washi/config.json` (the reminder time) | `config/ultrahand/assets/notifications/washi.rgba` (its 50×50 icon; written only when missing or different) |
| `switch/washi/profiles/<id>/profile.json` (the name; "Washi" when it can't be read) | `switch/washi/reminder-test.done` (its answer to a test) |
| `switch/washi/reminder-test` and `switch/washi/reminder-repeat` (whether a test was asked for, and whether the repeating test is on: it only looks that the files are there) | `switch/washi/reminder.log` (a few lines of what it did, for finding out why a note did not come; restarts past 8 KB) |

Its program ID is `4200000000534C4B`. Its logic is in `sysmodule/source/reminder.c`, tested on the PC by `test/test_reminder.c`.
How it behaves on the console (starting at boot, its clock, the note over a game) is only checked there: see section 10c of
[TESTING.md](../TESTING.md).
