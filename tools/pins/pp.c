// Push pin, round 3: see pp.h. World units are screen px: x right, y down the screen, z up off the card toward the eye; the pin's own
// space is in mm, its flange's underside at z = 0. Light is in "card units": the lit card shows its own colour (1.0), what is brighter
// (the soft box seen in the plastic) is above 1 and rolled off at the end.
#include "pp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 mul(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static V3 vmul(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return l > 0 ? mul(a, 1.0f / l) : a; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float smin(float a, float b, float k) { float h = clampf(0.5f + 0.5f * (b - a) / k, 0, 1); return mixf(b, a, h) - k * h * (1 - h); }
static float smoothf(float a, float b, float x) { float u = clampf((x - a) / (b - a), 0, 1); return u * u * (3 - 2 * u); }
static V3 vexp(V3 a) { return v3(expf(a.x), expf(a.y), expf(a.z)); }
static V3 reflect3(V3 d, V3 n) { return sub(d, mul(n, 2 * dot(d, n))); }
// d: unit, travelling; n: the side d comes from; eta = n1 / n2. 0 = all reflected
static int refract3(V3 d, V3 n, float eta, V3 *o)
{
    float ci = -dot(d, n), k = 1 - eta * eta * (1 - ci * ci);
    if (k < 0) return 0;
    *o = norm(add(mul(d, eta), mul(n, eta * ci - sqrtf(k))));
    return 1;
}
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static float rnd(uint32_t *st) { *st = *st * 1664525u + 1013904223u; return (float)(*st >> 8) * (1.0f / 16777216.0f); }

static const float K = 0.42f;     // the oblique view
static const float IOR = 1.52f, F0 = 0.043f;
static const float A_DIR = 0.70f, A_AMB = 0.30f; // the lit card's light: the soft box, and the room
static const float TOP_MM = 9.55f, FL_MM = 5.0f, NEEDLE_MM = 8.6f;

static const PPLook *g_l;
static float g_h, g_s, g_ct, g_st, g_cu, g_su;
static V3 g_L, g_V, g_U, g_W; // toward the light (the soft box's middle), toward the eye, the box's two sides
static float g_hu, g_hw, g_Lk; // the box's half sizes (rad) and its brightness
static V3 g_sig, g_alb, g_card; // the clear plastic's absorption per mm; the solid's colour; the paper's (linear)
static float g_haze;

// the light that lands on the card (rgb) and the room's light that reaches it, over a window of the card round the pin
typedef struct { int x0, y0, w, h; float *e, *a; } LMap;
static LMap g_m;

static V3 lin3(uint32_t rgb)
{
    float c[3] = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f};
    for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
    return v3(c[0], c[1], c[2]);
}
static float srgb1(float v)
{
    v = clampf(v, 0, 1);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
}
// the highlights roll off instead of clipping
static float tone1(float v) { return v < 0.82f ? v : 0.82f + 0.18f * (1 - expf(-(v - 0.82f) / 0.18f)); }

