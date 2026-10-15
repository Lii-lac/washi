#include "scenes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mockup screens: the real UI (source/ui.c) draws with the same skin; these show it with sample data. The clock below is a
// placeholder for the mockups; the app reads the console's clock.

static const SkinStatus MOCK_STATUS = {"Esports Switch", "2:14 PM", 82, 0};

typedef struct { const char *name; int games; const char *ago; int locked; } Person;

static const Person PEOPLE[] = {
    {"Alex", 6, "2d ago", 0}, {"Ben", 3, "1w ago", 0}, {"Chloe", 8, "1d ago", 1}, {"Dani", 4, "3d ago", 0},
    {"Eli", 5, "today", 0}, {"Jordan", 7, "2h ago", 0}, {"Kai", 2, "4w ago", 0}, {"Maya", 6, "1m ago", 0},
    {"Noor", 9, "4d ago", 1}, {"Priya", 4, "1d ago", 0}, {"Sam", 5, "5d ago", 0},
};
#define NPEOPLE ((int)(sizeof PEOPLE / sizeof PEOPLE[0]))
#define LOADED 7 // Maya

static int color_of(const char *name)
{
    for (int i = 0; i < NPEOPLE; i++)
        if (!strcmp(PEOPLE[i].name, name)) return SWATCH_VIVID[i % 6];
    return SWATCH_VIVID[0];
}

#define PI_F 3.14159265f

// ------------------------------------------------------------------------------------------------ home

static void tile(Cx *c, const Person *p, float x, float y, float w, float h, int sel, int loaded)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float k = sel ? 1.07f : 1.0f;
    float w2 = w * k, h2 = h * k, x2 = x - (w2 - w) / 2, y2 = y - (h2 - h) / 2;
    skin_card(c, x2, y2, w2, h2, 20, sel ? 1.0f : 0.0f);
    float cx = x2 + w2 / 2, ar = 42 * k;
    skin_avatar(c, cx, y2 + 60 * k, ar, p->name, color_of(p->name));
    if (loaded) gfx_ring(c->s, cx, y2 + 60 * k, ar + 6.5f, 3.0f, t->accent);
    char nm[64], dt[64];
    text_fit(u->semi, 20 * k, 0, p->name, w2 - 26, nm, sizeof nm);
    skin_txt(c, u->semi, 20 * k, 0, cx, y2 + 128 * k, 1, t->text, nm);
    if (loaded) snprintf(dt, sizeof dt, "Loaded now");
    else snprintf(dt, sizeof dt, "%d games  \xC2\xB7  %s", p->games, p->ago);
    char fit[64];
    text_fit(u->med, 14.5f * k, 0, dt, w2 - 18, fit, sizeof fit);
    skin_txt(c, loaded ? u->semi : u->med, 14.5f * k, 0, cx, y2 + 150 * k, 1, loaded ? t->accent_text : t->text3, fit);
    if (p->locked) skin_icon_lock(c, x2 + w2 - 22, y2 + 24, 16, t->text3);
    if (sel) skin_focus(c, x2, y2, w2, h2, 20);
}

static void rail(Cx *c, char cur)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    static const char *have = "ABCDEJKMNPS";
    float x = 1226, y0 = 252, step = 13.6f;
    for (int i = 0; i < 26; i++) {
        char l[2] = {(char)('A' + i), 0};
        float cy = y0 + i * step;
        if (l[0] == cur) {
            gfx_circle(c->s, x, cy, 9.5f, t->accent);
            skin_txtc(c, u->semi, 12, 0, x, cy, 1, col(1, 1, 1, 1), l);
        } else {
            skin_txtc(c, u->semi, 12, 0, x, cy, 1, strchr(have, l[0]) ? t->text2 : with_a(t->text3, 0.45f), l);
        }
    }
}

