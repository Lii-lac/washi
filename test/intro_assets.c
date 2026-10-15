// Test-only (included by app_host.c): renders the pieces of the intro mockups with the app's own code, so the page of mockups shows the real logo, the real strip, the
// real tape and paper and the real Home screen, not copies of them. `./app_host intro-assets` writes PNGs (straight alpha) and assets.json into INTRO_DIR
// (default out/app/49-intro-assets). The Home screen itself comes from `MATTE=black ./app_host readme-shots` and `MATTE=white ./app_host readme-shots` (a screen drawn
// on a black and on a white board gives the alpha of everything on it).
#include <sys/stat.h>

#include "../source/gfx/paper_roll.h"

extern int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);

static char g_intro_dir[400];
static FILE *g_intro_json;

static void intro_png(const char *name, const PaperSpr *s)
{
    char p[500];
    snprintf(p, sizeof p, "%s/%s", g_intro_dir, name);
    if (s) stbi_write_png(p, s->w, s->h, 4, s->px, s->w * 4);
}

// the app's own mark (the seal with its two strips of tape), baked `k` times as big as the 74 x 86 sprite of k = 1
static void intro_draw_mark(Surf *s, void *ud)
{
    float k = *(const float *)ud;
    Cx c = ui_cx(s);
    skin_washi_mark(&c, 37.0f * k, 40.0f * k, k);
}

typedef struct {
    const PaperSpr *strip, *tab, *strip_sh, *tab_sh;
    int len;
    float S;
    const char *name;
    int mode; // 0 the whole title, 1 without its name (the blank strip and fold), 2 only the name (for a page that writes it by hand)
} IntroTitle;

// the printed strip with its name and the folded end (skin.c: title_stage1), `S` times as big
static void intro_draw_title(Surf *s, void *ud)
{
    const IntroTitle *b = ud;
    Cx c = ui_cx(s);
    float S = b->S;
    if (b->mode != 2 && b->strip_sh) paper_spr_put(s, b->strip_sh, (int)(12 * S + b->len / 2), (int)(46 * S));
    if (b->mode != 2 && b->strip) paper_spr_put(s, b->strip, (int)(12 * S + b->len / 2), (int)(46 * S));
    if (b->mode != 1) skin_txtc(&c, c.skin->semi, 40.0f * S, 0, 26 * S, 45 * S, 0, hex(PAPER_TEXT, 1), b->name);
    if (b->mode == 2) return;
    if (b->tab_sh) paper_spr_put(s, b->tab_sh, (int)(12 * S + b->len - 9 * S), (int)(46 * S));
    if (b->tab) paper_spr_put(s, b->tab, (int)(12 * S + b->len - 9 * S), (int)(46 * S));
    gfx_line(s, 12.0f * S + b->len, 19 * S, 12.0f * S + b->len, 73 * S, 2.0f * S, col(0, 0, 0, 0.12f));
}

static void intro_draw_turned(Surf *s, void *ud)
{
    const PaperSpr *p = ud;
    paper_spr_xform(s, p, p->w * 0.5f, p->h * 0.5f, 1.0f, -1.2f * 3.14159265f / 180.0f, 1.0f);
}

typedef struct {
    const PaperSpr *card, *sh, *tape;
    int w, h, pad;
    char letter[4];
    float tape_dx;
} IntroTile;

static void intro_draw_tile(Surf *s, void *ud)
{
    const IntroTile *t = ud;
    Cx c = ui_cx(s);
    if (t->sh) paper_spr_put(s, t->sh, t->pad, t->pad);
    if (t->card) paper_spr_put(s, t->card, t->pad, t->pad);
    if (t->letter[0]) skin_txtc(&c, c.skin->semi, t->h * 0.66f, 0, t->pad + t->w * 0.5f, t->pad + t->h * 0.5f + 2, 1, hex(PAPER_TEXT, 1), t->letter);
    if (t->tape) paper_spr_put(s, t->tape, (int)(t->pad + t->w * 0.5f + t->tape_dx), t->pad + 3);
}

static void intro_draw_seal_only(Surf *s, void *ud)
{
    float R = *(const float *)ud;
    PaperSpr *seal = paper_seal(R, PAPER_INK, 5u);
    if (seal) paper_spr_xform(s, seal, s->w * 0.5f, s->h * 0.5f, 1.0f, -9.0f * 3.14159265f / 180.0f, 1.0f);
    paper_spr_free(seal);
}