// ---- shape
static V3 to_pin(V3 p)
{
    p.z -= g_h;
    float y = p.y * g_ct + p.z * g_st, z = -p.y * g_st + p.z * g_ct;
    p.y = y;
    p.z = z;
    float x = p.x * g_cu - p.z * g_su;
    z = p.x * g_su + p.z * g_cu;
    p.x = x;
    p.z = z;
    return mul(p, 1.0f / g_s);
}
static float rcyl(float rho, float z, float r, float zc, float hh, float rr)
{
    float dx = rho - r + rr, dz = fabsf(z - zc) - hh + rr;
    float ox = dx > 0 ? dx : 0, oz = dz > 0 ? dz : 0;
    return fminf(fmaxf(dx, dz), 0.0f) + sqrtf(ox * ox + oz * oz) - rr;
}
static float plastic_mm(V3 p)
{
    float rho = sqrtf(p.x * p.x + p.y * p.y);
    float fl = rcyl(rho, p.z, FL_MM, 0.52f, 0.52f, 0.44f); // the flange, its edge rounded
    const float zc = 4.6f, hh = 3.2f;                       // the grip, waisted, with fine ribs
    float w = clampf((p.z - zc) / hh, -1, 1), r = 2.40f + 0.58f * w * w;
    if (g_l->flutes) r += 0.085f * (1 - w * w * w * w) * cosf(g_l->flutes * atan2f(p.y, p.x));
    float grip = fmaxf((rho - r) * 0.86f, fabsf(p.z - zc) - hh);
    float top = rcyl(rho, p.z, 3.9f, 8.5f, 1.05f, 0.82f);   // the top, a little domed
    float dome = sqrtf(rho * rho + (p.z - (TOP_MM - 70.0f)) * (p.z - (TOP_MM - 70.0f))) - 70.0f; // (all but flat: the light catches its rim)
    top = fmaxf(top, dome);
    return smin(smin(fl, grip, 0.95f), top, 0.55f);
}
static float needle_mm(V3 p)
{
    float rho = sqrtf(p.x * p.x + p.y * p.y);
    float r = 0.40f * clampf((p.z + NEEDLE_MM) / 1.7f, 0.0f, 1.0f); // (its point)
    return fmaxf((rho - r) * 0.9f, fabsf(p.z - (5.6f - NEEDLE_MM) * 0.5f) - (5.6f + NEEDLE_MM) * 0.5f);
}
static float sdp(V3 w) { return plastic_mm(to_pin(w)) * g_s; }
static float sdn(V3 w) { return needle_mm(to_pin(w)) * g_s; }
static float sda(V3 w, int *mat)
{
    V3 p = to_pin(w);
    float a = plastic_mm(p), b = needle_mm(p);
    if (mat) *mat = a <= b ? 0 : 1;
    return fminf(a, b) * g_s;
}
static V3 nrm(V3 p, int needle)
{
    float e = 0.012f * g_s + 0.004f;
    float (*f)(V3) = needle ? sdn : sdp;
    return norm(v3(f(add(p, v3(e, 0, 0))) - f(sub(p, v3(e, 0, 0))), f(add(p, v3(0, e, 0))) - f(sub(p, v3(0, e, 0))),
                   f(add(p, v3(0, 0, e))) - f(sub(p, v3(0, 0, e)))));
}
static float ztop(void) { return g_h + (TOP_MM + 0.4f) * g_s + 1; }

// ---- the room
static __thread float t_rough = 0.01f; // (how glossy the reflection being looked up is: the lights' edges are spread by it)
static V3 sky(V3 r)
{
    // a dim room: the ceiling above, the walls brighter toward the horizon, warm
    float k = 0.20f + 0.16f * expf(-(r.z - 0.15f) * (r.z - 0.15f) / 0.08f) - 0.05f * clampf(r.z, 0, 1);
    V3 c = v3(k * 1.0f, k * 0.97f, k * 0.92f);
    // the soft box: a bright rectangle, a little brighter in the middle
    float cz = dot(r, g_L);
    if (cz > 0.5f) {
        float a = dot(r, g_U) / cz, b = dot(r, g_W) / cz;
        float e = 0.012f + 0.6f * t_rough;
        float in = (1 - smoothf(g_hu - e, g_hu + e, fabsf(a))) * (1 - smoothf(g_hw - e, g_hw + e, fabsf(b)));
        if (in > 0) c = add(c, mul(v3(1.0f, 0.985f, 0.95f), in * g_Lk * (0.82f + 0.18f * (1 - fminf(a * a / (g_hu * g_hu), 1)))));
    }
    // a window off to the right, cooler and much dimmer
    V3 wd = norm(v3(0.80f, 0.05f, 0.60f));
    float wz = dot(r, wd);
    if (wz > 0.5f) {
        V3 wu = norm(cross(wd, v3(0, 1, 0))), wv = cross(wd, wu);
        float a = dot(r, wu) / wz, b = dot(r, wv) / wz;
        float e = 0.012f + 0.6f * t_rough;
        float in = (1 - smoothf(0.115f - e, 0.115f + e, fabsf(a))) * (1 - smoothf(0.32f - e, 0.32f + e, fabsf(b)));
        c = add(c, mul(v3(0.90f, 0.95f, 1.0f), 1.6f * in));
    }
    return c;
}
static float map_at(const float *buf, int ch, int stride, float x, float y, float def)
{
    float fx = x - g_m.x0 - 0.5f, fy = y - g_m.y0 - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    if (ix < 0 || iy < 0 || ix >= g_m.w - 1 || iy >= g_m.h - 1) return def;
    float ux = fx - ix, uy = fy - iy;
    const float *p = buf + ((size_t)iy * g_m.w + ix) * stride + ch;
    float a = p[0], b = p[stride], c = p[(size_t)g_m.w * stride], d = p[(size_t)g_m.w * stride + stride];
    return mixf(mixf(a, b, ux), mixf(c, d, ux), uy);
}
// how bright the card is at (x, y), as a part of the lit card
static V3 card_m(float x, float y)
{
    if (!g_m.e) return v3(1, 1, 1);
    V3 e = v3(map_at(g_m.e, 0, 3, x, y, 1), map_at(g_m.e, 1, 3, x, y, 1), map_at(g_m.e, 2, 3, x, y, 1));
    V3 a = v3(map_at(g_m.a, 0, 3, x, y, 1), map_at(g_m.a, 1, 3, x, y, 1), map_at(g_m.a, 2, 3, x, y, 1));
    return add(mul(e, A_DIR), mul(a, A_AMB));
}
static V3 card_rad(float x, float y) { return vmul(g_card, card_m(x, y)); }