static void people_grid(Cx *c, int sel, int nobody)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    skin_txt(c, u->semi, 22, 0, 40, 238, 0, t->text, "Everyone");
    char buf[32];
    snprintf(buf, sizeof buf, "%d profiles", NPEOPLE);
    skin_txt(c, u->med, 17, 0, 40 + text_width(u->semi, 22, 0, "Everyone") + 14, 238, 0, t->text3, buf);
    float tw = 150, th = 164, gx = 18, gy = 18, x0 = 40, y0 = 262;
    for (int pass = 0; pass < 2; pass++) { // the raised tile is drawn last so it overlaps its neighbours
        for (int i = 0; i < NPEOPLE + 1; i++) {
            float x = x0 + (i % 7) * (tw + gx), y = y0 + (i / 7) * (th + gy);
            int is_sel = i == sel;
            if ((pass == 1) != is_sel) continue;
            if (i == NPEOPLE) {
                skin_card(c, x, y, tw, th, 20, 0);
                gfx_circle(c->s, x + tw / 2, y + 60, 34, t->panel_alt);
                gfx_line(c->s, x + tw / 2 - 12, y + 60, x + tw / 2 + 12, y + 60, 3.2f, t->text2);
                gfx_line(c->s, x + tw / 2, y + 48, x + tw / 2, y + 72, 3.2f, t->text2);
                skin_txt(c, u->semi, 19, 0, x + tw / 2, y + 128, 1, t->text2, "New profile");
                if (is_sel) skin_focus(c, x, y, tw, th, 20);
            } else {
                tile(c, &PEOPLE[i], x, y, tw, th, is_sel, !nobody && i == LOADED);
            }
        }
    }
}

static const Prompt HOME_PROMPTS[] = {{"A", "Choose"}, {"X", "Done playing"}, {"Y", "Search"}, {"+", "New profile"}, {"\xE2\x88\x92", "Admin"}, {"ZL", "About"}};

static void hero(Cx *c, int nobody)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float hx = 40, hy = 76, hw = 1200, hh = 120;
    skin_card(c, hx, hy, hw, hh, 24, 0);
    float ax = hx + 30 + 44, ay = hy + hh / 2;
    float tx = ax + 44 + 28;
    if (nobody) {
        gfx_circle(c->s, ax, ay, 44, t->panel_alt);
        gfx_ring(c->s, ax, ay, 44, 2.0f, with_a(t->text3, 0.6f));
        skin_icon_person(c, ax, ay + 2, 46, with_a(t->text3, 0.95f));
    } else {
        skin_avatar(c, ax, ay, 44, "Maya", color_of("Maya"));
        gfx_ring(c->s, ax, ay, 51, 3.2f, t->accent);
    }
    gfx_circle(c->s, tx + 4, hy + 36, 4.5f, nobody ? t->text3 : t->accent);
    skin_txt(c, u->semi, 13.5f, 1.8f, tx + 16, hy + 41, 0, t->text3, "CURRENTLY LOADED");
    skin_txt(c, u->semi, 42, -0.5f, tx - 1, hy + 85, 0, nobody ? t->text2 : t->text, nobody ? "Nobody" : "Maya");
    skin_txt(c, u->med, 18, 0, tx, hy + 111, 0, t->text2, nobody ? "Pick your name below to load your saves." : "6 games on the console  \xC2\xB7  since 1:48 PM");
    if (!nobody) { // the secondary action lives on the card
        float bw = 244, bh = 58, bx = hx + hw - 30 - bw, by = hy + (hh - bh) / 2;
        gfx_rrect(c->s, bx, by, bw, bh, 16, t->panel_alt);
        gfx_rrect_stroke(c->s, bx + 0.5f, by + 0.5f, bw - 1, bh - 1, 16, 1.0f, with_a(t->line, 0.9f));
        skin_glyph(c, bx + 24, by + bh / 2, "X", t->text);
        skin_txtc(c, u->semi, 21, 0, bx + 24 + 30 + 14, by + bh / 2, 0, t->text, "Done playing");
    }
}

