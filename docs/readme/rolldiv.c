// Draws the app's own progress roll (source/gfx/paper_roll.c) as a README divider: a straight, uncrimped strip along a horizontal guide with the roll at its end.
// usage: rolldiv <night 0|1> <roll x> <colour> <pattern> <seed> <out prefix>   writes <prefix>-white.png and <prefix>-black.png (python mattes them into one transparent PNG)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "stb_image_write.h"

#include "paper_roll.h"

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage\n"); return 1; }
    int night = atoi(argv[1]);
    float target = (float)atof(argv[2]);
    int colour = atoi(argv[3]), kind = atoi(argv[4]);
    unsigned seed = (unsigned)atoi(argv[5]);
    const char *pre = argv[6];
    paper_pal_set(night);
    int W = 1280, H = 120;
    RollRect guide = {-60, 42, 1340, 34};
    RollGeo g = roll_geo_from_seed(seed);
    g.rot = 0; g.dy = 0; g.no_crimp = 1; g.th = 28; g.r0 = g.r1 = 17;
    RollStrip *r = roll_strip_new_geo(&guide, (RollPick){colour, kind}, seed, 0, &g);
    float lo = 0, hi = 1, cx = 0, cy = 0, prog = 1;
    for (int i = 0; i < 40; i++) {
        prog = (lo + hi) / 2;
        roll_centre(r, prog, &cx, &cy);
        if (cx < target) lo = prog; else hi = prog;
    }
    roll_centre(r, prog, &cx, &cy);
    fprintf(stderr, "progress %.4f th %d rot %.2f roll at %.1f,%.1f radius %.1f\n", prog, r->th, r->rot, cx, cy, roll_radius(r, prog));
    for (int pass = 0; pass < 2; pass++) {
        Surf *s = surf_new(W, H);
        gfx_fill(s, hex(pass ? 0x000000 : 0xFFFFFF, 1));
        roll_strip_draw(s, r, prog, NULL);
        roll_roll_draw(s, r, prog, NULL);
        char path[512];
        snprintf(path, sizeof path, "%s-%s.png", pre, pass ? "black" : "white");
        stbi_write_png(path, s->w, s->h, 4, s->px, s->w * 4);
        surf_free(s);
    }
    return 0;
}
