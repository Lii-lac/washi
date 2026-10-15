<!-- Thanks for helping with Washi! Short, plain answers are best. Delete any section that does not apply. -->

## What and why
<!-- Why is this change needed? One or two sentences: the diff already shows what changed. -->

Closes #
<!-- The issue this closes, e.g. Closes #123. Delete this line if there isn't one. -->

## How I tested it
<!-- What did you run, and on which machine? What did you not test? "PC build only, no console yet" is a fine answer. -->

## Screenshots
<!-- Only if something you can see changed. Before and after, side by side, if you can. Use made-up profiles.
     Please keep save files, passwords, NetIDs and other personal data out of screenshots. Delete this section if nothing visible changed. -->
<!-- The Switch's profile pictures are Nintendo's artwork: if you have added them to your build (docs/profile-pictures.md), take screenshots without them. -->

## Risk and compatibility
<!-- Does this change what is stored on the SD card (profiles, backups, the swap journal, settings) or how saves are swapped? If so, how does an existing install keep working?
     Does it bring in a library, font or other asset from elsewhere? Name it and note its licence (the README lists the current ones). -->

## Checklist
<!-- Tick only what you have actually done. For a docs-only change most of these won't apply: delete the lines that don't. -->
- [ ] The app builds with no warnings, and its last line says `built ... washi.nro` (see `docs/building.md`). If `sysmodule/` changed, build the reminder as well.
- [ ] In `test/` (WSL on Windows), `make -f Makefile.host` and `make -f Makefile.app` both end with `0 failed`.
- [ ] Swap logic, backups or sign-out changed: the power-cut tests pass. They are part of `test_locker`, run by `make -f Makefile.host`.
- [ ] The look changed: I checked the screenshots from `make -f Makefile.app` (`test/out/app/`), and in the dark theme as well (`DARK=1`, see `docs/building.md`).
- [ ] The matching sections of `TESTING.md` were run on a real console, or "How I tested it" says which are still to do.
- [ ] My diff has only the changes I meant, and no whole-file diff from line endings (see `docs/building.md`, "Platform traps").

## Notes for the reviewer
<!-- Optional. Where should the reviewer start? What are you unsure about? What did you leave for later? -->