static void toast(Cx *c, const char *msg)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float w = text_width(u->semi, 18.5f, 0, msg) + 78, h = 50, x = (SCREEN_W - w) / 2, y = 20;
    gfx_shadow(c->s, x, y, w, h, h / 2, 5, 12, col(0, 0, 0, t->dark ? 0.5f : 0.22f));
    gfx_rrect(c->s, x, y, w, h, h / 2, t->dark ? hex(0x5A5A5A, 1) : hex(0x333333, 1));
    gfx_circle(c->s, x + 29, y + h / 2, 12, t->accent);
    skin_icon_check(c, x + 29, y + h / 2, 17, 2.6f, col(1, 1, 1, 1));
    skin_txtc(c, u->semi, 18.5f, 0, x + 51, y + h / 2, 0, col(1, 1, 1, 1), msg);
}

static void home(Cx *c, int nobody, int sel, char letter, const char *toast_msg)
{
    skin_background(c);
    skin_top_bar(c, NULL, &MOCK_STATUS);
    hero(c, nobody);
    people_grid(c, sel, nobody);
    rail(c, letter);
    skin_footer(c, HOME_PROMPTS, 6);
    if (toast_msg) skin_toast(c, toast_msg, 0);
}

// ------------------------------------------------------------------------------------------------ dialogs

static void sc_confirm(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    home(c, 0, 9, 'P', NULL);
    skin_dim(c);
    float w = 620, h = 500, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;
    skin_dialog(c, x, y, w, h);
    skin_avatar(c, 640, y + 88, 48, "Priya", color_of("Priya"));
    skin_txt(c, u->semi, 34, -0.3f, 640, y + 182, 1, t->text, "Play as Priya?");
    skin_txt(c, u->med, 20, 0, 640, y + 222, 1, t->text2, "4 games  \xC2\xB7  last played yesterday");
    skin_txt(c, u->reg, 18.5f, 0, 640, y + 270, 1, t->text3, "Maya\xE2\x80\x99s saves are backed up first,");
    skin_txt(c, u->reg, 18.5f, 0, 640, y + 296, 1, t->text3, "then yours are loaded.");
    static const Prompt ch[2] = {{"B", "Cancel"}, {"A", "Switch"}};
    skin_choices(c, x, y + h - 100, w, 100, ch, 2, 1, NULL);
}

static void sc_password(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    home(c, 0, 8, 'N', NULL);
    skin_dim(c);
    float w = 540, h = 608, x = (SCREEN_W - w) / 2, y = 56;
    skin_dialog(c, x, y, w, h);
    skin_avatar(c, 640, y + 70, 36, "Noor", color_of("Noor"));
    skin_txt(c, u->semi, 28, -0.2f, 640, y + 144, 1, t->text, "Noor\xE2\x80\x99s password");
    for (int i = 0; i < 6; i++) {
        float cx = 640 - 2.5f * 38 + i * 38, cy = y + 184;
        if (i < 3) gfx_circle(c->s, cx, cy, 9.5f, t->text);
        else gfx_ring(c->s, cx, cy, 9, 2.4f, with_a(t->text3, 0.9f));
    }
    skin_txtc(c, u->semi, 17, 0, 640, y + 222, 1, t->danger, "Wrong password  \xC2\xB7  3 tries left");
    static const char *keys[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"Delete", "0", "Cancel"}};
    float kw = 128, kh = 64, gap = 12, kx0 = 640 - (3 * kw + 2 * gap) / 2, ky0 = y + 256;
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 3; k++) {
            float kx = kx0 + k * (kw + gap), ky = ky0 + r * (kh + gap);
            int sel = r == 1 && k == 1;
            gfx_rrect(c->s, kx, ky, kw, kh, 14, t->panel_alt);
            gfx_rrect_stroke(c->s, kx + 0.5f, ky + 0.5f, kw - 1, kh - 1, 14, 1.0f, with_a(t->line, 0.9f));
            int small = strlen(keys[r][k]) > 1;
            skin_txtc(c, small ? u->semi : u->med, small ? 19 : 28, 0, kx + kw / 2, ky + kh / 2, 1, small ? t->text2 : t->text, keys[r][k]);
            if (sel) skin_focus(c, kx, ky, kw, kh, 14);
        }
    }
}

