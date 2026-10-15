// Washi for the Switch (libnx). Everything interesting is in app.c; this only wires the console to it.
#include <stdio.h>
#include <switch.h>

#include "app.h"
#ifdef WASHI_AVATARS
#include "avatars_bin.h" // the profile pictures (data/avatars.bin), made into an object by the build; the Makefile defines this only when the file is there
#endif
#include "backend_nx.h"
#include "intro_card_a4_png.h" // the intro's cards (data/intro_card_*.png, printed by the intro mockup page): the postcard,
#include "intro_card_d2_png.h" // the torn patterned washi,
#include "intro_card_f1_png.h" // the index card with pen doodles,
#include "intro_card_f2_png.h" // and the index card with the app's crayon doodles
#include "intro_logo_bin.h"    // and the logo it makes on them, made beforehand (data/intro_logo.bin, made by the PC test)
#include "pins.h"
#include "pins_bin.h" // the push pins of pinned profiles, rendered beforehand (data/pins.bin, made on a PC by tools/pins)
#include "platform_nx.h"
#include "qr_bin.h" // the club's QR code (data/qr.bin)
#include "qr_wax_bin.h" // and that code coloured in with wax crayon beforehand (data/qr_wax.bin, made by the PC test)
#include "ui.h"

#define APP_ROOT "sdmc:/switch/washi"

int main(int argc, char **argv)
{
    // before anything else (the least that could go wrong): a new version that never got going twice is swapped back out, and the kept one opens next
    if (net_nx_boot_check(argc > 0 ? argv[0] : NULL)) return 0;
#ifdef WASHI_AVATARS
    ui_set_avatar_pack(avatars_bin, avatars_bin_size); // (a packed file made by hand: used only if the console's own pictures can't be had)
#endif
    // The console's own profile pictures, read from its system data a picture at a time: always the ones its firmware has, and none of
    // Nintendo's artwork in this repository. If the console says no, or has none, it stays a packed file (above) or letters.
    {
        Result prc = romfsMountFromDataArchive(0x010000000000080AULL, NcmStorageId_BuiltInSystem, "avatar");
        char note[96] = "";
        if (R_FAILED(prc)) {
            snprintf(note, sizeof note, "the console's picture data wouldn't open (error 0x%X)", (unsigned)prc);
        } else if (ui_set_avatar_dir("avatar:/") == 0) {
            snprintf(note, sizeof note, "the console's picture data has no pictures");
            romfsUnmount("avatar");
        }
        ui_set_avatar_note(note);
    }
    ui_set_qr(qr_bin, qr_bin_size);
    ui_set_qr_wax(qr_wax_bin, qr_wax_bin_size);
    ui_intro_add_card(intro_card_a4_png, intro_card_a4_png_size, -42, -46); // (the logo's place on each: px from its middle; a start picks one)
    ui_intro_add_card(intro_card_d2_png, intro_card_d2_png_size, 0, -50);
    ui_intro_add_card(intro_card_f1_png, intro_card_f1_png_size, 0, -36);
    ui_intro_add_card(intro_card_f2_png, intro_card_f2_png_size, 0, -36);
    ui_intro_set_logo(intro_logo_bin, intro_logo_bin_size);
    pins_set_blob(pins_bin, pins_bin_size);
    ui_intro_prepare(); // (its strips are made on the other cores from now on, while the console's services start and the app reads the SD card)
    Platform *plat = platform_nx_init();
    if (!plat) {
        ui_intro_drop();
        return 1; // not even a screen to explain on
    }
    net_nx_attach(plat, argc > 0 ? argv[0] : NULL); // (Check for updates: this NRO's own path, to put a new one in its place)

    const char *why = NULL;
    Backend *be = backend_nx_init(&why);
    if (!be) {
        app_fatal(plat, "Couldn\xE2\x80\x99t start", why ? why : "A system service isn\xE2\x80\x99t available.");
        platform_nx_exit(plat);
        return 1;
    }

    int rc = app_run(plat, be, APP_ROOT);

    backend_nx_exit(be);
    net_nx_exit();
    platform_nx_exit(plat);
    return rc;
}
