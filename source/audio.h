// Sounds on the console (audio.c): a small mixer on audout. Not for the PC tests, which only count what the interface asks for.
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void); // starts the mixer; without it (or when the console has no audio) the sounds are simply not played
void audio_exit(void);
// Starts sound `snd` (SND_* in platform.h) at `gain` (0..1) after delay_ms. Sounds overlap.
void audio_play(int snd, float gain, int delay_ms);
void audio_stop_all(void); // every sound playing or waiting to start stops now

#endif