static void sc_switching(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    skin_background(c);
    skin_top_bar(c, "Please wait", NULL);
    float w = 880, h = 430, x = (SCREEN_W - w) / 2, y = 112;
    skin_card(c, x, y, w, h, 28, 1.0f);
    // steps
    float sy = y + 58;
    const char *steps[3] = {"Checked", "Backing up Maya", "Loading Priya"};
    float lw[3], tot = 0;
    for (int i = 0; i < 3; i++) { lw[i] = text_width(u->semi, 16.5f, 0, steps[i]); tot += 30 + 10 + lw[i]; }
    float avail = w - 112, link = (avail - tot) / 2, cx0 = x + 56;
    for (int i = 0; i < 3; i++) {
        float cx = cx0 + 15;
        if (i == 0) {
            gfx_circle(c->s, cx, sy, 15, t->accent);
            skin_icon_check(c, cx, sy, 19, 2.8f, col(1, 1, 1, 1));
        } else if (i == 1) {
            gfx_circle(c->s, cx, sy, 15, t->accent_soft);
            gfx_ring(c->s, cx, sy, 14, 2.8f, t->accent);
            skin_txtc(c, u->semi, 15.5f, 0, cx, sy, 1, t->text, "2");
        } else {
            gfx_ring(c->s, cx, sy, 14, 2.2f, with_a(t->text3, 0.7f));
            skin_txtc(c, u->semi, 15.5f, 0, cx, sy, 1, t->text3, "3");
        }
        skin_txtc(c, u->semi, 16.5f, 0, cx0 + 40, sy, 0, i == 1 ? t->text : i == 0 ? t->text2 : t->text3, steps[i]);
        float end = cx0 + 40 + lw[i];
        if (i < 2) gfx_rect(c->s, end + 14, sy - 1, link - 28, 2.4f, i == 0 ? t->accent : with_a(t->line, 1.0f));
        cx0 = end + link;
    }
    skin_txt(c, u->semi, 34, -0.3f, x + 56, y + 150, 0, t->text, "Backing up Maya\xE2\x80\x99s saves");
    // current game
    float gy = y + 192;
    skin_game_monogram(c, x + 56, gy, 88, "Splatoon 3", 2);
    float bx = x + 56 + 88 + 26, bw = w - 112 - 88 - 26, by = gy + 52;
    skin_txt(c, u->semi, 24, 0, bx, gy + 30, 0, t->text, "Splatoon 3");
    gfx_rrect(c->s, bx, by, bw, 14, 7, t->dark ? hex(0x575757, 1) : hex(0xE4E4E4, 1));
    float fw = bw * 0.61f;
    gfx_rrect(c->s, bx, by, fw, 14, 7, t->accent);
    skin_txt(c, u->semi, 16, 0, bx, gy + 100, 0, t->text2, "61%");
    skin_txt(c, u->med, 16, 0, bx + bw, gy + 100, 2, t->text3, "8.4 of 13.8 MB");
    // finished games
    float ly = gy + 140;
    const char *done[2] = {"Mario Kart 8 Deluxe", "Super Smash Bros. Ultimate"};
    const char *sz[2] = {"18.2 MB", "24.0 MB"};
    for (int i = 0; i < 2; i++) {
        float cy = ly + i * 40;
        gfx_circle(c->s, x + 56 + 13, cy, 13, t->accent_soft);
        skin_icon_check(c, x + 56 + 13, cy, 17, 2.5f, t->accent_text);
        skin_txtc(c, u->med, 19, 0, x + 56 + 40, cy, 0, t->text2, done[i]);
        skin_txtc(c, u->med, 16, 0, x + w - 56, cy, 2, t->text3, sz[i]);
    }
    skin_txtc(c, u->med, 16.5f, 0, 640, 600, 1, t->text3, "Keep the console on. Don\xE2\x80\x99t close the app or remove the SD card.");
}