static V3 steel_rad(V3 p, V3 d, uint32_t *rng);
static V3 inside(V3 p, V3 d, int depth, uint32_t *rng);

// what a ray from p going d sees, outside the plastic
static V3 trace_out(V3 p, V3 d, int depth, uint32_t *rng)
{
    float t = 0, zt = ztop(), far = 40.0f * g_s;
    for (int i = 0; i < 120; i++) {
        V3 q = add(p, mul(d, t));
        if (q.z <= 0 && d.z < 0) {
            float k = p.z / -d.z;
            return card_rad(p.x + d.x * k, p.y + d.y * k);
        }
        if ((q.z > zt && d.z >= 0) || t > far) break;
        int mat;
        float dd = sda(q, &mat);
        if (dd < 0.002f * g_s) {
            if (mat == 1) return depth > 0 ? steel_rad(q, d, rng) : v3(0.25f, 0.25f, 0.26f);
            V3 n = nrm(q, 0);
            if (g_l->finish == PP_GLOSS || depth <= 0) return mul(g_alb, 0.45f); // (another part of the pin, seen in this one: rough guess)
            float F = F0 + (1 - F0) * powf(1 - clampf(-dot(d, n), 0, 1), 5);
            V3 td;
            if (!refract3(d, n, 1 / IOR, &td)) return v3(0, 0, 0);
            V3 r = trace_out(add(q, mul(n, 0.02f * g_s)), reflect3(d, n), 0, rng);
            return add(mul(r, F), mul(inside(sub(q, mul(n, 0.02f * g_s)), td, depth - 1, rng), 1 - F));
        }
        t += fmaxf(dd * 0.95f, 0.004f * g_s);
    }
    if (d.z < 0) {
        float k = p.z / -d.z;
        return card_rad(p.x + d.x * k, p.y + d.y * k);
    }
    return sky(d);
}

// a glossy reflection: the mirror direction spread by `rough` (rad)
static V3 spread(V3 r, float rough, uint32_t *rng)
{
    V3 a = fabsf(r.z) < 0.9f ? v3(0, 0, 1) : v3(1, 0, 0), u = norm(cross(r, a)), w = cross(r, u);
    float ph = 6.2831853f * rnd(rng), rr = rough * sqrtf(-logf(1e-6f + rnd(rng)) * 0.5f);
    return norm(add(r, add(mul(u, rr * cosf(ph)), mul(w, rr * sinf(ph)))));
}

static V3 steel_rad(V3 p, V3 d, uint32_t *rng)
{
    V3 n = nrm(p, 1);
    float c = clampf(-dot(d, n), 0, 1);
    V3 f0 = v3(0.56f, 0.57f, 0.60f);
    float fr = powf(1 - c, 5);
    V3 F = v3(mixf(f0.x, 1, fr), mixf(f0.y, 1, fr), mixf(f0.z, 1, fr));
    V3 r = spread(reflect3(d, n), 0.03f, rng);
    float keep = t_rough;
    t_rough = 0.10f;
    V3 o = vmul(F, trace_out(add(p, mul(n, 0.03f * g_s)), r, 0, rng));
    t_rough = keep;
    return o;
}

// light from the soft box that reaches p inside the plastic (straight toward the light, absorbed on the way)
static V3 key_through(V3 p)
{
    float t = 0, tin = 0;
    for (int i = 0; i < 40; i++) {
        V3 q = add(p, mul(g_L, t));
        float dd = sdp(q);
        if (dd > 0.0f) break;
        float st = fmaxf(-dd, 0.05f * g_s);
        t += st;
        tin += st;
    }
    return vexp(mul(g_sig, -(tin / g_s)));
}

