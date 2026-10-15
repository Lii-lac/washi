// Storage, in Washi: a ruled card with how much of the SD card is free and a bar of tape split by person, a card of slips for what can
// be done about it, and on the right a taped sheet with a slip for every profile whose saves use the most, each with a strip of tape
// as long as its share. The focused slip is the one that is unstuck (lifted, its tape peeling), as on every Washi page.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx/tint.h"
#include "ui_scene.h"

static const Prompt ST_PROMPTS[] = {{"A", "Open"}, {"B", "Back"}};

enum { LX = 40, LW = 560, SD_Y = 172, SD_H = 234, ACT_Y = 432, RX = 624, RW = 616, RY = 174 };
enum { ACT_SLIP_X = 52, ACT_SLIP_W = 536, ACT_SLIP_H = 80, ACT_PITCH = 90 };
enum { ROW_SLIP_X = 636, ROW_SLIP_W = 592, ROW_SLIP_H = 76, ROW_PITCH = 86, BAR_H = 22 };

// tests: what the page last laid out (the card's bar, each row's bar) and where the focus is: a profile's row (0..), an action (10, 11)
int g_ui_storage_parts, g_ui_storage_who[40], g_ui_storage_focus = -1;
float g_ui_storage_w[40], g_ui_storage_row_w[8], g_ui_storage_row_min[8]; // (a row's bar: its length, and its narrowest part)

static int nrows(const UiStorage *s) { return s->shown + (s->npeople > s->shown); }

void ui_storage_card_rect(const UiStorage *s, float r[4])
{
    int n = nrows(s);
    r[0] = RX;
    r[1] = RY;
    r[2] = RW;
    r[3] = (n ? n : 1) * ROW_PITCH + 22;
}

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

// a person's colour for their tape
static uint32_t tint_rgb(uint32_t tint) { return tint_for_theme(tint, 0); }

#define OTHER_RGB 0xBDBDC4

// ---------------------------------------------------------------------------------------------------- bars made of tape

enum { MAXPART = 40 };

typedef struct {
    int n;
    float gap;        // between neighbours, px
    float w[MAXPART];
    int who[MAXPART]; // index into people, -1 for the grey part, -2 for the smallest people sharing one part
} Parts;

typedef struct {
    uint64_t b;
    int who;
} Ent;

// A bar of several parts, w px wide: people[from..to), each as wide as its share, then `other` bytes in grey. The grey takes at most
// `other_cap` of the bar (the profiles share the rest by size), so a big pile of old saves doesn't squeeze the profiles to slivers; the
// legend says how big it really is. `fill` = how much of the bar the parts take. Every part is at least 2 px wide and 2 px from the
// next; with too many for that they touch and share what there is, and with more than MAXPART the smallest people share the last part.
static void layout_parts(Parts *o, const UiStoragePerson *p, int from, int to, uint64_t other, float other_cap, float w, float fill)
{
    o->n = 0;
    o->gap = 0;
    Ent e[MAXPART];
    int ne = 0, room = MAXPART - (other > 0);
    uint64_t total = other;
    for (int i = from; i < to; i++) {
        if (!p[i].bytes) continue;
        total += p[i].bytes;
        if (ne == room) {
            e[ne - 1].b += p[i].bytes;
            e[ne - 1].who = -2;
        } else {
            e[ne++] = (Ent){p[i].bytes, i};
        }
    }
    if (other) e[ne++] = (Ent){other, -1};
    if (!total || fill <= 0) return;
    float bw = w * fill;
    if (bw < 8) bw = 8;
    double people = (double)(total - other), og = other ? (double)other / (double)total : 0;
    if (og > other_cap && people > 0) og = other_cap;
#define SHARE(k) (e[k].who == -1 ? og : (people > 0 ? (1.0 - og) * (double)e[k].b / people : 0))
    float gap = ne > 1 ? 2.0f : 0.0f, avail = bw - gap * (ne - 1), small = 0;
    if (avail < 2.0f * ne) { // not even 2 px each: the parts touch and share the bar
        gap = 0;
        avail = bw;
    }
    double big = 0;
    for (int k = 0; k < ne; k++) {
        if (avail * SHARE(k) < 2) small += 2;
        else big += SHARE(k);
    }
    float share = avail - small;
    for (int k = 0; k < ne; k++) {
        float pw = (float)(avail * SHARE(k));
        if (share <= 0) pw = avail / ne;
        else if (pw < 2) pw = 2;
        else pw = (float)(share * SHARE(k) / big);
        o->w[o->n] = pw;
        o->who[o->n++] = e[k].who;
    }
    o->gap = gap;
#undef SHARE
}