static void sc_ready(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    home(c, 0, 9, 'P', NULL);
    skin_dim(c);
    float w = 660, h = 540, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;
    skin_dialog(c, x, y, w, h);
    float cy = y + 92;
    gfx_circle_grad(c->s, 640, cy, 46, hex(0x19C4F0, 1), hex(0x00A8D8, 1));
    skin_icon_check(c, 640, cy, 54, 6.2f, col(1, 1, 1, 1));
    skin_txt(c, u->semi, 34, -0.3f, 640, y + 202, 1, t->text, "You\xE2\x80\x99re playing as Priya");
    skin_txt(c, u->med, 20, 0, 640, y + 242, 1, t->text2, "Your saves are loaded.");
    float nw = 520, nh = 72, nx = 640 - nw / 2, ny = y + 272;
    gfx_rrect(c->s, nx, ny, nw, nh, 18, t->panel_alt);
    gfx_rrect_stroke(c->s, nx + 0.5f, ny + 0.5f, nw - 1, nh - 1, 18, 1.0f, with_a(t->line, 0.9f));
    skin_icon_info(c, nx + 38, ny + nh / 2, 30, t->text2);
    skin_txt(c, u->med, 17, 0, nx + 72, ny + 31, 0, t->text, "Rocket League isn\xE2\x80\x99t installed, so its save");
    skin_txt(c, u->med, 17, 0, nx + 72, ny + 54, 0, t->text, "was skipped. It stays in your profile.");
    skin_txt(c, u->reg, 18.5f, 0, 640, y + 392, 1, t->text3, "When you\xE2\x80\x99re done, press X so your saves are backed up.");
    static const Prompt ch[1] = {{"A", "OK"}};
    skin_choices(c, x, y + h - 100, w, 100, ch, 1, 0, NULL);
}

static void sc_newprofile(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    skin_background(c);
    skin_top_bar(c, "New profile", NULL);
    skin_txt(c, u->semi, 15, 2.2f, 640, 132, 1, t->text3, "WHAT\xE2\x80\x99S YOUR NAME?");
    skin_txt(c, u->semi, 84, -1.0f, 640, 236, 1, t->text, "Jord");
    float tw = text_width(u->semi, 84, -1.0f, "Jord");
    gfx_rrect(c->s, 640 + tw / 2 + 6, 168, 5, 80, 2.5f, t->accent);
    gfx_rrect(c->s, 640 - 260, 262, 520, 4, 2, t->accent);
    const char *rows[3] = {"QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-."};
    float kw = 84, kh = 64, gap = 9, x0 = 640 - (10 * kw + 9 * gap) / 2, y0 = 318;
    for (int r = 0; r < 3; r++) {
        for (int k = 0; rows[r][k] && k < 10; k++) {
            float x = x0 + k * (kw + gap) + (r == 2 ? (kw + gap) / 2 : 0), y = y0 + r * (kh + gap);
            char l[2] = {rows[r][k], 0};
            skin_key_card(c, x, y, kw, kh, 14);
            skin_txtc(c, u->med, 28, 0, x + kw / 2, y + kh / 2, 1, t->text, l);
            if (l[0] == 'H') skin_focus(c, x, y, kw, kh, 14);
        }
    }
    float fy = y0 + 3 * (kh + gap), fx = x0;
    struct { const char *l; int span; } fn[3] = {{"Shift", 2}, {"space", 6}, {"Delete", 2}};
    for (int i = 0; i < 3; i++) {
        float w = fn[i].span * kw + (fn[i].span - 1) * gap;
        skin_key_card(c, fx, fy, w, kh, 14);
        skin_txtc(c, u->semi, 19, 0, fx + w / 2, fy + kh / 2, 1, t->text2, fn[i].l);
        fx += w + gap;
    }
    static const Prompt pr[] = {{"A", "Type"}, {"X", "Delete"}, {"Y", "Shift"}, {"+", "Done"}, {"B", "Cancel"}};
    skin_footer(c, pr, 5);
}