// a ray inside the clear plastic: absorbed as it goes, a little scattered (the haze), out again where it meets the surface
static V3 inside(V3 p, V3 d, int depth, uint32_t *rng)
{
    V3 T = v3(1, 1, 1), Ls = v3(0, 0, 0);
    const float minst = 0.03f * g_s;
    const float Ik = A_DIR / g_L.z;
    float since = 0;
    for (int b = 0; b < 6; b++) {
        for (int i = 0; i < 240; i++) {
            float dp = sdp(p), dn = sdn(p);
            if (dn < 0.004f * g_s) return add(Ls, vmul(T, mul(steel_rad(p, d, rng), 0.9f)));
            if (dp > 0) break;
            float st = clampf(fminf(-dp, dn) * 0.9f, minst, 0.5f * g_s), mm = st / g_s;
            if (g_haze > 0) {
                since += mm;
                if (since > 0.35f) { // (every third of a mm or so: the soft box's light scattered toward the eye, and the room's)
                    V3 k = key_through(p);
                    Ls = add(Ls, mul(vmul(T, add(mul(k, Ik * 0.25f), v3(0.09f, 0.09f, 0.09f))), g_haze * since));
                    since = 0;
                }
            }
            T = vmul(T, vexp(mul(g_sig, -mm)));
            p = add(p, mul(d, st));
            if (T.x + T.y + T.z < 0.004f) return Ls;
        }
        // back to the surface (it was just crossed), then out or round again
        V3 a = sub(p, mul(d, minst)), c = p;
        for (int k = 0; k < 6; k++) {
            V3 m = mul(add(a, c), 0.5f);
            if (sdp(m) > 0) c = m; else a = m;
        }
        p = a;
        V3 n = nrm(p, 0); // (outward)
        V3 o;
        if (refract3(d, mul(n, -1), IOR, &o)) {
            float ci = clampf(dot(o, n), 0, 1), F = F0 + (1 - F0) * powf(1 - ci, 5);
            V3 L = trace_out(add(p, mul(n, 0.03f * g_s)), o, depth, rng);
            // (the part reflected back in is not followed: it would come out somewhere darker still)
            return add(Ls, vmul(T, mul(L, 1 - F)));
        }
        d = reflect3(d, n); // all of it turned back in
        p = sub(p, mul(n, 0.02f * g_s));
    }
    return Ls;
}

// soft shadow toward the soft box from p (the pin's own parts): 1 lit
static float soft_shadow(V3 p)
{
    float res = 1.0f, t = 0.05f * g_s;
    for (int i = 0; i < 48 && t < 20.0f * g_s; i++) {
        float h = sda(add(p, mul(g_L, t)), NULL);
        if (h < 0.001f) return 0.0f;
        res = fminf(res, 5.0f * h / t);
        t += clampf(h, 0.03f * g_s, 1.0f * g_s);
    }
    return clampf(res, 0, 1);
}
static float occl(V3 p, V3 n)
{
    float o = 0, w = 1;
    for (int i = 1; i <= 5; i++) {
        float hr = 0.35f * g_s * i;
        V3 q = add(p, mul(n, hr));
        float d = fminf(sda(q, NULL), q.z);
        o += (hr - d) / g_s * w;
        w *= 0.6f;
    }
    return clampf(1.0f - 0.45f * o, 0, 1);
}

// the colour seen along a primary ray that met the pin at p
static V3 shade(V3 p, int mat, V3 d, uint32_t *rng)
{
    if (mat == 1) return steel_rad(p, d, rng);
    V3 n = nrm(p, 0);
    float c = clampf(-dot(d, n), 0, 1), F = F0 + (1 - F0) * powf(1 - c, 5);
    const float rough = g_l->finish == PP_GLOSS ? 0.055f : 0.035f;
    V3 r = spread(reflect3(d, n), rough * 0.3f, rng);
    t_rough = rough;
    V3 Lr = trace_out(add(p, mul(n, 0.02f * g_s)), r, 1, rng);
    t_rough = 0.01f;
    if (r.z > 0) Lr = mul(Lr, 0.35f + 0.65f * soft_shadow(add(p, mul(n, 0.03f * g_s)))); // (crude: the box hidden by the pin's own parts)
    if (g_l->finish == PP_GLOSS) {
        float sh = soft_shadow(add(p, mul(n, 0.03f * g_s))), ao = occl(p, n);
        float nl = clampf((dot(n, g_L) + 0.15f) / 1.15f, 0, 1);
        float up = 0.5f + 0.5f * n.z;
        V3 amb = add(mul(v3(1, 0.97f, 0.92f), A_AMB * up), mul(g_card, 0.9f * (1 - up)));
        V3 base = vmul(g_alb, add(mul(v3(1, 0.985f, 0.95f), nl * sh * A_DIR / g_L.z), mul(amb, ao)));
        return add(mul(Lr, F), mul(base, 1 - F));
    }
    V3 td;
    if (!refract3(d, n, 1 / IOR, &td)) return Lr;
    return add(mul(Lr, F), mul(inside(sub(p, mul(n, 0.02f * g_s)), td, 1, rng), 1 - F));
}