static const int PATS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

#define MIN_TAPE 14.0f // a part narrower than this is a plain sliver, not a cut piece of tape

typedef struct {
    float x, w; // where the strip lies on the guide
    uint32_t rgb;
    int pat, tiny;
} Run;

// The strips of a bar. Neighbouring parts too narrow to be tape are laid as one strip: a row of slivers would read as a bar code.
static int bar_runs(Run *r, const Parts *pt, const UiStoragePerson *p, float x)
{
    int n = 0;
    for (int i = 0; i < pt->n; i++) {
        float pw = pt->w[i];
        int who = pt->who[i], tiny = who != -1 && pw < MIN_TAPE;
        if (tiny && n && r[n - 1].tiny) {
            r[n - 1].w += pt->gap + pw;
            r[n - 1].rgb = paper_tape_rgb[6]; // the colour of the "others" slip's own tape
            r[n - 1].pat = PAT_DOTS;
        } else {
            uint32_t rgb = who == -1 ? OTHER_RGB : who == -2 ? paper_tape_rgb[6] : tint_rgb(p[who].tint);
            r[n++] = (Run){x, pw, rgb, PATS[(who + 1) & 3], tiny};
        }
        x += pw + pt->gap;
    }
    return n;
}

// One strip laid by hand: a hair crooked, a pixel off the guide, not quite the width of the next, each end cut with the pinking shears or
// with scissors (square, or at a slant), over or short of its neighbour's end, and the first and last a little past the guide, as the
// mockup's strips are. A long strip alone on its guide may be crimped (in a bar of several, a seam inside a part would read as the
// border between two). Every number comes from the seed, so a bar looks the same each time it is drawn.
static void lay_strip(Surf *s, const Run *u, int i, int n, float y, float h, uint32_t seed)
{
    PaperRng rn;
    paper_rng_seed(&rn, (seed + (uint32_t)i) ^ 0x57A9u);
    float j[9]; // turn, off the guide, width, left end, right end, left cut, right cut, crimp, overshoot
    for (int k = 0; k < 9; k++) j[k] = paper_rng_f(&rn);
    float lim = fminf(1.2f, 240.0f / u->w); // degrees: about 2 px of drift at each end of a long strip
    float tilt = (j[0] - 0.5f) * 2.0f * lim;
    float dy = (j[1] - 0.5f) * 2.0f;
    int wid = (int)h - 2 + (int)(j[2] * 5.0f);
    float L = u->x - 1.5f + 2.5f * j[3] - (i == 0 ? 1.0f + 2.5f * j[8] : 0.0f);
    float R = u->x + u->w - 1.0f + 3.5f * j[4] + (i == n - 1 ? 1.0f + 2.5f * (1.0f - j[8]) : 0.0f);
    unsigned flags = j[5] < 0.45f ? PAPER_TAPE_ZIG_L : j[5] < 0.8f ? PAPER_TAPE_SLANT_L : 0;
    flags |= j[6] < 0.45f ? PAPER_TAPE_ZIG_R : j[6] < 0.8f ? PAPER_TAPE_SLANT_R : 0;
    if (n == 1 && u->w >= 110.0f && j[7] < 0.5f) flags |= PAPER_TAPE_CRIMP;
    PaperTape t = {(int)lroundf(R - L), wid, u->rgb, u->pat, flags, tilt, seed + (uint32_t)i};
    PaperSpr *sp = paper_tape(&t);
    if (sp) paper_spr_put(s, sp, (int)lroundf((L + R) * 0.5f), (int)lroundf(y + h * 0.5f + dy));
    paper_spr_free(sp);
}

