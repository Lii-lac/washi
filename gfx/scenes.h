// The Washi screens, drawn with gfx.c. Used by the Switch preview app and by the PC screenshot tool.
//
// Style: after the Switch's own system UI. Flat white cards on a light grey ground (or charcoal in the dark theme), a bright
// cyan focus frame, round button glyphs along a footer rule, centred dialogs with hairline-separated rows, and Joy-Con blue
// and red as the only accents. Rounded type (M PLUS Rounded 1c).
#ifndef SCENES_H
#define SCENES_H

#include "skin.h"

typedef enum {
    SC_HOME, SC_HOME_NOBODY, SC_CONFIRM, SC_PASSWORD, SC_SWITCHING, SC_READY, SC_NEWPROFILE, SC_LEAVING, SC_ADMIN, SC_COUNT
} SceneId;

const char *scene_name(SceneId id);
const char *scene_blurb(SceneId id);
void scene_render(Skin *skin, SceneId id, Surf *out);           // draws a full 1280x720 screen into out

#endif