static void setup(const PPLook *L, float h)
{
    g_l = L;
    g_h = h;
    g_s = L->s;
    g_ct = cosf(L->tilt_x);
    g_st = sinf(L->tilt_x);
    g_cu = cosf(L->tilt_y);
    g_su = sinf(L->tilt_y);
    g_L = norm(v3(-0.16f, -0.62f, 0.77f));
    g_V = norm(v3(0, K, 1));
    g_U = norm(v3(-g_L.y, g_L.x, 0));
    g_W = cross(g_L, g_U);
    g_hu = 0.17f;
    g_hw = 0.11f;
    // its brightness: so that what it lays on the card adds up to A_DIR (the box's solid angle, about 4 hu hw, seen at g_L.z)
    g_Lk = A_DIR * 3.14159265f / (4 * g_hu * g_hw * g_L.z) / 0.91f;
    V3 t = lin3(L->dye);
    g_sig = v3(-logf(fmaxf(t.x, 1e-3f)), -logf(fmaxf(t.y, 1e-3f)), -logf(fmaxf(t.z, 1e-3f)));
    g_alb = t;
    g_card = lin3(L->card);
    g_haze = L->finish == PP_CLEAR ? L->haze : 0;
}

// one light path from the soft box to the card through (or past) the pin; adds what lands into e
static void photon(V3 land, V3 dl, float *e)
{
    V3 d = mul(dl, -1);
    float zt = ztop();
    V3 p = add(land, mul(dl, zt / dl.z));
    V3 T = v3(1, 1, 1);
    int in = 0;
    for (int i = 0, b = 0; i < 400; i++) {
        if (!in) {
            if (p.z <= 0) break;
            int mat;
            float dd = sda(p, &mat);
            if (dd < 0.002f * g_s) {
                if (mat == 1 || g_l->finish == PP_GLOSS) return;
                V3 n = nrm(p, 0), td;
                float F = F0 + (1 - F0) * powf(1 - clampf(-dot(d, n), 0, 1), 5);
                if (!refract3(d, n, 1 / IOR, &td)) return;
                T = mul(T, 1 - F);
                d = td;
                p = sub(p, mul(n, 0.02f * g_s));
                in = 1;
                continue;
            }
            float st = fmaxf(dd * 0.95f, 0.004f * g_s);
            if (p.z - st * -d.z <= 0 && d.z < 0) { p = add(p, mul(d, p.z / -d.z)); break; }
            p = add(p, mul(d, st));
        } else {
            float dp = sdp(p), dn = sdn(p);
            if (dn < 0.004f * g_s) return;
            if (dp > 0) {
                V3 a = sub(p, mul(d, 0.03f * g_s)), c = p;
                for (int k = 0; k < 6; k++) {
                    V3 m = mul(add(a, c), 0.5f);
                    if (sdp(m) > 0) c = m; else a = m;
                }
                p = a;
                V3 n = nrm(p, 0), o;
                if (refract3(d, mul(n, -1), IOR, &o)) {
                    float F = F0 + (1 - F0) * powf(1 - clampf(dot(o, n), 0, 1), 5);
                    T = mul(T, 1 - F);
                    d = o;
                    p = add(p, mul(n, 0.03f * g_s));
                    in = 0;
                } else {
                    if (++b > 5) return;
                    d = reflect3(d, n);
                    p = sub(p, mul(n, 0.02f * g_s));
                }
                continue;
            }
            float st = clampf(fminf(-dp, dn) * 0.9f, 0.03f * g_s, 0.5f * g_s);
            T = vmul(T, vexp(mul(g_sig, -st / g_s)));
            if (g_haze > 0) T = mul(T, expf(-g_haze * st / g_s));
            p = add(p, mul(d, st));
            if (T.x + T.y + T.z < 0.003f) return;
        }
    }
    if (in || d.z >= 0 || p.z > 0.05f * g_s) return; // (still inside, going up, or never got down)
    float fx = p.x - g_m.x0 - 0.5f, fy = p.y - g_m.y0 - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    if (ix < 0 || iy < 0 || ix >= g_m.w - 1 || iy >= g_m.h - 1) return;
    float ux = fx - ix, uy = fy - iy, w[4] = {(1 - ux) * (1 - uy), ux * (1 - uy), (1 - ux) * uy, ux * uy};
    int o4[4] = {0, 1, g_m.w, g_m.w + 1};
    for (int k = 0; k < 4; k++) {
        float *q = e + ((size_t)iy * g_m.w + ix + o4[k]) * 3;
        q[0] += T.x * w[k];
        q[1] += T.y * w[k];
        q[2] += T.z * w[k];
    }
}

