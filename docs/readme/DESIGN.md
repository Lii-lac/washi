# README pictures: design rules

The rules the README's pictures are drawn and checked against. If a rule or token changes, change it here and in
[`build.py`](build.py) in the same commit.

## 1. The theme comes from what Washi is

Washi is **a pinboard for one shared console**: each screen is paper pinned to a cream board with washi tape, and the app's own
name is a printed strip of tape. The README is drawn from the same materials, so a reader meets the same thing in the README
that they meet on the console. Nothing else is borrowed: no mascots, no badges. The one exception is the maker's seal in the
corner of the hero: a small ink-blue hanko with the GitHub mark and the handle Lii-lac, pressed like the app's stamps (`maker_mark` in `build.py`).

## 2. Rules

1. **The pictures show the real app.** Every screenshot is rendered by the app's own PC build (`shots.sh`, the `readme-shots`
   scenario), at the current source. The people in them are made up. Their pictures are the Switch's own (Nintendo's), from the maintainer's local
   `data/avatars.bin`, which is not in the repository (docs/profile-pictures.md); a build without it shows letters. Nothing is mocked up or painted over.
2. **Only the app's colours.** Neutrals and the ink blue come from `source/gfx/paper.h`; tape is the console's pastels
   (`source/gfx/paper.c`). No new accent colour.
3. **Tape is flat and matte.** When it carries text, in Paper night it is darkened and the text is light (`deep` and the `tx` token in
   `build.py`), because dark text on mid-tone tape reads as black on a dark page. Its pattern is fainter there so the text stays clean.
4. **One font:** M PLUS Rounded 1c (the app's, SIL OFL 1.1), embedded from `gfx/fonts_src`. Name at least 96 px and the promise at
   least 40 px, so both read when the picture is shown at phone width.
5. **Paper is matte.** Fine grain, tight warm contact shadows, never a glow. Prints and strips tilt by one or two degrees; text
   on them tilts with them and stays readable.
6. **Every picture carries its own ground** (the cream board, or Paper night's sky), so it reads on a white page and on a dark one.
   There are **two versions of each picture, one per theme**, as in the app. The README serves them with `<picture>` and
   `prefers-color-scheme`: the light file in `docs/readme/`, the dark one under the same name in `docs/readme/dark/`. The dividers
   and nav labels are transparent PNGs instead, so they sit on GitHub's own page colour.
7. **Contrast passes WCAG AA.** `build.py` checks every text colour against its ground and stops if one fails (4.5:1 under 24 px,
   3:1 from 24 px up).
8. **The README makes sense with the pictures off.** Each picture has alt text that says what it shows, and the cards and the key
   legend carry their facts in it.
9. **Light files, no metadata.** JPEG, written by `build.py` from a fresh bitmap, so no EXIF or account data survives.
10. **No GitHub rule under a picture.** GitHub draws a line under every `#` and `##` heading, so the README's headings are `###`,
    and the tape dividers are the only dividers.

## 3. The pieces

Everything is drawn from the app's own materials: taped paper cards, dashed-thread arrows, the app's button glyphs, crayon-ringed
step numbers and doodles, and **ink-blue stamps** (CHECKED, LOADED) used as in the app: "this one".

- **Dividers** (`div-*.png`, decoration, `alt=""`): a different one above each section. *Roll* is the app's own progress roll laying a
  straight tape to the right edge (`rolldiv.c`, `roll.sh`: the same drawing code as the Swap screen, matted onto transparent);
  *sampler* is eight scraps stuck on; *doodles* is crayon doodles along a dotted thread; *bunting* is tape flags on a thread;
  *peel* is one strip with its end lifting off the page.
- **Nav labels** (`nav-*.png`): tape labels with a crayon icon, linking to the sections.
- **Cards and keys** (`features.jpg`, `keys.jpg`): four taped cards for what Washi gives you, and the keys the player needs.
- **Diagrams** (`diagram-swap.jpg`, `diagram-sd-card.jpg`): a swap's three steps, and what lives on the SD card. A diagram only draws
  what the text next to it already says, and its alt text says it again in words. The people in them are the same made-up cast as
  the screenshots, cut from the same render.
- **Hero and strip** (`hero.jpg`, `strip.jpg`): the name on its strip beside the Home screen, and Home, Swap and Backups on taped prints.
  On the light hero the dark home screen is the print behind the front one; on the dark hero the light home screen is.

Left out on purpose: badges and logo walls.

## 4. Tokens

| Token | Value | Use |
|---|---|---|
| Board | `#F1EBDD` | ground of every picture |
| Paper | `#FFFEFB` | prints |
| Ink | `#2D2D2D` | text |
| Soft ink | `#5A5A5A` | secondary text |
| Thread | `#7A7590` | hairlines |
| Title strip / fold | `#D6FACA` / `#ECFDE6` | the printed name strip and its folded tab |
| Text on tape or the strip (`tx`) | `#2D2D2D` | text that sits on tape |
| Tape | `#FFCDCC #FFE3CD #FFFF9A #D4FAC7 #BFEFE1 #AED6F9 #E4CCFE #FFCCFA` | washi, in four patterns (stripes, dots, grid, chevron) |

Paper night (the dark theme), from `pal_night` in `source/gfx/paper.c`:

| Token | Value | Use |
|---|---|---|
| Board | `#2A2546` | ground of every dark picture, with a few stars |
| Paper | `#29283B` | prints and cards |
| Ink / soft ink | `#EFEAF2` / `#BDB6C8` | text |
| Thread | `#8E86A0` | hairlines |
| Ink blue | `#B4BCF0` | stamps and step marks |
| Title strip / fold | `#4E5390` / `#6B72B0` | the name strip: deep, so its light text reads |
| Text on tape or the strip (`tx`) | `#F6F1FA` | text that sits on tape |
| Tape | `#C38BA5 #E1B581 #E2D7B7 #84A894 #719FA4 #8394C1 #9F96CD #AD85B3` | washi from the night scene's colours; tape that carries text is mixed halfway toward `#12102A` |

## 5. The pictures

| File | Where it is used |
|---|---|
| `hero.jpg` | the top of the README (1280 x 640, so it also fits the repository's social preview) |
| `nav-*.png` | the four jump links under the intro |
| `features.jpg` | the four cards under the nav |
| `div-*.png` | the dividers, one above each section |
| `diagram-sd-card.jpg`, `diagram-swap.jpg` | Install and Using it |
| `strip.jpg`, `keys.jpg` | Using it |
| `dark/*` | the same pictures in Paper night; the README picks them with `<picture>` |
| `shots/*.jpg`, `shots/dark/*.jpg` | the screenshots (960 x 540) and the four profile pictures (80 x 80) the others are made from, per theme |

To redraw: `wsl bash docs/readme/shots.sh <folder>`, then `python docs/readme/build.py --import <folder>` (add `--theme light` or
`--theme dark` to draw one theme; without `--import` the existing screenshots are used). `shots.sh` and `roll.sh` need gcc;
`build.py` needs Python 3 with Pillow and NumPy, and Chrome.
Re-take the screenshots whenever the app's look changes.