static void sc_leaving(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    home(c, 0, 7, 'M', NULL);
    skin_dim(c);
    float w = 700, h = 440, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;
    skin_dialog(c, x, y, w, h);
    skin_txt(c, u->semi, 31, -0.3f, 640, y + 76, 1, t->text, "Maya\xE2\x80\x99s saves are still loaded");
    skin_txt(c, u->reg, 19, 0, 640, y + 118, 1, t->text2, "If someone else plays next, their progress");
    skin_txt(c, u->reg, 19, 0, 640, y + 144, 1, t->text2, "could land in Maya\xE2\x80\x99s profile.");
    struct { const char *t1, *t2; float h; } o[3] = {
        {"Done playing, then exit", "Backs up Maya\xE2\x80\x99s saves first. Recommended.", 100},
        {"Exit without signing out", NULL, 76},
        {"Stay", NULL, 76},
    };
    float oy = y + 172;
    for (int i = 0; i < 3; i++) {
        skin_hairline(c, x, oy, w);
        float cy = oy + o[i].h / 2;
        if (o[i].t2) {
            skin_txt(c, u->semi, 23, 0, 640, cy - 2, 1, t->text, o[i].t1);
            skin_txt(c, u->med, 16.5f, 0, 640, cy + 25, 1, t->accent_text, o[i].t2);
        } else {
            skin_txtc(c, u->semi, 23, 0, 640, cy, 1, i == 1 ? t->text2 : t->text, o[i].t1);
        }
        if (i == 0) skin_focus(c, x + 14, oy + 12, w - 28, o[i].h - 24, 16);
        oy += o[i].h;
    }
}

static void group_row(Cx *c, float x, float y, float w, float h, const char *label, const char *right, Col ink, int first, int chev)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    if (!first) gfx_rect(c->s, x + 22, y, w - 44, 2, with_a(t->line, 0.9f));
    skin_txtc(c, u->semi, 20, 0, x + 26, y + h / 2 + 1, 0, ink, label);
    if (right) skin_txtc(c, u->med, 17, 0, x + w - (chev ? 56 : 26), y + h / 2 + 1, 2, t->text3, right);
    if (chev) skin_icon_chev(c, x + w - 30, y + h / 2 + 1, 20, 2.4f, with_a(t->text3, 0.95f));
}

