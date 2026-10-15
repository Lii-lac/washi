// After an update the new version is on trial until its home screen has been up a while. A start that finds two starts before it never got that far puts the
// version kept from before back (the one that would not start is kept in its place) and leaves a note for it to say so. Plain files next to the app (self =
// its NRO's path): <self>.bak the kept version, <self>.trial the starts so far that did not get going, <self>.rolledback the note.
#ifndef TRIAL_H
#define TRIAL_H

#include <stddef.h>

// first thing at a start: 1 = the kept version is back in place (this start should end now: the next one is it), 0 = go on
int trial_start(const char *self);
// install_update put a new version in place: it is on trial from its first start
void trial_begin(const char *self);
// the running version got going: its trial (if any) is over
void trial_ok(const char *self);
// the kept version and the running one swap places (the running one is kept, to come back to the same way); its trial is over. 0 = done
int trial_swap(const char *self);
// 1 if a version is kept, with its version in ver ("" when it can't be read)
int trial_kept(const char *self, char *ver, size_t cap);
// 1 once after a start went back to this version, with the version that would not start in ver ("" if unknown); the note is gone after
int trial_note(const char *self, char *ver, size_t cap);
// an NRO's version, from its NACP (display version); 0 = read
int nro_version(const char *path, char *ver, size_t cap);

#endif
