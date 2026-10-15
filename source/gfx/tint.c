// Profile colours (see tint.h). The shifts are made in OKLCH, where a step of hue or lightness looks about as big on every colour.
#include <math.h>
#include <stddef.h>

#include "tint.h"

static float to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float to_srgb(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f; }

void tint_oklab(uint32_t rgb, float lab[3])
{
    float r = to_linear((float)((rgb >> 16) & 0xFF) / 255.0f), g = to_linear((float)((rgb >> 8) & 0xFF) / 255.0f),
          b = to_linear((float)(rgb & 0xFF) / 255.0f);
    float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    lab[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    lab[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    lab[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

// linear sRGB of an OKLab colour; 1 if it is a colour the screen can show
static int lab_to_linear(const float lab[3], float out[3])
{
    float l = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
    float m = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
    float s = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    out[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    out[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    out[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    const float e = 0.0005f;
    return out[0] >= -e && out[0] <= 1 + e && out[1] >= -e && out[1] <= 1 + e && out[2] >= -e && out[2] <= 1 + e;
}

static uint32_t pack(const float lin[3])
{
    uint32_t v = 0;
    for (int i = 0; i < 3; i++) {
        float c = lin[i] < 0 ? 0 : lin[i] > 1 ? 1 : lin[i];
        int b = (int)lroundf(to_srgb(c) * 255.0f);
        v = v << 8 | (uint32_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    }
    return v;
}

// the most chroma, up to C, that the screen can show at lightness L and hue h (radians)
static float fit_chroma(float L, float C, float h)
{
    float lab[3] = {L, C * cosf(h), C * sinf(h)}, lin[3];
    if (lab_to_linear(lab, lin)) return C;
    float lo = 0, hi = C;
    for (int i = 0; i < 24; i++) {
        float mid = (lo + hi) / 2;
        lab[1] = mid * cosf(h);
        lab[2] = mid * sinf(h);
        if (lab_to_linear(lab, lin)) lo = mid;
        else hi = mid;
    }
    return lo;
}

// L, C, h back to 0xRRGGBB, giving up chroma (never hue or lightness) until the screen can show it
static uint32_t from_lch(float L, float C, float h)
{
    float c = fit_chroma(L, C, h), lab[3] = {L, c * cosf(h), c * sinf(h)}, lin[3];
    lab_to_linear(lab, lin);
    return pack(lin);
}

// FNV-1a over the text, lowercased (so "Alex" and "alex" are one colour), then mixed so every bit depends on every letter
static uint32_t hash_more(uint32_t h, const char *s)
{
    for (; s && *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        h = (h ^ ch) * 16777619u;
    }
    return h;
}

static uint32_t mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

uint32_t profile_tint(uint32_t rgb, const char *name, const char *netid)
{
    uint32_t h = hash_more(2166136261u, name);
    h = hash_more((h ^ '\n') * 16777619u, netid);
    h = mix(h);
    float lab[3];
    tint_oklab(rgb, lab);
    float L = lab[0], C = hypotf(lab[1], lab[2]), hue = atan2f(lab[2], lab[1]);
    // each shift takes its own bits: which way, then how far (6..18 degrees of hue, 0.03..0.06 of lightness, 0.9..1.1 of chroma)
    float dh = (6.0f + 12.0f * (float)((h >> 1) & 0xFF) / 255.0f) * ((h & 1) ? 1.0f : -1.0f);
    float dl = (0.03f + 0.03f * (float)((h >> 10) & 0xFF) / 255.0f) * ((h >> 9 & 1) ? 1.0f : -1.0f);
    float kc = 0.9f + 0.2f * (float)((h >> 18) & 0xFF) / 255.0f;
    if (L + dl > 0.97f || L + dl < 0.12f) dl = -dl; // (white stays white-ish by going down, black by going up)
    if (C < 0.02f) return from_lch(L + dl, C, hue);  // a grey: only lighter or darker
    // A vivid colour can sit at the edge of what the screen shows (yellow, magenta): turned or lightened the hashed way it may have
    // to lose most of its colour, so the other ways are tried before that happens.
    float want = C * kc, best = -1, bl = L + dl, bh = hue;
    for (int k = 0; k < 4; k++) {
        float l = L + ((k & 1) ? -dl : dl), hh = hue + ((k & 2) ? -dh : dh) * 3.14159265f / 180.0f;
        if (l > 0.97f || l < 0.12f) continue;
        float got = fit_chroma(l, want, hh);
        if (got > best) {
            best = got;
            bl = l;
            bh = hh;
        }
        if (got >= 0.85f * want) break;
    }
    return from_lch(bl, want, bh);
}

// how far apart two colours look for a bar: lightness counts double, since a bar of the same lightness as the card is easy to miss
static float bar_dist(const float a[3], const float b[3])
{
    float dl = 2.0f * (a[0] - b[0]), da = a[1] - b[1], db = a[2] - b[2];
    return sqrtf(dl * dl + da * da + db * db);
}

#define APART 0.16f

static int clear_of(const float lab[3], const float (*ag)[3], int n)
{
    for (int i = 0; i < n; i++)
        if (bar_dist(lab, ag[i]) < APART) return 0;
    return 1;
}

uint32_t tint_apart(uint32_t c, const uint32_t *against, int n)
{
    float lc[3], ag[4][3];
    if (n > 4) n = 4;
    tint_oklab(c, lc);
    for (int i = 0; i < n; i++) tint_oklab(against[i], ag[i]);
    if (clear_of(lc, ag, n)) return c;
    float C = hypotf(lc[1], lc[2]), hue = atan2f(lc[2], lc[1]);
    for (int step = 1; step <= 120; step++) { // the nearest lightness, up or down, that is clear of all of them
        for (int dir = -1; dir <= 1; dir += 2) {
            float L = lc[0] + dir * step * 0.005f;
            if (L < 0.08f || L > 0.98f) continue;
            uint32_t v = from_lch(L, C, hue);
            float lv[3];
            tint_oklab(v, lv);
            if (clear_of(lv, ag, n)) return v;
        }
    }
    return c;
}

uint32_t tint_for_theme(uint32_t c, int dark)
{
    static const uint32_t light[3] = {0xFFFFFF, 0xE4E4E4, 0xFFFFFF}, black[3] = {0x333333, 0x575757, 0x1F2227};
    return tint_apart(c, dark ? black : light, 3);
}
