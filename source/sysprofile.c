#include "sysprofile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "fsx.h"

static void seterr(char *err, size_t en, const char *fmt, const char *a)
{
    if (err && en) snprintf(err, en, fmt, a ? a : "");
}

void sp_clean_nick(const char *in, char out[40])
{
    size_t i = 0, n = 0;
    while (in[i] && n < SP_NICK_BYTES) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x20) { // no control characters in a name
            i++;
            continue;
        }
        size_t len = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (n + len > SP_NICK_BYTES) break;
        size_t k = 0;
        while (k < len && in[i + k]) k++;
        if (k < len) break; // a character cut short
        memcpy(out + n, in + i, len);
        n += len;
        i += len;
    }
    out[n] = 0;
}

static void orig_paths(Store *st, char *dir, char *json, char *jpg, size_t cap)
{
    store_path(st, dir, cap, "system-profile");
    fsx_join(json, cap, dir, "original.json");
    fsx_join(jpg, cap, dir, "original.jpg");
}

int sp_has_original(Store *st)
{
    char dir[FSX_PATH], json[FSX_PATH], jpg[FSX_PATH];
    orig_paths(st, dir, json, jpg, FSX_PATH);
    return fsx_exists(json);
}

int sp_probe(Backend *be, char nick[40], int *icon_id, int *bg_id, char *err, size_t en)
{
    if (!be->sys_profile_read) {
        seterr(err, en, "%s", "Not available in this build.");
        return SP_ERR;
    }
    uint8_t ud[SP_UD_SIZE];
    if (be->sys_profile_read(be, nick, ud, NULL, 0, NULL) != BE_OK) {
        if (err && en) snprintf(err, en, "The console didn't allow it (error %08X).", (unsigned)be->last_rc);
        return SP_ERR;
    }
    if (icon_id) *icon_id = ud[SP_UD_ICON] | ud[SP_UD_ICON + 1] << 8 | ud[SP_UD_ICON + 2] << 16 | (int)ud[SP_UD_ICON + 3] << 24;
    if (bg_id) *bg_id = ud[SP_UD_BG];
    return SP_OK;
}

// Saves the console's current name and picture, once. The picture goes first and the .json is the "it's complete" marker.
static int save_original(Store *st, Backend *be, int need_image, char *err, size_t en)
{
    char dir[FSX_PATH], json[FSX_PATH], jpg[FSX_PATH];
    orig_paths(st, dir, json, jpg, FSX_PATH);
    if (fsx_exists(json)) return SP_OK;
    uint8_t *img = malloc(SP_IMG_MAX);
    if (!img) {
        seterr(err, en, "%s", "Out of memory.");
        return SP_ERR;
    }
    char nick[40];
    uint8_t ud[SP_UD_SIZE];
    size_t isz = 0;
    int have_img = 1;
    if (be->sys_profile_read(be, nick, ud, img, SP_IMG_MAX, &isz) != BE_OK) {
        have_img = 0; // maybe only the picture is the problem: try the name alone
        isz = 0;
        if (be->sys_profile_read(be, nick, ud, NULL, 0, NULL) != BE_OK) {
            free(img);
            if (err && en) snprintf(err, en, "Couldn't read the profile (error %08X). Nothing was changed.", (unsigned)be->last_rc);
            return SP_ERR;
        }
    }
    if (!have_img && need_image) {
        free(img);
        seterr(err, en, "%s", "Couldn't save the current picture; nothing was changed.");
        return SP_ERR;
    }
    int rc = SP_OK;
    if (fsx_mkdir_p(dir) != 0) rc = SP_ERR;
    if (rc == SP_OK && have_img && fsx_write_file(jpg, img, isz) != 0) rc = SP_ERR;
    if (rc == SP_OK) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "nickname", nick);
        cJSON_AddNumberToObject(j, "has_image", have_img);
        char hex[2 * SP_UD_SIZE + 1];
        for (int i = 0; i < SP_UD_SIZE; i++) snprintf(hex + 2 * i, 3, "%02x", ud[i]);
        cJSON_AddStringToObject(j, "user_data", hex);
        char *text = cJSON_Print(j);
        cJSON_Delete(j);
        if (!text || fsx_write_atomic(json, text, strlen(text), NULL, NULL) != 0) rc = SP_ERR;
        free(text);
    }
    free(img);
    if (rc != SP_OK) seterr(err, en, "%s", "Couldn't save the original; nothing was changed.");
    return rc;
}

