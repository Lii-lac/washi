#include "backend_nx.h"

#include "nacp_name.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define MAX_SIZES 1024

typedef struct {
    Backend be; // must stay first
    AccountUid target;
    int have_target;
    NsApplicationControlData *ctrl; // heap: it is ~150 KB
    uint64_t ctrl_title;
    int ctrl_ok;
    size_t ctrl_size;
    struct {
        uint64_t title, size;
    } sizes[MAX_SIZES];
    int nsizes;
} Nx;

static Nx *N(Backend *be)
{
    return (Nx *)be;
}

static int uid_eq(AccountUid a, AccountUid b)
{
    return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1];
}

static int parse_uid(const char *hex, AccountUid *out)
{
    if (strlen(hex) != 32) return -1;
    char hi[17], lo[17];
    memcpy(hi, hex, 16);
    hi[16] = 0;
    memcpy(lo, hex + 16, 16);
    lo[16] = 0;
    out->uid[0] = strtoull(hi, NULL, 16);
    out->uid[1] = strtoull(lo, NULL, 16);
    return 0;
}

static int set_target(Backend *be, const char *hex)
{
    Nx *n = N(be);
    n->have_target = parse_uid(hex, &n->target) == 0;
    return n->have_target ? BE_OK : BE_ERR;
}

