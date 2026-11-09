#include "RemixOcarinaCamera.h"

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

// Where the camera was when the ocarina came out, and whether it is still on its way back there
static bool sActive = false;
static f32 sSavedX = 0.0f;
static f32 sSavedY = 0.0f;
static s16 sScene = -1;
// Free look is a "manual camera": once the right stick moves, the camera leaves the game's own follow camera until
// something resets it. camX/camY only mean something while it is manual.
static bool sWasManual = false;

extern "C" int RemixOcarinaCamera_Begin(PlayState* play, Player* player) {
    if (!CVarGetInteger(CVAR_SETTING("FreeLook.Enabled"), 1) ||
        !CVarGetInteger(CVAR_ENHANCEMENT("RemixOcarinaFreeCamera"), 1)) {
        return 0;
    }
    // Playing for a spot or an actor keeps the default camera
    if (player->unk_6A8 != NULL ||
        (player->stateFlags2 & (PLAYER_STATE2_NEAR_OCARINA_ACTOR | PLAYER_STATE2_PLAY_FOR_ACTOR))) {
        return 0;
    }

    sWasManual = play->manualCamera;
    if (play->manualCamera) {
        sSavedX = play->camX;
        sSavedY = play->camY;
    } else {
        // The game's own camera does not turn back by itself while Link stands still, so remember where it looks
        Camera* camera = Play_GetCamera(play, CAM_ID_MAIN);
        VecSph sph;
        OLib_Vec3fDiffToVecSphGeo(&sph, &camera->at, &camera->eye);
        sSavedX = sph.yaw;
        sSavedY = sph.pitch;
    }
    sScene = play->sceneNum;
    sActive = true;
    return 1;
}

// After the song the camera glides back to where it was, unless the player is already steering it
static void OnFrameUpdate() {
    if (!sActive || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || gPlayState->sceneNum != sScene) {
        sActive = false;
        return;
    }
    if (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) {
        return; // still playing, the camera is free
    }

    Input* input = &gPlayState->state.input[0];
    if (std::abs(input->cur.right_stick_x) > 20 || std::abs(input->cur.right_stick_y) > 20) {
        sActive = false;
        return;
    }

    // The camera was never moved by hand, so there is nothing to bring back
    if (!gPlayState->manualCamera) {
        sActive = false;
        return;
    }

    f32 dx = static_cast<f32>(static_cast<s16>(static_cast<s16>(sSavedX) - static_cast<s16>(gPlayState->camX)));
    f32 dy = sSavedY - gPlayState->camY;
    if (std::fabs(dx) < 256.0f && std::fabs(dy) < 256.0f) {
        gPlayState->camX = sSavedX;
        gPlayState->camY = sSavedY;
        // Back where it was: if it was following Link on its own before, hand it back to the game
        if (!sWasManual) {
            gPlayState->manualCamera = false;
        }
        sActive = false;
        return;
    }
    gPlayState->camX += dx * 0.25f;
    gPlayState->camY += dy * 0.25f;
}

static void RegisterRemixOcarinaCamera() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { sActive = false; });
}

static RegisterShipInitFunc initFunc(RegisterRemixOcarinaCamera);
