#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

// Remix of Harkinian: settings that start different from Ship of Harkinian, to match the remake.
//
// They are written once, the first time the game runs with a new version of this list (bump the flag name when a
// default is added), so anything the player changes afterwards in the menus is kept.
static void ApplyRemixDefaults() {
    const char* flag = CVAR_GENERAL("RemixDefaultsV1");
    if (CVarGetInteger(flag, 0)) {
        return;
    }

    // 60 FPS instead of the original 20
    CVarSetInteger(CVAR_SETTING("InterpolationFPS"), 60);
    // Text as fast as it goes (top notch of the slider)
    CVarSetInteger(CVAR_ENHANCEMENT("TextSpeed"), 6);
    CVarSetInteger(CVAR_ENHANCEMENT("FasterPauseMenu"), 1);
    CVarSetInteger(CVAR_ENHANCEMENT("NewDrops"), 1);
    // Free look camera a little quicker than the stock sensitivity
    CVarSetFloat(CVAR_SETTING("FreeLook.CameraSensitivity.X"), 1.6f);
    CVarSetFloat(CVAR_SETTING("FreeLook.CameraSensitivity.Y"), 1.6f);

    CVarSetInteger(flag, 1);
    CVarSave();
}

static RegisterShipInitFunc initFunc(ApplyRemixDefaults);
