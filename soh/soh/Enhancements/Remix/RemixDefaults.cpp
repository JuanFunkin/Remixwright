#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

// Remix of Harkinian: settings that start different from Ship of Harkinian, to match the remake.
//
// They are written once, the first time the game runs with a new version of this list (bump the flag name when a
// default is added), so anything the player changes afterwards in the menus is kept.
static void ApplyRemixDefaults() {
    const char* flag = CVAR_GENERAL("RemixDefaultsV1");
    const char* flag2 = CVAR_GENERAL("RemixDefaultsV2");

    // V2: aiming from behind Link, walking while aiming, and the right stick aims
    if (!CVarGetInteger(flag2, 0)) {
        CVarSetInteger(CVAR_SETTING("MoveInFirstPerson"), 1);
        CVarSetInteger(CVAR_SETTING("Controls.RightStickAim"), 1);
        CVarSetInteger(CVAR_ENHANCEMENT("RemixThirdPersonAim"), 1);
        CVarSetInteger(flag2, 1);
        CVarSave();
    }

    // V4: the ocarina keeps its original buttons (A and the C buttons) until the new ones have their textures
    const char* flag4 = CVAR_GENERAL("RemixDefaultsV4");
    if (!CVarGetInteger(flag4, 0)) {
        CVarSetInteger(CVAR_SETTING("CustomOcarina.Enabled"), 0);
        CVarSetInteger(flag4, 1);
        CVarSave();
    }

    // V5: D-pad item buttons (all four directions are chosen in the item selector)
    const char* flag5 = CVAR_GENERAL("RemixDefaultsV5");
    if (!CVarGetInteger(flag5, 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), 1);
        CVarSetInteger(CVAR_ENHANCEMENT("RemixItemSelector"), 1);
        CVarSetInteger(flag5, 1);
        CVarSave();
    }

    // V6: the D-pad item buttons sit at the left of the screen (the right is for the item button)
    const char* flag6 = CVAR_GENERAL("RemixDefaultsV6");
    if (!CVarGetInteger(flag6, 0)) {
        CVarSetInteger(CVAR_COSMETIC("HUD.Dpad.PosType"), 1); // ANCHOR_LEFT
        CVarSetInteger(CVAR_COSMETIC("HUD.Dpad.PosX"), 24);
        CVarSetInteger(CVAR_COSMETIC("HUD.Dpad.PosY"), 160);
        CVarSetInteger(flag6, 1);
        CVarSave();
    }

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