int sp_apply(Store *st, Backend *be, const char *nick, const uint8_t *jpeg, size_t n, const SpIcon *icon, char *err, size_t en)
{
    if (!be->sys_profile_read || !be->sys_profile_store) {
        seterr(err, en, "%s", "Not available in this build.");
        return SP_ERR;
    }
    if (jpeg && (n == 0 || n > SP_IMG_MAX)) {
        seterr(err, en, "%s", "The picture is too big.");
        return SP_ERR;
    }
    int rc = save_original(st, be, jpeg != NULL, err, en);
    if (rc != SP_OK) return rc;
    char cur[40], clean[40];
    uint8_t ud[SP_UD_SIZE];
    if (be->sys_profile_read(be, cur, ud, NULL, 0, NULL) != BE_OK) {
        seterr(err, en, "%s", "Couldn't read the profile.");
        return SP_ERR;
    }
    if (nick) {
        sp_clean_nick(nick, clean);
        if (!clean[0]) snprintf(clean, sizeof clean, "Player");
    } else {
        snprintf(clean, sizeof clean, "%s", cur);
    }
    if (icon) { // the console keeps which character icon and background the picture is made from beside the picture itself
        for (int i = 0; i < 4; i++) ud[SP_UD_ICON + i] = (uint8_t)((unsigned)icon->icon_id >> (8 * i));
        ud[SP_UD_BG] = (uint8_t)icon->bg_id;
        memset(ud + SP_UD_MII, 0, 0x10); // (also for id 0, an Extra icon: no character and no Mii, so only the picture is left to show)
    }
    if (be->sys_profile_store(be, clean, icon ? ud : NULL, jpeg, n) != BE_OK) {
        if (err && en) snprintf(err, en, "The console didn't accept the change (error %08X).", (unsigned)be->last_rc);
        return SP_ERR;
    }
    return SP_OK;
}

int sp_original_nick(Store *st, char out[40])
{
    char dir[FSX_PATH], json[FSX_PATH], jpg[FSX_PATH];
    orig_paths(st, dir, json, jpg, FSX_PATH);
    char *text = NULL;
    size_t len = 0;
    if (!fsx_exists(json) || fsx_read_all(json, &text, &len) != 0) return 0;
    cJSON *j = cJSON_Parse(text);
    free(text);
    const cJSON *nk = j ? cJSON_GetObjectItemCaseSensitive(j, "nickname") : NULL;
    int ok = cJSON_IsString(nk);
    if (ok) snprintf(out, 40, "%.31s", nk->valuestring);
    cJSON_Delete(j);
    return ok;
}

int sp_restore(Store *st, Backend *be, char *err, size_t en)
{
    char dir[FSX_PATH], json[FSX_PATH], jpg[FSX_PATH];
    orig_paths(st, dir, json, jpg, FSX_PATH);
    if (!fsx_exists(json)) return SP_NOTHING;
    if (!be->sys_profile_store) {
        seterr(err, en, "%s", "Not available in this build.");
        return SP_ERR;
    }
    char *text = NULL;
    size_t len = 0;
    if (fsx_read_all(json, &text, &len) != 0) {
        seterr(err, en, "%s", "Couldn't read the saved original.");
        return SP_ERR;
    }
    cJSON *j = cJSON_Parse(text);
    free(text);
    const cJSON *nk = j ? cJSON_GetObjectItemCaseSensitive(j, "nickname") : NULL;
    const cJSON *hi = j ? cJSON_GetObjectItemCaseSensitive(j, "has_image") : NULL;
    if (!cJSON_IsString(nk)) {
        cJSON_Delete(j);
        seterr(err, en, "%s", "The saved original is damaged.");
        return SP_ERR;
    }
    char nick[40];
    snprintf(nick, sizeof nick, "%.31s", nk->valuestring);
    int has_image = cJSON_IsNumber(hi) && hi->valuedouble != 0;
    uint8_t ud[SP_UD_SIZE];
    int have_ud = 0;
    const cJSON *uj = cJSON_GetObjectItemCaseSensitive(j, "user_data");
    if (cJSON_IsString(uj) && strlen(uj->valuestring) == 2 * SP_UD_SIZE) {
        have_ud = 1;
        for (int i = 0; i < SP_UD_SIZE; i++) {
            unsigned v = 0;
            if (sscanf(uj->valuestring + 2 * i, "%2x", &v) != 1) have_ud = 0;
            ud[i] = (uint8_t)v;
        }
    }
    cJSON_Delete(j);
    char *img = NULL;
    size_t isz = 0;
    if (has_image && (fsx_read_all(jpg, &img, &isz) != 0 || isz == 0 || isz > SP_IMG_MAX)) {
        free(img);
        seterr(err, en, "%s", "Couldn't read the saved picture.");
        return SP_ERR;
    }
    int rc = be->sys_profile_store(be, nick, have_ud ? ud : NULL, (const uint8_t *)img, isz);
    free(img);
    if (rc != BE_OK) {
        if (err && en) snprintf(err, en, "The console didn't accept the change (error %08X).", (unsigned)be->last_rc);
        return SP_ERR;
    }
    return SP_OK;
}
