// The scene toolkit shared by the Washi screens that are not the home screen or the hub: a still picture (the base) with paper items
// on it (buttons, rows, keys, cards) that lift when they have the focus, each baked once, the whole painted by dirty rectangles.
// Dialogs (ui_dialogs.c), the picture picker, Storage, the reminder wheels and About are built on it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirty.h"
#include "ui_stickfx.h"
#include "ui_scene.h"

// ------------------------------------------------------------------------------------------------ baked pieces (small caches)

const PaperSpr *card_get(int w, int h, float r, uint32_t seed, int pad)
{
    enum { N = 12 };
    static struct {
        int w, h, pad;
        float r;
        uint32_t seed;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: new paper)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].w == w && c[i].h == h && c[i].pad == pad && c[i].r == r && c[i].seed == seed) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].w = w;
    c[k].h = h;
    c[k].pad = pad;
    c[k].r = r;
    c[k].seed = seed;
    c[k].s = paper_card_ex(w, h, r, seed, pad);
    return c[k].s;
}

const PaperSpr *ring_get(float radius, uint32_t seed, const PaperCrayon *st)
{
    enum { N = 12 };
    static struct {
        float r;
        uint32_t seed;
        const PaperCrayon *st;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: new wax)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].r == radius && c[i].seed == seed && c[i].st == st) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].r = radius;
    c[k].seed = seed;
    c[k].st = st;
    c[k].s = paper_ring(radius, st, seed);
    return c[k].s;
}

const PaperSpr *ghost_get(int w, int h, float r)
{
    enum { N = 10 };
    static struct {
        int w, h;
        float r;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: a new board under the patch)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].w == w && c[i].h == h && c[i].r == r) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].w = w;
    c[k].h = h;
    c[k].r = r;
    c[k].s = paper_ghost(w, h, r);
    return c[k].s;
}

// A wide card's shadows are made from a narrow card's: the middle column of each level is repeated, so the blur (the expensive part)
// is done on a small picture (as on the home screen).
static PaperShadows *shadows_make(int w, int h, float r, float grow, int n)
{
    const int wb = 240;
    if (w <= wb + 20) return paper_shadows(w, h, r, grow, n);
    PaperShadows *b = paper_shadows(wb, h, r, grow, n);
    if (!b) return NULL;
    PaperShadows *o = calloc(1, sizeof *o);
    if (!o) {
        paper_shadows_free(b);
        return NULL;
    }
    int Wb = b->s[0]->w, Hb = b->s[0]->h, half = Wb / 2;
    int ins_max = (int)ceilf((w - wb) * (1.0f + grow));
    int W2 = Wb + ins_max;
    if ((W2 - w) & 1) W2++;
    for (int i = 0; i < b->n; i++) {
        float k = 1.0f + grow * i / (float)(b->n - 1);
        int ins = (int)lroundf((w - wb) * k), L0 = (W2 - (Wb + ins)) / 2;
        PaperSpr *s = paper_spr_new(W2, Hb);
        if (!s) {
            paper_shadows_free(o);
            paper_shadows_free(b);
            return NULL;
        }
        s->ox = (W2 - w) / 2;
        s->oy = b->s[0]->oy;
        for (int y = 0; y < Hb; y++) {
            const uint8_t *src = b->s[i]->px + (size_t)y * Wb * 4;
            uint8_t *dst = s->px + (size_t)y * W2 * 4;
            memcpy(dst + (size_t)L0 * 4, src, (size_t)half * 4);
            for (int j = 0; j < ins; j++) memcpy(dst + (size_t)(L0 + half + j) * 4, src + (size_t)half * 4, 4);
            memcpy(dst + (size_t)(L0 + half + ins) * 4, src + (size_t)half * 4, (size_t)(Wb - half) * 4);
        }
        o->s[o->n++] = s;
    }
    paper_shadows_free(b);
    return o;
}

const PaperShadows *shadows_get(int w, int h, float r, float grow)
{
    enum { N = 10 };
    static struct {
        int w, h;
        float r, grow;
        PaperShadows *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: shadows of a new colour)
        for (int i = 0; i < N; i++) paper_shadows_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].w == w && c[i].h == h && c[i].r == r && c[i].grow == grow) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_shadows_free(c[k].s);
    c[k].w = w;
    c[k].h = h;
    c[k].r = r;
    c[k].grow = grow;
    c[k].s = shadows_make(w, h, r, grow, PAPER_SHADOW_LEVELS);
    return c[k].s;
}

// ------------------------------------------------------------------------------------------------ items and the scene

