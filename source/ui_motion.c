#include <math.h>
#include <string.h>

#include "ui_motion.h"

#define GLIDE_SHAPE 2.2f // a glide left to itself lasts this many times (distance / speed): it slows all the way, softly at the end
#define GLIDE_MIN 0.10f  // with less time than this to stop in, it is going too fast for the room it has: a spring takes it instead
#define BAND 0.55f       // how much of a pull past an end is followed at first

void mo_snap(Mover *m, float x, float px)
{
    memset(m, 0, sizeof *m);
    m->x = m->tgt = x;
    m->px = px;
}

void mo_spring(Mover *m, float tgt, float om, float zeta)
{
    m->tgt = tgt;
    m->om = om;
    m->zeta = zeta < 0.999f ? zeta : 0.999f;
    m->lead_t = 0;
    m->mode = MO_SPRING;
}

void mo_run(Mover *m, float tgt, float om, float zeta, float T)
{
    float from = m->mode != MO_SPRING ? m->x : m->lead_t > 0 ? m->lead : m->tgt; // (where the spring was pulling it to just now)
    mo_spring(m, tgt, om, zeta);
    if (T <= 0 || from == tgt) return;
    m->lead = from;
    m->lead_v = (tgt - from) / T;
    m->lead_t = T;
}

void mo_hold(Mover *m)
{
    m->mode = MO_HELD;
    m->v = 0;
}

static void glide(Mover *m, float tgt, float T)
{
    m->tgt = tgt;
    m->x0 = m->x;
    m->v0 = m->v;
    m->T = T;
    m->t = 0;
    m->mode = MO_GLIDE;
}

void mo_throw(Mover *m, float v, const Throw *w, float lo, float hi)
{
    m->v = v;
    float x = m->x;
    if (x < lo || x > hi) { // let go past an end: back to it
        mo_spring(m, x < lo ? lo : hi, w->om, w->zeta);
        return;
    }
    float tgt;
    if (fabsf(v) < w->flick) { // just let go
        if (w->step <= 0) {
            mo_snap(m, x, m->px);
            return;
        }
        tgt = roundf(x / w->step) * w->step;
    } else {
        tgt = x + v * w->carry;
        if (w->step > 0) { // the stop nearest to where it would run out, and never one behind the way it was thrown
            float s = w->step, slack = 0.02f * s;
            tgt = roundf(tgt / s) * s;
            if (v > 0) tgt = fmaxf(tgt, ceilf((x - slack) / s) * s);
            else tgt = fminf(tgt, floorf((x + slack) / s) * s);
        }
    }
    if (tgt < lo) tgt = lo;
    if (tgt > hi) tgt = hi;
    float d = tgt - x;
    if (fabsf(v) < w->flick || d * v <= 0) { // (nothing ahead of it: it was thrown at the end it is on)
        mo_spring(m, tgt, w->om, w->zeta);
        return;
    }
    float T = GLIDE_SHAPE * d / v, most = w->t0 + w->t1 * fabsf(d) / (w->step > 0 ? w->step : 1.0f);
    if (T < GLIDE_MIN) mo_spring(m, tgt, w->om, w->zeta);
    else glide(m, tgt, T < most ? T : most);
}

