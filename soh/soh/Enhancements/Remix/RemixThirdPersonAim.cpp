#include "RemixThirdPersonAim.h"

#include <cmath>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Frame in which the game last asked for the aiming camera and we answered with the third person one
static int sLastRequestFrame = -100;
static bool sActive = false;
static bool sWasManual = false;

static bool HoldsAimItem(Player* player) {
    switch (player->heldItemAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
        case PLAYER_IA_SLINGSHOT:
        case PLAYER_IA_HOOKSHOT:
        case PLAYER_IA_LONGSHOT:
            return true;
        default:
            return false;
    }
}

extern "C" int RemixThirdPersonAim_OverrideCamMode(Player* player, int* camMode) {
    if (gPlayState == nullptr || !CVarGetInteger(CVAR_ENHANCEMENT("RemixThirdPersonAim"), 1) ||
        !CVarGetInteger(CVAR_SETTING("FreeLook.Enabled"), 1)) {
        return 0;
    }
    // Riding keeps the original aim, and so does anything that is not the player's own aiming
    if ((player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER)) || !HoldsAimItem(player)) {
        return 0;
    }

    *camMode = CAM_MODE_NORMAL;
    sLastRequestFrame = gPlayState->state.frames;
    return 1;
}

extern "C" int RemixThirdPersonAim_IsActive(void) {
    return sActive ? 1 : 0;
}

static s16 StepAngle(s16 current, s16 target, f32 rate) {
    s16 diff = static_cast<s16>(target - current);
    return static_cast<s16>(current + static_cast<s16>(diff * rate));
}

static void OnFrameUpdate() {
    if (gPlayState == nullptr) {
        sActive = false;
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    bool aiming = player != nullptr && (gPlayState->state.frames - sLastRequestFrame) <= 2 &&
                  (player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) && player->unk_6AD == 2;

    if (!aiming) {
        if (sActive) {
            sActive = false;
            // Aiming is over: if the camera was following Link on its own before, hand it back to the game
            if (!sWasManual) {
                gPlayState->manualCamera = false;
            }
        }
        return;
    }

    if (!sActive) {
        sActive = true;
        sWasManual = gPlayState->manualCamera;
        if (!gPlayState->manualCamera) {
            Camera* camera = Play_GetCamera(gPlayState, CAM_ID_MAIN);
            VecSph sph;
            OLib_Vec3fDiffToVecSphGeo(&sph, &camera->at, &camera->eye);
            gPlayState->manualCamera = true;
            gPlayState->camX = sph.yaw;
            gPlayState->camY = sph.pitch;
        }
    }

    // The camera sits behind where Link aims. Aiming down means the camera is above, so the pitch matches.
    s16 targetYaw = static_cast<s16>(player->actor.focus.rot.y + 0x8000);
    f32 targetPitch = static_cast<f32>(CLAMP(player->actor.focus.rot.x, -0x1800, 0x3000));

    gPlayState->camX = StepAngle(static_cast<s16>(gPlayState->camX), targetYaw, 0.4f);
    gPlayState->camY += (targetPitch - gPlayState->camY) * 0.4f;
}

static void RegisterRemixThirdPersonAim() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sActive = false; });
}

static RegisterShipInitFunc initFunc(RegisterRemixThirdPersonAim);
