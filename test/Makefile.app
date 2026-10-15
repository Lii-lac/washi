# End-to-end tests of the whole app on a PC (screens, flows, fake console, scripted buttons; screenshots in out/app/).
#   make -f Makefile.app          run everything
#   ./app_host admin              run one scenario
CC ?= gcc
CFLAGS ?= -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer
S = ../source
GFX = ../source/gfx
# the version the console build gets (About and the admin page show it)
DEFS = -DAPP_VERSION=\"$(shell sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' ../Makefile)\"
SRC = $(S)/app.c $(S)/ui_core.c $(S)/ui_home.c $(S)/ui_dialogs.c $(S)/ui_scene.c $(S)/ui_list.c $(S)/ui_progress.c $(S)/ui_picker.c $(S)/ui_about.c $(S)/ui_update.c $(S)/ui_hub.c $(S)/ui_wheel.c $(S)/ui_storage.c $(S)/ui_backups.c $(S)/ui_spr.c $(S)/ui_extra.c $(S)/ui_thumbs.c $(S)/ui_motion.c $(S)/ui_fx.c $(S)/ui_stickfx.c $(S)/ui_wait.c $(S)/ui_intro.c $(S)/dirty.c      $(GFX)/gfx.c $(GFX)/paper.c $(GFX)/crayon.c $(GFX)/paper_roll.c $(GFX)/wall.c $(GFX)/skin.c $(GFX)/avatar_src.c $(GFX)/fontdata.c $(GFX)/gfx_image.c $(GFX)/tint.c $(GFX)/pins.c       $(S)/nacp_name.c $(S)/sysprofile.c $(S)/locker.c $(S)/store.c $(S)/custom_pics.c $(S)/icons.c $(S)/ui_icons.c $(S)/fsx.c $(S)/hash.c $(S)/cJSON.c       backend_host.c platform_host.c icon_sample.c app_host.c

test: app_host
	ASAN_OPTIONS=detect_leaks=0 ./app_host

app_host: $(SRC) $(wildcard $(S)/*.h) $(wildcard $(GFX)/*.h) backend_host.h platform_host.h
	$(CC) $(CFLAGS) $(DEFS) -I$(S) -I$(GFX) -o $@ $(SRC) -lm

# how long frames take to draw, without the sanitizers (what is slow on the console is slow here too, just less so)
bench: $(SRC) $(wildcard $(S)/*.h) $(wildcard $(GFX)/*.h) backend_host.h platform_host.h
	$(CC) -std=gnu11 -O3 $(DEFS) -I$(S) -I$(GFX) -o app_bench $(SRC) -lm
	BENCH=1 ./app_bench

# where the time goes (gprof), for one scenario: make -f Makefile.app prof SC=hub
prof: $(SRC) $(wildcard $(S)/*.h) $(wildcard $(GFX)/*.h) backend_host.h platform_host.h
	$(CC) -std=gnu11 -O2 -g -pg $(DEFS) -I$(S) -I$(GFX) -o app_prof $(SRC) -lm
	NOPNG=1 ./app_prof $(SC) > /dev/null
	gprof -b -p ./app_prof gmon.out | head -24
	rm -f gmon.out app_prof

clean:
	rm -rf app_host app_bench app_prof gmon.out out/app