static int read_saves(Nx *n, FsSaveDataType type, int match_uid, uint64_t *out, size_t max, size_t *count)
{
    *count = 0;
    FsSaveDataInfoReader it;
    Result rc = fsOpenSaveDataInfoReader(&it, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        n->be.last_rc = rc;
        return BE_ERR;
    }
    if (match_uid) n->nsizes = 0;
    FsSaveDataInfo info[32];
    s64 total = 0;
    while (R_SUCCEEDED(rc = fsSaveDataInfoReaderRead(&it, info, 32, &total)) && total > 0) {
        for (s64 i = 0; i < total; i++) {
            if (info[i].save_data_type != type) continue;
            if (info[i].save_data_index != 0) continue;
            if (match_uid && (!n->have_target || !uid_eq(info[i].uid, n->target))) continue;
            if (info[i].application_id == 0) continue;
            int dup = 0;
            for (size_t k = 0; k < *count; k++)
                if (out[k] == info[i].application_id) dup = 1;
            if (dup || *count >= max) continue;
            out[(*count)++] = info[i].application_id;
            if (match_uid && n->nsizes < MAX_SIZES) {
                n->sizes[n->nsizes].title = info[i].application_id;
                n->sizes[n->nsizes].size = info[i].size;
                n->nsizes++;
            }
        }
    }
    fsSaveDataInfoReaderClose(&it);
    if (R_FAILED(rc)) {
        n->be.last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static int list_saves(Backend *be, uint64_t *out, size_t max, size_t *count)
{
    return read_saves(N(be), FsSaveDataType_Account, 1, out, max, count);
}

static int list_device_saves(Backend *be, uint64_t *out, size_t max, size_t *count)
{
    return read_saves(N(be), FsSaveDataType_Device, 0, out, max, count);
}

static int save_size(Backend *be, uint64_t title, uint64_t *bytes)
{
    Nx *n = N(be);
    for (int i = 0; i < n->nsizes; i++) {
        if (n->sizes[i].title == title) {
            *bytes = n->sizes[i].size;
            return BE_OK;
        }
    }
    return BE_ERR;
}

static int mount_save(Backend *be, uint64_t title)
{
    Nx *n = N(be);
    if (!n->have_target) return BE_ERR;
    FsFileSystem fs;
    Result rc = fsOpen_SaveData(&fs, title, n->target);
    if (R_FAILED(rc)) {
        be->last_rc = rc;
        return BE_ERR_OPEN;
    }
    if (fsdevMountDevice("save", fs) < 0) return BE_ERR_OPEN; // fsdev closes fs on failure
    return BE_OK;
}

static void unmount_save(Backend *be)
{
    (void)be;
    fsdevUnmountDevice("save");
}

static int commit(Backend *be)
{
    Result rc = fsdevCommitDevice("save");
    if (R_FAILED(rc)) {
        be->last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static int write_save(Backend *be, FILE *f, const void *buf, size_t len)
{
    (void)be;
    return fwrite(buf, 1, len, f) == len ? BE_OK : BE_ERR;
}

static int ctrl_load(Nx *n, uint64_t title)
{
    if (n->ctrl_title == title) return n->ctrl_ok;
    u64 actual = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title, n->ctrl, sizeof *n->ctrl, &actual);
    n->ctrl_title = title;
    n->ctrl_ok = R_SUCCEEDED(rc) && actual >= sizeof(NacpStruct);
    n->ctrl_size = (size_t)actual;
    return n->ctrl_ok;
}

// constants are JKSV's (github.com/J-D-K/JKSV, src/fs.cpp createSaveData): thumbnail meta 0x40060, available 0x4000
static int create_save(Backend *be, uint64_t title)
{
    Nx *n = N(be);
    if (!n->have_target) return BE_ERR;
    if (!ctrl_load(n, title)) return BE_ERR_NOT_INSTALLED;
    NacpStruct *nacp = &n->ctrl->nacp;
    if (nacp->user_account_save_data_size <= 0) return BE_ERR_NO_ACCOUNT_SAVE;

    FsSaveDataAttribute attr;
    memset(&attr, 0, sizeof attr);
    attr.application_id = title;
    attr.uid = n->target;
    attr.system_save_data_id = 0;
    attr.save_data_type = FsSaveDataType_Account;
    attr.save_data_rank = 0;
    attr.save_data_index = 0;

    FsSaveDataCreationInfo crt;
    memset(&crt, 0, sizeof crt);
    crt.save_data_size = nacp->user_account_save_data_size;
    crt.journal_size = nacp->user_account_save_data_journal_size;
    crt.available_size = 0x4000;
    crt.owner_id = nacp->save_data_owner_id;
    crt.flags = 0;
    crt.save_data_space_id = FsSaveDataSpaceId_User;

    FsSaveDataMetaInfo meta;
    memset(&meta, 0, sizeof meta);
    meta.size = 0x40060;
    meta.type = FsSaveDataMetaType_Thumbnail;

    Result rc = fsCreateSaveDataFileSystem(&attr, &crt, &meta);
    if (R_FAILED(rc)) {
        be->last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static uint64_t journal_size(Backend *be, uint64_t title)
{
    Nx *n = N(be);
    if (!ctrl_load(n, title)) return 0;
    return (uint64_t)n->ctrl->nacp.user_account_save_data_journal_size;
}

static int title_name(Backend *be, uint64_t title, char *out, size_t cap)
{
    Nx *n = N(be);
    if (!ctrl_load(n, title)) return BE_ERR;
    // newer games keep their names compressed, which this libnx doesn't know about: read those ourselves
    if (nacp_names_compressed((const uint8_t *)&n->ctrl->nacp)) return nacp_name((const uint8_t *)&n->ctrl->nacp, out, cap) == 0 ? BE_OK : BE_ERR;
    NacpLanguageEntry *entry = NULL;
    if (R_FAILED(nacpGetLanguageEntry(&n->ctrl->nacp, &entry)) || !entry || !entry->name[0]) return BE_ERR;
    snprintf(out, cap, "%s", entry->name);
    return BE_OK;
}

// the icon sits after the NACP in the control data, as a JPEG
static int title_icon(Backend *be, uint64_t title, const uint8_t **jpeg, size_t *size)
{
    Nx *n = N(be);
    if (!ctrl_load(n, title) || n->ctrl_size <= sizeof(NacpStruct)) return BE_ERR;
    *jpeg = n->ctrl->icon;
    *size = n->ctrl_size - sizeof(NacpStruct);
    if (*size > sizeof n->ctrl->icon) *size = sizeof n->ctrl->icon;
    return BE_OK;
}

static int list_accounts(Backend *be, BeAccount *out, size_t max, size_t *count)
{
    (void)be;
    *count = 0;
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 total = 0;
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &total))) return BE_ERR;
    for (s32 i = 0; i < total && *count < max; i++) {
        BeAccount *a = &out[*count];
        memset(a, 0, sizeof *a);
        snprintf(a->uid, sizeof a->uid, "%016" PRIx64 "%016" PRIx64, (uint64_t)uids[i].uid[0], (uint64_t)uids[i].uid[1]);
        AccountProfile prof;
        AccountProfileBase base;
        memset(&base, 0, sizeof base);
        if (R_SUCCEEDED(accountGetProfile(&prof, uids[i]))) {
            if (R_SUCCEEDED(accountProfileGet(&prof, NULL, &base))) snprintf(a->name, sizeof a->name, "%s", base.nickname);
            accountProfileClose(&prof);
        }
        if (!a->name[0]) snprintf(a->name, sizeof a->name, "(unnamed)");
        (*count)++;
    }
    return BE_OK;
}

// The console's own profile for the account, through the profile editor of the admin account service (acc:su, command 205).
// Reading and writing go through the same object, and a write passes back the user data that was read, so only the nickname
// (and the picture, if one is given) changes.
static int open_editor(Nx *n, AccountProfile *ed)
{
    if (!n->have_target) return BE_ERR;
    Result rc = serviceDispatchIn(accountGetServiceSession(), 205, n->target, .out_num_objects = 1, .out_objects = &ed->s);
    if (R_FAILED(rc)) {
        n->be.last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static int sys_profile_read(Backend *be, char nick[40], uint8_t *user_data, uint8_t *img, size_t cap, size_t *img_size)
{
    Nx *n = N(be);
    AccountProfile ed;
    if (open_editor(n, &ed) != BE_OK) return BE_ERR;
    AccountProfileBase base;
    AccountUserData ud;
    memset(&base, 0, sizeof base);
    Result rc = accountProfileGet(&ed, &ud, &base);
    if (R_SUCCEEDED(rc)) {
        memset(nick, 0, 40);
        memcpy(nick, base.nickname, sizeof base.nickname);
        if (user_data) memcpy(user_data, &ud, sizeof ud);
        if (img) {
            u32 sz = 0;
            rc = accountProfileGetImageSize(&ed, &sz);
            if (R_SUCCEEDED(rc) && (sz == 0 || sz > cap)) rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (R_SUCCEEDED(rc)) rc = accountProfileLoadImage(&ed, img, cap, &sz);
            if (R_SUCCEEDED(rc)) *img_size = sz;
        }
    }
    accountProfileClose(&ed);
    if (R_FAILED(rc)) {
        n->be.last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static int sys_profile_store(Backend *be, const char *nick, const uint8_t *user_data, const uint8_t *jpeg, size_t len)
{
    Nx *n = N(be);
    AccountProfile ed;
    if (open_editor(n, &ed) != BE_OK) return BE_ERR;
    AccountProfileBase base;
    AccountUserData ud;
    memset(&base, 0, sizeof base);
    Result rc = accountProfileGet(&ed, &ud, &base);
    if (R_SUCCEEDED(rc)) {
        if (user_data) memcpy(&ud, user_data, sizeof ud);
        memset(base.nickname, 0, sizeof base.nickname);
        strncpy(base.nickname, nick, sizeof base.nickname - 1);
        // always later than the last edit, even if the clock is behind: the console tells a changed profile by this
        u64 stamp = (u64)time(NULL);
        base.lastEditTimestamp = stamp > base.lastEditTimestamp ? stamp : base.lastEditTimestamp + 1;
        if (jpeg && len > 0) {
            rc = serviceDispatchIn(&ed.s, 101, base,
                .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In | SfBufferAttr_FixedSize, SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
                .buffers = { { &ud, sizeof ud }, { jpeg, len } },
            );
        } else {
            rc = serviceDispatchIn(&ed.s, 100, base,
                .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_In | SfBufferAttr_FixedSize },
                .buffers = { { &ud, sizeof ud } },
            );
        }
    }
    accountProfileClose(&ed);
    if (R_FAILED(rc)) {
        n->be.last_rc = rc;
        return BE_ERR;
    }
    return BE_OK;
}

static int battery(Backend *be, int *percent, int *charging)
{
    (void)be;
    u32 pct = 0;
    PsmChargerType type = PsmChargerType_Unconnected;
    if (R_FAILED(psmGetBatteryChargePercentage(&pct)) || R_FAILED(psmGetChargerType(&type))) return BE_ERR;
    *percent = (int)pct;
    *charging = type != PsmChargerType_Unconnected;
    return BE_OK;
}

static int free_space(Backend *be, uint64_t *bytes)
{
    (void)be;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    s64 free_bytes = 0;
    if (!sd || R_FAILED(fsFsGetFreeSpace(sd, "/", &free_bytes))) return BE_ERR;
    *bytes = (uint64_t)free_bytes;
    return BE_OK;
}

static int total_space(Backend *be, uint64_t *bytes)
{
    (void)be;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    s64 total_bytes = 0;
    if (!sd || R_FAILED(fsFsGetTotalSpace(sd, "/", &total_bytes))) return BE_ERR;
    *bytes = (uint64_t)total_bytes;
    return BE_OK;
}

static time_t now(Backend *be)
{
    (void)be;
    return time(NULL);
}

static uint32_t rnd(Backend *be)
{
    (void)be;
    uint32_t v = 0;
    randomGet(&v, sizeof v);
    return v;
}

static int checkpoint(Backend *be, const char *name)
{
    (void)be;
    (void)name;
    return 0;
}

Backend *backend_nx_init(const char **why)
{
    static const char *dummy;
    if (!why) why = &dummy;
    if (R_FAILED(accountInitialize(AccountServiceType_Administrator))) {
        *why = "Couldn't start the account service.";
        return NULL;
    }
    if (R_FAILED(nsInitialize())) {
        *why = "Couldn't start the application-info service.";
        accountExit();
        return NULL;
    }
    if (R_FAILED(psmInitialize())) {
        *why = "Couldn't start the power service.";
        nsExit();
        accountExit();
        return NULL;
    }
    Nx *n = calloc(1, sizeof *n);
    if (n) n->ctrl = malloc(sizeof *n->ctrl);
    if (!n || !n->ctrl) {
        *why = "Out of memory.";
        free(n);
        psmExit();
        nsExit();
        accountExit();
        return NULL;
    }
    Backend *b = &n->be;
    b->ctx = n;
    b->save_root = "save:/";
    b->set_target = set_target;
    b->list_saves = list_saves;
    b->list_device_saves = list_device_saves;
    b->save_size = save_size;
    b->mount = mount_save;
    b->create = create_save;
    b->commit = commit;
    b->unmount = unmount_save;
    b->journal_size = journal_size;
    b->write = write_save;
    b->title_name = title_name;
    b->title_icon = title_icon;
    b->sys_profile_read = sys_profile_read;
    b->sys_profile_store = sys_profile_store;
    b->list_accounts = list_accounts;
    b->battery = battery;
    b->free_space = free_space;
    b->total_space = total_space;
    b->now = now;
    b->random = rnd;
    b->checkpoint = checkpoint;
    return b;
}

void backend_nx_exit(Backend *be)
{
    if (!be) return;
    Nx *n = N(be);
    fsdevUnmountDevice("save");
    free(n->ctrl);
    free(n);
    psmExit();
    nsExit();
    accountExit();
}