static void intro_draw_sheet(Surf *s, void *ud)
{
    const IntroTile *t = ud; // (a sheet is a tile without a letter)
    gfx_shadow(s, (float)t->pad, (float)t->pad, (float)t->w, (float)t->h, 10.0f, 16.0f, 46.0f, col(0, 0, 0, 0.62f));
    if (t->card) paper_spr_put(s, t->card, t->pad, t->pad);
}

// the lockup, as the corner of every screen draws it (mark sprite at (42, 9), title at (112, 12) when S = 1), `S` times as big, as mark<key>.png and title<key>.png
static void intro_lockup(float S, const char *key)
{
    char nm[48];
    g_paper_pat_scale = S;
    float mk = 1.12f * S;
    PaperSpr *mark = paper_bake((int)(74 * mk), (int)(86 * mk), intro_draw_mark, &mk);
    snprintf(nm, sizeof nm, "mark%s.png", key);
    intro_png(nm, mark);
    fprintf(g_intro_json, "  \"mark%s\": {\"x\": %.2f, \"y\": %.2f, \"w\": %d, \"h\": %d},\n", key, 42.0f * S, 9.0f * S, mark ? mark->w : 0, mark ? mark->h : 0);
    const char *name = "Washi";
    int len1 = (int)ceilf(text_width(G.skin.semi, 40, 0, name)) + 40;
    len1 += len1 & 1;
    int len = (int)lroundf(len1 * S);
    len += len & 1;
    PaperTape sp = {len, (int)(54 * S), 0xD6FACA, PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_GRAINY | PAPER_TAPE_COARSE | PAPER_TAPE_PAT(8), 0.0f, 31},
              tp = {(int)(18 * S), (int)(50 * S), 0xECFDE6, PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_PLAIN | PAPER_TAPE_COARSE, 0.0f, 32};
    PaperSpr *strip = paper_tape(&sp), *tab = paper_tape(&tp);
    PaperSpr *strip_sh = paper_shadow_of(strip, 0.5f * S, 1.5f * S, 1.2f * S, 0x2B2340, 0.30f), *tab_sh = paper_shadow_of(tab, 1.0f * S, 7.0f * S, 4.0f * S, 0x4A3A1A, 0.40f);
    IntroTitle tb = {strip, tab, strip_sh, tab_sh, len, S, name, 0};
    PaperSpr *st = paper_bake((int)((len1 + 24) * S), (int)(84 * S), intro_draw_title, &tb);
    PaperSpr *title = st ? paper_bake((int)((len1 + 24) * S), (int)(84 * S), intro_draw_turned, st) : NULL;
    snprintf(nm, sizeof nm, "title%s.png", key);
    intro_png(nm, title);
    fprintf(g_intro_json, "  \"title%s\": {\"x\": %.2f, \"y\": %.2f, \"w\": %d, \"h\": %d},\n", key, 112.0f * S, 12.0f * S, title ? title->w : 0, title ? title->h : 0);
    paper_spr_free(st);
    paper_spr_free(title);
    for (int md = 1; md <= 2; md++) { // the title without its name (titleblank) and only its name (titletext), baked the same way so they lie exactly on title.png
        tb.mode = md;
        PaperSpr *p1 = paper_bake((int)((len1 + 24) * S), (int)(84 * S), intro_draw_title, &tb);
        PaperSpr *p2 = p1 ? paper_bake((int)((len1 + 24) * S), (int)(84 * S), intro_draw_turned, p1) : NULL;
        snprintf(nm, sizeof nm, "title%s%s.png", md == 1 ? "blank" : "text", key);
        intro_png(nm, p2);
        paper_spr_free(p1);
        paper_spr_free(p2);
    }
    paper_spr_free(strip);
    paper_spr_free(tab);
    paper_spr_free(strip_sh);
    paper_spr_free(tab_sh);
    paper_spr_free(mark);
    g_paper_pat_scale = 1.0f;
}

