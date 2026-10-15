// The Washi app: screens and flows (first run, home, choosing a profile, admin, recovery). Platform-independent: it needs a
// Platform (buttons/screen/clock) and a Backend (the console's saves), so it runs on the Switch and in the PC test harness.
#ifndef APP_H
#define APP_H

#include "backend.h"
#include "platform.h"

// Runs until the user leaves (or the system asks the app to exit). `root` is the data folder on the SD card.
// Returns 0, or non-zero if it couldn't start.
int app_run(Platform *plat, Backend *be, const char *root);
// The app was called Save Locker once and kept its data in <sd>/switch/save-locker and "Save Locker Backups": if `root` (<sd>/switch/washi)
// does not exist yet but the old folder does, it is renamed, and likewise the backups folder. Nothing is merged or overwritten.
// app_run does this first; it is public for the tests.
void app_adopt_old_names(const char *root);
// Shows an error that stops the app from starting (the usual screens need a working backend).
void app_fatal(Platform *plat, const char *headline, const char *detail);

#endif
