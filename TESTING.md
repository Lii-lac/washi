# Testing Washi on the console

The swap logic and the whole interface are tested on the PC (see [building](docs/building.md#run-the-pc-tests): thousands of checks, including cutting the power
at every step of a swap, plus every screen driven by scripted button presses). What the PC **can't** test is the console itself:
the real save filesystems, the system keyboard, battery, clock, theme and game-running behaviour, and how the screen feels. This
checklist covers those. Work through it in order, and **stop at the first thing that surprises you**.

## 0. Before anything

1. **Back up every save with JKSV.** All of them, on the club account. Washi verifies every backup before it replaces
   anything, but this is the one thing that can't be too safe.
2. Copy `washi.nro` to `sdmc:/switch/washi.nro` (the app keeps its data in `sdmc:/switch/washi/`). If you
   installed the forwarder NSP, launch it from the Home menu too: it should open straight into the app, in full application mode.
3. Start it from hbmenu. Two ways to launch, and it's worth trying **both**:
   - hold **R** while launching a game (full application mode), and
   - open the Homebrew Menu from the **Album** (applet mode).

   The app should work in both. If it only works in one, note which.

## 1. First run

- [ ] The "Back up every save with JKSV first" screen appears. It does **not** appear on the second launch.
- [ ] If an account called "Esports Switch" exists it's offered; otherwise you pick from a list.
- [ ] You're asked for a 6-digit admin password, twice. The keypad works with the D-pad and A, and submits the moment the sixth digit is in.
- [ ] Home shows **CURRENTLY LOADED: Nobody** (the console wasn't cleared yet, so it isn't a "Guest" until the first sign-out).

## 1b. Look and feel

- [ ] The top right shows the console's real **time** (in your timezone), battery % and the account name. (Screenshots of the
      design showed 2:14 PM; that was a placeholder.)
- [ ] Every page is cream paper on a pinboard, whatever the console's theme (the dark look comes later, with its own mockup). There is
      no cyan anywhere. The top bar (console name, battery, clock) is on every page.
- [ ] Admin → Appearance: pick a 12 / 24-hour clock. The choice sticks after restart. (The theme choices change nothing yet.)
- [ ] Moving the focus feels smooth on every page: the slip, tile or key you leave sticks back down while the next one peels up, with a
      small tap and a peel sound (see 10b4). The password pad and the on-screen keyboard do the same with their keys; lists scroll smoothly.
      Hold a direction to check it keeps up. If it stutters, note which screen.
- [ ] Dialogs rise into place and fade in over the dimmed screen; when they close, the screen fades back. Screens (lists, the
      picture editor, progress) fade in. Nothing cuts.
- [ ] Pressing toward nothing (Left on the first tile, Right on New profile, Down on the last row, Up on the loaded profile's
      card, Up/Down at the ends of a list, the hub, a choice dialog, the keypad and the keyboard; any direction in a Yes/No dialog)
      makes the focused item lean that way and spring back: the item itself moves with its frame, nothing else does, and the
      focus stays where it was. It doesn't hold up the next press.
- [ ] Spacing: home, lists, the hub and dialogs feel open, not cramped; nothing is cut off ("10h ago" on a tile, long names).
- [ ] Games show their real icons on a profile's page (a letter tile appears for a game without one).
- [ ] A profile named with kanji / Korean / accented letters displays (the console's own font is used for what the built-in one lacks).
- [ ] Touch works: tap tiles, rows and the footer's buttons; drag lists, the wheels and the Backups row.

## 1c. Profile pictures

This list needs the console's pictures to be readable ([profile pictures](docs/profile-pictures.md)). Washi's log (a file for every start in `switch/washi/logs/`) says on each
start which it was; if it says letters, note the error code there. Without them it shows letters:

- [ ] Without the pictures: a new profile is made right after the name (and the optional NetID and password), with a letter on a random colour. **Edit profile → Picture and background → Picture** offers only the plain letter, and **Background** works as below.

With the console's pictures:

- [ ] The log says `profile pictures: the console's own, N pictures` (N is 147 or more), from the Homebrew Menu **and** from the Home-menu forwarder (the access may differ).
- [ ] The picture grid is in the console's own order (Mario's first, then Zelda, Animal Crossing, Splatoon, ARMS, Pikmin, Star Fox, Metroid, Kirby) and a new character from a recent system update is there.
- [ ] A picture chosen in Washi shows the same picture on the console's own account page (the icon id written to the account is the picture's number plus one).
- [ ] Start-up is no slower than before, and scrolling the grid stays smooth.

- [ ] Making a new profile asks name, an optional NetID, an optional password, then **Edit Icon** with a random picture (one nobody else has) on a random pastel-ish background (the lighter half of a colour: orange, never brown). OK makes the profile; **B cancels it** and nothing is made.
- [ ] Edit Icon looks like the console's: a big square preview, **Picture** and **Background** rows with their current choice, and **OK**.
- [ ] **Picture** opens a strip of cards with a sheet of pictures under it. The first card is **Console**: the plain letter first, then the console's pictures grouped by series (Mario, then all of Zelda together, Animal Crossing, Splatoon, ARMS, Kirby, ...), and "N pictures". The preview follows the cursor; **X** goes back to the plain letter; **A** keeps it, **B** goes back unchanged. The footer reads only A OK, B Back, X No picture, L R Card.
- [ ] With no Extra icons on the card, a dashed outline where the next card would be says "More pictures" and "Admin → This app → Extra icons", and there is no order chip.
- [ ] Moving past the bottom row of the sheet **glides** it up (an eased scroll, not a jump); the pictures are sharp.
- [ ] **Background** opens the console's 72 colours (6 across, 12 down). Compare a few with System Settings → Users → your icon → Background: they should match exactly.
- [ ] On the home screen **+** opens Edit profile for the **loaded** profile (it is already signed in, so no password); with nobody loaded, for the profile in focus (see 1f).
- [ ] Pictures show on the home grid, the loaded banner, the confirm and password dialogs, and Admin → Profiles.
- [ ] Opening the Picture page doesn't feel slow, and scrolling the sheet stays smooth: a picture that isn't ready yet is a pale square of the profile's colour for a moment, and the pictures fill in on their own while you move (they are made on a thread of their own).

## 1c-2. Extra icons (Admin → This app → Extra icons)