// ---- round three: the tape wall. Strips made as the progress roll's strips are (the same tape code: a zigzag, slanted or square cut at each end, now and then pressed
// down with a ridge), but wide, in many widths, colours and patterns; and, for the page to be checked against, the app's own roll drawn by paper_roll.c.
typedef struct { int len, wid, colour, kind; } WallSpec;
static const WallSpec WALL[16] = {
    // the base, laid top to bottom: horizontal, a little crooked, no two the same width, colour or pattern next to each other
    {1800, 124, 5, PAT_SCALLOP}, {1800, 150, 0, PAT_STRIPES}, {1800, 108, 3, PAT_ZIG}, {1800, 140, 6, PAT_HLINE}, {1800, 118, 2, PAT_GRID},
    {1800, 156, 4, PAT_CHECK}, {1800, 112, 1, PAT_DOTS}, {1800, 144, 7, PAT_WSTRIPE}, {1800, 128, 5, PAT_DOTS},
    // across them: three upright strips, two slanting ones, two short pieces
    {1040, 96, 3, PAT_STRIPES}, {1040, 124, 6, PAT_SCALLOP}, {1040, 84, 0, PAT_CHECK},
    {1700, 112, 2, PAT_WSTRIPE}, {1700, 92, 4, PAT_ZIG},
    {520, 100, 7, PAT_HLINE}, {380, 84, 1, PAT_GRID},
};

// ---- round four: the app's pressed tape on its own (a strip stuck down on the board, with the milky patches, the lit and shaded edges, the fibres and, sometimes, the
// ridge), made by paper_tape exactly as a strip on a card is, for the page's version of the same code to be checked against. The strip is stuck to a card that is far away,
// so nothing of the card's edge shows: only how the strip itself lies.
static void intro_export_tape_ref(void)
{
    static const struct { int len, wid, colour, kind; unsigned flags; unsigned seed; } REF[6] = {
        {320, 28, 0, PAT_STRIPES, 0, 11u}, {320, 28, 5, PAT_DOTS, PAPER_TAPE_CRIMP, 12u}, {320, 28, 4, PAT_CHECK, PAPER_TAPE_CRIMP, 13u},
        {320, 28, 2, PAT_SCALLOP, 0, 14u}, {320, 28, 6, PAT_WSTRIPE, PAPER_TAPE_CRIMP, 15u}, {320, 28, 1, PAT_HLINE, 0, 16u},
    };
    char nm[48];
    fprintf(g_intro_json, "  \"taperef\": [");
    for (int i = 0; i < 6; i++) {
        g_paper_pat_scale = 1.0f;
        PaperTape t = {REF[i].len, REF[i].wid, paper_tape_rgb[REF[i].colour], REF[i].kind, REF[i].flags, 0.0f, REF[i].seed, 0, 0, 0, 0, 0};
        paper_tape_host(&t, 100.0f, 100.0f, 10.0f, 5000.0f, 5000.0f);
        PaperSpr *spr = paper_tape(&t);
        snprintf(nm, sizeof nm, "tape_ref_%d.png", i);
        intro_png(nm, spr);
        int crimp = (t.flags & PAPER_TAPE_CRIMP) && t.len >= 36;
        float cx = 0, ky = 0, kr = 0;
        if (crimp) {
            PaperRng cr;
            paper_rng_seed(&cr, t.seed ^ 0xC12117u);
            float f1 = paper_rng_f(&cr), f2 = paper_rng_f(&cr), f3 = paper_rng_f(&cr), f4 = paper_rng_f(&cr);
            cx = t.len * (0.3f + 0.38f * f1);
            ky = (f2 < 0.5f ? -1.0f : 1.0f) * (1.2f + 1.6f * f3);
            kr = (f4 * 2.0f - 1.0f) * 0.3f;
        }
        fprintf(g_intro_json, "%s{\"file\": \"%s\", \"w\": %d, \"h\": %d, \"ox\": %d, \"oy\": %d, \"len\": %d, \"wid\": %d, \"kind\": %d, \"colour\": %d, \"seed\": %u, \"flags\": %u, \"crimp\": %d, \"cx\": %.4f, \"ky\": %.4f, \"kr\": %.4f}",
                i ? ", " : "", nm, spr ? spr->w : 0, spr ? spr->h : 0, spr ? spr->ox : 0, spr ? spr->oy : 0, t.len, t.wid, t.kind, REF[i].colour, t.seed, t.flags, crimp, (double)cx, (double)ky, (double)kr);
        paper_spr_free(spr);
    }
    fprintf(g_intro_json, "],\n");
}