static void draw_item(Surf *s, void *ud)
{
    const IBake *b = ud;
    Cx c = ui_cx(s);
    if (b->fill) { // (a dark slip, Paper night's, has the cards' faint light edge instead of a dark one; on cream every slip, the red one too, has the dark edge)
        int night_slip = !pal_light(g_pal.paper) && !pal_light(b->fill);
        gfx_rrect(s, D_PAD, D_PAD, b->w, b->h, b->r, hex(b->fill, 1));
        gfx_rrect_stroke(s, D_PAD + 0.5f, D_PAD + 0.5f, b->w - 1, b->h - 1, b->r, 1.0f, night_slip ? col(1, 1, 1, 0.06f) : col(0, 0, 0, 0.08f));
    } else if (b->card) {
        paper_spr_put(s, b->card, D_PAD, D_PAD);
    }
    float cx = D_PAD + b->w * 0.5f, cy = D_PAD + b->h * 0.5f;
    if (b->paint) {
        b->paint(&c, b->pud, D_PAD, D_PAD);
    } else if (b->glyph) {
        float gw = skin_glyph_w(b->glyph), lw = text_width(b->font, b->px, 0, b->label);
        float x0 = cx - (gw + 12 + lw) * 0.5f, gx = x0 + gw * 0.5f;
        // the letter's disc is a pastel one: its letter and ring stay dark when the label beside it is pale (on a dark slip), as the footer's do
        int disc_light = pal_light(b->gfill);
        Col gk = disc_light && col_light(b->ink) ? hex(PAL(glyph_ink, PAPER_TEXT), 1) : b->ink;
        Col gr = disc_light && col_light(b->gring) ? hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1) : b->gring;
        gfx_circle(s, gx, cy, 14, hex(b->gfill, 1));
        gfx_ring(s, gx, cy, 13.75f, 2.4f, gr);
        skin_txtc(&c, c.skin->semi, 16, 0, gx, cy, 1, gk, b->glyph);
        skin_txtc(&c, b->font, b->px, 0, x0 + gw + 12, cy, 0, b->ink, b->label);
    } else if (b->label && b->sub) {
        skin_txtc(&c, b->font, b->px, 0, cx, cy - 13, 1, b->ink, b->label);
        skin_txtc(&c, c.skin->med, 16.5f, 0, cx, cy + 17, 1, c.skin->th.text2, b->sub);
    } else if (b->label) {
        skin_txtc(&c, b->font, b->px, 0, cx, cy, 1, b->ink, b->label);
    }
}

typedef struct {
    const PaperSpr *sh0, *body, *tape;
    int w, h;
    float tdx, tdy, tilt;
} RestBake;

static void draw_rest(Surf *s, void *ud)
{
    const RestBake *b = ud;
    float cx = D_RPAD + b->w * 0.5f, cy = D_RPAD + b->h * 0.5f;
    if (b->sh0) paper_spr_put(s, b->sh0, D_RPAD, D_RPAD);
    if (b->tilt == 0.0f) gfx_blit_over(s, b->body->px, b->body->w, b->body->h, D_RPAD - D_PAD, D_RPAD - D_PAD);
    else paper_spr_xform(s, b->body, cx, cy, 1.0f, b->tilt, 1.0f);
    if (b->tape) {
        PaperXf cf = {cx, cy, 1.0f, b->tilt};
        PaperXf t = paper_xf_attach(cf, b->tdx, b->tdy);
        paper_spr_xform(s, b->tape, t.x, t.y, 1.0f, t.angle, 1.0f);
    }
}

static void sprs_free(Sprs *o)
{
    if (o->rest != o->body) paper_spr_free(o->rest);
    paper_spr_free(o->body);
    memset(o, 0, sizeof *o);
}

Scene *scene_new(Surf *base)
{
    Scene *sc = calloc(1, sizeof *sc);
    if (!sc) return NULL;
    sc->base = base;
    sc->cur = -1;
    return sc;
}

void item_free(DItem *it)
{
    sprs_free(&it->sp[0]);
    sprs_free(&it->sp[1]);
    paper_spr_free(it->blend);
    paper_spr_free(it->tape);
    paper_peel_free(it->peel);
    it->blend = it->tape = NULL;
    it->peel = NULL;
}

void scene_free(Scene *sc)
{
    if (!sc) return;
    for (int i = 0; i < sc->n; i++) item_free(&sc->it[i]);
    free(sc);
}

DItem *item_add(Scene *sc, float x, float y, float w, float h, float r, int lk, float grow)
{
    if (sc->n >= SC_MAX) return NULL;
    DItem *it = &sc->it[sc->n++];
    memset(it, 0, sizeof *it);
    it->active = 1;
    it->x = (float)lroundf(x);
    it->y = (float)lroundf(y);
    it->w = w;
    it->h = h;
    it->r = r;
    it->lk = lk;
    it->grow = grow;
    paper_lift_init(&it->lift, lk);
    it->lift.tapeless = 1; // (until item_tape sticks a strip on it)
    it->sh = shadows_get((int)w, (int)h, r, grow);
    it->ghost = ghost_get((int)w, (int)h, r + 1.0f);
    return it;
}