static V3 box_dir(uint32_t *rng)
{
    float a = (rnd(rng) * 2 - 1) * g_hu, b = (rnd(rng) * 2 - 1) * g_hw;
    return norm(add(g_L, add(mul(g_U, a), mul(g_W, b))));
}

static void light_map(void)
{
    const float s = g_s, R = FL_MM * s + 2, top = (TOP_MM + 0.4f) * s + g_h;
    const float rx = top * -g_L.x / g_L.z, ry = top * -g_L.y / g_L.z, m = 6 + 0.45f * top;
    g_m.x0 = (int)floorf(fminf(-R, -R + rx) - m);
    g_m.y0 = (int)floorf(fminf(-R, -R + ry) - m);
    g_m.w = (int)ceilf(fmaxf(R, R + rx) + m) - g_m.x0;
    g_m.h = (int)ceilf(fmaxf(R, R + ry) + m) - g_m.y0;
    size_t n = (size_t)g_m.w * g_m.h;
    float *e = calloc(n * 3, sizeof(float));
    float *a = malloc(n * 3 * sizeof(float));
    const int np = g_l->photons > 0 ? g_l->photons : 144, sq = (int)ceilf(sqrtf((float)np));
    const float Rb = (FL_MM + 0.6f) * s + 1, zt = ztop();
#pragma omp parallel
    {
        float *mine = calloc(n * 3, sizeof(float));
#pragma omp for schedule(dynamic, 2)
        for (int y = 0; y < g_m.h; y++)
            for (int x = 0; x < g_m.w; x++) {
                uint32_t rng = hash3((uint32_t)x, (uint32_t)y, 77u);
                for (int k = 0; k < sq * sq; k++) {
                    V3 land = v3(g_m.x0 + x + (k % sq + rnd(&rng)) / sq, g_m.y0 + y + (k / sq + rnd(&rng)) / sq, 0);
                    V3 dl = box_dir(&rng);
                    // (a path that passes nowhere near the pin lands where it was aimed)
                    float tz = zt / dl.z, px = land.x, py = land.y, qx = dl.x, qy = dl.y;
                    float tt = clampf(-(px * qx + py * qy) / (qx * qx + qy * qy + 1e-9f), 0, tz);
                    float cx = px + qx * tt, cy = py + qy * tt;
                    if (cx * cx + cy * cy > Rb * Rb + 4 * s * s) {
                        float *q = mine + ((size_t)y * g_m.w + x) * 3;
                        q[0] += 1;
                        q[1] += 1;
                        q[2] += 1;
                        continue;
                    }
                    photon(land, dl, mine);
                }
            }
#pragma omp critical
        for (size_t i = 0; i < n * 3; i++) e[i] += mine[i];
        free(mine);
    }
    for (size_t i = 0; i < n * 3; i++) e[i] /= (float)(sq * sq);
    // the room's light: what of the sky each point of the card still sees (the clear plastic lets some through, coloured)
    V3 thru = g_l->finish == PP_CLEAR ? mul(vexp(mul(g_sig, -3.0f)), 0.85f) : v3(0, 0, 0);
#pragma omp parallel for schedule(dynamic, 2)
    for (int y = 0; y < g_m.h; y++)
        for (int x = 0; x < g_m.w; x++) {
            V3 p = v3(g_m.x0 + x + 0.5f, g_m.y0 + y + 0.5f, 0);
            float *q = a + ((size_t)y * g_m.w + x) * 3;
            float rho = sqrtf(p.x * p.x + p.y * p.y);
            if (rho > R + 2.2f * top) { q[0] = q[1] = q[2] = 1; continue; }
            uint32_t rng = hash3((uint32_t)x, (uint32_t)y, 99u);
            V3 acc = v3(0, 0, 0);
            const int N = 8;
            for (int i = 0; i < N; i++)
                for (int j = 0; j < N; j++) {
                    float u1 = (i + rnd(&rng)) / N, u2 = (j + rnd(&rng)) / N, r = sqrtf(u1), ph = 6.2831853f * u2;
                    V3 dir = v3(r * cosf(ph), r * sinf(ph), sqrtf(fmaxf(0, 1 - u1)));
                    float t = 0.02f * s;
                    int hit = 0;
                    for (int k = 0; k < 64 && t < 4 * top; k++) {
                        V3 qq = add(p, mul(dir, t));
                        if (qq.z > zt) break;
                        float dd = sda(qq, NULL);
                        if (dd < 0.002f * s) { hit = 1; break; }
                        t += fmaxf(dd, 0.01f * s);
                    }
                    acc = add(acc, hit ? thru : v3(1, 1, 1));
                }
            acc = mul(acc, 1.0f / (N * N));
            // (far off, the pin hides a sliver of the room that no eye would see as a shadow; it would only show as the edge of
            // this window, so it fades out to nothing toward the window's edge)
            float bx = fminf(x, g_m.w - 1 - x), by = fminf(y, g_m.h - 1 - y), fade = smoothf(4, 22, fminf(bx, by));
            acc = v3(1 - (1 - acc.x) * fade, 1 - (1 - acc.y) * fade, 1 - (1 - acc.z) * fade);
            acc = v3(1 - fmaxf(0, 1 - acc.x - 0.02f) / 0.98f, 1 - fmaxf(0, 1 - acc.y - 0.02f) / 0.98f, 1 - fmaxf(0, 1 - acc.z - 0.02f) / 0.98f);
            q[0] = acc.x;
            q[1] = acc.y;
            q[2] = acc.z;
        }
    // (a light blur: the photons' grain)
    float *tmp = malloc(n * 3 * sizeof(float));
    for (int pass = 0; pass < 2; pass++) {
        float *src = pass ? tmp : e, *dst = pass ? e : tmp;
        for (int y = 0; y < g_m.h; y++)
            for (int x = 0; x < g_m.w; x++)
                for (int c = 0; c < 3; c++) {
                    float acc = 0, wsum = 0;
                    for (int k = -1; k <= 1; k++) {
                        int xx = pass ? x : x + k, yy = pass ? y + k : y;
                        if (xx < 0 || yy < 0 || xx >= g_m.w || yy >= g_m.h) continue;
                        float w = k == 0 ? 0.5f : 0.25f;
                        acc += src[((size_t)yy * g_m.w + xx) * 3 + c] * w;
                        wsum += w;
                    }
                    dst[((size_t)y * g_m.w + x) * 3 + c] = acc / wsum;
                }
    }
    free(tmp);
    g_m.e = e;
    g_m.a = a;
}