// the track the tape lies on: a dashed outline; then the parts as strips of tape, side by side
static void draw_bar(Surf *s, float x, float y, float w, float h, const Parts *pt, const UiStoragePerson *p, uint32_t seed)
{
    scene_dashed_rrect(s, x - 1.5f, y - 1.5f, w + 3, h + 3, 5.0f, 1.3f, 4.0f, 3.0f, hex(0x9A93A8, 0.70f));
    Run run[MAXPART];
    int n = bar_runs(run, pt, p, x);
    for (int i = 0; i < n; i++) {
        if (run[i].w < MIN_TAPE) gfx_rect(s, run[i].x, y, run[i].w, h, hex(run[i].rgb, 1));
        else lay_strip(s, &run[i], i, n, y, h, seed);
    }
}

static uint64_t saves_total(const UiStorage *s)
{
    uint64_t b = s->other_b;
    for (int i = 0; i < s->npeople; i++) b += s->people[i].bytes;
    return b;
}

static uint64_t largest_row(const UiStorage *s)
{
    uint64_t largest = 0, rest = 0;
    for (int i = 0; i < s->npeople; i++) {
        if (i < s->shown && s->people[i].bytes > largest) largest = s->people[i].bytes;
        if (i >= s->shown) rest += s->people[i].bytes;
    }
    return rest > largest ? rest : largest;
}

// ---------------------------------------------------------------------------------------------------- the pieces

// what is printed on the SD card's card
static void draw_sd(Surf *sf, float ox, float oy, void *ud)
{
    const UiStorage *s = ud;
    Cx c = ui_cx(sf);
    Skin *u = c.skin;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    for (float y = oy + 34; y < oy + SD_H - 8; y += 34) gfx_rect(sf, ox + 10, y, LW - 20, 1.3f, hex(0xA9B8DD, 0.40f)); // the ruled lines
    char a[24], line[96];
    if (s->have_space) {
        ui_format_bytes(s->free_b, a, sizeof a);
        snprintf(line, sizeof line, "%s free", a);
        skin_txt(&c, u->semi, 42, -0.3f, ox + 34, oy + 72, 0, ink, line);
        ui_format_bytes(s->total_b, a, sizeof a);
        snprintf(line, sizeof line, "of %s on the SD card", a);
        skin_txt(&c, u->med, 20, 0, ox + 34, oy + 104, 0, soft, line);
    } else {
        skin_txt(&c, u->semi, 42, -0.3f, ox + 34, oy + 72, 0, ink, "SD card");
        skin_txt(&c, u->med, 20, 0, ox + 34, oy + 104, 0, soft, "free space unknown");
    }
    ui_format_bytes(saves_total(s), a, sizeof a);
    skin_txt(&c, u->semi, 20, 0, ox + 34, oy + 150, 0, ink, "Saves");
    skin_txt(&c, u->med, 20, 0, ox + LW - 34, oy + 150, 2, soft, a);
    Parts pt;
    layout_parts(&pt, s->people, 0, s->npeople, s->other_b, 0.25f, LW - 68, 1.0f);
    g_ui_storage_parts = pt.n;
    for (int i = 0; i < pt.n && i < 40; i++) {
        g_ui_storage_w[i] = pt.w[i];
        g_ui_storage_who[i] = pt.who[i];
    }
    draw_bar(sf, ox + 34, oy + 160, LW - 68, 28, &pt, s->people, 700u);
    // what the bar is made of, in real sizes (the grey may be drawn smaller than it is)
    ui_format_bytes(saves_total(s) - s->other_b, a, sizeof a);
    snprintf(line, sizeof line, "In profiles  %s", a);
    skin_txt(&c, u->med, 17, 0, ox + 34, oy + 214, 0, soft, line);
    if (s->other_b) {
        ui_format_bytes(s->other_b, a, sizeof a);
        snprintf(line, sizeof line, "Not in a profile  %s", a);
        float lw = text_width(u->med, 17, 0, line);
        gfx_circle(sf, ox + LW - 34 - lw - 14, oy + 208.5f, 6, hex(OTHER_RGB, 1));
        skin_txt(&c, u->med, 17, 0, ox + LW - 34, oy + 214, 2, soft, line);
    }
}