PaperSpr *body_bake(const DItem *it, const IBake *ib)
{
    return paper_bake((int)it->w + 2 * D_PAD, (int)it->h + 2 * D_PAD, draw_item, (void *)ib);
}

// bakes look `var` of the item
void item_bake(DItem *it, int var, const IBake *ib)
{
    Sprs *o = &it->sp[var];
    sprs_free(o);
    o->body = body_bake(it, ib);
    if (!o->body) return;
    RestBake rb = {it->sh ? it->sh->s[0] : NULL, o->body, it->tape, (int)it->w, (int)it->h, it->tdx, it->tdy, it->tilt};
    o->rest = paper_bake((int)it->w + 2 * D_RPAD, (int)it->h + 2 * D_RPAD, draw_rest, &rb);
    o->rox = o->roy = D_RPAD;
    if (!o->rest) o->rest = o->body, o->rox = o->roy = D_PAD;
    it->force = 1;
}

// a strip across the item: its top left corner at (tx, ty) from the item's top left
void item_tape(DItem *it, const PaperTape *t, float tx, float ty)
{
    PaperTape th = *t;
    paper_tape_host(&th, it->w, it->h, it->r, tx, ty);
    it->tape = paper_tape(&th);
    it->lift.tapeless = it->tape == NULL;
    it->peel = paper_tape_peel_lazy(&th, PAPER_PEEL_STATES);
    paper_tape_offset(it->w, it->h, tx, ty, (float)t->len, (float)t->wid, &it->tdx, &it->tdy);
}

// ---- poses and what each covers

typedef struct { int x0, y0, x1, y1; } Box;

static void box_add(Box *b, float x0, float y0, float x1, float y1)
{
    int ax = (int)floorf(x0) - 1, ay = (int)floorf(y0) - 1, bx = (int)ceilf(x1) + 1, by = (int)ceilf(y1) + 1;
    if (b->x0 >= b->x1) {
        b->x0 = ax; b->y0 = ay; b->x1 = bx; b->y1 = by;
        return;
    }
    if (ax < b->x0) b->x0 = ax;
    if (ay < b->y0) b->y0 = ay;
    if (bx > b->x1) b->x1 = bx;
    if (by > b->y1) b->y1 = by;
}

static void box_xform(Box *b, float cx, float cy, float w, float h, float sc, float ang)
{
    float ca = fabsf(cosf(ang)), sa = fabsf(sinf(ang));
    float ex = (ca * w + sa * h) * 0.5f * sc + 2.0f, ey = (sa * w + ca * h) * 0.5f * sc + 2.0f;
    box_add(b, cx - ex, cy - ey, cx + ex, cy + ey);
}

static void shadow_pos(const DItem *it, const PaperSpr *sp, int *ax, int *ay)
{
    *ax = (int)lroundf(it->cx - it->w * 0.5f) - sp->ox;
    *ay = (int)lroundf(it->cy - it->h * 0.5f) - sp->oy;
}

static void item_bounds(DItem *it)
{
    Box b = {0, 0, 0, 0};
    memset(&it->bb, 0, sizeof it->bb);
    if (!it->active) return;
    const Sprs *sp = &it->sp[it->var];
    if (!it->moving) {
        const PaperSpr *rest = sp->rest;
        if (rest) box_add(&b, it->x - sp->rox, it->y - sp->roy, it->x - sp->rox + rest->w, it->y - sp->roy + rest->h);
    } else {
        if (sp->body) box_xform(&b, it->cx, it->cy, (float)sp->body->w, (float)sp->body->h, it->sc, it->ang);
        if (it->sh) {
            const PaperSpr *s = it->sh->s[paper_shadow_level(it->sh, it->ps.p)];
            int ax, ay;
            shadow_pos(it, s, &ax, &ay);
            box_add(&b, (float)ax, (float)ay, (float)(ax + s->w), (float)(ay + s->h));
        }
        if (it->ghost) box_xform(&b, it->x + it->w * 0.5f, it->y + it->h * 0.5f, (float)it->ghost->w, (float)it->ghost->h, 1.0f, it->tilt);
        if (it->tape) {
            PaperXf cf = {it->cx, it->cy, it->sc, it->ang}, t = paper_xf_attach(cf, it->tdx, it->tdy);
            const PaperSpr *ts = it->peel ? it->peel->s[0] : it->tape;
            box_xform(&b, t.x, t.y, (float)ts->w, (float)ts->h, t.scale, t.angle);
        }
    }
    it->bb.x = b.x0;
    it->bb.y = b.y0;
    it->bb.w = b.x1 - b.x0;
    it->bb.h = b.y1 - b.y0;
}

