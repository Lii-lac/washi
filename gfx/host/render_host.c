// PC screenshot tool: renders every screen in dark and light to PNG. Build: make -f Makefile.host   Run: ./render_host out
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../source/gfx/stb_image_write.h"
#include "../scenes.h"

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "out";
    char path[512];
    Surf *s = surf_new(SCREEN_W, SCREEN_H);
    for (int dark = 1; dark >= 0; dark--) {
        Skin skin;
        skin_init(&skin, dark);
        for (int id = 0; id < SC_COUNT; id++) {
            clock_t t0 = clock();
            scene_render(&skin, (SceneId)id, s);
            double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
            snprintf(path, sizeof path, "%s/%d-%s-%s.png", dir, id, dark ? "dark" : "light", scene_name((SceneId)id));
            for (char *p = path; *p; p++) if (*p == ' ' || *p == ',') *p = '_';
            stbi_write_png(path, SCREEN_W, SCREEN_H, 4, s->px, SCREEN_W * 4);
            printf("%s %-22s %6.0f ms\n", dark ? "dark " : "light", scene_name((SceneId)id), ms);
        }
    }
    surf_free(s);
    return 0;
}
