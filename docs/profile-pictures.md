# Profile pictures

Washi offers the Switch's own profile pictures, and it reads them from your console. They are Nintendo's artwork, so none of them is in
this repository, and nothing is copied off the console: Washi just asks for them when it needs them.

## How it works

- At start, Washi asks the console for its picture data (system data `010000000000080A`) and reads one picture at a time, when a
  screen needs it. The set is always the one your firmware has, in the order the console shows it, so new characters appear after a
  system update with no change to Washi.
- Each picture is 256 × 256 pixels. A profile remembers its picture by number (the console's icon id minus one), which is also the icon
  id Washi writes to the account when **Console profile** is on.
- If the console says no (the app wasn't given access to its system data), or the data has no pictures, every profile shows its first
  letter on its colour instead. Everything else works the same, and a new profile is made right after its name.

Washi's log (a file for every start in `switch/washi/logs/` on the SD card; Admin → This app → **Activity log**) has one line per start that says which it was, for example
`profile pictures: the console's own, 147 pictures`, or `none, every profile is a letter; the console's picture data wouldn't open (error 0x…)`.

## What you see

| | The console's pictures | Letters |
|---|---|---|
| Home screen, banner, dialogs | The picture on the profile's colour | A letter on the profile's colour |
| New profile | Starts with a random picture nobody else has | Starts with a letter and a random colour |
| Edit profile → Picture and background → **Picture** | The **Console** card: the letter, then every picture in the console's order | The **Console** card with only the plain letter |
| **Background** | The same 72 colours | The same 72 colours |
| Console profile (Admin → Console) | Shows the picture | Shows the letter |

## Extra icons

The pictures from Nintendo Switch Online and the games' own icon sets are not in the console's built-in data: they are loaded from
Nintendo's website. Washi can fetch the character pictures from a community collection,
[henry-debruin/nso-icons](https://github.com/henry-debruin/nso-icons) (one folder per game, 256 × 256 PNGs), and offer them on the same
Picture page, a card for each game.

1. **Get them:** Admin → This app → **Extra icons** (the console needs the internet). A slip at the top says how many games are on the
   card, and a strip of tape under it has a piece for every game (stuck down = on the card, dashed = not there). Below it is a row
   for every game with how many icons it has and its size. **A** gets that game's characters (about 70 icons, a few MB; on a game that
   is there already, it looks for new ones), **X** gets every game that is missing, **Y** removes a game, and **R3** changes the order
   (name, recently downloaded, most icons, on your card first). A download shows a bar on the row, and **B** stops it: what has come is
   kept, and the page carries on from there next time. Several icons come at once over connections that stay open.
2. **Use them:** in Edit Icon, open **Picture**. It is a strip of cards that slides like the Backups page's: first **Console** (the
   console's own pictures), then a card for every game on the SD card (the Background grid has none). **L** and **R** go to the
   previous and next card, **ZL** and **ZR** skip five, and the D-pad moves in the sheet of pictures below. **A** picks one, a tick marks
   the one the profile uses now, and **R3** (or the chip, with two games or more) changes the order of the games; Console stays first.
   Left or up on the first picture goes to the last, and right or down on the last to the first, also when the button is held. With no
   games on the card, a dashed outline where the next card would be says where to get them.

They are kept on the SD card as `switch/washi/pictures/<game>/<name>.png`, so you can also add pictures of your own: a PNG of any
size up to 4 MB (it is scaled to 256 × 256) in a folder of your choice, named with letters, digits, `.`, `_` and `-`.

- A profile remembers an Extra icon **by name**, so adding or removing games never changes anyone's picture. If its file is gone, the
  profile shows its first letter until the icon is downloaded again.
- With **Console profile** on, the console's own account shows the picture, but it has no icon id for it (only the console's own pictures do).
- GitHub lets a network ask for the list only about 60 times an hour. If the page says GitHub answered with an error (403), wait a while
  and open it again; downloads of the pictures themselves are not limited that way.
- The collection has no licence and the pictures are Nintendo's artwork: Washi never ships them. Each person's console downloads them
  onto its own SD card, and the page says so.

## If you get letters

1. Look at the log line above. An error code means the console refused to open its picture data.
2. Start Washi the way you normally do, and also once from the Homebrew Menu, to see whether it depends on how it was launched.
3. As a fallback you can still make a packed file of your own and link it into the app (below). Washi uses it only when the console's
   own pictures can't be had.

## Fallback: a packed file of your own (optional)

You need Python 3 with Pillow (`pip install pillow`) and a collection of the pictures as 256 × 256 PNGs with a `README.md` that lists
them in the console's order, such as [vishans/Nintendo-Switch-Profile-Icons-Avatars](https://github.com/vishans/Nintendo-Switch-Profile-Icons-Avatars).

```
python gfx/tools/pack_avatars.py path/to/collection/avatars path/to/collection/README.md data/avatars.bin
```

Then run `make clean` and the usual build (see [building](building.md#build-the-app)). The Makefile links `data/avatars.bin` in when it
is there. `data/avatars.bin` is in `.gitignore`: don't force it into a commit, and don't publish a build that contains it.

## For developers

The reader is `source/gfx/avatar_src.c`: each picture is a Yaz0-packed raw RGBA image (`chara/<id as 8 hex digits>.szs`) and
`chara/table.bin` lists the ids in the console's order. The PC tests (`test_avatar_src`, and the `console-pictures` scenario) run it on
pretend files made by `test/avatar_fixture.h`. To read real ones on a PC, point `WASHI_AVATAR_DIR` at a folder with `chara/` in it
(for example the console's data extracted from firmware) when running `test_avatar_src`.