- [ ] The page opens (the console is online): a slip says "N of M games on your card" with a piece of tape for every game under it, and a row for every game with its icon count, its size and "On the card" or "Get". The footer's A says **Get** on a game that is missing icons and **Update** on one that is all there. "Get" on one game shows the bar and "n of N", and finishes with "Got N icons". Note how long a game of about 70 icons takes (several icons come at once now, over connections that stay open: it should be clearly quicker than one at a time).
- [ ] **R3** opens the order menu (Name, Recently downloaded, Most icons, On your card first); the rows re-sort and the tape pieces follow.
- [ ] **B** during a download stops it, says how many icons are on the card, and the page carries on from there next time. Switching Wi-Fi off in the middle says the connection dropped, and nothing half-written is left (`*.tmp`).
- [ ] **X** (Get all) says how many icons and how big before it starts. **Y** (Remove) asks, then the game's folder is gone.
- [ ] The first open takes a few seconds to read GitHub's list (about 2 MB). Offline, or with Airplane Mode on, it says so instead of waiting for ever.
- [ ] Edit Icon → Picture: after the **Console** card come the games on the card, one card each (three cards show in full), with the sheet of that game's characters below. **L**/**R** slide one card (the same spring as the Backups cards: firm, landing within a quarter of a second), **ZL**/**ZR** five, the D-pad moves in the sheet, **R3** (or the chip, with two or more games) changes the order of the games while Console stays first. The icon the profile uses has a tick. There is no **Y** tab any more, and the Background grid has no cards.
- [ ] In the picture grid and the sheet, left or up on the first picture goes to the last, right or down on the last goes to the first, and holding the button keeps going round.
- [ ] Pick one: it shows on the home screen, in the banner and in the dialogs. Close and open Washi: it is still there. With Console profile on, the console's account shows the picture too.
- [ ] Remove that game, then open Washi: the profile shows its first letter; download the game again and the picture is back.
- [ ] Scrolling a game's sheet and moving between games stays smooth, also on a game of 300 or more icons (they all show, rows are drawn as they come into view), and the home screen does not get slower with hundreds of icons on the card.

## 1d. The console's own profile (Admin → Console profile)

Leave both switches off until the first check passes. Best tried on the offline emuMMC.

- [ ] **Check that it works** says "It works" and shows the account's current name. If it says "Not available", stop here: the console didn't allow it, and nothing was changed.
- [ ] Turn on **Show the player's name** (it asks first). Load a profile: System Settings → Users now shows that name, and so do games.
- [ ] **Sign out** (nobody loaded) puts the account's own name back and the **guest picture**: Princess Toadette on light blue, the club account's original. Admin → Console profile → **Guest picture** changes it. Admin → Console profile → **Put the original back** also works at any time.
- [ ] Turn on **Show the player's picture**: the account's picture becomes the profile's picture on its background colour (same colour as the console's editor would give). Renaming a loaded profile or changing its picture updates the console too.
- [ ] `switch/washi/system-profile/` holds `original.json` and `original.jpg` (the name and picture from before the first change).
- [ ] **Check it works** shows the account's current icon and background ids (`icon 12, background 5`). Compare with System Settings → Users → your icon: tell me which character and background colour it shows there, and the ids.
- [ ] With the picture on, Washi also writes an icon id (the picture's number + 1) and a background id (the colour's number) beside the picture. Check the Home menu corner icon after **Update it now**, and System Settings → Users → Edit Icon shows a sensible character and colour. **Put the original back** restores the original ids.
- [ ] The Home menu's own icon in the corner may keep the old name/picture until you open a game or the profile page (it keeps its own copy and nothing here can make it re-read). Note whether it ever updates on its own, and how soon.
- [ ] The account still works normally afterwards (log in with it, open a game, open System Settings → Users).

## 1e. Game names

- [ ] Every game on the progress screen, in Admin → Shared games and in the profile lists shows its real name. (Games from firmware 21 on keep their names compressed, and used to show as rubbish.)

## 1f. Touch screen