static PaperSpr *shadow_spr(void)
{
    PaperSpr *o = paper_spr_new(g_m.w, g_m.h);
    if (!o) return NULL;
    o->ox = -g_m.x0;
    o->oy = -g_m.y0;
    float c0[3] = {srgb1(g_card.x), srgb1(g_card.y), srgb1(g_card.z)};
    for (int y = 0; y < g_m.h; y++)
        for (int x = 0; x < g_m.w; x++) {
            V3 m = card_m(g_m.x0 + x + 0.5f, g_m.y0 + y + 0.5f);
            // (only darker: the card is near white, so light gathered by the plastic shows as a less dark, coloured spot; and a hair
            // either side of 1 is the photons' grain, not light)
            int edge = x < 3 || y < 3 || x >= g_m.w - 3 || y >= g_m.h - 3;
            m = v3(1 - fmaxf(0, 1 - m.x - 0.006f), 1 - fmaxf(0, 1 - m.y - 0.006f), 1 - fmaxf(0, 1 - m.z - 0.006f));
            if (edge) m = v3(1, 1, 1);
            const float sk = g_l->shadow_k > 0 ? g_l->shadow_k : 1.0f; // (Paper night's dark cards: the shadow a little stronger, as the cards' own are)
            m = v3(fmaxf(0, 1 - (1 - m.x) * sk), fmaxf(0, 1 - (1 - m.y) * sk), fmaxf(0, 1 - (1 - m.z) * sk));
            float tg[3] = {srgb1(g_card.x * m.x), srgb1(g_card.y * m.y), srgb1(g_card.z * m.z)};
            // laid over the card with alpha: the least alpha that reaches the target in every channel
            float al = 0;
            for (int c = 0; c < 3; c++) {
                float dl = tg[c] - c0[c];
                float need = dl < 0 ? -dl / fmaxf(c0[c], 1e-3f) : dl / fmaxf(1 - c0[c], 1e-3f);
                if (need > al) al = need;
            }
            al = clampf(al, 0, 1);
            uint8_t *q = o->px + ((size_t)y * g_m.w + x) * 4;
            if (al < 1.0f / 255) { q[0] = q[1] = q[2] = q[3] = 0; continue; }
            for (int c = 0; c < 3; c++) q[c] = (uint8_t)(clampf(c0[c] + (tg[c] - c0[c]) / al, 0, 1) * 255 + 0.5f);
            q[3] = (uint8_t)(al * 255 + 0.5f);
        }
    return o;
}

