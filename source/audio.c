// Sounds for the Switch (libnx audout). A thread keeps four short buffers queued and mixes whatever sounds are playing into the next one,
// so sounds overlap and nothing ever waits for the sound; the interface only says which sound (SND_*), how loud and how late.
// The sounds are small 48 kHz mono recordings and synthesis (made by tools/sfx.py) built into the
// program from data/*.wav, and the intro's few seconds of music (data/intro_music.wav, the intro mockup's notes) and its stamp chord alone (data/intro_chord.wav).
// The console's speaker plays nothing under about 200 Hz, so the low pats are only really heard on headphones.
#include "audio.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "crinkle_wav.h"
#include "done_wav.h"
#include "intro_chord_wav.h"
#include "intro_music_wav.h"
#include "peel_wav.h"
#include "platform.h"
#include "pin_home_wav.h"
#include "pin_in_wav.h"
#include "pin_out_wav.h"
#include "pin_up_wav.h"
#include "pop0_wav.h"
#include "pop1_wav.h"
#include "riffle_wav.h" // (a list shuffled into a new order)
#include "refuse_wav.h"
#include "slip0_wav.h"
#include "slip1_wav.h"
#include "slip2_wav.h"
#include "slip3_wav.h"
#include "stamp_wav.h"
#include "stick_wav.h"
#include "swish_wav.h"
#include "tear_wav.h"
#include "tick_wav.h"

#define AFRAMES 1024 // 21 ms at 48 kHz; 1024 stereo 16-bit frames are exactly one 0x1000 page
#define ABUFS 4
#define NVOICES 16
#define MASTER 0.7f

typedef struct {
    const int16_t *pcm;
    int n, pos; // pos < 0: not started yet (a delayed sound)
    float gain;
} Voice;
typedef struct {
    const int16_t *pcm;
    int n;
    float gain; // how loud this sound is next to the others
} Wav;

static Wav g_wav[SND_COUNT];
static Voice g_voice[NVOICES];
static Mutex g_mx;
static AudioOutBuffer g_buf[ABUFS];
static int16_t *g_mem;
static Thread g_thr;
static volatile int g_stop, g_ok;

// the PCM of a 16-bit mono RIFF file: its data chunk
static void load(int snd, const unsigned char *p, unsigned size, float gain)
{
    Wav w = {NULL, 0, gain};
    if (size >= 12 && !memcmp(p, "RIFF", 4)) {
        for (unsigned off = 12; off + 8 <= size;) {
            unsigned n = p[off + 4] | (p[off + 5] << 8) | (p[off + 6] << 16) | ((unsigned)p[off + 7] << 24);
            if (!memcmp(p + off, "data", 4)) {
                if (off + 8 + n > size) n = size - off - 8;
                w.pcm = (const int16_t *)(p + off + 8);
                w.n = (int)(n / 2);
                break;
            }
            off += 8 + n + (n & 1);
        }
    }
    g_wav[snd] = w;
}

static void fill(int i)
{
    int16_t *out = g_mem + (size_t)i * AFRAMES * 2;
    int32_t acc[AFRAMES];
    memset(acc, 0, sizeof acc);
    mutexLock(&g_mx);
    for (int v = 0; v < NVOICES; v++) {
        Voice *x = &g_voice[v];
        if (!x->pcm) continue;
        for (int k = 0; k < AFRAMES; k++) {
            if (x->pos < 0) { // a delayed one: silence until its time comes
                x->pos++;
                continue;
            }
            if (x->pos >= x->n) break;
            acc[k] += (int32_t)(x->pcm[x->pos++] * x->gain);
        }
        if (x->pos >= x->n) x->pcm = NULL;
    }
    mutexUnlock(&g_mx);
    for (int k = 0; k < AFRAMES; k++) {
        int32_t s = acc[k] > 32767 ? 32767 : acc[k] < -32768 ? -32768 : acc[k];
        out[2 * k] = out[2 * k + 1] = (int16_t)s;
    }
    g_buf[i].next = NULL;
    g_buf[i].buffer = out;
    g_buf[i].buffer_size = AFRAMES * 4;
    g_buf[i].data_size = AFRAMES * 4;
    g_buf[i].data_offset = 0;
    audoutAppendAudioOutBuffer(&g_buf[i]);
}