- [ ] Tap a profile tile: the "Play as ..." box opens. Tap **Switch** or **Cancel** (the two halves of the bottom of the box).
- [ ] The grid starts **most recently played first**; the chip at its top right (or R3) slides a sleeve of stickers out from under itself, one sticker for every other order (Recently played, Name, Newest, Most games; the one in use is the chip's own sticker), and the choice is still there after restarting. B, R3 again or any tap closes it (a tap picks nothing). A new order shuffles the tiles into place (10b7). The A-Z rail only shows in name order.
- [ ] **Search** (Y or the bar) for part of a name: only the matches stay in the grid, the best first ("adr", "ria", "ian", "arn" and "aia" all find Adrian). The bar shows the search and "N of M"; B, or the x at the bar's end, brings everyone back. Loading someone found this way also brings everyone back. Nothing matching: only New profile, and a note.
- [ ] **+ Edit profile** (signed in) opens a page with Picture and background, Name and NetID; a NetID that another profile has is refused; the page never shows the NetID itself.
- [ ] **+** with nobody loaded edits the profile that is selected on Home: with a password it asks for it first (a wrong one stays on Home, B cancels), and when you leave the editor nobody is loaded (the banner still says Guest).
- [ ] Admin → This app → **Activity log**: this start's log opens at once when it is the only one. After a few starts it lists them newest first, by the console's own time ("Today, 2:14 PM"), and A opens one. A log file older than 14 days is gone after the next start.
- [ ] Open the app with someone loaded for 3+ hours (set the console clock forward, or load and wait): it asks "Is this still ...?"; "Yes" is not asked again for another 3 hours, "Sign out" signs them out.
- [ ] About and the admin hub show the version (1.1.0) and hbmenu shows the same version.
- [ ] **Menus wrap**: a single press of Up at the top of a menu or list goes round to the bottom, and Down at the bottom goes round to the top (admin hub sections and rows, lists, Storage, Backups, choice boxes, Edit Icon rows). Holding the D-pad stops at the end instead (with the little bump), it does not spin round. Keypads, the keyboard and the order sleeve do not wrap (the sleeve's sticker gives a little push at its ends); the picture and colour grids do (1c-2).
- [ ] The home screen: a **search bar** sits between the loaded profile and the grid (tap it, or press Y); the footer reads only - About, + Edit profile (X is shown on the Sign out button, Y on the search bar, R3 on the chip); with a search, - Clear search comes first (About is then the logo's tap), and the bar's end shows the - button instead of Y. B does nothing on Home.
- [ ] While nobody is loaded the banner shows **Guest** with Miku's picture, "Miku" and "nothing played right now is saved".
- [ ] Tap **Sign out** on the loaded banner; tap the prompts along the bottom of any screen: each does what its button does. Tap the logo top-left: About.
- [ ] About looks right (icon, text, the three steps) and the QR code opens the club's Discord when scanned from a phone.
- [ ] Drag the home grid up and down: the cards follow the finger smoothly (not a row at a time), slide under the search bar's row and the footer, and let go they run on and settle on a whole row; the focused card lies down while the grid moves and peels up again where it settles. Pulled past the top or the end they give a little and spring back. A finger on the moving grid stops it.
- [ ] Drag a list (Admin pages), a text page (Activity log) up and down: they scroll. Tap a row of a list: it opens.
- [ ] Tap the keys of the password pad and of the on-screen keyboard.
- [ ] Edit Icon: tap Picture / Background / OK; in the grids tap a cell, and drag to scroll (it settles on a whole row).
- [ ] Tapping a letter on the A-Z strip jumps to the first profile with that letter.
- [ ] **B** on the password pad and the keyboard takes the last digit / letter off, and only closes the box when it is empty.

## 2. The keyboard (applet mode)

Press **+** to make a profile.

- [ ] In full application mode the system keyboard appears.
- [ ] In applet mode (opened from the Album) **either** the system keyboard appears **or** the built-in letter grid does (A types, X deletes, Y shift, + done). Both are fine; a blank screen or a freeze is not.
- [ ] If the system keyboard never works for you, put `"builtin_keyboard": 1` in `config.json` to skip it.

## 3. Passwords

- [ ] Make a profile **with** a 6-digit password and one **without**.
- [ ] A wrong password says how many tries are left. After 5 wrong tries it makes you wait.
- [ ] A profile with a password shows a small padlock on its tile.

## 4. The swap (use a game nobody cares about)

1. Make profiles **A** and **B**. Play the throwaway game as nobody; note your progress.
2. Choose **A** (this backs up the console's saves into `unclaimed/` the first time; that's expected). Play: make a visible change.
3. Choose **B**. The game should be **fresh** (B has no saves yet). Play: make a different change.
4. Choose **A** again. **A's progress from step 2 must be intact.** Then choose **B**: B's change must be there.
5. Press **X (Sign out)** as B. The progress screen backs up, then says "Clearing the console", and Home shows **Guest**. Launch the game: it is **fresh**, with none of B's save (it is safe in B's profile; choose B again to get it back).
- [ ] 2-5 all behave as described.
- [ ] The progress screen names the person and shows one line per game; it finishes with "You're playing as ...".
- [ ] `switch/washi/profiles/<name>/history/` gains a folder when a profile's saves change, and holds at most 5.

## 5. Play without the app

1. As B, press **X (Sign out)**. Launch the game (it is fresh) and make a visible change **without opening Washi**: this is a temporary guest session.
2. Open Washi and choose **A**.
- [ ] A's saves load correctly.
- [ ] The guest's change is simply replaced (it is **not** kept anywhere), no `unclaimed` folder appears, and **B's profile did not change**.
- [ ] Only the very first load on a console that already has saves keeps them, in `unclaimed/` (step 4.2). After a sign-out, never again.
3. Sign out again, play something as a guest, then open Washi and make a **new profile**.
- [ ] At the end it asks **"Keep what you just played?"** (with how many games). **Keep it**: a short backup, then "You're playing as ..."; the game still has the guest's progress, and the new profile is loaded. Sign out and load it again: the progress comes back.
- [ ] **Start fresh** instead: the profile is made but not loaded, and the guest's play is thrown away when someone loads, as before. A guest who played nothing is not asked.

## 5b. Each profile only has its own games

Use two throwaway games, G1 and G2, and profiles A and B.
1. As A, play G1 only (launch G2 and quit without saving is fine too). Press **X**.
2. As B, play G2 only. Press **X**.
- [ ] A's page in Admin → Profiles lists **1 game** (G1), B's lists **1 game** (G2): neither has the other's.
- [ ] As A, launch G2: it is fresh (B's progress isn't there). As B, launch G1: fresh too.
- [ ] `profiles/<A>/current/` holds only G1's folder; `profiles/<B>/current/` only G2's.

## 6. Interrupted swap (throwaway save only!)

1. Start a swap and **hold the power button to force off** while the progress screen is running.
2. Boot, reopen Washi.
- [ ] It says "The last swap stopped". **A Finish now** completes it and says "All set".
- [ ] Afterwards the right person is loaded and nothing is missing. Check the log (Admin → This app → Activity log).
- [ ] Try it a few times at different moments (early = backing up, late = loading).

## 7. A big save

- [ ] Swap with a large save (Smash Ultimate) loaded. It should complete without a "commit" or "journal" error. If it fails,
      the log says which game and step; the journal handling follows JKSV's (`writeLimit = journal - 1 MB`).

## 8. A game whose save doesn't exist yet

- [ ] Give profile B a backup of a game the console account has never played (copy its folder into `profiles/<id>/current/<TITLEID>/` from another console's JKSV backup). Choosing B creates the save and loads it.
- [ ] A backup for a game that is **not installed** is skipped with a warning, and stays in the profile.

## 9. A game is running

- [ ] Launch a game, press Home, open Washi (applet mode), try to swap. It must **refuse** with "Close the game (Home, then X)" and change nothing. Close the game and it works.

## 10. Battery and space

- [ ] Below 15% and unplugged, a swap is refused with a clear message. Plugged in, it works.

## 10b. Home screen by D-pad

- [ ] There is clear space between the top line (logo, battery, clock) and the loaded profile's card; the grid's last row still clears the footer line, also when a tile there is picked.
- [ ] The loaded profile's card, the search bar ("Search N profiles") and the order chip beside it have room inside: the guest's picture is well clear of its card's edges.
- [ ] From the first row of the grid, Up goes to the search bar (or to the chip, from the last tiles), then the loaded profile's card (from the chip: its Sign out button). Up again: nothing. Left and Right move between the search bar and the chip, and between the card and Sign out. Down goes back the same way to the grid.
- [ ] A on the chip drops down its menu; on the search bar the keyboard; on the card the loaded profile's page; on Sign out it signs out. A search that finds someone puts the frame on their tile.
- [ ] With nobody loaded (Guest), A or a tap on the card says to pick your name.

## 10b2. Washi Home (the new look of the home screen)

Home has been used on the console with every build since 2026-10-12, but this list has not been gone through from start to end. Every screen is in the new look now.

1. Open the app with one or more profiles and nobody loaded, then with someone loaded. Look at the whole screen once.
   - [ ] A cream board with white paper cards held by washi tape: the top bar (logo card, mint "Washi" strip, status card), the profile card, the search bar, the order chip, the tiles, the A-Z rail and a paper footer strip. (With the console in its dark theme it is Paper night instead: section 10b6.)
2. Walk the grid with the D-pad, one press at a time.
   - [ ] The tile in focus lifts off the board: its tape peels, it gets a deeper shadow, a pale ghost is left where it was, and it does one short wiggle. The tile you leave settles back and sticks down. There is no blue frame.
   - [ ] Nothing on the screen stutters or flickers except the two tiles involved.
3. Hold the D-pad in one direction until it stops, then hold the other way.
   - [ ] The focus keeps up with your hold (no lag, no queue of moves after you let go).
   - [ ] After the first move the repeats are the light version: no big wiggle.
4. Press toward nothing: Left on the first tile, Right on New profile, Down on the last row, Up on the profile card.
   - [ ] The item in focus leans that way and springs back. Nothing else moves, and the focus stays where it was.
5. Go Up from the first row: search bar or order chip, then the profile card, then Sign out beside it (Right on the card). Come back Down.
   - [ ] Each of those lifts like a tile (the card and search bar lift less, the chip and Sign out button a little more).
6. Press A on the search bar, then type a letter. Press B to clear it.
   - [ ] The grid narrows to matches and the focus goes to the first one; B brings everyone back.
7. Press R3 (or A on the chip) for the order menu, pick another order, and look at the grid.
   - [ ] The tiles re-sort (the menu itself: section 10b7).
8. With many profiles (more than two rows), hold Down.
   - [ ] The grid scrolls a row at a time and the lift follows. The A-Z rail follows the letters, and tapping a letter or one of its arrows on the touch screen jumps there.
9. Tap tiles, the search bar, the chip and Sign out on the touch screen.
   - [ ] Each does what A does on it.
10. Look at the loaded person's tile.
    - [ ] A small blue LOADED stamp sits on it, a little crooked and in a slightly different place each time someone is loaded (see 10b4).
11. Check the speed. Create an empty file called `debug-timing` in `/switch/washi/` on the SD card, then open the app and walk the grid for a few seconds.
    1. A small line appears at the top: `draw A/B ms   present C/D ms   N frames` (average/worst for the last 120 frames).
    2. Write down the numbers while you walk the grid, while you hold the D-pad, and while the grid scrolls. The aim is an average draw plus present of well under 16 ms (60 frames a second); the PC cannot predict this.
    3. Delete the `debug-timing` file when you are done.

## 10b3. The rest of the Washi screens (renamed app, dialogs, keypad, lists, hub, switching)

Nothing here has been run on the console. Washi used to be called Save Locker, so first:

1. Put `washi.nro` in `/switch/` and delete `save-locker.nro`. Delete the old Save Locker forwarder (System Settings → Data Management → Manage Software) and install the Washi one from `/NSZs/`.
2. Open Washi once. Your profiles and backups should all still be there: the app renames `/switch/save-locker/` to `/switch/washi/` and `Save Locker Backups` to `Washi Backups` on first start.
3. Restart the console once, so the reminder is the renamed one. It shows nothing until Washi has been opened once.

**Dialogs and keypad**
1. Tap a profile with no password: a taped paper card opens with Cancel and Switch, and Switch is lifted. Press Left or Down: it leans and springs back. Press B: it closes. Open it again and tap Switch with a finger: it switches.
2. Open the admin password pad: key 5 is lifted and the six rings are empty. Type two digits: two rings fill with ink blue. Move around with the D-pad, then hold a direction: the lift follows each key. Press Up on the top row: the key leans.
3. Press B: a digit is removed; B on an empty pad leaves. Enter six digits: the sixth fills the last ring and submits. Enter a wrong password: a red note shows under the rings.
4. Admin → Deleted profiles, then `ZR`+`X`: the red button is taped shut and Cancel is lifted. Press `A` before 3 seconds: nothing happens. After 3 seconds the button goes solid red, lifts and the tape peels. Press `A` right away (within 0.4 s): ignored; press `A` again: it deletes. `B` cancels at any time.
5. Create a profile: the keyboard has paper keys and one lifted key. Type a name: Shift lowercases after the first letter, `Y` toggles shift, `X`, `B` and `+` still work. Tap keys with a finger. Press Up on the top row and Down on the bottom row: keys lean.

**Lists and the order menu**
6. Admin → People → Profiles: each row is a paper slip on a taped sheet, and the title slip clears the logo and top bar. Press Down once: the old slip sticks back and the new one peels up and lifts. Hold Down: the slips keep moving, lighter, with no wiggle.
7. Press Down past the fifth row: the list jumps to the next page at once (it no longer glides) and the slips settle one after another, rising a few pixels into place, the wave running toward the slip in focus; an arrow shows under (and later over) the sheet. Hold Down: it steps a row at a time with no wave. Press Up on the first row: the slip leans. Drag a finger: it scrolls. Tap a slip: it opens. Tell me whether the wave feels right or busy.
8. Open Deleted profiles, Shared games (toggle one), Unclaimed saves and the Activity log: same look, no cyan. Open a profile page: the header picture has a crayon ring.
9. On Home press `R3`: the order sleeve slides out from under the chip (section 10b7). Down and Up: the next sticker comes out. `A` picks. `B`, `R3` or a tap closes it.

**Admin hub**
10. Open the admin hub: the "Admin" tag clears the logo, and there is no cyan anywhere. Press Down and Up on the tabs: the white tab slides 12 px right, the old one tints and slides back.
11. Press `A` or Right: the first slip lifts with peeling tape, a ghost and a small wiggle. Down and Up on the slips, and hold the D-pad: the lift follows with no wiggle on repeats. Left or `B` returns to the tabs.
12. Open Games and saves (five slips): the last slip and the description sit clear of the footer, and the sheet has a margin below its last slip (as wide as at its sides), so a lifted last slip and its shadow stay on the paper. Tap each tab, then a slip. `B` from a page returns with that slip still lifted.

**Switching**
13. Do a swap between two profiles that have games. There is ONE bar of tape in a crayon box just bigger than it. A roll runs along it laying a piece of tape for each game: a different colour and pattern each, each a little crooked (turning the other way from its neighbour), end to end with the joints just touching. A game's piece is as wide as its share of the bytes (a tiny save still gets a small piece). Each game's icon has a scribble in its piece colour (a game with no icon shows a coloured square with its initials).
14. When a game finishes, its roll is used up: it shrinks and fades where the tape ended, and the next game's roll drops in over the joint with a little squash. Nothing peels away; the finished bar is the whole swap and ends at the right-hand end of the box. Even a tiny save is seen being laid: the roll never lays tape faster than about 260 px a second and a piece takes at least 0.2 s, so a whole bar takes two seconds or more, and the two figures under it (the percent and "x of y") follow the roll; a big copy is slower than that anyway and is not held back. A game with nothing to copy (or one that is not installed) gets a piece straight away and says "Nothing to copy". The bar starts empty again for the next step (backing up, then loading). The roll taps as it turns, crinkles where it catches, and each piece is torn off with a soft pat (see 10b4).
15. Sign out: the finished-games list under the bar shows the last two. Back up a game with a very big save: the bar moves steadily and the copy is not noticeably slower than before. Do a swap with many games: the effects stay short; every piece still gets its moment, so tell me how long the whole swap takes and whether it drags.

## 10b4. The last Washi pages, the stamp, the crayon icons and the sounds

Nothing here has been run on the console, and I cannot hear the sounds: the PC only checks that each one is asked for at the right moment.
The console's speaker plays almost nothing under about 200 Hz, so the low pats are only really heard on headphones. Sounds are always on (the
console's volume is the control).

**The stamp**
1. Load someone from Home (pick a name, Switch). When the swap finishes and Home comes back: a big LOADED stamp comes down on the card at the top (from large and crooked into place), then a small one on that person's own tile, with a thud. The small one is a little different each time: some land half off the tile.
   - [ ] Ink that misses the tile stays on the board, under every screen (open Admin and look), until you close Washi. Ink on a tile goes when they are signed out.
2. Sign out, then load someone else: the new person is stamped the same way; the old tile is clean.
3. Close Washi and open it again with someone still loaded: the stamps are already there (no animation).

**The pages**
4. Edit profile → Picture and background: a sheet with two slips (Picture, Background) and a mint OK button; a taped print of the picture on the right with the small round one under it. A on Picture: a sheet of pictures. The one in focus has a dashed blue line round it and its corner curled; the print on the right is pinned a little crooked each time you move. Scroll with the D-pad and a finger: the dashed line at the right edge has a tape thumb that follows. Background shows the colours the same way. X on pictures: "No picture". B backs out.
5. About (Minus on Home): a taped sheet with the mark, three tape labels for the steps, the QR code on a taped print, and the Discord link underlined in crayon. The code is blocks coloured in with wax crayon (violet, hatch strokes in two directions, ragged edges, pinholes where the paper shows through) and fills most of its print. Scan it with a phone, from a few feet away too: it opens the club's Discord (`discord.gg/NUEsports`). Small and grey at the bottom right: the GitHub mark, "Lii-lac", then the version (1.0.0).
6. Admin → Games and saves → Storage: a ruled card for the free space with a bar of tape strips, one per person in their colour; two slips below; on the right a sheet of slips with each person's picture, size and a strip of tape as long as their share. The strips are laid by hand: each a little crooked, cut with the zigzag shears, square or at a slant, over or short of its neighbour by a pixel or two; the people whose share is too small to show are one lavender strip, not a row of slivers. Left and Right move between the cards; A opens.
7. Console → Sign-out reminder: two columns of slips fanned like a deck. The focused column's middle slip is lifted with its tape peeling; the other lies flat. Up and Down turn the focused column and each value that passes the middle gives a tap; flick by touch.
8. Admin → Games and saves → Backups: the tiles and rows are tilted paper with tape; the focused one is lifted over a ghost patch; "AUTOMATIC" is a paper label.

**The icons**
9. Back up or swap games: each game's icon on the switching card is repainted in heavy wax crayon (the first time Washi meets that game since it opened; later swaps with the same games reuse the picture). Write down whether the first swap is held up noticeably: the filter runs once per game and takes a fraction of a second on the console.

**Sounds**
10. Walk the Home grid: every step gives a tap and a peel; hold the D-pad: every step gives only a light tap. Press Left on the first tile: two low taps (a refusal). A on a tile: a small tap, then a pin sound as the dialog opens.
11. A swap: the roll taps as it turns (quietly, never more than about ten a second), a crinkle where it catches, a tear and a pat when a piece is cut, and "done" at the end; the page changes with a swish. Load someone: the stamp thud. The swap's sounds were made about 40% softer after the first console test ("a little too punchy"): tell me if they are still too much, or now too faint.
12. The corner mark and the "Washi" strip (every page): a soft, tight shadow under the little card, pink tape with a faint shadow, pale stripes on the strip and a flat light fold at its right end, as in the mockup.
13. Tell me which sounds are too loud, too quiet, too busy or missing, and whether any click or crackle.

## 10b5. Lightly pressed tape, the "still working" scrap, the press fix and the new forwarder

Nothing here has been run on the console. The PC checks the pictures, the pixels around a press and that the scrap puts the screen back exactly; it cannot tell how it feels or how fast the console draws it.

**The forwarder**
1. Delete the old Washi forwarder (System Settings → Data Management → Manage Software → Washi → Delete Software; it is title `01b4dcffdf365000`, version 1.0.6 or older), then install `/NSZs/Washi v1.0.7 by Esports Club [0114013574378000].nsp`. Its Home-menu icon is a navy seal pressed on cream washi with a person cut out of it. It must start the app.

**Pressing a button**
2. Press `A` (and tap with a finger) on Home tiles, dialog buttons, keypad keys, keyboard keys, list slips (the Profiles list), hub rows and Backups rows: the item pushes flat for a moment and comes back. Nothing around it may bend, shift or flicker (the warping in the video is the thing that was fixed). Look hardest at the Profiles list, where the rows above and below the one pressed, their tape, the card behind and the board must not move at all; a wide row slides its words in by 3 px at most.

**Tape**
3. Look at every page that has tape on it: the Home tiles and the tapes over the top card, the order menu, dialogs, the admin hub tabs and sheets, the lists, the Backups page, the picture pickers, the keypad and keyboard, the swap card. The tape is see-through, it lies lightly on the card (a little haze, a corner tenting up, a few soft creases) instead of wrapping hard round the edge, and it throws only a faint shadow. Tell me if any tape still looks hard, painted on, or too faint to see.
4. Backups: each tile has a short strip of tape across each of its top corners, slanting opposite ways, in different colours; each row has one across its left corner. The focused one lifts and its tape peels.
4b. Put on by hand: on the Backups tiles and rows, the admin hub rows and sheets, and the Storage rows, no two strips lie alike: each is a little off its corner, a little crooked, longer or shorter, with its ends cut differently; about a third have a pressed-down ridge, and a few have an end that stands up slightly. On the "Backing up..." bars, a bar of one or two games (a long strip) lies a little more crooked and usually has a ridge with the far half slightly out of line (many-game bars stay plain pieces). Tell me if it is too much or still too tidy, and on which screen.

**"Still working"**
5. Open something slow (Admin → Games and saves → Backups with a lot of backups, Storage, or a list on a nearly full SD card). If the wait lasts about a third of a second, a crayon starts drawing small symbols on the paper at the left of the footer strip, one after another (a star, a smiley, hearts, a kaomoji), until the bar is full or the work is done. The screen under it must not change.
6. The moment the work is done the rubbing out begins, and it does not hold anything up: the next screen fades in at once, and the rubber scribbles the symbols away over it (about half a second, missing bits and going back for them) while you can already press buttons, move the focus or touch. Press something during the rubbing out: the screen must answer straight away and the last of the ink keeps going.
7. Screens that open at once never show the symbols.
8. Tell me: does it come at all on the slow screens; does it move smoothly or in jumps (it can only move between pieces of work, so a long single job freezes it); does the rubbing out over the next screen slow that screen down (Home scrolling, the Profiles list) or cover something it should not; is anything left on the screen after it ends (in the 2026-10-08 captures a piece of the old scrap stayed at the left of the footer on the Profile screens: that must never happen again).

## 10b6. Paper night (the dark theme)

Nothing in this section has been run on the console yet.

1. Set the console to its dark theme (System Settings → Themes → Basic Black), then open Washi.
   - [ ] Home is Paper night: a cut-paper night scene (moon, stars, hills, a house with a lit window, pines), night-blue cards, pale writing.
   - [ ] Note how long the first screen takes to come up compared with the cream one (the night board is drawn once per theme change).
2. Admin → Appearance → Theme: it reads Follow the console. Press A: Paper (cream), again: Paper night, again: Follow the console.
   - [ ] Each change redraws the page in the new look at once, and the hub and Home are in that look when you go back.
3. Walk through every page in Paper night: dialogs, keypad, keyboard, lists, the hub, a swap, Edit Icon and its grids, Storage, the reminder wheels, Backups, About, Check for updates.
   - [ ] All writing reads well; nothing is cream where it should be night paper; the tape stays pastel; the About QR code still scans with a phone.
4. With Theme on Follow the console, switch the console back to its light theme while Washi is open (HOME, System Settings, then back to Washi).
   - [ ] Washi is cream paper again without restarting.

## 10b7. The order sleeve, the shuffles, the picture strip and the A-Z rail (the 2026-10-12 builds)

Nothing in this section has been run on the console yet. Do it in Paper and once in Paper night.

**The order sleeve** (every order chip: Home, Admin → This app → Extra icons, and the Picture page with two or more games)
1. On Home press `R3`.
   - [ ] Before you press it: the chip's sticker is printed a little soft (its colours toned down, its writing still dark), and it looks the same with the focus on the chip or elsewhere, on every page. The stickers in the sleeve are printed the same way, so nothing changes colour when one lands on the chip. In the dark theme they are toned much less (the same wash muddied them on the dark paper): clear and bright, but not louder than the page.
   - [ ] A glassine sleeve slides out from under the chip. The stickers for the other orders sit inside it, milky through the paper, and one sticks out of its mouth with its corner lifted a little off its backing.
2. Press Down and Up.
   - [ ] The sticker that was out slides back in and the next comes out, with a soft slip sound. At the first and the last sticker a press only gives it a little push; it does not wrap round.
   - [ ] Left and Right give the sticker that is out a little tug and push.
3. Press `A`.
   - [ ] In one motion: the peel runs on from the lifted corner, the sticker comes free, is carried up to the chip with its left end low, and is rolled on from left to right. Nothing snaps flat, flickers or jumps between those steps.
   - [ ] The moment the sticker is free, its backing is let go: it drifts a little to the left and floats down like a sheet of paper, rocking from side to side, and fades out in about half a second. Pick a few orders in a row (and again after reopening the app): no two fall alike (how far it is flicked, how fast it falls, how wide it rocks and how far it tips, which way it rocks first). It doesn't spin or go back into the sleeve. The sleeve's flap folds shut and the sleeve goes back under the chip.
   - [ ] Tell me whether the hover peel and the pick feel right, too slow, or too busy, and whether the sounds fit.
4. Open it again and close it with `B`, then with `R3`, then with a tap anywhere.
   - [ ] The sticker that was out tucks back in, the flap folds shut over the mouth, and the sleeve is pushed back under the chip, all in about a third of a second. Nothing changes. Tell me if this put-away is better now.
5. Do 1-4 on the Extra icons page (four orders) and on the Picture page.
   - [ ] Same sleeve and stickers; each order's sticker has its own little badge (a clock, an alphabet block, a burst, a stack of cards, an arrow into a tray, a grid, an SD card).

**The shuffles** (any new order, and a search)
6. On Home with more profiles than fit (three rows or more), change the order a few times, between Name and Recently played.
   - [ ] The grid doesn't fade: the tiles are picked up where they were and carried to their new places, one after another, each set down turned a little as its new place turns it. Tiles that were not on the screen rise in from just under their place; tiles that leave the screen sink and fade. It takes about half a second.
   - [ ] With Name order the A-Z rail slides in from the right edge; leaving Name order, it slides back out.
   - [ ] Search for a few letters (Y), then clear the search with B: the matches shuffle into their places the same way, and back.
   - [ ] Every shuffle plays a soft card riffle (Home, Extra icons, the Picture strip). On Home the tiles move smoothly, with at most one short catch as it starts.
7. On Admin → This app → Extra icons, change the order (R3).
   - [ ] The rows slide to their new places one after another, each lifting a little and bowing out to one side so they pass each other; a row from out of view comes in from the window's edge.
8. On the Picture page with two or more games, change the order.
   - [ ] The game cards are carried to their new places, each hopping up over the cards it passes; the Console card stays first.

**The picture strip** (Edit profile → Picture and background → Picture)
9. With some Extra icons games on the card, open it. Then remove them all (Admin → This app → Extra icons, Y) and open it again.
   - [ ] First the Console card and its sheet; the games follow as cards. With none, the dashed "More pictures" outline is where the next card would be (1c).
   - [ ] Scroll the sheet so the focused picture is half out of its window: the dashed cut line is cut off at the window's edge like the picture.
10. Hold the D-pad over a big sheet, and step through the games with L and R quickly.
   - [ ] No stutter: a picture not ready yet shows as a pale square of the profile's colour for a moment and fills in on its own.

**Home**
11. With profiles under many letters, set the order to Name. Press `R` a few times, then tap a letter far away on the A-Z rail.
   - [ ] The ink disc runs through the letters to the new one and the rail's window slides with it (quick for one letter, a little longer for a far one, never more than about a third of a second); it no longer jumps.
12. Look at the footers on Home, the Picture page and the Extra icons page.
   - [ ] They only list what isn't shown on the page itself (no X Sign out, Y Search or R3 in them).
12b. Open Extra icons with Wi-Fi on.
    - [ ] "Asking GitHub which games have icons" has dots that come one by one and go, while it waits; the text doesn't move. If it takes more than a moment, the crayon starts drawing on the footer's left as well, both keep moving, and the crayon is rubbed out as the next page (or a message) comes. The order chip's card is as close round the R3 mark on the left as round the sticker on the right.
13. In Paper night, open About.
    - [ ] The tape on the three steps' labels is a deep night colour with pale writing that reads well.
14. Start Washi from its Home-menu icon (the forwarder) and press `B` on Home.
    - [ ] Nothing happens (no sound, no dialog, Washi stays). Press HOME and close the software: it closes like a game, with no error message.
15. Search for someone (Y), then press `-`.
    - [ ] The search is cleared and everyone shows again; the bar's end showed the `-` button while the search was on.
16. Edit Icon -> Background: scroll the colour grid so the focused swatch is half under the sheet's top or bottom edge.
    - [ ] Its dashed outline is cut off at the edge like the swatches (the same as the pictures on the Picture page); same on the Picture grid.
    - [ ] A swatch wholly in the top or bottom row showing keeps its whole outline (not cut).
17. The colour grid and the Picture page's pictures: press Left on a row's first cell, Right on its last, Up in the top row, Down in the bottom row (also holding).
    - [ ] Each goes round to the other end of the same row or column (Down into a short last row lands on its last cell).
18. Home with many profiles: press R (then L) to step through the letters, and Down past the second row.
    - [ ] The cards glide to the new rows (no jump); the focused card lies down on the way and peels up on the new one.
    - [ ] With the focus on the search bar, the chip or the loaded card, L / R still work: the focus goes to the cards, on the next (or last) letter.
19. Enter a password (load someone who has one).
    - [ ] A typed digit's circle is the same crayon ring as an empty one, scribbled in under it with visible crayon strokes, nearly full; every digit is scribbled a little differently (and again after Delete and retyping).

## 10b8. Pinned profiles (the 2026-10-14 build; nothing here has been run on the console)
1. On Home, put the focus on a card, hold ZL + ZR and press A; enter the admin password (if there is one).
    - [ ] The grid shuffles and that card goes to the front. Then its taped strip comes off (up and away to the left, fading) while the card presses down, a push pin in the strip's colour comes down out of the air and goes in with a soft sound, and the card takes the focus: the pin rises ~14 px out of it with a creak.
    - [ ] The toast says the profile is pinned. The card now lies a little straighter than it did, with a faint dent round the pin.
2. Move the focus off the pinned card and back (D-pad).
    - [ ] Off: the pin goes home quickly with a dull seat sound. Back: the card lifts and wiggles (smaller than a taped card's, turning on its pin) and the pin rises again. Held D-pad: no rise sound, the pin still moves.
3. Pin a second profile.
    - [ ] It goes after the first pinned one, in every order (name, recently played, ...). A search ignores pins. Both cards keep their pins after leaving Home and coming back, and after closing and starting Washi.
4. On a pinned card, hold ZL + ZR, press A, enter the password again.
    - [ ] The pin is pulled up out of the card and away (fading) with a sound, a new strip comes down out of the air with its end curled and is pressed flat (a small stick), and the card takes the focus with the tape peeling. The grid shuffles it back to its place first.
5. Paper night (Basic Black): do steps 1 and 4 again.
    - [ ] The pin is a muted version of the strip's night colour; its shadow and the dent read on the dark card. (The night pin colours have not been shown to the user before this build.)
6. Without the admin password, or with the password dialog cancelled (B): nothing changes.

## 10c. The in-game reminder (needs Ultrahand; see finished/INSTALL.txt)

- [ ] After installing the new reminder and **restarting the console**: Admin → Console → **Test the in-game reminder**. Within a few seconds it says "Test reminder sent" and the real note pops up: the loaded person's name (or "Washi" with nobody loaded), "Don't forget to sign out when you're done playing!", with a white sign-out arrow (an open door frame, the arrow leaving it), for about 6 seconds. If it says "The reminder isn't running", the sysmodule didn't start; "Ultrahand isn't set up" means its notifications folder is missing.

- [ ] **Repeat every 30 seconds** (a switch): Admin → Console → Test the in-game reminder → move down to **Repeat every 30 seconds** and press `A` (or tap it): the paper knob slides right, a strip of mint dotted tape sticks down in the dashed guide, the knob gets a check and the word says ON; the list stays open. Press `A` again: the tape goes, the knob slides back and says OFF. Close the list with it ON: the Test row says "notes every 30 s". Close Washi and start a game: a note titled "Washi test" comes over it within a few seconds, then every 30 seconds, and the game keeps running and keeps the controls. It also works with nobody loaded. Open Washi again, open the test list: the switch is still ON (it reads the card). Switch it OFF and check that no more notes come for a minute. `/switch/washi/reminder.log` has a "repeating test: note written" line for each.
- [ ] The real one in a minute: Admin → Console → Sign-out reminder → hours 0, minutes **01** → OK (the row says "after 1 min"). Load a profile (swap), close Washi, start a game: a note titled with their name comes about a minute after the swap and then again every minute (up to half a minute late: it looks every 30 seconds) for as long as they stay loaded; sign out and it stops. The long way, if you want it: set the reminder to 0:15, load a profile, start a game. After 15 minutes the same toast, then every 15 minutes. A 3-hour setting reminds at 3 hours and then every hour.
- [ ] Sign out (or set the reminder off): no more toasts. "Is this still you?" → "Yes, still me" starts the count again.
- [ ] Without Ultrahand (or with its notifications off) nothing happens and nothing piles up in /config/ultrahand/notifications.
- [ ] The console boots normally with it installed; deleting atmosphere/contents/4200000000534C4B and restarting removes it.

## 11. Admin

- [ ] The admin page is a **hub**: sections on the left (People, Games and saves, Console, This app), their rows on the right. Up/Down picks a section and the right side follows; A or Right goes into the rows, B or Left comes back; B on the sections leaves. After a page closes you're back on the row you opened. Tap a section, tap a row, tap Back: all work.
- [ ] The hub's icons and text read well in **both themes** (Admin → Appearance → Theme until it says Paper night, then back): the icon tiles are visible on the dark cards, and nothing is too faint.
- [ ] Moving between sections (People → Games and saves and back, quickly too): the white tab slides right and the old one sticks back; nothing jumps.
- [ ] Rename, change password, remove password, change **avatar color**, delete (the folder moves to `profiles/.trash/`, it isn't erased).
- [ ] **Deleted profiles**: the profile just deleted is listed first ("deleted just now · N games"). A → Restore brings it back: it is on the home screen again, loads with its saves, and its earlier saves are in Roll back. Give its NetID to someone else before restoring: it comes back without a NetID and says so. X (acts when you let go of it) → **Delete for good** asks first (Cancel keeps it); confirmed, the entry leaves the list and its folder is gone from `profiles/.trash` on the SD card. Hold X, press ZR as well and let go of both (nothing happens until you let go; more than one deleted profile) = **Delete all**: a caution box with Cancel focused and a red "Delete all" button that stays dim for 3 seconds (no bar fills, the wait is just implied), then goes solid and the frame hops onto it; A does nothing until then (try pressing it early), then it deletes every deleted profile; B cancels at any time. With nothing deleted the page says "Nothing has been deleted."
- [ ] **Storage**: the free space and card size match System Settings → Data Management (within rounding). The bar under "Saves" is all the save data, one part per profile in that profile's colour (two people with the same background get two shades of it), deleted profiles' saves in grey at the end. The profiles that use most are listed on the right with bars of the same colours. The page opens in a second or two even with many profiles (it reads the whole folder).
- [ ] Storage: A on a profile opens its page (rename, NetID, password, ...); B comes back to Storage with the frame on that row. Rename someone there: their colour shifts a little. Delete someone there: Storage adds up again and they are gone from the list.
- [ ] Storage: with more than five profiles the last row is "N others". A opens Everyone's saves (the card grows into the page), largest first, with the frame on the first of the others; A opens a profile, B goes back, and B again shrinks the page back into the card with the frame on "N others". Left and Right move between the profiles and the two actions.
- [ ] Storage → **Backups kept per profile** → 2; play and swap a few times: each profile keeps 2 earlier saves. **Delete older backups now** says what it frees, asks first, and afterwards Roll back lists only the newest 2 for everyone; nobody's current saves change.
- [ ] **Unclaimed saves** (Games and saves): the row says how many there are ("2 to sort out", or "none"). Each one shows when it was kept, its games and its size, newest first. A → **Give to a profile** → pick one (the loaded profile can't be picked: "Sign out first") → Give: it says "They're <name>'s now", the entry leaves the list, and loading that profile shows those saves. That profile's Roll back has what they had before, and their games that weren't in the unclaimed ones are still there. A → **Delete** asks first (Cancel leaves it), then it is gone. With none left the page says "Nothing unclaimed."
- [ ] **Backups** (Games and saves, last row): the page title sits just above the cards; "Back up now" is the first tile and your own backups are starred tiles beside it, all one row (three show in full, the next peeks in at the screen edge with a round > button, and a count says "1 – 2 of 7 pinned"). With more than three the whole row slides, Back up now included: D-pad Left/Right, L and R step one tile, **ZL and ZR skip two** (not in the footer). The D-pad lands firmly; L, R, ZL and ZR swing a little past and bounce back once. **Y** jumps to Back up now from anywhere; **X** on a backup asks "Delete this backup?" (Cancel keeps it) and deletes it, X on Back up now does nothing, then an "Automatic" heading with a row for each automatic one ("After <name> signed out", Autosave tag). Each shows the pictures of the people in it. D-pad moves between them, the page scrolls, and a press toward nothing leans. Back up now → Back up: progress, then "Backed up" with the folder name; on the SD card there is `Washi Backups/mm-dd-yyyy--hh-mm/` with `profiles/`, `config.json` and the rest. The ones made by hand are listed first. Sign someone out: the first sign-out of the day makes an automatic backup (a second sign-out the same day makes none, and nothing slows it down); after five days only the newest 4 automatic ones are there (manual ones stay). A backup → **Restore** with someone loaded says "Sign out first"; signed out, it asks, shows progress, and afterwards profiles and settings are as they were then (make a profile first to see it go) and a new backup of how it was before is in the list. **Delete** (or X) asks first. With no room on the SD card it says so and leaves no folder.
- [ ] **How the Backups row moves** (v1.6.0, needs four or more pinned backups): the tile in focus is the one that is lifted (a ghost patch where it lay, its tape peeling), it goes with the row, never between two (v1.6.1), and it is brought to the middle slot (at either end of the row it sits in the outer slot); when the focus moves, the lift passes to the next tile as it does on the home grid. One press: the row is off at once and stops firmly on the next tile, with no creeping in and no bounce. Hold the D-pad: it runs at one even speed, no stutter between steps, and stops on a tile. Press the other way mid-slide: it turns round at once. Does it keep up (no dropped frames)?
- [ ] **The Backups row by touch**: drag the tiles sideways: they stay under the finger. Let go slowly: the row settles on the nearest tile. **Flick** it: it runs on, slowing down, and stops with a tile exactly in the middle, the frame on it (a harder flick goes further; a short quick flick still moves one tile); A and X then act on that tile. Pull it past its first or last tile: it gives a little and springs back. Put a finger on it while it runs: it stops there, and nothing opens. Tap a tile that shows in full: its box opens (B: the row is as you left it). Tap the tile peeking in at the edge, or a round < > button: it comes to the middle. Drag the page up and down: the rows follow the finger the same way.
- [ ] The reminder's wheels still follow a finger that moves very slowly (1.6.0 changed how a slow finger is noticed, for every screen).
- [ ] Console → **Sign-out reminder** opens two wheels (hours, minutes in 15s). Up/Down turns the one in the frame and it rolls smoothly (hold it: it keeps turning); Left/Right moves the frame; they go round (12 → 0, 45 → 00). B leaves it as it was.
- [ ] By touch: drag a wheel and it follows the finger; **flick** it and it spins on, slows down and stops exactly on a value; touch it while it spins and it stops there. Tapping a value above or below the band brings it to the middle.
- [ ] Set **1:00**: load a profile, wait over an hour (or change the clock), reopen the app: it asks "is this still you?". Set 0:00, or press Y (Turn off): the row says "off" and it never asks. Set 1:30: the row says "after 1 hour 30 min".
- [ ] The "Washi" name at the top is as tall as the cartridge next to it, in both themes.
- [ ] Roll back: pick an earlier version, then load that profile: the older saves appear. The newer ones are still in its history.
- [ ] Shared games: mark a game as shared; it stops being swapped (everyone keeps one save).
- [ ] Shared games also lists the **device saves** (games with one save for the whole console) as "always shared"; they can't be swapped per person.
- [ ] The admin page only opens with the chord ZL + L + D-pad up + Plus (Minus opens About, Plus edits the loaded profile; pressing Plus while ZL is held must not open the editor).
- [ ] Rename / **NetID** (admin only): set one (abc1234, any case); a wrong one is refused; Remove NetID clears it. Home → Y search finds that profile by its NetID, and the NetID is never shown anywhere else.

## 10b9. Read-only profiles

- [ ] Admin -> Profiles -> a profile -> **Read-only** asks first, then the switch is on and the page says "read-only". It is refused while that profile is loaded.
- [ ] Load a read-only profile: the message says play isn't kept. Play a game, change something, sign out (asks first, the toast says nothing was kept), load it again: the change is gone.
- [ ] Switching from a read-only profile to another says what was played isn't kept; the other profile is loaded as usual.
- [ ] Roll back and "Give to a profile" on a read-only profile say it is read-only. A new profile made by a guest can still keep guest play.
- [ ] On a read-only profile, Rename / NetID / password / Picture and background (admin page) and Plus -> Edit profile are dimmed and say it is read-only; nothing opens.
- [ ] Loading a profile with a big save (Odyssey, Smash, BotW) works: the save is committed in pieces with the file closed (a commit with a file open fails on the console). If it fails, the message ends with the console's error code.
- [ ] Switch **Read-only** off: the next sign-out backs up again.

## Before you open this to the club

- **Cloud saves:** if the Esports Switch account has Nintendo Switch Online save backup on, turn off *Automatic Save-Data Download*
  (System Settings → Data Management → Save Data Cloud), or the console may offer to pull a cloud copy over a swapped-in save.
- **Online + custom firmware:** running homebrew on the normal system (sysMMC) and going online risks a ban on a console that's
  the club's. The usual protection is CFW on an **emuMMC** that stays offline (90DNS/Incognito), booting the clean system for
  online play. Saves on the emuMMC are separate from the sysMMC's, so decide this **before** the club starts building saves.
