# Home-menu forwarder

Optional. A forwarder is a small installed title that puts **Washi** on the Home menu, so nobody has to go through the
Homebrew Menu. Its only job is to start `sdmc:/switch/washi.nro`.

## Install

1. Put the app at exactly `/switch/washi.nro` on the SD card. The forwarder looks there and nowhere else.
2. Build the forwarder (see [Rebuild it](#rebuild-it); the NSP, `Washi v1.0.7 by Esports Club [0114013574378000].nsp`, about
   290 KB, is not kept in this repository), then install it with an NSP installer such as Tinfoil or DBI.
3. Open **Washi** from the Home menu. It starts straight into the app, in full application mode, fading in from black.

The Washi icon appears on the Home menu. While someone is loaded and [Console profile](reference.md#console-profile) is on (Admin → Console), the account at the top left reads that person's name and shows their picture.

Updating the app never needs a new forwarder: replace `washi.nro` and the forwarder starts the new one.

**Replacing an older forwarder.** The app used to be called Save Locker, and its forwarders were too. An old one looks for
`/switch/save-locker.nro`, so it can't start the renamed app. Delete it first: System Settings → Data Management → Manage Software → **Save Locker**
→ **Delete Software**, then install the new one. The Save Locker forwarders used title IDs `01c440ed7098f000`, `01d51c587ef6b000` (with the old icon) and `0160b5c20534e000`; the
Washi forwarders before 1.0.7 (the card icon) used `01b4dcffdf365000`. If any is still installed, delete it too, or the Home menu shows two.

## No logo screen

Version 1.0.3 and later start with no Nintendo logo screen. The NSP has no logo section, and the app's own first frame is black and fades up
over 0.4 seconds (`FADE_SECONDS` in `source/platform_nx.c`).

## Rebuild it

You only need to when the forwarder's name, icon or version changes. It is made with [NTON](https://github.com/rlaphoenix/nton),
which needs your own `prod.keys` (they are not kept in this repository, and must never be added to it).

```powershell
nton build washi.nro --sdmc /switch/washi.nro
```

- **Run it from PowerShell, not Git Bash.** Git Bash rewrites `/switch/...` into a Windows path, and the forwarder then crashes at
  launch.
- **Keep the title ID** (`0114013574378000`, NTON's `--id`) and raise `--version` each build. Then delete the old forwarder on the
  console and install the new one.
- **The console caches Home-menu icons by title ID.** A changed icon needs a new `--id`, or the old forwarder deleted first.

The no-logo build needed three small patches to NTON, which are not in this repository:

1. NTON wrote a 16-byte version string into a 15-byte slice of the NACP, which shifted every later field by one byte:
   `[0x3060:0x306F]` becomes `[0x3060:0x3070]`.
2. Two NACP bytes: LogoType 2 and LogoHandling 0.
3. Two hacBrewPack flags: `--nologo --nopatchnacplogo`.

The exact option spellings for the title ID and version are NTON's; check `nton build --help` for the version you have.

## The icon

The Home-menu icon is `forwarder-icon.jpg` (256×256): a navy hanko seal pressed by hand onto cream washi, with a person cut out of it, a dotted ring, and two
pieces of tape. Pass it to NTON with `--icon`.

`icon.jpg` (256×256) is the app's own icon in the Homebrew Menu. It is drawn by `gfx/tools/make_icon.c` with the app's own renderer: a game card whose label is a person. Other
directions that were tried are in `gfx/tools/icon_drafts.c`. Build and run it from `gfx/tools/`:

```bash
gcc -O2 -I../../source/gfx -o make_icon make_icon.c ../../source/gfx/gfx.c -lm && ./make_icon ../../icon.jpg out
```
