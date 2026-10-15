// UI preview for the console: shows every screen drawn by the real renderer (gfx.c + scenes.c) so the design can be
// judged on the actual display and the actual hardware.
//   Left/Right or L/R  previous / next screen      X or Y  dark / light      + or Home  quit
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "scenes.h"

int main(void)
{
    NWindow *win = nwindowGetDefault();
    Framebuffer fb;
    framebufferCreate(&fb, win, SCREEN_W, SCREEN_H, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Skin skin[2];
    skin_init(&skin[0], 0);
    skin_init(&skin[1], 1);
    Surf *screen = surf_new(SCREEN_W, SCREEN_H);
    Surf *shown = surf_new(SCREEN_W, SCREEN_H);

    int scene = 0, dark = 0, dirty = 1;
    double ms = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (down & (HidNpadButton_Right | HidNpadButton_R)) { scene = (scene + 1) % SC_COUNT; dirty = 1; }
        if (down & (HidNpadButton_Left | HidNpadButton_L)) { scene = (scene + SC_COUNT - 1) % SC_COUNT; dirty = 1; }
        if (down & (HidNpadButton_X | HidNpadButton_Y | HidNpadButton_ZL | HidNpadButton_ZR)) { dark = !dark; dirty = 1; }

        if (dirty) {
            u64 t0 = armGetSystemTick();
            scene_render(&skin[dark], (SceneId)scene, screen);
            ms = (double)armTicksToNs(armGetSystemTick() - t0) / 1e6;
            surf_copy(shown, screen);
            char l1[96], l2[96];
            snprintf(l1, sizeof l1, "%d/%d  %s  %.0f ms", scene + 1, SC_COUNT, scene_name((SceneId)scene), ms);
            snprintf(l2, sizeof l2, "L/R screen  X theme  + quit");
            text_draw(shown, skin[dark].med, 12.5f, 0, 14, SCREEN_H - 34, 0, with_a(skin[dark].th.text3, 0.9f), l1);
            text_draw(shown, skin[dark].med, 12.5f, 0, 14, SCREEN_H - 14, 0, with_a(skin[dark].th.text3, 0.9f), l2);
            dirty = 0;
        }

        u32 stride;
        u8 *out = (u8 *)framebufferBegin(&fb, &stride);
        for (int y = 0; y < SCREEN_H; y++) memcpy(out + (size_t)y * stride, shown->px + (size_t)y * SCREEN_W * 4, SCREEN_W * 4);
        framebufferEnd(&fb);
    }

    surf_free(screen);
    surf_free(shown);
    framebufferClose(&fb);
    return 0;
}
