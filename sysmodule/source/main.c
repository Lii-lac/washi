// Washi's sign-out reminder: starts with the console (Atmosphere, atmosphere/contents/4200000000534C4B), checks every half
// minute, and while someone's saves are loaded has Ultrahand show a note titled with their name, "Don't forget to sign out when you're
// done playing!" (with a sign-out arrow), over whatever is running. It only reads Washi's files and writes the note; it changes nothing else. See reminder.h.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "notify_icon.h"
#include "reminder.h"

#define INNER_HEAP_SIZE 0x30000 // two small JSON files at a time
#define WASHI_ROOT "sdmc:/switch/washi"
#define ULTRAHAND "sdmc:/config/ultrahand"
#define TICK_NS (5ull * 1000000000ull) // a test asked for by the app is answered within this
#define CHECK_EVERY_TICKS 6             // the reminder itself: every half minute

u32 __nx_applet_type = AppletType_None; // a sysmodule has no applet
void __libnx_init_time(void);           // (libnx sets up time() with it; its headers don't declare it)
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

// The clock Washi stamps "loaded at" with (time(), the user system clock). It is asked for once at start-up; if the time service is not up yet that early in the
// boot, the reminder still starts (the first version of this program gave up there, and never ran) and asks again until it is: without the clock it cannot tell
// when anything is due, but the test notes do not need it.
static int g_time_ok;

static void time_try(void)
{
    if (g_time_ok) return;
    if (R_SUCCEEDED(timeInitialize())) {
        __libnx_init_time();
        g_time_ok = 1;
    }
}

void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    time_try();
    smExit();
}

void __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    if (g_time_ok) timeExit();
}
// A few lines of what it did, for finding out why a note did not come (a console has no other way to say): <root>/reminder.log, a line with the
// time (UTC, seconds since 1970) and what happened. Only events and the first minute of start-up are written, and the file starts again past 8 KB.
#define LOG_PATH WASHI_ROOT "/reminder.log"
#define LOG_MAX 8192
#define LOG_TICKS 12 // start-up ticks that report what the reminder can see

static void rlog(const char *fmt, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    FILE *f = fopen(LOG_PATH, "ab");
    if (!f) return;
    fprintf(f, "%lld %s\n", (long long)time(NULL), line);
    fclose(f);
}

static int seen_path(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    RmMemory seen = {0};
    RmRepeat repeat = {0};
    const RmIcon icon = {NOTIFY_ICON, sizeof NOTIFY_ICON};
    struct stat lsb;
    if (stat(LOG_PATH, &lsb) == 0 && lsb.st_size > LOG_MAX) remove(LOG_PATH);
    rlog("started, clock %s", g_time_ok ? "ready" : "NOT ready yet");
    for (unsigned tick = 0;; tick++) {
        if (!g_time_ok) {
            time_try();
            if (g_time_ok) rlog("the clock is ready now");
        }
        int64_t now = g_time_ok ? (int64_t)time(NULL) : 0;
        if (tick < LOG_TICKS)
            rlog("tick %u: washi folder %s, state.json %s, test request %s, ultrahand notifications folder %s", tick, seen_path(WASHI_ROOT) ? "yes" : "NO",
                 seen_path(WASHI_ROOT "/state.json") ? "yes" : "NO", seen_path(WASHI_ROOT "/" RM_TEST_ASK) ? "yes" : "no", seen_path(ULTRAHAND "/notifications") ? "yes" : "NO");
        if (rm_test_answer(WASHI_ROOT, ULTRAHAND, now, &icon)) rlog("a test was asked for and answered");
        int64_t rnow = now > 0 ? now : (int64_t)tick * 5 + 1; // (the repeating test only needs a clock that runs, not a right one)
        if (rm_repeat_due(WASHI_ROOT, &repeat, rnow)) rlog("repeating test: note %s", rm_notify(ULTRAHAND, RM_REPEAT_TITLE, RM_REPEAT_TEXT, rnow, &icon) == 0 ? "written" : "NOT written");
        if (tick % CHECK_EVERY_TICKS == 0) {
            RmState st;
            int rd = rm_read(WASHI_ROOT, &st);
            if (rd != 0 && tick == 0) rlog("state is not readable (nobody set up, or damaged)");
            if (rd == 0 && rm_due(&st, &seen, now)) rlog("a reminder was due for %s: %s", st.name, rm_notify(ULTRAHAND, st.name, RM_TEXT, now, &icon) == 0 ? "note written" : "note NOT written");
        }
        svcSleepThread(TICK_NS);
    }
    return 0;
}