static void item_pose(DItem *it, double now, int lean_on, int lx, int ly, int *anim)
{
    it->lx = it->ly = 0;
    memset(&it->ps, 0, sizeof it->ps);
    it->ps.scale = 1.0f;
    it->moving = 0;
    if (!it->active) return;
    paper_lift_pose(&it->lift, now, it->w, &it->ps);
    it->ps.scale += (it->grow - paper_lift_k[it->lk].scale) * it->ps.p;
    if (lean_on) {
        it->lx = (float)lx;
        it->ly = (float)ly;
    }
    int busy = paper_lift_busy(&it->lift, now) || lean_on || (it->mix > 0.0f && it->mix < 1.0f);
    it->moving = it->ps.p > 0.002f || busy;
    if (busy) *anim = 1;
    it->cx = it->x + it->w * 0.5f + it->ps.x + it->lx;
    it->cy = it->y + it->h * 0.5f + it->ps.y + it->ly;
    it->sc = it->ps.scale;
    it->ang = it->tilt + it->ps.angle;
}

// ---- painting

int rect_hit(const Rect *a, const Rect *b)
{
    return a->w > 0 && b->w > 0 && a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

void put(Surf *s, const PaperSpr *p, int x, int y, int ox, int oy)
{
    gfx_blit_over(s, p->px, p->w, p->h, x - ox, y - oy);
}

static void draw_resting(Surf *s, int ox, int oy, const DItem *it)
{
    const Sprs *sp = &it->sp[it->var];
    if (sp->rest) put(s, sp->rest, (int)it->x - sp->rox, (int)it->y - sp->roy, ox, oy);
}

static void draw_lifted(Surf *s, int ox, int oy, const DItem *it)
{
    const Sprs *sp = &it->sp[it->var];
    if (it->sh) {
        const PaperSpr *sh = it->sh->s[paper_shadow_level(it->sh, it->ps.p)];
        int ax, ay;
        shadow_pos(it, sh, &ax, &ay);
        put(s, sh, ax, ay, ox, oy);
    }
    if (sp->body) paper_spr_xform(s, sp->body, it->cx - ox, it->cy - oy, it->sc, it->ang, 1.0f);
    if (it->blend && it->mix > 0.002f) paper_spr_xform(s, it->blend, it->cx - ox, it->cy - oy, it->sc, it->ang, it->mix);
    if (it->tape) {
        PaperXf cf = {it->cx, it->cy, it->sc, it->ang}, t = paper_xf_attach(cf, it->tdx, it->tdy);
        const PaperSpr *ts = it->peel ? paper_peel_at(it->peel, it->ps.p) : it->tape;
        if (ts) paper_spr_xform(s, ts, t.x - ox, t.y - oy, t.scale, t.angle, 1.0f);
    }
}

static void paint_items(Scene *sc, Surf *s, const Rect *R)
{
    int ox = R->x, oy = R->y;
    for (int i = 0; i < sc->n; i++) { // everything lying still
        DItem *it = &sc->it[i];
        if (it->active && !it->moving && rect_hit(&it->bb, R)) draw_resting(s, ox, oy, it);
    }
    for (int i = 0; i < sc->n; i++) { // the patches left where things were lifted
        DItem *it = &sc->it[i];
        if (it->active && it->moving && it->ghost && it->ps.ghost > 0.004f && rect_hit(&it->bb, R))
            paper_spr_xform(s, it->ghost, it->x + it->w * 0.5f - ox, it->y + it->h * 0.5f - oy, 1.0f, it->tilt, it->ps.ghost);
    }
    int order[SC_MAX], n = 0;
    for (int i = 0; i < sc->n; i++)
        if (sc->it[i].active && sc->it[i].moving) order[n++] = i;
    for (int a = 1; a < n; a++) { // the lowest first (insertion sort, stable)
        int v = order[a], b = a;
        while (b > 0 && sc->it[order[b - 1]].ps.p > sc->it[v].ps.p) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    for (int k = 0; k < n; k++) {
        DItem *it = &sc->it[order[k]];
        if (rect_hit(&it->bb, R)) draw_lifted(s, ox, oy, it);
    }
}

static Surf *scratch_for(int w, int h)
{
    static Surf scr;
    if (!scr.px) scr.px = malloc((size_t)SCREEN_W * SCREEN_H * 4);
    scr.w = w;
    scr.h = h;
    return &scr;
}

// the rectangle R of `dst` made anew: the base, what the scene draws over it, and every item that touches it
static void paint_rect(Scene *sc, Surf *dst, const Rect *R)
{
    Surf *scr = scratch_for(R->w, R->h);
    if (!scr->px) return;
    for (int y = 0; y < R->h; y++)
        memcpy(scr->px + (size_t)y * R->w * 4, sc->base->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, (size_t)R->w * 4);
    if (sc->extra) sc->extra(sc->extra_ud, scr, R->x, R->y, R);
    paint_items(sc, scr, R);
    for (int y = 0; y < R->h; y++)
        memcpy(dst->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, scr->px + (size_t)y * R->w * 4, (size_t)R->w * 4);
}

void scene_inval(Scene *sc, int x, int y, int w, int h)
{
    if (sc->ninval < 8) {
        Rect r = {x, y, w, h};
        sc->inval[sc->ninval++] = r;
    } else {
        sc->force_full = 1;
    }
}

static void pose_key(const DItem *it, float *k)
{
    k[0] = (float)it->active;
    k[1] = (float)it->moving;
    k[2] = it->ps.p;
    k[3] = it->cx;
    k[4] = it->cy;
    k[5] = it->sc;
    k[6] = it->ang;
    k[7] = it->ps.ghost;
    k[8] = (float)it->var;
    k[9] = it->mix;
    k[10] = it->tape && it->peel ? it->ps.p : 0.0f;
    k[11] = 0;
}

// Makes this frame: poses, what changed, the changed rectangles painted into G.work and shown. first: the whole screen, as a full
// present (it plays the dialog's rise). Returns 1 while anything is moving.
int scene_frame(Scene *sc, int first)
{
    double now = (double)ui_now();
    float lox, loy;
    int lean_on = bump_offset(&sc->lean, &lox, &loy);
    int lx = (int)lroundf(lox), ly = (int)lroundf(loy), anim = lean_on;
    for (int i = 0; i < sc->n; i++) {
        item_pose(&sc->it[i], now, lean_on && i == sc->lean_item, lx, ly, &anim);
        item_bounds(&sc->it[i]);
    }
    Dirty d;
    dirty_clear(&d);
    if (first || sc->force_full) {
        dirty_all(&d);
    } else {
        for (int i = 0; i < sc->n; i++) {
            DItem *it = &sc->it[i];
            pose_key(it, it->pk);
            int changed = it->force || !!it->active != !!it->drawn || (it->active && memcmp(it->pk, it->pk_prev, sizeof it->pk));
            if (!changed) continue;
            if (it->drawn) dirty_add_rect(&d, it->bb_prev);
            if (it->active) dirty_add_rect(&d, it->bb);
        }
        for (int i = 0; i < sc->ninval; i++) dirty_add_rect(&d, sc->inval[i]);
    }
    if (d.full || d.n) {
        Rect full = {0, 0, SCREEN_W, SCREEN_H};
        if (d.full) {
            paint_rect(sc, G.work, &full);
            if (first) ui_present(G.work);
            else ui_present_rects(G.work, &full, 1);
        } else {
            for (int i = 0; i < d.n; i++) paint_rect(sc, G.work, &d.r[i]);
            ui_present_rects(G.work, d.r, d.n);
        }
    }
    for (int i = 0; i < sc->n; i++) {
        DItem *it = &sc->it[i];
        pose_key(it, it->pk_prev);
        it->bb_prev = it->bb;
        it->drawn = it->active;
        it->force = 0;
    }
    sc->ninval = 0;
    sc->force_full = 0;
    return anim;
}

// the focus moves: the old one sticks back down, the new one peels up
void scene_focus(Scene *sc, int want, int held)
{
    double now = (double)ui_now();
    if (want == sc->cur) return;
    if (sc->cur >= 0) paper_lift_release(&sc->it[sc->cur].lift, now, held);
    if (want >= 0) paper_lift_grab(&sc->it[want].lift, now, held);
    sc->cur = want;
}

// starts with item i already up and its wiggle done
void scene_start_up(Scene *sc, int i)
{
    PaperLift *l = &sc->it[i].lift;
    l->target = 1;
    l->from = 1;
    l->dur = 0;
    l->wig_t0 = -1;
    l->wig_rest = -0.4f;
    sc->cur = i;
}

long g_ui_key_wiggles;

// the item i pushes flat for a moment (an A press or tap, just before what it does). A key of the keypad or keyboard goes in further, and wiggles as it
// comes back up: the same wiggle it made when the focus landed on it, so that a press can be seen to have registered.
void scene_press(Scene *sc, int i)
{
    if (i < 0 || i >= sc->n || !sc->it[i].active) return;
    DItem *it = &sc->it[i];
    // what stays put under the item (the card, whatever the scene draws on it, the other items): the scene made over again in the rectangle the press works
    // in, with this item left out. Only what the frame has over that moves; the screen's own G.base knows nothing of a dialog's card.
    Surf *over = surf_new(SCREEN_W, SCREEN_H);
    if (over) {
        Rect pr = {it->bb.x - 12, it->bb.y - 12, it->bb.w + 32, it->bb.h + 36};
        if (pr.x < 0) pr.x = 0;
        if (pr.y < 0) pr.y = 0;
        if (pr.x + pr.w > SCREEN_W) pr.w = SCREEN_W - pr.x;
        if (pr.y + pr.h > SCREEN_H) pr.h = SCREEN_H - pr.y;
        it->active = 0;
        paint_rect(sc, over, &pr);
        it->active = 1;
    }
    float scale = it->lk == LIFT_KEY ? 0.93f : 0.99f;
    if (it->ghost) {
        ui_press_ghost(it->ghost, it->x + it->w * 0.5f, it->y + it->h * 0.5f, it->tilt, it->ps.ghost); // (the dashed patch where it sat stays put)
        if (it->moving) { // (and what of it the lifted card covers stays covered: the card as drawn, without the box's margins)
            float ca = fabsf(cosf(it->ang)), sa = fabsf(sinf(it->ang));
            float ex = (ca * it->w + sa * it->h) * 0.5f * it->sc, ey = (sa * it->w + ca * it->h) * 0.5f * it->sc;
            int x0 = (int)ceilf(it->cx - ex), y0 = (int)ceilf(it->cy - ey), x1 = (int)floorf(it->cx + ex), y1 = (int)floorf(it->cy + ey);
            ui_press_cover((Rect){x0, y0, x1 - x0, y1 - y0});
        }
    }
    ui_press_over(G.work, it->bb, scale, over);
    if (over) surf_free(over);
    if (it->lk != LIFT_KEY) return;
    paper_lift_wiggle_again(&it->lift, (double)ui_now());
    g_ui_key_wiggles++;
}

int scene_hit(const Scene *sc, float x, float y)
{
    for (int i = 0; i < sc->n; i++) {
        const DItem *it = &sc->it[i];
        if (it->active && in_rect(x, y, it->x, it->y, it->w, it->h)) return i;
    }
    return -1;
}

// a D-pad press as a direction to lean (0, 0 if there was none)
int lean_dir(uint64_t b, int *dx, int *dy)
{
    *dx = (b & BTN_RIGHT) ? 1 : (b & BTN_LEFT) ? -1 : 0;
    *dy = *dx ? 0 : (b & BTN_DOWN) ? 1 : (b & BTN_UP) ? -1 : 0;
    return *dx || *dy;
}

void scene_lean(Scene *sc, int item, int dx, int dy)
{
    bump_start(&sc->lean, dx, dy);
    sc->lean_item = item;
}

// ------------------------------------------------------------------------------------------------ page pieces
// What the pages built on scenes share: the title slip, and a taped card laid on the board.

typedef struct {
    const PaperSpr *card;
    const char *text;
} TagBake;

static void draw_tag(Surf *s, void *ud)
{
    const TagBake *b = ud;
    Cx c = ui_cx(s);
    paper_spr_put(s, b->card, D_PAD, D_PAD);
    skin_txtc(&c, c.skin->semi, 30, 0, D_PAD + 28, D_PAD + SCENE_TAG_H / 2 + 0.5f, 0, hex(PAL(text, PAPER_TEXT), 1), b->text);
}

float scene_title_tag(Surf *dst, const char *title, float x, float y)
{
    char fit[160];
    text_fit(G.skin.semi, 30, 0, title, 860, fit, sizeof fit);
    int w = (int)ceilf(text_width(G.skin.semi, 30, 0, fit)) + 56;
    w += w & 1;
    TagBake tb = {card_get(w, SCENE_TAG_H, 6.0f, 43u, D_PAD), fit};
    const PaperShadows *sh = shadows_get(w, SCENE_TAG_H, 6.0f, 0.0f);
    PaperSpr *body = paper_bake(w + 2 * D_PAD, SCENE_TAG_H + 2 * D_PAD, draw_tag, &tb);
    if (sh) paper_spr_put(dst, sh->s[0], (int)x, (int)y);
    if (body) paper_spr_xform(dst, body, x + w * 0.5f, y + SCENE_TAG_H * 0.5f, 1.0f, 0.6f * 3.14159265f / 180.0f, 1.0f);
    paper_spr_free(body);
    return (float)w;
}

void scene_chrome(Surf *dst, const char *title, const char *sub, const Prompt *prompts, int nprompts)
{
    Cx c = ui_cx(dst);
    skin_board(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, NULL, &st);
    if (title) {
        float w = scene_title_tag(dst, title, 52.0f, 104.0f);
        if (sub && sub[0]) {
            char fit[120];
            text_fit(G.skin.med, 19, 0, sub, 400, fit, sizeof fit);
            skin_txtc(&c, G.skin.med, 19, 0, 52.0f + w + 20, 104.0f + SCENE_TAG_H / 2, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), fit);
        }
    }
    if (prompts && nprompts > 0) skin_footer(&c, prompts, nprompts);
}

typedef struct {
    const PaperSpr *card;
    SceneContent content;
    void *ud;
    const SceneTape *tp;
    int ntp;
    PaperSpr *tapes[4];
    float w, h;
} CardOn;

static void draw_card_on(Surf *s, void *ud)
{
    const CardOn *b = ud;
    paper_spr_put(s, b->card, SCENE_PAD, SCENE_PAD);
    if (b->content) b->content(s, SCENE_PAD, SCENE_PAD, b->ud);
    for (int i = 0; i < b->ntp; i++)
        if (b->tapes[i]) paper_spr_put(s, b->tapes[i], SCENE_PAD + (int)(b->tp[i].x + b->tp[i].t.len * 0.5f), SCENE_PAD + (int)(b->tp[i].y + b->tp[i].t.wid * 0.5f));
}

void scene_card_on(Surf *dst, float x, float y, float w, float h, float r, uint32_t seed, float tilt_deg, int shadow, const SceneTape *tp, int ntp,
                   SceneContent content, void *ud)
{
    CardOn b;
    memset(&b, 0, sizeof b);
    b.card = card_get((int)w, (int)h, r, seed, SCENE_PAD);
    b.content = content;
    b.ud = ud;
    b.tp = tp;
    b.ntp = ntp > 4 ? 4 : ntp;
    b.w = w;
    b.h = h;
    for (int i = 0; i < b.ntp; i++) {
        PaperTape th = tp[i].t;
        paper_tape_host(&th, w, h, r, tp[i].x, tp[i].y); // (stuck over the card's edge: lightly pressed, not hugging it)
        b.tapes[i] = paper_tape(&th);
    }
    PaperSpr *body = paper_bake((int)w + 2 * SCENE_PAD, (int)h + 2 * SCENE_PAD, draw_card_on, &b);
    const PaperShadows *sh = shadows_get((int)w, (int)h, r, 0.0f);
    if (sh) paper_spr_put(dst, sh->s[shadow ? sh->n - 1 : 0], (int)x, (int)y);
    if (body) paper_spr_xform(dst, body, x + w * 0.5f, y + h * 0.5f, 1.0f, tilt_deg * 3.14159265f / 180.0f, 1.0f);
    paper_spr_free(body);
    for (int i = 0; i < b.ntp; i++) paper_spr_free(b.tapes[i]);
}

// A dashed rounded rectangle, `width` thick, walked along its outline in dashes of `dash` with `gap` between them.
void scene_dashed_rrect(Surf *s, float x, float y, float w, float h, float r, float width, float dash, float gap, Col k)
{
    float L[4] = {w - 2 * r, h - 2 * r, w - 2 * r, h - 2 * r}, arc = 1.5707963f * r, total = 2 * (L[0] + L[1]) + 4 * arc;
    float step = dash + gap, off = 0;
    int n = (int)(total / step);
    if (n < 4) n = 4;
    step = total / n; // a whole number of dashes round, so the pattern closes
    float d = dash * step / (dash + gap);
    for (int i = 0; i < n; i++) {
        float p0 = i * step + off, p1 = p0 + d, pts[2][2];
        for (int e = 0; e < 2; e++) {
            float p = e ? p1 : p0, q = p;
            // walk: top edge, top-right arc, right edge, bottom-right arc, bottom edge, bottom-left arc, left edge, top-left arc
            float segs[8] = {L[0], arc, L[1], arc, L[2], arc, L[3], arc};
            int k2 = 0;
            while (k2 < 8 && q > segs[k2]) { q -= segs[k2]; k2++; }
            float px, py;
            switch (k2 % 8) {
            case 0: px = x + r + q; py = y; break;
            case 1: { float a = -1.5707963f + q / r; px = x + w - r + cosf(a) * r; py = y + r + sinf(a) * r; break; }
            case 2: px = x + w; py = y + r + q; break;
            case 3: { float a = q / r; px = x + w - r + cosf(a) * r; py = y + h - r + sinf(a) * r; break; }
            case 4: px = x + w - r - q; py = y + h; break;
            case 5: { float a = 1.5707963f + q / r; px = x + r + cosf(a) * r; py = y + h - r + sinf(a) * r; break; }
            case 6: px = x; py = y + h - r - q; break;
            default: { float a = 3.14159265f + q / r; px = x + r + cosf(a) * r; py = y + r + sinf(a) * r; break; }
            }
            pts[e][0] = px;
            pts[e][1] = py;
        }
        gfx_line(s, pts[0][0], pts[0][1], pts[1][0], pts[1][1], width, k);
    }
}

typedef struct {
    const char *label;
    int idx;
    float w;
} OrderChip;

// the order chip as a still slip: Home's, with the R3 mark and the order's name (the pages of Extra icons)
static void order_chip_content(Surf *s, float ox, float oy, void *ud)
{
    const OrderChip *o = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    float cy = oy + 24;
    gfx_circle(s, ox + CHIP_R3_CX, cy, 15, PAL(glyph_flat, 0) ? u->th.panel : hex(paper_tape_rgb[5], 1));
    gfx_ring(s, ox + CHIP_R3_CX, cy, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
    skin_txtc(&c, u->semi, 12.5f, 0, ox + CHIP_R3_CX, cy, 1, hex(PAL(glyph_ink, PAPER_TEXT), 1), "R3");
    stk_sticker_toned(&c, ox + STK_X, oy + STK_Y, stk_w(o->w), STK_H, o->label, o->idx);
}

void ui_order_chip(Surf *s, float x, float y, float w, const char *label, int idx)
{
    OrderChip o = {label, idx, w};
    scene_card_on(s, x, y, w, 48, 6.0f, 17u, 0.0f, 0, NULL, 0, order_chip_content, &o);
}

int ui_keep_outside(UiKeep *k, const Surf *s, int x0, int y0, int x1, int y1, int wy0, int wy1)
{
    memset(k, 0, sizeof *k);
    if (x0 < 0) x0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y0 < 0) y0 = 0;
    if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return 1;
    k->x0 = x0;
    k->w = x1 - x0;
    k->ta = y0; // the rows above the window, and below it
    k->nt = (y1 < wy0 ? y1 : wy0) - y0;
    k->ba = y0 > wy1 ? y0 : wy1;
    k->nb = y1 - k->ba;
    if (k->nt < 0) k->nt = 0;
    if (k->nb < 0) k->nb = 0;
    if (!(k->nt + k->nb)) return 1;
    k->keep = malloc((size_t)k->w * (k->nt + k->nb) * 4);
    if (!k->keep) return 0; // (better no mark than one over what is round the window)
    for (int r = 0; r < k->nt; r++) memcpy(k->keep + (size_t)r * k->w * 4, s->px + ((size_t)(k->ta + r) * s->w + k->x0) * 4, (size_t)k->w * 4);
    for (int r = 0; r < k->nb; r++) memcpy(k->keep + (size_t)(k->nt + r) * k->w * 4, s->px + ((size_t)(k->ba + r) * s->w + k->x0) * 4, (size_t)k->w * 4);
    return 1;
}

void ui_put_back(UiKeep *k, Surf *s)
{
    if (!k->keep) return;
    for (int r = 0; r < k->nt; r++) memcpy(s->px + ((size_t)(k->ta + r) * s->w + k->x0) * 4, k->keep + (size_t)r * k->w * 4, (size_t)k->w * 4);
    for (int r = 0; r < k->nb; r++) memcpy(s->px + ((size_t)(k->ba + r) * s->w + k->x0) * 4, k->keep + (size_t)(k->nt + r) * k->w * 4, (size_t)k->w * 4);
    free(k->keep);
    k->keep = NULL;
}

int ui_keep_cell_mark(UiKeep *k, const Surf *s, int x0, int y0, int x1, int y1, float cy0, float cy1, int wy0, int wy1)
{
    return ui_keep_outside(k, s, x0, y0, x1, y1, cy0 >= (float)wy0 ? 0 : wy0, cy1 <= (float)wy1 ? s->h : wy1);
}

int ui_grid_nav(int sel, int count, int cols, int dx, int dy)
{
    if (count <= 0 || cols <= 0 || sel < 0 || sel >= count) return -1;
    const int row = sel / cols, col = sel % cols, last_row = (count - 1) / cols;
    const int row_end = row == last_row ? count - 1 : row * cols + cols - 1; // (the last cell of this row)
    int to = -1;
    if (dx > 0) to = sel < row_end ? sel + 1 : row * cols;
    else if (dx < 0) to = col > 0 ? sel - 1 : row_end;
    else if (dy > 0) {
        if (sel + cols < count) to = sel + cols;
        else if (row < last_row) to = count - 1; // (no cell under it in the short last row: the last one)
        else to = col;                            // (round to the top of the column)
    } else if (dy < 0) {
        if (row > 0) to = sel - cols;
        else to = last_row * cols + col < count ? last_row * cols + col : (last_row - 1) * cols + col; // (round to the bottom of the column)
    }
    return to == sel ? -1 : to;
}