static void intro_export_wall(void)
{
    intro_export_tape_ref();
    char nm[48];
    fprintf(g_intro_json, "  \"wall\": [");
    for (int i = 0; i < 16; i++) {
        const WallSpec *w = &WALL[i];
        g_paper_pat_scale = w->wid / 30.0f;                                   // the app's tape is about 30 px wide: the pattern grows with the strip
        PaperTape t = {w->len, w->wid, paper_tape_rgb[w->colour], w->kind, PAPER_TAPE_ZIG | PAPER_TAPE_OPAQUE | PAPER_TAPE_GRAINY, 0.0f, 700u + (uint32_t)i, 0, 0, 0, 0, 0};
        float hx = 0, hy = 0;
        paper_tape_hand(&t, &hx, &hy, 40u + (uint32_t)i, 0.35f);              // a hair different in length and width, ends cut this way or that, now and then a ridge
        t.tilt = 0.0f;                                                          // (the page turns it)
        PaperSpr *spr = paper_tape(&t);
        snprintf(nm, sizeof nm, "wall%02d.png", i);
        intro_png(nm, spr);
        // where the ridge is, as paper.c and paper_roll.c work it out from the same seed: the far half sits ky px to one side and kr degrees turned
        int crimp = (t.flags & PAPER_TAPE_CRIMP) && t.len >= 36;
        float cx = 0, ky = 0, kr = 0;
        if (crimp) {
            PaperRng cr;
            paper_rng_seed(&cr, t.seed ^ 0xC12117u);
            float f1 = paper_rng_f(&cr), f2 = paper_rng_f(&cr), f3 = paper_rng_f(&cr), f4 = paper_rng_f(&cr);
            cx = t.len * (0.3f + 0.38f * f1);
            ky = (f2 < 0.5f ? -1.0f : 1.0f) * (1.2f + 1.6f * f3);
            kr = (f4 * 2.0f - 1.0f) * 0.3f;
        }
        fprintf(g_intro_json, "%s{\"file\": \"%s\", \"w\": %d, \"h\": %d, \"ox\": %d, \"oy\": %d, \"len\": %d, \"wid\": %d, \"flags\": %u, \"crimp\": %d, \"cx\": %.2f, \"ky\": %.2f, \"kr\": %.3f, \"colour\": %d, \"kind\": %d}",
                i ? ", " : "", nm, spr ? spr->w : 0, spr ? spr->h : 0, spr ? spr->ox : 0, spr ? spr->oy : 0, t.len, t.wid, t.flags, crimp, cx, ky, kr, w->colour, w->kind);
        paper_spr_free(spr);
    }
    fprintf(g_intro_json, "],\n");
    g_paper_pat_scale = 1.0f;

    // the app's roll, drawn by the app's code on white, at four places along a strip: what the page's roll is checked against
    {
        RollRect guide = {60, 20, 400, 30};
        RollPick pick = {5, PAT_SCALLOP};
        RollGeo g = roll_geo_from_seed(7u);
        g.th = 28; g.dx = 0; g.dy = 0; g.rot = 0.0f; g.ov = 6.0f; g.r0 = 26.0f; g.r1 = 17.0f; g.no_crimp = 1;
        RollStrip *rs = roll_strip_new_geo(&guide, pick, 7u, 0, &g);
        if (rs) {
            intro_png("roll_ref_strip.png", rs->strip);
            fprintf(g_intro_json, "  \"rollref\": {\"th\": %d, \"rb\": %d, \"r0\": %.2f, \"r1\": %.2f, \"n\": %d, \"len\": %.1f, \"colour\": %d, \"kind\": %d, \"sw\": %d, \"sh\": %d, \"phase\": [",
                    rs->th, rs->rb, (double)rs->r0, (double)rs->r1, rs->n, (double)rs->len, pick.colour, pick.kind, rs->strip->w, rs->strip->h);
            for (int k = 0; k <= ROLL_PHASE_N; k++) fprintf(g_intro_json, "%s%.5f", k ? ", " : "", (double)rs->phase[k]);
            fprintf(g_intro_json, "], \"frames\": [");
            static const float PS[4] = {0.0f, 0.3f, 0.6f, 1.0f};
            for (int i = 0; i < 4; i++) {
                Surf *s = surf_new(520, 80);
                gfx_fill(s, col(1, 1, 1, 1));
                roll_roll_draw(s, rs, PS[i], NULL);
                snprintf(nm, sizeof nm, "roll_ref_%d.png", i);
                char p[500];
                snprintf(p, sizeof p, "%s/%s", g_intro_dir, nm);
                stbi_write_png(p, s->w, s->h, 4, s->px, s->w * 4);
                surf_free(s);
                float cx, cy;
                roll_centre(rs, PS[i], &cx, &cy);
                fprintf(g_intro_json, "%s{\"p\": %.2f, \"cx\": %.2f, \"cy\": %.2f, \"R\": %.3f, \"file\": \"%s\"}", i ? ", " : "", (double)PS[i], (double)cx, (double)cy, (double)roll_radius(rs, PS[i]), nm);
            }
            fprintf(g_intro_json, "]},\n");
            roll_strip_free(rs);
        }
    }
}