static PaperSpr *pin_spr(void)
{
    const float s = g_s, R = (FL_MM + 0.3f) * s + 3, top = ((TOP_MM + 0.4f) * s + g_h) * K + 4.2f * s + 3; // (the top's far edge)
    int W = (int)ceilf(2 * R), H = (int)ceilf(R + top + 1);
    PaperSpr *o = paper_spr_new(W, H);
    if (!o) return NULL;
    o->ox = W / 2;
    o->oy = (int)ceilf(top);
    const V3 d = mul(g_V, -1.0f);
    const int SS = g_l->ss > 0 ? g_l->ss : 4;
    const float z0 = ztop() + 2;
#pragma omp parallel for schedule(dynamic, 1)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            V3 acc = v3(0, 0, 0);
            int hits = 0;
            uint32_t rng = hash3((uint32_t)x, (uint32_t)y, 5u);
            for (int k = 0; k < SS * SS; k++) {
                float u = x + (k % SS + rnd(&rng)) / SS - o->ox, v = y + (k / SS + rnd(&rng)) / SS - o->oy;
                V3 p = v3(u, v + K * z0, z0), q = p;
                float t = 0;
                int mat = 0, hit = 0;
                for (int i = 0; i < 200; i++) {
                    q = add(p, mul(d, t));
                    if (q.z < 0) break;
                    float dd = sda(q, &mat);
                    if (dd < 0.002f * s) { hit = 1; break; }
                    t += fmaxf(dd * 0.95f, 0.003f * s);
                }
                if (!hit) continue;
                V3 c = shade(q, mat, d, &rng);
                acc = add(acc, v3(tone1(c.x), tone1(c.y), tone1(c.z)));
                hits++;
            }
            if (!hits) continue;
            uint8_t *qq = o->px + ((size_t)y * W + x) * 4;
            qq[0] = (uint8_t)(srgb1(acc.x / hits) * 255 + 0.5f);
            qq[1] = (uint8_t)(srgb1(acc.y / hits) * 255 + 0.5f);
            qq[2] = (uint8_t)(srgb1(acc.z / hits) * 255 + 0.5f);
            qq[3] = (uint8_t)(255.0f * hits / (SS * SS) + 0.5f);
        }
    return o;
}

int pp_render(const PPLook *L, float h, PaperSpr **pin, PaperSpr **shadow)
{
    setup(L, h);
    memset(&g_m, 0, sizeof g_m);
    light_map();
    if (shadow) *shadow = shadow_spr();
    if (pin) *pin = pin_spr();
    free(g_m.e);
    free(g_m.a);
    memset(&g_m, 0, sizeof g_m);
    return 1;
}

PaperSpr *pp_dent(const PPLook *L)
{
    setup(L, 0);
    const float R = FL_MM * L->s, w = 1.6f + 0.06f * R;
    int W = (int)ceilf(2 * (R + 3 * w)) + 2;
    PaperSpr *o = paper_spr_new(W, W);
    if (!o) return NULL;
    o->ox = o->oy = W / 2;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            float dx = x + 0.5f - o->ox, dy = y + 0.5f - o->oy, r = sqrtf(dx * dx + dy * dy);
            float g = expf(-powf((r - R - w * 0.5f) / w, 2.0f));
            float facing = r > 0 ? -(dx * g_L.x + dy * g_L.y) / (r * sqrtf(g_L.x * g_L.x + g_L.y * g_L.y)) : 0;
            float v = g * (0.07f * facing + 0.02f);
            uint8_t *q = o->px + ((size_t)y * W + x) * 4;
            const uint32_t dk = L->dent_dark ? L->dent_dark : 0x4A3A1A;
            const float sk = L->shadow_k > 0 ? L->shadow_k : 1.0f;
            if (v > 0) { q[0] = (dk >> 16) & 255; q[1] = (dk >> 8) & 255; q[2] = dk & 255; q[3] = (uint8_t)clampf(v * 255 * 1.4f * sk, 0, 255); }
            else { q[0] = q[1] = q[2] = 255; q[3] = (uint8_t)clampf(-v * 255 * 2.0f / sk, 0, 255); }
        }
    return o;
}
