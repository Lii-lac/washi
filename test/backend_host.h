// A pretend console for the host tests. Each "save filesystem" is a folder.
//
// It models the parts of the real thing that matter for not losing saves:
//  - a save only changes for real when it is COMMITTED; unmounting or a crash discards uncommitted changes
//  - a save can only take so many uncommitted bytes (its journal size)
//  - a running game makes mounting its save fail
//  - any call can be made to fail as if the power was cut right there (everything afterwards fails too)
#ifndef BACKEND_HOST_H
#define BACKEND_HOST_H

#include <stdint.h>

#include "../source/backend.h"

#define FAKE_MAX_TITLES 32

typedef struct {
    uint64_t id;
    int installed;
    uint64_t journal;
    int running;
    char name[64];       // shown instead of "Game xxxx" when set
} FakeTitle;

typedef struct {
    Backend be; // must stay first
    char base[300];       // temp folder holding everything below
    char sd[330];         // the store root (stands in for sdmc:/switch/washi)
    char committed[330];  // <TID>/... = what is really in each account save
    char live[330];       // working copy of the mounted save
    char root[400];       // be.save_root while mounted
    uint64_t mounted;
    int is_mounted;
    uint64_t uncommitted;

    FakeTitle titles[FAKE_MAX_TITLES];
    int ntitles;
    uint64_t device_saves[4];
    int ndevice;

    int battery_pct, charging;
    uint64_t free_bytes, total_bytes; // the SD card: what is free, and its size

    BeAccount accounts[4];
    int naccounts;

    long ops;          // counted calls so far
    long fail_at;      // 0 = never; otherwise the counted call number that "loses power"
    int crashed;
    char crash_name[64];
    int corrupt_next_write;   // silently write only half of the next chunk
    long writes;

    const uint8_t *icon_data;   // optional JPEG returned for every installed game
    size_t icon_size;

    // the account's system profile (name + picture), for the tests of the name/picture sync
    int sys_allowed;            // 0 = the console refuses to open the profile editor
    int sys_fail_store;         // 1 = the next store fails; 2 = every store fails
    char sys_nick[40];
    uint8_t sys_ud[0x80];       // the profile's user data (icon id, background colour id)
    uint8_t *sys_img;           // JPEG bytes
    size_t sys_img_size;
    int sys_stores;             // how many times the profile was written
    time_t clock;
    int clock_step;     // seconds the clock advances per reading (default 1; 0 = time stands still)
    uint32_t rng;
} FakeBe;

void fake_init(FakeBe *f);
void fake_destroy(FakeBe *f);
void fake_add_title(FakeBe *f, uint64_t id, int installed, uint64_t journal);
void fake_name_title(FakeBe *f, uint64_t id, const char *name);
FakeTitle *fake_title(FakeBe *f, uint64_t id);
void fake_save_path(FakeBe *f, uint64_t title, char *out, size_t n);       // committed folder of a save
int fake_put(FakeBe *f, uint64_t title, const char *rel, const char *text); // write a file into a save (committed)
int fake_put_n(FakeBe *f, uint64_t title, const char *rel, size_t bytes, const char *seed); // big deterministic file
void fake_arm(FakeBe *f, long nth_call); // lose power at the nth counted call from now (0 disarms)
void fake_reboot(FakeBe *f);             // the power is back: forget everything that wasn't committed

#endif