// ---- the app's own crayon doodles for the intro card (round 17b): the Home banner's eight shapes in the banner's colours, the ring the app draws round a picture,
// a tick and an underline in ink, all at the app's own size (the card page draws them 1/0.6 times as big on the card, so on the screen they are exactly the app's)
static void intro_export_doodles(void)
{
    static const struct { int kind, size; uint32_t rgb; const char *name; } D[] = {
        {PAPER_SHAPE_STAR, 120, 0xC7A4F6, "doodle_star"},   {PAPER_SHAPE_HEART, 110, 0xF6A6BD, "doodle_heart"},     {PAPER_SHAPE_FLOWER, 110, 0xFFD36E, "doodle_flower"},
        {PAPER_SHAPE_CLOUD, 130, 0xA5CFF3, "doodle_cloud"}, {PAPER_SHAPE_MOON, 110, 0xCDB8F5, "doodle_moon"},       {PAPER_SHAPE_SPARKLE, 100, 0xFFDF7A, "doodle_sparkle"},
        {PAPER_SHAPE_BURST, 100, 0xFFC08A, "doodle_burst"}, {PAPER_SHAPE_BADGE, 100, 0xF4A9D8, "doodle_badge"},     {PAPER_SHAPE_SPARKLE, 60, 0x9ADDBE, "doodle_sparkle2"},
        {PAPER_SHAPE_STAR, 60, 0xFFE08A, "doodle_star2"},   {PAPER_SHAPE_HEART, 56, 0xF8B0C4, "doodle_heart2"},
    };
    const int n = (int)(sizeof D / sizeof D[0]);
    PaperSpr *spr[16];
    char nm[48];
    for (int i = 0; i < n; i++) spr[i] = paper_shape(D[i].kind, D[i].size, D[i].rgb, 300u + (uint32_t)i);
    spr[n] = paper_ring(80.0f, &PAPER_CRAYON_STD, 311u);
    spr[n + 1] = paper_tick(40.0f, &PAPER_CRAYON_BLUE, 312u);
    spr[n + 2] = paper_underline(390, &PAPER_CRAYON_BLUE, 313u);
    static const char *const MORE[3] = {"doodle_ring", "doodle_tick", "doodle_underline"};
    fprintf(g_intro_json, "  \"doodles\": {");
    for (int i = 0; i < n + 3; i++) {
        const char *name = i < n ? D[i].name : MORE[i - n];
        snprintf(nm, sizeof nm, "%s.png", name);
        intro_png(nm, spr[i]);
        fprintf(g_intro_json, "%s\"%s\": {\"w\": %d, \"h\": %d, \"ox\": %d, \"oy\": %d}", i ? ", " : "", name, spr[i] ? spr[i]->w : 0, spr[i] ? spr[i]->h : 0, spr[i] ? spr[i]->ox : 0, spr[i] ? spr[i]->oy : 0);
        paper_spr_free(spr[i]);
    }
    fprintf(g_intro_json, "},\n");
}