static void audio_thread(void *arg)
{
    (void)arg;
    for (int i = 0; i < ABUFS; i++) fill(i);
    int ahead = 0;
    while (!g_stop) {
        AudioOutBuffer *rel = NULL;
        u32 cnt = 0;
        Result rc = audoutWaitPlayFinish(&rel, &cnt, 100000000ull);
        if (R_FAILED(rc) || cnt == 0) continue;
        for (u32 j = 0; j < cnt; j++) {
            fill(ahead);
            ahead = (ahead + 1) % ABUFS;
        }
    }
}

void audio_init(void)
{
    load(SND_PEEL, peel_wav, peel_wav_size, 0.9f);
    load(SND_STICK, stick_wav, stick_wav_size, 0.9f);
    load(SND_STAMP, stamp_wav, stamp_wav_size, 0.85f);
    load(SND_DONE, done_wav, done_wav_size, 0.75f);
    load(SND_SWISH, swish_wav, swish_wav_size, 0.8f);
    load(SND_REFUSE, refuse_wav, refuse_wav_size, 0.85f);
    load(SND_TICK, tick_wav, tick_wav_size, 0.6f);
    load(SND_SLIP0, slip0_wav, slip0_wav_size, 0.6f);
    load(SND_SLIP1, slip1_wav, slip1_wav_size, 0.6f);
    load(SND_SLIP2, slip2_wav, slip2_wav_size, 0.6f);
    load(SND_SLIP3, slip3_wav, slip3_wav_size, 0.6f);
    load(SND_TEAR, tear_wav, tear_wav_size, 0.8f);
    load(SND_CRINKLE, crinkle_wav, crinkle_wav_size, 0.55f);
    load(SND_POP0, pop0_wav, pop0_wav_size, 0.8f);
    load(SND_POP1, pop1_wav, pop1_wav_size, 0.8f);
    load(SND_RIFFLE, riffle_wav, riffle_wav_size, 0.7f);
    load(SND_PIN_IN, pin_in_wav, pin_in_wav_size, 0.9f);
    load(SND_PIN_UP, pin_up_wav, pin_up_wav_size, 0.9f);
    load(SND_PIN_HOME, pin_home_wav, pin_home_wav_size, 0.9f);
    load(SND_PIN_OUT, pin_out_wav, pin_out_wav_size, 0.9f);
    load(SND_INTRO, intro_music_wav, intro_music_wav_size, 0.85f);
    load(SND_CHORD, intro_chord_wav, intro_chord_wav_size, 0.85f); // (as loud as it is in the music)
    mutexInit(&g_mx);
    if (R_FAILED(audoutInitialize())) return; // no sound is not worth stopping for
    if (R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return;
    }
    g_mem = memalign(0x1000, (size_t)ABUFS * AFRAMES * 4);
    if (!g_mem) {
        audoutStopAudioOut();
        audoutExit();
        return;
    }
    memset(g_mem, 0, (size_t)ABUFS * AFRAMES * 4);
    if (R_FAILED(threadCreate(&g_thr, audio_thread, NULL, NULL, 0x10000, 0x2B, -2)) || R_FAILED(threadStart(&g_thr))) {
        audoutStopAudioOut();
        audoutExit();
        free(g_mem);
        g_mem = NULL;
        return;
    }
    g_ok = 1;
}

void audio_exit(void)
{
    if (!g_ok) return;
    g_stop = 1;
    threadWaitForExit(&g_thr);
    threadClose(&g_thr);
    audoutStopAudioOut();
    audoutExit();
    free(g_mem);
    g_mem = NULL;
    g_ok = 0;
}

void audio_play(int snd, float gain, int delay_ms)
{
    if (!g_ok || snd < 0 || snd >= SND_COUNT || !g_wav[snd].pcm) return;
    if (gain < 0) gain = 0;
    mutexLock(&g_mx);
    int pick = 0;
    for (int v = 0; v < NVOICES; v++) {
        if (!g_voice[v].pcm) {
            pick = v;
            break;
        }
        // all busy: take the one with the least left to play (nearly over; never the intro's music, nor a sound still waiting to start)
        if (g_voice[v].n - g_voice[v].pos < g_voice[pick].n - g_voice[pick].pos) pick = v;
    }
    g_voice[pick].pcm = g_wav[snd].pcm;
    g_voice[pick].n = g_wav[snd].n;
    g_voice[pick].pos = delay_ms > 0 ? -delay_ms * 48 : 0;
    g_voice[pick].gain = gain * g_wav[snd].gain * MASTER;
    mutexUnlock(&g_mx);
}

void audio_stop_all(void)
{
    if (!g_ok) return;
    mutexLock(&g_mx);
    for (int v = 0; v < NVOICES; v++) g_voice[v].pcm = NULL;
    mutexUnlock(&g_mx);
}