typedef struct {
    const UiItem *it;
} ActPaint;

static void paint_action(Cx *c, void *ud, float ox, float oy)
{
    const ActPaint *p = ud;
    const UiItem *it = p->it;
    Skin *u = c->skin;
    float cy = oy + ACT_SLIP_H * 0.5f, right = ox + ACT_SLIP_W - 30;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    if (it->chevron) {
        skin_icon_chev(c, right - 4, cy, 24, 3.0f, soft);
        right -= 40;
    }
    float det_w = 0;
    if (it->detail && it->detail[0]) {
        char det[64];
        text_fit(u->med, 20, 0, it->detail, 200, det, sizeof det);
        det_w = text_width(u->med, 20, 0, det);
        skin_txtc(c, u->med, 20, 0, right, cy, 2, soft, det);
    }
    float lx = ox + 28, ly = it->sub && it->sub[0] ? cy - 13 : cy;
    char lab[96];
    text_fit(u->semi, 25, 0, it->label, right - (det_w ? det_w + 24 : 0) - lx, lab, sizeof lab);
    skin_txtc(c, u->semi, 25, 0, lx, ly, 0, it->style == ITEM_DIM ? soft : ink, lab);
    if (ly != cy) {
        char sub[96];
        text_fit(u->med, 18, 0, it->sub, right - lx, sub, sizeof sub);
        skin_txtc(c, u->med, 18, 0, lx, cy + 17, 0, soft, sub);
    }
}

typedef struct {
    const UiStorage *s;
    int i;
    uint64_t largest;
} RowPaint;

// a profile's slip (or the others' slip, i == shown): its picture, its name and size, and a strip of tape as long as its share
static void paint_row(Cx *c, void *ud, float ox, float oy)
{
    const RowPaint *p = ud;
    const UiStorage *s = p->s;
    Skin *u = c->skin;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    float cy = oy + ROW_SLIP_H * 0.5f, ax = ox + 50, lx = ox + 92, right = ox + ROW_SLIP_W - 90;
    int others = p->i >= s->shown;
    char name[64], fit[64], a[24];
    uint64_t bytes = 0;
    if (others) {
        snprintf(name, sizeof name, "%d others", s->npeople - s->shown);
        for (int k = s->shown; k < s->npeople; k++) bytes += s->people[k].bytes;
        gfx_circle(c->s, ax, cy, 27, hex(PAL(patch, 0xECE9F1), 1));
        skin_icon_person(c, ax, cy + 1, 31, hex(PAL(thread, PAPER_THREAD), 1));
    } else {
        snprintf(name, sizeof name, "%s", s->people[p->i].name);
        bytes = s->people[p->i].bytes;
        skin_avatar(c, ax, cy, 27, s->people[p->i].name, s->people[p->i].look);
    }
    ui_format_bytes(bytes, a, sizeof a);
    float aw = text_width(u->med, 20, 0, a);
    skin_txtc(c, u->med, 20, 0, right, cy - 14, 2, soft, a);
    text_fit(u->semi, 25, 0, name, right - aw - 24 - lx, fit, sizeof fit);
    skin_txtc(c, u->semi, 25, 0, lx, cy - 14, 0, others ? soft : ink, fit);
    float f = p->largest ? (float)((double)bytes / (double)p->largest) : 0;
    Parts pt;
    if (others) layout_parts(&pt, s->people, s->shown, s->npeople, 0, 1.0f, right - lx, f);
    else layout_parts(&pt, s->people, p->i, p->i + 1, 0, 1.0f, right - lx, f);
    draw_bar(c->s, lx, cy + 7, right - lx, BAR_H, &pt, s->people, 800u + (uint32_t)p->i * 16u);
    if (p->i < 8) {
        g_ui_storage_row_w[p->i] = pt.n ? pt.gap * (pt.n - 1) : 0.0f;
        g_ui_storage_row_min[p->i] = pt.n ? pt.w[0] : 0.0f;
        for (int k = 0; k < pt.n; k++) {
            g_ui_storage_row_w[p->i] += pt.w[k];
            if (pt.w[k] < g_ui_storage_row_min[p->i]) g_ui_storage_row_min[p->i] = pt.w[k];
        }
    }
    skin_icon_chev(c, ox + ROW_SLIP_W - 40, cy, 24, 3.0f, soft);
}