static void intro_export(HostPlat *h)
{
    (void)h;
    if (getenv("TAPE")) g_skin_mark_tape = 1; // (the mark used to have two small tapes; the app has none now)
    const char *d = getenv("INTRO_DIR");
    snprintf(g_intro_dir, sizeof g_intro_dir, "%s", d && d[0] ? d : "out/app/49-intro-assets");
    mkdir("out", 0755);
    mkdir("out/app", 0755);
    mkdir(g_intro_dir, 0755);
    char p[500];
    snprintf(p, sizeof p, "%s/assets.json", g_intro_dir);
    g_intro_json = fopen(p, "wb");
    if (!g_intro_json) return;
    const float S = getenv("INTRO_S") ? (float)atof(getenv("INTRO_S")) : 3.2f;
    fprintf(g_intro_json, "{\n  \"S\": %.3f,\n", S);

    intro_lockup(S, "");    // the big one (mark.png, title.png)
    intro_lockup(1.0f, "1"); // and the real size, as the Home screen has it (mark1.png, title1.png)

    // ---- the seal alone (the ink, no tape) and the two strips that hold it on the corner mark, baked big (S_A), the strips flat so the page can turn and lay them
    {
        const float SA = 5.2f, k = 1.12f * SA, R = 26.0f * k;
        g_paper_pat_scale = SA;
        int W2 = (int)ceilf(2.0f * R) + 24;
        PaperSpr *seal = paper_bake(W2, W2, intro_draw_seal_only, (void *)&R);
        intro_png("seal.png", seal);
        fprintf(g_intro_json, "  \"seal\": {\"R\": %.2f, \"w\": %d, \"h\": %d},\n", R, seal ? seal->w : 0, seal ? seal->h : 0);
        PaperTape ta = {(int)(34 * k), (int)(13 * k), 0xFED5D4, PAT_STRIPES, PAPER_TAPE_ZIG, 0.0f, 5}, tb = {(int)(32 * k), (int)(13 * k), 0xAED6F9, PAT_DOTS, PAPER_TAPE_ZIG, 0.0f, 6};
        PaperSpr *sa = paper_tape(&ta), *sb = paper_tape(&tb);
        intro_png("tapeA.png", sa);
        intro_png("tapeB.png", sb);
        fprintf(g_intro_json, "  \"tapeA\": {\"w\": %d, \"h\": %d, \"dx\": -0.74, \"dy\": -0.78, \"tilt\": -38},\n", sa ? sa->w : 0, sa ? sa->h : 0);
        fprintf(g_intro_json, "  \"tapeB\": {\"w\": %d, \"h\": %d, \"dx\": 0.78, \"dy\": 0.80, \"tilt\": -38},\n", sb ? sb->w : 0, sb ? sb->h : 0);
        paper_spr_free(seal);
        paper_spr_free(sa);
        paper_spr_free(sb);
        g_paper_pat_scale = 1.0f;
    }

    // ---- strips of tape for a wall of it: opaque, long enough to cross the screen, as tall as a hand is wide
    static const int KINDS[8] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG, PAT_CHECK, PAT_SCALLOP, PAT_HLINE, PAT_WSTRIPE};
    g_paper_pat_scale = 3.2f;
    fprintf(g_intro_json, "  \"tapes\": [");
    for (int i = 0; i < 8; i++) {
        PaperTape t = {1760, 104, paper_tape_rgb[i], KINDS[i], PAPER_TAPE_ZIG | PAPER_TAPE_OPAQUE | PAPER_TAPE_GRAINY, 0.0f, 600u + (uint32_t)i};
        PaperSpr *spr = paper_tape(&t);
        char nm[32];
        snprintf(nm, sizeof nm, "tape%d.png", i);
        intro_png(nm, spr);
        fprintf(g_intro_json, "%s{\"file\": \"%s\", \"w\": %d, \"h\": %d}", i ? ", " : "", nm, spr ? spr->w : 0, spr ? spr->h : 0);
        paper_spr_free(spr);
    }
    fprintf(g_intro_json, "],\n");
    g_paper_pat_scale = 1.0f;

    // ---- paper tiles, a letter on each (the same card, shadow and tape as the Home tiles, bigger)
    static const char *const LET[6] = {"W", "A", "S", "H", "I", ""};
    int TW = 196, TH2 = 196, pad = 44;
    PaperSpr *card = paper_card_ex(TW, TH2, 9.0f, 71u, PAPER_CARD_PAD);
    PaperShadows *shs = paper_shadows(TW, TH2, 9.0f, 0.0f, 2);
    fprintf(g_intro_json, "  \"tiles\": {\"w\": %d, \"h\": %d, \"pad\": %d, \"files\": [", TW, TH2, pad);
    g_paper_pat_scale = 2.2f;
    struct { int w, h; float dx; } tapeT[6];
    for (int i = 0; i < 6; i++) {
        PaperTape t = {112, 36, paper_tape_rgb[(i * 3 + 1) & 7], KINDS[i & 3], PAPER_TAPE_ZIG, i & 1 ? 3.0f : -4.0f, 80u + (uint32_t)i};
        PaperSpr *tape = paper_tape(&t);
        IntroTile it = {card, shs ? shs->s[0] : NULL, tape, TW, TH2, pad, "", i & 1 ? 14.0f : -10.0f};
        snprintf(it.letter, sizeof it.letter, "%s", LET[i]);
        PaperSpr *spr = paper_bake(TW + 2 * pad, TH2 + 2 * pad, intro_draw_tile, &it);
        char nm[32];
        snprintf(nm, sizeof nm, "tile%d.png", i);
        intro_png(nm, spr);
        fprintf(g_intro_json, "%s\"%s\"", i ? ", " : "", nm);
        // round two: the same tile without its tape (the page lays the tape on after the tile lands) and the tape piece on its own
        IntroTile bare = it;
        bare.tape = NULL;
        PaperSpr *spb = paper_bake(TW + 2 * pad, TH2 + 2 * pad, intro_draw_tile, &bare);
        snprintf(nm, sizeof nm, "tile%d_bare.png", i);
        intro_png(nm, spb);
        snprintf(nm, sizeof nm, "tapeT%d.png", i);
        intro_png(nm, tape);
        tapeT[i].w = tape ? tape->w : 0;
        tapeT[i].h = tape ? tape->h : 0;
        tapeT[i].dx = it.tape_dx;
        paper_spr_free(spr);
        paper_spr_free(spb);
        paper_spr_free(tape);
    }
    fprintf(g_intro_json, "]},\n  \"tapeT\": [");
    for (int i = 0; i < 6; i++)
        fprintf(g_intro_json, "%s{\"w\": %d, \"h\": %d, \"dx\": %.1f, \"dy\": %d}", i ? ", " : "", tapeT[i].w, tapeT[i].h, tapeT[i].dx, 3 - TH2 / 2);
    fprintf(g_intro_json, "],\n");
    g_paper_pat_scale = 1.0f;
    paper_spr_free(card);
    paper_shadows_free(shs);

    // ---- a bigger blank tile for the seal (round two: D stamps the seal on its own tile), same card, shadow and corner radius, no tape
    {
        int SW2 = 236, SH2 = 236;
        PaperSpr *sc = paper_card_ex(SW2, SH2, 10.0f, 73u, PAPER_CARD_PAD);
        PaperShadows *ss = paper_shadows(SW2, SH2, 10.0f, 0.0f, 2);
        IntroTile it = {sc, ss ? ss->s[0] : NULL, NULL, SW2, SH2, pad, "", 0};
        PaperSpr *spr = paper_bake(SW2 + 2 * pad, SH2 + 2 * pad, intro_draw_tile, &it);
        intro_png("tileS.png", spr);
        fprintf(g_intro_json, "  \"tileS\": {\"w\": %d, \"h\": %d, \"pad\": %d},\n", SW2, SH2, pad);
        paper_spr_free(spr);
        paper_spr_free(sc);
        paper_shadows_free(ss);
    }

    // ---- a sheet of paper, lying on the dark
    {
        int SW = 1100, SH = 600, sp2 = 80;
        PaperSpr *sc = paper_card_ex(SW, SH, 10.0f, 91u, PAPER_CARD_PAD);
        IntroTile it = {sc, NULL, NULL, SW, SH, sp2, "", 0};
        PaperSpr *spr = paper_bake(SW + 2 * sp2, SH + 2 * sp2, intro_draw_sheet, &it);
        intro_png("sheet.png", spr);
        fprintf(g_intro_json, "  \"sheet\": {\"file\": \"sheet.png\", \"w\": %d, \"h\": %d, \"pad\": %d},\n", SW, SH, sp2);
        paper_spr_free(spr);
        paper_spr_free(sc);
    }

    // ---- the board with nothing on it (as every screen starts), as a picture
    {
        Surf *b = surf_new(SCREEN_W, SCREEN_H);
        Cx c = ui_cx(b);
        skin_ink_clear();
        skin_board(&c);
        snprintf(p, sizeof p, "%s/board.jpg", g_intro_dir);
        stbi_write_jpg(p, SCREEN_W, SCREEN_H, 4, b->px, 90);
        surf_free(b);
    }
    intro_export_wall();
    intro_export_doodles();
    fprintf(g_intro_json, "  \"done\": true\n}\n");
    fclose(g_intro_json);
    g_intro_json = NULL;
}

static void sc_intro_assets(void)
{
    W w;
    reminder_world(&w, "49-intro-assets", 0);
    HostPlat *h = &w.hp;
    hp_call(h, intro_export);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}
