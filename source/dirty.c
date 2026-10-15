#include "dirty.h"

#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

// share an area, or a side of some length (touching at a corner only does not count)
static int joinable(Rect a, Rect b)
{
    int ox = imin(a.x + a.w, b.x + b.w) - imax(a.x, b.x);
    int oy = imin(a.y + a.h, b.y + b.h) - imax(a.y, b.y);
    return ox >= 0 && oy >= 0 && (ox > 0 || oy > 0);
}

static Rect bbox(Rect a, Rect b)
{
    Rect r;
    r.x = imin(a.x, b.x);
    r.y = imin(a.y, b.y);
    r.w = imax(a.x + a.w, b.x + b.w) - r.x;
    r.h = imax(a.y + a.h, b.y + b.h) - r.y;
    return r;
}

static void remove_at(Dirty *d, int j)
{
    memmove(d->r + j, d->r + j + 1, (size_t)(d->n - j - 1) * sizeof d->r[0]);
    d->n--;
}

// joins every pair that overlaps or shares a side, until none is left (a join can reach a third one)
static void join_all(Dirty *d)
{
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < d->n && !again; i++)
            for (int j = i + 1; j < d->n; j++)
                if (joinable(d->r[i], d->r[j])) {
                    d->r[i] = bbox(d->r[i], d->r[j]);
                    remove_at(d, j);
                    again = 1;
                    break;
                }
    }
}

static void settle(Dirty *d)
{
    join_all(d);
    while (d->n > DIRTY_MAX) { // too many: join the two that cost the fewest extra pixels
        int bi = 0, bj = 1;
        long best = -1;
        for (int i = 0; i < d->n; i++)
            for (int j = i + 1; j < d->n; j++) {
                Rect b = bbox(d->r[i], d->r[j]);
                long waste = (long)b.w * b.h - (long)d->r[i].w * d->r[i].h - (long)d->r[j].w * d->r[j].h;
                if (best < 0 || waste < best) {
                    best = waste;
                    bi = i;
                    bj = j;
                }
            }
        d->r[bi] = bbox(d->r[bi], d->r[bj]);
        remove_at(d, bj);
        join_all(d);
    }
    for (int i = 0; i < d->n; i++)
        if (d->r[i].w >= SCREEN_W && d->r[i].h >= SCREEN_H) { // one rectangle is everything: say so
            dirty_all(d);
            return;
        }
}

void dirty_clear(Dirty *d)
{
    d->n = 0;
    d->full = 0;
}

void dirty_all(Dirty *d)
{
    d->n = 0;
    d->full = 1;
}

void dirty_add_rect(Dirty *d, Rect r)
{
    if (d->full) return;
    long x0 = r.x, y0 = r.y, x1 = (long)r.x + r.w, y1 = (long)r.y + r.h; // (long: a wild rectangle must not overflow)
    if (r.w <= 0 || r.h <= 0) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x1 <= x0 || y1 <= y0) return;
    Rect c = {(int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0)};
    d->r[d->n++] = c;
    settle(d);
}

void dirty_add(Dirty *d, int x, int y, int w, int h)
{
    Rect r = {x, y, w, h};
    dirty_add_rect(d, r);
}

void dirty_union(Dirty *d, const Dirty *o)
{
    if (o->full) {
        dirty_all(d);
        return;
    }
    for (int i = 0; i < o->n; i++) dirty_add_rect(d, o->r[i]);
}

long dirty_area(const Dirty *d)
{
    if (d->full) return (long)SCREEN_W * SCREEN_H;
    long a = 0;
    for (int i = 0; i < d->n; i++) a += (long)d->r[i].w * d->r[i].h;
    return a;
}

int dirty_has(const Dirty *d, int x, int y)
{
    if (d->full) return x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H;
    for (int i = 0; i < d->n; i++)
        if (x >= d->r[i].x && y >= d->r[i].y && x < d->r[i].x + d->r[i].w && y < d->r[i].y + d->r[i].h) return 1;
    return 0;
}

// ---- framebuffer history

void fbsim_init(FbSim *f, int images)
{
    memset(f, 0, sizeof *f);
    f->images = images < 1 ? 1 : images > 3 ? 3 : images;
}

int fbsim_slot(const FbSim *f)
{
    return (int)(f->presents % f->images);
}

void fbsim_present(FbSim *f, const Dirty *now, Dirty *copy)
{
    f->hist[f->presents % 3] = *now;
    f->presents++;
    dirty_clear(copy);
    if (f->presents <= f->images) { // some image has never been written: it holds nothing of ours
        dirty_all(copy);
        return;
    }
    for (int i = 0; i < f->images && !copy->full; i++) dirty_union(copy, &f->hist[(f->presents - 1 - i) % 3]);
}

void dirty_copy(const Surf *frame, uint8_t *dst, size_t stride, const Dirty *d)
{
    size_t row = (size_t)frame->w * 4;
    if (d->full) {
        if (stride == row) {
            memcpy(dst, frame->px, row * (size_t)frame->h);
        } else {
            for (int y = 0; y < frame->h; y++) memcpy(dst + (size_t)y * stride, frame->px + (size_t)y * row, row);
        }
        return;
    }
    for (int i = 0; i < d->n; i++) {
        const Rect *r = &d->r[i];
        size_t bytes = (size_t)r->w * 4;
        for (int y = r->y; y < r->y + r->h; y++)
            memcpy(dst + (size_t)y * stride + (size_t)r->x * 4, frame->px + (size_t)y * row + (size_t)r->x * 4, bytes);
    }
}

long dirty_violations(const Surf *prev, const Surf *frame, const Rect *r, int n)
{
    long bad = 0;
    size_t row = (size_t)frame->w * 4;
    for (int y = 0; y < frame->h; y++) {
        const uint8_t *a = frame->px + (size_t)y * row, *b = prev->px + (size_t)y * row;
        if (!memcmp(a, b, row)) continue; // (most rows: nothing changed)
        for (int x = 0; x < frame->w; x++) {
            if (!memcmp(a + (size_t)x * 4, b + (size_t)x * 4, 4)) continue;
            int claimed = 0;
            for (int i = 0; i < n && !claimed; i++)
                claimed = x >= r[i].x && y >= r[i].y && x - r[i].x < r[i].w && y - r[i].y < r[i].h;
            if (!claimed) bad++;
        }
    }
    return bad;
}