// ---------------------------------------------------------------------------------------------------- the page

static int act_item(int i) { return i; }

static Scene *build(const UiStorage *s, ActPaint *ap, RowPaint *rp)
{
    scene_chrome(G.base, "Storage", NULL, ST_PROMPTS, 2);
    SceneTape sd_tp[2] = {{{96, 26, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 11u}, -18, -16},
                          {{90, 26, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 12u}, LW - 72, -16}};
    scene_card_on(G.base, LX, SD_Y, LW, SD_H, 6.0f, 13u, 0.0f, 0, sd_tp, 2, draw_sd, (void *)s);
    Scene *sc = scene_new(G.base);
    if (!sc) return NULL;
    if (s->nactions > 0) {
        SceneTape tp = {{90, 26, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, 34.0f, 14u}, LW - 72, -16};
        scene_card_on(G.base, LX, ACT_Y, LW, s->nactions * ACT_PITCH + 18, 6.0f, 15u, 0.0f, 0, &tp, 1, NULL, NULL);
    }
    for (int i = 0; i < s->nactions; i++) {
        ap[i].it = &s->actions[i];
        DItem *it = item_add(sc, ACT_SLIP_X, ACT_Y + 14 + i * ACT_PITCH, ACT_SLIP_W, ACT_SLIP_H, 6.0f, LIFT_ROW, paper_lift_k[LIFT_ROW].scale);
        if (!it) continue;
        it->tilt = rad(i & 1 ? -0.3f : 0.3f);
        PaperTape t = {52, 20, paper_tape_rgb[(i * 3 + 5) & 7], PATS[i & 3], PAPER_TAPE_ZIG, -22.0f, 21u + (uint32_t)i};
        float tx = 4.0f, ty = -9.0f; // (put on by hand: no two strips lie alike)
        paper_tape_hand(&t, &tx, &ty, (uint32_t)i * 7919u + 101u, 0.6f);
        item_tape(it, &t, tx, ty);
        IBake ib = {.w = ACT_SLIP_W, .h = ACT_SLIP_H, .r = 6.0f, .card = card_get(ACT_SLIP_W, ACT_SLIP_H, 6.0f, 24u + (uint32_t)i, D_PAD), .paint = paint_action, .pud = &ap[i]};
        item_bake(it, 0, &ib);
    }
    int n = nrows(s);
    {
        float r[4];
        ui_storage_card_rect(s, r);
        SceneTape tp[2] = {{{56, 22, paper_tape_rgb[5], PAT_STRIPES, PAPER_TAPE_ZIG, -40.0f, 16u}, -14, -14},
                                                      {{90, 26, paper_tape_rgb[3], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 18u}, r[2] - 72, -16}};
        scene_card_on(G.base, r[0], r[1], r[2], r[3], 6.0f, 19u, 0.0f, 0, tp, 2, NULL, NULL);
    }
    if (!n) {
        Cx c = ui_cx(G.base);
        skin_txtc(&c, c.skin->med, 21, 0, RX + RW / 2.0f, RY + 50, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), "No profiles yet.");
    }
    uint64_t largest = largest_row(s);
    for (int i = 0; i < n; i++) {
        rp[i].s = s;
        rp[i].i = i;
        rp[i].largest = largest;
        DItem *it = item_add(sc, ROW_SLIP_X, RY + 10 + i * ROW_PITCH, ROW_SLIP_W, ROW_SLIP_H, 6.0f, LIFT_ROW, paper_lift_k[LIFT_ROW].scale);
        if (!it) continue;
        it->tilt = rad(i & 1 ? -0.25f : 0.3f);
        uint32_t rgb = i >= s->shown ? paper_tape_rgb[6] : tint_rgb(s->people[i].tint);
        PaperTape t = {52, 20, rgb, PATS[i & 3], PAPER_TAPE_ZIG, -24.0f, 31u + (uint32_t)i};
        float tx = 4.0f, ty = -9.0f;
        paper_tape_hand(&t, &tx, &ty, (uint32_t)i * 7919u + 211u, 0.6f);
        item_tape(it, &t, tx, ty);
        IBake ib = {.w = ROW_SLIP_W, .h = ROW_SLIP_H, .r = 6.0f, .card = card_get(ROW_SLIP_W, ROW_SLIP_H, 6.0f, 40u + (uint32_t)i, D_PAD), .paint = paint_row, .pud = &rp[i]};
        item_bake(it, 0, &ib);
    }
    return sc;
}

