// The sign-out reminder that shows over games: a small background program (a sysmodule) that reads what Washi keeps on the
// SD card and, while someone's saves are loaded, drops a note for Ultrahand (the overlay menu) to show as a toast. This part is
// plain C with no console calls, so the PC tests can run it (test/test_reminder.c).
#ifndef REMINDER_H
#define REMINDER_H

#include <stddef.h>
#include <stdint.h>

#define RM_EVERY_S 3600 // after the first one, a reminder at least every hour (more often when the setting is shorter than an hour: see rm_every)

typedef struct {
    char holder[64];    // whose saves are loaded: a profile id, or "unclaimed" (nobody)
    int64_t loaded_at;  // when they were loaded (seconds, the console's clock); "is this still you?" starts it again
    int idle;           // no swap or sign-out is half done
    int remind_minutes; // Admin -> Console -> Sign-out reminder (0 = off)
    char name[64];      // their name, from profiles/<holder>/profile.json ("" if it can't be read): the note's title
} RmState;

// what has been shown for the current session: the session is told apart by when it was loaded, and the count starts again when
// the reminder's setting changes (it was counted from the old one)
typedef struct {
    int64_t session;
    int sent;
    int minutes;
} RmMemory;

// Reads <root>/state.json, <root>/config.json and the loaded profile's name. 0 = read; -1 = there is no state (Washi not set
// up) or it is damaged.
int rm_read(const char *root, RmState *out);

// what the reminder says (plain apostrophe: it is in every font, whatever Ultrahand draws with)
#define RM_TEXT "Don't forget to sign out when you're done playing!"
#define RM_TITLE "Washi" // the title when the name can't be read

// How long after one reminder the next comes while the same person stays loaded: as often as the setting itself, but never less often than every
// RM_EVERY_S (an hour). A setting of 1 minute repeats every minute, 15 every quarter hour, 45 every 45 minutes, 3 hours every hour after the first.
int64_t rm_every(int remind_minutes);

// Whether a reminder is due at `now`; if so notes it in `m`. The first comes remind_minutes after the saves were loaded, then one
// every rm_every(); reminders missed (the console was asleep) are not caught up, only the latest is shown.
int rm_due(const RmState *s, RmMemory *m, int64_t now);

// The repeating test (Admin -> Console -> Test the in-game reminder -> "Repeat every 30 seconds"): while the file <root>/RM_REPEAT_FLAG exists, a note
// comes every RM_REPEAT_EVERY_S seconds, whoever is loaded and whatever the reminder's setting, until the app takes the file away. It is for starting a game
// and watching for the note over it.
#define RM_REPEAT_FLAG "reminder-repeat"
#define RM_REPEAT_EVERY_S 30
#define RM_REPEAT_TITLE "Washi test"
#define RM_REPEAT_TEXT "In-game reminder test: every 30 seconds until you turn it off in Washi."
typedef struct {
    int64_t last; // when the last one came (0: none yet since it was turned on)
} RmRepeat;

// 1 when the repeating test is on and a note is due at `now` (the first comes at once; then one every RM_REPEAT_EVERY_S seconds, give or take the few seconds
// between two looks); notes it in `m`. Off (the file is not there): 0, and the count is forgotten, so the next time it is turned on the first comes at once.
int rm_repeat_due(const char *root, RmRepeat *m, int64_t now);

// The picture Ultrahand shows beside our notes (50x50 RGBA8888, sysmodule/source/notify_icon.h): it looks for
// <ultrahand>/assets/notifications/washi.rgba, named after our .notify files.
typedef struct {
    const unsigned char *rgba;
    size_t n;
} RmIcon;

// Writes the note into <ultrahand>/notifications as a .notify file, replacing one of ours that wasn't shown yet, and puts the icon
// in place if it isn't there yet. The title is the loaded person's name (RM_TITLE when it is NULL or ""). Returns -1 (and writes
// nothing) when the folder isn't there: Ultrahand isn't installed.
int rm_notify(const char *ultrahand, const char *title, const char *msg, int64_t now, const RmIcon *icon);

// A test the app can ask for (Admin -> Console -> Test the in-game reminder): it writes <root>/RM_TEST_ASK; the reminder shows the
// real note at once (titled with the loaded person's name, as the real one is) and answers in <root>/RM_TEST_DONE: "ok", or
// "no-ultrahand" when Ultrahand's folder isn't there.
#define RM_TEST_ASK "reminder-test"
#define RM_TEST_DONE "reminder-test.done"
// 1 if a test was asked for (and answered), else 0
int rm_test_answer(const char *root, const char *ultrahand, int64_t now, const RmIcon *icon);

#endif