int mo_step(Mover *m, float dt)
{
    if (m->mode == MO_SPRING) {
        while (dt > 0) {
            // The exact motion of a damped spring over h seconds, so it is the same at any frame rate. While the place it is pulled
            // to is itself moving (mo_run), it settles into following that at the same speed, a fixed distance behind: `at` is
            // where it would be then, and e and ev are how far and how fast it is from that.
            float h = dt, at = m->tgt, lead_v = 0, s = m->zeta * m->om, wd = m->om * sqrtf(1.0f - m->zeta * m->zeta);
            if (m->lead_t > 0) {
                if (h > m->lead_t) h = m->lead_t;
                lead_v = m->lead_v;
                at = m->lead - 2.0f * m->zeta * lead_v / m->om;
            }
            float e = m->x - at, ev = m->v - lead_v, b = (ev + s * e) / wd, k = expf(-s * h), c = cosf(wd * h), sn = sinf(wd * h);
            m->x = at + lead_v * h + k * (e * c + b * sn);
            m->v = lead_v + k * ((b * wd - s * e) * c - (e * wd + s * b) * sn);
            if (m->lead_t > 0) {
                m->lead += lead_v * h;
                m->lead_t -= h;
                if (m->lead_t < 1e-5f) m->lead_t = 0;
            }
            dt -= h;
        }
        if (m->lead_t <= 0 && fabsf(m->x - m->tgt) < 0.3f * m->px && fabsf(m->v) < 12.0f * m->px) mo_snap(m, m->tgt, m->px);
        return m->mode != MO_REST;
    }
    if (m->mode == MO_GLIDE) {
        m->t += dt;
        float u = m->t / m->T;
        if (u >= 1.0f) {
            mo_snap(m, m->tgt, m->px);
            return 0;
        }
        // the smoothest way from x0 at speed v0 to a stop at tgt in T seconds: it starts by keeping its speed and lands softly
        float u2 = u * u, u3 = u2 * u, u4 = u3 * u, u5 = u4 * u, vT = m->v0 * m->T, d = m->tgt - m->x0;
        m->x = m->x0 + vT * (u - 6 * u3 + 8 * u4 - 3 * u5) + d * (10 * u3 - 15 * u4 + 6 * u5);
        m->v = (vT * (1 - 18 * u2 + 32 * u3 - 15 * u4) + d * (30 * u2 - 60 * u3 + 30 * u4)) / m->T;
        return 1;
    }
    return 0;
}

float mo_band(float raw, float lo, float hi, float room)
{
    if (raw >= lo && raw <= hi) return raw;
    float over = raw < lo ? lo - raw : raw - hi, give = over * BAND * room / (room + BAND * over);
    return raw < lo ? lo - give : hi + give;
}

float mo_band_rate(float raw, float lo, float hi, float room)
{
    if (raw >= lo && raw <= hi) return 1.0f;
    float over = raw < lo ? lo - raw : raw - hi, d = room + BAND * over;
    return BAND * room * room / (d * d);
}

float mo_unband(float x, float lo, float hi, float room)
{
    if (x >= lo && x <= hi) return x;
    float give = x < lo ? lo - x : x - hi;
    if (give > 0.98f * room) give = 0.98f * room;
    float over = give * room / (BAND * (room - give));
    return x < lo ? lo - over : hi + over;
}

void track_start(Track *k, uint64_t t_ms, float pos)
{
    k->n = 0;
    track_add(k, t_ms, pos);
}

void track_add(Track *k, uint64_t t_ms, float pos)
{
    int cap = (int)(sizeof k->t / sizeof k->t[0]);
    if (k->n == cap) {
        memmove(k->t, k->t + 1, (size_t)(cap - 1) * sizeof k->t[0]);
        memmove(k->p, k->p + 1, (size_t)(cap - 1) * sizeof k->p[0]);
        k->n--;
    }
    k->t[k->n] = t_ms;
    k->p[k->n++] = pos;
}

float track_speed(const Track *k, uint64_t now_ms)
{
    if (k->n < 2) return 0;
    uint64_t end = k->t[k->n - 1];
    if (now_ms > end + 70) return 0; // the finger had stopped before it lifted
    int i = k->n - 2; // from the place before, and further back while that is within 70 ms (four frames): its speed as it lifts
    while (i > 0 && end - k->t[i - 1] <= 70) i--;
    if (end <= k->t[i] || end - k->t[i] > 250) return 0;
    return (k->p[k->n - 1] - k->p[i]) * 1000.0f / (float)(end - k->t[i]);
}