static void sc_admin(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    skin_background(c);
    skin_top_bar(c, "Admin", NULL);
    // left: the list of areas
    float lx = 40, ly = 84, lw = 330, lh = 330;
    skin_card(c, lx, ly, lw, lh, 22, 0);
    const char *items[5] = {"Profiles", "Shared games", "Account", "Activity log", "Admin password"};
    float rh = 62;
    for (int i = 0; i < 5; i++) {
        float y = ly + 14 + i * rh;
        if (i) gfx_rect(c->s, lx + 24, y, lw - 48, 2, with_a(t->line, 0.9f));
        skin_txtc(c, u->semi, 21, 0, lx + 30, y + rh / 2, 0, i == 0 ? t->text : t->text2, items[i]);
        if (i == 0) skin_focus(c, lx + 10, y + 3, lw - 20, rh - 6, 14);
    }
    // right: one profile
    float dx = 394, dy = 84, dw = 846, dh = 548;
    skin_card(c, dx, dy, dw, dh, 24, 0);
    skin_avatar(c, dx + 32 + 40, dy + 28 + 40, 40, "Maya", color_of("Maya"));
    gfx_ring(c->s, dx + 32 + 40, dy + 28 + 40, 46, 3.0f, t->accent);
    skin_txt(c, u->semi, 36, -0.3f, dx + 32 + 80 + 28, dy + 28 + 46, 0, t->text, "Maya");
    skin_txt(c, u->med, 17.5f, 0, dx + 32 + 80 + 28, dy + 28 + 74, 0, t->text2, "6 games  \xC2\xB7  password set  \xC2\xB7  loaded now");
    float gx = dx + 28, gw = dw - 56, gy = dy + 126, grh = 50;
    gfx_rrect(c->s, gx, gy, gw, grh * 4, 16, t->panel_alt);
    const char *acts[4] = {"Rename", "Change password", "Roll back to earlier saves", "Delete profile"};
    for (int i = 0; i < 4; i++) {
        group_row(c, gx, gy + i * grh, gw, grh, acts[i], NULL, i == 3 ? t->danger : t->text, i == 0, i != 3);
        if (i == 2) skin_focus(c, gx + 8, gy + i * grh + 4, gw - 16, grh - 8, 12);
    }
    skin_txt(c, u->semi, 14.5f, 2.0f, dx + 32, gy + grh * 4 + 38, 0, t->text3, "EARLIER SAVES");
    float hy = gy + grh * 4 + 50, hrh = 46;
    gfx_rrect(c->s, gx, hy, gw, hrh * 3, 16, t->panel_alt);
    const char *hl[3] = {"Today, 1:48 PM", "Yesterday, 4:30 PM", "Tuesday, 7:12 PM"};
    const char *hr[3] = {"6 games", "6 games", "5 games"};
    for (int i = 0; i < 3; i++) {
        group_row(c, gx, hy + i * hrh, gw, hrh, hl[i], hr[i], t->text, i == 0, 0);
        if (i == 0) {
            float tx = gx + 26 + text_width(u->semi, 20, 0, hl[i]) + 16;
            gfx_rrect(c->s, tx, hy + hrh / 2 - 12, 74, 24, 12, t->accent_soft);
            skin_txtc(c, u->semi, 12, 1.3f, tx + 37, hy + hrh / 2 + 1, 1, t->accent_text, "LATEST");
        }
    }
    static const Prompt pr[] = {{"A", "Choose"}, {"B", "Back"}};
    skin_footer(c, pr, 2);
}

// ------------------------------------------------------------------------------------------------ entry points

static const char *NAMES[SC_COUNT] = {"Home", "Home, nobody loaded", "Confirm", "Password", "Switching", "Ready", "New profile", "Leaving", "Admin"};
static const char *BLURBS[SC_COUNT] = {
    "Who is on the console, and everyone else, as a grid of tiles. The framed tile is the one A will choose.",
    "What people see most: nothing loaded yet. A dark pill confirms the last sign-out.",
    "A dialog over the dimmed home. It says what will happen before anything does.",
    "Six digits, accepted the moment the sixth is entered. The framed key is the one A will press.",
    "Three steps, one game at a time, with sizes. Nothing to guess at.",
    "Plain confirmation, one thing to remember, and a note for anything that was skipped.",
    "Name entry. Where the system keyboard is available it is used instead.",
    "The nudge to press Done playing, with the safe choice first.",
    "Manage one profile, laid out like the Switch's own settings.",
};

const char *scene_name(SceneId id) { return NAMES[id]; }
const char *scene_blurb(SceneId id) { return BLURBS[id]; }

void scene_render(Skin *ui, SceneId id, Surf *out)
{
    Cx c = {ui, out};
    switch (id) {
    case SC_HOME: home(&c, 0, 9, 'P', NULL); break;
    case SC_HOME_NOBODY: home(&c, 1, 5, 'J', "Maya\xE2\x80\x99s saves are backed up"); break;
    case SC_CONFIRM: sc_confirm(&c); break;
    case SC_PASSWORD: sc_password(&c); break;
    case SC_SWITCHING: sc_switching(&c); break;
    case SC_READY: sc_ready(&c); break;
    case SC_NEWPROFILE: sc_newprofile(&c); break;
    case SC_LEAVING: sc_leaving(&c); break;
    case SC_ADMIN: sc_admin(&c); break;
    default: break;
    }
    gfx_dither(out);
}