int ui_storage(UiStorage *s)
{
    if (G.quit) return 0;
    int n = nrows(s);
    if (!n) s->in_actions = 1;
    if (s->nactions <= 0) s->in_actions = 0;
    if (s->sel >= n) s->sel = n - 1;
    if (s->sel < 0) s->sel = 0;
    int act = s->act < s->nactions ? s->act : s->nactions - 1; // (kept apart from the profile row: going across and back returns to it)
    if (act < 0) act = 0;
    ActPaint ap[8];
    RowPaint rp[8];
    if (s->nactions > 8 || n > 8) return 0;
    Scene *sc = build(s, ap, rp);
    if (!sc) return 0;
    ui_transition();
    scene_frame(sc, 1);
    int focus = s->in_actions ? act_item(act) : s->nactions + s->sel;
    scene_focus(sc, focus, 0);
    g_ui_storage_focus = s->in_actions ? 10 + act : s->sel;
    int result = -1;
    while (result < 0) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b == UI_QUIT) {
            result = 0;
            break;
        }
        if (b & UI_REDRAW) {
            int cur = sc->cur;
            scene_free(sc);
            sc = build(s, ap, rp);
            if (!sc) return 0;
            scene_frame(sc, 1);
            scene_start_up(sc, cur >= 0 ? cur : focus);
            b &= ~UI_REDRAW;
        }
        int was = s->in_actions, chosen = 0, pdx = 0, pdy = 0, held = G.plat->repeated != 0; // (pdx, pdy: a press toward nothing)
        if (b & UI_TOUCH) { // tap a slip to open it
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                int i = scene_hit(sc, te.x, te.y);
                if (i < 0) continue;
                if (i < s->nactions) {
                    act = i;
                    s->in_actions = chosen = 1;
                } else {
                    s->sel = i - s->nactions;
                    s->in_actions = 0;
                    chosen = 1;
                }
            }
        }
        if (chosen) {
        } else if (s->in_actions) {
            if (b & BTN_DOWN) { if (act < s->nactions - 1) act++; else if (s->nactions > 1 && wrap_ok()) act = 0; else pdy = 1; }
            else if (b & BTN_UP) { if (act > 0) act--; else if (s->nactions > 1 && wrap_ok()) act = s->nactions - 1; else pdy = -1; }
            else if (b & BTN_RIGHT) { if (n) s->in_actions = 0; else pdx = 1; }
            else if (b & BTN_LEFT) pdx = -1;
            else if (b & BTN_A) chosen = 1;
            else if (b & BTN_B) result = 0;
        } else {
            if (b & BTN_DOWN) { if (s->sel < n - 1) s->sel++; else if (n > 1 && wrap_ok()) s->sel = 0; else pdy = 1; }
            else if (b & BTN_UP) { if (s->sel > 0) s->sel--; else if (n > 1 && wrap_ok()) s->sel = n - 1; else pdy = -1; }
            else if (b & BTN_LEFT) { if (s->nactions) s->in_actions = 1; else pdx = -1; }
            else if (b & BTN_RIGHT) pdx = 1;
            else if (b & BTN_A) chosen = 1;
            else if (b & BTN_B) result = 0;
        }
        int want = s->in_actions ? act_item(act) : s->nactions + s->sel;
        g_ui_storage_focus = s->in_actions ? 10 + act : s->sel;
        if (pdx || pdy) scene_lean(sc, want, pdx, pdy);
        if (s->in_actions != was || want != focus) {
            scene_focus(sc, want, held);
            focus = want;
        }
        if (chosen) {
            scene_focus(sc, want, 0);
            scene_press(sc, want);
            result = 1;
        }
    }
    s->act = act;
    scene_free(sc);
    ui_screen_done();
    return result;
}
