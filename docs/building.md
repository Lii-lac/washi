# Building and testing

How to build Washi and its in-game reminder, run the PC tests, find your way around the source, and cut a release. Commands
are run from the repository's root folder unless a step says otherwise.

- [What you need](#what-you-need)
- [Build the app](#build-the-app)
- [Build the in-game reminder](#build-the-in-game-reminder)
- [Run the PC tests](#run-the-pc-tests)
- [What the PC tests don't cover](#what-the-pc-tests-dont-cover)
- [Source map](#source-map)
- [Cutting a release](#cutting-a-release)
- [Platform traps](#platform-traps)

## What you need

- **To build:** [Docker](https://www.docker.com/) (Docker Desktop on Windows, running). The official `devkitpro/devkita64` image
  has the whole Switch toolchain, so nothing else is installed. If you have devkitPro installed yourself, plain `make` works too.
- **To test:** gcc and make on Linux or macOS, or WSL (Ubuntu) on Windows. `gprof` for profiling.
- **Profile pictures:** the app reads them from the console itself, so the build needs nothing. `data/avatars.bin` is an optional
  packed file (fallback only), linked in when it is there; the PC tests use it for the picture-grid scenarios and skip them without it.
  They are Nintendo's artwork, so they are not in this repository: see [profile pictures](profile-pictures.md). Run `make clean` after
  adding or removing the file.
- **Optional:** Python 3 with Pillow, for the icon tools in `gfx/tools/`.

## Build the app

Pick the line for your shell. Each one mounts the repository at `/work` inside the container (see
[Platform traps](#platform-traps) for why it must always be `/work`).

PowerShell:

```powershell
docker run --rm -v "${PWD}:/work" -w /work devkitpro/devkita64 make -j4
```

Command Prompt:

```bat
docker run --rm -v "%CD%":/work -w /work devkitpro/devkita64 make -j4
```

Git Bash:

```bash
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/work" -w /work devkitpro/devkita64 make -j4
```

Linux or macOS:

```bash
docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64 make -j4
```

It works when the last line says `built ... washi.nro`. The output is `washi.nro` in the root folder. Copy it to the SD
card as `/switch/washi.nro`; the app makes its data folder, `/switch/washi/`, itself.

The version is `APP_VERSION` in the `Makefile`. About, the admin page and the Homebrew Menu all show it.

## Build the in-game reminder

Same image, run in `sysmodule/` (PowerShell shown; change the mount part as above for other shells):

```powershell
docker run --rm -v "${PWD}:/work" -w /work/sysmodule devkitpro/devkita64 make
```

The output is `sysmodule/washi-reminder.nsp`. Install it as described in [In-game reminder](reminder.md#install).

Its icon, `sysmodule/source/notify_icon.h`, is drawn by `python gfx/tools/make_notify_icon.py` (run from the root folder). It
also shares `source/cJSON.c` with the app.

## Run the PC tests

No Switch needed. The tests run the real logic and the real screens against a fake console. They write only to `/tmp` and
`test/out/`.

### Logic, reminder and motion

Inside WSL, Linux or macOS:

```bash
cd test && make -f Makefile.host
```

From Windows, in the root folder (WSL starts in the same folder):

```powershell
wsl -d Ubuntu -- bash -c "cd test && make -f Makefile.host"
```

It takes about a minute. Each program ends with `N checks, 0 failed`. `make -f Makefile.roll` runs the progress roll's checks, which are not part of the two suites. `./test_paper --sheets` writes
contact sheets of the paper look to `test/out/paper/` to look at, and `./test_paper --info` prints the bake times and a few hashes; neither asserts anything.

| Program | Source | What it checks |
|---|---|---|
| `test_locker` | `test/test_locker.c` | The swap logic in `locker.c`, `store.c` and `fsx.c`: all four cases of the [restore table](how-it-works.md#what-gets-swapped); shared and uninstalled games; sign-out clearing the console, and refusing to when the backup isn't complete; guest sessions; a guest keeping their play; profiles holding only the games their owner played; the first-run unclaimed saves and giving them away; history keeping exactly the set number; delete, restore and delete for good; deleting older history; journal-size commits; a silently truncated write; the log files (one per start, kept 14 days); the daily backup leaving out downloaded pictures and logs, and throwing away a copy when a file changes while it is copied; refusals (battery, space, a running game) changing nothing; the settings files; passwords; NetIDs; search ranking; game names; the console-profile logic; backups of everything, keeping 4 and restoring. Then **a power cut at every step** of eight operations (swaps, sign-outs, a first swap, a guest keeping their play, unclaimed saves given away): each is followed by recovery, a check that nothing was lost, and a check that the result equals an uninterrupted run. |
| `test_reminder` | `test/test_reminder.c` | The in-game reminder's logic (`sysmodule/source/reminder.c`): reading the files, when a note is due, the note file and icon, the test the app asks for. |
| `test_motion` | `test/test_motion.c` | `source/ui_motion.c`: the springs, throws and give-at-the-ends that move the Backups row. |
| `test_dirty` | `test/test_dirty.c` | `source/dirty.c`: which rectangles are copied to the screen, and the history of the last three frames, over a long random walk. |
| `test_trial` | `test/test_trial.c` | `source/trial.c`: an update's trial run, and going back to the version kept from before. |
| `test_paper` | `test/test_paper.c` | `source/gfx/paper.c`: the lift maths, seeded baking, sizes and contrast of the paper look. |
| `test_avatar_src` | `test/test_avatar_src.c` | `source/gfx/avatar_src.c`: reading the console's own profile pictures (the Yaz0 unpacker on good and bad data, the folder reader on pretend console files). With `WASHI_AVATAR_DIR=<folder>` it also reads every real picture. |
| `test_icons` | `test/test_icons.c` | The Extra icons without a screen: their keys and numbers (`custom_pics.c`), the SD card scan, GitHub's list, the addresses and the downloaded-PNG check (`icons.c`). |

### The whole app

Every screen runs on the PC against the same fake console, driven by scripted button presses and touches, and saves a PNG of each
step in `test/out/app/<number>-<scenario>/`:

```bash
cd test && make -f Makefile.app
```

The whole run takes about 8 minutes and ends with `N checks, 0 failed`. One scenario at a time:

```bash
cd test && make -f Makefile.app app_host && ASAN_OPTIONS=detect_leaks=0 ./app_host first-run
```

| Area | Scenarios |
|---|---|
| Starting | `first-run`, `recovery`, `interrupted`, `refusals` |
| Home | `many-profiles`, `shuffle`, `order-kept`, `sticker-art`, `home-focus`, `gridscroll`, `touch`, `lean`, `wrap`, `theme-follows` |
| Profiles | `password-lockout`, `builtin-keyboard`, `picture`, `console-pictures`, `extra-icons`, `online-picker`, `thumbs-async`, `edit-profile`, `edit-from-home`, `guest`, `guest-keeps`, `adopt-names` |
| Admin | `admin`, `hub`, `deleted`, `purge-all`, `storage`, `storage-many`, `unclaimed`, `backup`, `backups-many`, `backups-motion`, `backups-fuzz`, `online-icons`, `logs`, `about`, `update`, `update-real`, `update-back` |
| Paper look | `lists`, `pad-look`, `pad-press`, `banners`, `sounds`, `sounds-scroll`, `wait`, `wait-clip`, `intro`, `intro-skip`, `intro-assets`, `washi-progress-noicon`, `washi-progress-many` |
| Reminder | `reminder-signout`, `reminder-setting`, `reminder-test`, `reminder-soon` |

Options (put them before `./app_host`):

| Option | What it does |
|---|---|
| `DARK=1` | Runs with the console in its dark theme, so Washi follows it into Paper night; screenshots in `test/out/app-dark/`. The pixel checks read the theme's own colours and pass there too. |
| `TRACE_PNG=1` | Keeps every frame of a movement (`gridscroll`, `storage`, `shuffle`, `lists`, `lean`, `hub`, `backups-motion`). |
| `BK_DEBUG=1` | Prints the Backups row's position on every frame (`backups-motion`). |
| `SORT_ANIM=12` | Runs `sort-anim`: every frame of the order sleeve opening, a pick and closing (with `TRACE_PNG=1`). Without it the scenario does nothing. |
| `THUMBS_ASYNC=1` | Makes the small picture copies on their own thread, as on the console (the other scenarios make them at once so their frames don't depend on timing). |
| `NO_ORACLE=1` | Skips drawing every Home frame whole a second time to compare (faster). |

### Frame timing

```bash
cd test && NOPNG=1 BENCH_TOP=1 make -f Makefile.app bench
cd test && make -f Makefile.app prof SC=hub
```

`bench` times every frame without the sanitizers and lists the slowest. `PICKER_BENCH=1 BENCH=1 ./app_bench picker-bench` times the Picture page alone
(15 games, one of 300 icons; add `THUMBS_ASYNC=1` to make its small pictures on a thread, as the console does). `prof` shows where one scenario's time goes (gprof). The
console's CPU is several times slower than a PC: a frame that takes 2 ms here is about 16 ms there. Keep common frames (moving
the cursor, scrolling) near or under that.

`make -f Makefile.host clean` and `make -f Makefile.app clean` delete the test programs and screenshots.

## What the PC tests don't cover

The fake console stands in for everything below, so only the console can test it. [TESTING.md](../TESTING.md) is the checklist.

- The real save file systems and libnx calls (`source/backend_nx.c`), including how big saves commit.
- The Switch platform layer (`source/platform_nx.c`): buttons, touch, the system keyboard, clock, battery and theme. The
  held-button logic (`ZR` + `X` on Deleted profiles, telling a tap from an auto-repeat) runs only there; the tests inject its
  result.
- A running game, applet mode against full application mode, and a real power cut.
- The console's own profile editor (`source/sysprofile.c` is tested only against the fake).
- The reminder starting with the console, its clock, and Ultrahand drawing the note.
- The forwarder.
- Reading the console's own profile pictures from its system data, and the Extra icons downloads from GitHub (HTTPS, several at once).
- The small-picture thread on its own CPU core, and how its timing feels.
- The real frame rate, and how anything feels.

## Source map

| Path | What is there |
|---|---|
| `source/locker.c`, `locker.h` | The logic: swap, sign-out, recovery, roll back, checking, history, unclaimed saves, backups of everything. |
| `source/store.c`, `store.h` | The SD layout: config, the journal (`state.json`), profiles, passwords, deleted profiles, the log files (`logs/`, one per start, kept 14 days). |
| `source/fsx.c`, `hash.c` | File helpers (tree copy, tree compare by size and CRC32, atomic writes); SHA-256 and CRC32. |
| `source/backend.h` | The only interface to the console. |
| `source/backend_nx.c` | Its libnx implementation. |
| `source/sysprofile.c` | The console's own profile (name and picture) and its saved original. |
| `source/nacp_name.c` | Game names, including the compressed ones from firmware 21 on. |
| `source/app.c` | Every flow: first run, home, choosing, sign-out, every admin page, recovery. |
| `source/main.c` | Starts the console services, opens the console's profile pictures (system data `010000000000080A`) and calls `app_run`. |
| `source/trial.c`, `trial.h` | An update's trial run, and going back to the version kept from before if the new one doesn't start. |
| `source/net_nx.c`, `job_thread.h` | HTTPS on the console (libcurl over the console's ssl service; a pool of connections for downloads several at once); a worker thread, a lock and a sleep, on the console and on a PC. |
| `source/icons.c`, `custom_pics.c` | Extra icons without a screen: GitHub's list of the collection, the addresses, the downloaded-PNG check; the pictures on the SD card and the numbers the interface gives them. |
| `source/platform.h`, `platform_nx.c` | Buttons, touch, screen, clock, theme, keyboard, sound; the Switch implementation and the 0.4 s fade-in. |
| `source/audio.c`, `audio.h` | The sounds: a small mixer on audout (console only). The sounds are `data/*.wav`. |
| `source/ui.h`, `ui_internal.h`, `ui_core.c` | The interface toolkit: input, transitions, leaning, the press, sounds. |
| `source/ui_scene.c`, `ui_scene.h` | Paper items that lift on a still page, painted by dirty rectangles (dialogs, the picture picker, Storage, the wheels, About), and the page pieces they share. |
| `source/ui_home.c` | The home screen, the A-Z rail, and `ui_sort_menu` (every order menu). |
| `source/ui_stickfx.c`, `ui_fx.c` | The order menu's sleeve of stickers and the stickers' art (also on the chips); paper drawn turned and curled in 3D, with light and soft shadows. |
| `source/ui_intro.c` | The intro: the tape wall, the card, the seal, then Home coming in. |
| `source/ui_dialogs.c` | Messages, questions, choices, the password pad, the built-in keyboard. |
| `source/ui_list.c`, `ui_hub.c` | Settings-style lists; the admin hub. |
| `source/ui_storage.c`, `ui_backups.c` | The Storage and Backups pages. |
| `source/ui_spr.c`, `ui_spr.h` | Cached pictures of items with their shadows, laid over a page and slid about (the Backups row and the picture strip). |
| `source/ui_icons.c`, `ui_update.c` | Admin → This app: the Extra icons page (downloads on worker threads); Check for updates. |
| `source/ui_picker.c`, `ui_wheel.c`, `ui_progress.c`, `ui_about.c` | Edit Icon and its Background grid; the reminder's wheels; the progress screen; About. |
| `source/ui_extra.c`, `ui_thumbs.c` | Edit Icon's Picture page: the strip of cards (Console, then the Extra icons games) and its sheet; the small picture copies made on a thread of their own. |
| `source/ui_motion.c` | Springs and throws (pure maths, no drawing). |
| `source/dirty.c` | Which rectangles of a frame are copied to the screen. |
| `source/ui_wait.c` | The "still working" scrap: taped paper, crayon doodles, the peel. Moves when the long loops call `gfx_pump` / `fsx_pump`; `test/app_host.c wait` runs it (`WAIT_FRAMES=1` saves its frames, `WAIT_ON=1` lets it come down in every scenario). |
| `source/gfx/` | The software renderer, the look (`paper.c`: paper, tape, crayon marks, lift maths; `skin.c`; `paper_roll.c`: the switching bar), the crayon filter for game icons (`crayon.c`), profile colours (`tint.c`), the console's profile pictures (`avatar_src.c`: Yaz0 files read one at a time), fonts and image decoding (stb). |
| `source/swatches.h` | The console's 72 background colours. |
| `source/cJSON.c` | The vendored JSON library. |
| `data/` | `qr.bin` (the club's QR code), the `.wav` sounds and the intro's pictures, linked into the app, plus `avatars.bin` (an optional fallback packed file of pictures: you make it, git ignores it). |
| `test/` | The logic tests, the fake console (`backend_host.c`), the scripted platform (`platform_host.c`) and the whole-app scenarios (`app_host.c`). |
| `sysmodule/` | The in-game reminder: `source/reminder.c` (logic), `source/main.c` (the loop), the NPDM settings in `washi-reminder.json`. |
| `forwarder/` | Where the Home-menu forwarder NSP is kept once you have built it (git ignores it). See [forwarder](forwarder.md). |
| `gfx/` | Design work: the mock-up scene renderer (`make` in `gfx/` builds a preview app), `tools/` for the icons and the QR code. |
| `icon.jpg` | The app icon, drawn by `gfx/tools/make_icon.c`. |
| `tools/` | PowerShell helpers for copying files to the console over MTP, `sfx.py` (makes the sounds in `data/`) and `pins/` (the pins' pictures). |

## Cutting a release

There is no release page; a release is a set of files laid out as on the SD card.

1. Raise `APP_VERSION` in the `Makefile`.
2. Run both PC test suites. Both must end with `0 failed`.
3. [Build the app](#build-the-app). The build should have no warnings. If `sysmodule/` changed, [build the reminder](#build-the-in-game-reminder) too.
4. Lay the files out as on the SD card:
   - `switch/washi.nro`
   - optional: `atmosphere/contents/4200000000534C4B/exefs.nsp` and an empty `atmosphere/contents/4200000000534C4B/flags/boot2.flag`
   - optional: the forwarder NSP you built (see [forwarder](forwarder.md); it only changes when its name, icon or version does)
5. Copy them to the console and compare a hash of each copy with the original. An SD card mounted over USB can drop off
   mid-copy and leave a 0-byte file.
6. Work through [TESTING.md](../TESTING.md) on the console.
7. Commit the version change together with any doc changes it needs.

## Platform traps

- **Git Bash rewrites paths.** It turns arguments that start with `/` into Windows paths. For Docker, use `MSYS_NO_PATHCONV=1`
  and `$(pwd -W)` as above. For the forwarder, run NTON from PowerShell: from Git Bash, `--sdmc /switch/washi.nro` is
  rewritten and the forwarder crashes at launch.
- **Always mount at `/work`.** The `build/` folders keep dependency files with absolute paths (`/work/source/...`). Mounted
  anywhere else, `make` looks for files that aren't there. Delete `build/` (or run `make clean` in the container) to start over.
- **Changing only the `Makefile`'s flags doesn't rebuild anything.** Delete `build/` first.
- **Line endings are LF.** `.gitattributes` makes git check files out with LF on every system, so a diff shows only your change. If an
  editor on Windows saves CRLF, git turns it back on commit; `git diff --stat` showing a whole file as changed means something else is wrong.
- **Loops inside `wsl -- bash -c "..."` from Git Bash** lose their variables (`$s` arrives empty, so `./app_host $s` runs every
  scenario). Put the loop in a script file and run that.
- **Windows locks a folder a shell is standing in.** Leave `test/out/...` before deleting it.
- **`test/test_roll`** is built by hand from `test/test_roll.c` with `make -f Makefile.roll` (the progress roll's checks); it is not part of the two PC suites.
